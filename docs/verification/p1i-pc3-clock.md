# P1i: PC3 receive jitter, resume phase and memory audit

Base: `9f510f3`. Inputs are the two r17 logs/offset CSVs/memory logs in
`/tmp/w20-data`. No capture-owned files were changed. The scheduler header
change is additive: the original `decide`/`beginSegment` contracts and tests
remain; production video now uses `decidePaced` throughout a session.

## 1. C6 frame loss

The 15-minute summary accounts for **27,003 captured inputs**:
23,581 accepted + 3,422 collisions. The deficit is in scheduling, not a missing
12.7% of ddagrab output. Loss is 12.673% of inputs. Parsing all encoder PTS
lines gives 20,157 increments of one tick and **3,423 increments of two ticks**;
first PTS are `0,2,3,4,5,6,7,8,10,11,12,13,15,...`. There are no larger jumps.
This is the signature of quantization gaps paired with subsequent collisions.
The one extra gap versus collision is an endpoint/startup phase effect.

At 30 fps, T = 33,333.333 us and the nearest-slot boundary is T/2 = 16,666.667
us. For example, a stamp at n*T+18,000 rounds to n+1; the following stamp at
(n+1)*T+16,000 **also rounds to n+1** and is discarded. The first frame can
leave n empty. A two-millisecond receive-time change is enough; no dropped
capture or long encoder stall is required. Repeating this jitter repeats the
loss because every frame is independently rounded against the same fixed grid.

There are 91 sampled stamp diagnostics (first, then every 300 frames). Relative
to capture 1 and ideal `(index-1)*T`, the later stamp residuals have
min/median/max **12,526 / 27,383 / 33,106 us**. This spans a nearest-slot
boundary with only 20,580 us of variation. CaptureUs minus ddagrab sourcePts
ranges 6,977,699,395..6,977,721,527 us: its varying offset is another 22,132 us
of evidence that source PTS and post-grab receive stamps are not interchangeable.
Post-stamp encoder-arrival delays are 513 / 9,865 / 18,192 us (min/median/max).
Those arrival delays are *not* used to compute PTS. MediaClock simply subtracts
the shared origin in this unpaused run; it does not create extra time.

The old log does not contain every capture stamp or the session origin, so it
cannot reconstruct the precise pair behind each of the 3,422 collisions. The
regression is explicitly a synthetic reproduction, not a purported raw replay.
It uses 27,003 monotonic stamps with 3,422 alternating 2 ms boundary crossings:
the old scheduler gives exactly **23,581 / 3,422 / 3,423** for frames / collisions
/ skipped ticks. The new scheduler accepts **27,003 / 0 / 1**; the one initial
tick is the intentionally preserved positive start offset of this fixture.

Fix: for already paced capture, use the next consecutive tick while its error
from capture media time is strictly less than one frame. Outside that band,
return to nearest session tick, still skipping genuine outages and rejecting
excess/duplicate/backwards captures. This hysteresis avoids repeatedly crossing
a half-frame boundary. It does not integrate rounded frame durations or free-run
indefinitely: error remains bounded by one frame during ordinary capture.
Startup is still nearest absolute media tick; positive startup offsets survive.
Tests also cover 30 and 30000/1001 fps over 108,000 captures each, real gaps,
stale stamps, delayed startup, overflow and capture recovery after holdover.

Holdover was zero because it requires `captureStalled`, whose setter has no
production caller in this revision. It also deliberately does not fill ordinary
Lagged decisions. There are already enough captures here, so duplicating pictures
is unnecessary for this defect. Capture-loss supervisor integration remains a
separate matter. New sampled `tick_diagnostic` lines include mediaUs and chosen
tick together, plus resume decisions, to make subsequent field correlation exact.

Test: **media_clock_pc3_jitter** (`ci`).

## 2. A/V lag on resume

