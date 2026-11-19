"""Generate minimalist flat-badge .ico files for LiteWeasel.

Sizes included in every .ico: 16, 20, 24, 32, 48, 64, 256.
Small sizes (<=24px) for 'zh' and 'en' use pixel-snapped bitmap masks so they
stay crisp in the Windows 10/11 system tray and language bar.
"""
from __future__ import annotations

import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

SIZES = [16, 20, 24, 32, 48, 64, 256]

TEAL = (8, 145, 178, 255)        # #0891B2 - primary LiteWeasel teal-blue
TEAL_ACCENT = (103, 232, 249, 255)  # #67E8F9 - bright cyan accent dot
SLATE = (51, 65, 85, 255)        # #334155 - distinct slate-blue for EN mode
AMBER = (217, 119, 6, 255)       # #D97706 - maintenance / reload
WHITE = (255, 255, 255, 255)


def rounded_badge(size: int, bg: tuple[int, int, int, int]) -> Image.Image:
    # Render at 4x supersampling for clean anti-aliased rounded corners on >=24px
    scale = 1 if size <= 20 else 4
    w = size * scale
    img = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    pad = max(1, round(w * 0.06))
    radius = max(2, round(w * 0.24))
    draw.rounded_rectangle([pad, pad, w - 1 - pad, w - 1 - pad], radius=radius, fill=bg)
    if scale > 1:
        img = img.resize((size, size), Image.Resampling.LANCZOS)
    return img


def draw_pixel_glyph(img: Image.Image, rows: list[str], color=WHITE) -> None:
    """Draw a pixel-snapped glyph centered on a small icon."""
    w, h = img.size
    gh = len(rows)
    gw = len(rows[0])
    ox = (w - gw) // 2
    oy = (h - gh) // 2
    px = img.load()
    for y, row in enumerate(rows):
        for x, ch in enumerate(row):
            if ch == "#":
                px[ox + x, oy + y] = color


ZH_10x10 = [
    "....##....",
    "....##....",
    ".########.",
    ".##.##.##.",
    ".##.##.##.",
    ".##.##.##.",
    ".########.",
    "....##....",
    "....##....",
    "....##....",
]

EN_8x10 = [
    "...##...",
    "..####..",
    ".##..##.",
    ".##..##.",
    ".######.",
    ".######.",
    ".##..##.",
    ".##..##.",
    ".##..##.",
    "........",
]


def load_font(size: int, bold: bool = True) -> ImageFont.FreeTypeFont:
    candidates = [
        "C:/Windows/Fonts/msyhbd.ttc" if bold else "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/arialbd.ttf",
    ]
    for path in candidates:
        if Path(path).exists():
            return ImageFont.truetype(path, size=size)
    return ImageFont.load_default()


def draw_centered_text(img: Image.Image, text: str, ratio: float = 0.62) -> None:
    size = img.size[0]
    scale = 4
    w = size * scale
    overlay = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    font = load_font(int(w * ratio), bold=True)
    bbox = draw.textbbox((0, 0), text, font=font)
    tw = bbox[2] - bbox[0]
    th = bbox[3] - bbox[1]
    x = (w - tw) / 2 - bbox[0]
    y = (w - th) / 2 - bbox[1]
    draw.text((x, y), text, font=font, fill=WHITE)
    overlay = overlay.resize((size, size), Image.Resampling.LANCZOS)
    img.alpha_composite(overlay)


