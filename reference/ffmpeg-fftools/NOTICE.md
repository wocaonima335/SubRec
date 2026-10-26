# NOTICE: `ffmpeg_mux.c` is third-party code (FFmpeg)

| | |
| --- | --- |
| File | `reference/ffmpeg-fftools/ffmpeg_mux.c` |
| Origin | FFmpeg, `fftools/ffmpeg_mux.c` |
| FFmpeg version / commit of this copy | **Unknown / not verified.** The exact FFmpeg release or commit this file was copied from was not recorded and could not be checked (no network access when this notice was written). Its structure (`MuxThreadContext`, `sync_queue`, `ffmpeg_utils.h`) is that of a recent FFmpeg `fftools`, but it is **not claimed** to match commit `37064b2d16` (see the next section). TODO: maintainer to compare, e.g. `git diff 37064b2d16 -- fftools/ffmpeg_mux.c` in an FFmpeg checkout, and record the result here. |
| Copyright | The FFmpeg project and its contributors (see the unmodified header at the top of the file) |
| License | **GNU Lesser General Public License, version 2.1 or (at your option) any later version** (LGPL-2.1-or-later). License text: [`LICENSES/LGPL-2.1.txt`](../../LICENSES/LGPL-2.1.txt) |
| Modified? | The file is kept byte-for-byte as received; its copyright and license header is unchanged. |

## What this file is for

It is kept **for reference only**, as a design comparison for SubRec's own muxer
(`muxer/`). It is **not** SubRec code, is **not** compiled by any CMake target and is **not**
linked into any SubRec binary. It cannot be compiled on its own: it includes FFmpeg-internal
headers (`ffmpeg.h`, `ffmpeg_mux.h`, `ffmpeg_utils.h`, `sync_queue.h`) that are not shipped
in FFmpeg's installed headers.

## Licensing consequences

- This file stays under LGPL-2.1-or-later. The rest of SubRec is GPL-3.0 (see [`LICENSE`](../../LICENSE)).
  LGPL-2.1-or-later permits redistribution under the terms of the GPL, so keeping the file in
  this GPL-3.0 repository is consistent; the original header must be preserved.
- The SubRec Contributor License Agreement ([`CLA.md`](../../CLA.md)) does **not** cover this
  file and the maintainers cannot relicense it. Do not send modifications to it as contributions.
- Do not ship this file in commercial or proprietary distributions of SubRec.
- Listed in [`THIRD_PARTY_LICENSES.md`](../../THIRD_PARTY_LICENSES.md).

## FFmpeg binaries used to build and run SubRec (separate from the file above)

SubRec links against shared FFmpeg libraries that are **not** stored in this repository. The
build used during development, and the one the project's Windows packages are expected to
contain, is:

| | |
| --- | --- |
| Version string | `N-119453-g37064b2d16-20250509` |
| Kind | A git `master` snapshot of 2025-05-09. **Not an official FFmpeg release.** |
| Commit | `37064b2d16` |
| Libraries | libavutil 60.2.100, libavcodec 62.3 |
| DLLs | `avutil-60`, `avcodec-62`, `avformat-62`, `avfilter-11`, `avdevice-62`, `swscale-9`, `swresample-6` |
| Configuration | `--enable-gpl --enable-version3` (no `--enable-nonfree`); cross-compiled shared build; includes **x264** (core 165) and **x265** (4.1+151-f8be4634) |
| License of the resulting binaries | **GPL version 3 or later** |

### Where to get the corresponding source

| Component | Identification | Source |
| --- | --- | --- |
| FFmpeg | commit `37064b2d16` | <https://git.ffmpeg.org/ffmpeg.git>, <https://github.com/FFmpeg/FFmpeg/commit/37064b2d16> |
| x265 | `4.1+151-f8be4634` (API build 215, from the version string embedded in the DLL), commit `f8be4634` | <https://bitbucket.org/multicoreware/x265_git> (archive URL used by CI is **not verified**) |
| x264 | statically linked; the only version information is `core 165` (SEI text `x264 - core 165 - H.264/MPEG-4 AVC codec - Copyleft 2003-2025`), with no `r` revision number and no commit | <https://code.videolan.org/videolan/x264>, `stable` branch, the commit whose `x264.h` defines `X264_BUILD 165`. **The exact revision is unknown (TODO).** |
| Other enabled external libraries | listed in the configure line (`packaging/BUILD_CONFIG.txt`) | **TODO: versions and sources not yet collected** |

The full `./configure` command line is recorded in [`packaging/BUILD_CONFIG.txt`](../../packaging/BUILD_CONFIG.txt).
It was taken from the configuration string embedded in `avcodec-62.dll`. The build looks
similar to a BtbN `FFmpeg-Builds` win64 GPL shared build; this is an inference only and is
**not confirmed**.

**Inside every SubRec release package** (zip, and any future installer) the source is
shipped under `sources/`:

| Path in the package | Content |
| --- | --- |
| `sources/ffmpeg-37064b2d16.tar.gz` | FFmpeg source at commit `37064b2d16` (GitHub archive of that commit) |
| `sources/x265-f8be4634.tar.gz` | x265 source at commit `f8be4634` |
| `sources/x264-<COMMIT>.tar.gz` | x264 source, `stable` branch commit with `X264_BUILD 165` (commit TODO) |
| `sources/BUILD_CONFIG.txt` | FFmpeg configure command line, provenance and build notes (template: `packaging/BUILD_CONFIG.txt`) |
| `sources/SHA256SUMS` | SHA-256 of every other file in `sources/` (check with `sha256sum -c SHA256SUMS`) |

### Warnings (known gaps in source completeness)

1. **x264 may not be the exact source that was compiled in.** The DLLs only say `core 165`.
   The x264 archive in `sources/` is a best-effort match (a commit of the `stable` branch with
   `X264_BUILD 165`). If it is not byte-for-byte the code that was built, shipping it does
   not fully satisfy the GPL "corresponding source" requirement. Ideally FFmpeg should be
   **rebuilt from known sources** (known FFmpeg, x264 and x265 commits and a recorded build
   script) and the DLLs replaced.
2. **Other statically linked libraries.** The configure line enables many more external
   libraries, which are probably linked statically into the DLLs. Their sources are not in
   `sources/` yet (TODO). A rebuild with a minimal configuration would remove most of this problem.
3. The "similar to BtbN FFmpeg-Builds" remark is an inference, not a fact.
4. Until these items are resolved, the release workflow fails for version tags (`v*`) and
   marks other builds `INCOMPLETE-DO-NOT-DISTRIBUTE.txt`.

### GPL obligations when you distribute these binaries

Because this FFmpeg build is GPL-3.0-or-later, anyone who distributes it (for example a
SubRec release archive that contains the FFmpeg DLLs) must:

1. provide, or give a written offer to provide, the **corresponding source code**: the
   FFmpeg source at the commit above, the source of the enabled GPL components (including
   x264 and x265, at the versions actually built), and the build scripts and configuration
   needed to reproduce the DLLs;
2. include the GPL-3.0 license text and keep the copyright notices;
3. not add further restrictions on the recipients.

Distributing only the SubRec source code (users supply their own FFmpeg) does not
distribute FFmpeg. See [`THIRD_PARTY_LICENSES.md`](../../THIRD_PARTY_LICENSES.md) for details.
