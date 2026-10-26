# SubRec minimal FFmpeg build (DRAFT)

Status: **plan + draft scripts, never run.** They were written without network access and
without a MinGW toolchain. Nothing here is wired into `.github/workflows/`. Third-party build
options that come from memory are marked *verify* in the scripts.

Goal: replace the prebuilt, maximal FFmpeg (BtbN-like, ~70 external libraries, only partly
identifiable) with a **SubRec-specific minimal FFmpeg built from pinned, known sources**, so
that the release package can ship the exact corresponding source.

| File | Purpose |
| --- | --- |
| `versions.env` | Pinned upstream repos, refs and commits (externally verified with `git ls-remote` on 2026-10-26) |
| `build-ffmpeg.sh` | Fetch + verify pins, build static deps, configure/build FFmpeg, check result |
| `collect-sources.sh` | Create `sources/` (git-archive tarballs, build scripts, `BUILD_CONFIG.txt`, `SHA256SUMS`) |
| `ci-build-ffmpeg.yml.draft` | GitHub Actions job draft (ubuntu runner). Not a workflow until moved to `.github/workflows/` |

## 1. Components SubRec actually uses

Scanned from the SubRec sources (tests listed separately). Paths are repo-relative.

| Kind | Component | Evidence |
| --- | --- | --- |
| Video encoder | `h264_nvenc`, `h264_amf`, `h264_qsv`, `libx264` (tried in this order, selected by **name string**) | `encoder/ffvencoder.cpp:257-262`, lookup `:267`, per-encoder options `:321-339` |
| Video encoder input | `yuv420p` system-memory frames (no hw frames) | `encoder/ffvencoder.cpp:239` |
| Audio encoder | native `aac` | `encoder/ffaencoder.cpp:240` |
| Video decoder | `h264` (by name) or by `codec_id` of the DirectShow stream: `rawvideo` (screen-capture-recorder, YUY2/NV12 webcams), `mjpeg` (MJPG webcams) | `decoder/ffvdecoder.cpp:283-290`, `:315` |
| Audio decoder | by `codec_id` of the DirectShow stream: PCM (`pcm_s16le`; `pcm_u8`/`pcm_s32le`/`pcm_f32le` enabled defensively) | `decoder/ffadecoder.cpp:147` |
| Input device | `dshow` (all inputs: screen via the screen-capture-recorder filter, camera, microphone, virtual-audio-capturer) | `capture/dshow_capture_backend.cpp:57,64,145,158,194-195`; `demuxer/demuxer.cpp:375-377`; options `:388-407`; default URLs `recorder/ffrecorder_p.h:35-38` |
| Input device (not needed) | `gdigrab` only in `Demuxer::PrintDshowDevices()`, which has no callers | `demuxer/demuxer.cpp:474-506` |
| Device API | `avdevice_register_all`, `avdevice_list_input_sources` | `demuxer/demuxer.cpp:375`, `recorder/ffrecoder.cpp:226`, `capture/dshow_capture_backend.cpp:64` |
| Muxer | `mp4` (default format, always used) with `movflags=+faststart` | `muxer/ffmuxer.h:24`, `muxer/ffmuxer.cpp:183-185,271`, call site `event/ffopensourceevent.cpp:316` |
| Muxer (dead code) | `rtsp` branch, never reached (no caller passes a format) | `muxer/ffmuxer.cpp:187-189` |
| Protocol | `file` (`avio_open2`) | `muxer/ffmuxer.cpp:276` |
| Network | only `avformat_network_init()`; no network protocol used | `demuxer/demuxer.cpp:374`, `recorder/ffrecoder.cpp:227` |
| Audio filters | `abuffer`, `volume`, `amix`, `abuffersink` (+ `aresample`/`aformat` auto-inserted by `AVFILTER_AUTO_CONVERT_ALL`) | `filter/ffafilter.cpp:293,330,353,370`, auto-convert `:235,279` |
| Audio filter (unused) | `afftdn`: `createNoiseReductionFilter()` is never called | `filter/ffafilter.cpp:357-359` |
| Video filters | `buffer`, `buffersink` (+ `scale` auto-insert), but `FFVFilter::initFilter` returns before building the graph | `filter/ffvfilter.cpp:121,160,202` |
| swscale | `sws_getContext` (`SWS_FAST_BILINEAR`, `SWS_BILINEAR`, ITU-601 coefficients), `sws_scale` | `resampler/ffvresampler.cpp:76-78`, `capture/media/media_conversion.cpp:112-141` |
| swresample | `swr_alloc_set_opts2`, `swr_convert`, `swr_get_out_samples` (48 kHz FLTP stereo target) | `resampler/ffaresampler.cpp:82`, `capture/media/media_conversion.cpp:156-238`, `recorder/ffrecoder.cpp:191` |
| hwaccel / hwcontext | **none used directly** (no `av_hwdevice_ctx_create`, software decode only). NVENC/AMF/QSV create their device internally; `d3d11va`, `dxva2`, `cuda` hwcontexts are enabled only to support them | `decoder/ffvdecoder.cpp:286` |
| bsf / parser | none used directly. `h264`/`mjpeg` parsers enabled defensively for webcams; `mp4` muxer pulls its own deps | - |
| Libraries linked | avcodec, avformat, avutil, swscale, swresample, avfilter, avdevice; CMake copies every `bin/*.dll` | `CMakeLists.txt:55-61,131` |
| UI only | Cutting page file filter `*.mp4 *.avi *.mkv *.flv *.mov`: no FFmpeg code behind it yet | `qml/cuttingpage.qml:71` |

