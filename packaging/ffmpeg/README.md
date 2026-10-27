# SubRec minimal FFmpeg build

Status: the `gpl` variant was built successfully once (Debian 13, GCC 14 mingw-w64 posix; all
self-checks of `build-ffmpeg.sh` passed, 7 DLLs plus `ffmpeg.exe`, `collect-sources.sh` output
verified with `sha256sum -c`). The build is wired into `.github/workflows/windows-build.yml`
(job `ffmpeg`, Ubuntu 24.04), which has not run yet. The `lgpl` and `lgpl-openh264` variants
have not been run. Third-party build options that come from memory are marked *verify* in the
scripts. Warnings of the first build: [`BUILD_WARNINGS.md`](BUILD_WARNINGS.md).
**Changed since that build, not built yet:** the `ddagrab`, `hwdownload`, `hwupload`, `hwmap`
and `scale_d3d11` filters (screen backend), the test-only `mov` demuxer and the test-only `aac`
decoder; new warnings of the next run must be classified in `BUILD_WARNINGS.md`.

Goal: a **SubRec-specific minimal FFmpeg built from pinned, known sources** (instead of a
prebuilt FFmpeg with about 70 external libraries that could only be partly identified), so that
the release package ships the exact corresponding source.

| File | Purpose |
| --- | --- |
| `versions.env` | Pinned upstream repos, refs and commits (externally verified with `git ls-remote` on 2026-10-26) |
| `build-ffmpeg.sh` | Fetch + verify pins, build static deps, configure/build FFmpeg, check result |
| `collect-sources.sh` | Create `sources/` (git-archive tarballs, build scripts, `BUILD_CONFIG.txt`, `SHA256SUMS`) |
| `BUILD_WARNINGS.md` | Every warning of the first real build, with verdict and reason |

## 1. Components SubRec actually uses

Scanned from the SubRec sources (tests listed separately). Paths are repo-relative.

