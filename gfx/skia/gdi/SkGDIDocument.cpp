/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// A document-style SkDevice which turns Skia drawing into GDI calls on a (printer) HDC.
//
// GDI can only express a subset of the Skia drawing model: aliased solid fills and strokes,
// opaque bitmaps, and text. Every page is therefore drawn multiple times, first twice into an
// SkGDIDevice:
//
//  1. An analysis pass, which classifies every draw and accumulates the region of the page whose
//     contents GDI can't reproduce (blending with existing content, isolated layers, filters...).
//  2. An emission pass, which emits the supported draws as GDI calls.
//
// Finally the unsupported region is rasterized (including any native draw that happens to overlap
// it) and blitted on top. Translucent draws that don't overlap anything else
// are flattened against the (white) page and emitted natively, like cairo does.

#include "gdi/SkGDIDocument.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCPURecorder.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkMesh.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathUtils.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRegion.h"
#include "include/core/SkShader.h"
#include "include/core/SkVertices.h"
#include "include/pathops/SkPathOps.h"
#include "include/private/base/SkFloatingPoint.h"
#include "include/private/base/SkTPin.h"
#include "src/core/SkClipStack.h"
#include "src/core/SkClipStackDevice.h"
#include "src/core/SkFontDescriptor.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkGlyph.h"
#include "src/core/SkImageInfoPriv.h"
#include "src/core/SkMatrixPriv.h"
#include "src/core/SkPathPriv.h"
#include "src/core/SkSpecialImage.h"
#include "src/core/SkStrikeSpec.h"
#include "src/core/SkTHash.h"
#include "src/ports/SkTypeface_win_dw.h"
#include "src/text/GlyphRun.h"
#include "src/utils/win/SkTScopedComPtr.h"

#include <dwrite.h>

#include <cstring>
#include <functional>
#include <optional>
#include <vector>

namespace {

// GDI coordinates are integers; keep them well within the range every driver can deal with.
constexpr SkScalar kMaxCoord = 1 << 26;
// Tolerance, in device pixels, when converting conics to quads.
constexpr SkScalar kConicTolerance = 0.25f;
// Maximum number of pixels of a single raster bitmap.
constexpr int64_t kMaxRasterPixels = 16 * 1024 * 1024;
// ExtTextOut can choke on very long glyph arrays on some drivers.
constexpr size_t kMaxGlyphsPerCall = 4096;
// Text is emitted at this many GDI units per device pixel, for sub-pixel font sizes and glyph
// positions. Same as cairo's WIN32_FONT_LOGICAL_SCALE.
constexpr SkScalar kTextLogicalScale = 32;

LONG to_gdi(SkScalar v) { return (LONG)SkScalarRoundToInt(SkTPin(v, -kMaxCoord, kMaxCoord)); }

int fill_mode(const SkPath& path) {
    return path.getFillType() == SkPathFillType::kEvenOdd ? ALTERNATE : WINDING;
}

XFORM to_xform(const SkMatrix& m) {
    XFORM x;
    x.eM11 = m.getScaleX();
    x.eM12 = m.getSkewY();
    x.eM21 = m.getSkewX();
    x.eM22 = m.getScaleY();
    x.eDx = m.getTranslateX();
    x.eDy = m.getTranslateY();
    return x;
}

// Emits the given device-space path into the current GDI path bracket.
void emit_path(HDC dc, const SkPath& path) {
    std::vector<POINT> pts;
    std::vector<BYTE> types;
    pts.reserve(path.countPoints() * 2);
    types.reserve(path.countPoints() * 2);

    auto add = [&](SkPoint p, BYTE type) {
        pts.push_back({to_gdi(p.fX), to_gdi(p.fY)});
        types.push_back(type);
    };
    auto addQuad = [&](const SkPoint q[3]) {
        add(q[0] + (q[1] - q[0]) * (2.0f / 3), PT_BEZIERTO);
        add(q[2] + (q[1] - q[2]) * (2.0f / 3), PT_BEZIERTO);
        add(q[2], PT_BEZIERTO);
    };

    for (auto [verb, p, w] : SkPathPriv::Iterate(path)) {
        switch (verb) {
            case SkPathVerb::kMove:
                add(p[0], PT_MOVETO);
                break;
            case SkPathVerb::kLine:
                add(p[1], PT_LINETO);
                break;
            case SkPathVerb::kQuad:
                addQuad(p);
                break;
            case SkPathVerb::kConic: {
                SkAutoConicToQuads quadder;
                const SkPoint* quads = quadder.computeQuads(p, *w, kConicTolerance);
                for (int i = 0; i < quadder.countQuads(); ++i) {
                    addQuad(&quads[i * 2]);
                }
                break;
            }
            case SkPathVerb::kCubic:
                add(p[1], PT_BEZIERTO);
                add(p[2], PT_BEZIERTO);
                add(p[3], PT_BEZIERTO);
                break;
            case SkPathVerb::kClose:
                if (!types.empty() && types.back() != PT_MOVETO) {
                    types.back() |= PT_CLOSEFIGURE;
                }
                break;
        }
    }
    if (!pts.empty()) {
        PolyDraw(dc, pts.data(), types.data(), (int)pts.size());
    }
}

// Returns a non-inverse path covering the same area as |path| within |bounds|.
std::optional<SkPath> uninvert_path(const SkPath& path, const SkIRect& bounds) {
    if (!path.isInverseFillType()) {
        return path;
    }
    return Op(SkPath::Rect(SkRect::Make(bounds)), path, kIntersect_SkPathOp);
}

void fill_bitmap_info(BITMAPINFO* bmi, int width, int height) {
    memset(bmi, 0, sizeof(*bmi));
    bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi->bmiHeader.biWidth = width;
    bmi->bmiHeader.biHeight = -height;  // Top-down.
    bmi->bmiHeader.biPlanes = 1;
    bmi->bmiHeader.biBitCount = 32;
    bmi->bmiHeader.biCompression = BI_RGB;
}

// Draws an (opaque) BGRA bitmap stretched to the given rect in the current logical space.
void blit_bitmap(HDC dc, const SkBitmap& bm, const SkIRect& dst, bool smooth = true) {
    BITMAPINFO bmi;
    fill_bitmap_info(&bmi, bm.width(), bm.height());
    int oldMode = SetStretchBltMode(dc, smooth ? HALFTONE : COLORONCOLOR);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, dst.x(), dst.y(), dst.width(), dst.height(), 0, 0, bm.width(), bm.height(),
                  bm.getPixels(), &bmi, DIB_RGB_COLORS, SRCCOPY);
    SetStretchBltMode(dc, oldMode);
}

