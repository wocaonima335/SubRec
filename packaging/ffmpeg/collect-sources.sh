#!/usr/bin/env bash
# Produce the corresponding source for a SubRec release package from a finished
# build-ffmpeg.sh run:
#
#   <dest>/sources/
#     ffmpeg-<ref>.tar.xz, x264-<commit12>.tar.xz (gpl), openh264-<ref>.tar.xz (lgpl-openh264),
#     nv-codec-headers-<ref>.tar.xz, amf-headers-<ref>.tar.xz, libvpl-<ref>.tar.xz,
#     subrec-ffmpeg-build-scripts.tar.xz   (versions.env + these scripts)
#     BUILD_CONFIG.txt                     (pins, configure line, toolchain, DLL hashes)
#     SHA256SUMS                           (sha256sum format, LF, sorted)
#
# Archives are made with `git archive` from the exact checkouts that were compiled
# (build-ffmpeg.sh already refused modified or wrongly pinned checkouts), compressed with
# `xz -9 -T1` so that the bytes are reproducible for a given git/xz version.
#
# Run once for the "gpl" variant (sha256sum -c of the result passed).
#
# Usage: SUBREC_FFMPEG_VARIANT=gpl packaging/ffmpeg/collect-sources.sh <dest-dir>
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=versions.env
source "$SCRIPT_DIR/versions.env"

DEST=${1:?usage: collect-sources.sh <dest-dir>}
VARIANT=${SUBREC_FFMPEG_VARIANT:-gpl}
WORK=$(cd "${SUBREC_FFMPEG_WORK:-$PWD/_ffbuild}" && pwd)
SRC=$WORK/src
OUT=$WORK/out/ffmpeg-subrec-$VARIANT-win64
INFO=$OUT/share/subrec-ffmpeg

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

for f in sources.tsv configure-args.txt toolchain.txt binaries.sha256; do
    [ -f "$INFO/$f" ] || die "missing $INFO/$f: run build-ffmpeg.sh (same SUBREC_FFMPEG_VARIANT) first"
done
grep -qx "variant=$VARIANT" "$INFO/toolchain.txt" || die "build in $OUT is not variant $VARIANT"

S=$DEST/sources
rm -rf "$S"
mkdir -p "$S"
export TZ=UTC

# archive NAME FILE_STEM [PATH...]: git archive of the pinned commit (optionally a subtree).
archive() {
    local name=$1 stem=$2; shift 2
    local dir=$SRC/$name commit
    commit=$(awk -F'\t' -v n="$name" '$1 == n { print $4 }' "$INFO/sources.tsv")
    [ -n "$commit" ] || die "$name is not in sources.tsv"
    [ "$(git -C "$dir" rev-parse HEAD)" = "$commit" ] || die "$name checkout moved since the build"
    git -C "$dir" archive --format=tar --prefix="$stem/" "$commit" -- "$@" | xz -9 -T1 > "$S/$stem.tar.xz"
}

while IFS=$'\t' read -r name _repo ref commit; do
    case $name in
        ffmpeg)           archive ffmpeg           "ffmpeg-$ref" ;;
        x264)             archive x264             "x264-${commit:0:12}" ;;
        openh264)         archive openh264         "openh264-$ref" ;;
        nv-codec-headers) archive nv-codec-headers "nv-codec-headers-$ref" ;;
        libvpl)           archive libvpl           "libvpl-$ref" ;;
        # Only the headers are used to build (the AMF runtime ships with the AMD driver).
        amf)              archive amf              "amf-headers-$ref" amf/public/include LICENSE.txt ;;
        *)                die "unknown source $name" ;;
    esac
done < "$INFO/sources.tsv"

tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
    -C "$SCRIPT_DIR/.." -cf - ffmpeg/versions.env ffmpeg/build-ffmpeg.sh ffmpeg/collect-sources.sh ffmpeg/README.md ffmpeg/BUILD_WARNINGS.md \
    | xz -9 -T1 > "$S/subrec-ffmpeg-build-scripts.tar.xz"

case $VARIANT in
    gpl)  license="GPL-2.0-or-later (FFmpeg configured with --enable-gpl for libx264; no --enable-version3, no --enable-nonfree)" ;;
    *)    license="LGPL-2.1-or-later (no --enable-gpl, no --enable-version3, no --enable-nonfree)" ;;
esac

{
    cat <<EOF
SubRec release package - FFmpeg build configuration
====================================================

This FFmpeg was built by SubRec's own scripts from the pinned sources in this directory.
Rebuild: unpack subrec-ffmpeg-build-scripts.tar.xz and run
  SUBREC_FFMPEG_VARIANT=$VARIANT ffmpeg/build-ffmpeg.sh
on the build host listed below (scripts fetch the same commits; the archives here are
byte-for-byte \`git archive\` exports of those commits, unmodified, no patches applied).

Variant : $VARIANT
License : $license

Sources (name, upstream repository, ref, commit)
------------------------------------------------
EOF
    column -t -s $'\t' "$INFO/sources.tsv" 2>/dev/null || cat "$INFO/sources.tsv"
    cat <<EOF

Statically linked into the DLLs: x264 (gpl) or OpenH264 (lgpl-openh264), the libvpl
dispatcher, and the MinGW-w64 runtime (libgcc / libstdc++ under the GCC Runtime Library
Exception, winpthreads, mingw-w64 CRT startup code). nv-codec-headers and the AMF headers are
header-only. NVENC / AMF / Intel VPL runtimes are loaded from the GPU driver at run time and
are not shipped.

./configure arguments (one per line; build paths replaced by <WORK>)
-------------------------------------------------------------------
EOF
    cat "$INFO/configure-args.txt"
    cat <<EOF

Build host and toolchain
------------------------
EOF
    cat "$INFO/toolchain.txt"
    cat <<EOF

DLLs and ffmpeg.exe (SHA-256, after strip --strip-unneeded)
-----------------------------------------------------------
EOF
    cat "$INFO/binaries.sha256"
} > "$S/BUILD_CONFIG.txt"

( cd "$S" && find . -maxdepth 1 -type f ! -name SHA256SUMS -printf '%f\n' | LC_ALL=C sort \
    | xargs sha256sum > SHA256SUMS )

if grep -q TODO "$S/BUILD_CONFIG.txt"; then die "BUILD_CONFIG.txt contains TODO"; fi
ls -l "$S" >&2
du -sh "$S" >&2
