# P1-D capture-stamp propagation

The local FFmpeg source used for verification is the n8.0.3 tree at
`/workspace/r14/ffwork/src/ffmpeg` (`RELEASE` contains `8.0.3`). Its
`avcodec/decode.c` copies `packet->opaque_ref` only when
`AV_CODEC_FLAG_COPY_OPAQUE` is set. `av_frame_ref`, `av_frame_clone`, and
`av_frame_copy_props` copy `opaque_ref`. The buffersrc path either moves or
clones the input frame, and buffersink moves or references the queued frame, so
the software `buffer -> scale -> format -> buffersink` graph preserves the
stamp; `media_clock_opaque_props` exercises this on every CI build that has
the test FFmpeg components.

`av_hwframe_transfer_data` itself does not call `av_frame_copy_props` in the
generic transfer path. The ddagrab `hwdownload` filter does an explicit
`av_frame_copy_props` after transfer, and the system-memory encoder conversion
now does the same after its transfer. GPU passthrough returns the frame from
buffersink directly. D3D11 end-to-end propagation still needs a Windows GPU
run; the software graph test cannot prove driver-specific frame allocation.

The production paths are therefore:

* ddagrab GPU: source, split, and sink keep the original AVFrame properties;
  the grab thread stamps immediately after `read()` and before enqueue.
* ddagrab CPU: `hwdownload` and the following format/scale filters copy frame
  properties; the grab thread stamps the resulting sink frame before enqueue.
* DirectShow: demux stamps `AVPacket::opaque_ref`; the decoder now enables
  `AV_CODEC_FLAG_COPY_OPAQUE`; the swscale resampler copies properties into its
  newly allocated frame.
* Encoder D3D11 download: `av_hwframe_transfer_data` is followed by checked
  `av_frame_copy_props`; a failed copy aborts conversion.

Missing stamps remain an observable error at the video-clock consumer. No later
wall-clock value is substituted.

## Review of the P1-Z change

The following issues were found in `ed3a87c`:

* `thread/ffscreengrabthread.cpp:316` used a function-static failure counter,
  so its first-error log was shared by all sessions. It is harmless to stamp
  data, but the counter is not session scoped and should be moved into the
  per-session state if repeated diagnostics are required.
* `demuxer/demuxer.cpp:118-128` duplicated the private `CaptureStamp` wire
  layout instead of using a shared packet helper. This was corrected by adding
  shared `setPacketCaptureUs` helper to `queue/media_clock.h`; the
  existing frame helpers are unchanged.
* `demuxer/demuxer.cpp:210` stamps after the successful `av_read_frame` result
  and before queueing, which is the intended boundary. The packet's original
  device PTS remains untouched.

No hardware transfer result is treated as proof of property preservation; all
new-frame and transfer sites in the recording path now check and copy props.
