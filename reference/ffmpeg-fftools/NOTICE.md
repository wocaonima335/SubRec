# NOTICE: `ffmpeg_mux.c` is third-party code (FFmpeg)

| | |
| --- | --- |
| File | `reference/ffmpeg-fftools/ffmpeg_mux.c` |
| Origin | FFmpeg, `fftools/ffmpeg_mux.c` |
| FFmpeg version / commit of this copy | **Byte-identical to `fftools/ffmpeg_mux.c` of FFmpeg n8.0.3** (commit `8ae0b34901ba60a802f183ee75a250a9fc3e09a5`), checked with `diff` against a checkout of that tag: no difference. The file was **not** compared with other FFmpeg versions (for example the `37064b2d16` development snapshot SubRec was first built against), so the version it was originally copied from is unknown; the same content may exist in neighbouring versions. The identity with n8.0.3 only says what the file equals, not where it came from. |
| Copyright | The FFmpeg project and its contributors (see the unmodified header at the top of the file) |
| License | **GNU Lesser General Public License, version 2.1 or (at your option) any later version** (LGPL-2.1-or-later). License text: [`LICENSES/LGPL-2.1.txt`](../../LICENSES/LGPL-2.1.txt) |
| Modified? | The file is kept byte-for-byte as received; its copyright and license header is unchanged. |

## What this file is for

It is kept **for reference only**, as a design comparison for SubRec's own muxer
(`muxer/`). It is **not** SubRec code, is **not** compiled by any CMake target and is **not**
linked into any SubRec binary. It cannot be compiled on its own: it includes FFmpeg-internal
headers (`ffmpeg.h`, `ffmpeg_mux.h`, `ffmpeg_utils.h`, `sync_queue.h`) that are not shipped
in FFmpeg's installed headers. It is unrelated to the FFmpeg libraries SubRec ships (below),
apart from happening to be identical to that version's file.

## Licensing consequences

- This file stays under LGPL-2.1-or-later. The rest of SubRec is GPL-3.0 (see [`LICENSE`](../../LICENSE)).
  LGPL-2.1-or-later permits redistribution under the terms of the GPL, so keeping the file in
  this GPL-3.0 repository is consistent; the original header must be preserved.
- The SubRec Contributor License Agreement ([`CLA.md`](../../CLA.md)) does **not** cover this
  file and the maintainers cannot relicense it. Do not send modifications to it as contributions.
- Do not ship this file in commercial or proprietary distributions of SubRec.
- Listed in [`THIRD_PARTY_LICENSES.md`](../../THIRD_PARTY_LICENSES.md).

## FFmpeg binaries shipped with SubRec (separate from the file above)

SubRec links against shared FFmpeg libraries that are **not** stored in this repository. The
Windows packages contain a minimal FFmpeg that SubRec's own scripts build from pinned, unmodified
upstream sources ([`packaging/ffmpeg/`](../../packaging/ffmpeg/)); the CI builds it in the
`ffmpeg` job of `.github/workflows/windows-build.yml`.

