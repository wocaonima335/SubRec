# System audio + microphone mixing

The audio buttons are independent. The scheduler exposes `audioMode` values
`system`, `microphone`, `both`, and `none`; `setMixMicrophone(deviceId)` enables
the `both` mode and `clearMix()` returns a mix to system-only (other modes are unchanged). A mix requires a WASAPI
system source and a DirectShow microphone. When the selected system backend is
DirectShow, the UI disables the combination and shows the reason. Capture
configuration changes are refused while recording is active.

The microphone dropdown in Settings remembers the microphone independently of
the current mode. Enabling both uses that microphone, or the first available
microphone if none is selected. Switching from both to either single source
clears the mix. All mode/device changes are refused during Preparing, Running,
Paused, and Stopping; mode notifications also cover mix and device changes.

When both buttons are off (`none`), SubRec writes a **video-only MP4 with no audio
track**. It opens no audio device and starts no audio workers. The muxer expects
only the video stream, and its worker receives no audio queue, so neither header
creation nor packet interleaving waits for absent audio. RecorderSyncQueue uses
exact rational video frame counting instead of an audio clock in this mode;
paused frames are discarded without advancing PTS. Each session resets the mode
and clock, so switching back to audio restores normal A/V synchronization.
This avoids encoding a synthetic silent AAC track.

The `ci` tests `scheduler_audio_modes` cover transitions and active-capture
refusal. `video_only_pipeline` exercises production sync/queue/mux code, pause
and restart, then reads and decodes the MP4 to verify one video stream and
continuous timestamps (requires the test FFmpeg MOV demuxer/H.264 decoder).

For the existing record_seconds autotest, both variables now take effect together:

```powershell
$env:SUBREC_AUTOTEST_SCENARIO = 'record_seconds'
$env:SUBREC_AUTOTEST_RECORD_MS = '5000'
$env:SUBREC_AUTOTEST_SYSTEM_AUDIO = '1'
$env:SUBREC_AUTOTEST_MIC_DEVICE = '麦克风 (iKF-Solo)'
$env:SUBREC_SYSTEM_AUDIO_BACKEND = 'wasapi'
$env:SUBREC_AUTOTEST_OUTPUT = 'C:\Temp\mixed.mp4'
.\appSubRec.exe
```

With only MIC_DEVICE, recording remains microphone-only. With only SYSTEM_AUDIO=1,
recording remains system-only. Mixing still uses one encoder and one AAC track.
The QML controls and scheduler API expose all four modes. Existing autotest
variables continue to select microphone-only or mixed recordings.

## Timing and format

* WASAPI attaches the packet QPC timestamp, mapped to `av_gettime_relative`, to
  output frames (including swresample delay). Its single-source sample-count PTS
  contract (1/48000) remains unchanged. In mix mode only, `QueueAudioSink`
  overwrites `pts` with that `capture_time_us` and sets `time_base` to 1/1000000
  before enqueueing (the reference is moved, not copied); see
  [WASAPI_LOOPBACK_INTERFACE.md](WASAPI_LOOPBACK_INTERFACE.md) section 4.1.
* DirectShow preserves packet PTS deltas. Its clock epoch is calibrated when the
  demux thread receives its first packet, subtracting the PCM packet duration.
  This starts before video preparation to avoid calibrating against a backed-up
  decoder queue. Missing PTS uses receive time minus packet duration. The absolute
  calibration cannot measure the microphone driver's hidden fixed buffering latency;
  physical loopback/impulse measurement on PC2 is still needed for exact offset.
* The mix worker converts each actual input layout/rate/format with swresample to
  48 kHz stereo FLTP, accounting for resampler delay. In
  `filter/audio_timeline_mixer.cpp`, consecutive packets within **10 ms** of the
  predicted `nextUs` advance continuously from converted samples and resampler
  delay. Device PTS is an observation of clock error, not a new position for every
  packet. Format changes or deviations exceeding 10 ms flush/reinitialize the
  resampler and reposition on the device timestamp.
