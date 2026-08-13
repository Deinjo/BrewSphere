#!/usr/bin/env python3
"""Build the BrewSphere brand system and the embedded 240x240 startup asset."""

from __future__ import annotations

import argparse
import os
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
BRAND_DIR = ROOT / "docs/assets/brand"

COLORS = {
    "deep": "#071A2A",
    "navy": "#0B3552",
    "ring": "#06264A",
    "blue": "#3D83DF",
    "cyan": "#41DEDE",
    "cream": "#FFF1C9",
    "amber": "#FF9D00",
    "orange": "#E87300",
    "muted": "#9BB1C2",
}


def emblem_body() -> str:
    return f"""
  <defs>
    <clipPath id="beerClip">
      <path d="M219 207 C220 252 224 301 229 339 Q256 348 283 339 C288 301 292 252 293 207 Z"/>
    </clipPath>
  </defs>
  <circle cx="256" cy="256" r="246" fill="{COLORS['navy']}"/>
  <circle cx="256" cy="256" r="222" fill="none" stroke="{COLORS['ring']}" stroke-width="48"/>
  <circle cx="256" cy="256" r="222" fill="none" stroke="{COLORS['amber']}" stroke-width="48"
          stroke-dasharray="174.4 1220.5" transform="rotate(-45 256 256)"/>
  <g stroke="{COLORS['cream']}" stroke-width="10" stroke-linecap="butt">
    <path d="M394.6 117.4 L430 82"/>
    <path d="M452 256 L502 256"/>
    <path d="M394.6 394.6 L430 430"/>
    <path d="M117.4 394.6 L82 430"/>
    <path d="M60 256 L10 256"/>
    <path d="M117.4 117.4 L82 82"/>
  </g>
  <circle cx="256" cy="256" r="246" fill="none" stroke="{COLORS['cream']}" stroke-width="6"/>
  <circle cx="256" cy="256" r="177" fill="none" stroke="{COLORS['cream']}" stroke-width="11"/>
  <g fill="none" stroke="{COLORS['cream']}" stroke-width="6" stroke-linecap="round">
    <path d="M169 139 C111 194 111 318 169 373"/>
    <path d="M343 139 C401 194 401 318 343 373"/>
  </g>
  <g clip-path="url(#beerClip)">
    <path d="M205 273 Q250 286 307 270 L307 365 L205 365 Z" fill="{COLORS['amber']}"/>
    <path d="M205 329 Q256 347 307 326 L307 365 L205 365 Z" fill="{COLORS['orange']}"/>
  </g>
  <path d="M205 171 C205 151 222 142 239 148 C248 137 267 137 276 148
           C294 142 311 151 311 171 Z" fill="{COLORS['cream']}"/>
  <path d="M207 188 C206 237 210 286 220 350 Q256 365 292 350
           C302 286 306 237 305 188" fill="none" stroke="{COLORS['cream']}"
        stroke-width="13" stroke-linecap="square" stroke-linejoin="round"/>
  <path d="M226 340 Q256 350 286 340" fill="none" stroke="{COLORS['cream']}" stroke-width="8"/>
  <circle cx="273" cy="237" r="12" fill="{COLORS['cyan']}" stroke="{COLORS['cream']}" stroke-width="3"/>
  <circle cx="249" cy="267" r="9" fill="{COLORS['amber']}" stroke="{COLORS['cream']}" stroke-width="2"/>
  <circle cx="267" cy="286" r="10" fill="#BFEFFF" stroke="{COLORS['cream']}" stroke-width="2"/>
"""


def emblem_svg() -> str:
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" role="img" aria-labelledby="title">
  <title id="title">BrewSphere emblem</title>{emblem_body()}
