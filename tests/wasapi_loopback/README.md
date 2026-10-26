# WASAPI loopback tests

The hardware-independent `AudioPipeline` accepts injected decoded PCM packets and a fake
`AudioFrameSink`. Tests cover float 48 kHz / 44.1 kHz / 5.1 input, fixed stereo FLTP output,
continuous PTS, resampler drain, idle sample counts, backwards clock targets, abort without
EOF and a closed sink. The executable does not require an audio device or Qt.

Build on the Windows FFmpeg toolchain:

```
cmake -S tests/wasapi_loopback -B build/wasapi-tests -DFFMPEG_ROOT=<shared-ffmpeg-root>
cmake --build build/wasapi-tests
ctest --test-dir build/wasapi-tests --output-on-failure
```

`tools/pc2_verify.ps1` includes this test project, then plays a generated WAV in a loop and
records five seconds with the native source, stops playback, and records five seconds again.
It requires the native startup log (fallback cannot pass), AAC/H.264 tracks and duration
within 1.5 seconds. Stop other audio applications before the idle case. Playback uses the
Windows default SoundPlayer endpoint; verify that it matches the selected console endpoint.

Still requires Windows manual testing: unplug/replug playback device, switch default device,
stop/restart Audiosrv, queue backpressure, repeated start/stop/cancel, pause/resume, 44.1 kHz
and multichannel endpoint formats, and listening / A/V synchronization checks. Automated
readback verifies tracks and duration, not audible content or lip sync.

Linux validation performed: MinGW syntax checks for native COM/packet RAII and PCM pipeline;
CMake cross-build of this test executable, including FFmpeg linking. No Windows tests were
executed on Linux. Qt application compilation requires a real Windows Qt kit.
