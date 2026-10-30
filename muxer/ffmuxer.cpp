#include "logging/frame_log.h"
#include "ffmuxer.h"
#include <libavutil/timestamp.h>
#include <cerrno>
#include <cstring>
#include <QDebug>
#include <QString>

FFMuxer::FFMuxer() {}
FFMuxer::~FFMuxer() { close(); }

int FFMuxer::failLocked(int error) {
  if (!terminalError) terminalError = error;
  streamsReady.notify_all();
  return terminalError;
}

int FFMuxer::init(const std::string &url_, const std::string &format_, bool audioEnabled) {
  std::lock_guard<std::mutex> lock(mutex);
  if (fmtCtx) return AVERROR(EBUSY);
  expectAudio = audioEnabled;
  ++generation;
  abortRequested.store(false);
  terminalError = trailerResult = closeResult = 0;
  headerFlag = trailerFlag = false;
  url = url_;
  format = format_;
  const int ret = initMuxer();
  if (ret < 0) {
    closeLocked();
    return failLocked(ret);
  }
  return 0;
}

int FFMuxer::addStream(const AVCodecContext *codecCtx) {
  std::lock_guard<std::mutex> lock(mutex);
  if (abortRequested.load()) return AVERROR_EXIT;
  if (terminalError) return terminalError;
  if (!fmtCtx) return AVERROR(EINVAL);
  if (headerFlag || trailerFlag) return AVERROR(EBUSY);
  if (!codecCtx || !codecCtx->codec || codecCtx->time_base.num <= 0 ||
      codecCtx->time_base.den <= 0 ||
      (codecCtx->codec_type != AVMEDIA_TYPE_AUDIO &&
       codecCtx->codec_type != AVMEDIA_TYPE_VIDEO)) return failLocked(AVERROR(EINVAL));
  const bool audio = codecCtx->codec_type == AVMEDIA_TYPE_AUDIO;
  if (audio && !expectAudio) return failLocked(AVERROR(EINVAL));
  if ((audio && aStream) || (!audio && vStream)) return failLocked(AVERROR(EEXIST));
  AVStream *stream = avformat_new_stream(fmtCtx, nullptr);
  if (!stream) return failLocked(AVERROR(ENOMEM));
  const int ret = avcodec_parameters_from_context(stream->codecpar, codecCtx);
  if (ret < 0) return failLocked(ret);
  stream->time_base = codecCtx->time_base;
  if (audio) {
    aStream = stream;
    aStreamIndex = stream->index;
    aTimeBase = codecCtx->time_base;
  } else {
    vStream = stream;
    vStreamIndex = stream->index;
    vTimeBase = codecCtx->time_base;
    vFrameRate = codecCtx->framerate;
  }
  streamsReady.notify_all();
  return 0;
}

