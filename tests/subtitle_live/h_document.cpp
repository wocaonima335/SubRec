#include "h_test_support.h"

void idempotent()
{
    Rig r; r.fixer = SentenceFixer(looseRules(true));
    auto a = final(1, 0, 1000, "甲"), b = final(2, 1100, 2100, "乙");
    r.merger.onFinal(a); r.merger.onFinal(b);
    CHECK(r.doc.cues().size() == 1); CHECK(r.doc.cues()[0].text == "甲乙");
    const auto revision = r.doc.revision();
    r.merger.onFinal(b); r.merger.onFinal(a);
    b.id = 99; r.merger.onFinal(b);
    CHECK(r.doc.revision() == revision); CHECK(r.doc.cues()[0].text == "甲乙");
    CHECK(r.doc.segmentsForSidecar().size() == 2);
}
void out_of_order()
{
    Rig r; r.merger.onFinal(final(3, 4000, 5000)); const auto rev = r.doc.revision();
    r.merger.onFinal(final(1, 0, 1000)); r.merger.onFinal(final(2, 2000, 3000));
    CHECK(r.doc.revision() == rev); CHECK(r.doc.cues().size() == 1);
    CHECK(r.doc.resumePoint16k() == 5000 * 16);
    r.merger.onFinal(final(4, 6000, 7000)); CHECK(r.doc.cues().size() == 2);
    CHECK(!r.doc.acceptFinal(final(5, 5000, 5000)));
}
void resume()
{
    Temp t("resume"); auto path = srtPartPath(t.dir / "draft.srt"); Rig r;
    r.merger.onFinal(final(1, 0, 1000)); r.merger.onFinal(final(2, 2000, 3000, "世界。"));
    CHECK(r.merger.flushPart(path, {}).ok());
    SubtitleDocument doc; CHECK(SubtitleMerger::resumeFromPart(path, doc).ok());
    CHECK(doc.resumePoint16k() == 3000 * 16); CHECK(doc.cues().size() == 2);
    CHECK(formatSrt(doc.cues(), {}) == read(path));
    CHECK(!SubtitleMerger::resumeFromPart(path, doc).ok());
    LocalAgreement la(2, 300); SentenceFixer fixer(looseRules()); ManualClock clock;
    SubtitleMerger merger(doc, {}, fixer, la, clock);
    // A host id may be lower than the number of restored cues: one final can
    // produce several cues, and SRT does not retain those original identities.
    merger.onFinal(final(1, 4000, 5000, "继续。"));
    CHECK(merger.flushPart(path, {}).ok()); const auto bytes = read(path);
    CHECK(bytes.find("\r\n\r\n3\r\n00:00:04,000") != std::string::npos);
    CHECK(parse(path).size() == 3);
    SubtitleDocument empty; CHECK(writeFileAtomic(path, "broken").ok());
    CHECK(!SubtitleMerger::resumeFromPart(path, empty).ok()); CHECK(empty.revision() == 0);
}
void throttling()
{
    Temp t("throttle"); Rig r; LiveConfig live; live.partFlushIntervalMs = 1000; live.writeConfirmedToPart = true;
    r.merger.setLiveConfig(live); auto path = t.dir / "x.srt.part";
    CHECK(!r.merger.shouldFlushPart(10000));
    auto p = partial(); r.merger.onPartial(p); r.merger.onPartial(p);
    CHECK(!r.merger.shouldFlushPart(999)); CHECK(r.merger.shouldFlushPart(1000));
    r.clock.set(1000); CHECK(r.merger.flushPart(path, {}).ok()); CHECK(!r.merger.shouldFlushPart(10000));
    r.merger.onFinal(final(1, 0, 4000)); CHECK(r.merger.shouldFlushPart(1000));
    CHECK(!r.merger.flushPart(t.dir / "missing" / "x", {}).ok()); CHECK(r.merger.shouldFlushPart(1000));
    CHECK(r.merger.flushPart(path, {}).ok()); CHECK(!r.merger.shouldFlushPart(1000));
    r.merger.onFinal(final(1, 0, 4000)); CHECK(!r.merger.shouldFlushPart(1000));
}
void provisional_on()
{
    Temp t("provisional"); Rig r; LiveConfig config; config.writeConfirmedToPart = true; r.merger.setLiveConfig(config);
    auto p = partial(1, 2999); r.merger.onPartial(p); r.merger.onPartial(p); CHECK(r.doc.cues().empty());
    p.upto16k = 3000 * 16; r.merger.onPartial(p);
    CHECK(r.doc.cues().size() == 1); CHECK(r.doc.cues()[0].provisional);
    CHECK(r.doc.cues()[0].text == "你好，"); CHECK(r.doc.cues()[0].endMs == 2000);
    const auto path = t.dir / "x.srt.part"; CHECK(r.merger.flushPart(path, {}).ok()); CHECK(parse(path)[0].text == "你好，");
    r.merger.onFinal(final(1, 0, 4000, "您好。"));
    CHECK(r.doc.cues().size() == 1); CHECK(!r.doc.cues()[0].provisional); CHECK(r.doc.cues()[0].text == "您好。");
    CHECK(r.merger.flushPart(path, {}).ok()); CHECK(parse(path)[0].text == "您好。");
    Rig noPunct; noPunct.merger.setLiveConfig(config); p = partial(); p.tokens.erase(p.tokens.begin() + 2); p.text = "你好世";
    noPunct.merger.onPartial(p); noPunct.merger.onPartial(p); CHECK(noPunct.doc.cues().empty());
}
void provisional_drop()
{
    for (const auto &text : {std::string("谢谢观看"), std::string("嗯")}) {
        Rig r; LiveConfig live; live.writeConfirmedToPart = true; r.merger.setLiveConfig(live);
        r.merger.onPartial(partial()); r.merger.onPartial(partial()); CHECK(r.doc.cues().size() == 1);
        r.merger.onFinal(final(1, 0, 4000, text)); CHECK(r.doc.cues().empty());
        CHECK(r.doc.state(1) == SentenceState::Dropped); CHECK(r.doc.resumePoint16k() == 64000);
        CHECK(!r.merger.current());
    }
}
void provisional_off()
{
    Rig r; Temp t("off"); auto path = t.dir / "x.srt.part";
    r.merger.onPartial(partial()); r.merger.onPartial(partial()); CHECK(r.doc.cues().empty());
    CHECK(r.merger.flushPart(path, {}).ok()); CHECK(parse(path).empty());
    LiveConfig live; live.writeConfirmedToPart = true; r.merger.setLiveConfig(live); r.merger.onPartial(partial());
    CHECK(r.doc.cues().size() == 1);
    live.writeConfirmedToPart = false; r.merger.setLiveConfig(live); CHECK(r.doc.cues().empty());
    CHECK(r.merger.flushPart(path, {}).ok()); CHECK(parse(path).empty());
}
void boundaries()
{
    Rig r; r.merger.onSplice(32000); r.merger.onSplice(16000); r.merger.onSplice(32000);
    CHECK((r.doc.hardBoundaries16k() == std::vector<int64_t>{16000, 32000}));
    const auto rev = r.doc.revision(); r.doc.addHardBoundary(-1); r.doc.addHardBoundary(16000); CHECK(r.doc.revision() == rev);
    r.merger.onFinal(final(1, 0, 4000, "甲乙丙丁")); CHECK(r.doc.cues().size() >= 3);
    for (const auto &c : r.doc.cues()) for (auto b : {1000, 2000}) CHECK(!(c.beginMs < b && c.endMs > b));
}
void states()
{
    SubtitleDocument doc; CHECK(doc.state(1) == SentenceState::InProgress);
    CHECK(doc.acceptFinal(final(1, 0, 1000))); CHECK(doc.state(1) == SentenceState::Pass1Final);
    doc.setSentenceState(1, SentenceState::Fixed); const auto rev = doc.revision();
    doc.setSentenceState(1, SentenceState::Fixed); CHECK(doc.revision() == rev);
    doc.setRefineTotal(2); CHECK(doc.refineProgress() == 0);
    auto f = final(1, 0, 1000, "您好。"); f.pass = 2;
    CHECK(doc.acceptRefined(f)); CHECK(doc.state(1) == SentenceState::Refined); CHECK(doc.refineProgress() == 0.5);
    CHECK(!doc.acceptRefined(f)); CHECK(doc.refineProgress() == 0.5);
    doc.setSentenceState(2, SentenceState::Refined); CHECK(doc.refineProgress() == 1);
    doc.setSentenceState(2, SentenceState::Fixed); CHECK(doc.refineProgress() == 0.5);
    doc.setRefineTotal(-1); CHECK(doc.refineProgress() == 0);
    doc.markPoison(20000, 30000); const auto side = doc.segmentsForSidecar(); CHECK(side.size() == 2);
    CHECK(side[1].text.empty()); CHECK(doc.state(side[1].id) == SentenceState::Poisoned);
    CHECK(doc.resumePoint16k() == 30000); CHECK(doc.cues().size() == 1);
    std::vector<Segment> roundtrip; CHECK(fromJson(toJson(side), roundtrip)); CHECK(roundtrip.size() == 2);
}
void live_config()
{
    Rig r; LiveConfig live; live.agreementN = 3; live.confirmGuardMs = 500; live.revisableCues = 0;
    r.merger.setLiveConfig(live); CHECK(r.fixer.rules().revisableCues == 0);
    auto p = partial(1, 2400); r.merger.onPartial(p); r.merger.onPartial(p); CHECK(r.agreement.state().confirmed.empty());
    r.merger.onPartial(p); CHECK(r.agreement.state().confirmedText == "你");
    r.merger.onLevelChanged(DegradeLevel::L1FinalOnly); CHECK(r.merger.current()->frozen); CHECK(r.merger.current()->tentativeText.empty());
    r.merger.onLevelChanged(DegradeLevel::L0Live); p.upto16k = 5000 * 16;
    r.merger.onPartial(p); CHECK(r.agreement.state().confirmedText == "你");
    p.id = 2; for (int i = 0; i < 3; ++i) r.merger.onPartial(p); CHECK(r.agreement.state().confirmedText == "你好，世");
    r.merger.onLevelChanged(DegradeLevel::L2CatchUp); CHECK(!r.merger.current());
    const auto pushes = r.agreement.stats().pushes; r.merger.onPartial(p); CHECK(r.agreement.stats().pushes == pushes);
    r.merger.onHostRestarted(); CHECK(r.agreement.state().confirmed.empty()); CHECK(!r.merger.current());
    r.merger.onLevelChanged(DegradeLevel::L1FinalOnly); p.id = 3;
    for (int i = 0; i < 4; ++i) r.merger.onPartial(p);
    CHECK(r.agreement.state().frozen); CHECK(r.agreement.state().confirmed.empty());
    r.merger.onLevelChanged(DegradeLevel::L0Live); p.id = 4;
    for (int i = 0; i < 3; ++i) r.merger.onPartial(p);
    const auto confirmed = r.agreement.state().confirmedText;
    r.merger.onLevelChanged(DegradeLevel::L1FinalOnly);
    live.agreementN = 2; live.confirmGuardMs = 300; r.merger.setLiveConfig(live);
    CHECK(r.agreement.state().confirmedText == confirmed); CHECK(r.agreement.state().frozen);
    r.merger.onLevelChanged(DegradeLevel::L0Live); p.id = 5;
    r.merger.onPartial(p); CHECK(r.agreement.state().confirmed.empty());
    r.merger.onPartial(p); CHECK(r.agreement.state().confirmedText == confirmed);
}
void refined_files()
{
    Temp t("refined"); Rig r; const auto path = t.dir / "x.srt";
    r.merger.onFinal(final(1, 0, 1000)); CHECK(r.merger.finalize(path, {}).ok()); const auto original = read(path);
    auto f = final(1, 0, 1000, "您好。"); f.pass = 2; r.merger.onRefined(f);
    CHECK(r.doc.cues()[0].text == "您好。"); CHECK(r.merger.finalizeRefined(path, {}, true).ok());
    CHECK(read(path) == original); CHECK(parse(refinePartPath(path))[0].text == "您好。");
    CHECK(r.merger.finalizeRefined(path, {}, false).ok()); CHECK(read(liveSrtPath(path)) == original);
    CHECK(parse(path)[0].text == "您好。");
}
int main(int argc, char **argv)
{
    try {
        CHECK(argc == 2); const std::string name = argv[1];
        if (name == "idempotent") idempotent(); else if (name == "out_of_order") out_of_order();
        else if (name == "resume") resume(); else if (name == "throttling") throttling();
        else if (name == "provisional_on") provisional_on(); else if (name == "provisional_drop") provisional_drop();
        else if (name == "provisional_off") provisional_off(); else if (name == "boundaries") boundaries();
        else if (name == "states") states(); else if (name == "live_config") live_config();
        else if (name == "refined_files") refined_files(); else CHECK(false);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