| | |
| --- | --- |
| FFmpeg | release **n8.0.3**, commit `8ae0b34901ba60a802f183ee75a250a9fc3e09a5`, no patches |
| DLLs | `avutil-60`, `avcodec-62`, `avformat-62`, `avfilter-11`, `avdevice-62`, `swscale-9`, `swresample-6` (about 11 MB together) |
| Configuration | `--enable-gpl --enable-libx264` and a short list of selected components (`--disable-everything`); **no** `--enable-version3`, **no** `--enable-nonfree`; cross-compiled with MinGW-w64, shared FFmpeg libraries, everything else static. The full `./configure` line is in `sources/BUILD_CONFIG.txt` of every package |
| License of the resulting binaries | **GPL version 2 or later** (compatible with SubRec's GPL-3.0) |
| `ffmpeg.exe` | Built for acceptance checks only; **not** shipped, because SubRec never starts it |

### Components and their sources

| Component | Version / commit | License | How it is in the binaries | Source in the package |
| --- | --- | --- | --- | --- |
| FFmpeg | n8.0.3, `8ae0b349` ([git.ffmpeg.org](https://git.ffmpeg.org/ffmpeg.git)) | LGPL-2.1+; GPL-2.0+ as built here | shared DLLs | `sources/ffmpeg-n8.0.3.tar.xz` (10.9 MB) |
| x264 | `stable` branch commit `b35605ace3ddf7c1a5d67a2eb553f034aef41d55` (`X264_BUILD 165`; [code.videolan.org](https://code.videolan.org/videolan/x264)) | GPL-2.0+ | static in `avcodec-62.dll`, 8-bit 4:2:0 only | `sources/x264-b35605ace3dd.tar.xz` (0.75 MB) |
| libvpl (Intel oneVPL dispatcher) | v2.16.0, `778a66d6` ([github.com/intel/libvpl](https://github.com/intel/libvpl)) | MIT | static dispatcher in `avcodec-62.dll`; the runtime comes with the Intel graphics driver | `sources/libvpl-v2.16.0.tar.xz` (6.1 MB, whole repository) |
| nv-codec-headers | n12.2.72.0, `c6927834` | MIT | header-only (NVENC is loaded from the NVIDIA driver at run time) | `sources/nv-codec-headers-n12.2.72.0.tar.xz` (66 KB) |
| AMD AMF headers | v1.5.3, `57ee0cfb` | MIT | header-only (the AMF runtime comes with the AMD driver) | `sources/amf-headers-v1.5.3.tar.xz` (62 KB) |
| MinGW-w64 runtime | from the build toolchain (recorded in `BUILD_CONFIG.txt`) | ZPL / public domain / MIT; libgcc and libstdc++ under the GCC Runtime Library Exception | static | toolchain, not bundled |

x265 and the previous prebuilt FFmpeg (a development snapshot with about 70 external libraries)
are **not** part of SubRec's packages any more.

**Inside every SubRec release package** (zip, and any future installer) the source is
shipped under `sources/`, produced by `packaging/ffmpeg/collect-sources.sh` from exactly the
checkouts that were compiled (`git archive`, `xz -9`):

| Path in the package | Content |
| --- | --- |
| `sources/ffmpeg-n8.0.3.tar.xz` | FFmpeg source at commit `8ae0b349` |
| `sources/x264-b35605ace3dd.tar.xz` | x264 source at commit `b35605ace3ddf7c1a5d67a2eb553f034aef41d55` |
| `sources/libvpl-v2.16.0.tar.xz`, `sources/nv-codec-headers-n12.2.72.0.tar.xz`, `sources/amf-headers-v1.5.3.tar.xz` | sources of the MIT-licensed components (AMF: headers and license only) |
| `sources/subrec-ffmpeg-build-scripts.tar.xz` | `versions.env`, `build-ffmpeg.sh`, `collect-sources.sh`, the build README and the build warning notes |
| `sources/BUILD_CONFIG.txt` | pinned commits, the `./configure` arguments, the build host and toolchain versions, SHA-256 of every DLL |
| `sources/SHA256SUMS` | SHA-256 of every other file in `sources/` (check with `sha256sum -c SHA256SUMS`) |

Total about 18 MB. The release workflow fails for version tags (`v*`) if `sources/` is
incomplete or if a packaged DLL differs from the hash recorded in `BUILD_CONFIG.txt`; other
builds get `sources/INCOMPLETE-DO-NOT-DISTRIBUTE.txt`.

### Limits of this statement

- The build scripts and the first build were produced on Debian 13 with GCC 14 (mingw-w64 posix).
  CI uses Ubuntu 24.04, whose toolchain is older; the versions used are written into every
  `BUILD_CONFIG.txt`. A rebuild gives functionally the same libraries but not necessarily
  byte-identical DLLs.
- The MinGW-w64 runtime parts that end up in the DLLs come from the build toolchain, which is not
  bundled; its licenses permit this for the runtime parts used (see `THIRD_PARTY_LICENSES.md`).
- Build warnings of the first build are classified in
  [`packaging/ffmpeg/BUILD_WARNINGS.md`](../../packaging/ffmpeg/BUILD_WARNINGS.md).

### GPL obligations when you distribute these binaries

Because this FFmpeg build is GPL-2.0-or-later (it contains x264), anyone who distributes it (for
example a SubRec release archive that contains the FFmpeg DLLs) must:

1. provide, or give a written offer to provide, the **corresponding source code**: the
   FFmpeg source at the commit above, the source of x264 and the other bundled components at
   the versions actually built, and the build scripts and configuration needed to reproduce
   the DLLs (all in `sources/`);
2. include the license texts (`licenses/ffmpeg/` in the package, plus the GPL-3.0 text of SubRec itself) and
   keep the copyright notices;
3. not add further restrictions on the recipients.

Distributing only the SubRec source code (users supply their own FFmpeg) does not
distribute FFmpeg. See [`THIRD_PARTY_LICENSES.md`](../../THIRD_PARTY_LICENSES.md) for details.
