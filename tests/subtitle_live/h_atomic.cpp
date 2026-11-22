#include "h_test_support.h"
namespace subtitle::testing { extern thread_local int64_t atomicFailAfter; }
int main()
{
    try {
        Temp temp("atomic"); const auto path = temp.dir / "x.srt.part"; Rig r;
        r.merger.onFinal(final(1, 0, 1000)); CHECK(r.merger.flushPart(path, {}).ok());
        const auto original = read(path); r.merger.onFinal(final(2, 2000, 3000));
        // B's existing seam fails the real writer after bytes have reached .tmp.
        testing::atomicFailAfter = 17;
        const auto status = r.merger.flushPart(path, {});
        testing::atomicFailAfter = -1;
        CHECK(status.code == ErrorCode::SrtWriteFailed); CHECK(read(path) == original);
        CHECK(!std::filesystem::exists(path.string() + ".tmp")); CHECK(r.merger.shouldFlushPart(0));
        CHECK(r.merger.flushPart(path, {}).ok()); CHECK(parse(path).size() == 2); CHECK(!r.merger.shouldFlushPart(0));
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
