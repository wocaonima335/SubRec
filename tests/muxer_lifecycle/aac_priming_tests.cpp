#include "aac_priming_fixture.h"
namespace {
void run(const std::filesystem::path &root, int videoStart, int reorder, bool videoFirst) {
    auto name = "aac-video-" + std::to_string(videoStart) + "-reorder-" + std::to_string(reorder) + ".mp4";
    const std::string path = (root / name).string();
    Codec a(avcodec_alloc_context3(avcodec_find_encoder_by_name("aac")));
    CHECK(a && a->codec);
    a->sample_rate = 48000; a->sample_fmt = AV_SAMPLE_FMT_FLTP; a->bit_rate = audio_encoding::kAacBitRate;
    a->time_base = {1,48000}; av_channel_layout_default(&a->ch_layout, 2);
    a->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    CHECK(avcodec_open2(a.get(), a->codec, nullptr) == 0);
    CHECK(a->initial_padding == 1024 && a->frame_size == 1024);
    // addStream needs codec/type metadata, not an opened encoder, for copied video packets.
    Codec v(avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264)));
    CHECK(v && v->codec);
    v->width = v->height = 16; v->pix_fmt = AV_PIX_FMT_YUV420P;
    v->time_base = {1,30}; v->framerate = {30,1};
    v->extradata_size = 33; // Annex B SPS and PPS
    v->extradata = static_cast<uint8_t *>(av_mallocz(v->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE));
    CHECK(v->extradata); std::memcpy(v->extradata, h264, v->extradata_size);
    FFMuxer mux; CHECK(mux.init(path) == 0);
    if (videoFirst) { CHECK(mux.addStream(v.get()) == 0); CHECK(mux.addStream(a.get()) == 0); }
    else { CHECK(mux.addStream(a.get()) == 0); CHECK(mux.addStream(v.get()) == 0); }
    CHECK(mux.writeHeader() == 0);
    std::vector<Packet> packets;
    auto drain = [&] {
        int ret;
        for (;;) {
            Packet p(av_packet_alloc()); CHECK(p);
            ret = avcodec_receive_packet(a.get(), p.get());
            if (ret < 0) break;
            p->stream_index = mux.getAStreamIndex(); packets.push_back(std::move(p));
        }
        CHECK(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF);
    };
    for (int n = 0; n < 48; ++n) {
        std::unique_ptr<AVFrame,FrameDelete> f(av_frame_alloc()); CHECK(f);
        f->format = a->sample_fmt; f->sample_rate = a->sample_rate;
        CHECK(av_channel_layout_copy(&f->ch_layout, &a->ch_layout) == 0);
        f->nb_samples = 1024; f->pts = n*1024; CHECK(av_frame_get_buffer(f.get(), 0) == 0);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 1024; ++i)
            reinterpret_cast<float *>(f->data[ch])[i] = .2f * std::sin(6.283185307179586 * 440 * (n*1024+i) / 48000);
        CHECK(avcodec_send_frame(a.get(), f.get()) == 0); drain();
    }
    CHECK(avcodec_send_frame(a.get(), nullptr) == 0); drain();
    CHECK(packets.size() > 1 && packets.front()->pts == -1024 && packets.front()->dts == -1024);
    CHECK(packets[1]->pts == 0);
    for (int i = 0; i < 30; ++i) {
        Packet p(av_packet_alloc()); CHECK(p && av_new_packet(p.get(), sizeof(h264)) == 0);
        std::memcpy(p->data, h264, sizeof(h264)); p->stream_index = mux.getVStreamIndex();
        p->pts = videoStart+i; p->dts = p->pts-reorder; p->duration = 1; p->flags = AV_PKT_FLAG_KEY;
        packets.push_back(std::move(p));
    }
    std::stable_sort(packets.begin(), packets.end(), [&](const Packet &x, const Packet &y) {
        return av_compare_ts(x->dts, x->stream_index == mux.getAStreamIndex() ? a->time_base : v->time_base,
                             y->dts, y->stream_index == mux.getAStreamIndex() ? a->time_base : v->time_base) < 0;
    });
    for (auto &p : packets) CHECK(mux.mux(p.get()) == 0);
    CHECK(mux.writeTrailer() == 0); CHECK(mux.close() == 0);
    checkRecording(path, videoStart, reorder);
    std::cout << "PASS: elst media_time=1024, audio start=0, video start=" << videoStart
              << "/30 reorder=" << reorder << "/30, " << path << '\n';
}
}
int main() {
    try {
        auto root = std::filesystem::temp_directory_path() / ("subrec-aac-priming-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        CHECK(std::filesystem::create_directory(root));
        run(root, 0, 0, false); run(root, 0, 2, true); run(root, 3, 0, true);
        // Retain the MP4 fixtures for PowerShell/PC2 parser verification.
    } catch (const std::exception &e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