// Rasterizes |draw| (in device space) for the |area| of the page on top of white, at |scale|
// times the device resolution.
bool rasterize(const SkIRect& area, SkScalar scale,
               const std::function<void(SkCanvas*)>& draw, SkBitmap* bm) {
    int64_t w = SkScalarCeilToInt(area.width() * scale);
    int64_t h = SkScalarCeilToInt(area.height() * scale);
    if (w <= 0 || h <= 0) {
        return false;
    }
    if (w * h > kMaxRasterPixels) {
        SkScalar shrink = SkScalarSqrt(SkScalar(kMaxRasterPixels) / SkScalar(w * h));
        w = std::max<int64_t>(1, SkScalarFloorToInt(w * shrink));
        h = std::max<int64_t>(1, SkScalarFloorToInt(h * shrink));
    }
    if (!bm->tryAllocPixels(SkImageInfo::Make(w, h, kBGRA_8888_SkColorType,
                                              kPremul_SkAlphaType))) {
        return false;
    }
    SkCanvas canvas(*bm);
    canvas.clear(SK_ColorWHITE);
    canvas.scale(SkScalar(w) / area.width(), SkScalar(h) / area.height());
    canvas.translate(-area.x(), -area.y());
    canvas.clipRect(SkRect::Make(area));
    draw(&canvas);
    // Make sure that nothing is left translucent (e.g. due to kClear or kSrc), since GDI ignores
    // the alpha channel.
    canvas.drawColor(SK_ColorWHITE, SkBlendMode::kDstOver);
    return true;
}

COLORREF to_colorref(SkColor4f c, bool flattenOnWhite) {
    if (flattenOnWhite) {
        c = {c.fR * c.fA + 1 - c.fA, c.fG * c.fA + 1 - c.fA, c.fB * c.fA + 1 - c.fA, 1};
    }
    SkColor sk = c.toSkColor();
    return RGB(SkColorGetR(sk), SkColorGetG(sk), SkColorGetB(sk));
}

class AutoGDIObject {
public:
    AutoGDIObject(HDC dc, HGDIOBJ obj) : fDC(dc), fObj(obj), fOld(SelectObject(dc, obj)) {}
    ~AutoGDIObject() {
        SelectObject(fDC, fOld);
        DeleteObject(fObj);
    }

private:
    HDC fDC;
    HGDIOBJ fObj;
    HGDIOBJ fOld;
};

// Whether GDI renders |lf| using the exact same font file as |face|, so that glyph ids match.
bool gdi_font_matches(const LOGFONTW& lf, IDWriteFontFace* face) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) {
        return false;
    }
    bool matches = true;
    {
        AutoGDIObject font(dc, CreateFontIndirectW(&lf));
        const UINT32 kTables[] = {DWRITE_MAKE_OPENTYPE_TAG('h', 'e', 'a', 'd'),
                                  DWRITE_MAKE_OPENTYPE_TAG('m', 'a', 'x', 'p'),
                                  DWRITE_MAKE_OPENTYPE_TAG('n', 'a', 'm', 'e')};
        for (UINT32 tag : kTables) {
            const void* data;
            UINT32 size;
            void* context;
            BOOL exists;
            if (FAILED(face->TryGetFontTable(tag, &data, &size, &context, &exists)) || !exists) {
                matches = false;
                break;
            }
            std::vector<uint8_t> gdiData(size);
            matches = GetFontData(dc, tag, 0, nullptr, 0) == size &&
                      GetFontData(dc, tag, 0, gdiData.data(), size) == size &&
                      !memcmp(gdiData.data(), data, size);
            face->ReleaseFontTable(context);
            if (!matches) {
                break;
            }
        }
    }
    DeleteDC(dc);
    return matches;
}

const DWriteFontTypeface* as_dwrite_typeface(const SkTypeface& typeface) {
    SkFontDescriptor desc;
    bool serialize = false;
    typeface.getFontDescriptor(&desc, &serialize);
    if (desc.getFactoryId() != DWriteFontTypeface::FactoryId) {
        return nullptr;
    }
    // A LOGFONT can't express a particular instance of a variable font.
    if (typeface.getVariationDesignPosition({}) > 0) {
        return nullptr;
    }
    return static_cast<const DWriteFontTypeface*>(&typeface);
}

std::optional<LOGFONTW> compute_logfont(const DWriteFontTypeface& dw) {
    SkTScopedComPtr<IDWriteGdiInterop> interop;
    LOGFONTW lf;
    if (FAILED(dw.fFactory->GetGdiInterop(&interop)) ||
        FAILED(interop->ConvertFontFaceToLOGFONT(dw.fDWriteFontFace.get(), &lf)) ||
        !gdi_font_matches(lf, dw.fDWriteFontFace.get())) {
        return {};
    }
    return lf;
}

bool is_image_opaque(const SkImage* image) {
    if (image->isOpaque()) {
        return true;
    }
    SkPixmap pm;
    if (image->peekPixels(&pm)) {
        return pm.computeIsOpaque();
    }
    SkBitmap tmp;
    return tmp.tryAllocPixels(image->imageInfo().makeColorType(kBGRA_8888_SkColorType)
                                      .makeAlphaType(kPremul_SkAlphaType)) &&
           image->readPixels(nullptr, tmp.pixmap(), 0, 0) && tmp.pixmap().computeIsOpaque();
}

