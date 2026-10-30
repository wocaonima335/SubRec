#include "ddagrab_screen_source.h"
#include "gpu_preview.h"

#include <QDebug>
#include <QStringList>
#include <exception>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

namespace {
QString ffmpegErrorText(int errorCode)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    if (av_strerror(errorCode, buffer, sizeof(buffer)) < 0) {
        return QStringLiteral("unknown FFmpeg error %1").arg(errorCode);
    }
    return QString::fromLocal8Bit(buffer);
}

CaptureStartResult failure(const InputRequest &request,
                           CaptureErrorStage stage,
                           int ffmpegError,
                           const QString &message,
                           const QString &remediation)
{
    CaptureError error;
    error.stage = stage;
    error.role = CaptureDeviceKind::Screen;
    error.backend = CaptureBackend::DesktopDuplication;
    error.deviceId = request.deviceId;
    error.deviceName = request.displayName;
    error.ffmpegError = ffmpegError;
    error.ffmpegMessage = ffmpegErrorText(ffmpegError);
    error.userMessage = message;
    error.remediation = remediation;
    return CaptureStartResult::failure(std::move(error));
}

const char *modeName(DdagrabFrameMode mode)
{
    switch (mode) {
    case DdagrabFrameMode::Auto: return "auto";
    case DdagrabFrameMode::Gpu: return "gpu";
    case DdagrabFrameMode::Cpu: return "cpu";
    }
    return "auto";
}

} // namespace

QString ddagrabGpuGraphSpec(const DdagrabOptions &options)
{
    // The preview branch crosses a single-slot handoff before hwdownload/scale/fps.
    // Running those filters in this native graph would serialize GPU transfers with capture.
    return QStringLiteral("ddagrab=output_idx=%1:framerate=%2:draw_mouse=%3,split=2[out][preview_out]")
        .arg(options.outputIndex).arg(options.frameRate).arg(options.drawMouse ? 1 : 0);
}

DdagrabOptions DdagrabOptions::fromRequest(const InputRequest &request)
{
    DdagrabOptions options;
    // The backend writes "output_idx=N" into the request url (see DdagrabCaptureBackend).
    const QString url = request.url.trimmed();
    if (url.startsWith(QLatin1String("output_idx="))) {
        bool ok = false;
        const int index = url.mid(int(sizeof("output_idx=") - 1)).toInt(&ok);
        if (ok && index >= 0) {
            options.outputIndex = index;
        }
    }
    if (qEnvironmentVariableIsSet("SUBREC_DDAGRAB_OUTPUT")) {
        bool ok = false;
        const int index = qEnvironmentVariableIntValue("SUBREC_DDAGRAB_OUTPUT", &ok);
        if (ok && index >= 0) {
            options.outputIndex = index;
        }
    }
    const QString mode = qEnvironmentVariable("SUBREC_DDAGRAB_MODE").trimmed().toLower();
    if (mode == QLatin1String("gpu")) {
        options.mode = DdagrabFrameMode::Gpu;
    } else if (mode == QLatin1String("cpu")) {
        options.mode = DdagrabFrameMode::Cpu;
    } else if (!mode.isEmpty() && mode != QLatin1String("auto")) {
        qWarning() << "[Ddagrab] ignoring unknown SUBREC_DDAGRAB_MODE" << mode;
    }
    return options;
}

DdagrabScreenSource::DdagrabScreenSource() = default;

DdagrabScreenSource::~DdagrabScreenSource()
{
    close();
}

bool DdagrabScreenSource::isAvailable()
{
    return avfilter_get_by_name("ddagrab") != nullptr;
}

QStringList DdagrabScreenSource::d3d11FrameEncoders()
{
    QStringList names;
    for (const char *name : {"h264_nvenc", "h264_amf", "h264_qsv"}) {
        const AVCodec *codec = avcodec_find_encoder_by_name(name);
        if (!codec) {
            continue;
        }
        for (int i = 0;; ++i) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
            if (!config) {
                break;
            }
            if (config->pix_fmt == AV_PIX_FMT_D3D11
                && (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX)) {
                names.append(QString::fromLatin1(name));
                break;
            }
        }
    }
    return names;
}

