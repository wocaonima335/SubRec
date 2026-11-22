// 协议编解码单测：每种消息 encode/decode 往返、超长行、非 JSON、未知 type、缺必需字段、类型错误、
// 可选字段缺省、未知字段忽略、Unicode 转义、acceptedByRole。

#include "test_framework.h"

#include "subtitle/host/asr_protocol.h"

#include <cmath>
#include <string>

using namespace subtitle;
using namespace subtitle::proto;

namespace {

// round-trip 后按字段比较（decode 成功路径不触碰 Status；失败时打印错误信息）
Message roundtrip(const Message &m)
{
    const std::string line = encode(m);
    CHECK_MSG(line.find('\n') == std::string::npos, "encode 结果不含换行");
    CHECK_MSG(!line.empty() && line.front() == '{' && line.back() == '}', "encode 是单行 JSON 对象");
    Status st;
    auto out = decode(line, &st);
    CHECK_MSG(out.has_value(), "decode failed: " + st.message);
    return *out;
}

} // namespace

TEST(proto_roundtrip_all_messages)
{
    {   // hello（主→子）
        Hello h;
        h.protocol = 2;
        h.role = kRoleRefine;
        Message m = roundtrip(Hello{h});
        auto *r = std::get_if<Hello>(&m);
        CHECK(r && r->protocol == 2 && r->role == kRoleRefine);
    }
    {   // hello（子→主）
        Hello h;
        h.protocol = 2;
        h.version = "1.2.3";
        h.backends = {"cpu", "vulkan"};
        Hello::Gpu g{"RTX 5080", 16384, "vulkan"};
        h.gpus = {g};
        Message m = roundtrip(Hello{h});
        auto *r = std::get_if<Hello>(&m);
        CHECK(r && r->version == "1.2.3");
        CHECK(r->backends.size() == 2 && r->backends[1] == "vulkan");
        CHECK(r->gpus.size() == 1 && r->gpus[0].vramMB == 16384 && r->gpus[0].api == "vulkan");
    }
    {   // open / append / splice / setLevel / pause / resume / finish / cancel / quit
        Open op;
        op.jobId = 9;
        op.spoolPath = "/tmp/中文 路径/spool.wav";
        op.resumeFrom16k = 16000;
        op.committed16k = 32000;
        op.live = false;
        Message mo = roundtrip(Message{op});
        auto *r = std::get_if<Open>(&mo);
        CHECK(r && r->jobId == 9 && r->spoolPath == op.spoolPath && r->resumeFrom16k == 16000
              && r->committed16k == 32000 && r->live == false);

        Message ma = roundtrip(Message{Append{5, 123456}});
        auto *ra = std::get_if<Append>(&ma);
        CHECK(ra && ra->jobId == 5 && ra->committed16k == 123456);

        Message ms = roundtrip(Message{Splice{7, 999999}});
        auto *rs = std::get_if<Splice>(&ms);
        CHECK(rs && rs->jobId == 7 && rs->at16k == 999999);

        SetLevel sl;
        sl.jobId = 4;
        sl.level = 2;
        sl.threads = 1;
        sl.partialIntervalMs = 0;
        Message ml = roundtrip(Message{sl});
        auto *rl = std::get_if<SetLevel>(&ml);
        CHECK(rl && rl->jobId == 4 && rl->level == 2 && rl->threads == 1 && rl->partialIntervalMs == 0);

        Message mp0 = roundtrip(Message{Pause{3}});
        CHECK(std::get_if<Pause>(&mp0)->jobId == 3);
        Message mr0 = roundtrip(Message{Resume{3}});
        CHECK(std::get_if<Resume>(&mr0)->jobId == 3);
        Message mf = roundtrip(Message{Finish{8, 480000}});
        auto *rf = std::get_if<Finish>(&mf);
        CHECK(rf && rf->jobId == 8 && rf->total16k == 480000);
        Message mc = roundtrip(Message{Cancel{8}});
        CHECK(std::get_if<Cancel>(&mc)->jobId == 8);
        Message mq = roundtrip(Message{Quit{}});
        CHECK(std::holds_alternative<Quit>(mq));
    }
    {   // refine
        Refine rf;
        rf.jobId = 2;
        rf.spoolPath = "spool.wav";
        rf.segments = {{0, 0, 16000}, {5, 16000, 32000}};
        rf.prompt = "热词 initial prompt";
        auto m = roundtrip(Message{rf});
        auto *r = std::get_if<Refine>(&m);
        CHECK(r && r->jobId == 2 && r->segments.size() == 2 && r->segments[1].id == 5
              && r->segments[1].b16k == 16000 && r->segments[1].e16k == 32000 && r->prompt == rf.prompt);
    }
    {   // loaded / hb / busy / progress / done / cancelled / error / log
        Message m = roundtrip(Message{Loaded{"cpu", 42, "gpu fallback"}});
        auto *r = std::get_if<Loaded>(&m);
        CHECK(r && r->backend == "cpu" && r->loadMs == 42 && r->fallbackReason == "gpu fallback");

        Message mh = roundtrip(Message{Heartbeat{17, "decoding", 233}});
        auto *rh = std::get_if<Heartbeat>(&mh);
        CHECK(rh && rh->seq == 17 && rh->state == "decoding" && rh->rssMB == 233);

        Message mb = roundtrip(Message{Busy{6, "final", 16000, 32000}});
        auto *rb = std::get_if<Busy>(&mb);
        CHECK(rb && rb->jobId == 6 && rb->kind == "final" && rb->b16k == 16000 && rb->e16k == 32000);

        Message mp = roundtrip(Message{Progress{6, 32000, 123.5}});
        auto *rp = std::get_if<Progress>(&mp);
        CHECK(rp && rp->jobId == 6 && rp->done16k == 32000 && std::fabs(rp->decodeMsTotal - 123.5) < 1e-9);

        Message md = roundtrip(Message{Done{6}});
        CHECK(std::get_if<Done>(&md)->jobId == 6);
        Message mcd = roundtrip(Message{Cancelled{6}});
        CHECK(std::get_if<Cancelled>(&mcd)->jobId == 6);

        Message me = roundtrip(Message{Error{0, 303, "load failed \"bad\" model"}});
        auto *re = std::get_if<Error>(&me);
        CHECK(re && re->jobId == 0 && re->code == 303 && re->message == "load failed \"bad\" model");

        Message mg = roundtrip(Message{Log{"warn", "line1\nline2\ttab"}});
        auto *rg = std::get_if<Log>(&mg);
        CHECK(rg && rg->level == "warn" && rg->text == "line1\nline2\ttab");
    }
}