// Caches that persist across pages.
class DocumentCaches {
public:
    const LOGFONTW* logFontFor(const SkTypeface& typeface) {
        IDWriteFontFace** face = fTypefaceFaces.find(typeface.uniqueID());
        if (!face) {
            const DWriteFontTypeface* dw = as_dwrite_typeface(typeface);
            face = fTypefaceFaces.set(typeface.uniqueID(),
                                      dw ? dw->fDWriteFontFace.get() : nullptr);
            // Typefaces get re-created every time a page is drawn, but font faces are generally
            // shared.
            if (dw && !fLogFonts.find(*face)) {
                fLogFonts.set(*face, {SkTScopedComPtr<IDWriteFontFace>(SkRefComPtr(*face)),
                                      compute_logfont(*dw)});
            }
        }
        if (!*face) {
            return nullptr;
        }
        const LogFontEntry* entry = fLogFonts.find(*face);
        return entry->fLogFont.has_value() ? &entry->fLogFont.value() : nullptr;
    }

private:
    struct LogFontEntry {
        // Keeps the key alive.
        SkTScopedComPtr<IDWriteFontFace> fFace;
        std::optional<LOGFONTW> fLogFont;
    };
    skia_private::THashMap<SkTypefaceID, IDWriteFontFace*> fTypefaceFaces;
    skia_private::THashMap<IDWriteFontFace*, LogFontEntry> fLogFonts;
};

// Conservative coarse coverage of what has been drawn on the page so far.
class CoverageGrid {
public:
    explicit CoverageGrid(const SkIRect& bounds)
            : fBounds(bounds)
            , fCols((bounds.width() + kCell - 1) / kCell)
            , fRows((bounds.height() + kCell - 1) / kCell)
            , fCells(size_t(fCols) * fRows, false) {}

    bool intersects(const SkIRect& r) const {
        bool found = false;
        this->forEachCell(r, [&](size_t i) { found = found || fCells[i]; });
        return found;
    }

    void mark(const SkIRect& r) {
        this->forEachCell(r, [&](size_t i) { fCells[i] = true; });
    }

private:
    static constexpr int kCell = 32;

    template <typename Fn>
    void forEachCell(SkIRect r, Fn&& fn) const {
        if (!r.intersect(fBounds)) {
            return;
        }
        int x0 = (r.fLeft - fBounds.fLeft) / kCell, x1 = (r.fRight - 1 - fBounds.fLeft) / kCell;
        int y0 = (r.fTop - fBounds.fTop) / kCell, y1 = (r.fBottom - 1 - fBounds.fTop) / kCell;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                fn(size_t(y) * fCols + x);
            }
        }
    }

    SkIRect fBounds;
    int fCols;
    int fRows;
    std::vector<bool> fCells;
};

enum class Support {
    kNative,
    // Supported if nothing is drawn below, by flattening the draw against the white page.
    kNeedsFlattening,
    kUnsupported,
};

enum class Decision {
    kSkip,
    kEmit,
    kEmitFlattened,
};

// The effective clip of a device, as a list of device-space paths to intersect.
struct ClipInfo {
    std::vector<SkPath> fPaths;
    bool fHasShader = false;
    bool fEmpty = false;
    uint32_t fID = 0;
};

struct PageState {
    PageState(const SkIRect& bounds, SkScalar rasterScale, DocumentCaches* caches)
            : fBounds(bounds), fRasterScale(rasterScale), fCaches(caches), fDrawn(bounds) {}

    // Null during the analysis pass.
    HDC fDC = nullptr;
    const SkIRect fBounds;
    const SkScalar fRasterScale;
    DocumentCaches* const fCaches;
    // During the emission pass, the region that will be rasterized, computed by the analysis.
    const SkRegion* fFallbackFromAnalysis = nullptr;

    CoverageGrid fDrawn;
    SkRegion fFallback;
    uint32_t fNextClipID = 1;
    uint32_t fAppliedClipID = 0;

    Decision decide(const SkIRect& bounds, Support support) {
        Decision decision = Decision::kSkip;
        if (support == Support::kNative) {
            decision = Decision::kEmit;
        } else if (support == Support::kNeedsFlattening && !fDrawn.intersects(bounds)) {
            decision = Decision::kEmitFlattened;
        } else {
            fFallback.op(bounds, SkRegion::kUnion_Op);
        }
        fDrawn.mark(bounds);
        if (!fDC || (fFallbackFromAnalysis && fFallbackFromAnalysis->contains(bounds))) {
            return Decision::kSkip;
        }
        return decision;
    }

    void applyClip(const ClipInfo& clip) {
        if (clip.fID == fAppliedClipID) {
            return;
        }
        // The page setup did a SaveDC() right before any clipping happened.
        RestoreDC(fDC, -1);
        SaveDC(fDC);
        for (const SkPath& path : clip.fPaths) {
            SkRect r;
            if (path.isEmpty()) {
                IntersectClipRect(fDC, 0, 0, 0, 0);
            } else if (path.isRect(&r)) {
                SkIRect ir = r.round();
                IntersectClipRect(fDC, ir.fLeft, ir.fTop, ir.fRight, ir.fBottom);
            } else {
                BeginPath(fDC);
                emit_path(fDC, path);
                EndPath(fDC);
                SetPolyFillMode(fDC, fill_mode(path));
                SelectClipPath(fDC, RGN_AND);
            }
        }
        fAppliedClipID = clip.fID;
    }
};

struct Geometry {
    SkPath fPath;  // In device space.
    bool fHairline = false;
};

class SkGDIDevice final : public SkClipStackDevice {
public:
    enum class Kind {
        // Draws go straight to the page (the root device, and pass-through layers).
        kDirect,
        // A layer which needs compositing that GDI can't do: its contents are rasterized.
        kIsolated,
    };

