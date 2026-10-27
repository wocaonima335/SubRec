# Build warnings of `build-ffmpeg.sh` (first real build)

Source: first two real runs (`gpl` variant, GCC 14-posix mingw-w64 cross toolchain on Debian 13,
FFmpeg n8.0.3 `8ae0b349`, x264 stable `b35605ac`, libvpl v2.16.0). Run 1 built the DLLs, run 2
added `ffmpeg.exe`; the x264/FFmpeg warnings repeat identically in both. Every line that contains
`warning` (or `CMake Warning`) in the two logs was classified: 48 log lines = 8 distinct
warnings. Nothing was patched: all sources stay byte-identical to the pinned commits.

Verdict legend: **harmless** = no change needed; **fixed** = problem was in our own scripts and is
already corrected in the same commit as this file.

| # | Warning (file:line, flag) | Count per run | Verdict |
| --- | --- | --- | --- |
| 1 | libvpl CMake: `Manually-specified variables were not used by the project: BUILD_TOOLS` | 1 (run 1) | **fixed** (script nit) |
| 2 | x264 `configure`: `Unknown option --enable-win32thread, ignored` | 1 per run (printed by configure; not a compiler warning) | **fixed** (script nit) |
| 3 | x264 `common/cpu.c:111` `x264_getauxval` defined but not used `-Wunused-function` | 1 | harmless |
| 4 | x264 `encoder/rdo.c:834-857` `dangling pointer to 'ssd0'/'ssd1'/'cost_siglast' may be used` `-Wdangling-pointer=` | 17 | harmless |
| 5 | x264 `encoder/ratecontrol.c:2082` `array subscript [0, 2] is outside array bounds of 'double[3]'` `-Warray-bounds=` | 1 | harmless |
| 6 | FFmpeg `libavcodec/h2645_sei.c:242` label `unsupported_provider_code` defined but not used `-Wunused-label` | 1 | harmless |
| 7 | FFmpeg `libavcodec/vlc.c:443` array subscript 3/4/5 above bounds of `uint16_t[3]` `-Warray-bounds=` | 3 | harmless |
| 8 | FFmpeg `fftools/cmdutils.c:966` `strncat` specified bound 10 equals source length `-Wstringop-overflow=` (run 2 only, `ffmpeg.exe`) | 1 | harmless |

No warning came from SubRec's own code, from nv-codec-headers, AMF, the linker or the
post-build checks (import check, strip, hash, license guard all passed silently).

## Reasoning per entry

### 1. libvpl `BUILD_TOOLS` unused (fixed)
The option does not exist in libvpl v2.16.0 (the examples/tools are controlled by
`BUILD_EXAMPLES`, `INSTALL_EXAMPLES`, `BUILD_TESTS`, which are accepted and were verified to be
used). Passing an unknown `-D` only makes CMake print this notice; it cannot change the result.
Removed `-DBUILD_TOOLS=OFF` from `build-ffmpeg.sh` (and the stale "verify" comment), so the
notice no longer appears. The built `libvpl.a` is unaffected, so existing binaries stay valid.

### 2. x264 `--enable-win32thread` unknown (fixed)
x264's configure only knows `--disable-win32thread`. On Windows targets the default (`thread=auto`)
already selects Win32 threads, and the log proves it: `common/win32thread.o` is compiled and
linked into `libx264.a`, and no `pthread` library was probed. The flag was therefore a no-op.
Removed it from `build-ffmpeg.sh`. (Consequence checked: the import check of the DLLs shows no
`libwinpthread-1.dll`, so the x264 code does not need it.)

### 3. x264 `x264_getauxval` unused (harmless)
`getauxval()` exists only on Linux/ELF. x264 defines the wrapper unconditionally and uses it
only in the ARM/PPC/LoongArch CPU-detection branches; on x86-64 Windows nothing calls it and the
compiler drops it. Dead code in upstream, no effect on the binary.

### 4. x264 `rdo.c` dangling pointer, 17 lines (harmless)
All 17 hits are expansions of the same `TRELLIS_LOOP` macro in `quant_trellis_cabac()`. The
macro declares `uint64_t ssd0[2], ssd1[2]` inside the loop body; the second expansion
(`TRELLIS_LOOP(1)`, inside `if (0)`, line 884) is entered only through `goto next1` from the first
one. GCC 14 cannot follow that cross-scope `goto` and reports the arrays as "out of scope". Reading
the code: the `goto` target (`next1:;`) is the empty statement at the end of the loop body,
nothing reads `ssd0/ssd1` after it, and every iteration assigns both arrays (`for k` loop at the
top of the body) before they are read. So there is no use of an out-of-scope object. This is a
diagnostic of GCC 12+ (`-Wdangling-pointer`) on this unchanged x264 code, not a defect we
introduced. No patch: patching x264 would break the "unmodified sources" claim for no benefit.

