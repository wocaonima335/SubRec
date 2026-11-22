// M0 桩：subtitle_live 中归工作包 H 的部分（local_agreement / sentence_fixer / subtitle_merger）。
// H 实现时拆成同名 .cpp 并删除本文件。桩行为：不确认任何 token、不改写 final；写盘返回 NotSupported。

#include "subtitle/live/local_agreement.h"
#include "subtitle/live/sentence_fixer.h"
#include "subtitle/live/subtitle_merger.h"

#include <utility>

namespace subtitle {

namespace {
Status m0Stub(const char *what)
{
    return Status::error(ErrorCode::NotSupported, std::string("M0 stub: ") + what);
}
} // namespace

// ---- LocalAgreement（H）----
LocalAgreement::LocalAgreement(int n, int guardMs) : m_n(n < 2 ? 2 : (n > 3 ? 3 : n)), m_guardMs(guardMs) {}
void LocalAgreement::reset(SegId id)
{
    m_state = ConfirmedPrefix{};
    m_state.id = id;
    m_history.clear();
}
std::vector<Word> LocalAgreement::push(const PartialSegment &)
{
    ++m_stats.pushes;
    return {};
}
void LocalAgreement::freeze() { m_state.frozen = true; }
const ConfirmedPrefix &LocalAgreement::state() const { return m_state; }
int LocalAgreement::mismatchAgainst(const FinalSegment &) const { return 0; }
LocalAgreement::Stats LocalAgreement::stats() const { return m_stats; }
void LocalAgreement::recordFinal(const FinalSegment &) {}

// ---- SentenceFixer（H）----
SentenceFixer::SentenceFixer(FixerRules rules) : m_rules(std::move(rules)) {}
SentenceFixer::Outcome SentenceFixer::apply(FinalSegment &, SubtitleDocument &, bool) { return Outcome::Kept; }

// ---- SubtitleDocument（H）----
SubtitleDocument::SubtitleDocument() = default;
bool SubtitleDocument::acceptFinal(const FinalSegment &) { return false; }
void SubtitleDocument::addHardBoundary(int64_t at16k) { m_hardBoundaries16k.push_back(at16k); }
void SubtitleDocument::markPoison(int64_t, int64_t) {}
int64_t SubtitleDocument::resumePoint16k() const { return m_resume16k; }
const std::vector<Cue> &SubtitleDocument::cues() const { return m_cues; }
std::vector<Segment> SubtitleDocument::segmentsForSidecar() const { return {}; }
uint64_t SubtitleDocument::revision() const { return m_revision; }
void SubtitleDocument::setProvisional(SegId, const ConfirmedPrefix &) {}
void SubtitleDocument::dropProvisional(SegId) {}
std::vector<Cue *> SubtitleDocument::revisableTail(int) { return {}; }
bool SubtitleDocument::acceptRefined(const FinalSegment &) { return false; }
SentenceState SubtitleDocument::state(SegId id) const
{
    auto it = m_states.find(id);
    return it == m_states.end() ? SentenceState::InProgress : it->second;
}
void SubtitleDocument::markUserEdited(SegId id) { m_states[id] = SentenceState::UserEdited; }
double SubtitleDocument::refineProgress() const { return m_refineTotal > 0 ? double(m_refineDone) / m_refineTotal : 0.0; }
const std::vector<int64_t> &SubtitleDocument::hardBoundaries16k() const { return m_hardBoundaries16k; }
void SubtitleDocument::setSentenceState(SegId id, SentenceState s) { m_states[id] = s; }
void SubtitleDocument::setRefineTotal(int routedCount) { m_refineTotal = routedCount; }

// ---- SubtitleMerger（H）----
SubtitleMerger::SubtitleMerger(SubtitleDocument &doc, CueConfig cue, SentenceFixer &fixer,
                               LocalAgreement &agreement, IClock &clock)
    : m_doc(doc), m_cueConfig(cue), m_fixer(fixer), m_agreement(agreement), m_clock(clock)
{
}
void SubtitleMerger::onPartial(const PartialSegment &) {}
void SubtitleMerger::onFinal(const FinalSegment &) {}
void SubtitleMerger::onRefined(const FinalSegment &) {}
void SubtitleMerger::onLevelChanged(DegradeLevel level) { m_level = level; }
std::optional<ConfirmedPrefix> SubtitleMerger::current() const { return std::nullopt; }
bool SubtitleMerger::shouldFlushPart(int64_t) const { return false; }
Status SubtitleMerger::flushPart(const std::filesystem::path &, const SrtOptions &) { return m0Stub("SubtitleMerger::flushPart"); }
Status SubtitleMerger::finalize(const std::filesystem::path &, const SrtOptions &) { return m0Stub("SubtitleMerger::finalize"); }
Status SubtitleMerger::finalizeRefined(const std::filesystem::path &, const SrtOptions &, bool)
{
    return m0Stub("SubtitleMerger::finalizeRefined");
}
Status SubtitleMerger::resumeFromPart(const std::filesystem::path &, SubtitleDocument &)
{
    return m0Stub("SubtitleMerger::resumeFromPart");
}
void SubtitleMerger::setLiveConfig(const LiveConfig &live) { m_live = live; }
void SubtitleMerger::onSplice(int64_t at16k) { m_doc.addHardBoundary(at16k); }
void SubtitleMerger::onHostRestarted() {}

} // namespace subtitle