    SkGDIDevice(PageState* page, SkISize size, Kind kind, SkGDIDevice* parent)
            : SkClipStackDevice(SkImageInfo::MakeUnknown(size.width(), size.height()),
                                SkSurfaceProps())
            , fPage(page)
            , fKind(kind)
            , fHasParent(parent) {
        if (parent) {
            fParentClip = parent->clipInfo();
            fParentClipBounds = parent->globalClipBounds();
        } else {
            fParentClipBounds = page->fBounds;
        }
    }

    SkRecorder* baseRecorder() const override { return skcpu::Recorder::TODO(); }

    sk_sp<SkDevice> createDevice(const CreateInfo& info, const SkPaint* paint) override {
        bool passThrough = fKind == Kind::kDirect &&
                           (!paint || (paint->isSrcOver() && paint->getAlpha() == 0xFF &&
                                       !paint->getColorFilter() && !paint->getImageFilter() &&
                                       !paint->getMaskFilter() && !paint->getShader()));
        if (!passThrough && paint &&
            (paint->getImageFilter() || paint->getColorFilter() || !paint->asBlendMode())) {
            // The output of the layer is not bounded by its contents.
            this->markUnsupported(this->globalClipBounds());
        }
        return sk_make_sp<SkGDIDevice>(fPage, info.fInfo.dimensions(),
                                       passThrough ? Kind::kDirect : Kind::kIsolated, this);
    }

    void drawDevice(SkDevice* device, const SkSamplingOptions&, const SkPaint&) override {
        // createDevice() only ever creates SkGDIDevices.
        auto* layer = static_cast<SkGDIDevice*>(device);
        if (layer->fKind == Kind::kIsolated) {
            this->markUnsupported(layer->fIsolatedBounds);
        }
    }

    void drawSpecial(SkSpecialImage* image, const SkMatrix& localToDevice,
                     const SkSamplingOptions&, const SkPaint&,
                     SkCanvas::SrcRectConstraint) override {
        this->markUnsupported(this->toGlobalBounds(
                localToDevice.mapRect(SkRect::Make(image->dimensions()))));
    }

    void drawCoverageMask(const SkSpecialImage* mask, const SkMatrix& maskToDevice,
                          const SkSamplingOptions&, const SkPaint&) override {
        this->markUnsupported(this->toGlobalBounds(
                maskToDevice.mapRect(SkRect::Make(mask->dimensions()))));
    }

    void drawPaint(const SkPaint& paint) override {
        SkIRect bounds = this->globalClipBounds();
        Decision decision = this->decide(bounds, this->classifyPaint(paint));
        if (decision != Decision::kSkip) {
            this->emitGeometry({SkPath::Rect(SkRect::Make(bounds)), false}, paint,
                               decision == Decision::kEmitFlattened, bounds);
        }
    }

    void drawPoints(SkCanvas::PointMode mode, SkSpan<const SkPoint> pts,
                    const SkPaint& paint) override {
        SkPathBuilder builder;
        switch (mode) {
            case SkCanvas::kPolygon_PointMode:
                builder.addPolygon(pts, false);
                break;
            case SkCanvas::kLines_PointMode:
                for (size_t i = 0; i + 1 < pts.size(); i += 2) {
                    builder.moveTo(pts[i]);
                    builder.lineTo(pts[i + 1]);
                }
                break;
            case SkCanvas::kPoints_PointMode:
                for (SkPoint p : pts) {
                    builder.moveTo(p);
                    builder.lineTo(p);
                }
                break;
        }
        SkPaint strokePaint(paint);
        strokePaint.setStyle(SkPaint::kStroke_Style);
        if (mode == SkCanvas::kPoints_PointMode &&
            strokePaint.getStrokeCap() == SkPaint::kButt_Cap) {
            strokePaint.setStrokeCap(SkPaint::kSquare_Cap);
        }
        this->drawPath(builder.detach(), strokePaint);
    }

    void drawRect(const SkRect& r, const SkPaint& paint) override {
        this->drawPath(SkPath::Rect(r), paint);
    }

    void drawOval(const SkRect& oval, const SkPaint& paint) override {
        this->drawPath(SkPath::Oval(oval), paint);
    }

    void drawRRect(const SkRRect& rr, const SkPaint& paint) override {
        this->drawPath(SkPath::RRect(rr), paint);
    }

    void drawPath(const SkPath& path, const SkPaint& paint) override {
        std::optional<Geometry> geometry = this->makeGeometry(path, paint);
        if (!geometry) {
            this->markUnsupported(this->globalClipBounds());
            return;
        }
        SkRect devBounds = geometry->fPath.getBounds();
        if (geometry->fHairline) {
            devBounds.outset(1, 1);
        }
        SkIRect bounds = devBounds.roundOut().makeOutset(1, 1);
        Support support = this->classifyPaint(paint);
        if (geometry->fHairline && paint.getShader()) {
            support = Support::kUnsupported;
        }
        Decision decision = this->decide(bounds, support);
        if (decision != Decision::kSkip) {
            this->emitGeometry(*geometry, paint, decision == Decision::kEmitFlattened, bounds);
        }
    }

    void drawImageRect(const SkImage* image, const SkRect* src, const SkRect& dst,
                       const SkSamplingOptions& sampling, const SkPaint& paint,
                       SkCanvas::SrcRectConstraint) override {
        SkRect srcRect = src ? *src : SkRect::Make(image->bounds());
        SkMatrix srcToDst = SkMatrix::RectToRect(srcRect, dst);
        if (!srcRect.intersect(SkRect::Make(image->bounds()))) {
            return;
        }
        SkRect dstRect = srcToDst.mapRect(srcRect);
        SkMatrix ctm = this->globalCTM();
        SkIRect bounds = ctm.mapRect(dstRect).roundOut().makeOutset(1, 1);
        Decision decision = this->decide(bounds, this->classifyImage(image, paint, ctm));
        if (decision != Decision::kSkip) {
            this->emitImage(image, srcRect, dstRect, sampling, paint,
                            decision == Decision::kEmitFlattened);
        }
    }

