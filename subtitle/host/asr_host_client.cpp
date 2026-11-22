// M0 桩：AsrHostClient 与 DefaultAsrHostClientFactory。真实实现归工作包 D
// （QProcess、Job Object、优先级、JSON-lines 收发、append 节流）。
// 桩行为：不启动进程；命令方法为空操作；start 立即发出 failed(0, NotSupported)。

#include "subtitle/host/asr_host_client.h"
#include "subtitle/host/asr_host_client_factory.h"

#include <utility>

AsrHostClient::AsrHostClient(QString exePath, QObject *parent)
    : QObject(parent), m_exePath(std::move(exePath)), m_role(QStringLiteral("live"))
{
}

AsrHostClient::~AsrHostClient() = default;

void AsrHostClient::setRole(const QString &role) { m_role = role; }
void AsrHostClient::setExtraArguments(const QStringList &args) { m_extraArgs = args; }
void AsrHostClient::start(bool)
{
    emit failed(0, static_cast<int>(subtitle::ErrorCode::NotSupported), QStringLiteral("M0 stub: AsrHostClient::start"));
}
void AsrHostClient::stop(int) {}
void AsrHostClient::kill() {}
bool AsrHostClient::isRunning() const { return false; }

void AsrHostClient::load(const subtitle::EngineRequest &, const subtitle::LiveConfig &) {}
void AsrHostClient::open(subtitle::JobId, const QString &, qint64, qint64, bool) {}
void AsrHostClient::append(subtitle::JobId, qint64) {}
void AsrHostClient::splice(subtitle::JobId, qint64) {}
void AsrHostClient::setLevel(subtitle::JobId, subtitle::DegradeLevel, int, int) {}
void AsrHostClient::pauseJob(subtitle::JobId) {}
void AsrHostClient::resumeJob(subtitle::JobId) {}
void AsrHostClient::finish(subtitle::JobId, qint64) {}
void AsrHostClient::cancel(subtitle::JobId) {}
void AsrHostClient::refine(subtitle::JobId, const QString &, const QVector<subtitle::RefineRequestSegment> &, const QString &) {}
void AsrHostClient::setPriority(bool) {}
bool AsrHostClient::assignJobObject(const subtitle::JobLimits &) { return false; }

DefaultAsrHostClientFactory::DefaultAsrHostClientFactory(QString exePath, QStringList extraArguments)
    : m_exePath(std::move(exePath)), m_extraArguments(std::move(extraArguments))
{
}

std::unique_ptr<AsrHostClient> DefaultAsrHostClientFactory::create(const QString &role)
{
    auto client = std::make_unique<AsrHostClient>(m_exePath);
    client->setRole(role);
    client->setExtraArguments(m_extraArguments);
    return client;
}