</svg>
"""


def compact_svg() -> str:
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128" role="img" aria-labelledby="title">
  <title id="title">BrewSphere compact mark</title>
  <defs><clipPath id="beer"><path d="M48 48 L80 48 L76 93 Q64 98 52 93 Z"/></clipPath></defs>
  <circle cx="64" cy="64" r="61" fill="{COLORS['navy']}"/>
  <circle cx="64" cy="64" r="53" fill="none" stroke="{COLORS['blue']}" stroke-width="9"/>
  <circle cx="64" cy="64" r="53" fill="none" stroke="{COLORS['amber']}" stroke-width="9"
          stroke-dasharray="41.6 291.4" transform="rotate(-45 64 64)"/>
  <g stroke="{COLORS['cream']}" stroke-width="3" stroke-linecap="butt">
    <path d="M97.2 30.8 L105.7 22.3"/>
    <path d="M111 64 L123 64"/>
    <path d="M97.2 97.2 L105.7 105.7"/>
    <path d="M30.8 97.2 L22.3 105.7"/>
    <path d="M17 64 L5 64"/>
    <path d="M30.8 30.8 L22.3 22.3"/>
  </g>
  <circle cx="64" cy="64" r="61" fill="none" stroke="{COLORS['cream']}" stroke-width="2"/>
  <path d="M47 43 Q48 35 55 37 Q64 30 72 37 Q80 35 81 43 Z" fill="{COLORS['cream']}"/>
  <path d="M46 47 Q46 72 51 96 Q64 102 77 96 Q82 72 82 47" fill="none"
        stroke="{COLORS['cream']}" stroke-width="6" stroke-linejoin="round"/>
  <path d="M44 69 Q64 75 84 68 L84 101 L44 101 Z" fill="{COLORS['amber']}" clip-path="url(#beer)"/>
  <circle cx="69" cy="61" r="4" fill="{COLORS['cyan']}"/>
</svg>
"""


def monochrome_svg() -> str:
    color = COLORS["navy"]
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" role="img" aria-labelledby="title">
  <title id="title">BrewSphere monochrome mark</title>
  <g fill="none" stroke="{color}" stroke-linecap="round" stroke-linejoin="round">
    <circle cx="256" cy="256" r="220" stroke-width="30"/>
    <circle cx="256" cy="256" r="178" stroke-width="10"/>
    <path d="M205 171 C205 151 222 142 239 148 C248 137 267 137 276 148 C294 142 311 151 311 171" stroke-width="16"/>
    <path d="M207 188 C206 237 210 286 220 350 Q256 365 292 350 C302 286 306 237 305 188" stroke-width="16"/>
    <path d="M220 276 Q256 287 292 275" stroke-width="12"/>
    <circle cx="273" cy="237" r="11" stroke-width="8"/>
    <circle cx="250" cy="262" r="7" stroke-width="7"/>
  </g>
</svg>
"""


def horizontal_svg() -> str:
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 360" role="img" aria-labelledby="title">
  <title id="title">BrewSphere horizontal logo</title>
  <g transform="translate(28 28) scale(.594)">{emblem_body()}</g>
  <text x="385" y="174" fill="{COLORS['navy']}" font-family="Noto Sans,Arial,sans-serif"
        font-size="88" font-weight="700">BrewSphere</text>
  <text x="391" y="226" fill="#62869D" font-family="Noto Sans,Arial,sans-serif"
        font-size="25" font-weight="400">FERMENTATION DISPLAY</text>
  <rect x="391" y="250" width="330" height="5" rx="2.5" fill="{COLORS['amber']}"/>
</svg>
"""


def startup_svg() -> str:
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">
  <rect width="512" height="512" fill="{COLORS['deep']}"/>
  {emblem_body()}
</svg>
"""


def wordmark_startup_svg() -> str:
    return f"""<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" role="img" aria-labelledby="title">
  <title id="title">BrewSphere two-line startup wordmark</title>
  <rect width="512" height="512" fill="{COLORS['deep']}"/>
  <circle cx="256" cy="256" r="246" fill="{COLORS['navy']}"/>
  <circle cx="256" cy="256" r="246" fill="none" stroke="{COLORS['cream']}" stroke-width="6"/>
  <text x="256" y="233" text-anchor="middle" fill="{COLORS['cream']}"
        font-family="Noto Sans,Arial,sans-serif" font-size="96" font-weight="700">Brew</text>
  <text x="256" y="343" text-anchor="middle" fill="{COLORS['amber']}"
        font-family="Noto Sans,Arial,sans-serif" font-size="96" font-weight="700">Sphere</text>