int FFMuxer::mux(AVPacket *packet) {
  std::lock_guard<std::mutex> lock(mutex);
  if (abortRequested.load()) return AVERROR_EXIT;
  if (terminalError) return terminalError;
  if (!fmtCtx || !headerFlag || !packet) return AVERROR(EINVAL);
  if (trailerFlag) return AVERROR_EOF;

  int streamIndex = packet->stream_index;

  // === 步骤1：打印输入包信息 ===
  const char *streamType = (streamIndex == aStreamIndex)   ? "Audio"
                           : (streamIndex == vStreamIndex) ? "Video"
                                                           : "Unknown";

  // 使用静态缓冲区替代 av_ts2str 宏
  char pts_buf[AV_TS_MAX_STRING_SIZE];
  char dts_buf[AV_TS_MAX_STRING_SIZE];
  char duration_buf[AV_TS_MAX_STRING_SIZE];

  av_ts_make_string(pts_buf, packet->pts);
  av_ts_make_string(dts_buf, packet->dts);
  av_ts_make_string(duration_buf, packet->duration);

  qCDebug(subrec::logging::frameLog).noquote() << "[Mux Input]" << streamType
                     << "pkt stream_index=" << streamIndex << "pts=" << pts_buf
                     << "dts=" << dts_buf << "duration=" << duration_buf
                     << "size=" << packet->size;

  AVRational srcTimeBase, dstTimeBase;
  {
    if (streamIndex == aStreamIndex) {
      srcTimeBase = aTimeBase;
      dstTimeBase = aStream->time_base;
    } else if (streamIndex == vStreamIndex) {
      srcTimeBase = vTimeBase;
      dstTimeBase = vStream->time_base;
    } else {
      return AVERROR(EINVAL);
    }
  }

  av_packet_rescale_ts(packet, srcTimeBase, dstTimeBase);

  if (packet->pts == AV_NOPTS_VALUE && packet->dts != AV_NOPTS_VALUE) {
    packet->pts = packet->dts;
  } else if (packet->dts == AV_NOPTS_VALUE && packet->pts != AV_NOPTS_VALUE) {
    packet->dts = packet->pts;
  }

  // Preserve encoder priming and video reorder timestamps. In particular, native
  // AAC starts at -1024 samples; movenc needs that DTS to write the trimming elst.
  // Clamping each stream independently also destroys their relative alignment.
  if (packet->duration < 0) {
    packet->duration = 0;
  }
  if (packet->pts != AV_NOPTS_VALUE && packet->dts != AV_NOPTS_VALUE &&
      packet->dts > packet->pts) {
    packet->dts = packet->pts;
  }

  if (packet->pts == AV_NOPTS_VALUE || packet->dts == AV_NOPTS_VALUE) {
    av_ts_make_string(pts_buf, packet->pts);
    av_ts_make_string(dts_buf, packet->dts);
    qDebug().noquote() << "[Mux Drop]" << streamType
                       << "pkt dropped: invalid pts/dts (pts=" << pts_buf
                       << ", dts=" << dts_buf << ")";
    return 0;
  }

  // 写入操作（独占锁）
  {
    // === 步骤4：打印最终写入的包信息 ===
    av_ts_make_string(pts_buf, packet->pts);
    av_ts_make_string(dts_buf, packet->dts);

    qCDebug(subrec::logging::frameLog).noquote()
        << "[Mux Write]" << streamType
        << "pkt stream_index=" << packet->stream_index << "pts=" << pts_buf
        << "(" << QString::number(av_q2d(dstTimeBase) * packet->pts, 'f', 6)
        << "s)"
        << "dts=" << dts_buf << "("
        << QString::number(av_q2d(dstTimeBase) * packet->dts, 'f', 6) << "s)"
        << "size=" << packet->size;

    int ret = av_interleaved_write_frame(fmtCtx, packet);
    if (ret < 0) {
      qDebug().noquote() << "[Muxer] Mux Fail!";
      printError(ret);
      return failLocked(ret);
    }
  }
  return 0;
}

int FFMuxer::writeHeader(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mutex);
  const auto currentGeneration = generation;
  if (abortRequested.load()) return AVERROR_EXIT;
  if (terminalError) return terminalError;
  if (!fmtCtx) return AVERROR(EINVAL);
  if (trailerFlag) return AVERROR_EOF;
  if (headerFlag) return 0;
  // Wait only for streams enabled for this session.
  const bool ready = streamsReady.wait_for(lock, timeout, [&] {
    return generation != currentGeneration || !fmtCtx || abortRequested.load() ||
           terminalError || (vStream && (!expectAudio || aStream));
  });
  if (generation != currentGeneration || !fmtCtx || abortRequested.load())
    return AVERROR_EXIT;
  if (terminalError) return terminalError;
  if (!ready) return failLocked(AVERROR(ETIMEDOUT));
  if (trailerFlag) return AVERROR_EOF;
  if (headerFlag) return 0;
  AVDictionary *options = nullptr;
  const char *formatName = fmtCtx->oformat ? fmtCtx->oformat->name : nullptr;
  if (format == "mp4" || (formatName && std::strcmp(formatName, "mp4") == 0)) {
    av_dict_set(&options, "movflags", "+faststart", 0);
    // n8.0.3 mov_write_edts_tag derives media_time from start_dts/start_cts,
    // not codecpar->initial_padding (already copied by addStream). Keep the
    // session origin and let edit lists trim AAC priming / express video delay.
    av_dict_set(&options, "avoid_negative_ts", "disabled", 0);
    av_dict_set(&options, "use_editlist", "1", 0);
  }
  if (format == "rtsp") {
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "stimeout", "5000000", 0);
  }
  const int ret = avformat_write_header(fmtCtx, &options);
  av_dict_free(&options);
  if (ret < 0) { printError(ret); return failLocked(ret); }
  headerFlag = true;
  return 0;
}

