#!/usr/bin/env bash
# Build the minimal FFmpeg that SubRec needs (Windows x64, shared DLLs) from pinned sources,
# by cross-compiling with MinGW-w64 on Linux (Ubuntu 24.04 is the reference host).
#
# DRAFT: written without network access and NEVER RUN. Option names of third-party build
# systems that were taken from memory are marked "verify". Expect fixes on the first run.
#
# Usage:
#   SUBREC_FFMPEG_VARIANT=gpl packaging/ffmpeg/build-ffmpeg.sh
#
# Environment:
#   SUBREC_FFMPEG_VARIANT          gpl (default) | lgpl | lgpl-openh264   (see README.md)
#   SUBREC_FFMPEG_TEST_COMPONENTS  1 (default) adds the few components SubRec's tests use
#   SUBREC_FFMPEG_WORK             work directory (default: ./_ffbuild)
#   JOBS                           parallel jobs (default: nproc)
#
# Result: $SUBREC_FFMPEG_WORK/out/ffmpeg-subrec-<variant>-win64/{bin,include,lib,share}
# (the layout cmake/SubRecFFmpeg.cmake expects for FFMPEG_ROOT). Then run
# collect-sources.sh to produce the GPL/LGPL corresponding source for the release package.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=versions.env
source "$SCRIPT_DIR/versions.env"

VARIANT=${SUBREC_FFMPEG_VARIANT:-gpl}
WITH_TEST_COMPONENTS=${SUBREC_FFMPEG_TEST_COMPONENTS:-1}
WORK=$(mkdir -p "${SUBREC_FFMPEG_WORK:-$PWD/_ffbuild}" && cd "${SUBREC_FFMPEG_WORK:-$PWD/_ffbuild}" && pwd)
HOST=x86_64-w64-mingw32
JOBS=${JOBS:-$(nproc)}

SRC=$WORK/src          # git checkouts (pristine; archived by collect-sources.sh)
BLD=$WORK/build        # out-of-tree build directories
DEPS=$WORK/deps        # static dependencies (x264, libvpl, headers)
OUT=$WORK/out/ffmpeg-subrec-$VARIANT-win64
INFO=$OUT/share/subrec-ffmpeg

log() { printf '==> %s\n' "$*" >&2; }
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

case $VARIANT in
    gpl|lgpl|lgpl-openh264) ;;
    *) die "unknown SUBREC_FFMPEG_VARIANT '$VARIANT' (gpl | lgpl | lgpl-openh264)" ;;
esac

for tool in git make nasm pkg-config cmake ninja xz sha256sum "$HOST-gcc" "$HOST-g++" "$HOST-objdump" "$HOST-strip"; do
    command -v "$tool" >/dev/null || die "missing build tool: $tool (see README.md, 'Build host')"
done
if ! "$HOST-gcc" -v 2>&1 | grep -q 'Thread model: posix'; then
    log "warning: $HOST-gcc is not the posix-thread variant; libvpl (C++) may fail to build"
fi

mkdir -p "$SRC" "$BLD" "$DEPS" "$INFO"

# Only our own .pc files; never the build host's.
export PKG_CONFIG_LIBDIR=$DEPS/lib/pkgconfig
export PKG_CONFIG_PATH=

