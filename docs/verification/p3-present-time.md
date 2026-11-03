# P3a: presentation-time video stamps and P1 residual A/V offset

Base `1d8a569`, branch `p3-present-time`. Plan: `SubRec_docs/P2_P3_PLAN.md` §3.
P3b (capture decoupling / ACCESS_LOST recovery) is not part of this branch.

## Facts this work starts from

- P1 PC3, system audio only: A/V offset median about 75 ms (range 13–90 ms); 3 of 180
  pairs above 100 ms (worst 111.7 ms). Positive = sound later than picture.
- marker2's own queueing delay is a constant 23.5 ms (beep behind WASAPI padding).
- Video was stamped with the *receive* time after `buffersink` (`thread/ffscreengrabthread.cpp`),
  i.e. after the ddagrab tick, the texture copy, pointer drawing and the graph.
- `VideoTickScheduler::decidePaced` (P1i) removes per-frame jitter once paced, so the
  stamp source mainly decides the session's *phase*; a receive stamp carries the random
  latency of the frames that set that phase.

## Changes

| Part | Files | Behavior |
|---|---|---|
| FFmpeg patch (not built here) | `packaging/ffmpeg/patches/0001-ddagrab-export-present-time.patch`, `build-ffmpeg.sh`, `collect-sources.sh`, `README.md` | ddagrab exports `DXGI_OUTDUPL_FRAME_INFO.LastPresentTime` as `ddagrab.present_qpc` and `ddagrab.present_new` frame metadata. Build scripts apply it to an exported copy; the pinned checkout stays pristine; patches ship with the corresponding source. |
| QPC mapping | `capture/qpc_session_clock.*` | Process-wide (QPC, `av_gettime_relative`) anchors, refreshed every 10 s (best of three bracketed reads), least squares over the last 16 anchors. Slope, residual and drift are logged. |
| Stamp rules | `capture/present_stamp.h` | New image: presentation time. Duplicate / pointer-only: last new image + k frames, capped at receive time. Missing or implausible (future, > 250 ms old): receive time, counted. Strictly increasing. |
| Video | `thread/ffscreengrabthread.cpp`, `capture/ddagrab_screen_source.cpp` (comment) | Stamps through PresentStamper; warns once and counts `present_fallback` without the metadata. `[FFScreenGrabThread] present_stamp progress/summary` lines. |
| Audio | `capture/wasapi_loopback_capture.cpp` | GetBuffer QPC mapped through the same QpcSessionClock (one-shot start pair only as fallback). |
| Beacon tools | `tools/marker2/marker2.cs`, `tools/marker2/analyze_beacons.py` | marker2 logs `beep_qpc`, `flash_qpc` (after `DwmFlush`), `beacon_delay_ms`, `qpc_freq`. The analyzer pairs one-to-one, applies edit lists via ffmpeg, and reports raw and beacon-corrected offsets. |

No constant offset correction was added anywhere.

## Expected effect (hypotheses, to be measured on PC3)

1. Raw median moves toward the beacon skew plus the vsync/compose time, roughly
   23.5 ms + 0–17 ms, instead of ~75 ms. Corrected median (raw − beacon delay) should be
   within ±20 ms.
2. Spread (p95 − p5) shrinks, because the phase no longer contains the random receive
   latency of the first frames after start/resume. Simulation (`media_clock_present`,
   200 sessions): phase error ≤ 16.7 ms with presentation stamps vs up to 50 ms with
   receive stamps.
3. If `qpc_residual_us` is in the hundreds of microseconds or `qpc_drift_us` grows by
   milliseconds, `av_gettime_relative()` is not QPC based in the shipped FFmpeg (H3).
   The shared mapping already keeps audio and video consistent in that case.
4. If the corrected median stays near 50 ms, the remainder is elsewhere (WASAPI loopback
   path or analysis); the logs then separate it: `latency_mean_us` is receive − present.

## Linux verification (this sandbox)

- `tests/media_clock`: 12 tests pass (10 existing + `media_clock_qpc`, `media_clock_present`;
  `media_clock_opaque_props` is skipped as before because the local FFmpeg lacks graph filters).
  QPC: 80 ppm skew estimated at 80.0 ppm, residual 21 µs, drift 144.03 ms over 30 min,
  worst mapping error < 40 µs.
- `tests/startup_failure` 5/5 and `tests/test_hooks` 8/8 (they compile the grab thread).
- `analyze_beacons.py --selftest`: synthetic 30 s MP4 (AAC + edit list), 15/15 pairs,
  raw 40.0 ms, corrected 24.0 ms, spread 0.0 ms.
- MinGW `-fsyntax-only` (normal and `SUBREC_STRIP_TEST_HOOKS=1`) with the Windows SDK
  headers: `qpc_session_clock.cpp`, `wasapi_loopback_capture.cpp`, `ffscreengrabthread.cpp`,
  `ddagrab_screen_source.cpp`, the new test — no errors or warnings.
- FFmpeg patch: `patch --dry-run -p1` applies to n8.0.3; the patched `vsrc_ddagrab.c` parses
  with MinGW + d3d11/dxgi headers (only an unrelated `NULL_IF_CONFIG_SMALL` error from the
  hand-made `config.h`).

Not verified: building the patched FFmpeg, D3D11/WASAPI at run time, marker2 compilation
(no C# compiler here), and any PC3 measurement.

## Known limitations (PC3, 2026-11)

- V3 not met: beacon-corrected A/V offset median 37–46 ms, p5–p95 spread ≈47–71 ms
  (L1/L2/B1). Beacon flashes land on two frame phases (x.500 / x.533), producing two
  clusters ~33 ms apart at 30 fps, so the p95−p5 ≤ 25 ms criterion is not achievable as
  specified; needs per-phase evaluation or 60 fps measurement. Accepted for now.
- V4 partial: slotCollisions 33 per 30 min (L2) vs ≈6 on the unpatched baseline; follow-up tuning.
- Baseline (unpatched) runs must replace the FFmpeg DLLs that CMake copies into the build
  directory; switching PATH alone keeps the patched DLLs loaded.
- The patched FFmpeg package is first built by CI with this change.
