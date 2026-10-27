#include "filter/audio_timeline_mixer.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <chrono>
#ifdef __unix__
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " line " + std::to_string(__LINE__)); } while (false)
namespace {
constexpr int64_t epoch = 123456000000;
AudioTimelineMixer::Frame pcm(int64_t offsetSamples, int samples, float left, float right,
                               int rate = 48000, int channels = 2) {
    AudioTimelineMixer::Frame f(av_frame_alloc()); CHECK(f);
    f->format = AV_SAMPLE_FMT_FLTP; f->sample_rate = rate;
    av_channel_layout_default(&f->ch_layout, channels);
    f->nb_samples = samples;
    // Exercise non-zero clock epoch and an input time base different from abuffer.
    f->pts = epoch + av_rescale_q(offsetSamples, {1,48000}, {1,1000000});
    f->time_base = {1,1000000}; CHECK(av_frame_get_buffer(f.get(), 0) >= 0);
    for (int ch = 0; ch < channels; ++ch) {
        auto *p = reinterpret_cast<float *>(f->extended_data[ch]);
        for (int i = 0; i < samples; ++i) p[i] = ch ? right : left;
    }
    return f;
}
float value(const AVFrame &f, int channel, int sample) {
    return reinterpret_cast<const float *>(f.extended_data[channel])[sample];
}
namespace fs = std::filesystem;
void dumpEnvironment(const fs::path &path) {
#ifdef _WIN32
    CHECK(_wputenv_s(L"SUBREC_AUDIO_MIX_DUMP_DIR", path.c_str()) == 0);
#else
    CHECK(setenv("SUBREC_AUDIO_MIX_DUMP_DIR", path.c_str(), 1) == 0);
#endif
}
struct DumpEnvironment {
    fs::path previous;
    DumpEnvironment() {
#ifdef _WIN32
        const auto *value = _wgetenv(L"SUBREC_AUDIO_MIX_DUMP_DIR");
#else
        const auto *value = std::getenv("SUBREC_AUDIO_MIX_DUMP_DIR");
#endif
        if (value) previous = value;
        dumpEnvironment({});
    }
    ~DumpEnvironment() { dumpEnvironment(previous); }
};
struct TempDumpDirectory {
    fs::path path = fs::temp_directory_path() /
        ("subrec-mix-dump-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDumpDirectory() { CHECK(fs::create_directory(path)); }
    ~TempDumpDirectory() { std::error_code ignored; fs::remove_all(path, ignored); }
};
std::string readText(const fs::path &path) {
    std::ifstream file(path, std::ios::binary); CHECK(file.good());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
std::vector<float> readRaw(const fs::path &path) {
    const auto bytes = readText(path);
    CHECK(bytes.size() % 4 == 0);
    std::vector<float> values;
    for (size_t i = 0; i < bytes.size(); i += 4) {
        uint32_t bits = 0;
        for (unsigned b = 0; b < 4; ++b) bits |= uint32_t(static_cast<unsigned char>(bytes[i+b])) << (8*b);
        float value; std::memcpy(&value, &bits, 4); values.push_back(value);
    }
    return values;
}
void debugDumpTimeline() {
    DumpEnvironment environment;
    TempDumpDirectory temporary;
    // Default is off: no side effects even when frames are rendered.
    { AudioTimelineMixer mixer(epoch); CHECK(mixer.render(480)); }
    CHECK(fs::is_empty(temporary.path));
    const auto dir = temporary.path / fs::u8path(u8"音频转储");
    dumpEnvironment(dir);
    std::vector<float> mixed;
    {
        AudioTimelineMixer mixer(epoch);
        auto system = pcm(0, 2400, .2f, .1f);
        auto mic = pcm(480, 960, .4f, .3f);
        mixer.push(0, *system); mixer.push(1, *mic); mixer.finish(0); mixer.finish(1);
        for (int64_t pts = 0; pts < 2880; pts += 480) {
            const bool keep = pts != 960; // A pause removes this interval from output.
            auto out = mixer.render(pts + 480, .5, .5, keep);
            if (keep) for (int n = 0; n < out->nb_samples; ++n)
                for (int ch = 0; ch < 2; ++ch) mixed.push_back(value(*out, ch, n));
        }
    } // joins the dump writer, flushes both files and finalizes JSON
    const auto session = fs::directory_iterator(dir)->path();
    auto sys = readRaw(session / "system.f32le");
    auto mic = readRaw(session / "microphone.f32le");
    CHECK(sys.size() == 2400 * 2 && mic.size() == sys.size() && mixed.size() == sys.size());
    for (size_t i = 0; i < sys.size(); ++i) {
        CHECK(std::abs(sys[i] + mic[i] - mixed[i]) < 1e-6);
        const int n = i / 2, ch = i % 2;
        CHECK(std::abs(sys[i] - (n < 1920 ? (ch ? .05f : .1f) : 0)) < 1e-6);
        CHECK(std::abs(mic[i] - (n >= 480 && n < 960 ? (ch ? .15f : .2f) : 0)) < 1e-6);
    }
    for (const auto *name : {"system.json", "microphone.json"}) {
        const auto json = readText(session / name);
        for (const auto *field : {"\"format\": \"f32le\"", "\"sample_rate\": 48000", "\"channels\": 2",
                                 "\"start_pts\": 0", "\"offset_from_mix_output_pts_zero_samples\": 0",
                                 "\"sample_frames\": 2400", "\"payload_bytes\": 19200", "\"complete\": true",
                                 "\"capture_clock_start_us\": 123456000000",
                                 "{\"file_sample\": 0, \"capture_pts\": 0, \"samples\": 960}",
                                 "{\"file_sample\": 960, \"capture_pts\": 1440, \"samples\": 1440}"})
            CHECK(json.find(field) != std::string::npos);
    }
    // Same epoch and base directory must not overwrite the previous recording.
    { AudioTimelineMixer mixer(epoch);
      CHECK(mixer.render(480, 1, 1, false)); // starts paused
      CHECK(mixer.render(960)); }
    CHECK(std::distance(fs::directory_iterator(dir), fs::directory_iterator{}) == 2);
    CHECK(readRaw(session / "system.f32le").size() == sys.size());
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (entry.path() == session) continue;
        const auto json = readText(entry.path() / "system.json");
        CHECK(json.find("\"capture_timeline_start_pts\": 480") != std::string::npos);
        CHECK(json.find("\"start_pts\": 0") != std::string::npos);
        CHECK(readRaw(entry.path() / "system.f32le").size() == 480 * 2);
    }
    // Resampling happens before the tap; limiting happens after it.
    const auto convertedDir = temporary.path / "converted";
    dumpEnvironment(convertedDir);
    {
        AudioTimelineMixer mixer(epoch);
        auto system = pcm(0, 960, .8f, .4f);
        auto mono = pcm(0, 882, .8f, .8f, 44100, 1);
        mixer.push(0, *system); mixer.push(1, *mono); mixer.finish(1);
        while (auto out = mixer.render(960))
            for (int n = 0; n < out->nb_samples; ++n) CHECK(std::abs(value(*out, 0, n)) <= .950001f);
    }
    const auto converted = fs::directory_iterator(convertedDir)->path();
    const auto convertedMic = readRaw(converted / "microphone.f32le");
    const auto convertedSys = readRaw(converted / "system.f32le");
    CHECK(convertedMic.size() == 960 * 2 && convertedSys.size() == convertedMic.size());
    for (int n = 0; n < 960; ++n) {
        CHECK(std::abs(convertedMic[n*2] - convertedMic[n*2+1]) < 1e-6);
        CHECK(convertedMic[n*2] > .5f && convertedMic[n*2] < .6f);
        CHECK(convertedMic[n*2] + convertedSys[n*2] > 1.0f);
    }

    // A regular file cannot be a dump directory. Failure stays out of the audio path.
    const auto invalid = temporary.path / "not-a-directory";
    { std::ofstream file(invalid); file << "keep"; }
    dumpEnvironment(invalid);
    {
        AudioTimelineMixer mixer(epoch);
        auto input = pcm(0, 960, .2f, .2f); mixer.push(0, *input);
        for (int n = 0; n < 2; ++n) {
            auto out = mixer.render(960); CHECK(out->pts == n * 480);
            CHECK(std::abs(value(*out, 0, 0) - .2f) < 1e-6);
        }
    }
    CHECK(readText(invalid) == "keep");
}
void debugDumpWriteFailure() {
#ifdef __unix__
    // Inject a real short write in an isolated child. Initial JSON fits within
    // 4000 bytes, as does the first 3840-byte PCM block; the second does not.
    // No Windows-only test hook
    // or fault-injection switch is added to the production writer.
    TempDumpDirectory temporary;
    const auto dir = temporary.path / "short-write";
    const pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        try {
            std::signal(SIGXFSZ, SIG_IGN);
            rlimit limit{}; CHECK(getrlimit(RLIMIT_FSIZE, &limit) == 0);
            limit.rlim_cur = 4000; CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0);
            dumpEnvironment(dir);
            {
                AudioTimelineMixer mixer(epoch);
                auto input = pcm(0, 960, .2f, .2f); mixer.push(0, *input);
                for (int i = 0; i < 2; ++i) {
                    auto out = mixer.render(960); CHECK(out->pts == i * 480);
                    CHECK(std::abs(value(*out, 0, 0) - .2f) < 1e-6);
                }
            }
            _exit(0);
        } catch (...) { _exit(1); }
    }
    int status = 0; CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    const auto session = fs::directory_iterator(dir)->path();
    for (const auto *name : {"system", "microphone"}) {
        const auto json = readText(session / (std::string(name) + ".json"));
        CHECK(json.find("\"complete\": false") != std::string::npos);
        CHECK(json.find("\"sample_frames\": 480") != std::string::npos);
        CHECK(fs::file_size(session / (std::string(name) + ".f32le")) == 480 * 8);
    }
#endif
}
void alignmentAndSilence() {
    AudioTimelineMixer mixer(epoch);
    auto system = pcm(0, 4800, .2f, .2f);
    auto mic = pcm(960, 960, .4f, .4f); // starts 20 ms late, ends 40 ms in
    mixer.push(0, *system); mixer.push(1, *mic);
    mixer.finish(1); // An ended input must not end the other source.
    for (int64_t pts = 0; pts < 4800; pts += 480) {
        auto f = mixer.render(4800); CHECK(f && f->pts == pts);
        CHECK(f->sample_rate == 48000 && f->format == AV_SAMPLE_FMT_FLTP);
        CHECK(f->ch_layout.nb_channels == 2 && f->nb_samples == 480);
        for (int i = 0; i < f->nb_samples; ++i) {
            float expected = pts + i >= 960 && pts + i < 1920 ? .6f : .2f;
            CHECK(std::abs(value(*f, 0, i) - expected) < 1e-5);
            CHECK(std::abs(value(*f, 1, i) - expected) < 1e-5);
        }
    }
    CHECK(!mixer.render(4800));
    CHECK(mixer.stats().silence[1] == 3840);
    // A late packet cannot shift the already-rendered timeline or be replayed.
    auto late = pcm(0, 480, .9f, .9f); mixer.push(1, *late);
    auto idle = mixer.render(5280); CHECK(idle->pts == 4800);
    CHECK(value(*idle, 0, 0) == 0);
}
void limiter() {
    AudioTimelineMixer mixer(epoch);
    auto f = pcm(0, 4800, .8f, .4f);
    mixer.push(0, *f); mixer.push(1, *f);
    for (int i = 0; i < 10; ++i) {
        auto out = mixer.render(4800);
        for (int j = 0; j < out->nb_samples; ++j) {
            float l = value(*out,0,j), r = value(*out,1,j);
            CHECK(std::isfinite(l) && std::abs(l) <= .950001);
            CHECK(std::abs(l - 2*r) < 1e-6); // linked gain preserves stereo image
        }
    }
    CHECK(mixer.stats().limited == 4800);
}
void resamplingAndDrain() {
    AudioTimelineMixer mixer(epoch);
    auto mono = pcm(0, 44100, .25f, .25f, 44100, 1);
    mixer.push(1, *mono); mixer.finish(1);
    CHECK(mixer.end() == 48000);
    int samples = 0;
    while (auto out = mixer.render(mixer.end())) {
        samples += out->nb_samples;
        for (int i = 0; i < out->nb_samples; ++i) {
            CHECK(value(*out,0,i) > 0 && value(*out,0,i) < .3f);
            CHECK(std::abs(value(*out,0,i) - value(*out,1,i)) < 1e-6);
        }
    }
    CHECK(samples == 48000);
}
void packedMicrophoneAndOverlap() {
    AudioTimelineMixer mixer(epoch);
    auto f = pcm(0, 480, .0f, .0f, 48000, 1);
    av_frame_unref(f.get());
    f->format = AV_SAMPLE_FMT_S16; f->sample_rate = 48000; f->nb_samples = 480;
    f->time_base = {1,1000000}; f->pts = epoch; av_channel_layout_default(&f->ch_layout, 1);
    CHECK(av_frame_get_buffer(f.get(), 0) >= 0);
    for (int i = 0; i < 480; ++i) reinterpret_cast<int16_t *>(f->data[0])[i] = 8192;
    mixer.push(1, *f);
    auto out = mixer.render(480);
    CHECK(value(*out,0,0) > .1f && value(*out,0,0) < .3f);
    CHECK(std::abs(value(*out,0,0) - value(*out,1,0)) < 1e-6);
    // The second packet overlaps already emitted samples by 240 samples.
    f->pts = epoch + 5000; mixer.push(1, *f);
    out = mixer.render(960);
    CHECK(value(*out,0,0) > 0);
    CHECK(value(*out,0,239) > 0 && value(*out,0,240) == 0);
    CHECK(mixer.stats().trimmed[1] == 240);
}
void clockDriftAndPacketSizes() {
    AudioTimelineMixer mixer(epoch);
    // Each packet is 479 samples but capture timestamps advance 480 samples.
    // Missing samples must stay at their clock position, not accumulate skew.
    for (int packet = 0; packet < 500; ++packet) {
        auto f = pcm(packet * 480, 479, .2f, .2f);
        mixer.push(0, *f);
        auto out = mixer.render((packet+1)*480);
        CHECK(out->pts == packet*480);
        CHECK(std::abs(value(*out,0,478)-.2f) < 1e-6);
        CHECK(value(*out,0,479) == 0);
    }
    CHECK(mixer.position() == 240000);
    CHECK(mixer.stats().silence[0] == 500);
    CHECK(mixer.stats().frames[0] == 500);
}
}
int main() {
    try {
        DumpEnvironment environment;
        debugDumpTimeline();
        debugDumpWriteFailure();
        alignmentAndSilence(); limiter(); resamplingAndDrain(); packedMicrophoneAndOverlap(); clockDriftAndPacketSizes();
        std::cout << "PASS: timestamp alignment, silence, drift, limiter, conversion and raw/JSON debug dumps\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
