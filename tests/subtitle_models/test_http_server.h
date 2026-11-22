#pragma once
// 工作包 E 测试专用 HTTP 服务器：QTcpServer 监听 127.0.0.1，支持 200/206/404/断流/
// 篡改内容/忽略 Range/挂起等行为注入。绝不联网（只接受回环连接）。

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QVector>

#include <algorithm>

class TestHttpServer : public QObject {
    Q_OBJECT
public:
    enum class Behavior {
        Serve,        // 正常：带 Range 回 206，否则 200
        IgnoreRange,  // 无视 Range，一律 200 全量（测「从头重下」）
        WrongContent, // 返回被篡改的内容（大小不变，触发 sha256 失败）
        ShortBody,    // 少 10 字节且 Content-Length 与发送一致（触发大小校验失败）
        BreakEarly,   // 声明完整 Content-Length 但只发一半就断开（触发断线续传/镜像切换）
        Stall,        // 收到请求后挂住不响应（测 cancelDownload/remove）
        NotFound,     // 404
    };
    struct Route {
        QString path;
        QByteArray body;
        Behavior behavior = Behavior::Serve;
    };
    struct Request {
        QString path;
        qint64 rangeFrom = -1;   // Range: bytes=N- 的 N；没带 Range 头为 -1
        int statusServed = 0;    // 实际响应状态码；BreakEarly 断开为 0
    };

    explicit TestHttpServer(QObject *parent = nullptr)
        : QObject(parent), m_server(new QTcpServer(this))
    {
        connect(m_server, &QTcpServer::newConnection, this, &TestHttpServer::acceptConnections);
    }

    bool start() { return m_server->listen(QHostAddress::LocalHost, 0); }

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server->serverPort()).arg(path));
    }

    void addRoute(const Route &route) { m_routes.append(route); }

    const QVector<Request> &requests() const { return m_requests; }

    int requestCount(const QString &path) const
    {
        return int(std::count_if(m_requests.cbegin(), m_requests.cend(),
                                 [&](const Request &r) { return r.path == path; }));
    }

private slots:
    void acceptConnections()
    {
        while (m_server->hasPendingConnections()) {
            QTcpSocket *socket = m_server->nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this,
                    [this, socket] { readRequest(socket); });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QAbstractSocket::errorOccurred, socket,
                    [socket] { socket->deleteLater(); });
            connect(socket, &QObject::destroyed, this, [this, socket] { m_buffers.remove(socket); });
        }
    }

private:
    void readRequest(QTcpSocket *socket)
    {
        if (socket->property("handled").toBool())
            return;
        m_buffers[socket].append(socket->readAll());
        const QByteArray &buffer = m_buffers[socket];
        const int headerEnd = buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        socket->setProperty("handled", true);
        const QByteArray requestLine = buffer.left(buffer.indexOf("\r\n"));
        const QByteArray headers = buffer.left(headerEnd);
        const QList<QByteArray> parts = requestLine.split(' ');
        if (parts.size() < 2 || parts.at(0) != "GET") {
            socket->disconnectFromHost();
            return;
        }
        handle(socket, QString::fromLatin1(parts.at(1)), parseRangeFrom(headers));
    }

    static qint64 parseRangeFrom(const QByteArray &rawHeaders)
    {
        const QByteArray headers = rawHeaders.toLower();
        const int tag = headers.indexOf("range:");
        if (tag < 0)
            return -1;
        const int eol = headers.indexOf("\r\n", tag);
        const QByteArray value = headers.mid(tag + 6, eol - tag - 6).trimmed();
        if (!value.startsWith("bytes="))
            return -1;
        const int dash = value.indexOf('-');
        if (dash <= 6)
            return -1;
        bool ok = false;
        const qint64 from = value.mid(6, dash - 6).toLongLong(&ok);
        return ok ? from : qint64(-1);
    }

    void handle(QTcpSocket *socket, const QString &path, qint64 rangeFrom)
    {
        const auto it = std::find_if(m_routes.cbegin(), m_routes.cend(),
                                     [&](const Route &r) { return r.path == path; });
        if (it == m_routes.cend()) {
            m_requests.append({path, rangeFrom, 404});
            writeSimple(socket, 404);
            return;
        }
        Request record{path, rangeFrom, 0};
        if (it->behavior == Behavior::NotFound) {
            record.statusServed = 404;
            m_requests.append(record);
            writeSimple(socket, 404);
            return;
        }
        if (it->behavior == Behavior::Stall) {
            m_requests.append(record);   // 挂住：不响应也不断开
            return;
        }
        QByteArray content = it->body;
        if (it->behavior == Behavior::WrongContent) {
            for (char &byte : content)
                byte = char(quint8(byte) ^ 0x5A);
        } else if (it->behavior == Behavior::ShortBody) {
            content = content.left(qMax(1, content.size() - 10));
        }
        if (it->behavior == Behavior::BreakEarly) {
            // 声明完整长度，只发一半，然后断开（QNetworkReply 报 RemoteHostClosedError）
            const QByteArray head = QStringLiteral(
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: %1\r\n"
                "Connection: close\r\n\r\n").arg(it->body.size()).toLatin1();
            socket->write(head);
            socket->write(it->body.left(it->body.size() / 2));
            socket->flush();
            socket->disconnectFromHost();
            m_requests.append(record);   // statusServed 保持 0 = 连接中断
            return;
        }
        const bool partial = it->behavior == Behavior::Serve && rangeFrom >= 0
                             && rangeFrom < content.size();
        if (partial) {
            record.statusServed = 206;
            socket->write(QStringLiteral(
                "HTTP/1.1 206 Partial Content\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: %1\r\n"
                "Content-Range: bytes %2-%3/%4\r\n"
                "Connection: close\r\n\r\n")
                    .arg(content.size() - rangeFrom)
                    .arg(rangeFrom)
                    .arg(content.size() - 1)
                    .arg(content.size())
                    .toLatin1());
            socket->write(content.mid(int(rangeFrom)));
        } else {
            record.statusServed = 200;
            socket->write(QStringLiteral(
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: application/octet-stream\r\n"
                "Content-Length: %1\r\n"
                "Connection: close\r\n\r\n").arg(content.size()).toLatin1());
            socket->write(content);
        }
        socket->flush();
        socket->disconnectFromHost();
        m_requests.append(record);
    }

    static void writeSimple(QTcpSocket *socket, int status)
    {
        const QByteArray body = "not found";
        socket->write(QStringLiteral(
            "HTTP/1.1 %1 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %2\r\n"
            "Connection: close\r\n\r\n").arg(status).arg(body.size()).toLatin1());
        socket->write(body);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer *m_server = nullptr;
    QVector<Route> m_routes;
    QVector<Request> m_requests;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