Tests (optional, `SUBREC_FFMPEG_TEST_COMPONENTS=1`, default on): `lavfi` indev with `sine` and
`color` (`tests/input_startup/input_startup_tests.cpp:48-67`), `wav`/`matroska` demuxers
(`:41,43`), `null`/`nut` muxers and `pcm_s16le`/`rawvideo` encoders
(`tests/muxer_lifecycle/muxer_lifecycle_tests.cpp:17,27,52,116`).

Code notes found while scanning (no change made in this step):

- `filter/ffafilter.cpp:377` sets the `abuffersink` option `sample_fmts`, which FFmpeg 7.1
  deprecated in favour of `sample_formats`. Whether FFmpeg 8 still accepts it is **not
  verified**, and the return value is ignored. Check the log on the first run.
- A user-chosen output name with another extension is still written as MP4 (`muxer/ffmuxer.h:24`).

## 2. Configure line (variant `gpl`, test components on)

`build-ffmpeg.sh` generates this. `<OUT>`/`<DEPS>` are build directories.

```
configure --prefix=<OUT> --enable-cross-compile --cross-prefix=x86_64-w64-mingw32- --target-os=mingw32 --arch=x86_64
  --pkg-config=pkg-config --pkg-config-flags=--static --enable-shared --disable-static
  --disable-everything --disable-autodetect --disable-programs --disable-doc --disable-network --disable-debug
  --enable-w32threads --enable-avdevice --enable-avfilter --enable-swscale --enable-swresample
  --enable-d3d11va --enable-dxva2 --enable-ffnvcodec --enable-nvenc --enable-cuda --enable-amf --enable-libvpl
  --enable-encoder=aac,h264_nvenc,h264_amf,h264_qsv,pcm_s16le,rawvideo,libx264
  --enable-decoder=rawvideo,mjpeg,h264,pcm_u8,pcm_s16le,pcm_s32le,pcm_f32le
  --enable-parser=h264,mjpeg --enable-muxer=mp4,null,nut --enable-demuxer=wav,matroska
  --enable-indev=dshow,lavfi --enable-protocol=file
  --enable-filter=volume,amix,aresample,aformat,scale,format,anull,null,sine,color
  --enable-gpl --enable-libx264
  --extra-cflags=-I<DEPS>/include --extra-ldflags="-L<DEPS>/lib -static" --extra-version=subrec-gpl
```

