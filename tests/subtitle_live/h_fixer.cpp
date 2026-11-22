#include "h_test_support.h"

void window()
{
    SubtitleDocument doc;
    for (int i = 0; i < 4; ++i) CHECK(doc.acceptFinal(final(i, i * 2000, i * 2000 + 1000, "甲")));
    auto tail = doc.revisableTail(2); CHECK(tail.size() == 2); CHECK(tail[0]->segmentId == 2); CHECK(tail[1]->segmentId == 3);
    const auto before = doc.cues(); SentenceFixer fixer(looseRules(true));
    auto old = final(10, 1100, 1500, "乙"); CHECK(fixer.apply(old, doc, true) == SentenceFixer::Outcome::Kept);
    auto recent = final(11, 7100, 7500, "乙"); CHECK(fixer.apply(recent, doc, true) == SentenceFixer::Outcome::MergedIntoPrevious);
    CHECK(doc.cues()[0].text == before[0].text); CHECK(doc.cues()[1].text == before[1].text);
    CHECK(doc.cues()[3].text == "甲乙"); CHECK(doc.cues()[3].endMs == 7500);
    auto secondLast = final(12, 5100, 5500, "丙");
    CHECK(fixer.apply(secondLast, doc, true) == SentenceFixer::Outcome::MergedIntoPrevious);
    CHECK(doc.cues()[2].text == "甲丙"); CHECK(doc.cues()[0].text == before[0].text);
    CHECK(doc.revisableTail(0).empty()); CHECK(doc.revisableTail(-1).empty());
}
void merge_boundaries()
{
    struct Row { int gap, chars; bool merge; };
    for (const auto &row : std::vector<Row>{{299, 2, true}, {300, 2, false}, {301, 2, false}, {0, 2, true}, {0, 1, false}, {-1, 2, false}}) {
        SubtitleDocument doc; CHECK(doc.acceptFinal(final(1, 0, 1000, "甲")));
        auto rules = looseRules(true); rules.cue.maxCharsPerLine = row.chars; SentenceFixer fixer(rules);
        auto f = final(2, 1000 + row.gap, 2000 + row.gap, "乙");
        CHECK((fixer.apply(f, doc, true) == SentenceFixer::Outcome::MergedIntoPrevious) == row.merge);
        CHECK(doc.cues()[0].text == (row.merge ? "甲乙" : "甲"));
    }
    for (const auto &text : {std::string("甲。"), std::string("甲!"), std::string("甲；")}) {
        SubtitleDocument doc; CHECK(doc.acceptFinal(final(1, 0, 1000, text)));
        SentenceFixer fixer(looseRules(true)); auto f = final(2, 1001, 2000, "乙");
        CHECK(fixer.apply(f, doc, true) == SentenceFixer::Outcome::Kept);
    }
    SubtitleDocument doc; CHECK(doc.acceptFinal(final(1, 0, 1000, "甲"))); doc.addHardBoundary(1000 * 16);
    SentenceFixer fixer(looseRules(true)); auto f = final(2, 1000, 2000, "乙");
    CHECK(fixer.apply(f, doc, true) == SentenceFixer::Outcome::Kept);
}
void finished()
{
    SubtitleDocument doc;
    for (int i = 0; i < 5; ++i) CHECK(doc.acceptFinal(final(i, i * 2000, i * 2000 + 1000, "甲")));
    SentenceFixer fixer(looseRules(true)); auto f = final(10, 1100, 1500, "乙");
    CHECK(fixer.apply(f, doc, true) == SentenceFixer::Outcome::Kept);
    CHECK(fixer.apply(f, doc, false) == SentenceFixer::Outcome::MergedIntoPrevious);
    CHECK(doc.cues()[0].text == "甲乙"); CHECK(doc.cues()[0].endMs == 1500);
    for (size_t i = 1; i < doc.cues().size(); ++i) CHECK(doc.cues()[i].text == "甲");
}
void user_edited()
{
    Rig r; r.merger.onFinal(final(1, 0, 1000)); r.doc.markUserEdited(1);
    auto f = final(1, 0, 1000, "改写。"); f.pass = 2; const auto rev = r.doc.revision();
    CHECK(!r.doc.acceptRefined(f)); r.merger.onRefined(f);
    CHECK(r.doc.revision() == rev); CHECK(r.doc.cues()[0].text == "你好。"); CHECK(r.doc.cues()[0].edited);
    CHECK(r.doc.state(1) == SentenceState::UserEdited);
}
void refined_merge()
{
    Rig r; r.fixer = SentenceFixer(looseRules(true));
    r.merger.onFinal(final(1, 0, 1000, "甲")); r.merger.onFinal(final(2, 1100, 2100, "乙"));
    CHECK(r.doc.cues().size() == 1);
    auto f = final(2, 1100, 2100, "丙。"); f.pass = 2; r.merger.onRefined(f);
    CHECK(r.doc.cues().size() == 1); CHECK(r.doc.cues()[0].text == "甲丙。");
    CHECK(r.doc.state(2) == SentenceState::Refined);
    auto first = final(1, 0, 1000, "丁。"); first.pass = 2; r.merger.onRefined(first);
    CHECK(r.doc.cues().size() == 2); CHECK(r.doc.cues()[0].text == "丁。"); CHECK(r.doc.cues()[1].text == "丙。");
    Rig dropped; dropped.merger.onFinal(final(1, 0, 1000));
    f = final(1, 0, 1000, "谢谢观看"); f.pass = 2; const auto rev = dropped.doc.revision();
    dropped.merger.onRefined(f); CHECK(dropped.doc.revision() == rev); CHECK(dropped.doc.cues()[0].text == "你好。");
}
int main(int argc, char **argv)
{
    try {
        CHECK(argc == 2); const std::string name = argv[1];
        if (name == "window") window(); else if (name == "merge_boundaries") merge_boundaries();
        else if (name == "finished") finished(); else if (name == "user_edited") user_edited();
        else if (name == "refined_merge") refined_merge(); else CHECK(false);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
