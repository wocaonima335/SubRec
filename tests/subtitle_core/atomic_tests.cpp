#include "test_support.h"
#include "subtitle/core/subtitle_writers.h"
namespace subtitle::testing { extern thread_local int64_t atomicFailAfter; }
int main()
{
    using namespace subtitle;
    try {
        Temp temp("midwrite"); const auto path = temp.dir / "original.srt";
        const std::string original = "original complete subtitle\r\n";
        CHECK(writeFileAtomic(path, original).ok());
        // The real open/write/close path runs; the failure occurs after 17 bytes
        // have been written to .tmp, before flush/replace. This also works as root.
        testing::atomicFailAfter = 17;
        const auto status = writeFileAtomic(path, std::string(200000, 'x'));
        testing::atomicFailAfter = -1;
        CHECK(status.code == ErrorCode::SrtWriteFailed);
        CHECK(read(path) == original);
        CHECK(!std::filesystem::exists(path.string() + ".tmp"));
        CHECK(writeFileAtomic(path, "recovered").ok() && read(path) == "recovered");
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
