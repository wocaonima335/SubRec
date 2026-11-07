#include "shell/single_instance.h"
#include "shell/single_instance_protocol.h"

#include <QByteArray>
#include <QDeadlineTimer>
#include <QDebug>
#include <QElapsedTimer>
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

namespace {

// Failure diagnostics only (never on the success path). Reads socket getters and values the
// caller already has; no extra wait/read calls, no payload, server name or user name.
void logSocketFailure(const char *side, const char *stage, const QLocalSocket &socket,
                      const QString &values, qint64 elapsedMs)
{
    qWarning().noquote() << QStringLiteral("[Shell] single-instance %1: %2 failed (%3; error=%4 "
                                           "\"%5\", state=%6, bytesAvailable=%7, bytesToWrite=%8, "
                                           "elapsedMs=%9)")
                                .arg(QLatin1String(side), QLatin1String(stage), values,
                                     QString::number(int(socket.error())), socket.errorString(),
                                     QString::number(int(socket.state())),
                                     QString::number(socket.bytesAvailable()),
                                     QString::number(socket.bytesToWrite()),
                                     QString::number(elapsedMs));
}

QString yesNo(bool value) { return value ? QStringLiteral("true") : QStringLiteral("false"); }

} // namespace

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
                QElapsedTimer elapsed;
                elapsed.start();
                QDeadlineTimer deadline(1000);
                int waits = 0;
                bool lastWait = true;
                while (!socket->canReadLine() && !deadline.hasExpired()
                       && socket->bytesAvailable() <= qint64(proto::kMaxLine)
                       && socket->state() == QLocalSocket::ConnectedState) {
                    lastWait = socket->waitForReadyRead(int(qMax<qint64>(1, deadline.remainingTime())));
                    ++waits;
                }
                proto::Parsed req = proto::Parsed::Invalid;
                qint64 lineLength = -1; // -1: no complete line arrived
                if (socket->canReadLine()) {
                    const QByteArray line = socket->readLine(qint64(proto::kMaxLine) + 1);
                    lineLength = line.size();
                    req = proto::parseRequest(std::string_view(line.constData(), size_t(line.size())));
                }
                if (req == proto::Parsed::Invalid) {
                    qWarning().noquote() << "[Shell] single-instance: invalid request ignored";
                    logSocketFailure("server", "parse", *socket,
                                     QStringLiteral("lineLength=%1, waits=%2, lastWaitForReadyRead=%3")
                                         .arg(lineLength).arg(waits).arg(yesNo(lastWait)),
                                     elapsed.elapsed());
                    socket->abort();
                } else {
                    const std::string ack = proto::ackLine();
                    const qint64 written = socket->write(ack.data(), qint64(ack.size()));
                    if (written != qint64(ack.size()))
                        logSocketFailure("server", "write", *socket,
                                         QStringLiteral("ret=%1, expected=%2").arg(written).arg(qint64(ack.size())),
                                         elapsed.elapsed());
                    // waitForBytesWritten can return false with nothing left to write, so only
                    // a result of false with bytes still queued is reported.
                    if (!socket->waitForBytesWritten(500) && socket->bytesToWrite() > 0)
                        logSocketFailure("server", "waitForBytesWritten", *socket,
                                         QStringLiteral("ret=false, written=%1").arg(written),
                                         elapsed.elapsed());
                    socket->disconnectFromServer();
                    if (socket->state() != QLocalSocket::UnconnectedState
                        && !socket->waitForDisconnected(200))
                        logSocketFailure("server", "waitForDisconnected", *socket,
                                         QStringLiteral("ret=false, written=%1").arg(written),
                                         elapsed.elapsed());
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
    QElapsedTimer elapsed;
    elapsed.start();
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
            logSocketFailure("client", "connect", socket,
                             QStringLiteral("retryable=%1, remainingMs=%2")
                                 .arg(yesNo(retryable)).arg(deadline.remainingTime()),
                             elapsed.elapsed());
            return false;
        }
        socket.abort();
        QThread::msleep(25);
    }
    const std::string line = proto::requestLine(request == Request::Show);
    const qint64 written = socket.write(line.data(), qint64(line.size()));
    if (!socket.waitForBytesWritten(int(deadline.remainingTime()))) {
        logSocketFailure("client", "waitForBytesWritten", socket,
                         QStringLiteral("ret=false, written=%1, expected=%2, remainingMs=%3")
                             .arg(written).arg(qint64(line.size())).arg(deadline.remainingTime()),
                         elapsed.elapsed());
        return false;
    }
    int waits = 0;
    while (!socket.canReadLine()) {
        // Same calls in the same order as before: waitForReadyRead only when not expired.
        const bool expired = deadline.hasExpired();
        if (expired || !socket.waitForReadyRead(int(deadline.remainingTime()))) {
            logSocketFailure("client", expired ? "deadline before waitForReadyRead" : "waitForReadyRead",
                             socket,
                             QStringLiteral("ret=%1, written=%2, waits=%3, remainingMs=%4")
                                 .arg(expired ? QStringLiteral("not called") : QStringLiteral("false"))
                                 .arg(written).arg(waits).arg(deadline.remainingTime()),
                             elapsed.elapsed());
            return false;
        }
        ++waits;
    }
    const QByteArray reply = socket.readLine(qint64(proto::kMaxLine) + 1);
    const bool acked = proto::isAck(std::string_view(reply.constData(), size_t(reply.size())));
    if (!acked) {
        // Only the reply length is logged, never its content.
        logSocketFailure("client", reply.isEmpty() ? "readLine" : "isAck", socket,
                         QStringLiteral("replyLength=%1, written=%2, waits=%3")
                             .arg(reply.size()).arg(written).arg(waits),
                         elapsed.elapsed());
    }
    return acked;
}

} // namespace subrec::shell