| Kind | Component | Evidence |
| --- | --- | --- |
| Video encoder | `h264_nvenc`, `h264_amf`, `h264_qsv`, `libx264` (tried in this order, selected by **name string**) | `encoder/ffvencoder.cpp:301-306`, lookup `:325`, per-encoder options `:396-416` |
| Video encoder input | `yuv420p` system-memory frames (dshow, ddagrab CPU mode); `AV_PIX_FMT_D3D11` frames with `hw_frames_ctx` (ddagrab GPU mode) for encoders whose `hw_configs` accept them (`h264_nvenc`, `h264_amf`), else download + swscale to `yuv420p` | `encoder/ffvencoder.cpp:283`, D3D11 check `:39`, passthrough `:363`, download `:484-494` |
| Audio encoder | native `aac` | `encoder/ffaencoder.cpp:240` |
| Video decoder | `h264` (by name) or by `codec_id` of the DirectShow stream: `rawvideo` (screen-capture-recorder, YUY2/NV12 webcams), `mjpeg` (MJPG webcams) | `decoder/ffvdecoder.cpp:283-290`, `:315` |
| Audio decoder | by `codec_id` of the DirectShow stream: PCM (`pcm_s16le`; `pcm_u8`/`pcm_s32le`/`pcm_f32le` enabled defensively) | `decoder/ffadecoder.cpp:147` |
| Audio decoder (test only) | native `aac`, looked up by name to decode the encoder's output in the mix round-trip test; the product never decodes AAC. Part of the test components (`SUBREC_FFMPEG_TEST_COMPONENTS=1`) | `tests/audio_timeline_mix/audio_mix_aac_tests.cpp:50` |
| Input device | `dshow` (all inputs: screen via the screen-capture-recorder filter, camera, microphone, virtual-audio-capturer) | `capture/dshow_capture_backend.cpp:57,64,145,158,194-195`; `demuxer/demuxer.cpp:375-377`; options `:388-407`; default URLs `recorder/ffrecorder_p.h:35-38` |
| Screen source (filter, no input device) | `ddagrab` (DXGI Desktop Duplication) + `hwdownload`, `format`, `scale` for the CPU path; selected by device id `ddagrab:N` / `SUBREC_CAPTURE_BACKEND` | `capture/ddagrab_screen_source.cpp:94,188-196`, hw frames `:140`, encoder probe `:251` |
| Filters kept for later (unused) | `hwupload`, `hwmap`, `scale_d3d11` (GPU-side conversion, e.g. NV12 for QSV) | `packaging/ffmpeg/build-ffmpeg.sh` comment at the filter list |
| Input device (not needed) | `gdigrab` only in `Demuxer::PrintDshowDevices()`, which has no callers | `demuxer/demuxer.cpp:474-506` |
| Device API | `avdevice_register_all`, `avdevice_list_input_sources` | `demuxer/demuxer.cpp:375`, `recorder/ffrecoder.cpp:226`, `capture/dshow_capture_backend.cpp:64` |
| Muxer | `mp4` (default format, always used) with `movflags=+faststart` | `muxer/ffmuxer.h:24`, `muxer/ffmuxer.cpp:183-185,271`, call site `event/ffopensourceevent.cpp:344` |
| Muxer (dead code) | `rtsp` branch, never reached (no caller passes a format) | `muxer/ffmuxer.cpp:187-189` |
| Protocol | `file` (`avio_open2`) | `muxer/ffmuxer.cpp:276` |
| Network | only `avformat_network_init()`; no network protocol used | `demuxer/demuxer.cpp:374`, `recorder/ffrecoder.cpp:227` |
| Audio filters | `abuffer`, `volume`, `amix`, `abuffersink` (+ `aresample`/`aformat` auto-inserted by `AVFILTER_AUTO_CONVERT_ALL`). Single-source recordings build no graph: `FFAFilterThread` only calls the pass-through `processSingleAudioFrame()` (`thread/ffafilterthread.cpp:194,233`). **Mixed (system + microphone) recordings** build `abuffer` ×2 → `amix` → `abuffersink` in `AudioTimelineMixer`. The older `FFAFilter` dual graph is exercised only by `tests/audio_filter_dual` | `filter/audio_timeline_mixer.cpp:19-33`; `filter/ffafilter.cpp:344,381,404,424`, auto-convert `:269,329` |
| Audio filter (unused) | `afftdn`: `createNoiseReductionFilter()` is never called | `filter/ffafilter.cpp:411-413` |
| Video filters | `buffer`, `buffersink` (+ `scale` auto-insert), but `FFVFilter::initFilter` returns before building the graph | `filter/ffvfilter.cpp:121,160,202` |
| swscale | `sws_getContext` (`SWS_FAST_BILINEAR`, `SWS_BILINEAR`, ITU-601 coefficients), `sws_scale` | `resampler/ffvresampler.cpp:76-78`, `capture/media/media_conversion.cpp:112-141` |
| swresample | `swr_alloc_set_opts2`, `swr_convert`, `swr_get_out_samples` (48 kHz FLTP stereo target) | `resampler/ffaresampler.cpp:82`, `capture/media/media_conversion.cpp:156-238`, `recorder/ffrecoder.cpp:191` |
| hwaccel / hwcontext | `d3d11va` hwcontext: `ddagrab` creates its D3D11 device and frames context; SubRec passes that `hw_frames_ctx` to NVENC/AMF and uses `av_hwframe_transfer_data` for the download fallback. Decoding stays software. `dxva2`, `cuda` hwcontexts are enabled for the encoders | `capture/ddagrab_screen_source.cpp:140`, `encoder/ffvencoder.cpp:363,484`, `decoder/ffvdecoder.cpp:286` |
| bsf / parser | none used directly. `h264`/`mjpeg` parsers enabled defensively for webcams; `mp4` muxer pulls its own deps | - |
| Libraries linked | avcodec, avformat, avutil, swscale, swresample, avfilter, avdevice; CMake copies every `bin/*.dll` | `CMakeLists.txt:55-61,131` |
| UI only | Cutting page file filter `*.mp4 *.avi *.mkv *.flv *.mov`: no FFmpeg code behind it yet | `qml/cuttingpage.qml:71` |

