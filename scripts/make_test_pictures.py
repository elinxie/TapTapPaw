#!/usr/bin/env python3
"""Writes the picture fixtures used by scripts/layout_smoke_test.py into sketch/test/fixtures/ (needs Pillow).
SJPG is built the same way the web page builds it: the image cut into 16-row strips, each a baseline JPEG."""
import io, os, struct
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sketch", "test", "fixtures")


def sjpg(img, quality=70, progressive=False):
    w, h = img.size
    parts = []
    for y in range(0, h, 16):
        buf = io.BytesIO()
        img.crop((0, y, w, min(h, y + 16))).save(buf, "JPEG", quality=quality, progressive=progressive)
        parts.append(buf.getvalue())
    head = b"_SJPG__\0V1.00\0" + struct.pack("<HHHH", w, h, len(parts), 16) + b"".join(struct.pack("<H", len(p)) for p in parts)
    return head + b"".join(parts)


def picture(w, h, seed):
    img = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(img)
    for y in range(h):  # vertical gradient
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=(int(30 + 200 * t), int(60 + 80 * (1 - t)), int(160 - 100 * t)))
    for i in range(6):
        x, y, r = (seed * 37 + i * 41) % w, (seed * 53 + i * 67) % h, 10 + (i * 7) % 30
        d.ellipse([x - r, y - r, x + r, y + r], fill=((i * 50) % 255, 220, (i * 90) % 255))
    d.rectangle([4, 4, w - 5, h - 5], outline=(255, 255, 255), width=2)
    return img


def main():
    os.makedirs(OUT, exist_ok=True)
    files = {
        "pic_240x320.sjpg": sjpg(picture(240, 320, 1)),
        "pic_96x64.sjpg": sjpg(picture(96, 64, 2)),
        "pic_progressive.sjpg": sjpg(picture(96, 64, 3), progressive=True),  # the board's decoder cannot read progressive JPEG
    }
    buf = io.BytesIO()
    picture(96, 96, 4).save(buf, "JPEG", quality=70)
    files["art_96.jpg"] = buf.getvalue()
    for name, data in files.items():
        open(os.path.join(OUT, name), "wb").write(data)
        print("%-22s %6d bytes" % (name, len(data)))


if __name__ == "__main__":
    main()