TEST(proto_roundtrip_load_engine_request)
{
    Load ld;
    ld.request.engineId = "sherpa-onnx";
    ld.request.modelId = "sense-voice-small";
    ld.request.modelDir = "C:\\模型\\dir";
    ld.request.language = "zh";
    ld.request.threads = 2;
    ld.request.backend = Backend::Vulkan;
    ld.request.vad.threshold = 0.6f;
    ld.request.vad.minSpeechMs = 250;
    ld.request.vad.minSilenceMs = 700;
    ld.request.vad.speechPadMs = 150;
    ld.request.vad.maxSegmentMs = 10000;
    ld.request.initialPrompt = "热词";
    ld.live.partialIntervalMs = 800;
    ld.live.maxSegmentMs = 10000;
    ld.live.endpointSilenceMs = 500;
    ld.live.appendThrottleMs = 200;
    ld.live.partFlushIntervalMs = 5000;
    ld.live.threadsWhileRecording = 1;
    ld.live.agreementN = 3;
    ld.live.confirmGuardMs = 400;
    ld.live.writeConfirmedToPart = true;
    ld.live.provisionalMinSegmentMs = 3500;
    ld.live.revisableCues = 2;
    ld.fakeOptions = "rtf=0.1,seed=7";
    auto m = roundtrip(Message{ld});
    auto *r = std::get_if<Load>(&m);
    CHECK(r != nullptr);
    const EngineRequest &q = r->request;
    CHECK(q.engineId == "sherpa-onnx" && q.modelId == "sense-voice-small");
    CHECK(q.modelDir.u8string() == "C:\\模型\\dir");
    CHECK(q.language == "zh" && q.threads == 2 && q.backend == Backend::Vulkan);
    CHECK(std::fabs(q.vad.threshold - 0.6f) < 1e-6f && q.vad.minSilenceMs == 700
          && q.vad.speechPadMs == 150 && q.vad.maxSegmentMs == 10000);
    CHECK(q.initialPrompt == "热词");
    const LiveConfig &c = r->live;
    CHECK(c.partialIntervalMs == 800 && c.maxSegmentMs == 10000 && c.endpointSilenceMs == 500
          && c.appendThrottleMs == 200 && c.partFlushIntervalMs == 5000 && c.threadsWhileRecording == 1
          && c.agreementN == 3 && c.confirmGuardMs == 400 && c.writeConfirmedToPart
          && c.provisionalMinSegmentMs == 3500 && c.revisableCues == 2);
    CHECK(r->fakeOptions == "rtf=0.1,seed=7");
}

