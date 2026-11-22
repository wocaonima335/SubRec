#pragma once
#include "subtitle/live/local_agreement.h"
#include "subtitle/live/sentence_fixer.h"
#include "subtitle/live/subtitle_merger.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(expr) do { if (!(expr)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #expr); } while (false)
using namespace subtitle;
struct Temp {
    std::filesystem::path dir;
    explicit Temp(const std::string &name) {
        dir = std::filesystem::temp_directory_path() / ("subrec-h-" + name + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        CHECK(std::filesystem::create_directory(dir));
    }
    ~Temp() { std::error_code ec; std::filesystem::remove_all(dir, ec); }
};
inline std::string read(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary); CHECK(in.good());
    return {std::istreambuf_iterator<char>(in), {}};
}
inline FinalSegment final(SegId id, int64_t begin, int64_t end, std::string text = "你好。") {
    FinalSegment f; f.job = 7; f.id = id; f.begin16k = begin * 16; f.end16k = end * 16; f.text = std::move(text); return f;
}
inline PartialSegment partial(SegId id = 1, int64_t upto = 4000) {
    PartialSegment p; p.job = 7; p.id = id; p.upto16k = upto * 16;
    p.tokens = {{0, 1000, "你"}, {1000, 2000, "好"}, {2000, 2000, "，"}, {2000, 3900, "世"}};
    p.text = "你好，世"; return p;
}
inline FixerRules looseRules(bool merge = false) {
    FixerRules rules; rules.mergeShortCues = merge;
    rules.hallucination.maxCharsPerSec = 1000; rules.hallucination.minCharsPerSec = 0;
    rules.hallucination.blacklist = {"谢谢观看"}; return rules;
}
struct Rig {
    SubtitleDocument doc;
    SentenceFixer fixer{looseRules()};
    LocalAgreement agreement{2, 300};
    ManualClock clock;
    SubtitleMerger merger{doc, CueConfig{}, fixer, agreement, clock};
};
inline std::vector<Cue> parse(const std::filesystem::path &p) {
    std::vector<Cue> cues; CHECK(parseSrt(read(p), cues)); return cues;
}
