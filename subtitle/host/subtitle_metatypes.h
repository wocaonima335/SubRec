#pragma once
// M0 冻结：跨线程信号载荷的 Qt 元类型声明。凡在 Queued 连接或 QVariant 中传递 subtitle:: 类型的
// 翻译单元都 include 本头；qRegisterSubtitleMetaTypes() 在 SubtitleService 构造时调用一次（工作包 D）。
// 需要 Qt Core。

#include "subtitle/subtitle_types.h"

#include <QMetaType>

Q_DECLARE_METATYPE(subtitle::PartialSegment)
Q_DECLARE_METATYPE(subtitle::FinalSegment)
Q_DECLARE_METATYPE(subtitle::ConfirmedPrefix)
Q_DECLARE_METATYPE(subtitle::RecorderHealth)
Q_DECLARE_METATYPE(subtitle::EngineRequest)
Q_DECLARE_METATYPE(subtitle::CueConfig)
Q_DECLARE_METATYPE(subtitle::LiveConfig)
Q_DECLARE_METATYPE(subtitle::DegradeLevel)

namespace subtitle {

inline void qRegisterSubtitleMetaTypes()
{
    qRegisterMetaType<subtitle::PartialSegment>();
    qRegisterMetaType<subtitle::FinalSegment>();
    qRegisterMetaType<subtitle::ConfirmedPrefix>();
    qRegisterMetaType<subtitle::RecorderHealth>();
    qRegisterMetaType<subtitle::EngineRequest>();
    qRegisterMetaType<subtitle::CueConfig>();
    qRegisterMetaType<subtitle::LiveConfig>();
    qRegisterMetaType<subtitle::DegradeLevel>();
}

} // namespace subtitle