TEST(proto_roundtrip_partial_tokens)
{
    PartialMsg pm;
    pm.seg.job = 7;
    pm.seg.id = 3;
    pm.seg.begin16k = 1600;
    pm.seg.upto16k = 25600;
    pm.seg.text = "你好世界";
    pm.seg.tokens = {{100, 500, "你", -1.f}, {500, 900, "好", -1.f}};
    pm.seg.seq = 4;
    auto m = roundtrip(Message{pm});
    auto *r = std::get_if<PartialMsg>(&m);
    CHECK(r);
    const PartialSegment &s = r->seg;
    CHECK(s.job == 7 && s.id == 3 && s.begin16k == 1600 && s.upto16k == 25600 && s.seq == 4);
    CHECK(s.text == "你好世界" && s.tokens.size() == 2);
    CHECK(s.tokens[0].text == "你" && s.tokens[0].beginMs == 100 && s.tokens[0].endMs == 500);
}

TEST(proto_roundtrip_final_words_prob)
{
    FinalMsg fm;
    fm.seg.job = 7;
    fm.seg.id = 2;
    fm.seg.begin16k = 0;
    fm.seg.end16k = 18272;
    fm.seg.text = "第2段";
    fm.seg.language = "zh";
    fm.seg.emotion = "HAPPY";
    fm.seg.event = "BGM";
    fm.seg.words = {{0, 381, "第", 0.9f}, {381, 761, "2", -1.f}};
    fm.seg.forcedCut = true;
    fm.seg.decodeMs = 12.5f;
    fm.seg.pass = 1;
    fm.seg.modelId = "fake";
    auto m = roundtrip(Message{fm});
    auto *r = std::get_if<FinalMsg>(&m);
    CHECK(r);
    const FinalSegment &s = r->seg;
    CHECK(s.job == 7 && s.id == 2 && s.begin16k == 0 && s.end16k == 18272);
    CHECK(s.text == "第2段" && s.language == "zh" && s.emotion == "HAPPY" && s.event == "BGM");
    CHECK(s.words.size() == 2);
    CHECK(s.words[0].text == "第" && s.words[0].prob >= 0.8999f && s.words[0].prob <= 0.9001f);
    CHECK(s.words[1].prob < 0);    // p 缺省 = 无置信度
    CHECK(s.forcedCut && std::fabs(s.decodeMs - 12.5f) < 1e-3f && s.pass == 1 && s.modelId == "fake");
}

TEST(proto_decode_line_too_long)
{
    Status st;
    auto m = decode(std::string(kMaxLineBytes + 1, 'a'), &st);
    CHECK(!m.has_value());
    CHECK(st.code == ErrorCode::HostProtocolError);
    // 恰好等于上限不算超长（会因非 JSON 失败，但不是超长错误路径）
    auto m2 = decode(std::string(kMaxLineBytes, 'a'), &st);
    CHECK(!m2.has_value());
    CHECK(st.code == ErrorCode::HostProtocolError);
}

TEST(proto_decode_not_json)
{
    Status st;
    for (std::string_view bad : {"", "not json", "{", "{\"type\":", "[]", "\"str\"", "42",
                                 "{\"type\":\"log\",\"level\":\"info\",\"text\":\"x\"}extra",
                                 "{\"type\":\"error\",\"code\":1e999}"}) {
        st = Status{};
        auto m = decode(bad, &st);
        CHECK(!m.has_value());
        CHECK_MSG(st.code == ErrorCode::HostProtocolError, std::string(bad));
    }
}