`--enable-version3` is **not** used: no selected dependency needs it, so the `gpl` build is
GPL-2.0-or-later (compatible with SubRec's GPL-3.0). `buffer`/`abuffer`/`buffersink`/
`abuffersink` are always compiled into libavfilter. The script fails if configure silently
drops any requested component, if the license flags differ from the variant, if the DLL set
differs from `EXPECTED_DLLS`, or if any DLL imports a non-system DLL (MinGW runtime, x264, ...).

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
| x265 | not built | SubRec has no HEVC encoder candidate | - | - |

## 3. FFmpeg version and header compatibility

- **n8.0.3 (chosen).** It has the same library majors as the DLLs SubRec was developed with
  (avutil-60, avcodec-62, avformat-62, avfilter-11, avdevice-62, swscale-9, swresample-6), so the
  DLL names and APIs stay the same. The dev snapshot `37064b2d16` is already after the 8.0 major
  bump, so removed APIs are already handled in the code. Point releases are bug-fix only.
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
| Code change needed | none | add `h264_mf` to `encoder/ffvencoder.cpp:257-262` + test | add `libopenh264` candidate + test | clear error message |
| Quality at the same bitrate | best, though SubRec runs x264 at `ultrafast`/`zerolatency` with most tools off (`encoder/ffvencoder.cpp:337-339`), so the lead is small | lower; fewer rate-control knobs | lower (mostly constrained baseline) | n/a |
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
| CI | `ubuntu-24.04` runner (`ci-build-ffmpeg.yml.draft`); hand the zip to the Windows job | `windows-latest` + `msys2/setup-msys2`, slower |
| Script changes | none | small: tool prefix (`x86_64-w64-mingw32-objdump`/`strip` may not exist; use `objdump`/`strip`) |

CRT: Ubuntu's mingw-w64 and MSYS2 MINGW64 target msvcrt. MSYS2 UCRT64 targets UCRT. The
FFmpeg API passes no CRT objects (FILE*, malloc/free pairs) across the DLL boundary, so a
CRT mismatch with Qt's MinGW is acceptable. The toolchain is recorded in `BUILD_CONFIG.txt`.

## 6. Estimates (not measured)

- Build time: ~8–15 min on a 4-vCPU Linux runner (deps ~3 min, FFmpeg minimal ~3–6 min, xz of
  sources ~1–2 min). On a Windows PC with MSYS2: ~15–30 min. On a Windows CI runner with MSYS2: ~20–40 min.
- DLLs (stripped, gpl): ~8–13 MB total (avcodec ~4–7 MB including x264; avutil ~1–1.5;
  swscale ~0.7–1.5; avformat ~0.6–1.2; avfilter ~0.6–1.2; swresample/avdevice < 0.5). That is
  ~3.5–6 MB inside the zip. The current maximal build is roughly 100–170 MB of DLLs (from memory).
- `sources/` (gpl): FFmpeg ~11–12 MB tar.xz, x264 ~0.8, libvpl ~1–2, AMF headers ~0.3–0.6,
  nv-codec-headers ~0.1, scripts < 0.1. Total ~13–15 MB. For comparison, today's `sources/` is
  18.1 MiB (measured), and it is incomplete.

## 7. Follow-ups (not done in this step)

- First real run: fix *verify* items, record actual sizes and times.
- Point `windows-build.yml` at the artifact of this build instead of `FFMPEG_URL`, and replace
  its "Collect corresponding source" step and `packaging/BUILD_CONFIG.txt` with the output of
  `collect-sources.sh`. Then update `THIRD_PARTY_LICENSES.md`, `reference/ffmpeg-fftools/NOTICE.md`
  and `README.md` (x265 and the other libraries disappear; the license becomes GPL-2.0-or-later
  FFmpeg + x264).
- Test on machines with NVIDIA (old and new drivers), AMD, Intel, and no GPU encoder. Test
  webcams with YUY2, NV12 and MJPG output.