CaptureStartResult DdagrabScreenSource::open(const InputRequest &request, bool forceCpuFrames)
{
    close();
    m_options = DdagrabOptions::fromRequest(request);
    if (forceCpuFrames) {
        m_options.mode = DdagrabFrameMode::Cpu;
    }

    if (!isAvailable()) {
        return failure(request, CaptureErrorStage::InputFormat, AVERROR_FILTER_NOT_FOUND,
                       QStringLiteral("This FFmpeg build has no ddagrab (Desktop Duplication) filter."),
                       QStringLiteral("Use the FFmpeg built by packaging/ffmpeg/build-ffmpeg.sh, or set "
                                      "SUBREC_CAPTURE_BACKEND=dshow."));
    }

    QString error;
    int ret = 0;
    if (m_options.mode != DdagrabFrameMode::Cpu) {
        ret = buildGraph(true, &error);
        if (ret >= 0) {
            AVBufferRef *framesContext = av_buffersink_get_hw_frames_ctx(m_sink);
            if (m_options.mode == DdagrabFrameMode::Gpu) {
                m_gpuFrames = true;
            } else if (framesContext && probeGpuEncoder(framesContext)) {
                m_gpuFrames = true;
            } else {
                qInfo() << "[Ddagrab] no encoder accepts D3D11 frames here; using system-memory frames";
            }
            if (m_gpuFrames) {
                try {
                    m_preview = std::make_shared<GpuPreview>(m_previewConsumer);
                } catch (const std::exception &e) {
                    // Keep draining the split sink even if a preview worker cannot start.
                    qWarning() << "[Preview] unable to start worker:" << e.what();
                }
                qInfo().noquote() << "[Ddagrab] opened" << describe();
                return CaptureStartResult::success();
            }
            freeGraph();
        } else if (m_options.mode == DdagrabFrameMode::Gpu) {
            return failure(request, CaptureErrorStage::InputOpen, ret,
                           QStringLiteral("Desktop Duplication (ddagrab, GPU frames) could not start: %1")
                               .arg(error),
                           QStringLiteral("Unset SUBREC_DDAGRAB_MODE or set it to cpu, or use "
                                          "SUBREC_CAPTURE_BACKEND=dshow."));
        } else {
            qWarning().noquote() << "[Ddagrab] GPU graph failed:" << error << "- trying CPU graph";
        }
    }

    ret = buildGraph(false, &error);
    if (ret < 0) {
        return failure(request, CaptureErrorStage::InputOpen, ret,
                       QStringLiteral("Desktop Duplication (ddagrab) could not start: %1").arg(error),
                       QStringLiteral("Check that the display is attached to the default GPU and not "
                                      "on a secure desktop (UAC/lock screen), or set "
                                      "SUBREC_CAPTURE_BACKEND=dshow."));
    }
    m_gpuFrames = false;
    qInfo().noquote() << "[Ddagrab] opened" << describe();
    return CaptureStartResult::success();
}

int DdagrabScreenSource::buildGraph(bool gpuFrames, QString *error)
{
    freeGraph();
    m_graph = avfilter_graph_alloc();
    if (!m_graph) {
        *error = QStringLiteral("cannot allocate filter graph");
        return AVERROR(ENOMEM);
    }

    // ddagrab creates its own D3D11 device on the default adapter. dup_frames (default 1)
    // keeps a constant frame rate on a static desktop, like the dshow screen filter did.
    QString spec;
    if (gpuFrames) {
        spec = ddagrabGpuGraphSpec(m_options);
    } else {
        spec = QStringLiteral("ddagrab=output_idx=%1:framerate=%2:draw_mouse=%3")
                   .arg(m_options.outputIndex)
                   .arg(m_options.frameRate)
                   .arg(m_options.drawMouse ? 1 : 0);
        // Same colour conversion as the dshow path (FFVResampler: BT.601, limited range).
        spec += QStringLiteral(",hwdownload,format=bgra,"
                               "scale=flags=bilinear:out_color_matrix=bt601:out_range=tv,"
                               "format=yuv420p");
    }

    int ret = avfilter_graph_create_filter(&m_sink, avfilter_get_by_name("buffersink"), "out",
                                           nullptr, nullptr, m_graph);
    if (ret < 0) {
        *error = QStringLiteral("buffersink: %1").arg(ffmpegErrorText(ret));
        freeGraph();
        return ret;
    }

    if (gpuFrames) {
        m_previewFrame = av_frame_alloc();
        if (!m_previewFrame) { freeGraph(); *error = "cannot allocate preview frame"; return AVERROR(ENOMEM); }
        ret = avfilter_graph_create_filter(&m_previewSink, avfilter_get_by_name("buffersink"),
                                           "preview_out", nullptr, nullptr, m_graph);
        if (ret < 0) {
            *error = QStringLiteral("preview buffersink: %1").arg(ffmpegErrorText(ret));
            freeGraph();
            return ret;
        }
    }

    AVFilterInOut *sinkInput = avfilter_inout_alloc();
    AVFilterInOut *previewInput = gpuFrames ? avfilter_inout_alloc() : nullptr;
    AVFilterInOut *openOutputs = nullptr; // ddagrab is a source: the chain has no open input
    if (!sinkInput || (gpuFrames && !previewInput)) {
        avfilter_inout_free(&previewInput);
        avfilter_inout_free(&sinkInput);
        freeGraph();
        *error = QStringLiteral("cannot allocate filter in/out");
        return AVERROR(ENOMEM);
    }
    sinkInput->name = av_strdup("out");
    sinkInput->filter_ctx = m_sink;
    sinkInput->pad_idx = 0;
    sinkInput->next = nullptr;
    if (previewInput) {
        previewInput->name = av_strdup("preview_out");
        previewInput->filter_ctx = m_previewSink;
        previewInput->pad_idx = 0;
        previewInput->next = nullptr;
        sinkInput->next = previewInput;
    }

    const QByteArray specUtf8 = spec.toUtf8();
    ret = avfilter_graph_parse_ptr(m_graph, specUtf8.constData(), &sinkInput, &openOutputs, nullptr);
    avfilter_inout_free(&sinkInput);
    avfilter_inout_free(&openOutputs);
    if (ret < 0) {
        *error = QStringLiteral("parse \"%1\": %2").arg(spec, ffmpegErrorText(ret));
        freeGraph();
        return ret;
    }

    // Configuring the graph runs ddagrab's DuplicateOutput and the first AcquireNextFrame.
    ret = avfilter_graph_config(m_graph, nullptr);
    if (ret < 0) {
        *error = QStringLiteral("configure \"%1\": %2").arg(spec, ffmpegErrorText(ret));
        freeGraph();
        return ret;
    }

    m_width = av_buffersink_get_w(m_sink);
    m_height = av_buffersink_get_h(m_sink);
    if (m_width <= 0 || m_height <= 0) {
        *error = QStringLiteral("invalid capture size %1x%2").arg(m_width).arg(m_height);
        freeGraph();
        return AVERROR(EINVAL);
    }
    if (!gpuFrames && ((m_width | m_height) & 1)) {
        // yuv420p needs even sizes; libx264 rejects odd ones.
        qWarning() << "[Ddagrab] odd capture size" << m_width << "x" << m_height;
    }
    return 0;
}

