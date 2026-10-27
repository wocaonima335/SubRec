# System audio + microphone mixing

Select a microphone alongside the WASAPI system source through
`CaptureScheduler::selectMixMicrophone(deviceId)`. Selecting a single source with
`changeAudioSource()` disables mixing. A mix requires WASAPI; a failed WASAPI open
is reported instead of silently switching the mixed recording to DirectShow.

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
No new QML controls are added; the scheduler API and autotest expose this mode.

## Timing and format

* WASAPI attaches the packet QPC timestamp, mapped to `av_gettime_relative`, to
  output frames (including swresample delay). Its single-source sample-count PTS
  contract remains unchanged; only the mix adapter consumes capture timestamps.
* DirectShow preserves packet PTS deltas. Its clock epoch is calibrated when the
  demux thread receives its first packet, subtracting the PCM packet duration.
  This starts before video preparation to avoid calibrating against a backed-up
  decoder queue. Missing PTS uses receive time minus packet duration. The absolute
  calibration cannot measure the microphone driver's hidden fixed buffering latency;
  physical loopback/impulse measurement on PC2 is still needed for exact offset.
* The mix worker converts each actual input layout/rate/format with swresample to
  48 kHz stereo FLTP, accounting for resampler delay. Every packet is positioned on
  the shared session timeline; it does not replace device PTS with sample counts.
* A 200 ms wait window accommodates scheduling and the 50 ms DirectShow buffer.
  Missing intervals, late startup and an ended input produce silence. Late/overlapping
  samples are trimmed rather than shifting future audio; rate drift therefore does
  not accumulate as an unbounded inter-source offset. This first implementation
  corrects with silence/trim, not gradual resampling compensation; timestamp jitter
  or large overruns may produce audible transitions.
* Both `abuffer` sources get aligned PTS in 1/48000 time base. `amix normalize=0`
  sums the selected gains. A stereo-linked sample-peak limiter then applies common
  gain with instantaneous attack, 50 ms exponential release and a **0.84 ceiling**
  (-1.51 dBFS), followed by **0.4 AAC headroom gain** (-7.96 dB). The encoder input
  is therefore bounded to **0.336 (-9.47 dBFS)**. Both channels receive the same
  gain; there is no hard clipping, extra buffering, sample insertion or PTS shift.
  These constants live in `AudioTimelineMixer`; the worker logs both gains and
  the resulting PCM ceiling. `limited_samples` counts limiter activity before
  the fixed headroom gain, not clipped AAC samples.
* The substantial headroom is intentional for the current native AAC encoder at
  **65536 bps, 48 kHz stereo FLTP**, with default codec options. Reducing the
  ceiling to 0.84 alone was insufficient: even 0.84 * 0.5 produced a +0.77 dBFS
  decoded peak on the square-wave regression. Mixed recordings are quieter;
  per-source volume controls still apply before limiting. Single-source recordings
  bypass this mixer and retain their existing behavior.
* This is a sample-peak limiter with empirically tested codec headroom, not a
  certified true-peak limiter. The regression also evaluates 8x bandlimited
  reconstruction with swresample, including filter tails. This estimates intersample
  peaks; it is not a BS.1770-conformance measurement or a mathematical bound for
  every possible waveform/codec. Re-run it when changing gains or AAC settings,
  and recheck the actual PC2 MP4 decode before reducing the headroom.
* On pause, inputs continue draining and output within the capture-time pause
  interval is discarded. Frames crossing a pause boundary are split, so the
  200 ms mix wait window does not move the pause point. On normal stop, both inputs reach EOF
  before the shared encoder receives finish. Abort closes queues to unblock producers.

The existing FFmpeg build already contains `amix` and `aresample` (as well as the
buffer source/sink). No additional production filters are needed; `alimiter` is
replaced by the limiter above. **Rebuild FFmpeg with `SUBREC_FFMPEG_TEST_COMPONENTS=1` for
AAC verification**: `packaging/ffmpeg/build-ffmpeg.sh` now adds the `aac` decoder
alongside `wrapped_avframe` in that branch (`--enable-decoder=...,aac`). The supplied
Windows build has the AAC encoder but lacks the AAC decoder. Recording can use
that existing build; the new test fails with an explicit rebuild message when the
decoder is missing, rather than silently skipping peak verification.

## Verification

`tests/audio_timeline_mix` is a standalone CMake/CTest project (same FFMPEG_ROOT as
other native tests). It is also registered by the main audio_filter_dual test build.
It exercises nonzero clock epochs, late startup, gaps, EOF, late packets, drift,
44.1 kHz mono resampling/drain, packed S16 microphone input and stereo-linked
no-clipping behavior. `audio_filter_dual` additionally exercises the production
filter worker with both queues and a pause interval crossing frame boundaries.

`audio_mix_aac_tests` is registered in both test builds (`ctest -R audio_mix_aac
--output-on-failure`). It sends two 0.99-amplitude sources through the production
mixer, then the native `aac` encoder with the exact `FFAEncoder::initAudio`
settings, and decodes the resulting packets using the encoder's codec parameters.
It covers 440/997 Hz sine, square waves, gated tones with impulses, and high-frequency
sources with differing phases. It flushes both codecs, measures priming/padding too,
and asserts decoded sample peak **< 1.0**, 8x reconstructed peak **< 1.0**, and
RMS **> 0.01**. A short last input frame exercises final codec draining. No muxer,
AAC parser, device or Qt runtime is required by this standalone test.

Measured using native Linux FFmpeg **n8.0.3**, 65536 bps, default AAC settings:

| Input (both sources near full scale) | Old 0.95 ceiling: decoded peak dBFS | New decoded peak dBFS | New 8x reconstructed peak (linear) |
| --- | ---: | ---: | ---: |
| 440/997 Hz sine | +3.81 | -4.91 | 0.569 |
| Square waves | +7.77 | -1.34 | 0.859 |
| Gated sine / impulses | +4.52 | -4.50 | 0.596 |
| High frequencies / differing phases | +5.50 | -3.52 | 0.684 |

The original limiter fails this regression; the updated mixer passes it and the
existing timeline/dump tests. Both standalone test executables compile and link
with MinGW against the supplied Windows FFmpeg. Windows/Qt recording and the
PC2-specific AAC decode still need to be verified on PC2 after the decoder rebuild.

`tools/pc2_verify.ps1 -MicDevice '<device name>' ...` additionally records while
playing its generated tone with both sources enabled. The log must contain
`[AudioMix] finished system_frames= ... mic_frames= ...` with nonzero counts.
The summary also reports generated silence, trimmed samples and limiter activity.

Validation on Linux: MinGW compile/link against the supplied Windows FFmpeg build;
core, WASAPI pipeline and mixed-worker tests executed against a temporary native
build of the same n8.0.3 sources (worker Qt logging/UI calls use temporary stubs).
ASan/UBSan core tests passed with leak detection disabled because this sandbox
runs under ptrace; LeakSanitizer could not run.
Full Qt/Windows recording, driver latency, long-run clock drift, pause/resume,
start/cancel/stop races and listening tests still require PC2.

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
before `amix`, the limiter and AAC headroom gain. Neither attenuation is present
in the dumps; multiply by the headroom gain as well when comparing amplitudes.
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