    void drawVertices(const SkVertices* vertices, sk_sp<SkBlender>, const SkPaint&,
                      bool) override {
        this->markUnsupported(this->toGlobalBounds(vertices->bounds()));
    }

    void drawMesh(const SkMesh& mesh, sk_sp<SkBlender>, const SkPaint&) override {
        this->markUnsupported(this->toGlobalBounds(mesh.bounds()));
    }

private:
    void onDrawGlyphRunList(SkCanvas*, const sktext::GlyphRunList& list,
                            const SkPaint& paint) override {
        const SkMatrix ctm = this->globalCTM();
        const Support paintSupport = this->classifyPaint(paint);
        for (const sktext::GlyphRun& run : list) {
            if (run.glyphsIDs().empty()) {
                continue;
            }
            SkBulkGlyphMetrics metrics(SkStrikeSpec::MakeWithNoDevice(run.font(), &paint));
            SkSpan<const SkGlyph*> glyphs = metrics.glyphs(run.glyphsIDs());
            bool hasColorGlyphs = false;
            SkRect localBounds = SkRect::MakeEmpty();
            for (size_t i = 0; i < glyphs.size(); ++i) {
                hasColorGlyphs |= glyphs[i]->isColor();
                if (!glyphs[i]->isEmpty()) {
                    localBounds.join(glyphs[i]->rect().makeOffset(run.positions()[i] +
                                                                  list.origin()));
                }
            }
            if (localBounds.isEmpty()) {
                continue;
            }
            SkRect storage;
            if (paint.canComputeFastBounds()) {
                localBounds = paint.computeFastBounds(localBounds, &storage);
            }
            SkIRect bounds = ctm.mapRect(localBounds).roundOut().makeOutset(1, 1);
            Decision decision =
                    this->decide(bounds, hasColorGlyphs ? Support::kUnsupported : paintSupport);
            if (decision == Decision::kSkip) {
                continue;
            }
            bool flatten = decision == Decision::kEmitFlattened;
            if (this->emitGlyphRunAsText(run, list.origin(), paint, flatten)) {
                continue;
            }
            struct Rec {
                SkPathBuilder fBuilder;
                SkPoint fOrigin;
                const SkPoint* fPos;
            } rec = {{}, list.origin(), run.positions().data()};
            run.font().getPaths(
                    run.glyphsIDs(),
                    [](const SkPath* path, const SkMatrix& mx, void* ctx) {
                        auto* rec = static_cast<Rec*>(ctx);
                        if (path) {
                            SkMatrix m = mx;
                            m.postTranslate(rec->fPos->fX + rec->fOrigin.fX,
                                            rec->fPos->fY + rec->fOrigin.fY);
                            rec->fBuilder.addPath(*path, m);
                        }
                        rec->fPos++;
                    },
                    &rec);
            if (std::optional<Geometry> geometry = this->makeGeometry(rec.fBuilder.detach(),
                                                                      paint)) {
                this->emitGeometry(*geometry, paint, flatten, bounds);
            }
        }
    }

    // -- Coordinate spaces. Emission always happens in global (page) device space.

    SkMatrix globalCTM() const {
        return SkMatrix::Concat(this->deviceToGlobal().asM33(), this->localToDevice());
    }

    SkIRect toGlobalBounds(const SkRect& localBounds) const {
        return this->globalCTM().mapRect(localBounds).roundOut().makeOutset(1, 1);
    }

    SkIRect globalClipBounds() const {
        SkIRect bounds = SkMatrixPriv::MapRect(this->deviceToGlobal(),
                                               SkRect::Make(this->devClipBounds()))
                                 .roundOut();
        if (!bounds.intersect(fParentClipBounds)) {
            return SkIRect::MakeEmpty();
        }
        return bounds;
    }

    const ClipInfo& clipInfo() {
        uint32_t genID = this->cs().getTopmostGenID();
        if (fClipInfo.fID && genID == fClipInfoGenID) {
            return fClipInfo;
        }
        fClipInfo = fParentClip;
        if (fHasParent) {
            fClipInfo.fPaths.push_back(SkPath::Rect(SkRect::Make(this->getGlobalBounds())));
        }
        const size_t ownStart = fClipInfo.fPaths.size();
        const SkMatrix toGlobal = this->deviceToGlobal().asM33();
        SkClipStack::B2TIter iter(this->cs());
        while (const SkClipStack::Element* element = iter.next()) {
            using Type = SkClipStack::Element::DeviceSpaceType;
            if (element->isReplaceOp()) {
                fClipInfo.fPaths.resize(ownStart);
            }
            if (element->getDeviceSpaceType() == Type::kEmpty) {
                fClipInfo.fEmpty = true;
                continue;
            }
            if (element->getDeviceSpaceType() == Type::kShader) {
                fClipInfo.fHasShader = true;
                continue;
            }
            SkPath path = element->asDeviceSpacePath().makeTransform(toGlobal);
            if (element->getOp() == SkClipOp::kDifference) {
                path.toggleInverseFillType();
            }
            std::optional<SkPath> uninverted = uninvert_path(path, fPage->fBounds);
            if (!uninverted) {
                fClipInfo.fHasShader = true;  // Treat as unsupported.
                continue;
            }
            fClipInfo.fPaths.push_back(std::move(*uninverted));
        }
        fClipInfo.fID = fPage->fNextClipID++;
        fClipInfoGenID = genID;
        return fClipInfo;
    }

    // -- Classification. This must give the same result in the analysis and emission passes.

