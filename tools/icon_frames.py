"""Q59c: rebuild the tray/exe ICO frames from the supplied artwork.

No redrawing: the 256 px master is the only source, every smaller frame is a
Lanczos downscale of it. The size list covers the tray's *physical* pixel size
at common Windows scaling factors (SM_CXSMICON x scale):

    16 = 100 %, 20 = 125 %, 24 = 150 %, 28 = 175 %, 32 = 200 %, 40 = 250 %
    plus 48/64/256 for Explorer and the exe icon.

Why 28 matters here: the tray supervisor used to be a DPI-unaware process, so
SystemInformation.SmallIconSize reported 16 on a 175 % display while the taskbar
drew 28 px -- Windows stretched the 16 px frame and the icon looked thin and
blurry. Program.cs now asks for the physical size, and the ICO has to contain
that exact frame or Windows resamples again.

    python tools/icon_frames.py            # rewrite both ICOs
    python tools/icon_frames.py --check    # only report the frames
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / "assets"
ICONS = (ASSETS / "dropbridge.ico", ASSETS / "dropbridge-tray-white.ico")
SIZES = (16, 20, 24, 28, 32, 40, 48, 64, 256)


def rebuild(path: Path, write: bool) -> None:
    icon = Image.open(path)
    icon.size = (256, 256)
    icon.load()
    master = icon.convert("RGBA")

    print(f"== {path.name}")
    if write:
        master.save(path, format="ICO", sizes=[(s, s) for s in SIZES])
        icon = Image.open(path)
        print(f"  wrote {path.stat().st_size} bytes, sizes={sorted(icon.info.get('sizes', []))}")

    for size in SIZES:
        frame = master.resize((size, size), Image.LANCZOS)
        alpha = frame.getchannel("A").histogram()
        solid = sum(alpha[200:])
        semi = sum(alpha[20:200])
        print(f"  {size:>3}px: solid={solid:4d} semi={semi:4d}")


def main(argv: list[str]) -> int:
    write = "--check" not in argv
    for path in ICONS:
        if not path.exists():
            print(f"missing {path}")
            return 1
        rebuild(path, write)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
