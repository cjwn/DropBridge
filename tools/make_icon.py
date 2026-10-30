"""Generate the AirBridge32 tray/exe icon with Pillow (bundled).

Blue rounded square with a white AirDrop-like ring and a downward arrow,
rendered at several sizes into assets/airbridge32.ico. Run once; the .ico is
committed so the build does not depend on Pillow.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SIZES = (16, 24, 32, 48, 64, 128, 256)
BG = (0, 122, 255, 255)      # AirDrop-ish blue
FG = (255, 255, 255, 255)


def render(size: int) -> Image.Image:
    scale = 8
    s = size * scale
    image = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    radius = int(s * 0.22)
    draw.rounded_rectangle((0, 0, s - 1, s - 1), radius=radius, fill=BG)
    # Three concentric arcs suggest a broadcast; a solid disc anchors the centre.
    centre = (s / 2, s / 2)
    for index, fraction in enumerate((0.30, 0.44, 0.58)):
        box = (centre[0] - s * fraction / 2, centre[1] - s * fraction / 2,
               centre[0] + s * fraction / 2, centre[1] + s * fraction / 2)
        draw.arc(box, start=200, end=340, fill=FG, width=max(2, int(s * 0.045)))
    dot = s * 0.075
    draw.ellipse((centre[0] - dot, centre[1] - dot * 0.2,
                  centre[0] + dot, centre[1] + dot * 1.8), fill=FG)
    return image.resize((size, size), Image.LANCZOS)


def main() -> int:
    out = Path(__file__).resolve().parent.parent / "assets" / "dropbridge.ico"
    out.parent.mkdir(parents=True, exist_ok=True)
    # Pillow builds every requested size from one master image, which keeps the
    # ICO multi-resolution (Explorer, tray, alt-tab each pick their own).
    master = render(256)
    master.save(out, format="ICO", sizes=[(size, size) for size in SIZES])
    print(f"wrote {out} ({out.stat().st_size} bytes, sizes {SIZES})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