# --- fetch ----------------------------------------------------------------------------
# fetch NAME REPO REF COMMIT [SPARSE_PATH...]
# Shallow-clones REF and requires that it resolves to COMMIT (commit id, or the tag object
# id for annotated tags). A moved branch (x264 "stable") falls back to fetching COMMIT.
fetch() {
    local name=$1 repo=$2 ref=$3 commit=$4; shift 4
    local dir=$SRC/$name head tagobj
    if [ ! -d "$dir/.git" ]; then
        log "clone $name $ref"
        if [ $# -gt 0 ]; then
            git clone --quiet --depth 1 --filter=blob:none --sparse --branch "$ref" "$repo" "$dir"
            git -C "$dir" sparse-checkout set "$@"
        else
            git clone --quiet --depth 1 --branch "$ref" "$repo" "$dir"
        fi
    fi
    head=$(git -C "$dir" rev-parse HEAD)
    tagobj=$(git -C "$dir" rev-parse -q --verify "refs/tags/$ref" 2>/dev/null || true)
    if [ "$head" != "$commit" ] && [ "$tagobj" != "$commit" ]; then
        log "$name: $ref is at $head, fetching pinned $commit"
        git -C "$dir" fetch --quiet --depth 1 origin "$commit" \
            || die "$name: cannot fetch pinned commit $commit (server may not allow fetching by id)"
        git -C "$dir" checkout --quiet --detach "$commit"
        head=$(git -C "$dir" rev-parse HEAD)
    fi
    [ "$head" = "$commit" ] || [ "$tagobj" = "$commit" ] \
        || die "$name: $ref resolves to $head (tag object ${tagobj:-none}), pinned $commit"
    [ -z "$(git -C "$dir" status --porcelain --untracked-files=no)" ] \
        || die "$name: tracked files are modified; the source must be pristine"
    printf '%s\t%s\t%s\t%s\n' "$name" "$repo" "$ref" "$head" >> "$INFO/sources.tsv"
}

: > "$INFO/sources.tsv"
fetch ffmpeg           "$FFMPEG_REPO"  "$FFMPEG_REF"  "$FFMPEG_COMMIT"
fetch nv-codec-headers "$NVCODEC_REPO" "$NVCODEC_REF" "$NVCODEC_COMMIT"
fetch amf              "$AMF_REPO"     "$AMF_REF"     "$AMF_COMMIT" amf/public/include
fetch libvpl           "$LIBVPL_REPO"  "$LIBVPL_REF"  "$LIBVPL_COMMIT"
case $VARIANT in
    gpl)           fetch x264     "$X264_REPO"     "$X264_REF"     "$X264_COMMIT" ;;
    lgpl-openh264) fetch openh264 "$OPENH264_REPO" "$OPENH264_REF" "$OPENH264_COMMIT" ;;
esac

# --- dependencies (all static or header-only) -------------------------------------------
log "nv-codec-headers"
make -C "$SRC/nv-codec-headers" PREFIX="$DEPS" install >/dev/null

log "AMF headers"
mkdir -p "$DEPS/include/AMF"
cp -a "$SRC/amf/amf/public/include/." "$DEPS/include/AMF/"

log "libvpl (static dispatcher)"
# verify: libvpl CMake option names (BUILD_TOOLS/BUILD_EXAMPLES/INSTALL_EXAMPLES/BUILD_TESTS).
cmake -S "$SRC/libvpl" -B "$BLD/libvpl" -G Ninja \
    -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_C_COMPILER="$HOST-gcc" -DCMAKE_CXX_COMPILER="$HOST-g++" -DCMAKE_RC_COMPILER="$HOST-windres" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_TOOLS=OFF -DBUILD_EXAMPLES=OFF -DINSTALL_EXAMPLES=OFF -DBUILD_TESTS=OFF
cmake --build "$BLD/libvpl" --parallel "$JOBS"
cmake --install "$BLD/libvpl"
# The static dispatcher is C++; make sure FFmpeg's configure check links libstdc++.
vplpc=$DEPS/lib/pkgconfig/vpl.pc
[ -f "$vplpc" ] || die "libvpl did not install $vplpc"
if ! grep -q -- '-lstdc++' "$vplpc"; then
    if grep -q '^Libs\.private:' "$vplpc"; then
        sed -i 's/^Libs\.private:.*/& -lstdc++/' "$vplpc"
    else
        echo 'Libs.private: -lstdc++' >> "$vplpc"
    fi
fi

if [ "$VARIANT" = gpl ]; then
    log "x264 (static, 8-bit 4:2:0 only: SubRec encodes yuv420p, encoder/ffvencoder.cpp:239)"
    mkdir -p "$BLD/x264"
    ( cd "$BLD/x264" && "$SRC/x264/configure" \
        --host="$HOST" --cross-prefix="$HOST-" --prefix="$DEPS" \
        --enable-static --enable-pic --disable-cli --disable-opencl --enable-win32thread \
        --bit-depth=8 --chroma-format=420 \
        --disable-avs --disable-swscale --disable-lavf --disable-ffms --disable-gpac --disable-lsmash \
      && make -j"$JOBS" && make install-lib-static )
fi

if [ "$VARIANT" = lgpl-openh264 ]; then
    log "OpenH264 (static)"
    # verify: OpenH264 Makefile variables for MinGW cross builds.
    make -C "$SRC/openh264" -j"$JOBS" OS=mingw_nt ARCH=x86_64 \
        CC="$HOST-gcc" CXX="$HOST-g++" AR="$HOST-ar" PREFIX="$DEPS" install-static
