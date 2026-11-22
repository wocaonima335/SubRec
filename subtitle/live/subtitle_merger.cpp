#include "subtitle/live/subtitle_merger.h"
#include "subtitle/live/local_agreement.h"
#include "subtitle/live/sentence_fixer.h"
#include "subtitle/core/cue_builder.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>

namespace subtitle {
namespace {
Segment sidecar(const FinalSegment &f)
{
    Segment s; s.id = f.id; s.begin16k = f.begin16k; s.end16k = f.end16k;
    s.text = f.text; s.words = f.words; s.language = f.language; return s;
}
bool readFile(const std::filesystem::path &path, std::string &bytes)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    bytes.assign(std::istreambuf_iterator<char>(stream), {});
    return !stream.bad();
}
std::vector<Cue> outputCues(const SubtitleDocument &doc, bool provisional)
{
    auto cues = doc.cues();
    if (!provisional) cues.erase(std::remove_if(cues.begin(), cues.end(), [](const Cue &c) { return c.provisional; }), cues.end());
    resolveOverlaps(cues); return cues;
}
}
SubtitleDocument::SubtitleDocument() = default;
std::vector<int64_t> SubtitleDocument::boundariesMs() const
{
    std::vector<int64_t> result;
    for (auto b : m_hardBoundaries16k) result.push_back(asrSamplesToMs(b));
    return result;
}
void SubtitleDocument::sortCues()
{
    std::stable_sort(m_cues.begin(), m_cues.end(), [](const Cue &a, const Cue &b) {
        return a.beginMs < b.beginMs || (a.beginMs == b.beginMs && a.endMs < b.endMs);
    });
}
bool SubtitleDocument::canAccept(const FinalSegment &f) const
{
    if (f.pass != 1 || f.id < 0 || f.begin16k < 0 || f.end16k <= f.begin16k || f.end16k <= m_resume16k) return false;
    if (m_finals.count(f.id)) return false;
    for (const auto &entry : m_finals) {
        const auto &old = entry.second;
        if (old.job == f.job && old.begin16k == f.begin16k && old.end16k == f.end16k && old.pass == f.pass) return false;
    }
    return true;
}
bool SubtitleDocument::acceptPrepared(const FinalSegment &f, std::vector<Cue> cues)
{
    if (!canAccept(f)) return false;
    dropProvisional(f.id);
    m_finals[f.id] = f;
    m_cues.insert(m_cues.end(), cues.begin(), cues.end()); sortCues();
    m_resume16k = f.end16k;
    m_states[f.id] = SentenceState::Pass1Final;
    ++m_revision; return true;
}
bool SubtitleDocument::acceptFinal(const FinalSegment &f)
{
    if (!canAccept(f)) return false;
    return acceptPrepared(f, buildCues(f, m_cueConfig, boundariesMs()));
}
void SubtitleDocument::addHardBoundary(int64_t at16k)
{
    if (at16k < 0) return;
    const auto it = std::lower_bound(m_hardBoundaries16k.begin(), m_hardBoundaries16k.end(), at16k);
    if (it == m_hardBoundaries16k.end() || *it != at16k) { m_hardBoundaries16k.insert(it, at16k); ++m_revision; }
}
void SubtitleDocument::markPoison(int64_t b16k, int64_t e16k)
{
    if (b16k < 0 || e16k <= b16k) return;
    for (const auto &p : m_poison) if (p.begin16k == b16k && p.end16k == e16k) return;
    Segment p; p.id = m_states.empty() || m_states.begin()->first >= 0 ? -1 : m_states.begin()->first - 1;
    p.begin16k = b16k; p.end16k = e16k;
    m_poison.push_back(p); m_states[p.id] = SentenceState::Poisoned;
    m_resume16k = std::max(m_resume16k, e16k); ++m_revision;
}
int64_t SubtitleDocument::resumePoint16k() const { return m_resume16k; }
const std::vector<Cue> &SubtitleDocument::cues() const { return m_cues; }
std::vector<Segment> SubtitleDocument::segmentsForSidecar() const
{
    auto result = m_poison;
    for (const auto &entry : m_finals) result.push_back(sidecar(entry.second));
    std::stable_sort(result.begin(), result.end(), [](const Segment &a, const Segment &b) { return a.begin16k < b.begin16k; });
    return result;
}
uint64_t SubtitleDocument::revision() const { return m_revision; }
void SubtitleDocument::setProvisional(SegId id, const ConfirmedPrefix &prefix)
{
    if (id < 0 || prefix.confirmed.empty() || prefix.confirmedText.empty() || m_finals.count(id)) return;
    Cue c; c.segmentId = id; c.provisional = true; c.text = prefix.confirmedText;
    c.beginMs = prefix.confirmed.front().beginMs; c.endMs = asrSamplesToMs(prefix.confirmedUpto16k);
    if (c.endMs <= c.beginMs || prefix.confirmedUpto16k <= m_resume16k) return;
    for (auto &old : m_cues) {
        if (old.provisional && old.segmentId == id) {
            if (old.beginMs != c.beginMs || old.endMs != c.endMs || old.text != c.text) { old = c; sortCues(); ++m_revision; }
            setSentenceState(id, SentenceState::PartlyConfirmed); return;
        }
    }
    m_cues.push_back(std::move(c)); sortCues(); ++m_revision;
    setSentenceState(id, SentenceState::PartlyConfirmed);
}
void SubtitleDocument::dropProvisional(SegId id)
{
    const auto size = m_cues.size();
    m_cues.erase(std::remove_if(m_cues.begin(), m_cues.end(), [id](const Cue &c) { return c.provisional && c.segmentId == id; }), m_cues.end());
    if (m_cues.size() != size) ++m_revision;
}
std::vector<Cue *> SubtitleDocument::revisableTail(int n)
{
    std::vector<Cue *> result;
    for (auto it = m_cues.rbegin(); it != m_cues.rend() && int(result.size()) < n; ++it)
        if (!it->provisional) result.push_back(&*it);
    std::reverse(result.begin(), result.end()); return result;
}
bool SubtitleDocument::acceptRefined(const FinalSegment &f)
{
    const auto s = state(f.id);
    if (f.pass != 2 || (s != SentenceState::Pass1Final && s != SentenceState::Fixed)) return false;
    const auto found = m_finals.find(f.id);
    if (found == m_finals.end() || f.begin16k != found->second.begin16k || f.end16k != found->second.end16k ||
        f.job != found->second.job) return false;
    auto replacement = buildCues(f, m_cueConfig, boundariesMs());
    if (replacement.empty()) return false;
    // A cross-sentence cue retains source finals. Split that group back into its
    // source sentences when refining one member, so no neighbouring text is lost.
    const SegId root = m_mergedInto.count(f.id) ? m_mergedInto.at(f.id) : f.id;
    std::vector<SegId> members{root};
    for (const auto &entry : m_mergedInto) if (entry.second == root) members.push_back(entry.first);
    for (auto id : members) if (state(id) == SentenceState::UserEdited) return false;
    m_cues.erase(std::remove_if(m_cues.begin(), m_cues.end(), [&](const Cue &c) {
        return std::find(members.begin(), members.end(), c.segmentId) != members.end();
    }), m_cues.end());
    m_finals[f.id] = f;
    for (auto id : members) {
        auto it = m_finals.find(id);
        if (it != m_finals.end()) {
            auto cues = id == f.id ? replacement : buildCues(it->second, m_cueConfig, boundariesMs());
            m_cues.insert(m_cues.end(), cues.begin(), cues.end());
        }
        m_mergedInto.erase(id);
    }
    sortCues(); setSentenceState(f.id, SentenceState::Refined); ++m_revision; return true;
}
SentenceState SubtitleDocument::state(SegId id) const
{
    const auto it = m_states.find(id); return it == m_states.end() ? SentenceState::InProgress : it->second;
}
void SubtitleDocument::markUserEdited(SegId id)
{
    const auto root = m_mergedInto.count(id) ? m_mergedInto.at(id) : id;
    setSentenceState(root, SentenceState::UserEdited); setSentenceState(id, SentenceState::UserEdited);
    for (const auto &entry : m_mergedInto) if (entry.second == root) setSentenceState(entry.first, SentenceState::UserEdited);
    for (auto &c : m_cues) if (c.segmentId == root && !c.edited) { c.edited = true; ++m_revision; }
}
double SubtitleDocument::refineProgress() const
{
    return m_refineTotal > 0 ? std::min(1.0, double(m_refineDone) / m_refineTotal) : 0.0;
}
const std::vector<int64_t> &SubtitleDocument::hardBoundaries16k() const { return m_hardBoundaries16k; }
void SubtitleDocument::setSentenceState(SegId id, SentenceState s)
{
    const auto found = m_states.find(id);
    if (found != m_states.end() && found->second == s) return;
    if (found != m_states.end() && found->second == SentenceState::Refined) --m_refineDone;
    if (s == SentenceState::Refined) ++m_refineDone;
    m_states[id] = s; ++m_revision;
}
void SubtitleDocument::setRefineTotal(int count)
{
    count = std::max(0, count);
    if (m_refineTotal != count) { m_refineTotal = count; ++m_revision; }
}