void FFMuxer::requestAbort() {
  // Set before taking the lock so an in-flight FFmpeg I/O can be interrupted.
  abortRequested.store(true);
  std::lock_guard<std::mutex> lock(mutex);
  streamsReady.notify_all(); // Same mutex as wait predicate: no lost wakeup.
}

int FFMuxer::interruptIO(void *opaque) {
  return static_cast<FFMuxer *>(opaque)->abortRequested.load() ? 1 : 0;
}

int FFMuxer::writeTrailerLocked() {
  if (!fmtCtx || !headerFlag) return terminalError ? terminalError : AVERROR(EINVAL);
  if (trailerFlag) return trailerResult;
  // A failed trailer may already have deinitialized the muxer. Never retry it.
  trailerFlag = true;
  trailerResult = av_write_trailer(fmtCtx);
  if (trailerResult < 0) printError(trailerResult);
  return trailerResult;
}

int FFMuxer::writeTrailer() {
  std::lock_guard<std::mutex> lock(mutex);
  return writeTrailerLocked();
}

int FFMuxer::getAStreamIndex() {
  std::lock_guard<std::mutex> lock(mutex);
  return aStreamIndex;
}
int FFMuxer::getVStreamIndex() {
  std::lock_guard<std::mutex> lock(mutex);
  return vStreamIndex;
}
int FFMuxer::getStreamTiming(AVMediaType type, AVRational *timeBase, AVRational *frameRate) {
  std::lock_guard<std::mutex> lock(mutex);
  if (!timeBase) return AVERROR(EINVAL);
  if (type == AVMEDIA_TYPE_AUDIO && aStream) {
    *timeBase = aTimeBase;
    if (frameRate) *frameRate = AVRational{0, 1};
  } else if (type == AVMEDIA_TYPE_VIDEO && vStream) {
    *timeBase = vTimeBase;
    if (frameRate) *frameRate = vFrameRate;
  } else return AVERROR(EINVAL);
  return 0;
}

int FFMuxer::closeLocked() {
  if (fmtCtx) {
    if (headerFlag) closeResult = writeTrailerLocked();
    if (fmtCtx->pb && !(fmtCtx->oformat->flags & AVFMT_NOFILE)) {
      const int ret = avio_closep(&fmtCtx->pb);
      if (closeResult >= 0 && ret < 0) closeResult = ret;
    }
    avformat_free_context(fmtCtx);
    fmtCtx = nullptr;
  }
  aStream = vStream = nullptr;
  aStreamIndex = vStreamIndex = -1;
  aTimeBase = vTimeBase = vFrameRate = AVRational{0, 1};
  headerFlag = false;
  url.clear();
  format.clear();
  ++generation;
  streamsReady.notify_all();
  return closeResult;
}
int FFMuxer::close() {
  std::lock_guard<std::mutex> lock(mutex);
  return closeLocked();
}

int FFMuxer::initMuxer() {
  int ret = avformat_alloc_output_context2(&fmtCtx, nullptr, format.c_str(), url.c_str());
  if (ret < 0) return ret;
  if (!fmtCtx) return AVERROR(ENOMEM);
  fmtCtx->interrupt_callback = AVIOInterruptCB{&FFMuxer::interruptIO, this};
  if (!(fmtCtx->oformat->flags & AVFMT_NOFILE)) {
    ret = avio_open2(&fmtCtx->pb, url.c_str(), AVIO_FLAG_WRITE,
                    &fmtCtx->interrupt_callback, nullptr);
    if (ret < 0) return ret;
  }
  return 0;
}

void FFMuxer::printError(int ret) {
  char errorBuffer[AV_ERROR_MAX_STRING_SIZE];
  if (av_strerror(ret, errorBuffer, sizeof(errorBuffer)) < 0)
    qDebug().noquote() << "[Muxer] Unknown Error";
  else qDebug().noquote() << "[Muxer] Error:" << errorBuffer;
}
