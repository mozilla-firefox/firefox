#!/usr/bin/env python3
"""Generates COLRv1Paint.ttf, a COLRv1 test font for paint-graph shapes that
CAhem.ttf does not cover.

Run with fontTools installed (pip install fonttools):

    python3 colrv1-paint-generate.py

Every glyph is the full em square, so a glyph renders as a solid block and the
only thing under test is how the COLRv1 paint graph fills it.

  A  PaintGlyph -> PaintSolid(foreground)
     Uses palette index 0xFFFF, so the glyph is painted in the text color.
     No other in-tree COLRv1 font depends on the text color, which makes it
     impossible to test caches or optimizations that key on it.

  B  PaintGlyph -> PaintTransform(T) -> PaintLinearGradient(GRAD)
  C  PaintGlyph -> PaintLinearGradient(T applied to GRAD)
     B is the paint-graph shape real color-emoji fonts use, where a pattern
     sits under a transform; CAhem only ever puts a pattern directly under
     PaintGlyph. C is B with the transform folded into the gradient's control
     points by hand, so B and C must rasterize identically. That makes them a
     reference pair for any change that composes the transform into the pattern
     rather than clipping and filling.

  D  PaintGlyph -> PaintTransform(T) -> PaintSolid(foreground)
     Both of the above at once: a transform over a color that depends on the
     text color.

  E  PaintGlyph -> PaintLinearGradient(GRAD)
     GRAD without the transform, so a test can assert E differs from B. Without
     that, "B matches C" would still pass if T were ignored entirely.

  F  PaintGlyph -> PaintTranslate -> PaintScale -> PaintLinearGradient(GRAD)
  G  PaintGlyph -> PaintLinearGradient(GRAD scaled, then translated)
     A chain of two transforms that do not commute, so F only matches G if they
     are composed in the right order.

  H  PaintGlyph -> PaintGlyph -> PaintLinearGradient(GRAD)
     The outer glyph's paint is not a single pattern, so it has to be used as a
     clip. Both glyphs are the em square, so H must match E.

  I  PaintGlyph -> PaintTransform(S) -> PaintRadialGradient(R)
  J  PaintGlyph -> PaintRadialGradient(S applied to R)
  K  PaintGlyph -> PaintTranslate -> PaintSweepGradient(W)
  L  PaintGlyph -> PaintSweepGradient(W translated)
     The same as B and C, for the other two gradient types.

T is a skew plus a translate rather than a scale, so that composing it in the
wrong order produces a visibly different result.
"""

from fontTools.colorLib.builder import buildCOLR, buildCPAL
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib.tables import otTables as ot

UPEM = 1000
COLOR_GLYPH_NAMES = "ABCDEFGHIJKL"
FOREGROUND = 0xFFFF

# T: x' = x + 0.5y + 100, y' = y
T = {"xx": 1.0, "yx": 0.0, "xy": 0.5, "yy": 1.0, "dx": 100.0, "dy": 0.0}


def apply_t(x, y):
    return (
        T["xx"] * x + T["xy"] * y + T["dx"],
        T["yx"] * x + T["yy"] * y + T["dy"],
    )


# Gradient control points, chosen so that T maps them all to integers: the
# COLRv1 gradient fields are int16 font units, so C can express T(GRAD) exactly.
GRAD = {"p0": (0, 0), "p1": (500, 0), "p2": (0, 500)}
GRAD_T = {k: apply_t(*v) for k, v in GRAD.items()}

# F and G: scale x by 0.5 first, then translate by (100, 0).
CHAIN_SCALE_X = 0.5
CHAIN_DX = 100
GRAD_CHAIN = {k: (x * CHAIN_SCALE_X + CHAIN_DX, y) for k, (x, y) in GRAD.items()}

# S: a uniform scale plus a translate, the only kind of transform that can be
# folded into a radial gradient's center and radius.
S_SCALE = 0.5
S = {"xx": S_SCALE, "yx": 0.0, "xy": 0.0, "yy": S_SCALE, "dx": 100.0, "dy": 200.0}
R = {"c": (500, 500), "r": 400}
R_S = {
    "c": (R["c"][0] * S_SCALE + S["dx"], R["c"][1] * S_SCALE + S["dy"]),
    "r": R["r"] * S_SCALE,
}

SWEEP_CENTER = (400, 300)
SWEEP_DX, SWEEP_DY = 100, 200

COLOR_LINE = {
    "ColorStop": [
        {"StopOffset": 0.0, "PaletteIndex": 0, "Alpha": 1.0},
        {"StopOffset": 1.0, "PaletteIndex": 1, "Alpha": 1.0},
    ],
    "Extend": "pad",
}

# Opaque and distinguishable, so a gradient misplacement is obvious.
PALETTE = [
    (1.0, 0.0, 0.0, 1.0),
    (0.0, 0.0, 1.0, 1.0),
]