fi

# --- FFmpeg ----------------------------------------------------------------------------
# Components SubRec uses (file:line evidence in README.md, "Components").
encoders=(aac h264_nvenc h264_amf h264_qsv)
decoders=(rawvideo mjpeg h264 pcm_u8 pcm_s16le pcm_s32le pcm_f32le)
parsers=(h264 mjpeg)
muxers=(mp4)
demuxers=()
indevs=(dshow)
protocols=(file)
# buffer/buffersink/abuffer/abuffersink are always built into libavfilter (not configurable).
# scale/aresample/format/aformat are inserted automatically by AVFILTER_AUTO_CONVERT_ALL.
filters=(volume amix aresample aformat scale format anull null)
extra=()

if [ "$WITH_TEST_COMPONENTS" = 1 ]; then
    # tests/input_startup and tests/muxer_lifecycle
    indevs+=(lavfi); filters+=(sine color); demuxers+=(wav matroska)
    muxers+=(null nut); encoders+=(pcm_s16le rawvideo)
fi

case $VARIANT in
    gpl)
        encoders+=(libx264); extra+=(--enable-gpl --enable-libx264) ;;
    lgpl)
        # Software fallback via the Windows Media Foundation H.264 encoder. Needs a code change:
        # add "h264_mf" to the candidates in encoder/ffvencoder.cpp:257-262.
        encoders+=(h264_mf); extra+=(--enable-mediafoundation) ;;
    lgpl-openh264)
        encoders+=(h264_mf libopenh264); extra+=(--enable-mediafoundation --enable-libopenh264) ;;
esac

join() { local IFS=,; echo "$*"; }

