# P1 A/V offset breakdown

The beacon convention is `audio_s - video_s`: positive values mean that the
audio beep was detected later than the video flash. The PC3 files contain 60
pairs in each five-minute run. `p1_system_5m/off2.csv` measures median **71.7
ms**, p90 **105.0 ms**, minimum **56.7 ms**, maximum **140.0 ms**. The
both-mode file measures median **77.5 ms**, p90 **96.7 ms**, with one negative
pairing outlier at -571.7 ms and a maximum of 101.7 ms. The supplied P0
both-mode median was 81.7 ms. The system-only result proves that the remaining
offset is not introduced solely by the microphone path.

## Timestamp path

| Term | System/video path | Evidence | What is known |
|---|---|---|---|
| Physical flash to desktop presentation | Marker window compositor and monitor | estimated, fixture source unavailable | `marker.log` reports `flash_after` of 0.8–4.9 ms in system-only and 0.8–3.2 ms in both-mode (medians 1.5 and 1.4 ms). That is the marker command-to-flash report, not photon time. The marker source is not in this repository, so render-buffer latency cannot be measured here. |
| Desktop presentation to ddagrab acquisition | DXGI Desktop Duplication | source-verified, hardware unmeasured | FFmpeg 8.0.3's ddagrab checks `LastPresentTime` only to reject invalid/duplicate acquisitions. Its output PTS is scheduled from `av_gettime_relative()` and refreshed after `AcquireNextFrame`; it does not export `LastPresentTime` or a QPC timestamp. |
| Acquisition return to SubRec stamp | `FFScreenGrabThread::read()` then `setFrameCaptureUs(frame, av_gettime_relative())` | source-verified, runtime unmeasured | This includes filter-sink delivery and thread scheduling. The stamp is taken after `read()`, so it dates consumer receipt rather than the physical present. |
| Stamp to video tick/encode | `MediaClock::mediaUs(captureUs)` and `VideoTickScheduler::decide()` | source-verified; diagnostic added | Tick quantization is at most half a 30-fps frame (~16.7 ms); queue and encoder delay do not move the assigned PTS. New `[VideoClock] stamp_diagnostic` lines report post-stamp consumer delay. |
| Marker beep render to WASAPI loopback | Windows render engine and shared-mode buffering | estimated | The beep enters loopback only after the marker's render buffer is submitted. `marker.log` does not expose the audio device period or render-to-loopback delay. |
| Loopback packet delivery | `IAudioCaptureClient::GetBuffer` | source-verified | `GetBuffer` returns `qpcPosition` in 100-ns units. The capture thread converts it to the session domain as `startUs + (qpc-startQpc)/10`; it uses receipt time only when `AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR` is set, subtracting one packet duration. |
| WASAPI QPC to FFmpeg clock base | `startQpc=qpc100ns()`, `startUs=av_gettime_relative()` | source-verified, drift unmeasured | FFmpeg 8.0.3 `av_gettime_relative()` is monotonic only when the platform build supplies `clock_gettime(CLOCK_MONOTONIC)`. Windows configure uses the `GetSystemTimeAsFileTime` fallback when that capability is absent, with the documented 42-hour offset; this is not QPC. The start pair aligns the origins, but frequency drift and scheduling are not logged. |
| Mixer holdback and render | `mixHoldbackUs` then `AudioTimelineMixer::renderAt` | source-verified; diagnostic added | The mixer intentionally waits for late packets and maps capture timestamps through `MediaClock`. This is buffering, not a timestamp offset. New `[AudioClock] mixer_diagnostic` lines report source timestamp to mixer receipt delay; the WASAPI source timestamp has already been converted from QPC. |
| Audio encoder/muxer | sample cursor and AAC priming/edit list | source-verified | AAC PTS remains the contiguous mixer output sample timeline. Priming affects file playback start and beacon interpretation, but no PC3 MP4 edit-list values were supplied for independent measurement. |

The two new diagnostics are rate-limited to the first item and every 300th item.
They do not alter queueing, timestamps, frame selection, or error handling.
The video diagnostic intentionally does not subtract ddagrab PTS from captureUs:
ddagrab's `first_pts` is private to the filter instance, so those values do not
share an absolute origin. A correct absolute present-time diagnostic requires a
capture-source change.

## Unified-clock fix proposal

The principled fix is to transport event timestamps rather than receipt
timestamps. ZCode should expose the DXGI `LastPresentTime`/QPC value from
ddagrab (or stamp immediately after acquisition in the capture source), convert
it once to the session clock domain using a measured QPC-to-`av_gettime_relative`
anchor, and call `setFrameCaptureUs` with that value. This belongs in
`capture/ddagrab_screen_source.*` and `thread/ffscreengrabthread.*`, which are
ZCode-owned. P1-C's `FFVEncoderThread` then consumes the unchanged stamp.

For WASAPI, the existing `GetBuffer` QPC conversion is already the right event
timestamp mechanism. The owner of `capture/wasapi_loopback_capture.cpp` should
add a one-shot anchor/frequency diagnostic and a timestamp-error counter; no
constant correction should be introduced. If Windows builds `av_gettime_relative`
from wall time, an explicit QPC-to-session anchor with periodic drift reporting
is preferable to assuming the clocks have identical rates.

## Black-frame observations

In the five-minute both-mode recording, the reported near-black Y average occurs
at one-based frame indices **6675** (~222.5 s) and **8156** (~271.9 s). The
trimmed log shows ordinary accepted encoder ticks around both points, no
holdover ticks, and no per-frame error. The corresponding video packets are
normal-sized NVENC packets (8739 and 8738 bytes), while the final summary reports
74 slot collisions/lagged ticks and zero holdovers. Therefore the evidence does
not support a scheduler hold frame or slot-collision cause.

The black frames occur only in both-mode runs, but the video source and encoder
are shared with system-only mode. The strongest remaining hypotheses are a
transient blank/zero Desktop Duplication texture or a D3D11 surface-pool reuse
race while the audio/mixer workload changes scheduling. The current logs cannot
distinguish those from a beacon extraction artifact. The preview branch is
single-slot and reports only two dropped preview frames; its counters do not
identify the recording surface. A decisive follow-up is a source-side luma/hash
sample immediately after the ddagrab sink and before enqueue, paired with the
encoder input hash at the same tick. That instrumentation must be added by
ZCode in the capture files or by a jointly owned frame probe; no timing behavior
was changed here.