Resume keyframe PTS are **902, 1651, 2400, 3149, 3898, 4647, 5396, 6145, 6894,
7643**: every subsequent interval is 749 ticks = 24.966667 s. Logged media pause
positions advance approximately 25 s. The old `beginSegment(firstMediaUs)` sets
`baseTick=lastTick+1` but `baseUs=firstMediaUs`. It therefore removes not just the
pause, but the uncaptured active time between the previous picture and the first
new picture. Repeating that re-anchor accumulates the compression on video only.
Nine successive 749-versus-750 increments account for **300 ms**; ten cycles
can account for approximately **333.333 ms**. CSV mean offset goes from 56.111 ms
(first six beacons) to 372.667 ms (last five), a **316.556 ms** rise, consistent
with that mechanism and the beacon/frame quantization. In the unpaused run the
same means are 60.834 and 63.667 ms, only 2.833 ms apart.

Fix: the first post-resume picture is assigned its nearest **session media**
tick (at least the next tick for strict monotonicity) and remains a forced I
picture. It never resets the media origin to its arrival or its capture time.
MediaClock already excludes exactly the pause interval for both streams. Audio
mapping, mixer holdback and AAC buffering need no compensating constant.

Tests: **media_clock_pc3_pauses** reproduces a missing pre-pause capture and
40 ms post-resume capture latency over ten 5 s pauses/25 s active segments.
The old schedule loses one tick each cycle; the corrected audio-sample minus
video-tick offset stays at 6,667 us (+/-1 us). **audio_video_resume_beacons**
passes actual PCM pulses through AudioTimelineMixer over ten 5 s pauses and
compares detected output sample onsets with production scheduler ticks. Offset
stays 6,667 us; total output is exactly 528,000 samples for 11 active seconds.
Both tests are `ci` labelled.

## 3. Marker2 startup cast

The original `IMMDeviceEnumerator` declaration used
`BCDE0395-E52F-467C-8E3D-C4579291692E`, which is the **coclass CLSID**.
The constructor's `(IMMDeviceEnumerator)new MMDeviceEnumerator()` (original
line 107, fixed line 108) consequently asks QueryInterface for that CLSID and
fails. The correct interface IID is **A95664D2-9614-4F35-A746-DE8DB63617E6**.
Only the interface attribute changes; the coclass retains its CLSID. STA use,
Activate/GetService marshalling and marker timing/rendering are unchanged.
Construction precedes StreamWriter creation, explaining the absent log. This
source-level cause is definitive; WER method-token/IL mapping cannot be verified
without the original compiled executable.

The fixed file is both `/tmp/w20-data/marker2/marker2.cs` and the bundled source
`tools/marker2/marker2.cs`. `mcs`, `csc`, `mono`, and `dotnet` are absent here:
no C# compilation or Windows COM execution is claimed. On PC3 use the same .NET 4
csc command as before (`/target:winexe`, System.Drawing/System.Windows.Forms).

## 4. Memory: bounded application ownership, expected MP4 retention

No new per-frame leak was established. The raw byte values are 391,995,392 to
408,924,160, a rise of 16,928,768 bytes. The 15 samples span **14 minutes**, so
endpoint normalization is **72.55 MB/h**, or **671.78 bytes per captured frame**
at 30 fps; dividing by the nominal 15-minute recording gives the reported
67.72 MB/h. The final ten minutes (sample 5 onward) rise only 4,218,880 bytes:
**25.31 MB/h, 234.38 bytes/captured frame**. Startup/high-water allocations
materially affect the full-run extrapolation. There is no post-stop memory
sample to distinguish heap retention from driver retention or a true leak.

Audited ownership and growing storage:

- Mixer telemetry remains 18..21 system frames, 4..6 microphone frames,
  1..63 ahead samples; source queues are 0..2. Packet queues stay at 0..1 audio
  and 3..6 video. There is no steadily growing application backlog in this trace.