</svg>
"""


def render_svg(inkscape: Path, svg: Path, png: Path, width: int, height: int) -> None:
    subprocess.run(
        [
            str(inkscape), str(svg), "--export-type=png",
            f"--export-filename={png}", f"--export-width={width}",
            f"--export-height={height}",
        ],
        check=True,
        capture_output=True,
    )


def rgb565(pixel: tuple[int, int, int]) -> int:
    red, green, blue = pixel
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def expand565(color: int) -> tuple[int, int, int]:
    red = (color >> 11) & 0x1F
    green = (color >> 5) & 0x3F
    blue = color & 0x1F
    return ((red << 3) | (red >> 2), (green << 2) | (green >> 4),
            (blue << 3) | (blue >> 2))


def quantize_rgb565(image: Image.Image) -> Image.Image:
    colors = [rgb565(pixel) for pixel in image.convert("RGB").getdata()]
    output = Image.new("RGB", image.size)
    output.putdata([expand565(color) for color in colors])
    return output


def encode_rle(image: Image.Image) -> list[int]:
    pixels = [rgb565(pixel) for pixel in image.convert("RGB").getdata()]
    words: list[int] = []
    index = 0
    while index < len(pixels):
        color = pixels[index]
        count = 1
        while index + count < len(pixels) and pixels[index + count] == color and count < 0xFFFF:
            count += 1
        words.extend((count, color))
        index += count
    return words


def cpp_values(values: list[int], width: int = 12) -> str:
    return "\n".join(
        "  " + ", ".join(f"0x{value:04X}" for value in values[index:index + width]) + ","
        for index in range(0, len(values), width)
    )


def write_startup_cpp(
    output: Path, emblem_runs: list[int], wordmark_runs: list[int]
) -> None:
    output.write_text(
        '#include "ui/brand_assets.h"\n\n#include <pgmspace.h>\n\n'
        'namespace ui::brand {\n\n'
        'const uint16_t kStartupRuns[] PROGMEM = {\n'
        + cpp_values(emblem_runs)
        + '\n};\nconst size_t kStartupRunWordCount = '
        'sizeof(kStartupRuns) / sizeof(kStartupRuns[0]);\n\n'
        'const uint16_t kWordmarkStartupRuns[] PROGMEM = {\n'
        + cpp_values(wordmark_runs)
        + '\n};\nconst size_t kWordmarkStartupRunWordCount = '
        'sizeof(kWordmarkStartupRuns) / sizeof(kWordmarkStartupRuns[0]);\n\n'
        '}  // namespace ui::brand\n',
        encoding="ascii",
    )


def create_wordmark_startup(output: Path, bold: Path) -> None:
    scale = 4
    size = 240 * scale
    image = Image.new("RGB", (size, size), COLORS["deep"])
    draw = ImageDraw.Draw(image)
    margin = 5 * scale
    draw.ellipse(
        (margin, margin, size - margin - 1, size - margin - 1),
        fill=COLORS["navy"],
    )
    draw.ellipse(
        (margin, margin, size - margin - 1, size - margin - 1),
        outline=COLORS["cream"], width=3 * scale,
    )
    font = ImageFont.truetype(str(bold), 48 * scale)
    draw.text((size // 2, 93 * scale), "Brew", font=font,
              fill=COLORS["cream"], anchor="mm")
    draw.text((size // 2, 149 * scale), "Sphere", font=font,
              fill=COLORS["amber"], anchor="mm")
    image = image.resize((240, 240), Image.Resampling.LANCZOS)
    quantize_rgb565(image).save(output)


def create_banner(emblem_png: Path, output: Path, regular: Path, bold: Path) -> None:
    image = Image.new("RGB", (1200, 360), "#F4F6F7")
    panel = (12, 20, 1188, 340)
    panel_layer = Image.new("RGB", image.size, "#06264A")
    panel_draw = ImageDraw.Draw(panel_layer)
    # Broad, low-contrast shapes add depth without introducing RGB565-style
    # gradient banding or competing with the wordmark.
    panel_draw.ellipse((900, -180, 1260, 150), fill="#0A3551")
    panel_draw.ellipse((-190, 225, 190, 540), fill="#0B3A55")
    panel_mask = Image.new("L", image.size, 0)
    ImageDraw.Draw(panel_mask).rounded_rectangle(panel, radius=16, fill=255)
    image.paste(panel_layer, (0, 0), panel_mask)
    draw = ImageDraw.Draw(image)

    emblem = Image.open(emblem_png).convert("RGBA").resize(
        (290, 290), Image.Resampling.LANCZOS
    )
    image.paste(emblem, (38, 35), emblem)

    title_font = ImageFont.truetype(str(bold), 78)
    subtitle_font = ImageFont.truetype(str(regular), 26)
    footer_font = ImageFont.truetype(str(regular), 28)
    text_x = 370
    title_y = 62
    draw.text((text_x, title_y), "Brew", font=title_font, fill=COLORS["cream"])
    sphere_x = text_x + round(draw.textlength("Brew", font=title_font))
    draw.text((sphere_x, title_y), "Sphere", font=title_font,
              fill=COLORS["amber"])
    draw.text((text_x, 166), "DAS RUNDE AUGE DEINES BRAUPROZESSES",
              font=subtitle_font, fill=COLORS["cyan"])
    draw.rounded_rectangle((text_x, 216, 1080, 221), radius=2.5,
                           fill=COLORS["cream"])
    draw.text((text_x, 236), "GÄRUNGS-MONITORING", font=footer_font,
              fill=COLORS["cyan"])
    image.save(output)


def create_horizontal_png(
    emblem_png: Path, output: Path, regular: Path, bold: Path
) -> None:
    image = Image.new("RGBA", (1200, 360), (0, 0, 0, 0))
    emblem = Image.open(emblem_png).convert("RGBA").resize(
        (304, 304), Image.Resampling.LANCZOS
    )
    image.alpha_composite(emblem, (28, 28))
    draw = ImageDraw.Draw(image)
    title_font = ImageFont.truetype(str(bold), 88)
    subtitle_font = ImageFont.truetype(str(regular), 25)
    draw.text((385, 75), "BrewSphere", font=title_font, fill=COLORS["navy"])
    draw.text((391, 205), "FERMENTATION DISPLAY", font=subtitle_font,
              fill="#62869D")
    draw.rounded_rectangle((391, 250, 721, 255), radius=2.5,
                           fill=COLORS["amber"])
    image.save(output)


def create_overview(
    emblem_png: Path, compact_png: Path, monochrome_png: Path,
    horizontal_png: Path, startup_emblem_png: Path,
    startup_wordmark_png: Path, banner_png: Path, display_png: Path,
    output: Path, regular: Path, bold: Path
) -> None:
    image = Image.new("RGB", (1200, 2080), "#E9EFF3")
    draw = ImageDraw.Draw(image)
    title_font = ImageFont.truetype(str(bold), 42)
    label_font = ImageFont.truetype(str(regular), 21)
    draw.rectangle((0, 0, 1200, 100), fill=COLORS["deep"])
    draw.text((45, 22), "BrewSphere Brand System", font=title_font,
              fill=COLORS["cream"])

    cards = [(40, 135, 400, 515), (420, 135, 780, 515),
             (800, 135, 1160, 515)]
    for box in cards:
        draw.rounded_rectangle(box, radius=18, fill="white", outline="#CBD7DF",
                               width=2)
    draw.text((66, 465), "Primary emblem", font=label_font, fill=COLORS["navy"])
    draw.text((446, 465), "Compact mark", font=label_font, fill=COLORS["navy"])
    draw.text((826, 465), "Monochrome mark", font=label_font, fill=COLORS["navy"])

    emblem = Image.open(emblem_png).convert("RGBA").resize((290, 290), Image.Resampling.LANCZOS)
    compact = Image.open(compact_png).convert("RGBA").resize((210, 210), Image.Resampling.LANCZOS)
    monochrome = Image.open(monochrome_png).convert("RGBA").resize((250, 250), Image.Resampling.LANCZOS)
    image.paste(emblem, (75, 160), emblem)
    image.paste(compact, (495, 195), compact)
    image.paste(monochrome, (855, 175), monochrome)

    draw.rounded_rectangle((40, 550, 1160, 855), radius=18, fill="white",
                           outline="#CBD7DF", width=2)
    horizontal = Image.open(horizontal_png).convert("RGBA").resize(
        (850, 255), Image.Resampling.LANCZOS
    )
    image.paste(horizontal, (175, 555), horizontal)
    draw.text((66, 815), "Horizontal logo", font=label_font,
              fill=COLORS["navy"])

    draw.rounded_rectangle((40, 890, 580, 1325), radius=18, fill="white",
                           outline="#CBD7DF", width=2)
    draw.rounded_rectangle((600, 890, 1160, 1325), radius=18, fill="white",
                           outline="#CBD7DF", width=2)
    startup_emblem = Image.open(startup_emblem_png).convert("RGB").resize(
        (300, 300), Image.Resampling.NEAREST
    )
    startup_wordmark = Image.open(startup_wordmark_png).convert("RGB").resize(
        (300, 300), Image.Resampling.NEAREST
    )
    image.paste(startup_emblem, (160, 935))
    image.paste(startup_wordmark, (730, 935))
    draw.text((66, 1265), "Startup 1 - primary emblem", font=label_font,
              fill=COLORS["navy"])
    draw.text((626, 1265), "Startup 2 - wordmark", font=label_font,
              fill=COLORS["navy"])

    draw.rounded_rectangle((40, 1360, 1160, 1695), radius=18, fill="white",
                           outline="#CBD7DF", width=2)
    draw.text((66, 1380), "README banner", font=label_font,
              fill=COLORS["navy"])
    banner = Image.open(banner_png).convert("RGB").resize(
        (900, 270), Image.Resampling.LANCZOS
    )
    image.paste(banner, (150, 1410))

    draw.rounded_rectangle((40, 1715, 1160, 2045), radius=18, fill="white",
                           outline="#CBD7DF", width=2)
    display = Image.open(display_png).convert("RGB").resize(
        (260, 260), Image.Resampling.NEAREST
    )
    image.paste(display, (470, 1730))
    draw.text((66, 1740), "Display preview - radial background", font=label_font,
              fill=COLORS["navy"])
    image.save(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--inkscape", type=Path,
                        default=Path(r"C:/Program Files/Inkscape/bin/inkscape.exe"))
    parser.add_argument(
        "--font-dir", type=Path,
        default=Path(os.environ.get("BREWSPHERE_NOTO_SANS", "X:/Noto_Sans/static")),
    )
    args = parser.parse_args()
    regular = args.font_dir / "NotoSans-Regular.ttf"
    bold = args.font_dir / "NotoSans-Bold.ttf"
    for required in (args.inkscape, regular, bold):
        if not required.is_file():
            parser.error(f"required file not found: {required}")

    BRAND_DIR.mkdir(parents=True, exist_ok=True)
    sources = {
        "brewsphere-emblem.svg": emblem_svg(),
        "brewsphere-logo-horizontal.svg": horizontal_svg(),
        "brewsphere-mark-compact.svg": compact_svg(),
        "brewsphere-mark-monochrome.svg": monochrome_svg(),
        "brewsphere-startup.svg": startup_svg(),
        "brewsphere-startup-wordmark.svg": wordmark_startup_svg(),
    }
    for name, content in sources.items():
        (BRAND_DIR / name).write_text(content, encoding="utf-8")

    emblem_png = BRAND_DIR / "brewsphere-emblem-512.png"
    render_svg(args.inkscape, BRAND_DIR / "brewsphere-emblem.svg", emblem_png, 512, 512)
    render_svg(args.inkscape, BRAND_DIR / "brewsphere-mark-compact.svg",
               BRAND_DIR / "brewsphere-mark-compact-128.png", 128, 128)
    horizontal_png = BRAND_DIR / "brewsphere-logo-horizontal-1200x360.png"
    monochrome_png = BRAND_DIR / "brewsphere-mark-monochrome-512.png"
    create_horizontal_png(emblem_png, horizontal_png, regular, bold)
    render_svg(args.inkscape, BRAND_DIR / "brewsphere-mark-monochrome.svg",
               monochrome_png, 512, 512)
    with tempfile.TemporaryDirectory() as temp_dir:
        startup_raw = Path(temp_dir) / "startup.png"
        render_svg(args.inkscape, BRAND_DIR / "brewsphere-startup.svg", startup_raw, 240, 240)
        startup = quantize_rgb565(Image.open(startup_raw))
        startup.save(BRAND_DIR / "brewsphere-startup-240.png")
        emblem_runs = encode_rle(startup)
        if sum(emblem_runs[::2]) != 240 * 240:
            raise RuntimeError("startup RLE does not decode to 240x240 pixels")
    startup_emblem_png = BRAND_DIR / "brewsphere-startup-240.png"
    startup_wordmark_png = BRAND_DIR / "brewsphere-startup-wordmark-240.png"
    create_wordmark_startup(startup_wordmark_png, bold)
    wordmark_runs = encode_rle(Image.open(startup_wordmark_png))
    if sum(wordmark_runs[::2]) != 240 * 240:
        raise RuntimeError("wordmark startup RLE does not decode to 240x240 pixels")
    write_startup_cpp(
        ROOT / "src/ui/brand_assets.cpp", emblem_runs, wordmark_runs
    )
    banner_png = BRAND_DIR / "brewsphere-readme-banner-1200x360.png"
    create_banner(emblem_png, banner_png, regular, bold)
    display_png = BRAND_DIR / "brewsphere-display-preview.png"
    if not display_png.is_file():
        raise RuntimeError(f"required display preview not found: {display_png}")
    create_overview(
        emblem_png, BRAND_DIR / "brewsphere-mark-compact-128.png",
        monochrome_png, horizontal_png, startup_emblem_png,
        startup_wordmark_png, banner_png, display_png,
        BRAND_DIR / "brewsphere-brand-overview.png", regular, bold,
    )
    print(f"Generated brand assets in {BRAND_DIR}")
    print(f"Emblem startup RLE: {len(emblem_runs) * 2:,} bytes")
    print(f"Wordmark startup RLE: {len(wordmark_runs) * 2:,} bytes")


if __name__ == "__main__":
    main()
