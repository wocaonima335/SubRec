// M0 桩：RefineScheduler（M2）。真实实现归工作包 R。
// 桩行为：routes 一律 false（不送精修）；enqueue 立即报告 finished(job, 0, 0)。

#include "subtitle/service/refine_scheduler.h"

#include "subtitle/live/subtitle_merger.h"

RefineScheduler::RefineScheduler(AsrHostClientFactory &factory, subtitle::RefineRouting routing, QObject *parent)
    : QObject(parent), m_factory(factory), m_routing(routing)
{
}

RefineScheduler::~RefineScheduler() = default;

void RefineScheduler::enqueue(subtitle::JobId job, const subtitle::SubtitleDocument &, const QString &)
{
    emit finished(job, 0, 0);
}

bool RefineScheduler::routes(const subtitle::FinalSegment &, const subtitle::RefineRouting &) { return false; }
void RefineScheduler::interrupt() {}
void RefineScheduler::resume() {}
