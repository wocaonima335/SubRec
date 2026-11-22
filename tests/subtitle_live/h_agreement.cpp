#include "h_test_support.h"

void timing()
{
    struct Row { int n; std::vector<std::string> expected; };
    for (const auto &row : std::vector<Row>{{2, {"", "你好，", "你好，"}}, {3, {"", "", "你好，"}}}) {
        LocalAgreement la(row.n, 300); auto p = partial();
        for (const auto &text : row.expected) { la.push(p); CHECK(la.state().confirmedText == text); }
        CHECK(la.stats().confirmedTokens == 3);
    }
    CHECK(LocalAgreement(0, 300).n() == 2); CHECK(LocalAgreement(10, 300).n() == 3);
}
void guard()
{
    for (auto end : {699, 700, 701}) {
        LocalAgreement la(2, 300); auto p = partial(1, 1000);
        p.tokens = {{0, end, "字"}}; p.text = "字";
        la.push(p); la.push(p); CHECK(la.state().confirmedText == (end <= 700 ? "字" : ""));
        p.upto16k = 1001 * 16; la.push(p); CHECK(la.state().confirmedText == "字");
    }
}
void punctuation()
{
    LocalAgreement la(2, 300); auto p = partial();
    p.tokens = {{0, 500, "ＨＥＬＬＯ"}, {500, 600, "，"}, {600, 800, "world"}};
    la.push(p);
    p.tokens = {{0, 500, "hello"}, {500, 600, "！"}, {600, 800, "WORLD"}, {800, 850, "."}};
    const auto added = la.push(p);
    CHECK(added.size() == 4); CHECK(la.state().confirmedText == "hello！WORLD.");
    CHECK(la.state().confirmedUpto16k == 850 * 16);
    CHECK(la.push(p).empty());
}
void regression()
{
    LocalAgreement la(2, 300); auto p = partial(); la.push(p); la.push(p);
    p.tokens[0].text = "他";
    for (int i = 1; i <= 3; ++i) {
        CHECK(la.push(p).empty()); CHECK(la.state().confirmedText == "你好，"); CHECK(la.stats().regressions == uint64_t(i));
    }
    p.tokens[0].text = "你"; p.upto16k = 4500 * 16;
    la.push(p); la.push(p); CHECK(la.state().confirmedText == "你好，世");
}
void freeze()
{
    LocalAgreement la(2, 300); auto p = partial(); la.push(p); la.push(p); la.freeze();
    p.upto16k = 5000 * 16;
    CHECK(la.push(p).empty()); CHECK(la.state().confirmedText == "你好，");
    CHECK(la.state().tentativeText.empty()); CHECK(la.state().frozen);
    p.id = 2; la.push(p); CHECK(!la.state().frozen); CHECK(la.state().confirmed.empty());
    la.push(p); CHECK(la.state().confirmedText == "你好，世");
}
void mismatch()
{
    struct Row { std::string a, b; int distance; };
    for (const auto &row : std::vector<Row>{{"你好", "你号世界", 1}, {"甲乙丙", "乙丙甲", 2}, {"你好", "你", 1}, {"你好", "你好世界", 0}}) {
        LocalAgreement la(2, 300); auto p = partial(); p.tokens.clear(); p.text = row.a;
        la.push(p); la.push(p); auto f = final(1, 0, 4000, row.b);
        CHECK(la.mismatchAgainst(f) == row.distance); la.recordFinal(f);
        CHECK(la.stats().mismatchChars == uint64_t(row.distance));
        CHECK(la.stats().finalChars == countCodePoints(row.b));
        la.reset(2); CHECK(la.stats().mismatchChars == uint64_t(row.distance));
    }
}
void compatibility()
{
    LocalAgreement exact(2, 300);
    auto literal = partial(); literal.tokens.clear(); literal.text = " ，! ";
    exact.push(literal); exact.push(literal); CHECK(exact.state().confirmedText == literal.text);
    for (int n : {2, 3}) {
        LocalAgreement la(n, 100000); auto p = partial(1, 100); p.tokens.clear(); p.text = "你好，世";
        for (int i = 1; i <= n; ++i) { la.push(p); CHECK(la.state().confirmedText == (i == n ? p.text : "")); }
        p.text += "界"; la.push(p); CHECK(la.state().tentativeText == "界");
        la.reset(2); CHECK(la.state().confirmed.empty());
    }
}
void resets()
{
    Rig r; auto p = partial(); r.merger.onPartial(p); r.merger.onPartial(p);
    CHECK(!r.agreement.state().confirmed.empty());
    p.id = 2; r.merger.onPartial(p); CHECK(r.agreement.state().confirmed.empty());
    r.merger.onPartial(p); r.merger.onSplice(2000 * 16);
    CHECK(r.agreement.state().confirmed.empty()); CHECK(!r.merger.current());
    r.merger.onPartial(p); CHECK(r.agreement.state().confirmed.empty()); r.merger.onPartial(p);
    r.merger.onFinal(final(2, 0, 4000)); CHECK(r.agreement.state().confirmed.empty());
    CHECK(r.agreement.stats().finalChars == 3); CHECK(r.merger.current()->confirmedText == "你好。");
    r.clock.advance(3000); CHECK(!r.merger.current());
    p.id = 3; p.begin16k = 4000 * 16; p.upto16k = 8000 * 16;
    r.merger.onPartial(p); r.merger.onPartial(p); r.merger.onHostRestarted();
    CHECK(!r.merger.current()); CHECK(r.agreement.state().confirmed.empty());
    r.merger.onPartial(p); CHECK(r.agreement.state().confirmed.empty());
}
int main(int argc, char **argv)
{
    try {
        CHECK(argc == 2); const std::string name = argv[1];
        if (name == "timing") timing(); else if (name == "guard") guard();
        else if (name == "punctuation") punctuation(); else if (name == "regression") regression();
        else if (name == "freeze") freeze(); else if (name == "mismatch") mismatch();
        else if (name == "compatibility") compatibility(); else if (name == "resets") resets(); else CHECK(false);
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
