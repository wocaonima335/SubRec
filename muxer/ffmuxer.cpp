#include "logging/frame_log.h"
#include "ffmuxer.h"
#include <libavutil/timestamp.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <utility>
#include <QDebug>
#include <QString>
#include "capture/test_hook_gate.h"

namespace {
// Packet-level write rejections (EINVAL/INVALIDDATA) are skipped, as OBS's
// ffmpeg-mux does; a sustained run means the output is broken.
constexpr uint64_t kMaxConsecutiveRejectedWrites = 30;
constexpr uint64_t kMaxRejectedWritesPerWindow = 100;
constexpr int64_t kRejectedWindowUs = 60 * 1000000LL;
// Upper bound on one fragment while recording: a crash loses at most this much.
constexpr const char *kFragmentDurationUs = "2000000";
}

FFMuxer::FFMuxer() {}
FFMuxer::~FFMuxer() { close(); }

int64_t FFMuxer::steadyNowUs() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch()).count();
}

void FFMuxer::armFinalizeDeadline(std::chrono::milliseconds budget) {
  const int64_t deadline = steadyNowUs() + std::max<int64_t>(1, budget.count()) * 1000;
  int64_t current = finalizeDeadlineUs.load();
  // Keep an earlier deadline (e.g. armed by requestAbort); never extend it.
  while ((current == 0 || deadline < current) &&
         !finalizeDeadlineUs.compare_exchange_weak(current, deadline)) {}
}

void FFMuxer::setFinalizeBudget(std::chrono::milliseconds budget) {
  finalizeBudgetMs.store(std::max<int64_t>(1, budget.count()));
}

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
  hardInterrupt.store(false);
  finalizeDeadlineUs.store(0);
  finalizeResult = FinalizeState::NotFinalized;
  terminalError = trailerResult = closeResult = 0;
  for (auto &guard : guards) guard.configure(PacketGuardConfig{0, false, 30});
  skippedPacketCount = skippedWarningCount = 0;
  writtenPacketCount.store(0);
  rejectedWriteCount = consecutiveRejectedWrites = rejectedWindowCount = 0;
  rejectedWindowStartUs = 0;
  timescaleMismatchCount = 0;
  guardDroppedCount[0] = guardDroppedCount[1] = 0;
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
  MuxWriteResult result = MuxWriteResult::Fatal;
  return muxLocked(packet, &result);
}

MuxWriteResult FFMuxer::write(AVPacket *packet) {
  std::lock_guard<std::mutex> lock(mutex);
  MuxWriteResult result = MuxWriteResult::Fatal;
  muxLocked(packet, &result);
  return result;
}

int FFMuxer::muxLocked(AVPacket *packet, MuxWriteResult *result) {
  *result = MuxWriteResult::Fatal;
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

  // Third timestamp line (the encoder output is the second). It preserves encoder
  // priming and video reorder timestamps: native AAC starts at -1024 samples and
  // movenc needs that DTS to write the trimming elst, so streams are never clamped
  // independently. Missing timestamps and non-increasing DTS are dropped.
  const int guardIndex = streamIndex == aStreamIndex ? 0 : 1;
  PacketGuard &guard = guards[guardIndex];
  const int64_t previousDts = guard.lastDts();
  GuardReason reason = GuardReason::None;
  const GuardVerdict verdict = guard.check(*packet, &reason);
  if (verdict != GuardVerdict::Accept) {
    if (reason == GuardReason::MissingTimestamp) {
      ++guardDroppedCount[guardIndex];
      av_ts_make_string(pts_buf, packet->pts);
      av_ts_make_string(dts_buf, packet->dts);
      qDebug().noquote() << "[Mux Drop]" << streamType
                         << "pkt dropped: invalid pts/dts (pts=" << pts_buf
                         << ", dts=" << dts_buf << ")";
    } else {
      ++skippedPacketCount;
      if (skippedWarningCount < 3 || skippedPacketCount % 100 == 0) {
        ++skippedWarningCount;
        qWarning() << "[Muxer] dropped non-increasing dts stream=" << streamType
                   << "dts=" << packet->dts << "previous=" << previousDts
                   << "count=" << skippedPacketCount;
      }
    }
    if (verdict == GuardVerdict::Fatal) {
      qWarning() << "[Muxer] too many consecutive invalid packets stream=" << streamType
                 << "reason=" << guardReasonName(reason);
      return failLocked(AVERROR(EINVAL));
    }
    *result = MuxWriteResult::Skipped;
    return 0;
  }

#if !defined(SUBREC_STRIP_TEST_HOOKS) || !SUBREC_STRIP_TEST_HOOKS
  const auto target = capture::test::muxFailAtPacket();
  if (target > 0 && writtenPacketCount.load() + 1 == target) {
    qWarning() << "[TestHook] SUBREC_TEST_MUX_FAIL_AT_PACKET enabled packet=" << target;
    return failLocked(AVERROR(EIO));
  }
#endif

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
    if (ret == AVERROR_INVALIDDATA || ret == AVERROR(EINVAL)) {
      // Packet-level rejection: skip it and keep recording unless it persists.
      ++rejectedWriteCount;
      ++consecutiveRejectedWrites;
      const int64_t now = steadyNowUs();
      if (!rejectedWindowStartUs || now - rejectedWindowStartUs > kRejectedWindowUs) {
        rejectedWindowStartUs = now;
        rejectedWindowCount = 0;
      }
      ++rejectedWindowCount;
      printError(ret);
      qWarning() << "[Muxer] skipped rejected packet stream=" << streamType
                 << "count=" << rejectedWriteCount;
      if (consecutiveRejectedWrites >= kMaxConsecutiveRejectedWrites ||
          rejectedWindowCount >= kMaxRejectedWritesPerWindow) {
        qDebug().noquote() << "[Muxer] Mux Fail!";
        return failLocked(ret);
      }
      *result = MuxWriteResult::Skipped;
      return 0;
    }
    if (ret < 0) {
      qDebug().noquote() << "[Muxer] Mux Fail!";
      printError(ret);
      return failLocked(ret);
    }
    consecutiveRejectedWrites = 0;
    writtenPacketCount.fetch_add(1);
  }
  *result = MuxWriteResult::Ok;
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
    // Recoverable while recording: fragments at keyframes and at most every
    // kFragmentDurationUs. Setting frag_duration disables FFmpeg's implicit
    // frag_keyframe default, so both are given explicitly.
    av_dict_set(&options, "movflags", "+hybrid_fragmented+frag_keyframe", 0);
    av_dict_set(&options, "frag_duration", kFragmentDurationUs, 0);
    // movenc keeps the open fragment in memory; flushing AVIO after each packet
    // hands every completed fragment to the OS instead of a 32 KiB write buffer.
    av_dict_set(&options, "flush_packets", "1", 0);
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
  // Rescaling encoder ticks to the container time base must be injective, or two
  // distinct DTS could collide (the original 1666 s failure). It is exact when the
  // container tick divides the encoder tick (movenc turns 1/30 into 1/15360).
  for (const auto &[stream, encoderTb] :
       {std::pair<AVStream *, AVRational>{aStream, aTimeBase},
        std::pair<AVStream *, AVRational>{vStream, vTimeBase}}) {
    if (!stream) continue;
    const AVRational ratio = av_div_q(encoderTb, stream->time_base);
    if (ratio.den != 1 || ratio.num < 1) {
      ++timescaleMismatchCount;
      qWarning() << "[Muxer] container time base cannot represent every encoder tick stream="
                 << stream->index << "encoder=" << encoderTb.num << "/" << encoderTb.den
                 << "container=" << stream->time_base.num << "/" << stream->time_base.den;
    }
  }
  return 0;
}