- Frame queues move refs; released frames and packets unref their opaque buffers.
  CaptureStamp is 24 bytes; AVBufferRef is 24 bytes plus shared buffer/allocation
  overhead. Restamping replaces the old ref. copy_props/ref/clone share the
  tagged buffer; held/preview references are bounded. Preview has one pending
  slot, and the encoder retains one last frame. No per-frame stamp map exists.
- MediaClock stores 16-byte pause intervals, not frame history: zero entries in
  this unpaused run. Diagnostics are scalar counters. Logging writes synchronously
  to QFile, without an accumulating in-memory log. Optional audio dump segments
  coalesce contiguous output and are not a frame-by-frame log in this run.
- Ordinary nonfragmented MP4 retains its per-packet sample index until trailer.
  The available FFmpeg source uses a 72-byte MOVIentry. At 23,581/900 video
  packets/s plus 48,000/1,024 AAC packets/s that predicts **18.94 MB/h**, or
  **175.38 bytes/captured frame**. At restored 30 fps it is 19.93 MB/h / 184.5
  bytes/captured frame. These are source-based estimates, not a PC3 heap profile;
  allocator capacity/slack and finalization add overhead. This is expected
  duration-dependent storage, also present in P0, rather than an unreachable
  allocation. MP4 format/edit-list behavior is preserved.

The encoder now reuses its retained AVFrame shell instead of allocating/freeing
one for every accepted picture. The tested native shell is 416 bytes, so this
avoids roughly 39.2 MB/h of shell allocation traffic at the observed rate (44.9
MB/h at 30 fps), **not that amount of retained memory or a proven leak fix**.
References are still unreffed before replacement and freed at worker exit.

**media_clock_pc3_stamps** (`ci`) runs 27,003 real FFmpeg reference lifecycles:
props copy, clone, queue move, reused retained frame and restamp. A custom buffer
free callback verifies precisely one old live stamp while held and zero after
release on every iteration. No native stamp leak appears. ASan/UBSan pass with
leak detection disabled; LeakSanitizer itself fails under this sandbox's ptrace,
so no LSAN result is claimed. Hardware D3D11/NVENC allocations are not exercised.
The unexplained early rise is unresolved; the <=50 MB/h field target requires
another sustained PC3 run and is not claimed as verified.

## Validation and delivery

- Native media-clock suite: 9 passed; the existing opaque_props graph test skips
  because the local FFmpeg lacks buffer/scale/format graph components.
- Native audio timeline suite: all 5 passed, including PCM/video resume beacons,
  AAC round-trip, drift, single-source clocks and mixer tests.
- Real encoder-worker loops with link-time codec fakes: video, audio, audio_sync,
  missing_stamp all pass. No hardware encoder result is claimed.
- All four changed C++ translation units compile to real MinGW objects with
  Qt 6.8 headers and Windows FFmpeg headers, both normal and with
  `-DSUBREC_STRIP_TEST_HOOKS=1` (8 compiles).
- ASan/UBSan: jitter, pauses and stamp lifetime tests pass (`detect_leaks=0`).
- Native tests use FFmpeg 8.0.3 in `/tmp/w18-r3-native`; commands:
  `cmake -S tests/media_clock -B /tmp/w20-clock-tests -DFFMPEG_ROOT=/tmp/w18-r3-native -DQt6_DIR=/tmp/w18-r3-qt`,
  `cmake -S tests/audio_timeline_mix -B /tmp/w20-mix-tests -DFFMPEG_ROOT=/tmp/w18-r3-native`,
  then build and run ctest with `LD_LIBRARY_PATH=/tmp/w18-r3-native/lib`.
- The supplied workspace is detached at the base and `.git` is mounted read-only.
  Commits are therefore made with copied metadata in `/tmp/w20-p1i.git`, using
  `/tmp/w20` as the worktree, on `p1-media-clock`. The requested bundle contains
  the commits above 9f510f3; the original read-only HEAD cannot be advanced here.
