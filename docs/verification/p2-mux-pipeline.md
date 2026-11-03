# P2: encode/mux layering, finalize states and recoverable output

Base `1d8a569`, branch `p2-mux-pipeline`. Plan: `SubRec_docs/P2_P3_PLAN.md` §2.
Only the design of OBS (encoder/output split, interleaving on the opposing stream,
skipping packet-level mux errors, always finalizing, hybrid MP4) was used; no OBS
code was copied.

## What changed

| Area | Files | Behavior |
|---|---|---|
| PacketGuard | `queue/packet_guard.*` | Per-stream check: fill missing pts/dts from the other, lower dts > pts, fill non-positive duration, drop missing timestamps and non-increasing dts. 30 consecutive drops become fatal. |
| Encoder output (2nd line) | `encoder/ffvencoder.*`, `encoder/ffaencoder.*` | The duplicated receive loops are merged into `drainPackets()`; every packet passes the guard before it is queued. A fatal guard result makes `encode()` fail, which the encoder threads already turn into `captureError` + stop. |
| Interleaver | `queue/packet_interleaver.*` | Two FIFOs ordered by dts in microseconds (ties to video). A head is written only when the other unfinished stream has a head at or after it. Lookahead of one packet per stream sets `duration = next.dts - dts`. Video before the first keyframe is dropped. Startup: up to 24 audio packets are staged before video, then the producer queue applies backpressure (AAC priming is never discarded). Steady state: if the other stream is silent for 1 s it is treated as absent (counted as a stall release). |
| IMuxSink | `muxer/mux_sink.h` | `write()` -> Ok/Skipped/Fatal; `finalize(budget)` -> Finalized / Recoverable / FailedNoData. |
| FFMuxer | `muxer/ffmuxer.*` | Implements IMuxSink. Muxer-side checks (3rd line) use PacketGuard. `av_interleaved_write_frame` EINVAL/INVALIDDATA are skipped (30 in a row or 100 per minute are fatal). `requestAbort()` interrupts I/O at once only before any packet was written; afterwards it only stops accepting packets and arms the finalize budget. `movflags=+hybrid_fragmented+frag_keyframe`, `frag_duration=2000000`, `flush_packets=1`. After the header, a warning is logged if a container time base cannot represent every encoder tick. |
| Mux worker | `thread/ffmuxerthread.*` | Non-blocking pull into the interleaver, write, and on every exit `finalize(capture_stop::timeout())`; `finalizeState()` is exposed for P4. `Recoverable` is reported through the existing mux-error path. |

Unchanged on purpose: all timestamp values (video tick, audio sample index), AAC
priming / edit lists, `recorder/**`, `main.cpp`, the encoder threads, top-level CMake.

## Diagnostics

`[MuxThread] closed ...` keeps the old fields and adds `rejected_writes`,
`duration_adjusted`, `dropped_before_keyframe`, `finalize_state`. `interleaveStalls`
now counts stall releases (previously only observed waits). `[Muxer] finalize_state=`
and `[MuxThread] finalize_state=` are logged once per session; encoders log
`packet guard accepted=... dropped=...` on close.

## Linux verification (this sandbox)

Native FFmpeg 8 (`/tmp/w18-r3-native`) and Qt 6.8 (`/tmp/qtl/6.8.0/gcc_64`):

- `tests/muxer_lifecycle`: 10/10 pass (7 existing + `packet_guard`,
  `packet_interleaver`, `finalize_state`). `aac_pipeline_priming` still covers the
  2.3 s late video, missing EOFs and the bootstrap/backpressure diagnostics.
- `tests/encoder_failure`: 4/4 pass (fake FFMuxer gained the IMuxSink overrides).
- `packet_guard` and `packet_interleaver` also pass under ASan/UBSan (leak detection off).
- `finalize_state` crash test: a forked writer produces 20 s of AAC + keyframes and
  exits without a trailer; the file contains `moof`, opens with libavformat and
  decodes 18.0 s of audio (lost ≤ one 2 s fragment). Without `flush_packets` it was 15.0 s.
- MinGW (`x86_64-w64-mingw32-g++ -fsyntax-only`, Qt 6.8 MinGW headers), normal and
  `SUBREC_STRIP_TEST_HOOKS=1`: all changed translation units compile (only the
  pre-existing `QCryptographicHash::addData` deprecation warnings).

Not verified here: hardware encoders (NVENC/AMF/QSV), the full Windows application
link and run, the PowerShell `mp4_edit_list_parser` test, and stop time of a 30-minute
hybrid file on a real disk.

## Known limitations / log notes (PC3, 2026-11)

- A `[MuxThread] closed … finalize_state= not_finalized` line before `run() start` is
  expected: initVideo's reset closes a muxer thread that never ran. The real finalize
  state is logged when recording stops.
