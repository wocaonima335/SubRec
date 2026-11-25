#!/usr/bin/env python3
"""Assemble an optional Windows C API host bundle from local files; no network."""
import argparse
import os
from pathlib import Path
import shutil

from assets import HERE, manifest, verify


def assemble(root, exe, payload, vad=None, vad_file=None):
    pins = manifest()
    root, exe, payload = Path(root), Path(exe), Path(payload)
    if vad not in (None, "v6.2.3") or bool(vad) != bool(vad_file):
        raise ValueError("VAD requires both --vad v6.2.3 and --vad-file; default is no VAD")
    targets = (payload / "asr", payload / "licenses/asr")
    for target in targets:
        if target.exists() or target.is_symlink():
            raise ValueError(f"Use a fresh ASR destination (avoids stale DLLs/models): {target}")
    if not (root / "include/sherpa-onnx/c-api/c-api.h").is_file():
        raise ValueError("SHERPA_ONNX_ROOT must be the full Windows package, including c-api.h")
    if not exe.is_file() or exe.stat().st_size == 0:
        raise ValueError(f"Missing or empty built MSVC subrec-asr.exe: {exe}")
    runtime = []
    for name in pins["runtime"]["ship"]:
        source = root / "lib" / name
        if not source.is_file() or source.stat().st_size != pins["runtime"]["lib_files"][name]:
            raise ValueError(f"Missing/wrong-size pinned Windows DLL: {source}")
        runtime.append((source, targets[0] / name))
    license_root = HERE.parent / "licenses/asr"
    for name, pin in pins["licenses"].items():
        verify(license_root / name, pin)
    notice = license_root / "NOTICE.txt"
    if not notice.is_file():
        raise ValueError(f"Missing bundle notice: {notice}")
    if vad:
        verify(vad_file, pins["assets"]["vad-v6.2.3"])
        runtime.append((Path(vad_file), targets[0] / "silero_vad.onnx"))
    # Preflight all inputs before writing; copy only this explicit runtime allowlist.
    for target in targets:
        target.mkdir(parents=True)
    for source, target in [(exe, targets[0] / "subrec-asr.exe")] + runtime:
        shutil.copyfile(source, target)
    for name in [*pins["licenses"], "NOTICE.txt"]:
        shutil.copyfile(license_root / name, targets[1] / name)
    print(f"Assembled {targets[0]}; VAD: {vad or 'absent (S0 decision pending)'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sherpa-onnx-root", type=Path, default=os.environ.get("SHERPA_ONNX_ROOT"))
    parser.add_argument("--exe", type=Path, required=True, help="built MSVC /MT host")
    parser.add_argument("--payload", type=Path, required=True)
    parser.add_argument("--vad", choices=["v6.2.3"], help="reserved candidate; use only with S0 approval")
    parser.add_argument("--vad-file", type=Path)
    args = parser.parse_args()
    if args.sherpa_onnx_root is None:
        parser.error("provide SHERPA_ONNX_ROOT or --sherpa-onnx-root")
    try:
        assemble(args.sherpa_onnx_root, args.exe, args.payload, args.vad, args.vad_file)
    except (OSError, ValueError) as error:
        parser.exit(1, f"ASR assembly failed: {error}\n")


if __name__ == "__main__":
    main()
