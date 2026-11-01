#include "shell/single_instance.h"
#include "shell/single_instance_protocol.h"

#include <QByteArray>
#include <QDeadlineTimer>
#include <QDebug>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>

#include <atomic>
#include <future>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace subrec::shell {

namespace proto = single_instance_protocol;

struct SingleInstance::Worker {
    std::atomic<bool> stop{false};
    std::atomic<bool> listening{false};
    QThread *thread = nullptr;
};

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
}

SingleInstance::~SingleInstance()
{
    if (m_worker) {
        m_worker->stop = true;
        if (m_worker->thread) {
            m_worker->thread->wait();
            delete m_worker->thread;
        }
        delete m_worker;
        m_worker = nullptr;
    }
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
    if (m_worker) return m_worker->listening;
    m_worker = new Worker;
    const QString name = serverName();
    std::promise<bool> started;
    std::future<bool> startedFuture = started.get_future();
    Worker *w = m_worker;
    w->thread = QThread::create([this, w, name, &started] {
        // All server objects live on this thread and use blocking calls only, so no
        // event loop is needed here and the GUI thread may be busy (#5).
        QLocalServer server;
        server.setSocketOptions(QLocalServer::UserAccessOption); // current user only
        // We own the mutex, so any existing pipe/socket with this name is stale.
        QLocalServer::removeServer(name);
        const bool ok = server.listen(name);
        if (!ok)
            qWarning().noquote() << QStringLiteral("[Shell] single-instance listen failed: %1")
                                        .arg(server.errorString());
        w->listening = ok;
        started.set_value(ok);
        if (!ok) return;
        while (!w->stop) {
            if (!server.waitForNewConnection(100)) continue;
            while (QLocalSocket *socket = server.nextPendingConnection()) {
                QDeadlineTimer deadline(1000);
                while (!socket->canReadLine() && !deadline.hasExpired()
                       && socket->bytesAvailable() <= qint64(proto::kMaxLine)
                       && socket->state() == QLocalSocket::ConnectedState) {
                    socket->waitForReadyRead(int(qMax<qint64>(1, deadline.remainingTime())));
                }
                proto::Parsed req = proto::Parsed::Invalid;
                if (socket->canReadLine()) {
                    const QByteArray line = socket->readLine(qint64(proto::kMaxLine) + 1);
                    req = proto::parseRequest(std::string_view(line.constData(), size_t(line.size())));
                }
                if (req == proto::Parsed::Invalid) {
                    qWarning().noquote() << "[Shell] single-instance: invalid request ignored";
                    socket->abort();
                } else {
                    const std::string ack = proto::ackLine();
                    socket->write(ack.data(), qint64(ack.size()));
                    socket->waitForBytesWritten(500);
                    socket->disconnectFromServer();
                    if (socket->state() != QLocalSocket::UnconnectedState)
                        socket->waitForDisconnected(200);
                    if (req == proto::Parsed::Show)
                        QMetaObject::invokeMethod(this, [this] { emit showRequested(); },
                                                  Qt::QueuedConnection);
                }
                delete socket;
            }
        }
        server.close();
    });
    w->thread->setObjectName(QStringLiteral("SubRecSingleInstance"));
    w->thread->start();
    return startedFuture.get();
}

bool SingleInstance::notifyPrimary(Request request, int timeoutMs)
{
#ifdef _WIN32
    // Let the primary take the foreground when it shows its window (§4.2 item 4).
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    QDeadlineTimer deadline(timeoutMs < 0 ? 0 : timeoutMs);
    QLocalSocket socket;
    // Race (#5): a secondary that loses the mutex race can run before the primary has
    // called listen(). The pipe then does not exist yet, so keep retrying until the
    // deadline instead of failing on the first ServerNotFound/ConnectionRefused.
    for (;;) {
        socket.connectToServer(serverName());
        if (socket.waitForConnected(int(deadline.remainingTime()))) break;
        const auto err = socket.error();
        const bool retryable = err == QLocalSocket::ServerNotFoundError
                            || err == QLocalSocket::ConnectionRefusedError
                            || err == QLocalSocket::SocketTimeoutError;
        if (!retryable || deadline.hasExpired()) {
            qWarning().noquote() << QStringLiteral("[Shell] single-instance connect failed: %1")
                                        .arg(socket.errorString());
            return false;
        }
        socket.abort();
        QThread::msleep(25);
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