void FFMuxer::requestAbort() {
  // Set before taking the lock so an in-flight FFmpeg I/O can be interrupted.
  abortRequested.store(true);
  // Nothing written yet: cancel I/O at once. Otherwise keep the trailer writable
  // and only bound how long finalization may take.
  if (writtenPacketCount.load() == 0) hardInterrupt.store(true);
  else armFinalizeDeadline(std::chrono::milliseconds(finalizeBudgetMs.load()));
  std::lock_guard<std::mutex> lock(mutex);
  streamsReady.notify_all(); // Same mutex as wait predicate: no lost wakeup.
}

int FFMuxer::interruptIO(void *opaque) {
  auto *muxer = static_cast<FFMuxer *>(opaque);
  if (muxer->hardInterrupt.load()) return 1;
  const int64_t deadline = muxer->finalizeDeadlineUs.load();
  return deadline && steadyNowUs() > deadline ? 1 : 0;
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
  finalize(std::chrono::milliseconds(finalizeBudgetMs.load()));
  std::lock_guard<std::mutex> lock(mutex);
  return closeLocked();
}

FinalizeState FFMuxer::finalize(std::chrono::milliseconds budget) {
  std::lock_guard<std::mutex> lock(mutex);
  if (finalizeResult != FinalizeState::NotFinalized) return finalizeResult;
  // Nothing to finalize: never initialized, or init failed and already closed.
  if (!fmtCtx) return FinalizeState::NotFinalized;
  const bool hadData = headerFlag && writtenPacketCount.load() > 0;
  armFinalizeDeadline(budget);
  const auto started = std::chrono::steady_clock::now();
  const int ret = closeLocked();
  finalizeResult = !hadData ? FinalizeState::FailedNoData
                 : ret >= 0 ? FinalizeState::Finalized : FinalizeState::Recoverable;
  qInfo() << "[Muxer] finalize_state=" << finalizeStateName(finalizeResult)
          << "result=" << ret << "packets=" << writtenPacketCount.load()
          << "elapsed_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::steady_clock::now() - started).count();
  return finalizeResult;
}

FinalizeState FFMuxer::finalizeState() const {
  std::lock_guard<std::mutex> lock(mutex);
  return finalizeResult;
}

uint64_t FFMuxer::writtenPackets() const { return writtenPacketCount.load(); }

uint64_t FFMuxer::rejectedWrites() const {
  std::lock_guard<std::mutex> lock(mutex);
  return rejectedWriteCount;
}

uint64_t FFMuxer::timescaleMismatches() const {
  std::lock_guard<std::mutex> lock(mutex);
  return timescaleMismatchCount;
}

uint64_t FFMuxer::skippedPackets() const {
  std::lock_guard<std::mutex> lock(mutex);
  return skippedPacketCount;
}

uint64_t FFMuxer::guardDropped() const {
  std::lock_guard<std::mutex> lock(mutex);
  return guardDroppedCount[0] + guardDroppedCount[1];
}
uint64_t FFMuxer::guardDropped(AVMediaType type) const {
  std::lock_guard<std::mutex> lock(mutex);
  return type == AVMEDIA_TYPE_AUDIO ? guardDroppedCount[0]
       : type == AVMEDIA_TYPE_VIDEO ? guardDroppedCount[1] : 0;
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
