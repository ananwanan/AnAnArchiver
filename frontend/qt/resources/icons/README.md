# Application icon

`ananarchiver.png` is the original application icon, now used by 轻匣 (LiteArc).
The compatibility asset filenames and resource URLs remain unchanged. The icon
was generated with the built-in `image_gen` tool in built-in mode, with
`transparent_background: true`; no reference images were used. The selected
1254 × 1254 PNG is stored at its original resolution with its generated RGBA alpha
channel preserved. Platform icon containers are derived from this source.

The blue archive container and amber zipper are intended to stay recognizable at
small desktop icon sizes. The artwork contains no text or third-party branding.

## Platform assets

- `ananarchiver.png`: original RGBA image, embedded at
  `qrc:/branding/ananarchiver.png` for the window and About dialog.
- `ananarchiver.ico`: PNG-backed Windows icon frames at 16, 24, 32, 48, 64,
  128 and 256 pixels, embedded into the executable's native resources.
- `ananarchiver.icns`: macOS icon frames at 16 through 1024 pixels, including
  Retina representations, copied into the app bundle's Resources directory.

Platform exports only resize the generated artwork and preserve transparency.
The committed exports are consumed directly; building the application does not
require an image-generation or image-conversion dependency.

## Exact generation prompt

This historical prompt retains the product name used when the icon was created.

```text
Use case: logo-brand
Asset type: original AnAnArchiver application icon for Windows, Linux and macOS.
Primary request: create a distinctive, production-ready compact archive-container icon, a single blue archive box with an unmistakable simple warm amber vertical zipper/closure.
Scene/backdrop: actual transparent background, no backdrop, no tile, no floor or cast shadow outside the icon.
Subject: one compact rounded rectangular archive box/container, with a broad front face and just a small top/side facet giving restrained depth. A simple large amber zipper clasp and closure on the front, readable when reduced to 16 or 32 pixels. Prioritize a memorable strong silhouette and bold simple forms; very few details.
Style/medium: polished modern geometric app icon, mostly flat with subtle dimensional facet shading, clean edges, restrained and professional.
Composition/framing: square 1:1 canvas, centered subject occupying about 82 percent of the canvas, balanced empty transparent margin, no cropped edges.
Color palette: saturated understated blue front face, darker navy side facet, warm amber/gold closure, minimal colors and strong contrast.
Constraints: no text, no letters, no watermark; no extra objects, no clutter; entirely original design, do not resemble or copy any existing archive-app brand or icon.
Avoid: stacked books, rainbow colors, photorealistic cardboard texture, heavy gradients, glass, gloss, excessive reflections, tiny details, decorative badges, background color, checkerboard drawn into the image.
```
