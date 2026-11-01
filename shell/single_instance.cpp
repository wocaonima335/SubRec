#include "shell/single_instance.h"
#include "shell/single_instance_protocol.h"

#include <QByteArray>
#include <QDeadlineTimer>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace subrec::shell {

namespace proto = single_instance_protocol;

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

SingleInstance::~SingleInstance()
{
    if (m_server) m_server->close();
#ifdef _WIN32
    if (m_mutex) CloseHandle(static_cast<HANDLE>(m_mutex));
#endif
    m_mutex = nullptr;
}

QString SingleInstance::serverName()
{
    QString user = qEnvironmentVariable("USERNAME");
    if (user.isEmpty()) user = qEnvironmentVariable("USER");
    return QString::fromStdString(proto::serverNameFor(user.toStdString()));
}

SingleInstance::Role SingleInstance::acquire()
{
#ifdef _WIN32
    if (m_mutex) return Role::Primary; // already acquired by this object
    HANDLE h = CreateMutexW(nullptr, FALSE, L"Local\\SubRec.SingleInstance");
    const DWORD err = GetLastError();
    if (!h) {
        qWarning().noquote() << QStringLiteral("[Shell] CreateMutex failed, error=%1; "
                                               "continuing as primary").arg(static_cast<qulonglong>(err));
        return Role::Primary; // fail open
    }
    if (err == ERROR_ALREADY_EXISTS) {
        CloseHandle(h);
        return Role::Secondary;
    }
    m_mutex = h;
    return Role::Primary;
#else
    return Role::Primary;
#endif
}

bool SingleInstance::listen()
{
    if (m_server) return m_server->isListening();
    m_server = new QLocalServer(this);
    // Only the current user may connect.
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    const QString name = serverName();
    // We own the mutex, so any existing pipe/socket with this name is stale.
    QLocalServer::removeServer(name);
    if (!m_server->listen(name)) {
        qWarning().noquote() << QStringLiteral("[Shell] single-instance listen failed: %1")
                                    .arg(m_server->errorString());
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                if (!socket->canReadLine()) {
                    if (socket->bytesAvailable() > qint64(proto::kMaxLine)) socket->abort();
                    return;
                }
                const QByteArray line = socket->readLine(qint64(proto::kMaxLine) + 1);
                const proto::Parsed req = proto::parseRequest(
                    std::string_view(line.constData(), size_t(line.size())));
                if (req == proto::Parsed::Invalid) {
                    qWarning().noquote() << "[Shell] single-instance: invalid request ignored";
                    socket->abort();
                    return;
                }
                const std::string ack = proto::ackLine();
                socket->write(ack.data(), qint64(ack.size()));
                socket->flush();
                socket->disconnectFromServer();
                if (req == proto::Parsed::Show) emit showRequested();
            });
        }
    });
    return true;
}

bool SingleInstance::notifyPrimary(Request request, int timeoutMs)
{
#ifdef _WIN32
    // Let the primary take the foreground when it shows its window (§4.2 item 4).
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    QDeadlineTimer deadline(timeoutMs < 0 ? 0 : timeoutMs);
    QLocalSocket socket;
    socket.connectToServer(serverName());
    if (!socket.waitForConnected(int(deadline.remainingTime()))) {
        qWarning().noquote() << QStringLiteral("[Shell] single-instance connect failed: %1")
                                    .arg(socket.errorString());
        return false;
    }
    const std::string line = proto::requestLine(request == Request::Show);
    socket.write(line.data(), qint64(line.size()));
    if (!socket.waitForBytesWritten(int(deadline.remainingTime()))) return false;
    while (!socket.canReadLine()) {
        if (deadline.hasExpired() || !socket.waitForReadyRead(int(deadline.remainingTime())))
            return false;
    }
    const QByteArray reply = socket.readLine(qint64(proto::kMaxLine) + 1);
    return proto::isAck(std::string_view(reply.constData(), size_t(reply.size())));
}

} // namespace subrec::shell
