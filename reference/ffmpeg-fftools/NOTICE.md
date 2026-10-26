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
| Configuration | `--enable-gpl --enable-version3` (no `--enable-nonfree`); includes **x264** and **x265** |
| License of the resulting binaries | **GPL version 3 or later** |

### Where to get the corresponding source

- FFmpeg commit `37064b2d16`:
  - <https://git.ffmpeg.org/ffmpeg.git>
  - <https://github.com/FFmpeg/FFmpeg/commit/37064b2d16> (GitHub mirror)
- x264 and x265 (and every other external library enabled in that build): TODO, record the
  exact versions or commits and the full `./configure` command line used for the build. The
  exact x264/x265 versions are **currently unknown**.

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
