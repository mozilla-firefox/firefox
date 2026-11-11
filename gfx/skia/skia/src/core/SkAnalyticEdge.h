/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkAnalyticEdge_DEFINED
#define SkAnalyticEdge_DEFINED

#include "include/private/base/SkAssert.h"
#include "include/private/base/SkDebug.h"
#include "include/private/base/SkFixed.h"
#include "include/private/base/SkSafe32.h"

#include <cstdint>

struct SkPoint;

struct SkAnalyticEdge {
    // Similar to SkEdge, the conic edges will be converted to quadratic edges
    enum class Type : int8_t {
        kLine,
        kQuad,
        kCubic,
    };
    enum class Winding : int8_t {
        kCW = 1,    // clockwise
        kCCW = -1,  // counter clockwise
    };

    SkAnalyticEdge* fNext;
    SkAnalyticEdge* fPrev;

    SkFixed fX;
    SkFixed fDX;
    SkFixed fUpperX;        // The x value when y = fUpperY
    SkFixed fY;             // The current y
    SkFixed fUpperY;        // The upper bound of y (our edge is from y = fUpperY to y = fLowerY)
    SkFixed fLowerY;        // The lower bound of y (our edge is from y = fUpperY to y = fLowerY)
    SkFixed fDY;            // abs(1/fDX); may be SK_MaxS32 when fDX is close to 0.
                            // fDY is only used for blitting trapezoids.

    Type fEdgeType;          // Remembers the *initial* edge type

    int8_t  fCurveCount;    // only used by kQuad(+) and kCubic(-)
    uint8_t fCurveShift;    // appled to all Dx/DDx/DDDx except for fCubicDShift exception
    Winding fWinding;

    static constexpr int kDefaultAccuracy = 2;  // default accuracy for snapping

    static inline SkFixed SnapY(SkFixed y) {
        constexpr int accuracy = kDefaultAccuracy;
        // This approach is safer than left shift, round, then right shift
        return ((unsigned)y + (SK_Fixed1 >> (accuracy + 1))) >> (16 - accuracy) << (16 - accuracy);
    }

    // Update fX, fY of this edge so fY = y
    inline void goY(SkFixed y) {
        if (y == fY + SK_Fixed1) {
            fX = Sk32_sat_add(fX, fDX);
            fY = y;
        } else if (y != fY) {
            // Drop lower digits as our alpha only has 8 bits
            // (fDX and y - fUpperY may be greater than SK_Fixed1)
            fX = Sk64_pin_to_s32(fUpperX + ((int64_t)fDX * (y - fUpperY) >> 16));
            fY = y;
        }
    }

    inline void goY(SkFixed y, int yShift) {
        SkASSERT(yShift >= 0 && yShift <= kDefaultAccuracy);
        SkASSERT(fDX == 0 || y - fY == SK_Fixed1 >> yShift);
        fY = y;
        fX = Sk32_sat_add(fX, fDX >> yShift);
    }

    bool setLine(const SkPoint& p0, const SkPoint& p1);
    bool updateLine(SkFixed ax, SkFixed ay, SkFixed bx, SkFixed by, SkFixed slope);

    // return true if we're NOT done with this edge
    bool update(SkFixed last_y);

#ifdef SK_DEBUG
    void dump() const {
        SkDebugf("edge: upperY:%d lowerY:%d y:%g x:%g dx:%g w:%d\n",
                 fUpperY,
                 fLowerY,
                 SkFixedToFloat(fY),
                 SkFixedToFloat(fX),
                 SkFixedToFloat(fDX),
                 static_cast<int8_t>(fWinding));
    }

    void validate() const {
         SkASSERT(fPrev && fNext);
         SkASSERT(fPrev->fNext == this);
         SkASSERT(fNext->fPrev == this);

         SkASSERT(fUpperY < fLowerY);
         SkASSERT(fWinding == Winding::kCW || fWinding == Winding::kCCW);
    }
#endif
};

struct SkAnalyticQuadraticEdge : public SkAnalyticEdge {
    SkFixed fQx, fQy;
    SkFixed fQDx, fQDy;
    SkFixed fQDDx, fQDDy;
    SkFixed fQLastX, fQLastY;

    // snap y to integer points in the middle of the curve to accelerate AAA path filling
    SkFixed fSnappedX, fSnappedY;

    bool setQuadraticWithoutUpdate(const SkPoint pts[3], int shiftUp);
    bool setQuadratic(const SkPoint pts[3]);
    bool updateQuadratic();
    inline void keepContinuous() {
        // We use fX as the starting x to ensure the continuouty.
        // Without it, we may break the sorted edge list.
        SkASSERT(SkAbs32(fX - SkFixedMul(fY - fSnappedY, fDX) - fSnappedX) < SK_Fixed1);
        SkASSERT(SkAbs32(fY - fSnappedY) < SK_Fixed1); // This may differ due to smooth jump
        fSnappedX = fX;
        fSnappedY = fY;
    }
};

struct SkAnalyticCubicEdge : public SkAnalyticEdge {
    SkFixed fCx, fCy;
    SkFixed fCDx, fCDy;
    SkFixed fCDDx, fCDDy;
    SkFixed fCDDDx, fCDDDy;
    SkFixed fCLastX, fCLastY;

    SkFixed fSnappedY; // to make sure that y is increasing with smooth jump and snapping

    uint8_t fCubicDShift;   // applied to fCDx and fCDy

    bool setCubicWithoutUpdate(const SkPoint pts[4], int shiftUp);
    bool setCubic(const SkPoint pts[4]);
    bool updateCubic();
    inline void keepContinuous() {
        SkASSERT(SkAbs32(fX - SkFixedMul(fDX, fY - SnapY(fCy)) - fCx) < SK_Fixed1);
        fCx = fX;
        fSnappedY = fY;
    }
};

#endif