def make_weasel(size: int) -> Image.Image:
    img = rounded_badge(size, TEAL)
    if size <= 20:
        # Crisp pixel-snapped 'L' + cyan dot
        rows = [
            "##......",
            "##....##",
            "##....##",
            "##......",
            "##......",
            "##......",
            "#######.",
            "#######.",
        ]
        draw_pixel_glyph(img, rows, WHITE)
        # Color the top-right 2x2 dot bright cyan
        px = img.load()
        ox = (size - 8) // 2
        oy = (size - 8) // 2
        for dy in (1, 2):
            for dx in (6, 7):
                px[ox + dx, oy + dy] = TEAL_ACCENT
        return img

    scale = 4
    w = size * scale
    ov = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    stroke = max(4, round(w * 0.14))
    left = round(w * 0.28)
    top = round(w * 0.24)
    bottom = round(w * 0.76)
    right = round(w * 0.72)
    # Clean geometric 'L'
    d.rounded_rectangle([left, top, left + stroke, bottom], radius=stroke // 3, fill=WHITE)
    d.rounded_rectangle([left, bottom - stroke, right, bottom], radius=stroke // 3, fill=WHITE)
    # Bright cyan accent dot at top-right
    dot_r = max(3, round(w * 0.095))
    cx = right - dot_r // 2
    cy = top + dot_r // 2
    d.ellipse([cx - dot_r, cy - dot_r, cx + dot_r, cy + dot_r], fill=TEAL_ACCENT)
    ov = ov.resize((size, size), Image.Resampling.LANCZOS)
    img.alpha_composite(ov)
    return img


def make_zh(size: int) -> Image.Image:
    img = rounded_badge(size, TEAL)
    if size <= 20:
        draw_pixel_glyph(img, ZH_10x10, WHITE)
    else:
        draw_centered_text(img, "中", ratio=0.60)
    return img


def make_en(size: int) -> Image.Image:
    img = rounded_badge(size, SLATE)
    if size <= 20:
        draw_pixel_glyph(img, EN_8x10, WHITE)
    else:
        draw_centered_text(img, "A", ratio=0.62)
    return img


def make_full(size: int) -> Image.Image:
    img = rounded_badge(size, TEAL)
    scale = 4
    w = size * scale
    ov = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    r = round(w * 0.26)
    c = w // 2
    d.ellipse([c - r, c - r, c + r, c + r], fill=WHITE)
    ov = ov.resize((size, size), Image.Resampling.LANCZOS)
    img.alpha_composite(ov)
    return img


def make_half(size: int) -> Image.Image:
    img = rounded_badge(size, TEAL)
    scale = 4
    w = size * scale
    ov = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    r = round(w * 0.26)
    c = w // 2
    stroke = max(4, round(w * 0.08))
    d.ellipse([c - r, c - r, c + r, c + r], outline=WHITE, width=stroke)
    d.pieslice([c - r, c - r, c + r, c + r], start=90, end=270, fill=WHITE)
    ov = ov.resize((size, size), Image.Resampling.LANCZOS)
    img.alpha_composite(ov)
    return img


def make_reload(size: int) -> Image.Image:
    img = rounded_badge(size, AMBER)
    scale = 4
    w = size * scale
    ov = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    c = w / 2
    r = w * 0.25
    stroke = max(4, round(w * 0.09))
    bbox = [c - r, c - r, c + r, c + r]
    d.arc(bbox, start=25, end=155, fill=WHITE, width=stroke)
    d.arc(bbox, start=205, end=335, fill=WHITE, width=stroke)
    # Two clean arrowheads
    head = w * 0.095
    for angle_deg in (155, 335):
        rad = math.radians(angle_deg)
        ax = c + r * math.cos(rad)
        ay = c + r * math.sin(rad)
        tx = -math.sin(rad)
        ty = math.cos(rad)
        nx = math.cos(rad)
        ny = math.sin(rad)
        pts = [
            (ax + tx * head * 0.7, ay + ty * head * 0.7),
            (ax - tx * head * 0.5 + nx * head * 0.75, ay - ty * head * 0.5 + ny * head * 0.75),
            (ax - tx * head * 0.5 - nx * head * 0.75, ay - ty * head * 0.5 - ny * head * 0.75),
        ]
        d.polygon(pts, fill=WHITE)
    ov = ov.resize((size, size), Image.Resampling.LANCZOS)
    img.alpha_composite(ov)
    return img


def save_ico(path: Path, builder) -> None:
    frames = [builder(s) for s in SIZES]
    frames[-1].save(
        path,
        format="ICO",
        sizes=[(s, s) for s in SIZES],
        append_images=frames[:-1],
    )


def main() -> None:
    repo = Path(__file__).resolve().parent.parent
    res = repo / "resource"
    targets = {
        res / "weasel.ico": make_weasel,
        res / "zh.ico": make_zh,
        res / "en.ico": make_en,
        res / "full.ico": make_full,
        res / "half.ico": make_half,
        res / "reload.ico": make_reload,
        repo / "WeaselSetup" / "WeaselSetup.ico": make_weasel,
    }
    for path, fn in targets.items():
        save_ico(path, fn)
        print(f"wrote {path.relative_to(repo)} ({path.stat().st_size} B)")


if __name__ == "__main__":
    main()