    Decision decide(SkIRect bounds, Support support) {
        if (!bounds.intersect(this->globalClipBounds())) {
            return Decision::kSkip;
        }
        if (fKind == Kind::kIsolated) {
            fIsolatedBounds.join(bounds);
            return Decision::kSkip;
        }
        const ClipInfo& clip = this->clipInfo();
        if (clip.fEmpty) {
            return Decision::kSkip;
        }
        if (clip.fHasShader) {
            support = Support::kUnsupported;
        }
        return fPage->decide(bounds, support);
    }

    void markUnsupported(const SkIRect& bounds) {
        (void)this->decide(bounds, Support::kUnsupported);
    }

    static SkColor4f PaintColor(const SkPaint& paint) {
        if (paint.asBlendMode() == SkBlendMode::kClear) {
            return SkColors::kTransparent;
        }
        SkColor4f color = paint.getColor4f();
        if (SkColorFilter* filter = paint.getColorFilter()) {
            color = filter->filterColor4f(color, nullptr, nullptr);
        }
        return color;
    }

    Support classifyPaint(const SkPaint& paint) const {
        if (paint.getMaskFilter() || paint.getImageFilter()) {
            return Support::kUnsupported;
        }
        std::optional<SkBlendMode> mode = paint.asBlendMode();
        if (!mode) {
            return Support::kUnsupported;
        }
        switch (*mode) {
            case SkBlendMode::kSrcOver:
            case SkBlendMode::kSrc:
                break;
            case SkBlendMode::kClear:
                return Support::kNeedsFlattening;
            default:
                return Support::kUnsupported;
        }
        if (const SkShader* shader = paint.getShader()) {
            // Non-solid paints are rasterized locally and clipped to the geometry.
            const SkColorFilter* filter = paint.getColorFilter();
            bool opaque = shader->isOpaque() && paint.getAlpha() == 0xFF &&
                          (!filter || filter->isAlphaUnchanged());
            return opaque ? Support::kNative : Support::kNeedsFlattening;
        }
        return PaintColor(paint).isOpaque() ? Support::kNative : Support::kNeedsFlattening;
    }

    Support classifyImage(const SkImage* image, const SkPaint& paint, const SkMatrix& ctm) const {
        if (paint.getMaskFilter() || paint.getImageFilter() || paint.getColorFilter() ||
            ctm.hasPerspective() || SkColorTypeIsAlphaOnly(image->colorType())) {
            return Support::kUnsupported;
        }
        std::optional<SkBlendMode> mode = paint.asBlendMode();
        if (mode != SkBlendMode::kSrcOver && mode != SkBlendMode::kSrc) {
            return Support::kUnsupported;
        }
        if (paint.getAlpha() == 0xFF && is_image_opaque(image)) {
            return Support::kNative;
        }
        return Support::kNeedsFlattening;
    }

    // Converts a local-space path into the device-space area that it covers.
    std::optional<Geometry> makeGeometry(const SkPath& path, const SkPaint& paint) const {
        const SkMatrix ctm = this->globalCTM();
        Geometry geometry;
        if (paint.getPathEffect() || paint.getStyle() != SkPaint::kFill_Style) {
            SkPathBuilder builder;
            geometry.fHairline =
                    !skpathutils::FillPathWithPaint(path, paint, &builder, nullptr, ctm);
            geometry.fPath = builder.detach(&ctm);
        } else {
            geometry.fPath = path.makeTransform(ctm);
        }
        std::optional<SkPath> uninverted =
                uninvert_path(geometry.fPath, this->globalClipBounds());
        if (!uninverted) {
            return {};
        }
        geometry.fPath = std::move(*uninverted);
        return geometry;
    }

    // -- Emission.

    void emitGeometry(const Geometry& geometry, const SkPaint& paint, bool flatten,
                      const SkIRect& bounds) {
        HDC dc = fPage->fDC;
        fPage->applyClip(this->clipInfo());

        if (geometry.fHairline) {
            LOGBRUSH brush = {BS_SOLID, to_colorref(PaintColor(paint), flatten), 0};
            AutoGDIObject pen(dc, ExtCreatePen(PS_COSMETIC | PS_SOLID, 1, &brush, 0, nullptr));
            BeginPath(dc);
            emit_path(dc, geometry.fPath);
            EndPath(dc);
            StrokePath(dc);
            return;
        }

        if (!paint.getShader()) {
            HBRUSH brush = CreateSolidBrush(to_colorref(PaintColor(paint), flatten));
            SkRect rect;
            if (geometry.fPath.isRect(&rect)) {
                SkIRect ir = rect.round();
                RECT r = {ir.fLeft, ir.fTop, ir.fRight, ir.fBottom};
                FillRect(dc, &r, brush);
                DeleteObject(brush);
                return;
            }
            AutoGDIObject selected(dc, brush);
            BeginPath(dc);
            emit_path(dc, geometry.fPath);
            EndPath(dc);
            SetPolyFillMode(dc, fill_mode(geometry.fPath));
            FillPath(dc);
            return;
        }

        // Rasterize the shader over the bounds, and clip that to the geometry.
        SkIRect area = bounds;
        if (!area.intersect(this->globalClipBounds())) {
            return;
        }
        const SkMatrix ctm = this->globalCTM();
        SkBitmap bm;
        bool ok = rasterize(area, fPage->fRasterScale,
                            [&](SkCanvas* canvas) {
                                canvas->concat(ctm);
                                SkPaint fill(paint);
                                fill.setStyle(SkPaint::kFill_Style);
                                fill.setPathEffect(nullptr);
                                canvas->drawPaint(fill);
                            },
                            &bm);
        if (!ok) {
            return;
        }
        SaveDC(dc);
        BeginPath(dc);
        emit_path(dc, geometry.fPath);
        EndPath(dc);
        SetPolyFillMode(dc, fill_mode(geometry.fPath));
        SelectClipPath(dc, RGN_AND);
        blit_bitmap(dc, bm, area);
        RestoreDC(dc, -1);
    }