TEST(proto_decode_unknown_type)
{
    Status st;
    auto m = decode("{\"type\":\"brand-new-msg\",\"jobId\":1}", &st);
    CHECK(!m.has_value());
    CHECK(st.code == ErrorCode::NotSupported);
    // type 缺失 / 非字符串 → 协议错误
    auto m2 = decode("{\"jobId\":1}", &st);
    CHECK(!m2.has_value() && st.code == ErrorCode::HostProtocolError);
    auto m3 = decode("{\"type\":42}", &st);
    CHECK(!m3.has_value() && st.code == ErrorCode::HostProtocolError);
}

TEST(proto_decode_missing_required_fields)
{
    struct Case {
        std::string json;
        std::string what;
    };
    const Case cases[] = {
        {"{\"type\":\"hello\"}", "hello.protocol"},
        {"{\"type\":\"load\"}", "load.request"},
        {"{\"type\":\"load\",\"request\":{}}", "request.engineId"},
        {"{\"type\":\"open\",\"spoolPath\":\"x\"}", "open.jobId"},
        {"{\"type\":\"open\",\"jobId\":1}", "open.spoolPath"},
        {"{\"type\":\"append\",\"jobId\":1}", "append.committed16k"},
        {"{\"type\":\"splice\",\"jobId\":1}", "splice.at16k"},
        {"{\"type\":\"setLevel\",\"jobId\":1}", "setLevel.level"},
        {"{\"type\":\"finish\",\"jobId\":1}", "finish.total16k"},
        {"{\"type\":\"loaded\"}", "loaded.backend"},
        {"{\"type\":\"hb\",\"seq\":1}", "hb.state"},
        {"{\"type\":\"busy\",\"jobId\":1,\"b16k\":0,\"e16k\":1}", "busy.kind"},
        {"{\"type\":\"partial\",\"jobId\":1,\"id\":0,\"b16k\":0}", "partial.upto16k"},
        {"{\"type\":\"final\",\"jobId\":1,\"id\":0,\"e16k\":1}", "final.b16k"},
        {"{\"type\":\"progress\",\"jobId\":1}", "progress.done16k"},
        {"{\"type\":\"error\"}", "error.code"},
        {"{\"type\":\"log\",\"level\":\"info\"}", "log.text"},
        {"{\"type\":\"refine\",\"jobId\":1,\"segments\":[]}", "refine.spoolPath"},
        {"{\"type\":\"refine\",\"jobId\":1,\"spoolPath\":\"x\"}", "refine.segments"},
        {"{\"type\":\"refine\",\"jobId\":1,\"spoolPath\":\"x\",\"segments\":[{\"id\":0,\"b16k\":0}]}",
         "refine.segment.e16k"},
    };
    for (const Case &c : cases) {
        Status st;
        auto m = decode(c.json, &st);
        CHECK_MSG(!m.has_value(), c.what);
        CHECK_MSG(st.code == ErrorCode::HostProtocolError, c.what + ": " + st.message);
    }
}

TEST(proto_decode_bad_field_types)
{
    struct Case {
        std::string json;
        std::string what;
    };
    const Case cases[] = {
        {"{\"type\":\"hello\",\"protocol\":\"2\"}", "protocol string"},
        {"{\"type\":\"hello\",\"protocol\":2.5}", "protocol fraction"},
        {"{\"type\":\"open\",\"jobId\":\"7\",\"spoolPath\":\"x\"}", "jobId string"},
        {"{\"type\":\"open\",\"jobId\":7,\"spoolPath\":123}", "spoolPath number"},
        {"{\"type\":\"open\",\"jobId\":7,\"spoolPath\":\"x\",\"live\":\"yes\"}", "live string"},
        {"{\"type\":\"busy\",\"jobId\":1,\"kind\":5,\"b16k\":0,\"e16k\":1}", "kind number"},
        {"{\"type\":\"final\",\"jobId\":1,\"id\":0,\"b16k\":0,\"e16k\":1,\"words\":\"none\"}",
         "words not array"},
        {"{\"type\":\"final\",\"jobId\":1,\"id\":0,\"b16k\":0,\"e16k\":1,\"words\":[{\"t\":\"a\",\"b\":0}]}",
         "word missing e"},
        {"{\"type\":\"load\",\"request\":{\"engineId\":\"fake\"},\"live\":5}", "live number"},
        {"{\"type\":\"load\",\"request\":{\"engineId\":\"fake\",\"backend\":\"directml\"}}",
         "unknown backend"},
        {"{\"type\":\"append\",\"jobId\":1,\"committed16k\":9007199254740993}",
         "beyond 2^53"},   // double 精度之外（解析层面即丢失整性或超限）
    };
    for (const Case &c : cases) {
        Status st;
        auto m = decode(c.json, &st);
        CHECK_MSG(!m.has_value(), c.what);
        CHECK_MSG(st.code == ErrorCode::HostProtocolError, c.what + ": " + st.message);
    }
}

