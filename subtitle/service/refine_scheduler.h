#pragma once
// M0 冻结：句级模型精修调度 RefineScheduler（design-live-v2.md §4.3.3、§10.3、D28–D30）。M2 功能，默认关。
// 接口归属：M0；实现归工作包 R。CMake 目标 subtitle_service（Qt Core，Runner 线程）。
//
// - enqueue：初稿定稿后调用；按 routes() 选句，经工厂创建 role=refine 的宿主实例（NORMAL 优先级、
//   JobLimits::forRefine()），发 refine{segments, prompt}，逐句收 final{pass:2} → refined 信号。
// - routes（D28）：refineAll → true；skipMandarin 且 language=="zh" 且 asciiLetterRatio < 阈值 → false；
//   language 非 zh 或 ASCII 比例 ≥ 阈值 → true；glossaryMissTriggers 且 glossaryMiss → true（M2）。
// - interrupt：新录制开始 / 退出时 kill 精修实例、记断点（码 614）；resume：录制结束后按 FIFO 续跑。
// - 空/幻觉结果保留初稿（615）；UserEdited 句跳过（613）；实例启动失败或重启耗尽 → failed(712)，初稿完好。
// - 本类不写文件；合并与落盘由 SubtitleMerger::onRefined / finalizeRefined 完成。

#include "subtitle/host/subtitle_metatypes.h"
#include "subtitle/subtitle_types.h"

#include <QObject>
#include <QString>

class AsrHostClientFactory;

namespace subtitle {
class SubtitleDocument;
}

class RefineScheduler : public QObject { Q_OBJECT
public:
    RefineScheduler(AsrHostClientFactory &, subtitle::RefineRouting, QObject *parent = nullptr);
    ~RefineScheduler() override;
    void enqueue(subtitle::JobId, const subtitle::SubtitleDocument &, const QString &spool);  // 初稿定稿后
    static bool routes(const subtitle::FinalSegment &pass1, const subtitle::RefineRouting &);
    void interrupt();                               // 新录制开始 / 退出：kill 精修实例，记断点
    void resume();                                  // 录制结束后续跑（FIFO）
signals:
    void refined(subtitle::FinalSegment pass2);
    void progress(subtitle::JobId, double ratio);
    void finished(subtitle::JobId, int refinedCount, int keptCount);
    void failed(subtitle::JobId, int code, QString message);

private:
    AsrHostClientFactory &m_factory;
    subtitle::RefineRouting m_routing;
};