SubtitleMerger::SubtitleMerger(SubtitleDocument &doc, CueConfig cue, SentenceFixer &fixer,
                             LocalAgreement &agreement, IClock &clock)
    : m_doc(doc), m_cueConfig(cue), m_fixer(fixer), m_agreement(agreement), m_clock(clock), m_lastFlushMs(clock.nowMs())
{
    m_doc.m_cueConfig = cue;
    m_live.agreementN = agreement.n(); m_live.confirmGuardMs = agreement.guardMs();
    m_live.revisableCues = fixer.rules().revisableCues;
    auto rules = fixer.rules(); rules.cue = cue; m_fixer = SentenceFixer(std::move(rules));
}
void SubtitleMerger::onPartial(const PartialSegment &p)
{
    if (m_level >= DegradeLevel::L2CatchUp || p.id < 0 || p.upto16k <= m_doc.resumePoint16k()) return;
    if (m_agreement.state().id != p.id) {
        if (m_agreement.n() != std::clamp(m_live.agreementN, 2, 3) ||
            m_agreement.guardMs() != std::max(0, m_live.confirmGuardMs))
            m_agreement = LocalAgreement(m_live.agreementN, m_live.confirmGuardMs);
        m_agreement.reset(p.id);
        if (m_level == DegradeLevel::L1FinalOnly) m_agreement.freeze();
    }
    m_agreement.push(p); m_display = m_agreement.state(); m_displayUntilMs = 0;
    if (!m_agreement.state().confirmed.empty()) m_doc.setSentenceState(p.id, SentenceState::PartlyConfirmed);
    if (!m_live.writeConfirmedToPart || p.upto16k - p.begin16k < int64_t(m_live.provisionalMinSegmentMs) * kAsrSamplesPerMs) return;
    auto prefix = m_agreement.state();
    // Write only the last complete confirmed clause, using its token timestamp.
    size_t words = 0; std::string text, complete;
    for (size_t i = 0; i < prefix.confirmed.size(); ++i) {
        const auto &w = prefix.confirmed[i];
        text += w.text;
        for (size_t b = 0; b < w.text.size();) {
            const auto start = b++;
            while (b < w.text.size() && (static_cast<unsigned char>(w.text[b]) & 0xc0) == 0x80) ++b;
            if (std::string("，。！？；,.!?;").find(w.text.substr(start, b - start)) != std::string::npos) {
                words = i + 1; complete = text.substr(0, text.size() - w.text.size() + b);
            }
        }
    }
    if (!words) return;
    prefix.confirmed.resize(words); prefix.confirmedText = complete;
    prefix.confirmedUpto16k = prefix.confirmed.back().endMs * kAsrSamplesPerMs;
    m_doc.setProvisional(p.id, prefix);
}
void SubtitleMerger::onFinal(const FinalSegment &f)
{
    if (!m_doc.canAccept(f)) return; // No display, statistics or rule mutations for duplicates.
    if (m_agreement.state().id == f.id) { m_agreement.recordFinal(f); m_agreement.reset(-1); }
    auto fixed = f;
    const auto outcome = m_fixer.apply(fixed, m_doc, true);
    if (outcome == SentenceFixer::Outcome::Dropped) fixed.text.clear();
    auto cues = outcome == SentenceFixer::Outcome::Kept ? buildCues(fixed, m_cueConfig, m_doc.boundariesMs()) : std::vector<Cue>{};
    m_doc.acceptPrepared(fixed, std::move(cues));
    m_doc.setSentenceState(f.id, outcome == SentenceFixer::Outcome::Dropped ? SentenceState::Dropped : SentenceState::Fixed);
    m_finalPending = true;
    if (!m_display || m_display->id == f.id) {
        m_display.reset();
        if (!fixed.text.empty()) {
            ConfirmedPrefix display; display.job = f.job; display.id = f.id;
            display.confirmedText = fixed.text; display.confirmedUpto16k = f.end16k;
            m_display = std::move(display); m_displayUntilMs = m_clock.nowMs() + 3000;
        }
    }
}
void SubtitleMerger::onRefined(const FinalSegment &f)
{
    // Run rules transactionally: rejected/empty refinements cannot mutate neighbours.
    auto copy = m_doc;
    if (!copy.acceptRefined(f)) return;
    auto fixed = f;
    // Exclude this sentence's old cues while finding its chronological predecessor.
    copy.m_cues.erase(std::remove_if(copy.m_cues.begin(), copy.m_cues.end(), [&](const Cue &c) { return c.segmentId == f.id; }), copy.m_cues.end());
    const auto outcome = m_fixer.apply(fixed, copy, false);
    if (outcome == SentenceFixer::Outcome::Dropped) return;
    copy.m_finals[f.id] = fixed;
    if (outcome == SentenceFixer::Outcome::Kept) {
        auto cues = buildCues(fixed, m_cueConfig, copy.boundariesMs());
        copy.m_cues.insert(copy.m_cues.end(), cues.begin(), cues.end());
    }
    copy.sortCues(); ++copy.m_revision; m_doc = std::move(copy);
}
void SubtitleMerger::onLevelChanged(DegradeLevel level)
{
    m_level = level;
    if (level >= DegradeLevel::L1FinalOnly) {
        m_agreement.freeze();
        if (m_display) { m_display->frozen = true; m_display->tentativeText.clear(); }
    }
    if (level >= DegradeLevel::L2CatchUp) m_display.reset();
}
std::optional<ConfirmedPrefix> SubtitleMerger::current() const
{
    if (m_level >= DegradeLevel::L2CatchUp || (m_displayUntilMs && m_clock.nowMs() >= m_displayUntilMs)) return std::nullopt;
    return m_display;
}
bool SubtitleMerger::shouldFlushPart(int64_t nowMs) const
{
    return m_doc.revision() != m_flushedRevision && (m_finalPending ||
        (nowMs >= m_lastFlushMs && nowMs - m_lastFlushMs >= std::max(0, m_live.partFlushIntervalMs)));
}
Status SubtitleMerger::flushPart(const std::filesystem::path &path, const SrtOptions &options)
try
{
    const auto status = writeFileAtomic(path, formatSrt(outputCues(m_doc, m_live.writeConfirmedToPart), options));
    if (status.ok()) { m_flushedRevision = m_doc.revision(); m_lastFlushMs = m_clock.nowMs(); m_finalPending = false; }
    return status;
}
catch (const std::bad_alloc &) { return Status::error(ErrorCode::OutOfMemory); }
catch (const std::exception &) { return Status::error(ErrorCode::SrtWriteFailed, "Subtitle file operation failed"); }
Status SubtitleMerger::finalize(const std::filesystem::path &path, const SrtOptions &options)
try
{
    return writeFileAtomic(path, formatSrt(outputCues(m_doc, false), options));
}
catch (const std::bad_alloc &) { return Status::error(ErrorCode::OutOfMemory); }
catch (const std::exception &) { return Status::error(ErrorCode::SrtWriteFailed, "Subtitle file operation failed"); }
Status SubtitleMerger::finalizeRefined(const std::filesystem::path &path, const SrtOptions &options, bool userEditedFile)
try
{
    const auto bytes = formatSrt(outputCues(m_doc, false), options);
    if (userEditedFile) return writeFileAtomic(refinePartPath(path), bytes);
    std::string original;
    if (!readFile(path, original)) return Status::error(ErrorCode::SrtWriteFailed, "Cannot read live subtitle draft");
    auto status = writeFileAtomic(liveSrtPath(path), original);
    if (!status.ok()) return status;
    return writeFileAtomic(path, bytes);
}
catch (const std::bad_alloc &) { return Status::error(ErrorCode::OutOfMemory); }
catch (const std::exception &) { return Status::error(ErrorCode::SrtWriteFailed, "Subtitle file operation failed"); }
Status SubtitleMerger::resumeFromPart(const std::filesystem::path &path, SubtitleDocument &doc)
try
{
    if (!doc.m_cues.empty() || !doc.m_states.empty() || !doc.m_finals.empty() || doc.m_resume16k)
        return Status::error(ErrorCode::InvalidArgument, "Resume requires an empty subtitle document");
    std::string bytes; std::vector<Cue> cues;
    if (!readFile(path, bytes) || !parseSrt(bytes, cues)) return Status::error(ErrorCode::JobResumeFromPart, "Cannot parse subtitle part file");
    SubtitleDocument restored; restored.m_cueConfig = doc.m_cueConfig;
    // SRT has no source segment ids. Reserve negative synthetic ids so a
    // resumed host's nonnegative ids cannot collide with reconstructed cues.
    SegId id = -static_cast<SegId>(cues.size());
    for (auto &cue : cues) {
        if (cue.endMs > std::numeric_limits<int64_t>::max() / kAsrSamplesPerMs)
            return Status::error(ErrorCode::JobResumeFromPart, "Subtitle timestamp exceeds sample range");
        cue.segmentId = id++;
        FinalSegment f; f.id = cue.segmentId; f.begin16k = cue.beginMs * kAsrSamplesPerMs;
        f.end16k = cue.endMs * kAsrSamplesPerMs; f.text = cue.text;
        restored.m_finals[f.id] = f; restored.m_states[f.id] = SentenceState::Fixed;
        restored.m_resume16k = std::max(restored.m_resume16k, f.end16k);
    }
    restored.m_cues = std::move(cues); restored.sortCues();
    restored.m_revision = doc.m_revision + 1; doc = std::move(restored); return Status::success();
}
catch (const std::bad_alloc &) { return Status::error(ErrorCode::OutOfMemory); }
catch (const std::exception &) { return Status::error(ErrorCode::JobResumeFromPart, "Subtitle file operation failed"); }
void SubtitleMerger::setLiveConfig(const LiveConfig &live)
{
    // Apply agreement tuning at a segment boundary: never erase an active
    // confirmed prefix or unfreeze the same sentence because settings changed.
    if (m_agreement.state().id < 0 && (m_agreement.n() != std::clamp(live.agreementN, 2, 3) ||
        m_agreement.guardMs() != std::max(0, live.confirmGuardMs)))
        m_agreement = LocalAgreement(live.agreementN, live.confirmGuardMs);
    auto rules = m_fixer.rules(); rules.revisableCues = live.revisableCues; rules.cue = m_cueConfig;
    m_fixer = SentenceFixer(std::move(rules)); m_live = live;
    if (!live.writeConfirmedToPart) {
        std::vector<SegId> ids;
        for (const auto &c : m_doc.cues()) if (c.provisional) ids.push_back(c.segmentId);
        for (auto id : ids) m_doc.dropProvisional(id);
    }
}
void SubtitleMerger::onSplice(int64_t at16k)
{
    m_doc.addHardBoundary(at16k); onHostRestarted();
}
void SubtitleMerger::onHostRestarted()
{
    m_doc.dropProvisional(m_agreement.state().id);
    m_agreement.reset(-1); m_display.reset(); m_displayUntilMs = 0;
}
} // namespace subtitle
