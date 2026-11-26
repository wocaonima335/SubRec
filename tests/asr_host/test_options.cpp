// 命令行 / --fake 参数解析与假引擎门控测试。

#include "host_driver.h"
#include "test_framework.h"

#include "engine_fake.h"
#include "host_options.h"

#include "subtitle/host/asr_protocol.h"

using namespace asrhost;
using namespace subtitle;

TEST(opts_parse_host_arguments)
{
    HostOptions o;
    Status st;
    const char *argv[] = {"subrec-asr",
                          "--role=refine",
                          "--parent-pid=1234",
                          "--engine=fake",
                          "--fake=rtf=0.2,seed=9",
                          "--log-level=debug"};
    CHECK(parseHostOptions(6, argv, o, &st));
    CHECK(o.role == "refine" && o.parentPid == 1234 && o.engine == "fake");
    CHECK(o.fakeOptions == "rtf=0.2,seed=9" && o.logLevel == "debug" && !o.showVersion);

    const char *argv2[] = {"subrec-asr", "--version"};
    CHECK(parseHostOptions(2, argv2, o, &st) && o.showVersion);

    const char *argv3[] = {"subrec-asr"};   // 全部缺省
    CHECK(parseHostOptions(1, argv3, o, &st));
    CHECK(o.role == "live" && o.parentPid == 0 && o.engine.empty() && o.logLevel == "info");

    const char *bad[] = {"subrec-asr", "--nonsense"};
    CHECK(!parseHostOptions(2, bad, o, &st) && st.code == ErrorCode::InvalidArgument);

    const char *badRole[] = {"subrec-asr", "--role=worker"};
    CHECK(!parseHostOptions(2, badRole, o, &st) && st.code == ErrorCode::InvalidArgument);
}

TEST(opts_fake_parse_all_keys)
{
    FakeEngineOptions o;
    Status st;
    CHECK(parseFakeOptions(
        "rtf=0.25,crashAtSeg=3,hangAtSeg=4,hangAll=5,oomAtSeg=6,illegalInstr,garbageStdout,"
        "loadFail=303,partialNoise=0.5,partialRegress=2,finalDiffers=0.75,lang=mix,"
        "refineRtf=1.5,refineEmpty=0.25,seed=77",
        o, &st));
    CHECK(std::fabs(o.rtf - 0.25) < 1e-9 && o.crashAtSeg == 3 && o.hangAtSeg == 4);
    CHECK(o.hangAll == 5 && o.oomAtSeg == 6 && o.illegalInstr && o.garbageStdout);
    CHECK(o.loadFail == 303 && std::fabs(o.partialNoise - 0.5) < 1e-9 && o.partialRegress == 2);
    CHECK(std::fabs(o.finalDiffers - 0.75) < 1e-9 && o.lang == "mix");
    CHECK(std::fabs(o.refineRtf - 1.5) < 1e-9 && std::fabs(o.refineEmpty - 0.25) < 1e-9
          && o.seed == 77);

    // 空串 → 全部默认
    FakeEngineOptions d;
    CHECK(parseFakeOptions("", d, &st));
    CHECK(d.rtf == 0 && d.crashAtSeg == -1 && !d.illegalInstr && d.lang == "zh" && d.seed == 1);

    // 同键重复：后者为准
    CHECK(parseFakeOptions("rtf=1,rtf=2", d, &st) && std::fabs(d.rtf - 2) < 1e-9);
}

TEST(opts_fake_parse_errors)
{
    FakeEngineOptions o;
    Status st;
    CHECK(!parseFakeOptions("bogus=1", o, &st) && st.code == ErrorCode::InvalidArgument);
    CHECK(!parseFakeOptions("rtf", o, &st));
    CHECK(!parseFakeOptions("rtf=abc", o, &st));
    CHECK(!parseFakeOptions("crashAtSeg=1.5", o, &st));
    CHECK(!parseFakeOptions("lang=fr", o, &st));
    CHECK(!parseFakeOptions("seed=-1", o, &st));
    // 未知键的报错信息包含出错的键
    CHECK(parseFakeOptions("what=1", o, &st) == false && st.message.find("what") != std::string::npos);
}

TEST(opts_fake_format_roundtrip)
{
    FakeEngineOptions o;
    Status st;
    const std::string text = "rtf=0.5,crashAtSeg=2,lang=en,seed=3";
    CHECK(parseFakeOptions(text, o, &st));
    const std::string formatted = formatFakeOptions(o);
    FakeEngineOptions o2;
    CHECK(parseFakeOptions(formatted, o2, &st));
    CHECK(std::fabs(o2.rtf - o.rtf) < 1e-9 && o2.crashAtSeg == o.crashAtSeg && o2.lang == o.lang
          && o2.seed == o.seed);
}

TEST(opts_fake_engine_requires_hooks)
{
    // 子进程不设置 SUBREC_ASR_TEST_HOOKS：engineId=fake → EngineUnknown(300)（§8）
    HostSession s;
    CHECK(s.proc.spawn(asrExe(), {"--engine=fake"}, {}, {"SUBREC_ASR_TEST_HOOKS"}));
    s.handshake();
    proto::Load ld;
    ld.request.engineId = "fake";
    s.send(ld);
    proto::Message m;
    CHECK_MSG(s.waitFor("error", m, 5000), "expected error for fake without hooks");
    auto *err = std::get_if<proto::Error>(&m);
    CHECK(err);
    CHECK_MSG(err->code == static_cast<int>(ErrorCode::EngineUnknown), "EngineUnknown(300)");
    s.quit();
    auto exit = s.proc.waitForExit(5000);
    CHECK(!exit.timedOut && exit.code == 0);
}