### 5. x264 `ratecontrol.c:2082` array bounds (harmless)
`last_qscale_for` is `double[3]` indexed by `pict_type`, which is `SLICE_TYPE_P/B/I = 0/1/2`
(`common/base.h`), i.e. always inside the array; the range GCC prints (`[0, 2]`) is itself fully
inside `[0, 3)`, so this is a false positive from inlining `get_diff_limited_q()` into
`init_pass2()`. In addition, `init_pass2()` is only used for 2-pass rate control with a stats
file, which SubRec never enables (`encoder/ffvencoder.cpp:415` passes only single-pass real-time
`x264-params`).

### 6. FFmpeg `h2645_sei.c` unused label (harmless)
The three `goto unsupported_provider_code` statements (lines 195, 212, 228) sit inside
`#if CONFIG_HEVC_SEI` (lines 189-240), the label (line 242) is outside it. SubRec builds no HEVC
decoder, so `CONFIG_HEVC_SEI` is 0, the `goto`s vanish and the label is left unused. Pure
consequence of the minimal configuration; behavior is unaffected.

### 7. FFmpeg `vlc.c:443` array bounds, 3 lines (harmless)
In `add_level()` the 16-bit case writes `info.val16[curlevel]`, where `val16` has
`VLC_MULTI_MAX_SYMBOLS / 2 = 3` entries. The recursion is guarded by
`curlevel + 1 < max_symbols` with `max_symbols = VLC_MULTI_MAX_SYMBOLS >> is16bit = 3`, so
`curlevel` never exceeds 2; GCC only sees the guard-less unrolled copies produced by inlining the
recursion (the reported subscripts 3, 4, 5 are those unrolled levels). Furthermore the only
callers of `ff_vlc_init_multi_from_lengths()` are `utvideodec.c` and `magicyuv.c`
(`grep` in the pinned tree); neither decoder is enabled in SubRec's build, so the function is
never reached at run time.

### 8. FFmpeg `cmdutils.c:966` strncat (harmless)
`strncat(datadir, "/ffpresets", desired_size - 1 - datadir_len)` where
`desired_size = datadir_len + strlen("/ffpresets") + 1`, so the bound is exactly `strlen("/ffpresets")
= 10` and the buffer (just reallocated to `desired_size`) has room for the 10 characters plus the
terminating NUL. GCC warns whenever the bound equals the source length (it suspects a
truncation-unaware bound). Correct code, upstream, in `ffmpeg.exe`'s preset-file lookup only;
and `ffmpeg.exe` is not shipped in the SubRec package (see `README.md`, "ffmpeg.exe").

## Things this review does not prove
- #4, #5, #7 rest on reading the code and on GCC's known behavior, not on running a sanitizer
  build. For SubRec's use (single-pass x264 real-time encoding, no 2-pass, no utvideo/magicyuv)
  the affected paths are either not reached (#5, #7) or are the normal encoding path (#4); if
  you want more than code reading for #4, run the x264 encoder tests under a sanitizer build.
- The pins for libvpl and OpenH264 in `versions.env` are still not confirmed as commit ids rather
  than tag objects. The libvpl pin did work in the real build (the strict `HEAD == pin` check
  passed), so it is a commit; OpenH264 is only needed for the optional `lgpl-openh264` variant,
  which has not been built yet.
- Runtime issue found afterwards (not a build warning): `filter/ffafilter.cpp` set the
  `sample_fmts` option of `abuffersink` with a string. In n8.0.3 that option is a binary (hex)
  option, so the call failed with `EINVAL` (ignored) and the restriction never applied. Now fixed
  with `sample_formats` set before `avfilter_init_str` (see `README.md`, "Code notes"). A
  single-source recording does **not** check this (no graph is built); the synthetic ctest
  `tests/audio_filter_dual` does, and mixed recordings set the same option in `AudioTimelineMixer`.
- The classified build predates the test-only `aac` decoder (added with `audio_mix_aac`, commit
  `d375900`), as well as `wrapped_avframe`, the `mov` demuxer, `ddagrab` and its helper filters.
  Their warnings, if any, are **not** covered by the table above: the rebuilt log must be
  classified under "Policy for future builds". The #6 and #7 verdicts are unchanged by `aac`: it is
  neither an HEVC decoder nor a caller of `ff_vlc_init_multi_from_lengths()`.
- The test components (`aac` decoder, `wrapped_avframe`, `mov`/`nut`/`wav`/`matroska` demuxers, `lavfi`,
  ...) exist only with `SUBREC_FFMPEG_TEST_COMPONENTS=1`, the script default (local and PC2 test
  builds). The CI release build sets `0` and does not compile them, so their warnings, if any,
  appear only in test builds. No `=0` build has been run yet either: its log is not classified
  here and must be checked under "Policy for future builds" like any other new build (for
  example, a different set of enabled components can change which warnings appear).

## Policy for future builds
Any new line containing `warning` that is not in the table above should be classified here
before the build is accepted (`grep -E "warning|CMake Warning|Unknown option" build.log | sort | uniq -c`).