    void emitImage(const SkImage* image, const SkRect& src, const SkRect& dst,
                   const SkSamplingOptions& sampling, const SkPaint& paint, bool flatten) {
        SkIRect isrc = src.roundOut();
        if (!isrc.intersect(image->bounds())) {
            return;
        }
        SkBitmap bm;
        if (!bm.tryAllocPixels(SkImageInfo::Make(isrc.width(), isrc.height(),
                                                 kBGRA_8888_SkColorType, kPremul_SkAlphaType))) {
            return;
        }
        if (flatten) {
            SkCanvas canvas(bm);
            canvas.clear(SK_ColorWHITE);
            SkPaint alpha;
            alpha.setAlphaf(paint.getAlphaf());
            canvas.drawImage(image, -isrc.x(), -isrc.y(), SkSamplingOptions(), &alpha);
        } else if (!image->readPixels(nullptr, bm.pixmap(), isrc.x(), isrc.y())) {
            return;
        }

        HDC dc = fPage->fDC;
        const SkMatrix ctm = this->globalCTM();
        fPage->applyClip(this->clipInfo());
        SaveDC(dc);
        if (SkRect::Make(isrc) != src) {
            // We had to round out the source rect, clip to the actual destination.
            BeginPath(dc);
            emit_path(dc, SkPath::Rect(dst).makeTransform(ctm));
            EndPath(dc);
            SelectClipPath(dc, RGN_AND);
        }
        SkMatrix bitmapToDevice = SkMatrix::Concat(ctm, SkMatrix::RectToRect(src, dst));
        bitmapToDevice.preTranslate(isrc.x(), isrc.y());
        XFORM xform = to_xform(bitmapToDevice);
        if (SetWorldTransform(dc, &xform)) {
            bool smooth = sampling.useCubic || sampling.filter != SkFilterMode::kNearest;
            blit_bitmap(dc, bm, SkIRect::MakeWH(bm.width(), bm.height()), smooth);
        }
        RestoreDC(dc, -1);
    }

    // Emits the glyph run with ExtTextOut, if the font and paint allow it. Returns false if
    // nothing was emitted and the run needs to be drawn some other way.
    bool emitGlyphRunAsText(const sktext::GlyphRun& run, SkPoint origin, const SkPaint& paint,
                            bool flatten) {
        const SkFont& font = run.font();
        if (paint.getShader() || paint.getPathEffect() || font.isEmbolden() ||
            font.getScaleX() == 0 ||
            (paint.getStyle() != SkPaint::kFill_Style && paint.getStrokeWidth() > 0)) {
            return false;
        }
        const SkMatrix ctm = this->globalCTM();
        if (ctm.hasPerspective()) {
            return false;
        }
        const LOGFONTW* baseLogFont = fPage->fCaches->logFontFor(*font.getTypeface());
        if (!baseLogFont) {
            return false;
        }

        // Glyph space is local space without the font's horizontal scale and skew. Emit the
        // text in glyph space scaled up by |scale|, so that integer GDI coordinates and font
        // heights are roughly in units of 1/kTextLogicalScale device pixels, and let the world
        // transform do the rest.
        const SkMatrix fontMatrix =
                SkMatrix::MakeAll(font.getScaleX(), font.getSkewX(), 0, 0, 1, 0, 0, 0, 1);
        const SkMatrix glyphToDevice = SkMatrix::Concat(ctm, fontMatrix);
        const SkScalar scale = SkScalarSqrt(SkScalarAbs(
                glyphToDevice.getScaleX() * glyphToDevice.getScaleY() -
                glyphToDevice.getSkewX() * glyphToDevice.getSkewY())) * kTextLogicalScale;
        SkMatrix localToGdi;
        if (!SkIsFinite(scale) || scale <= 0 || !fontMatrix.invert(&localToGdi)) {
            return false;
        }
        localToGdi.postScale(scale, scale);
        const LONG height = SkScalarRoundToInt(font.getSize() * scale);
        if (height <= 0) {
            return false;
        }
        SkMatrix gdiToDevice = glyphToDevice;
        gdiToDevice.preScale(1 / scale, 1 / scale);

        LOGFONTW lf = *baseLogFont;
        lf.lfHeight = -height;
        lf.lfWidth = 0;
        lf.lfEscapement = 0;
        lf.lfOrientation = 0;

        HDC dc = fPage->fDC;
        fPage->applyClip(this->clipInfo());
        SaveDC(dc);
        XFORM xform = to_xform(gdiToDevice);
        if (!SetWorldTransform(dc, &xform)) {
            RestoreDC(dc, -1);
            return false;
        }
        {
            AutoGDIObject selected(dc, CreateFontIndirectW(&lf));
            SetTextAlign(dc, TA_BASELINE | TA_LEFT | TA_NOUPDATECP);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, to_colorref(PaintColor(paint), flatten));

            SkSpan<const SkGlyphID> ids = run.glyphsIDs();
            SkSpan<const SkPoint> positions = run.positions();
            std::vector<POINT> gdiPos(ids.size());
            for (size_t i = 0; i < ids.size(); ++i) {
                SkPoint p = localToGdi.mapPoint(positions[i] + origin);
                gdiPos[i] = {to_gdi(p.fX), to_gdi(p.fY)};
            }
            std::vector<INT> dxy;
            for (size_t start = 0; start < ids.size(); start += kMaxGlyphsPerCall) {
                size_t count = std::min(kMaxGlyphsPerCall, ids.size() - start);
                dxy.assign(count * 2, 0);
                for (size_t i = 0; i + 1 < count; ++i) {
                    dxy[i * 2] = gdiPos[start + i + 1].x - gdiPos[start + i].x;
                    // ETO_PDY offsets go up.
                    dxy[i * 2 + 1] = gdiPos[start + i].y - gdiPos[start + i + 1].y;
                }
                static_assert(sizeof(SkGlyphID) == sizeof(WORD));
                ExtTextOutW(dc, gdiPos[start].x, gdiPos[start].y, ETO_GLYPH_INDEX | ETO_PDY,
                            nullptr, reinterpret_cast<LPCWSTR>(ids.data() + start), (UINT)count,
                            dxy.data());
            }
        }
        RestoreDC(dc, -1);
        return true;
    }

    PageState* const fPage;
    const Kind fKind;
    const bool fHasParent;
    ClipInfo fParentClip;
    SkIRect fParentClipBounds;
    ClipInfo fClipInfo;
    uint32_t fClipInfoGenID = 0;
    // For isolated layers, the global bounds of their contents.
    SkIRect fIsolatedBounds = SkIRect::MakeEmpty();
};

