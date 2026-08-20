/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ThemeDrawing.h"

namespace mozilla::widget {

/*static*/
void ThemeDrawing::FillRect(DrawTarget& aDt, const LayoutDeviceRect& aRect,
                            const sRGBColor& aColor) {
  aDt.FillRect(aRect.ToUnknownRect(), gfx::ColorPattern(ToDeviceColor(aColor)));
}

/*static*/
void ThemeDrawing::FillRect(WebRenderBackendData& aWrData,
                            const LayoutDeviceRect& aRect,
                            const sRGBColor& aColor) {
  const bool kBackfaceIsVisible = true;
  auto dest = wr::ToLayoutRect(aRect);
  aWrData.mBuilder.PushRect(dest, dest, kBackfaceIsVisible, false, false,
                            wr::ToColorF(ToDeviceColor(aColor)));
}

/*static*/
LayoutDeviceIntCoord ThemeDrawing::SnapBorderWidth(CSSCoord aCssWidth,
                                                   DPIRatio aDpiRatio) {
  return SnapBorderWidth(aCssWidth * aDpiRatio);
}

LayoutDeviceIntCoord ThemeDrawing::SnapBorderWidth(LayoutDeviceCoord aWidth) {
  if (aWidth == 0.0f) {
    return 0;
  }
  return std::max(LayoutDeviceIntCoord(1), aWidth.Truncated());
}

/*static*/
void ThemeDrawing::PaintArrow(DrawTarget& aDrawTarget,
                              const LayoutDeviceRect& aRect,
                              const float aArrowPolygonX[],
                              const float aArrowPolygonY[],
                              const float aArrowPolygonSize,
                              const int32_t aArrowNumPoints,
                              const sRGBColor aFillColor) {
  const float scale = ScaleToFillRect(aRect, aArrowPolygonSize);

  auto center = aRect.Center().ToUnknownPoint();

  RefPtr<gfx::PathBuilder> builder = aDrawTarget.CreatePathBuilder();
  gfx::Point p =
      center + gfx::Point(aArrowPolygonX[0] * scale, aArrowPolygonY[0] * scale);
  builder->MoveTo(p);
  for (int32_t i = 1; i < aArrowNumPoints; i++) {
    p = center +
        gfx::Point(aArrowPolygonX[i] * scale, aArrowPolygonY[i] * scale);
    builder->LineTo(p);
  }
  RefPtr<gfx::Path> path = builder->Finish();

  aDrawTarget.Fill(path, gfx::ColorPattern(ToDeviceColor(aFillColor)));
}

void ThemeDrawing::PaintRoundedRectWithRadius(
    WebRenderBackendData& aWrData, const LayoutDeviceRect& aRect,
    const LayoutDeviceRect& aClipRect, const sRGBColor& aBackgroundColor,
    const sRGBColor& aBorderColor, const CSSCoord& aBorderWidth,
    const CSSCoord& aRadius, const DPIRatio& aDpiRatio) {
  const bool kBackfaceIsVisible = true;
  const LayoutDeviceCoord borderWidth(SnapBorderWidth(aBorderWidth, aDpiRatio));
  const LayoutDeviceCoord radius(aRadius * aDpiRatio);
  const wr::LayoutRect dest = wr::ToLayoutRect(aRect);
  const wr::LayoutRect clip = wr::ToLayoutRect(aClipRect);

  // Push the background.
  if (aBackgroundColor.a != 0.0f) {
    auto backgroundColor = wr::ToColorF(ToDeviceColor(aBackgroundColor));
    wr::LayoutRect backgroundRect = [&] {
      LayoutDeviceRect bg = aRect;
      bg.Deflate(borderWidth);
      return wr::ToLayoutRect(bg);
    }();
    if (radius == 0.0f) {
      aWrData.mBuilder.PushRect(backgroundRect, clip, kBackfaceIsVisible, false,
                                false, backgroundColor);
    } else {
      // NOTE(emilio): This follows DisplayListBuilder::PushRoundedRect and
      // draws the rounded fill as an extra thick rounded border instead of a
      // rectangle that's clipped to a rounded clip. Refer to that method for a
      // justification. See bug 1694269.
      LayoutDeviceCoord backgroundRadius =
          std::max(0.0f, float(radius) - float(borderWidth));
      wr::BorderSide side = {backgroundColor, wr::BorderStyle::Solid};
      const wr::BorderSide sides[4] = {side, side, side, side};
      float h = backgroundRect.width() * 0.6f;
      float v = backgroundRect.height() * 0.6f;
      wr::LayoutSideOffsets widths = {v, h, v, h};
      wr::BorderRadius radii = {{backgroundRadius, backgroundRadius},
                                {backgroundRadius, backgroundRadius},
                                {backgroundRadius, backgroundRadius},
                                {backgroundRadius, backgroundRadius},
                                1.0f,
                                1.0f,
                                1.0f,
                                1.0f};
      aWrData.mBuilder.PushBorder(backgroundRect, clip, kBackfaceIsVisible,
                                  widths, {sides, 4}, radii);
    }
  }

  if (borderWidth != 0.0f && aBorderColor.a != 0.0f) {
    // Push the border.
    const auto borderColor = ToDeviceColor(aBorderColor);
    const auto side = wr::ToBorderSide(borderColor, StyleBorderStyle::Solid);
    const wr::BorderSide sides[4] = {side, side, side, side};
    const LayoutDeviceSize sideRadius(radius, radius);
    const auto widths =
        wr::ToBorderWidths(borderWidth, borderWidth, borderWidth, borderWidth);
    const auto wrRadius =
        wr::ToBorderRadius(sideRadius, sideRadius, sideRadius, sideRadius);
    aWrData.mBuilder.PushBorder(dest, clip, kBackfaceIsVisible, widths,
                                {sides, 4}, wrRadius);
  }
}

void ThemeDrawing::PaintRoundedRectWithRadius(
    DrawTarget& aDrawTarget, const LayoutDeviceRect& aRect,
    const LayoutDeviceRect& aClipRect, const sRGBColor& aBackgroundColor,
    const sRGBColor& aBorderColor, const CSSCoord& aBorderWidth,
    const CSSCoord& aRadius, const DPIRatio& aDpiRatio) {
  const LayoutDeviceCoord borderWidth(SnapBorderWidth(aBorderWidth, aDpiRatio));
  const bool needsClip = !(aRect == aClipRect);
  if (needsClip) {
    aDrawTarget.PushClipRect(aClipRect.ToUnknownRect());
  }

  LayoutDeviceRect rect(aRect);
  // Deflate the rect by half the border width, so that the middle of the
  // stroke fills exactly the area we want to fill and not more.
  rect.Deflate(borderWidth * 0.5f);

  LayoutDeviceCoord radius(aRadius * aDpiRatio - borderWidth * 0.5f);
  // Fix up the radius if it's too large with the rect we're going to paint.
  {
    LayoutDeviceCoord min = std::min(rect.width, rect.height);
    if (radius * 2.0f > min) {
      radius = min * 0.5f;
    }
  }

  Maybe<gfx::ColorPattern> backgroundPattern;
  if (aBackgroundColor.a != 0.0f) {
    backgroundPattern.emplace(ToDeviceColor(aBackgroundColor));
  }
  Maybe<gfx::ColorPattern> borderPattern;
  if (borderWidth != 0.0f && aBorderColor.a != 0.0f) {
    borderPattern.emplace(ToDeviceColor(aBorderColor));
  }

  if (borderPattern || backgroundPattern) {
    if (radius != 0.0f) {
      gfx::RectCornerRadii radii(radius, radius, radius, radius);
      RefPtr<gfx::Path> roundedRect =
          MakePathForRoundedRect(aDrawTarget, rect.ToUnknownRect(), radii);

      if (backgroundPattern) {
        aDrawTarget.Fill(roundedRect, *backgroundPattern);
      }
      if (borderPattern) {
        aDrawTarget.Stroke(roundedRect, *borderPattern,
                           gfx::StrokeOptions(borderWidth));
      }
    } else {
      if (backgroundPattern) {
        aDrawTarget.FillRect(rect.ToUnknownRect(), *backgroundPattern);
      }
      if (borderPattern) {
        aDrawTarget.StrokeRect(rect.ToUnknownRect(), *borderPattern,
                               gfx::StrokeOptions(borderWidth));
      }
    }
  }

  if (needsClip) {
    aDrawTarget.PopClip();
  }
}

}  // namespace mozilla::widget