bool DdagrabScreenSource::probeGpuEncoder(AVBufferRef *framesContext)
{
    const auto *frames = reinterpret_cast<const AVHWFramesContext *>(framesContext->data);
    for (const QString &name : d3d11FrameEncoders()) {
        const AVCodec *codec = avcodec_find_encoder_by_name(name.toLatin1().constData());
        AVCodecContext *probe = codec ? avcodec_alloc_context3(codec) : nullptr;
        if (!probe) {
            continue;
        }
        probe->width = m_width;
        probe->height = m_height;
        probe->time_base = AVRational{1, m_options.frameRate};
        probe->framerate = AVRational{m_options.frameRate, 1};
        probe->pix_fmt = AV_PIX_FMT_D3D11;
        probe->sw_pix_fmt = frames->sw_format;
        probe->bit_rate = 4 * 1024 * 1024;
        probe->max_b_frames = 0;
        probe->hw_frames_ctx = av_buffer_ref(framesContext);
        const int ret = probe->hw_frames_ctx ? avcodec_open2(probe, codec, nullptr) : AVERROR(ENOMEM);
        avcodec_free_context(&probe);
        qInfo().noquote() << QStringLiteral("[Ddagrab] GPU-frame probe %1 (%2): %3")
                                 .arg(name, QString::fromLatin1(av_get_pix_fmt_name(frames->sw_format)),
                                      ret >= 0 ? QStringLiteral("ok") : ffmpegErrorText(ret));
        if (ret >= 0) {
            m_probedEncoder = name;
            return true;
        }
    }
    return false;
}

int DdagrabScreenSource::read(AVFrame *frame)
{
    if (!m_sink) return AVERROR(EINVAL);
    const int ret = av_buffersink_get_frame(m_sink, frame);
    if (ret >= 0 && m_previewSink) {
        // split has already enqueued the same capture at both sinks. Never request another
        // source frame for preview: that would block in ddagrab and accumulate main frames.
        if (m_previewFrame) {
            while (av_buffersink_get_frame_flags(m_previewSink, m_previewFrame,
                                                 AV_BUFFERSINK_FLAG_NO_REQUEST) >= 0) {
                if (m_preview) m_preview->offer(m_previewFrame);
                av_frame_unref(m_previewFrame);
            }
        }
    }
    return ret;
}

void DdagrabScreenSource::freeGraph()
{
    m_preview.reset();
    av_frame_free(&m_previewFrame);
    avfilter_graph_free(&m_graph);
    m_sink = nullptr;
    m_previewSink = nullptr;
}

void DdagrabScreenSource::close()
{
    freeGraph();
    m_gpuFrames = false;
    m_width = 0;
    m_height = 0;
    m_probedEncoder.clear();
}

QString DdagrabScreenSource::describe() const
{
    return QStringLiteral("output_idx=%1 %2x%3@%4 frames=%5 (mode %6%7)")
        .arg(m_options.outputIndex)
        .arg(m_width)
        .arg(m_height)
        .arg(m_options.frameRate)
        .arg(m_gpuFrames ? QStringLiteral("d3d11") : QStringLiteral("yuv420p"))
        .arg(QString::fromLatin1(modeName(m_options.mode)))
        .arg(m_probedEncoder.isEmpty() ? QString() : QStringLiteral(", probe ") + m_probedEncoder);
}
