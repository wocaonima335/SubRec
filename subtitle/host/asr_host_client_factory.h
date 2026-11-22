#pragma once
// M0 冻结：AsrHostClient 工厂（design-live-v2.md §10.3 RefineScheduler 构造参数 AsrHostClientFactory &）。
// 接口归属：M0；生产实现 DefaultAsrHostClientFactory 归工作包 D。CMake 目标 subtitle_host（Qt Core）。
//
// 用途：Runner 与 RefineScheduler 不直接 new AsrHostClient，而是经工厂创建，测试即可注入「假 ASR」：
// - 进程内假引擎：exePath = 真 subrec-asr，extraArguments = {"--engine=fake", "--fake=crashAtSeg=3,..."}；
// - 进程外假宿主：exePath = subrec-asr-fakehost（源码 tools/asr_host/fake_host，按脚本回放协议消息）。
// 两种方式都不需要模型（docs/subtitle/ipc-protocol.md §8、design-live-v2.md §11.1）。
// 线程：create 在 Runner 线程调用；返回对象归调用方所有（parent 为空时）。

#include "subtitle/host/asr_host_client.h"

#include <QString>
#include <QStringList>

#include <memory>

class AsrHostClientFactory {
public:
    virtual ~AsrHostClientFactory() = default;
    // role："live" | "refine"
    virtual std::unique_ptr<AsrHostClient> create(const QString &role) = 0;
};

// 生产实现：exePath 默认为 <appDir>/asr/subrec-asr.exe；extraArguments 只在测试中设置。
class DefaultAsrHostClientFactory : public AsrHostClientFactory {
public:
    explicit DefaultAsrHostClientFactory(QString exePath, QStringList extraArguments = {});
    std::unique_ptr<AsrHostClient> create(const QString &role) override;

private:
    QString m_exePath;
    QStringList m_extraArguments;
};