args=(
    --prefix="$OUT"
    --enable-cross-compile --cross-prefix="$HOST-" --target-os=mingw32 --arch=x86_64
    --pkg-config=pkg-config --pkg-config-flags=--static
    --enable-shared --disable-static
    --disable-everything --disable-autodetect
    --disable-programs --disable-doc --disable-network --disable-debug
    --enable-w32threads
    --enable-avdevice --enable-avfilter --enable-swscale --enable-swresample
    --enable-d3d11va --enable-dxva2
    --enable-ffnvcodec --enable-nvenc --enable-cuda
    --enable-amf
    --enable-libvpl
    --enable-encoder="$(join "${encoders[@]}")"
    --enable-decoder="$(join "${decoders[@]}")"
    --enable-parser="$(join "${parsers[@]}")"
    --enable-muxer="$(join "${muxers[@]}")"
    --enable-indev="$(join "${indevs[@]}")"
    --enable-protocol="$(join "${protocols[@]}")"
    --enable-filter="$(join "${filters[@]}")"
    "${extra[@]}"
    --extra-cflags="-I$DEPS/include"
    --extra-ldflags="-L$DEPS/lib -static"
    --extra-version="subrec-$VARIANT"
)
[ ${#demuxers[@]} -eq 0 ] || args+=(--enable-demuxer="$(join "${demuxers[@]}")")

log "FFmpeg configure ($VARIANT)"
rm -rf "$BLD/ffmpeg" "$OUT/bin" "$OUT/lib" "$OUT/include"
mkdir -p "$BLD/ffmpeg"
( cd "$BLD/ffmpeg" && "$SRC/ffmpeg/configure" "${args[@]}" ) \
    || { tail -n 40 "$BLD/ffmpeg/ffbuild/config.log" >&2 || true; die "FFmpeg configure failed"; }

# configure silently drops components whose dependencies are missing: check every one.
upper() { tr '[:lower:]' '[:upper:]' <<<"$1"; }
need_cfg() { grep -q "^#define $1 1\$" "$2" || die "FFmpeg configure did not enable $1"; }
cc=$BLD/ffmpeg/config_components.h
for c in "${encoders[@]}";  do need_cfg "CONFIG_$(upper "$c")_ENCODER"  "$cc"; done
for c in "${decoders[@]}";  do need_cfg "CONFIG_$(upper "$c")_DECODER"  "$cc"; done
for c in "${parsers[@]}";   do need_cfg "CONFIG_$(upper "$c")_PARSER"   "$cc"; done
for c in "${muxers[@]}";    do need_cfg "CONFIG_$(upper "$c")_MUXER"    "$cc"; done
for c in "${demuxers[@]}";  do need_cfg "CONFIG_$(upper "$c")_DEMUXER"  "$cc"; done
for c in "${indevs[@]}";    do need_cfg "CONFIG_$(upper "$c")_INDEV"    "$cc"; done
for c in "${protocols[@]}"; do need_cfg "CONFIG_$(upper "$c")_PROTOCOL" "$cc"; done
for c in "${filters[@]}";   do need_cfg "CONFIG_$(upper "$c")_FILTER"   "$cc"; done

# License guard: the variant must produce exactly the intended license.
cfgh=$BLD/ffmpeg/config.h
grep -q '^#define CONFIG_NONFREE 0$' "$cfgh" || die "nonfree is enabled"
grep -q '^#define CONFIG_VERSION3 0$' "$cfgh" || die "version3 is enabled (not needed by any dependency)"
if [ "$VARIANT" = gpl ]; then
    grep -q '^#define CONFIG_GPL 1$' "$cfgh" || die "gpl variant without CONFIG_GPL"
else
    grep -q '^#define CONFIG_GPL 0$' "$cfgh" || die "LGPL variant has CONFIG_GPL enabled"
fi

log "FFmpeg build"
make -C "$BLD/ffmpeg" -j"$JOBS"
make -C "$BLD/ffmpeg" install

# --- checks on the result ----------------------------------------------------------------
cd "$OUT/bin"
actual=$(ls -1 ./*.dll | sed 's|^\./||' | sort | tr '\n' ' ')
expected=$(tr ' ' '\n' <<<"$EXPECTED_DLLS" | sed '/^$/d' | sort | tr '\n' ' ')
[ "$actual" = "$expected" ] || die "DLL set changed (soname bump?): got [$actual] expected [$expected]"

"$HOST-strip" --strip-unneeded ./*.dll

# No MinGW runtime or third-party DLL may be needed at run time (all linked statically).
system_dlls='^(kernel32|user32|gdi32|advapi32|ole32|oleaut32|shell32|shlwapi|psapi|bcrypt|msvcrt|ucrtbase|api-ms-win-[a-z0-9-]+|ws2_32|secur32|d3d9|d3d11|dxgi|dxva2|mfplat|mf|evr|cfgmgr32|setupapi|version|winmm)\.dll$'
for dll in ./*.dll; do
    while read -r imp; do
        lower=$(tr '[:upper:]' '[:lower:]' <<<"$imp")
        case " $EXPECTED_DLLS " in *" $imp "*) continue ;; esac
        [[ $lower =~ $system_dlls ]] || die "$dll imports $imp (not a Windows system DLL)"
    done < <("$HOST-objdump" -p "$dll" | sed -n 's/^[[:space:]]*DLL Name: //p')
done

# --- record what was built -----------------------------------------------------------------
sanitize() { sed -e "s|$WORK|<WORK>|g" -e "s|${HOME:-/nonexistent-home}|<HOME>|g"; }
{
    echo "variant=$VARIANT"
    echo "test_components=$WITH_TEST_COMPONENTS"
    echo "host=$(uname -srm)"
    if [ -r /etc/os-release ]; then (. /etc/os-release; echo "os=${PRETTY_NAME:-unknown}"); fi
    echo "cc=$("$HOST-gcc" --version | head -n1)"
    echo "cxx=$("$HOST-g++" --version | head -n1)"
    echo "nasm=$(nasm -v)"
    echo "cmake=$(cmake --version | head -n1)"
    if command -v dpkg-query >/dev/null; then
        echo "mingw-w64-headers=$(dpkg-query -W -f='${Version}' mingw-w64-x86-64-dev 2>/dev/null || echo unknown)"
    fi
} | sanitize > "$INFO/toolchain.txt"
printf '%s\n' "${args[@]}" | sanitize > "$INFO/configure-args.txt"
sha256sum ./*.dll > "$INFO/dlls.sha256"

mkdir -p "$INFO/licenses"
for name in $(cut -f1 "$INFO/sources.tsv"); do
    for f in "$SRC/$name"/COPYING* "$SRC/$name"/LICENSE*; do
        if [ -f "$f" ]; then cp "$f" "$INFO/licenses/$name-$(basename "$f")"; fi
    done
done

log "done: $OUT"
cat "$INFO/dlls.sha256" >&2
