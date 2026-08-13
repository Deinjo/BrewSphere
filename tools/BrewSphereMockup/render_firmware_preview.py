#!/usr/bin/env python3
"""Render a host-side preview of the generated ESP32 BrewSphere screen."""

from __future__ import annotations

import argparse
import math
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))

from scripts.build_brew_assets import (  # noqa: E402
    build_font,
    curved_label_mask,
    render_static_background,
    rgb565,
)


COLORS = {
    "background": (0x0B, 0x35, 0x52),
    "panel": (0x17, 0x3F, 0x5D),
    "blue": (0xFF, 0x9D, 0x00),
    "cyan": (0x41, 0xDE, 0xDE),
    "cream": (0xFF, 0xE3, 0x9B),
    "plato": (0xFF, 0xF1, 0xC9),
    "label": (0xE8, 0xF0, 0xF5),
    "muted": (0x9B, 0xB1, 0xC2),
    "batch_label": (0x7B, 0x96, 0xA8),
    "batch_value": (0xF0, 0xF4, 0xF6),
    "recipe": (0xD5, 0xE0, 0xE7),
}


def expand565(color: int) -> tuple[int, int, int]:
    red = (color >> 11) & 0x1F
    green = (color >> 5) & 0x3F
    blue = color & 0x1F
    return (
        (red << 3) | (red >> 2),
        (green << 2) | (green >> 4),
        (blue << 3) | (blue >> 2),
    )


def blend565(foreground: int, background: int, alpha: int) -> int:
    inverse = 15 - alpha
    red = (((foreground >> 11) & 0x1F) * alpha + ((background >> 11) & 0x1F) * inverse + 7) // 15
    green = (((foreground >> 5) & 0x3F) * alpha + ((background >> 5) & 0x3F) * inverse + 7) // 15
    blue = ((foreground & 0x1F) * alpha + (background & 0x1F) * inverse + 7) // 15
    return (red << 11) | (green << 5) | blue


class Frame:
    def __init__(self, image: Image.Image) -> None:
        self.pixels = [rgb565(pixel) for pixel in image.convert("RGB").getdata()]

    def pixel(self, x: int, y: int) -> int:
        return self.pixels[y * 240 + x]

    def set_pixel(self, x: int, y: int, color: int) -> None:
        if 0 <= x < 240 and 0 <= y < 240:
            self.pixels[y * 240 + x] = color

    def blend_pixel(self, x: int, y: int, color: int, alpha: int) -> None:
        if 0 <= x < 240 and 0 <= y < 240 and alpha:
            self.set_pixel(x, y, color if alpha == 15 else blend565(color, self.pixel(x, y), alpha))

    def image(self) -> Image.Image:
        output = Image.new("RGB", (240, 240))
        output.putdata([expand565(pixel) for pixel in self.pixels])
        return output


class RasterFont:
    def __init__(self, path: Path, size: int, codepoints: list[int]) -> None:
        glyphs, self.bitmap = build_font(path, size, codepoints)
        self.glyphs = {glyph[0]: glyph for glyph in glyphs}

    def glyph(self, character: str) -> tuple[int, ...] | None:
        return self.glyphs.get(ord(character), self.glyphs.get(ord("?")))

    def width_x64(self, text: str) -> int:
        return sum(self.glyph(character)[6] for character in text if self.glyph(character))