Tests (optional, `SUBREC_FFMPEG_TEST_COMPONENTS=1`, default on): `lavfi` indev with `sine` and
`color` (`tests/input_startup/input_startup_tests.cpp:48-67`), `wav`/`matroska` demuxers
(`:41,43`), `null`/`nut` muxers and `pcm_s16le`/`rawvideo` encoders
(`tests/muxer_lifecycle/muxer_lifecycle_tests.cpp:17,27,52,116`), and the `mov` demuxer, which
only `tools/pc2_verify.ps1` uses (reads a SubRec `.mp4` back with `ffmpeg.exe -c copy -f null`),
and the native `aac` **decoder** (`tests/audio_timeline_mix/audio_mix_aac_tests.cpp:50`, decodes the
`aac` encoder's packets to check the reconstructed peaks). The test components default to **on**
(`build-ffmpeg.sh:29`), the CI workflow does not override that, so the packages it builds contain
`wrapped_avframe` and `aac` decoders too. Only `SUBREC_FFMPEG_TEST_COMPONENTS=0` leaves them out;
`need_cfg` checks the decoders listed in the script's `decoders` array, so it only requires
`CONFIG_AAC_DECODER` when the test components are on.
Found on PC2 (first test run): `muxer_lifecycle` reads `copied.nut` back with
`avformat_open_input` (line 133), so it needs the `nut` **demuxer**; it reads nothing else back
(the `.mp4` cases only write). The `lavfi` indev labels its video stream with the `wrapped_avframe` codec id, so
every consumer that opens a decoder for it (reported on PC2 for the lavfi input) needs the
**decoder** `wrapped_avframe`; lavfi audio from `sine` is s16, already
covered by `pcm_s16le`. Both are now in the test components (as is `aac`, see above).

Install layout: FFmpeg's `make install` puts the MSVC-style import libraries `<comp>.lib` into
`bin/`. `build-ffmpeg.sh` moves them to `lib/` (where `cmake/SubRecFFmpeg.cmake`,
`capture/media/CMakeLists.txt` and `tests/muxer_lifecycle` look for `${FFMPEG_ROOT}/lib/<comp>.lib`)
and fails if a `.lib` or `lib<comp>.dll.a` is missing, is not an archive, or if any `.lib` is left in `bin/`.

Code notes found while scanning:

- `filter/ffafilter.cpp` (`createBufferSinkFilter`) used to call
  `av_opt_set(ctx, "sample_fmts", "8|8")` after the filter was initialized. In n8.0.3
  (`libavfilter/buffersink.c`, `FF_API_BUFFERSINK_OPTS` is still on for libavfilter 11) that option
  exists but is `AV_OPT_TYPE_BINARY`, which `av_opt_set` parses as a **hex string**
  (`libavutil/opt.c`, `set_string_binary`): `"8|8"` has an odd length and non-hex characters, so
  the call returned `AVERROR(EINVAL)`, which was ignored. The sink therefore accepted every sample
  format (no harm today, because `abuffer` already delivers `fltp`, but the restriction never
  applied). Fixed: the filter is now created with `avfilter_graph_alloc_filter`, the new array
  option `sample_formats` is set to `fltp` **before** `avfilter_init_str` (init appends the list
  terminator, so setting it afterwards would leave an unterminated list), and every return value is
  checked. **A single-source recording does not exercise this** (pass-through, no graph); a mixed
  recording builds its own graph in `AudioTimelineMixer`, which sets the same `sample_formats`
  option on its sink (`filter/audio_timeline_mixer.cpp:26-27`). The `FFAFilter` path is covered by the
  synthetic test `tests/audio_filter_dual` (S16 `abuffer` → restricted sink must deliver FLTP),
  which runs with the main project's `ctest`.
- The same test found and now covers three more bugs of the (unused) dual graph in
  `FFAFilter::initializeDualAudioFilter`: both `volume` filters were linked to `amix` input pad 0
  (`avfilter_link` returns `EINVAL` for an occupied pad), the `abuffer` → `volume` links were
  missing, and a failed init left a half-built graph behind. Runtime volume changes used
  `av_opt_set_double(ctx, "volume", ...)` on the `AVFilterContext`, which cannot reach the
  filter's private string option; they now use `avfilter_process_command(ctx, "volume", ...)`.
- A user-chosen output name with another extension is still written as MP4 (`muxer/ffmuxer.h:24`).

## 2. Configure line (variant `gpl`, test components on)

`build-ffmpeg.sh` generates this. `<OUT>`/`<DEPS>` are build directories.

```
configure --prefix=<OUT> --enable-cross-compile --cross-prefix=x86_64-w64-mingw32- --target-os=mingw32 --arch=x86_64
  --pkg-config=pkg-config --pkg-config-flags=--static --enable-shared --disable-static
  --disable-everything --disable-autodetect --disable-ffplay --disable-ffprobe --disable-doc --disable-network --disable-debug
  --enable-w32threads --enable-avdevice --enable-avfilter --enable-swscale --enable-swresample
  --enable-d3d11va --enable-dxva2 --enable-ffnvcodec --enable-nvenc --enable-cuda --enable-amf --enable-libvpl
  --enable-encoder=aac,h264_nvenc,h264_amf,h264_qsv,pcm_s16le,rawvideo,libx264
  --enable-decoder=rawvideo,mjpeg,h264,pcm_u8,pcm_s16le,pcm_s32le,pcm_f32le,wrapped_avframe,aac
  --enable-parser=h264,mjpeg --enable-muxer=mp4,null,nut --enable-demuxer=wav,matroska,nut,mov
  --enable-indev=dshow,lavfi --enable-protocol=file
  --enable-filter=volume,amix,aresample,aformat,scale,format,anull,null,ddagrab,hwdownload,hwupload,hwmap,scale_d3d11,sine,color
  --enable-gpl --enable-libx264
  --extra-cflags=-I<DEPS>/include --extra-ldflags="-L<DEPS>/lib -static" --extra-version=subrec-gpl
```

The line above is written by hand from the script and is **not yet the output of a real run**
since `ddagrab`/`hwdownload`/`hwupload`/`hwmap`/`scale_d3d11`, the `mov` demuxer and the `aac`
decoder were added;
the authoritative line of a build is `share/subrec-ffmpeg/configure-args.txt` /
`sources/BUILD_CONFIG.txt`.

**ddagrab self-check.** `ddagrab_filter_deps="d3d11va IDXGIOutput1 DXGI_OUTDUPL_FRAME_INFO"`
and `scale_d3d11_filter_deps="d3d11va"` (n8.0.3 `configure`); `hwupload`/`hwdownload`/`hwmap`
have no deps. The two DXGI names are `check_type` results of `windows.h dxgi1_2.h` that
configure does not write to `config.h`, so the script checks `CONFIG_DDAGRAB_FILTER` in
`config_components.h` and `CONFIG_D3D11VA` in `config.h`, and on failure prints the matching
`check_type` lines of `config.log` (too old mingw-w64 headers are the usual cause).
`HAVE_IDXGIOUTPUT5` is only logged (ddagrab uses it for 10-bit/HDR output, SubRec asks for the
default 8-bit BGRA). All five filters are LGPL-2.1-or-later code; the license of the build does
not change. Run-time imports stay inside the existing system-DLL list (`d3d11`, `dxgi`).

`--enable-version3` is **not** used: no selected dependency needs it, so the `gpl` build is
GPL-2.0-or-later (compatible with SubRec's GPL-3.0). `buffer`/`abuffer`/`buffersink`/
`abuffersink` are always compiled into libavfilter. The script fails if configure silently
drops any requested component, if the license flags differ from the variant, if the DLL set
differs from `EXPECTED_DLLS`, or if any DLL or `ffmpeg.exe` imports a non-system DLL (MinGW
runtime, x264, ...).

**ffmpeg.exe (acceptance/CI smoke-test tool, not used by SubRec itself, NOT packaged).** Only the `ffmpeg` program is built
(`--disable-ffplay --disable-ffprobe`). Its configure dependencies (`ffmpeg_deps` /
`ffmpeg_select` in n8.0.3's `configure`, read from the checkout) are avcodec, avfilter,
avformat and threads, which are already enabled; configure additionally force-enables the
filters `aformat anull atrim crop format hflip null rotate transpose trim vflip` even with
`--disable-everything`. The script verifies they are on. fftools uses libavdevice when present
(`-devices`, `-sources`) and links `ole32`/`psapi`/`shell32`, which are Windows system DLLs.
`ffmpeg.exe` is stripped, import-checked and hashed together with the DLLs
(`share/subrec-ffmpeg/binaries.sha256`). It is **not shipped**: SubRec links the FFmpeg
libraries and never starts `ffmpeg.exe` (searched the sources for `QProcess`, `CreateProcess`,
`ShellExecute`, `system(`, `popen`, `_spawn`, `exec*`: no hit that runs it), and
`cmake/SubRecFFmpeg.cmake` copies only `bin/*.dll`. Shipping it would add a second GPL
executable and its attack surface for no function (0.46 MB). The CI workflow runs it from the
build artifact only to check that the DLLs load and that the four video encoders and `aac` are
present, and checks that the package does not contain it. If SubRec ever starts it, copy it into
the package and list it in `THIRD_PARTY_LICENSES.md`; no new source is needed since the source
is already in `ffmpeg-<ref>.tar.xz`.

### External libraries and licenses

| Library | Version (pin) | Use | License | Linked |
| --- | --- | --- | --- | --- |
| FFmpeg | n8.0.3 `8ae0b349` | everything | LGPL-2.1-or-later; GPL-2.0-or-later with `--enable-gpl` | shared DLLs |
| x264 | stable `b35605ac` (X264_BUILD 165) | `libx264` (gpl variant) | GPL-2.0-or-later (commercial license sold by x264 LLC) | static, 8-bit 4:2:0 only |
| nv-codec-headers | n12.2.72.0 | NVENC API headers + dynamic loader | MIT | header-only; driver DLLs loaded at run time |
| AMD AMF headers | v1.5.3 `57ee0cfb` | `h264_amf` | MIT | header-only; `amfrt64.dll` comes with the AMD driver |
| Intel libvpl | v2.16.0 `778a66d6` | `h264_qsv` dispatcher | MIT | static; runtime comes with the Intel driver |
| OpenH264 | v2.7.0 `1bca6231` | `libopenh264` (optional variant) | BSD-2-Clause (patent note below) | static |
| MinGW-w64 runtime | from the toolchain | CRT startup, winpthreads, libgcc/libstdc++ | ZPL/public domain, MIT, GCC Runtime Library Exception | static |

## 3. FFmpeg version and header compatibility

- **n8.0.3 (chosen).** It has the same library majors as the DLLs SubRec was developed with
  (avutil-60, avcodec-62, avformat-62, avfilter-11, avdevice-62, swscale-9, swresample-6), so the
  DLL names and APIs stay the same. Point releases are bug-fix only.
- **n8.1.x (alternative).** FFmpeg normally keeps the majors in an x.1 release (7.1 kept 7.0's),
  so 8.1 likely has the same DLL names. This is **not verified**: check `libav*/version_major.h`.
  `EXPECTED_DLLS` makes the build fail on any rename.
- **nv-codec-headers n12.2.72.0 (chosen over n13.x).** FFmpeg 8.0's configure accepts ffnvcodec
  12.x and 13.x (from memory, *verify*). The header API version sets the **minimum NVIDIA
  driver**: about 551.xx for 12.2 and about 570.xx for 13.0 (from memory, *verify* against
  the nv-codec-headers README). SubRec uses no SDK 13 feature, so 12.2 reaches more users.
- **AMF v1.5.3.** FFmpeg 8.0 requires AMF headers ≥ some 1.4.x (configure version check). 1.5.3
  is newer. Compile compatibility is likely but **not verified**. If it fails, pin an older 1.4.x
  tag, which needs a new `git ls-remote`.
- **libvpl v2.16.0.** FFmpeg requires `vpl >= 2.6`. v2.17.0 is an equally verified alternative.

## 4. GPL vs pure-LGPL

| | `gpl` (FFmpeg + x264) | `lgpl` (HW encoders + `h264_mf`) | `lgpl-openh264` | LGPL, HW only |
| --- | --- | --- | --- | --- |
| Open-source SubRec (GPL-3.0) | OK, same behaviour as today | OK | OK | OK |
| Closed commercial SubRec | **No**, unless x264 is commercially licensed and legal review signs off | OK (dynamic linking, ship FFmpeg source, allow relinking/replacing DLLs; EULA must allow reverse engineering for debugging per LGPL-2.1 §6) | OK, but see patents | OK |
| Software fallback (no NVENC/AMF/QSV: VMs, RDP, old/disabled iGPU, broken drivers) | x264 works on any CPU | Windows MF H.264 encoder (built into Windows, except N/KN editions without the Media Feature Pack) | OpenH264 | **none**: recording fails |
| Code change needed | none | add `h264_mf` to `encoder/ffvencoder.cpp:301-306` + test | add `libopenh264` candidate + test | clear error message |
| Quality at the same bitrate | best, though SubRec runs x264 at `ultrafast`/`zerolatency` with most tools off (`encoder/ffvencoder.cpp:413-415`), so the lead is small | lower; fewer rate-control knobs | lower (mostly constrained baseline) | n/a |
| H.264/AAC patents | not covered by any license | encoder licensed through Windows | building OpenH264 yourself does **not** get Cisco's patent coverage (that covers only Cisco's own binary, downloaded separately) | not covered by any license |

Recommendation:

1. **Open-source release now:** variant `gpl`. It keeps today's behaviour, adds only one GPL
   library (x264), and its sources are exact.
2. **Future commercial build:** the same scripts with variant `lgpl`, after adding and testing
   `h264_mf` as the software fallback. Get legal review for the LGPL obligations, the
   H.264/AAC patent licensing and Qt's licensing (separate from this).

## 5. Build host

| | Linux cross-compile (recommended) | MSYS2 on Windows |
| --- | --- | --- |
| Packages | Ubuntu 24.04: `build-essential git ca-certificates pkg-config nasm cmake ninja-build xz-utils mingw-w64 gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 mingw-w64-x86-64-dev`, then `update-alternatives --set x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix` (same for `g++`) | MINGW64 env: `pacman -S --needed git make diffutils pkgconf mingw-w64-x86_64-toolchain mingw-w64-x86_64-nasm mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja` |
| Reproducibility | pinned container/runner image, apt versions recorded in `toolchain.txt` | rolling distro, hard to pin |
| Speed | fast configure (native fork) | FFmpeg configure is slow under MSYS2 (fork emulation) |
| CI | `ubuntu-24.04` runner (job `ffmpeg` in `windows-build.yml`); hands the artifact to the Windows job | `windows-latest` + `msys2/setup-msys2`, slower |
| Script changes | none | small: tool prefix (`x86_64-w64-mingw32-objdump`/`strip` may not exist; use `objdump`/`strip`) |

CRT: Ubuntu's mingw-w64 and MSYS2 MINGW64 target msvcrt. MSYS2 UCRT64 targets UCRT. The
FFmpeg API passes no CRT objects (FILE*, malloc/free pairs) across the DLL boundary, so a
CRT mismatch with Qt's MinGW is acceptable. The toolchain is recorded in `BUILD_CONFIG.txt`.

## 6. Measured sizes (first real build, `gpl` variant)

- DLLs (stripped): about 11 MB in total: avcodec 4.99 MB (includes x264), avutil 2.48 MB,
  swscale 1.81 MB, avformat 0.70 MB, plus avfilter, avdevice and swresample (smaller);
  `ffmpeg.exe` 0.46 MB (not packaged).
- `sources/`: 18 MB: `ffmpeg-n8.0.3.tar.xz` 10,873,616 B, `libvpl-v2.16.0.tar.xz` 6,090,924 B,
  `x264-b35605ace3dd.tar.xz` 747,636 B, `nv-codec-headers-n12.2.72.0.tar.xz` 65,888 B,
  `amf-headers-v1.5.3.tar.xz` 61,608 B, `subrec-ffmpeg-build-scripts.tar.xz` 14,492 B.
  (libvpl is large because the whole repository is archived, not only the dispatcher.)
- Build time was not recorded; the CI job has a 90 minute limit.

## 6a. Test run on PC2 (first real machine test, `gpl` variant)

- GPU NVIDIA GeForce RTX 5060 Ti, driver 32.0.16.1088.
- `ffmpeg.exe -encoders` lists `libx264`, `h264_amf`, `h264_nvenc`, `h264_qsv`, `aac`;
  `-devices` lists `dshow` and `lavfi` (the dshow list showed no device: no camera/microphone on PC2).
- 1080p30, 5 s, `color` + `sine` generators, exit code 0 for both: `libx264` 62,066 bytes
  (13.4x real time), `h264_nvenc` 98,792 bytes (5.49x real time).
- SubRec main project and the `capture_lifecycle`, `input_startup` and `media_conversion` ctests
  pass (`media_conversion` after the `.lib` files were copied to `lib/` by hand, see "Install layout").
- `muxer_lifecycle` failed (missing `nut` demuxer); fixed by this change, to be re-run.
- Not covered: the AMD (AMF) and Intel (QSV) encoders were not exercised, and no camera was tested.

## 7. Follow-ups

- First CI run of `windows-build.yml` (Ubuntu 24.04 has an older mingw-w64 GCC/NASM/CMake than the
  Debian 13 host of the first build; the used versions are recorded in `BUILD_CONFIG.txt`).
- Confirm that the libvpl and OpenH264 pins are commit ids (see `versions.env`), and run the
  `lgpl` / `lgpl-openh264` variants before relying on them.
- Run `tools/pc2_verify.ps1` on PC2 after the rebuild (main + standalone ctests, ffmpeg.exe
  ddagrab CPU/NVENC clips, SubRec autotest recordings with `SUBREC_CAPTURE_BACKEND=ddagrab` in
  `SUBREC_DDAGRAB_MODE=auto` and `cpu` plus the chosen microphone, read back with `-c copy`).
- Test on machines with NVIDIA (old and new drivers), AMD, Intel, and no GPU encoder. Test
  webcams with YUY2, NV12 and MJPG output. Run `ctest` of the main project (includes
  `audio_filter_dual`, which checks the corrected `abuffersink` setup; a recording does not).
