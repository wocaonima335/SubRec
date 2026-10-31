# P1-C unified clock: A/V offset analysis and integration status

Baseline: ba733e6 (P0 R3); clock contracts: bdff5c1. P1-C changes only the clock,
scheduling, filtering, recorder integration and their tests. Capture stamping,
D3D11 graph/surface integration and ACCESS_LOST recovery remain ZCode work.
The two published contract headers are unchanged.

## Evidence: measured versus inferred

The user reports a PC3 30-minute **both** recording on ba733e6: median offset
83 ms, fitted 85.5 -> 88.6 ms, 20/180 pairs above 100 ms, maximum 123 ms. This
is a reported hardware measurement, not a new P1 measurement. `/tmp/w19-data`
was absent in this sandbox, so the raw pairs and MP4 edit lists could not be
recomputed/inspected. The fitted change is 3.1 ms over the run (about 6.2 ms/hour
if extrapolated), while 11.1% of the reported pairs exceed the C5 absolute bound.

The available `/tmp/w18-data/analyze.py` explicitly defines offset as audio onset
minus video onset: **positive means audio late**. It uses 5 ms audio bins, a
25%-of-global-peak energy threshold and nearest-flash pairing within one second;
pairing is not one-to-one. At 30 fps the video detector is also frame-quantized.
These facts establish sign and measurement limitations, not a correction value.

## What the P1 code removes

| Mechanism in P0 | P1 behavior | Status |
| --- | --- | --- |
| Video tick based on emitted AAC sample cursor | Capture stamp -> MediaClock -> rational frame tick | Code-established change; hardware benefit unmeasured |
| Audio-output delay plus 200 ms video compensation | No audio dependency or compensating video offset | Code-established; mixer still waits 200 ms for late input |
| One-frame (33.333 ms at 30 fps) targetLag and 80 ms lead gate | No targetLag/lead gate | Code-established; cannot assign the entire remaining 83 ms to this |
| 150 ms startup lead/drop bound and a count starting at the first processed video frame | Initial tick rounds actual capture media time; delayed video start remains positive | Code-established; edit-list policy unchanged |
| Filter overwrites video PTS using queue-arrival wall time | Filter preserves capture properties; encoder uses scheduler tick | Code-established |
| Single-source device sample count bypasses mixer alignment | System-only and mic-only also use timestamped mixer, silence filling and PI tracking | Code-established; PC3 single-source comparison pending |

For captures within a segment, tick = baseTick +
`av_rescale_rnd(mediaUs-baseUs, fps.num, fps.den*1000000, AV_ROUND_NEAR_INF)`.
Encoder PTS is the tick in the inverse frame-rate time base, never a round trip
through microseconds. This removes the audio-output clock's scheduling phase
from video timestamps. It **does not imply an 83 ms improvement**: previous
video phase depended on startup, scheduling and capture delivery as well as the
one-frame lag policy. No 83/85/100 ms calibration constant has been added.

At startup the origin is (tick 0, media time 0), so a slow first picture is not
backdated to zero. At resume the first valid post-resume capture starts the
contracted new segment at lastTick+1 and is forced to I. This preserves numbering
across pauses, rather than including pause duration in video PTS.

Audio render targets are mapped through the same MediaClock. Incoming PCM is
held with its original absolute capture time until the render cutoff, then split
at pause boundaries and mapped onto the compacted media axis. The 200 ms cutoff
is buffering, not an audio PTS shift. Missing input renders silence; AAC still
uses consecutive sample indices, including normal final-block padding and priming.
The audio encoder is no longer pause-gated by recorder arrival time: it must drain
buffered pre-pause PCM. Clock mapping removes paused PCM before that stage.

## What remains uncertain or physically outside this correction

- WASAPI QPC timestamp conversion and the meaning of the device's capture stamp;
  playback/loopback latency and beacon audio/visual emission skew.
- DirectShow microphone calibration: existing code anchors first device PTS to
  first demux receipt minus one packet duration. That does not measure arbitrary
  driver buffering. Microphone acoustic propagation and delayed/mixed beep copies
  can also move the detector's threshold crossing.
- A video stamp taken immediately **after grab** dates acquisition completion,
  not necessarily the monitor's physical flash/desktop presentation. Driver,
  compositor and capture latency still need measurement. Queue/encoder waiting
  after that stamp will no longer shift the video timestamp.
- Capture jitter, nearest-tick quantization (up to half a frame within a segment),
  collisions and missed slots. Capture sequence tests establish correctness, not
  the PC3 C6 drop-rate bound.
- Hybrid MP4/edit lists and AAC priming are preserved. The P0 tests still verify
  priming and positive start offsets, but supplied PC3 MP4s were unavailable for
  an actual elst comparison. There is no evidence justifying an edit-list change.
- Ten pause/resume cycles may expose source-boundary/resampler tails or segment
  phase effects. PCM boundary splitting is sample-quantized (at most one input
  sample at a boundary); hardware pause/stop acceptance remains pending.

`[AVSync] avDeltaUs` is audio-produced sample cursor minus latest accepted video
PTS, logged at most once per second. Above +/-100 ms it logs Degraded but never
alters timestamps. **This is a production-progress diagnostic, not beacon skew.**
Because mixed PCM is deliberately buffered, it can show roughly -200 ms even
with correctly aligned media. Do not interpret that warning alone as C5 failure
or compensate video from it. The old P0 stop-summary accessors return zero only
for source compatibility with the separately owned session code; P1 video counts
come from `[VideoClock] summary` (frames, slotCollisions, laggedTicks, holdoverTicks).

