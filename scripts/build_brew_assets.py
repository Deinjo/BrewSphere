#!/usr/bin/env python3
"""Generate compact BrewSphere UI assets from the canonical SVG and Noto Sans."""

from __future__ import annotations

import argparse
import math
import os
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
SVG_NS = "http://www.w3.org/2000/svg"
ET.register_namespace("", SVG_NS)


def remove_element(root: ET.Element, target: ET.Element) -> None:
    for parent in root.iter():
        if target in list(parent):
            parent.remove(target)
            return


def build_static_svg(source: Path, output: Path) -> None:
    tree = ET.parse(source)
    root = tree.getroot()
    remove_ids = {"attenuationProgress", "03_scale_dots", "06_dynamic_values"}
    for element in list(root.iter()):
        if element.get("id") in remove_ids or element.tag == f"{{{SVG_NS}}}text":
            remove_element(root, element)
    tree.write(output, encoding="utf-8", xml_declaration=True)


def render_static_background(svg: Path, inkscape: Path) -> Image.Image:
    with tempfile.TemporaryDirectory() as temp_dir:
        temp = Path(temp_dir)
        stripped_svg = temp / "static.svg"
        png = temp / "static.png"
        build_static_svg(svg, stripped_svg)
        subprocess.run(
            [
                str(inkscape),
                str(stripped_svg),
                "--export-type=png",
                f"--export-filename={png}",
                "--export-width=240",
                "--export-height=240",
            ],
            check=True,
            capture_output=True,
        )
        image = Image.open(png).convert("RGBA")
        background = Image.new("RGBA", image.size, (0x0B, 0x35, 0x52, 255))
        return Image.alpha_composite(background, image).convert("RGB")


