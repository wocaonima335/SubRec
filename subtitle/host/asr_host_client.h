#pragma once
// M0 冻结：AsrHostClient——主进程侧的子进程代理（design-live-v2.md §10.3 C、§7.2–§7.6、§5.2；
// 上版 design-verified.md §11.3 C）。协议细节见 subtitle/host/asr_protocol.h 与 docs/subtitle/ipc-protocol.md。
//
// 接口归属：M0；实现归工作包 D。CMake 目标 subtitle_host（Qt Core）。
// 线程：对象活在 SubtitleJobRunner 线程（"subtitle" QThread），所有方法只在该线程调用，信号在该线程发出。
//
// 行为约定
// - start：QProcess 启动 exePath，参数 = --role=<role> --parent-pid=<pid> + extraArguments；lowPriority 时以
//   BELOW_NORMAL_PRIORITY_CLASS 创建（setCreateProcessArgumentsModifier）。started 后立即 assignJobObject
//   （由调用方传入限额），再发 hello{protocol=2, role}。子进程回 hello → ready(backends)；版本不符 →
//   failed(0, HostVersionMismatch) 并 stop。
// - 命令方法只把一行 JSON 写入 stdin（QProcess::write，非阻塞）；append 在内部节流到 LiveConfig::appendThrottleMs，
//   后发覆盖先发（§6.1）。
// - stdout 按行解析（proto::decode）；单行超 1 MiB 或解析失败 → failed(0, HostProtocolError)，由 Runner 决定重启。
// - stderr 限速转发到日志（第三方库输出都在这里，§7.2）。
// - 子进程退出（任何原因）→ exited(exitCode, crashed)；上版的 crashed() 信号合并到这里。
// - 看门狗判定不在本类：Runner 把 heartbeat/busy/loaded 转给 HostWatchdog，需要时调用 kill()。
// - 不同 role 的实例互相独立：实时宿主 role=live，精修宿主 role=refine（M2，RefineScheduler 通过工厂创建）。

#include "subtitle/host/job_limits.h"
#include "subtitle/host/subtitle_metatypes.h"
#include "subtitle/subtitle_types.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace subtitle {

struct RefineRequestSegment {                   // AsrHostClient::refine 的参数（对应 proto::RefineSegment）
    SegId id = -1;
    qint64 b16k = 0, e16k = 0;
};

} // namespace subtitle

class AsrHostClient : public QObject { Q_OBJECT
public:
    explicit AsrHostClient(QString exePath, QObject *parent = nullptr);
    ~AsrHostClient() override;

    // ---- 进程生命周期 ----
    void setRole(const QString &role);              // "live"（默认）| "refine"；start 前调用
    void setExtraArguments(const QStringList &args);   // 如 --engine=fake --fake=...（测试注入假 ASR）
    void start(bool lowPriority);
    void stop(int graceMs);                         // 发 quit，graceMs 后仍未退出则 kill
    void kill();                                    // 看门狗判定后立即结束
    bool isRunning() const;

    // ---- 命令（→ 子进程）----
    void load(const subtitle::EngineRequest &, const subtitle::LiveConfig &live = {});
    void open(subtitle::JobId, const QString &spool, qint64 resumeFrom16k, qint64 committed16k, bool live);
    void append(subtitle::JobId, qint64 committed16k);
    void splice(subtitle::JobId, qint64 at16k);
    void setLevel(subtitle::JobId, subtitle::DegradeLevel, int threads, int partialIntervalMs);
    void pauseJob(subtitle::JobId);  void resumeJob(subtitle::JobId);
    void finish(subtitle::JobId, qint64 total16k);
    void cancel(subtitle::JobId);
    void refine(subtitle::JobId, const QString &spool, const QVector<subtitle::RefineRequestSegment> &segments,
                const QString &prompt);             // M2，仅 role=refine
    void setPriority(bool belowNormal);              // SetPriorityClass：录制中 BELOW_NORMAL，无录制 NORMAL
    bool assignJobObject(const subtitle::JobLimits &);   // 启动后立即调用；非 Windows 返回 false

signals:
    void ready(QStringList backends);
    void loaded(QString backend, QString fallbackReason);
    void partialReady(subtitle::PartialSegment);  void finalReady(subtitle::FinalSegment);
    void heartbeat(qint64 seq, int rssMB);         void busy(qint64 b16k, qint64 e16k, qint64 samples);
    void progress(subtitle::JobId jobId, qint64 done16k, double decodeMsTotal);
    void jobDone(subtitle::JobId jobId);
    void jobCancelled(subtitle::JobId jobId);
    void failed(subtitle::JobId jobId, int errorCode, QString message);   // jobId=0 表示宿主级错误
    void hostLog(QString level, QString text);     // 子进程 log 消息
    void exited(int exitCode, bool crashed);       // 上版 crashed() 合并到这里

private:
    QString m_exePath;
    QString m_role;
    QStringList m_extraArgs;
};