## Missing capture stamps: intentional integration block

There is no fallback, switch or transition offset. The first frame without the
contracted opaque_ref stamp increments `missing_capture_stamps`, emits one
`[VideoClock] missing_capture_stamp` warning, sets a descriptive captureError,
requests stopOnCaptureError, seals/aborts the video output as appropriate and
ends the worker. Its summary includes the missing count. This occurs before
encoder initialization/encoding for that frame. Subsequent shutdown errors must
not overwrite the first fatal reason (P0 behavior is unchanged).

**End-to-end recordings are blocked until P1-Z stamps every active video path**
with setFrameCaptureUs immediately after grab and preserves it through ref/clone,
filter output and any replacement D3D11 surfaces. No files under capture/ or
thread/ffscreengrabthread.*, and no main.cpp changes, are included here. A missing
stamp can therefore cause a deliberate failure on camera/CPU paths too; there is
no special exemption for non-D3D11 inputs.

The encoder worker owns the scheduler and retained last-frame reference. A future
supervisor may setCaptureStalled(true); only the worker executes clock-driven hold
callbacks, one due tick per turn. Pauses and pending resume anchors suppress
holds. Normal Lagged decisions leave gaps and do not repeat frames. The callback
submits last frame with the requested tick and only advances hold counts after
success. P1-C does not add a stall detector, ACCESS_LOST recovery or a stable
replacement GPU frame pool. Source EOF still drains the recording. P3 must wire
the supervisor and guarantee retained hardware-frame/device lifetime.

## Verification and tests

New pure tests (all ci):

- media_clock_clock: origin, historical pause intervals, duplicates/invalid
  transitions, concurrent reads and session reset (`clock;sync`).
- media_clock_30 / media_clock_ntsc: each 1,000,000 exact rational frame times,
  half-frame error bound, explicit old 50k/150k boundaries, random backward/
  duplicate/gapped sequences, counters and overflow rejection (`clock;sync`).
- media_clock_segments: delayed first picture, pause/resume anchors, hold callback
  failure/retry/bounds, due-time floor and recovery collisions (`clock;sync`).
- media_clock_audio_chunks: format/rate/layout/plane validation, sample PTS,
  final padding and diagnostic-only A/V observation (`audio;sync`).
- audio_single_source_clock: each source alone, missing-input silence, samples ==
  media time, PCM queued before a pause, pause-through-EOF, 44.1k input split by
  render cutoffs and exclusion of a distinguishable paused signal
  (`audio;mix;clock;synthetic`).
- encoder_failure_missing_stamp: real worker and queue, missing count=1, zero
  encode calls, nonempty captureError and exactly one queued stop request (`ci`).

Existing video_only_pipeline now uses MediaClock/VideoTickScheduler and still
checks a decodable 90-frame MP4, pause/resume and two sessions. The two old P0
sync tick/holdback test registrations are replaced by media_clock tests; their
baseline source files are retained. Existing encoder failure tests now stamp
synthetic frames and link the clock implementation so they still test encoding
errors rather than the new missing-stamp guard. These test-only build/fixture
updates are required by the integration; no other team's production files change.

Results: 34 native cases passed (30 CTest cases across media_clock,
audio_timeline_mix, muxer_lifecycle, test_hooks and startup_failure; plus the four
encoder-boundary cases). The clock, rational scheduler and segment/hold tests also
passed ASan/UBSan with leak detection disabled: LeakSanitizer cannot run under
this sandbox's ptrace environment. No leak-sanitizer result is claimed.

Host verification uses native FFmpeg n8 built under /tmp/w18-r3-native and the
available Qt 6.8 headers with system Qt6Core, plus an empty temporary corecrt.h
for the unused Windows CRT include. Native moc was unavailable (package fetch
failed DNS); encoder-boundary tests use a temporary Qt QMetaObjectBuilder-based
metadata adapter for the existing test recorder only, with real queued Qt calls.
Windows test builds retain normal AUTOMOC. No adapter is committed.

MinGW verification uses the real x86_64-w64-mingw32-g++ with C++17, Windows Qt 6.8
and FFmpeg headers, compiling all 15 changed/new .cpp files in normal and stripped modes (30 objects).
All passed. Stripped production objects contain no SUBREC_TEST_ or [TestHook]
strings; the existing test_hooks_strip and test_hooks_strip_binary cases passed.
Hardware NVENC/D3D11/WASAPI acceptance and a complete Windows application link/run
are outside this Linux sandbox verification.

## Next PC3 measurements

After P1-Z integration, repeat identical system-only and both-mode beacons with
raw pairs, matching emission script/interval, MP4s and VideoClock summaries. Keep
microphone gain/acoustics fixed; a both-mode run with muted microphone gain helps
separate detector/acoustic effects from clock alignment. Compare first/middle/last
absolute offsets, drift, outlier pairing, stream starts/elst, C6 counts and stopped
resource usage. Run the ten-pause test and four-mode cycle. Until then no C1-C9
hardware pass, <=100 ms absolute offset or improved memory bound is claimed.