TEST(proto_decode_optional_defaults)
{
    Status st;
    auto m = decode("{\"type\":\"load\",\"request\":{\"engineId\":\"fake\"}}", &st);
    CHECK(m.has_value());
    auto *ld = std::get_if<Load>(&*m);
    CHECK(ld);
    CHECK(ld->request.language == "zh" && ld->request.threads == 1
          && ld->request.backend == Backend::Auto);
    CHECK(ld->request.vad.minSilenceMs == 500 && ld->request.vad.threshold == 0.5f);
    CHECK(ld->live.partialIntervalMs == 1000 && ld->live.maxSegmentMs == 10000);
    CHECK(ld->fakeOptions.empty());

    auto m2 = decode("{\"type\":\"open\",\"jobId\":3,\"spoolPath\":\"s.wav\"}", &st);
    CHECK(m2.has_value());
    auto *op = std::get_if<Open>(&*m2);
    CHECK(op && op->resumeFrom16k == 0 && op->committed16k == 0 && op->live);

    auto m3 = decode("{\"type\":\"final\",\"jobId\":3,\"id\":0,\"b16k\":0,\"e16k\":16000}", &st);
    CHECK(m3.has_value());
    auto *fin = std::get_if<FinalMsg>(&*m3);
    CHECK(fin);
    CHECK(fin->seg.text.empty() && fin->seg.language.empty() && !fin->seg.forcedCut
          && fin->seg.decodeMs == 0.f && fin->seg.pass == 1 && fin->seg.modelId.empty()
          && fin->seg.words.empty());
}

TEST(proto_decode_unknown_fields_ignored)
{
    Status st;
    auto m = decode("{\"type\":\"append\",\"jobId\":5,\"committed16k\":99,"
                    "\"futureField\":{\"deep\":[1,2]},\"another\":true}",
                    &st);
    CHECK(m.has_value());
    auto *ap = std::get_if<Append>(&*m);
    CHECK(ap && ap->jobId == 5 && ap->committed16k == 99);
}

TEST(proto_decode_unicode_escapes)
{
    Status st;
    auto m = decode("{\"type\":\"log\",\"level\":\"info\",\"text\":\"\\u4e2d\\u6587 \\ud83d\\ude00 "
                    "slash\\/ \\u0041\"}",
                    &st);
    CHECK(m.has_value());
    auto *lg = std::get_if<Log>(&*m);
    CHECK(lg);
    const char *expected = "中文 \xF0\x9F\x98\x80 slash/ A";
    CHECK(lg->text == expected);
    // 孤立代理 → 协议错误
    auto bad = decode("{\"type\":\"log\",\"level\":\"i\",\"text\":\"\\ud83d\"}", &st);
    CHECK(!bad.has_value() && st.code == ErrorCode::HostProtocolError);
}

TEST(proto_accepted_by_role)
{
    CHECK(acceptedByRole(Message{Open{}}, kRoleLive));
    CHECK(!acceptedByRole(Message{Refine{}}, kRoleLive));
    CHECK(!acceptedByRole(Message{Open{}}, kRoleRefine));
    CHECK(acceptedByRole(Message{Hello{}}, kRoleRefine));
    CHECK(acceptedByRole(Message{Load{}}, kRoleRefine));
    CHECK(acceptedByRole(Message{Refine{}}, kRoleRefine));
    CHECK(acceptedByRole(Message{Cancel{}}, kRoleRefine));
    CHECK(acceptedByRole(Message{Quit{}}, kRoleRefine));
    CHECK(!acceptedByRole(Message{Finish{}}, kRoleRefine));
}
