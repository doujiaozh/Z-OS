import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

WIDTH = 8
HEIGHT = 16
FIRST = 32
LAST = 126
FONT_CANDIDATES = [
    r"C:\Windows\Fonts\consola.ttf",
    r"C:\Windows\Fonts\lucon.ttf",
    r"C:\Windows\Fonts\cour.ttf",
    r"C:\Windows\Fonts\arial.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
]


def load_font(size):
    for candidate in FONT_CANDIDATES:
        path = Path(candidate)
        if path.exists():
            try:
                return ImageFont.truetype(str(path), size)
            except OSError:
                continue
    return ImageFont.load_default()


def render_glyph(font, character):
    image = Image.new("L", (WIDTH, HEIGHT), 0)
    draw = ImageDraw.Draw(image)
    draw.text((0, 0), character, fill=255, font=font)
    rows = []
    for y in range(HEIGHT):
        value = 0
        for x in range(WIDTH):
            if image.getpixel((x, y)) > 96:
                value |= 1 << (7 - x)
        rows.append(value)
    return rows


def build_font(size):
    font = load_font(size)
    glyphs = [render_glyph(font, chr(code)) for code in range(FIRST, LAST + 1)]
    return glyphs


def score(glyphs):
    total = 0
    for index, rows in enumerate(glyphs):
        code = FIRST + index
        if code == 32:
            continue
        lit = sum(bin(row).count("1") for row in rows)
        if lit == 0:
            return -1
        total += lit
    return total


def main():
    best = None
    best_score = -1
    best_size = 0
    for size in range(11, 17):
        glyphs = build_font(size)
        value = score(glyphs)
        if value > best_score:
            best = glyphs
            best_score = value
            best_size = size
    if best is None or best_score < 0:
        raise SystemExit("could not render a usable font")
    lines = []
    lines.append("#ifndef Z_OS_FONT_DATA_H")
    lines.append("#define Z_OS_FONT_DATA_H")
    lines.append("")
    lines.append("#define FONT_FIRST_CHAR 32")
    lines.append("#define FONT_LAST_CHAR 126")
    lines.append("#define FONT_GLYPH_COUNT 95")
    lines.append("#define FONT_WIDTH 8")
    lines.append("#define FONT_HEIGHT 16")
    lines.append("")
    lines.append("static const unsigned char font8x16[FONT_GLYPH_COUNT][FONT_HEIGHT] = {")
    for index, rows in enumerate(best):
        code = FIRST + index
        label = chr(code)
        if label == "'":
            label = "quote"
        elif label == "\\":
            label = "backslash"
        formatted = ", ".join("0x%02X" % row for row in rows)
        lines.append("    { " + formatted + " },")
    lines.append("};")
    lines.append("")
    lines.append("#endif")
    target = Path(__file__).resolve().parent.parent / "kernel" / "gui" / "font_data.h"
    target.write_text("\n".join(lines) + "\n", encoding="ascii")
    print("font size", best_size, "lit pixels", best_score, "target", target)


if __name__ == "__main__":
    main()