* Long-term drift uses a per-source PI controller: it averages one second of PTS
  errors, uses proportional gain 0.2 and integral gain 0.01, and calls
  `swr_set_compensation(delta, 480000)`. Delta is the total sample adjustment over
  **10 seconds** of output, refreshed about once per source second. The adjustment
  slews by at most approximately **50 ppm per second**, with a desired correction
  clamped to ±1000 ppm; 50 ppm is not an absolute rate limit. This allows gradual
  tracking of clocks differing by 50–200 ppm without packet-boundary jumps.
* A 200 ms wait window accommodates scheduling and the 50 ms DirectShow buffer.
  Missing intervals, late startup and an ended input produce silence. Late/overlapping
  samples are trimmed. Large discontinuities still require silence/trim and can
  produce audible transitions. Packets more than two seconds ahead of the output
  cursor are dropped and counted before changing timeline state.
* Both `abuffer` sources get aligned PTS in 1/48000 time base. `amix normalize=0`
  sums the sources after their per-source gains (default 1.0 each). A stereo-linked
  sample-peak limiter then applies common gain with instantaneous attack, 50 ms
  exponential release and a **0.7079 ceiling** (approximately **−3 dBFS**).
  There is **no additional AAC headroom multiplier**. The release coefficient is
  precomputed in the constructor. There is no lookahead, extra buffering, sample
  insertion or PTS shift in the limiter. See `filter/audio_timeline_mixer.h/.cpp`.
* `[AudioMix] finished` in `thread/ffafilterthread.cpp` reports
  `over_ceiling_samples` and `over_ceiling_ratio`: stereo sample frames whose
  **pre-limiter** peak exceeds the ceiling. Release-only attenuation does not
  count. `max_gain_reduction_db` measures the maximum applied gain attenuation.
  These are not counts of clipped AAC samples.
* Native AAC uses **128 × 1024 = 131072 bps**, 48 kHz stereo FLTP and default
  codec options (`encoder/audio_encoding_config.h`, `encoder/ffaencoder.cpp`).
  The mixed PCM is bounded to 0.7079 (within floating-point rounding), but AAC
  encoding/decoding can overshoot that peak. This is a sample-peak limiter, not a
  certified true-peak limiter or a universal guarantee against decoded overshoot.
  Single-source recordings bypass this mixer and retain their existing behavior.
* The codec regression estimates intersample peaks with 8x bandlimited
  reconstruction using swresample, including filter tails. This is not a BS.1770
  conformance measurement. Sine, synthetic music and supplied WAV material must
  have decoded sample peak **< −0.5 dBFS** and reconstructed peak **< 1.0**.
  Extreme square/transient/high-frequency fixtures allow sample peaks up to
  **+6 dBFS** because native AAC can overshoot sharp waveforms; their 8x peaks are
  report-only. See the exact assertions and RMS floors below.
* On pause, inputs continue draining and output within the capture-time pause
  interval is discarded. Frames crossing a pause boundary are split, so the
  200 ms mix wait window does not move the pause point. On normal stop, both inputs reach EOF
  before the shared encoder receives finish. Abort closes queues to unblock producers.

The existing FFmpeg build already contains `amix` and `aresample` (as well as the
buffer source/sink). No additional production filters are needed; `alimiter` is
replaced by the limiter above. **Rebuild FFmpeg with `SUBREC_FFMPEG_TEST_COMPONENTS=1` for
AAC verification**: `packaging/ffmpeg/build-ffmpeg.sh` now adds the `aac` decoder
alongside `wrapped_avframe` in that branch (`--enable-decoder=...,aac`).
The CI release build uses `SUBREC_FFMPEG_TEST_COMPONENTS=0`, so it has the AAC
encoder but no AAC decoder. `audio_mix_aac` is labelled
`needs_ffmpeg_test_components` and excluded from release-FFmpeg CI runs; direct
execution without the decoder fails with an explicit rebuild message.

