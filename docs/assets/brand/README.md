# BrewSphere Brand System

## Concept

The BrewSphere identity combines brewing craft with a precise circular
instrument. The glass communicates the product, while the segmented ring
connects the mark to the GC9A01 gauge interface.

## Assets

| File | Purpose |
|---|---|
| `brewsphere-emblem.svg` | Primary scalable emblem |
| `brewsphere-logo-horizontal.svg` | Logo and wordmark on light backgrounds |
| `brewsphere-mark-compact.svg` | Small icon and avatar |
| `brewsphere-mark-monochrome.svg` | Single-color technical use |
| `brewsphere-startup-240.png` | RGB565 hardware startup emblem |
| `brewsphere-startup-wordmark-240.png` | RGB565 two-line startup wordmark |
| `brewsphere-readme-banner-1200x360.png` | GitHub README banner |
| `brewsphere-brand-overview.png` | Visual overview of the logo system |

## Colors

| Role | Hex |
|---|---|
| Deep background | `#071A2A` |
| BrewSphere navy | `#0B3552` |
| Instrument blue | `#3D83DF` |
| Active cyan | `#41DEDE` |
| Warm white | `#FFF1C9` |
| Amber | `#FF9D00` |
| Deep amber | `#E87300` |

Use the emblem unchanged for brand recognition. Runtime states and warnings
belong to the interface and must not recolor the primary logo.

## Regeneration

```powershell
python scripts/build_brand_assets.py --font-dir X:/Noto_Sans/static
```

The asset output is tested with Pillow 11.1.0 and Inkscape 1.4.2. Install the
Python dependency with `pip install -r requirements-brand.txt`. Generated
firmware data is written to `src/ui/brand_assets.cpp`; normal PlatformIO builds
need neither tool. The horizontal SVG keeps editable text and therefore expects
Noto Sans on the editing system; committed PNG exports use the explicitly
provided TTF files and are reproducible.
