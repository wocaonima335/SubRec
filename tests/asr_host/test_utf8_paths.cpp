// UTF-8 非ASCII（中文）路径端到端：spool 与 fakehost 脚本都放在中文目录里。
// 覆盖整条链路：测试建目录/写文件（Windows 需转宽字符）→ spawn 的宽字符命令行（--script=中文）→
// wmain → UTF-8 argv → 协议里的 UTF-8 spoolPath → 宿主 _wfsopen 打开 spool。两平台路径字节一致。

#include "host_driver.h"
#include "test_framework.h"
#include "test_paths.h"
#include "wav_builder.h"

#include "subtitle/host/asr_protocol.h"

#include <cstdio>
#include <string>

using namespace subtitle;

namespace {

// 中文目录名（不含路径分隔符，目录建在当前工作目录=构建目录下）
const char *kDir = "中文目录-字幕测试";

} // namespace

TEST(utf8_path_chinese_spool_and_script)
{
    CHECK_MSG(testpaths::makeDirUtf8(kDir), "mkdir failed: " + std::string(kDir));

    // ---- 宿主：中文路径的 spool 走完整 live 流程（open → final → finish → done）----
    {
        const std::string spool = std::string(kDir) + "/音频-语音测试-0.wav";
        WavSpoolBuilder wav;
        CHECK_MSG(wav.create(spool), "create spool under Chinese dir");
        wav.appendSpeechMs(1000);
        wav.appendSilenceMs(1200);
        wav.flush();
        const int64_t total = wav.writtenSamples();

        HostSession s;
        s.start();
        CHECK_MSG(s.loadFake(0, 10000), "load fake");
        s.openJob(spool, 0, total);
        proto::Message m;
        CHECK_MSG(s.waitFor("final", m, 8000), "final via Chinese spool path");
        CHECK(std::get<proto::FinalMsg>(m).seg.text == "第0段");   // 真的读到了音频，不是碰巧通过
        s.finishJob(total);
        CHECK_MSG(s.waitFor("done", m, 8000), "done via Chinese spool path");
        s.quit();
        auto exit = s.proc.waitForExit(5000);
        CHECK(!exit.timedOut && !exit.signaled && exit.code == 0);
    }

    // ---- fakehost：中文路径的脚本经 --script= 传入（spawn 命令行转义 + _wfopen 读取）----
    {
        const std::string scriptPath = std::string(kDir) + "/脚本-回放-0.script";
        CHECK_MSG(testpaths::writeFileUtf8(scriptPath,
                                           "expect hello\n"
                                           "reply-hello\n"
                                           "expect quit\n"
                                           "exit 0\n"),
                  "write script under Chinese dir");
        HostSession s;
        CHECK(s.proc.spawn(asrFakeHostExe(), {"--script=" + scriptPath}));
        s.handshake();
        s.quit();
        auto exit = s.proc.waitForExit(5000);
        CHECK(!exit.timedOut && !exit.signaled && exit.code == 0);
    }
}