## Verification

`tests/audio_timeline_mix` is a standalone CMake/CTest project (same FFMPEG_ROOT as
other native tests). The top-level test build adds this directory directly,
without duplicate registration by `audio_filter_dual`.
It exercises nonzero clock epochs, late startup, gaps, EOF, late packets, drift,
44.1 kHz mono resampling/drain, packed S16 microphone input and stereo-linked
no-clipping behavior. `audio_filter_dual` additionally exercises the production
filter worker with both queues and a pause interval crossing frame boundaries.

`audio_mix_aac_tests` is registered in both test builds (`ctest -R audio_mix_aac
--output-on-failure`). It sends two 0.99-amplitude sources through the production
mixer, then the native `aac` encoder with the exact `FFAEncoder::initAudio`
settings, and decodes the resulting packets using the encoder's codec parameters.
It covers 440/997 Hz sine, square waves, gated tones with impulses, high-frequency
sources with differing phases, and built-in music-like material (harmonics,
envelopes and noise, crest factor around 12–15 dB). Optional
`SUBREC_AUDIO_MIX_AAC_SYSTEM` / `SUBREC_AUDIO_MIX_AAC_MIC` paths supply 48 kHz WAV
material; unset paths print SKIP for the WAV portion while synthetic music still
runs. WAV fixtures are read by the test's own parser, not a WAV muxer.

All scenarios require finite decoded samples and an RMS floor. Synthetic floors
are measured baselines minus 1.5 dB; WAV uses the mixed PCM RMS adjusted for codec
padding minus 1.5 dB. Sine/music/WAV enforce decoded sample peak < −0.5 dBFS and
8x peak < 1.0. Square/transient/high-frequency cases enforce PCM peak
<= 0.7079 + 1e-6 and decoded sample peak <= +6 dBFS, while reporting 8x peaks.
The +6 dBFS guard catches extreme amplification beyond the accepted native AAC
overshoot; it does not certify those pathological fixtures as unclipped playback.
Exact baselines and the named extreme-peak limit are in
`tests/audio_timeline_mix/audio_mix_aac_tests.cpp`.

Both codecs are flushed, and priming/padding is included in peak measurements.
A short last input frame exercises final draining. No muxer, AAC parser, device
or Qt runtime is required by this standalone test. Re-run the tests and check
actual recorded MP4 decoding when changing gains or encoder settings; historical
measurements from the old 65536 bps / 0.336 PCM ceiling are not current acceptance
results.

`tools/pc2_verify.ps1 -MicDevice '<device name>' ...` additionally records while
playing its generated tone with both sources enabled. The log must contain
`[AudioMix] finished system_frames= ... mic_frames= ...` with nonzero counts.
The summary also reports generated silence, trimmed samples and limiter activity.

Hardware-independent tests do not replace Windows/Qt recording checks. Driver
latency, physical clock drift, pause/resume, startup/stop races and listening
tests still need verification with the actual PC2/PC3 devices.

## Optional pre-mix debug dumps

Set `SUBREC_AUDIO_MIX_DUMP_DIR` to a directory before starting a mixed recording:

```powershell
$env:SUBREC_AUDIO_MIX_DUMP_DIR = 'C:\Temp\SubRec-audio-dumps'
```

The variable works with normal scheduler mixing and AutoTest; no additional CLI
flag is needed. Unset/empty means disabled. Single-source recording does not
instantiate the mixer and does not create dumps. Windows Unicode paths are supported.

Each session creates a new `mix-<capture-clock-start-us>-<sequence>` subdirectory,
containing `system.f32le`, `microphone.f32le` and one `.json` sidecar for each.
Existing sessions are never overwritten. These are **raw IEEE-754 float32,
little-endian, interleaved L/R, 48000 Hz stereo** files: eight bytes per sample
frame. They do not need a WAV/raw muxer or any change to the minimal FFmpeg build.