def rgb565(pixel: tuple[int, int, int]) -> int:
    red, green, blue = pixel
    return ((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3)


def encode_background(image: Image.Image) -> list[int]:
    pixels = [rgb565(pixel) for pixel in image.getdata()]
    words: list[int] = []
    index = 0
    while index < len(pixels):
        value = pixels[index]
        count = 1
        while index + count < len(pixels) and pixels[index + count] == value and count < 0xFFFF:
            count += 1
        words.extend((count, value))
        index += count
    return words


def pack_alpha(image: Image.Image) -> bytes:
    values = [(value + 8) // 17 for value in image.getdata()]
    packed = bytearray()
    for index in range(0, len(values), 2):
        high = values[index]
        low = values[index + 1] if index + 1 < len(values) else 0
        packed.append((high << 4) | low)
    return bytes(packed)


def build_font(font_path: Path, size: int, codepoints: list[int]) -> tuple[list[tuple[int, ...]], bytes]:
    font = ImageFont.truetype(str(font_path), size)
    glyphs: list[tuple[int, ...]] = []
    bitmap = bytearray()
    for codepoint in codepoints:
        character = chr(codepoint)
        left, top, right, bottom = font.getbbox(character, anchor="ls")
        width = max(0, right - left)
        height = max(0, bottom - top)
        offset = len(bitmap)
        if width and height:
            glyph_image = Image.new("L", (width, height), 0)
            ImageDraw.Draw(glyph_image).text(
                (-left, -top), character, font=font, fill=255, anchor="ls"
            )
            bitmap.extend(pack_alpha(glyph_image))
        advance_x64 = round(font.getlength(character) * 64)
        glyphs.append((codepoint, offset, width, height, left, top, advance_x64))
    return glyphs, bytes(bitmap)


def rle_alpha(image: Image.Image) -> bytes:
    values = [(value + 8) // 17 for value in image.getdata()]
    encoded = bytearray()
    index = 0
    while index < len(values):
        value = values[index]
        count = 1
        while index + count < len(values) and values[index + count] == value and count < 255:
            count += 1
        encoded.extend((count, value))
        index += count
    return bytes(encoded)


def curved_label_mask(font_path: Path, percent: int) -> Image.Image:
    scale = 4
    canvas = Image.new("L", (240 * scale, 240 * scale), 0)
    font = ImageFont.truetype(str(font_path), 14 * scale)
    text = f"Vergärgrad {percent}%"
    advances = [font.getlength(character) for character in text]

    center_x = 120.0 * scale
    center_y = (92.0 + math.sqrt(82.0**2 - 75.0**2)) * scale
    radius = 82.0 * scale
    start_angle = math.atan2(92.0 * scale - center_y, 45.0 * scale - center_x)
    end_angle = math.atan2(92.0 * scale - center_y, 195.0 * scale - center_x)
    arc_length = (end_angle - start_angle) * radius
    cursor = (arc_length - sum(advances)) / 2.0

    tile_size = 96 * scale
    tile_center = tile_size // 2
    for character, advance in zip(text, advances):
        distance = cursor + advance / 2.0
        angle = start_angle + distance / radius
        baseline_x = center_x + math.cos(angle) * radius
        baseline_y = center_y + math.sin(angle) * radius

        tile = Image.new("L", (tile_size, tile_size), 0)
        ImageDraw.Draw(tile).text(
            (tile_center - advance / 2.0, tile_center),
            character,
            font=font,
            fill=255,
            anchor="ls",
        )
        rotation = math.degrees(angle) + 90.0
        tile = tile.rotate(-rotation, resample=Image.Resampling.BICUBIC, center=(tile_center, tile_center))
        layer = Image.new("L", canvas.size, 0)
        layer.paste(tile, (round(baseline_x - tile_center), round(baseline_y - tile_center)))
        canvas = ImageChops.lighter(canvas, layer)
        cursor += advance

    canvas = canvas.resize((240, 240), Image.Resampling.LANCZOS)
    return canvas.crop((35, 25, 205, 95))


def cpp_values(values: list[int] | bytes, width: int, formatter) -> str:
    lines = []
    for index in range(0, len(values), width):
        lines.append("  " + ", ".join(formatter(value) for value in values[index:index + width]) + ",")
    return "\n".join(lines)


def write_cpp(
    output: Path,
    background_words: list[int],
    fonts: list[tuple[str, list[tuple[int, ...]], bytes]],
    mask_data: bytes,
    mask_offsets: list[int],
) -> None:
    sections = [
        '#include "ui/brew_assets.h"',
        "",
        "#include <pgmspace.h>",
        "",
        "namespace ui::assets {",
        "",
        "const uint16_t kBackgroundRuns[] PROGMEM = {",
        cpp_values(background_words, 12, lambda value: f"0x{value:04X}"),
        "};",
        "const size_t kBackgroundRunWordCount = sizeof(kBackgroundRuns) / sizeof(kBackgroundRuns[0]);",
        "",
    ]
    for name, glyphs, bitmap in fonts:
        sections.extend(
            [
                f"static const Glyph k{name}Glyphs[] PROGMEM = {{",
                "\n".join(
                    f"  {{{codepoint}, {offset}, {width}, {height}, {x_offset}, {y_offset}, {advance}}},"
                    for codepoint, offset, width, height, x_offset, y_offset, advance in glyphs
                ),
                "};",
                f"static const uint8_t k{name}Bitmap[] PROGMEM = {{",
                cpp_values(bitmap, 20, lambda value: f"0x{value:02X}"),
                "};",
                f"const Font k{name} = {{k{name}Glyphs, k{name}Bitmap, "
                f"static_cast<uint16_t>(sizeof(k{name}Glyphs) / sizeof(k{name}Glyphs[0]))}};",
                "",
            ]
        )
    sections.extend(
        [
            "const uint8_t kAttenuationMasks[] PROGMEM = {",
            cpp_values(mask_data, 20, lambda value: f"0x{value:02X}"),
            "};",
            "const uint32_t kAttenuationMaskOffsets[102] PROGMEM = {",
            cpp_values(mask_offsets, 10, str),
            "};",
            "",
            "}  // namespace ui::assets",
            "",
        ]
    )
    output.write_text("\n".join(sections), encoding="ascii")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--font-dir",
        type=Path,
        default=Path(os.environ.get("BREWSPHERE_NOTO_SANS", "X:/Noto_Sans/static")),
    )
    parser.add_argument(
        "--inkscape",
        type=Path,
        default=Path(r"C:/Program Files/Inkscape/bin/inkscape.exe"),
    )
    parser.add_argument(
        "--svg",
        type=Path,
        default=ROOT / "tools/BrewSphereMockup/brewsphere-display-corrected.svg",
    )
    parser.add_argument("--output", type=Path, default=ROOT / "src/ui/brew_assets.cpp")
    args = parser.parse_args()

    regular = args.font_dir / "NotoSans-Regular.ttf"
    bold = args.font_dir / "NotoSans-Bold.ttf"
    for required in (regular, bold, args.inkscape, args.svg):
        if not required.is_file():
            parser.error(f"required file not found: {required}")

    background = encode_background(render_static_background(args.svg, args.inkscape))
    latin = list(range(0x20, 0x100))
    font_specs = [
        ("NotoRegular6", regular, 6, sorted(set(map(ord, "ZIEL 0123456789.-P°")))),
        ("NotoRegular12", regular, 12, latin),
        ("NotoRegular15", regular, 15, sorted(set(map(ord, "SI:0123456789.-°C ")))),
        ("NotoBold13", bold, 13, latin),
        ("NotoBold35", bold, 35, sorted(set(map(ord, "0123456789.- °P")))),
    ]
    fonts = []
    for name, path, size, codepoints in font_specs:
        glyphs, bitmap = build_font(path, size, codepoints)
        fonts.append((name, glyphs, bitmap))

    mask_data = bytearray()
    mask_offsets = [0]
    for percent in range(101):
        mask_data.extend(rle_alpha(curved_label_mask(regular, percent)))
        mask_offsets.append(len(mask_data))

    write_cpp(args.output, background, fonts, bytes(mask_data), mask_offsets)
    print(f"Generated {args.output}")
    print(f"Background RLE: {len(background) * 2:,} bytes")
    print(f"Font bitmaps: {sum(len(bitmap) for _, _, bitmap in fonts):,} bytes")
    print(f"Curved labels: {len(mask_data):,} bytes")


if __name__ == "__main__":
    main()
