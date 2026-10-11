/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SkGDIDocument_DEFINED
#define SkGDIDocument_DEFINED

#include "include/core/SkScalar.h"
#include "include/core/SkSize.h"
#include "include/core/SkTypes.h"

#include <windows.h>

#include <functional>
#include <memory>

class SkCanvas;

namespace SkGDI {

struct Options {
    // Resolution of the bitmaps used for content that GDI can't express (blending, non-trivial
    // layers, filters...), relative to the device resolution. Should be <= 1.
    SkScalar fRasterScale = 1.0f;
};

/**
 * Replays pages into |dc| as GDI calls, rasterizing only the regions of the page that GDI can't
 * express, in the spirit of SkPDF and cairo's win32 printing surface.
 *
 * The caller remains responsible for StartDoc/StartPage/EndPage/EndDoc.
 */
class SK_API Document {
public:
    virtual ~Document() = default;

    /**
     * Emits a page of |size| device units of the DC. The page contents need to be analyzed
     * before anything can be emitted, so |draw| is called multiple times, and must draw exactly
     * the same content every time. Nothing drawn needs to outlive each call.
     */
    virtual void drawPage(SkISize size, const std::function<void(SkCanvas*)>& draw) = 0;
};

SK_API std::unique_ptr<Document> MakeDocument(HDC dc, const Options& options = Options());

}  // namespace SkGDI

#endif