The tap is after resampling, alignment, silence insertion and per-source gain,
before `amix` and the limiter. Limiter attenuation is not present in the dumps;
there is no separate AAC headroom gain to apply when comparing amplitudes.
Initial silence, missing-input gaps and trailing silence are included. File sample
frame N (byte offset `N * 8`) corresponds to recorded mixed PCM sample N before
AAC encoding. Paused/discarded frames are omitted from **both** dumps, just as they
are omitted from the encoder input.

Both sidecars specify `start_pts: 0`, `time_base: [1,48000]`,
`offset_from_mix_output_pts_zero_samples: 0`, sample rate, channel order, valid
`sample_frames` and `payload_bytes`. `capture_clock_start_us` is the original
session epoch on the capture clock. `capture_timeline_start_pts` and
`capture_segments` map the compact recorded timeline back to the original
capture timeline: each segment has `file_sample`, `capture_pts`, and `samples`.
For example, `{file_sample: 240, capture_pts: 720, samples: 304}` means dump
sample 240 corresponds to original capture sample 720 after a 480-sample pause.
Both files share the same mapping, including when one source starts late.

File I/O runs on a bounded background worker. Open/write/metadata failures or
queue overflow disable only the dump and emit an `[AudioMixDump]` warning;
recording keeps running. On failure, both raw files are truncated to their last
successfully written common prefix when the filesystem permits it. Metadata
starts with `complete: false` and is finalized when the mixer is destroyed.
Use `complete: true` sessions for full-recording comparison; a crash or failing
filesystem can leave incomplete files/sidecars. A filesystem failure can also
prevent the final status update itself.

`audio_timeline_mix` tests cover disabled mode, Unicode paths, exact interleaved
samples, startup/gap/trailing silence, pause mapping, resampling before the tap,
limiting after the tap, separate sessions, and an invalid destination without
changing mixed output. On Linux, an isolated child with a file-size limit also
injects a real short write and checks that recording output is unaffected and
both dump files retain the same valid prefix.

## Video startup test hooks

`SUBREC_TEST_VIDEO_START_DELAY_MS=1..60000` delays the first video packet/frame;
`SUBREC_TEST_VIDEO_OPEN_TIMEOUT=1` simulates a ddagrab open timeout. In application
builds they take effect only when `SUBREC_AUTOTEST_SCENARIO` is nonempty after
trimming (AutoTest mode). Setting either hook alone during normal GUI recording
has no effect. Active hooks retain their `[TestHook]` log messages.

`capture/test_hook_gate.h` centralizes this gate. Standalone fake-device tests
can explicitly compile with `SUBREC_ENABLE_TEST_HOOKS=1`; the application modules
never receive that definition merely because `SUBREC_BUILD_LIFECYCLE_TESTS=ON`.
`tests/startup_failure` uses the test definition; the real-module
`capture_lifecycle_tests` fixture temporarily sets an AutoTest scenario.
`tests/test_hooks` checks production gating and the explicit test-build opt-in.
Its independent `runtime_hook_gate` CTest exercises the real screen worker with
fake native input and the runtime gate (no `SUBREC_ENABLE_TEST_HOOKS`, NUT or
FFmpeg test components). The full NUT-backed lifecycle regression remains.
The application and gate share `autoTestScenarioActive()` for whitespace checks,
matching `QChar::isSpace()` including NBSP and ideographic/line spaces. Windows
reads UTF-16 via `GetEnvironmentVariableW`; other platforms decode UTF-8.

The GitHub release configuration sets `SUBREC_STRIP_TEST_HOOKS=ON`. This compiles the
two hook paths and their environment names out of the application modules; the
separate `build/ci-tests` configuration leaves it OFF so hook and startup tests
remain available. The option defaults to OFF. Combining it with
`SUBREC_BUILD_LIFECYCLE_TESTS=ON` is a configure error; defining both strip and
`SUBREC_ENABLE_TEST_HOOKS=1` in one target is a compile error. The independent
`test_hooks_strip` target always checks the stripped variant with static assertions.
