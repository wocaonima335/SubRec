#!/usr/bin/env python3
"""Portable, dependency-free preparation and verification for Windows packaging."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import zipfile

HERE = Path(__file__).resolve().parent
VERSION = re.compile(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?")
MARKER = "INCOMPLETE-DO-NOT-DISTRIBUTE.txt"


def version(ref_type, ref_name, sha):
    if not re.fullmatch(r"[0-9a-fA-F]{7,40}", sha):
        raise ValueError("Expected a Git commit hash")
    if ref_type == "tag":
        if not ref_name.startswith("v"):
            raise ValueError("Release tags must start with v")
        display = ref_name[1:]
        stem = f"SubRec-{ref_name}"
    else:
        display = f"0.0.0-ci.{sha[:7]}"
        stem = f"SubRec-ci-{sha[:7]}"
    match = VERSION.fullmatch(display)
    if not match or any(int(n) > 65535 for n in match.groups()):
        raise ValueError("Expected vX.Y.Z[-prerelease], numeric components <= 65535")
    # D10: unresolved icon licenses permit pre-releases only.
    if ref_type == "tag" and "-" not in display:
        raise ValueError("Stable releases blocked until icon licenses are resolved (D10)")
    return dict(version=display, numeric=".".join(match.groups()) + ".0", stem=stem)


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def prepare(payload, compiler, stem, is_tag):
    for name in ("ChineseSimplified.isl", "ChineseSimplified.LICENSE.txt"):
        if not (HERE / name).is_file():
            raise ValueError(f"Missing {name}: obtain upstream material; see packaging/installer/README.md")
    for name in ("appSubRec.exe", "LICENSE.txt", "THIRD_PARTY_LICENSES.md", "SOURCES.txt",
                 "sources/SHA256SUMS", "sources/BUILD_CONFIG.txt"):
        if not (payload / name).is_file():
            raise ValueError(f"Missing payload file: {name}")
    license_file = compiler / "License.txt"
    if not license_file.is_file():
        raise ValueError("Inno Setup License.txt missing; do not substitute a guessed license")
    target = payload / "licenses/inno-setup"
    target.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(license_file, target / "LICENSE.txt")
    for name in ("ChineseSimplified.isl", "ChineseSimplified.LICENSE.txt"):
        shutil.copyfile(HERE / name, target / name)
    shutil.copyfile(HERE / "THIRD_PARTY_SOURCES.md", target / "THIRD_PARTY_SOURCES.md")
    current = {p.name for p in payload.iterdir() if p.is_dir()}
    historical = {line.strip() for line in (HERE / "cleanup-directories.txt").read_text().splitlines()
                  if line.strip() and not line.lstrip().startswith("#")}
    directories = sorted(current | historical)
    for name in directories:
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", name):
            raise ValueError(f"Unsafe deployment directory for InstallDelete: {name}")
    lines = [f'Type: filesandordirs; Name: "{{app}}\\{name}"' for name in directories]
    (HERE / "installdelete.generated.iss").write_text("\n".join(lines) + "\n", encoding="utf-8-sig")
    missing = current - historical
    if missing:
        print("Retain these directories in cleanup-directories.txt before release: " + ", ".join(sorted(missing)))
        if is_tag:
            raise ValueError("New deployment directories must be retained for future upgrade cleanup")
    if any(payload.rglob(MARKER)):
        if is_tag:
            raise ValueError("Incomplete payload cannot be released")
        stem += "-INCOMPLETE"
    return stem


def asset_names(stem):
    if not re.fullmatch(r"SubRec-[A-Za-z0-9.-]+", stem):
        raise ValueError("Unsafe artifact stem")
    return [stem + "-win64-setup.exe", stem + "-win64-portable.zip",
            stem + "-ffmpeg-corresponding-source.zip"]


def archive_tree(archive, root, prefix=""):
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"Unexpected symlink in payload: {path}")
        archive.write(path, prefix + path.relative_to(root).as_posix())


def bundle(payload, output, stem):
    names = asset_names(stem)
    if not (output / names[0]).is_file():
        raise ValueError("Installer must be built and smoke-tested first")
    with zipfile.ZipFile(output / names[1], "w", zipfile.ZIP_DEFLATED) as archive:
        archive_tree(archive, payload)
    with zipfile.ZipFile(output / names[2], "w", zipfile.ZIP_DEFLATED) as archive:
        archive_tree(archive, payload / "sources", "sources/")
        archive_tree(archive, payload / "licenses/ffmpeg", "licenses/")
    (output / "SHA256SUMS.txt").write_text(
        "".join(f"{sha256(output / name)}  {name}\n" for name in names), encoding="ascii")


def verify(output, stem):
    names = asset_names(stem)
    if "INCOMPLETE" in stem:
        raise ValueError("Incomplete artifacts cannot be released")
    if {p.name for p in output.iterdir()} != set(names + ["SHA256SUMS.txt"]):
        raise ValueError("Unexpected or missing release assets")
    expected = "".join(f"{sha256(output / name)}  {name}\n" for name in names)
    if (output / "SHA256SUMS.txt").read_text(encoding="ascii") != expected:
        raise ValueError("Release checksum manifest mismatch")
    for name in names[1:]:
        with zipfile.ZipFile(output / name) as archive:
            if any(Path(item).name == MARKER for item in archive.namelist()):
                raise ValueError("Incomplete source/license material in release archive")
            if archive.testzip() is not None:
                raise ValueError("Corrupt release archive")


def emit(values):
    text = "".join(f"{key}={value}\n" for key, value in values.items())
    print(text, end="")
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
            stream.write(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("version")  # Uses GitHub's non-secret ref/commit environment.
    prep = commands.add_parser("prepare")
    prep.add_argument("--compiler", type=Path, required=True)
    for command in (prep, commands.add_parser("bundle"), commands.add_parser("verify")):
        command.add_argument("--stem", required=True)
        command.add_argument("--payload", type=Path, default=Path("dist/SubRec"))
        command.add_argument("--output", type=Path, default=Path("out"))
    args = parser.parse_args()
    if args.command == "version":
        emit(version(os.environ.get("GITHUB_REF_TYPE", "branch"),
                     os.environ.get("GITHUB_REF_NAME", ""), os.environ["GITHUB_SHA"]))
    elif args.command == "prepare":
        emit({"stem": prepare(args.payload, args.compiler, args.stem,
                              os.environ.get("GITHUB_REF_TYPE") == "tag")})
    elif args.command == "bundle":
        bundle(args.payload, args.output, args.stem)
    else:
        verify(args.output, args.stem)


if __name__ == "__main__":
    main()
