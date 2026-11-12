#!/usr/bin/env python3
"""Build a multi-size Windows ICO from the maintainer-selected AI-generated PNG.

Requires Pillow. Source, exact prompt and copyright uncertainty are recorded in
icon/README.md and THIRD_PARTY_LICENSES.md. No image model is called by this script.
"""
from hashlib import sha256
from pathlib import Path

from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "packaging/installer/icon/subrec-icon-source.png"
OUTPUT = ROOT / "packaging/installer/SubRec.ico"
SIZES = [(16, 16), (32, 32), (48, 48), (256, 256)]


def main():
    expected = "379598a9fa591801d582cb9e85338ed20e902ccd31713feb97ff086ee2a3b550"
    if sha256(SOURCE.read_bytes()).hexdigest() != expected:
        raise SystemExit("Icon source changed; review provenance before regenerating")
    # Historical note from the superseded SVG conversion, retained for traceability:
    # A single solid recording-red glyph, transparent background, no new artwork.
    # Current source is the selected AI PNG; resize each frame from the original.
    frames = []
    with Image.open(SOURCE) as source:
        if source.size != (1024, 1024) or source.mode != "RGBA":
            raise SystemExit("Expected the original 1024x1024 RGBA candidate")
        for size in SIZES:
            frame = source.resize(size, Image.Resampling.LANCZOS)
            if size[0] <= 32:
                alpha = frame.getchannel("A")
                frame = frame.convert("RGB").filter(
                    ImageFilter.UnsharpMask(radius=0.5, percent=110, threshold=2)
                ).convert("RGBA")
                frame.putalpha(alpha)
            frames.append(frame)
    frames[-1].save(OUTPUT, format="ICO", sizes=SIZES, append_images=frames[:-1])
    with Image.open(OUTPUT) as icon:
        assert icon.ico.sizes() == set(SIZES)
        for frame in frames:
            decoded = icon.ico.getimage(frame.size)
            assert decoded.mode == "RGBA" and decoded.tobytes() == frame.tobytes()
    print(f"{OUTPUT.relative_to(ROOT)}: {SIZES}; SHA256 {sha256(OUTPUT.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