def linear_gradient(points):
    (x0, y0), (x1, y1), (x2, y2) = points["p0"], points["p1"], points["p2"]
    return {
        "Format": ot.PaintFormat.PaintLinearGradient,
        "ColorLine": COLOR_LINE,
        "x0": round(x0),
        "y0": round(y0),
        "x1": round(x1),
        "y1": round(y1),
        "x2": round(x2),
        "y2": round(y2),
    }


def radial_gradient(circle):
    (x, y), r = circle["c"], circle["r"]
    return {
        "Format": ot.PaintFormat.PaintRadialGradient,
        "ColorLine": COLOR_LINE,
        "x0": round(x),
        "y0": round(y),
        "r0": 0,
        "x1": round(x),
        "y1": round(y),
        "r1": round(r),
    }


def sweep_gradient(center):
    return {
        "Format": ot.PaintFormat.PaintSweepGradient,
        "ColorLine": COLOR_LINE,
        "centerX": center[0],
        "centerY": center[1],
        "startAngle": 0,
        "endAngle": 180,
    }


def solid_foreground():
    return {
        "Format": ot.PaintFormat.PaintSolid,
        "PaletteIndex": FOREGROUND,
        "Alpha": 1.0,
    }


def transform(paint, matrix=T):
    return {
        "Format": ot.PaintFormat.PaintTransform,
        "Paint": paint,
        "Transform": matrix,
    }


def translate(paint, dx, dy):
    return {
        "Format": ot.PaintFormat.PaintTranslate,
        "Paint": paint,
        "dx": dx,
        "dy": dy,
    }


def scale(paint, scale_x, scale_y):
    return {
        "Format": ot.PaintFormat.PaintScale,
        "Paint": paint,
        "scaleX": scale_x,
        "scaleY": scale_y,
    }


def paint_glyph(paint):
    return {
        "Format": ot.PaintFormat.PaintGlyph,
        "Glyph": "square",
        "Paint": paint,
    }


def square():
    pen = TTGlyphPen(None)
    pen.moveTo((0, 0))
    pen.lineTo((UPEM, 0))
    pen.lineTo((UPEM, UPEM))
    pen.lineTo((0, UPEM))
    pen.closePath()
    return pen.glyph()


def main():
    glyph_order = [".notdef", "square", *COLOR_GLYPH_NAMES]

    fb = FontBuilder(UPEM, isTTF=True)
    fb.setupGlyphOrder(glyph_order)
    fb.setupCharacterMap({ord(c): c for c in COLOR_GLYPH_NAMES})

    # Every glyph carries the square outline, so metrics and ink bounds are
    # right even where the COLR table is ignored.
    fb.setupGlyf({name: square() for name in glyph_order})
    fb.setupHorizontalMetrics({name: (UPEM, 0) for name in glyph_order})
    fb.setupHorizontalHeader(ascent=UPEM, descent=0)
    fb.setupNameTable({"familyName": "COLRv1Paint", "styleName": "Regular"})
    fb.setupOS2(sTypoAscender=UPEM, sTypoDescender=0, usWinAscent=UPEM)
    fb.setupPost()

    color_glyphs = {
        "A": paint_glyph(solid_foreground()),
        "B": paint_glyph(transform(linear_gradient(GRAD))),
        "C": paint_glyph(linear_gradient(GRAD_T)),
        "D": paint_glyph(transform(solid_foreground())),
        "E": paint_glyph(linear_gradient(GRAD)),
        "F": paint_glyph(
            translate(scale(linear_gradient(GRAD), CHAIN_SCALE_X, 1.0), CHAIN_DX, 0)
        ),
        "G": paint_glyph(linear_gradient(GRAD_CHAIN)),
        "H": paint_glyph(paint_glyph(linear_gradient(GRAD))),
        "I": paint_glyph(transform(radial_gradient(R), S)),
        "J": paint_glyph(radial_gradient(R_S)),
        "K": paint_glyph(translate(sweep_gradient(SWEEP_CENTER), SWEEP_DX, SWEEP_DY)),
        "L": paint_glyph(
            sweep_gradient((SWEEP_CENTER[0] + SWEEP_DX, SWEEP_CENTER[1] + SWEEP_DY))
        ),
    }
    assert list(color_glyphs) == list(COLOR_GLYPH_NAMES)

    # An explicit clip box per glyph, so bounds do not depend on walking the
    # paint graph. The skew in T moves ink outside the em square.
    clip_boxes = {name: (-UPEM, -UPEM, 2 * UPEM, 2 * UPEM) for name in color_glyphs}

    fb.font["COLR"] = buildCOLR(
        color_glyphs,
        version=1,
        glyphMap=fb.font.getReverseGlyphMap(),
        clipBoxes=clip_boxes,
    )
    fb.font["CPAL"] = buildCPAL([PALETTE])

    # Pin the head timestamps, so regenerating produces a byte-identical font
    # and the checked-in binary can be verified against this script.
    fb.font["head"].created = 0
    fb.font["head"].modified = 0

    fb.save("COLRv1Paint.ttf")


if __name__ == "__main__":
    main()
