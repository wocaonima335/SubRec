# P0 R3: PC3 timing and memory investigation

Input: `w18-data/t60_SubRec_filtered.log`, UTF-16 `mem.log`, `off.csv`,
`analyze.py`, `analyze.txt`, and `r14_t2_both_60s/`. Baseline: `6d661e7`.
The recording files themselves were not supplied. All MB below are decimal MB.

## Observations from the supplied data

- `analyze.py` defines **offset = audio onset - video onset**. Positive values
  mean audio is late relative to the picture.
- The 60-minute CSV has 362 pairs: mean 286.874 ms, median 296.667 ms. An ordinary
  least-squares fit using every pair is 282.937 -> 290.811 ms over 3600 seconds,
  or +7.874 ms/hour. This is predominantly a constant offset, not large drift.
- Pairing is nearest-flash within one second, not one-to-one. The detector uses
  5 ms audio bins, a global 25%-of-peak threshold and 30 fps video frames. It
  finds 379 beeps for 360 flashes. Diagnostic filtering to `0 < offset < 500 ms`
  leaves 360 pairs, mean 292.695 ms, slope -7.537 ms/hour. This is **not** an
  acceptance-filtering rule; it shows why the small drift's sign is not robust.
- r14 has 13 pairs: 11 positive pairs average 204.545 ms (median 210 ms); the
  other two are -895 and -745 ms. Its raw mean is consequently misleading.
  The new run's first six pairs (first minute) already average 276.389 ms.
  The additional roughly 75 ms therefore cannot be explained by accumulating
  drift over an hour. The beacons repeat every 5 seconds in r14 and every
  10 seconds in the long run; current beacon emission timing is unavailable.
- Both mux logs report `firstAudio(us)=-21333 firstVideo(us)=0`. At 48 kHz that
  audio timestamp is AAC's 1024-sample priming. Mixer start to AAC initialization
  is about 199 ms in r14 and 202 ms in the long run. The long run reports 13
  dropped video frames, 9 catch-ups, zero skipped mux packets, and zero mux
  bootstrap drops. The mixer reports negligible silence/trim relative to an hour
  of samples and zero future-packet drops.
- r14 logs the faststart second pass. P0 uses hybrid_fragmented. **No measured
  comparison of `elst`, track start_time or decoded first-sample timestamps is
  possible without the MP4s.** Equal logged input origins are evidence against
  a newly shifted input origin; they do not prove identical container edits.
- Private bytes grow from 407,228,416 to 484,294,656: +77.066 MB over 59m01s
  (78.350 MB/hour normalized). Minutes 1-54 grow at about 60.7 MB/hour; minutes
  11-54 about 58.3 MB/hour. Minute 54->55 adds 18.715 MB; minutes 55-60 resume
  about 57.1 MB/hour. There is no post-stop memory sample. This is not evidence
  of a cache already reaching a plateau.

## Timing cause and P0 correction

Code-established defect: `runMixed()` renders capture time minus a fixed 200 ms
holdback. PCM sample zero still describes session start. `RecorderSyncQueue`
paces **current video** using the **delayed emitted-audio sample cursor**, with
no allowance for that holdback. The uncorrected clock can therefore timestamp
current pictures too early, which has the observed audio-late sign. The limiter
has immediate attack and no look-ahead; it does not introduce another 200 ms.

The shared `capture::audio::mixHoldbackUs` now drives both mixer rendering and
video pacing. Only both-mode configures that compensation. The queue adds it
when computing the video pacing clock, never to audio samples/PTS, and keeps
its raw audio cursor available for logging. Catch-up remains ceil-to-tick,
strictly increasing integer ticks. Existing 150 ms pre-audio startup, 80 ms
maximum lead and one-frame target lag policies remain, now relative to the
corrected pacing clock. Reset removes compensation for the next session.
Single-source audio, video-only, AAC priming and the mux edit-list policy are
unchanged; compensated video start offsets are intentionally preserved.

A synthetic 200,000-frame test models 200 ms delayed audio delivery: current
video tracks the compensated capture position within the existing one-tick
rounding bound, all video PTS increase, and PCM PTS are unchanged. It also checks
startup drops, pause/resume, a ten-second audio jump, reset and video-only mode.

**Inference, not PC3 measurement:** removing a nominal 200 ms component from
~285 ms suggests ~85 ms residual. This does not establish C5: frame quantization,
source/driver buffering, initial video acquisition and beacon emission latency
can put individual pairs over 100 ms. The existing 33.333 ms target lag can
contribute a frame, but the available logs cannot allocate the extra ~75 ms
between source startup/phase, devices, beacon detection and container handling.
The holdback was present in both runs, so it does not alone explain their
*difference*. The 150 ms startup limit is a bound, not a measured extra delay.
No measured 285 ms magic offset, device-latency guess or AAC priming subtraction
has been introduced.

If PC3 still exceeds C5, compare identical short and long runs with the same
beacon interval and retain both MP4s. Inspect per-track first PTS/DTS, start_time,
`elst` (especially AAC media_time=1024), and first decoded timestamps. Compare
system-only against both with microphone gain zero to separate mixed-clock
pacing from acoustic/threshold effects. Measure source capture timestamps versus
video submission before further compensation. A capture-timestamp-based video
scheduler is a larger follow-up if variable device latency dominates; changing
faststart/hybrid or discarding edit lists is not justified by these data.