def draw_text(
    frame: Frame,
    font: RasterFont,
    text: str,
    x: int,
    baseline_y: int,
    foreground: int,
    background: int,
    anchor: str = "left",
) -> None:
    width_x64 = font.width_x64(text)
    cursor_x64 = x * 64
    if anchor == "middle":
        cursor_x64 -= width_x64 // 2
    elif anchor == "right":
        cursor_x64 -= width_x64

    for character in text:
        glyph = font.glyph(character)
        if glyph is None:
            continue
        _, offset, width, height, x_offset, y_offset, advance = glyph
        glyph_x = (cursor_x64 + 32) // 64 + x_offset
        glyph_y = baseline_y + y_offset
        for pixel in range(width * height):
            packed = font.bitmap[offset + pixel // 2]
            alpha = packed & 0x0F if pixel & 1 else packed >> 4
            if alpha:
                color = foreground if alpha == 15 else blend565(
                    foreground, frame.pixel(glyph_x + pixel % width,
                                            glyph_y + pixel // width), alpha
                )
                frame.set_pixel(glyph_x + pixel % width, glyph_y + pixel // width, color)
        cursor_x64 += advance


def draw_gauge(frame: Frame, attenuation: int, end_attenuation: int) -> None:
    image = frame.image()
    draw = ImageDraw.Draw(image)
    ratio = max(0, min(100, attenuation)) / 100.0
    if ratio:
        points = []
        end_angle = 145.0 + 250.0 * ratio
        for angle in range(145, math.ceil(end_angle) + 1):
            actual = min(float(angle), end_angle)
            radians = math.radians(actual)
            points.append((round(120 + math.cos(radians) * 110), round(120 + math.sin(radians) * 110)))
        blue = expand565(rgb565(COLORS["blue"]))
        draw.line(points, fill=blue, width=8, joint="curve")
        for point in (points[0], points[-1]):
            draw.ellipse((point[0] - 4, point[1] - 4, point[0] + 4, point[1] + 4), fill=blue)

    for index in range(41):
        ratio = index / 40
        radians = math.radians(145 + 250 * ratio)
        x = round(120 + math.cos(radians) * 97)
        y = round(120 + math.sin(radians) * 97)
        radius = 3 if index % 4 == 0 else 2
        color_name = "blue" if ratio < end_attenuation / 100 else "cream"
        color = expand565(rgb565(COLORS[color_name]))
        draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=color)

    frame.pixels = [rgb565(pixel) for pixel in image.getdata()]


def render_decorated_static(
    svg: Path, inkscape: Path, radial_background: bool
) -> Image.Image:
    source = svg.read_text(encoding="utf-8")
    if radial_background:
        gradient = (
            '<radialGradient id="brewBackgroundGradient" cx="50%" cy="50%" r="50%">'
            '<stop offset="0%" stop-color="#104362"/>'
            '<stop offset="100%" stop-color="#07283f"/>'
            '</radialGradient>'
        )
        source = source.replace("</defs>", gradient + "</defs>", 1)
        source = source.replace(
            'r="120" fill="#0b3552"',
            'r="120" fill="url(#brewBackgroundGradient)"',
            1,
        )
    with tempfile.TemporaryDirectory() as temp_dir:
        decorated_svg = Path(temp_dir) / "brewsphere-decorated.svg"
        decorated_svg.write_text(source, encoding="utf-8")
        return render_static_background(decorated_svg, inkscape)


def apply_hop_watermark(background: Image.Image, icon_path: Path) -> Image.Image:
    icon = Image.open(icon_path).convert("RGBA")
    # Preserve the fine original line work, but strengthen it before reducing
    # the icon to the physical display resolution.
    alpha = icon.getchannel("A").filter(ImageFilter.MaxFilter(3))
    alpha = alpha.resize((75, 100), Image.Resampling.LANCZOS)
    alpha = alpha.point(lambda value: round(value * 0.42))
    watermark = Image.new("RGBA", alpha.size, (0x1A, 0x57, 0x72, 0))
    watermark.putalpha(alpha)
    result = background.convert("RGBA")
    result.alpha_composite(watermark, (83, 64))
    return result.convert("RGB")


def render(args: argparse.Namespace) -> Image.Image:
    regular = args.font_dir / "NotoSans-Regular.ttf"
    bold = args.font_dir / "NotoSans-Bold.ttf"
    static = (
        render_decorated_static(args.svg, args.inkscape, args.radial_background)
        if args.radial_background
        else render_static_background(args.svg, args.inkscape)
    )
    if args.hop_watermark:
        static = apply_hop_watermark(static, args.hop_image)
    frame = Frame(static)
    draw_gauge(frame, args.attenuation, args.end_attenuation)

    label = curved_label_mask(regular, args.attenuation)
    cream = rgb565(COLORS["cream"])
    for position, value in enumerate(label.getdata()):
        frame.blend_pixel(35 + position % 170, 25 + position // 170, cream, (value + 8) // 17)

    latin = list(range(0x20, 0x100))
    fonts = {
        "regular6": RasterFont(regular, 6, sorted(set(map(ord, "ZIEL 0123456789.-P°")))),
        "regular12": RasterFont(regular, 12, latin),
        "regular15": RasterFont(regular, 15, sorted(set(map(ord, "SI:0123456789.-°C ")))),
        "bold13": RasterFont(bold, 13, latin),
        "bold35": RasterFont(bold, 35, sorted(set(map(ord, "0123456789.- °P")))),
    }
    color = {name: rgb565(value) for name, value in COLORS.items()}

    draw_text(frame, fonts["regular12"], "PLATO", 120, 70, color["label"], color["background"], "middle")
    draw_text(frame, fonts["bold35"], f"{args.plato:.1f} °P", 120, 105, color["plato"], color["background"], "middle")
    draw_text(frame, fonts["regular6"], f"ZIEL {args.target:.1f} °P", 120, 119, color["muted"], color["background"], "middle")
    draw_text(frame, fonts["bold13"], args.status, 120, 135, color["plato"], color["background"], "middle")

    draw_text(frame, fonts["regular15"], "S:", 42, 157, color["cyan"], color["background"])
    draw_text(frame, fonts["regular15"], f"{args.target_temp:.1f}", 87, 157, color["cyan"], color["background"], "right")
    draw_text(frame, fonts["regular15"], "°C", 105, 157, color["cyan"], color["background"], "right")
    draw_text(frame, fonts["regular15"], "I:", 135, 157, color["cyan"], color["background"])
    draw_text(frame, fonts["regular15"], f"{args.fridge_temp:.1f}", 180, 157, color["cyan"], color["background"], "right")
    draw_text(frame, fonts["regular15"], "°C", 197, 157, color["cyan"], color["background"], "right")

    batch_label = "BATCH:"
    batch_value = f" #{args.batch_number} {args.batch_name}"
    batch_width = fonts["regular12"].width_x64(batch_label) // 64
    value_width = fonts["regular12"].width_x64(batch_value) // 64
    batch_x = 120 - (batch_width + value_width) // 2
    draw_text(frame, fonts["regular12"], batch_label, batch_x, 190, color["batch_label"], color["panel"])
    draw_text(frame, fonts["regular12"], batch_value, batch_x + batch_width, 190, color["batch_value"], color["panel"])
    draw_text(frame, fonts["regular12"], args.recipe, 120, 206, color["recipe"], color["panel"], "middle")

    day_label = "TAG:"
    day_value = f" {args.day}"
    day_label_width = fonts["regular12"].width_x64(day_label) // 64
    day_value_width = fonts["regular12"].width_x64(day_value) // 64
    day_x = 120 - (day_label_width + day_value_width) // 2
    draw_text(frame, fonts["regular12"], day_label, day_x, 229, color["batch_label"], color["background"])
    draw_text(frame, fonts["regular12"], day_value, day_x + day_label_width, 229, color["recipe"], color["background"])
    return frame.image()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--font-dir", type=Path, default=Path("X:/Noto_Sans/static"))
    parser.add_argument("--inkscape", type=Path, default=Path(r"C:/Program Files/Inkscape/bin/inkscape.exe"))
    parser.add_argument("--svg", type=Path, default=Path(__file__).with_name("brewsphere-display-corrected.svg"))
    parser.add_argument("--output", type=Path, default=Path(__file__).with_name("firmware-preview.png"))
    parser.add_argument("--radial-background", action="store_true")
    parser.add_argument("--hop-watermark", action="store_true")
    parser.add_argument(
        "--hop-image", type=Path, default=Path(__file__).with_name("Hops.png")
    )
    parser.add_argument("--attenuation", type=int, default=81)
    parser.add_argument("--end-attenuation", type=int, default=84)
    parser.add_argument("--plato", type=float, default=24.2)
    parser.add_argument("--target", type=float, default=2.6)
    parser.add_argument("--status", default="GAERUNG")
    parser.add_argument("--target-temp", type=float, default=3.0)
    parser.add_argument("--fridge-temp", type=float, default=3.4)
    parser.add_argument("--batch-number", type=int, default=39)
    parser.add_argument("--batch-name", default="Sud")
    parser.add_argument("--recipe", default="Erdbeer-Woelkchen")
    parser.add_argument("--day", type=int, default=13)
    args = parser.parse_args()
    render(args).save(args.output)
    print(f"Generated {args.output}")


if __name__ == "__main__":
    main()
