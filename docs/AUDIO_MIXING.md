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
  gain with instantaneous attack, 50 ms exponential release and a 0.95 ceiling.
  It attenuates peaks instead of hard clipping. It is not a true-peak/oversampling
  limiter; the PCM bound does not guarantee an identical AAC reconstructed peak.
* On pause, inputs continue draining and output within the capture-time pause
  interval is discarded. Frames crossing a pause boundary are split, so the
  200 ms mix wait window does not move the pause point. On normal stop, both inputs reach EOF
  before the shared encoder receives finish. Abort closes queues to unblock producers.

The existing FFmpeg build already contains `amix` and `aresample` (as well as the
buffer source/sink). No additional FFmpeg filters, libraries or rebuild are needed;
`alimiter` is replaced by the limiter above. `build-ffmpeg.sh` is unchanged.

## Verification

`tests/audio_timeline_mix` is a standalone CMake/CTest project (same FFMPEG_ROOT as
other native tests). It is also registered by the main audio_filter_dual test build.
It exercises nonzero clock epochs, late startup, gaps, EOF, late packets, drift,
44.1 kHz mono resampling/drain, packed S16 microphone input and stereo-linked
no-clipping behavior. `audio_filter_dual` additionally exercises the production
filter worker with both queues and a pause interval crossing frame boundaries.

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