## Memory cause and correction

**Confirmed leak:** both success loops in `FFAEncoder::encode()` allocated an
`AVPacket` and called `FFAPacketQueue::enqueue()`. Enqueue moves its references
into an `FFPacket`; it does not own or free the input shell. Success never freed
that shell. Video encode already frees/reuses its shell correctly. AAC now frees
it after enqueue in normal encoding and flush; allocation failure is also checked.
No packet payload or queue ownership contract changes.

At 48kHz/1024 samples, AAC emits about 168,750 packets/hour. `sizeof(AVPacket)`
is 104 on the tested 64-bit FFmpeg headers: **17.55 MB/hour of unreachable shells
before allocator alignment/metadata**. The exact Windows heap/private-byte cost
was not measured. The allocation-balance unit test runs the actual encoder and
queue, verifies 2,001 output packets including drain, and observes zero outstanding
shells after every encode and flush. It fails against the baseline encoder.

**Expected retention, not this leak:** reading the available FFmpeg n8 source
shows hybrid MOV keeps `cluster_written` entries across fragments so its trailer
can construct ordinary MP4 tables. `MOVIentry` is 72 bytes on the tested 64-bit
build; about 276,750 AAC+30fps video packets/hour imply roughly **19.93 MB/hour**
of entries alone. Fragment metadata, reallocations, allocator retention and final
conversion need additional memory. This is duration-dependent index storage,
not an indefinitely filling media-payload queue. The 18.7 MB step is compatible
with allocation/reallocation behavior but cannot be attributed from a process
private-byte counter alone. No FFmpeg/OBS code was copied into this patch.

Code inspection found bounded packet/frame queues, consumed mixer frame deques,
fixed audio-meter storage and a coalesced latest-frame preview path, not a growing
frame archive. The logger streams directly to QFile, with no application log
history. GPU driver caches, Qt event backlogs and heap fragmentation cannot be
excluded by code inspection. 
Two `[RecordingBuffers]` lines per minute now expose mixer pending frame counts,
mixer queued sample horizon, decoded input queue depths, encoded packet queue
depths and cumulative processed packet/skip counts. These are worker-owned or
locked snapshots; they do not retain samples. Packet counters include skipped
writes (reported separately). No test hook is involved.

**The <=50 MB/hour target is not yet established.** The fixed leak plus the MOV
index accounts for a substantial part of the growth, but subtracting only known
shell bytes does not prove a pass. On PC3 repeat the same hour and sample memory
before recording, every minute, immediately after stop and after 1/5 minutes
idle; compare the new buffer diagnostics. Flat queues with packet-proportional
growth and release at stop support retained index storage. Continued growth
across stopped sessions calls for heap/GPU tracing. If hybrid index retention
itself exceeds budget, segmented output or pure fragmented MP4 without conversion
would require an explicit format decision; this patch preserves hybrid_fragmented.

## AutoTest reporting

The early fatal-stop watcher used by record_seconds and mode_cycle now uses the
same report helper as normal duration completion. FAIL includes `file`, `bytes`,
`encoder`, `hardware`, `resolution`, `encoderDetail` and the first `captureError`.
Finalization gating, prompt exit code 7 and first-error-wins behavior are preserved.

## Tests and sandbox verification

All 20 native tests passed: muxer_lifecycle (9), test_hooks (8), and
audio_timeline_mix (3). This includes AAC priming, real AAC pipeline finalization,
duplicate DTS/injected mux failure, existing 200,000-tick regression, video-only,
first-error-wins, early fatal completion, strip binary and mixed-audio drift.
The initial minimal FFmpeg build lacked RAWVIDEO, required by muxer_lifecycle;
enabling that test component resolved its setup failure. The mixed-clock test
accounts for the existing one-tick ceil rounding at microsecond boundaries.

New CTest cases:

- `mixed_audio_clock_delay`: `ci;sync`; synthetic, no device.
- `aac_packet_ownership`: `ci;audio`; real production AAC/queue, no device or
  additional release-absent FFmpeg components.
- `autotest_result_metadata`: `ci`; exact FAIL fields, PASS and missing-file cases.

Native suites use a locally built minimal FFmpeg n8 LGPL configuration. Native
Qt Core tests use the available real Qt 6.8 headers with the system Qt6Core
library; a temporary empty corecrt.h only bypasses an unused Windows CRT include.
The test setup lives under /tmp and is not a product build configuration.

MinGW verification compiles all six changed production translation units and
three new test translation units to objects, both normal and stripped, using
real x86_64-w64-mingw32-g++ and Windows Qt 6.8/FFmpeg headers. main.cpp uses an
empty temporary main.moc for this compile-only check: the unchanged QmlBridge
moc was not generated, and the full Windows application was not linked or run.
Stripped production objects contain no SUBREC_TEST_ or [TestHook] strings.

Hardware NVENC/D3D11/WASAPI timing, the 60-minute memory budget and MP4s from PC3
remain to be verified on Windows. No hardware acceptance result is claimed.