// When printing to an EMF-backed DC, ExtTextOut with ETO_GLYPH_INDEX doesn't work unless this
// has been called (see cairo's _cairo_win32_printing_surface_init_language_pack).
bool clip_to_region(HDC dc, const SkRegion& region) {
    std::vector<RECT> rects;
    for (SkRegion::Iterator it(region); !it.done(); it.next()) {
        const SkIRect& r = it.rect();
        rects.push_back({r.fLeft, r.fTop, r.fRight, r.fBottom});
    }
    const DWORD rectsSize = DWORD(rects.size() * sizeof(RECT));
    std::vector<uint8_t> storage(sizeof(RGNDATAHEADER) + rectsSize);
    auto* data = reinterpret_cast<RGNDATA*>(storage.data());
    const SkIRect& bounds = region.getBounds();
    data->rdh = {sizeof(RGNDATAHEADER), RDH_RECTANGLES, DWORD(rects.size()), rectsSize,
                 {bounds.fLeft, bounds.fTop, bounds.fRight, bounds.fBottom}};
    memcpy(data->Buffer, rects.data(), rectsSize);
    HRGN rgn = ExtCreateRegion(nullptr, DWORD(storage.size()), data);
    if (!rgn) {
        return false;
    }
    int result = ExtSelectClipRgn(dc, rgn, RGN_AND);
    DeleteObject(rgn);
    return result != ERROR;
}

void init_language_pack() {
    if (GetModuleHandleW(L"LPK.DLL")) {
        return;
    }
    if (HMODULE gdi = GetModuleHandleW(L"GDI32.DLL")) {
        using InitLanguagePack = BOOL(WINAPI*)(int);
        if (auto init = reinterpret_cast<InitLanguagePack>(
                    reinterpret_cast<void*>(GetProcAddress(gdi, "GdiInitializeLanguagePack")))) {
            init(0);
        }
    }
}

class GDIDocument final : public SkGDI::Document {
public:
    GDIDocument(HDC dc, const SkGDI::Options& options) : fDC(dc), fOptions(options) {}

    void drawPage(SkISize size, const std::function<void(SkCanvas*)>& draw) override {
        if (size.isEmpty()) {
            return;
        }
        const SkIRect pageBounds = SkIRect::MakeSize(size);
        const SkScalar rasterScale = std::min<SkScalar>(fOptions.fRasterScale, 1);

        PageState analysis(pageBounds, rasterScale, &fCaches);
        Playback(draw, &analysis);

        const int savedDC = SaveDC(fDC);
        SetGraphicsMode(fDC, GM_ADVANCED);
        ModifyWorldTransform(fDC, nullptr, MWT_IDENTITY);
        SetMapMode(fDC, MM_TEXT);
        // Restored by PageState::applyClip() every time the clip changes.
        SaveDC(fDC);
        {
            PageState emission(pageBounds, rasterScale, &fCaches);
            emission.fDC = fDC;
            emission.fFallbackFromAnalysis = &analysis.fFallback;
            Playback(draw, &emission);
        }
        RestoreDC(fDC, -1);

        this->rasterizeFallback(draw, analysis.fFallback, rasterScale);
        RestoreDC(fDC, savedDC);
    }

private:
    static void Playback(const std::function<void(SkCanvas*)>& draw, PageState* state) {
        SkCanvas canvas(sk_make_sp<SkGDIDevice>(state, state->fBounds.size(),
                                                SkGDIDevice::Kind::kDirect, nullptr));
        draw(&canvas);
    }

    // Every rasterization needs to draw the whole page, so rasterize the bounds of the fallback
    // region in as few strips as possible, and clip the result to the region.
    void rasterizeFallback(const std::function<void(SkCanvas*)>& draw, const SkRegion& fallback,
                           SkScalar scale) {
        if (fallback.isEmpty()) {
            return;
        }
        if (!fallback.isRect() && !clip_to_region(fDC, fallback)) {
            return;
        }
        const SkIRect& area = fallback.getBounds();
        // Split tall areas into strips to bound memory usage.
        int64_t rowPixels = std::max<int64_t>(1, SkScalarCeilToInt(area.width() * scale));
        int stripHeight = std::max<int>(
                1, SkScalarFloorToInt((kMaxRasterPixels / 4 / rowPixels) / scale));
        for (int y = area.fTop; y < area.fBottom; y += stripHeight) {
            SkRegion strip(SkIRect::MakeLTRB(area.fLeft, y, area.fRight,
                                             std::min(y + stripHeight, area.fBottom)));
            if (!strip.op(fallback, SkRegion::kIntersect_Op)) {
                continue;
            }
            SkBitmap bm;
            if (rasterize(strip.getBounds(), scale, draw, &bm)) {
                blit_bitmap(fDC, bm, strip.getBounds());
            }
        }
    }

    HDC fDC;
    SkGDI::Options fOptions;
    DocumentCaches fCaches;
};

}  // namespace

namespace SkGDI {

std::unique_ptr<Document> MakeDocument(HDC dc, const Options& options) {
    if (!dc) {
        return nullptr;
    }
    init_language_pack();
    return std::make_unique<GDIDocument>(dc, options);
}

}  // namespace SkGDI
