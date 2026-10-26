# NOTICE: `ffmpeg_mux.c` is third-party code (FFmpeg)

| | |
| --- | --- |
| File | `reference/ffmpeg-fftools/ffmpeg_mux.c` |
| Origin | FFmpeg, `fftools/ffmpeg_mux.c` |
| FFmpeg version / commit | `<TO BE FILLED BY THE MAINTAINER: FFmpeg version or commit this copy was taken from>` |
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
