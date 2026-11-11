/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/core/SkEdgeBuilder.h"

#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "include/core/SkTypes.h"
#include "include/private/base/SkDebug.h"
#include "include/private/base/SkFixed.h"
#include "include/private/base/SkFloatingPoint.h"
#include "include/private/base/SkSafe32.h"
#include "src/base/SkSafeMath.h"
#include "src/core/SkAnalyticEdge.h"
#include "src/core/SkEdge.h"
#include "src/core/SkEdgeClipper.h"
#include "src/core/SkGeometry.h"
#include "src/core/SkLineClipper.h"
#include "src/core/SkPathPriv.h"

SkEdgeBuilder::Combine SkBasicEdgeBuilder::combineVertical(const SkEdge* edge, SkEdge* last) {
    // We only consider edges that were originally lines to be vertical to avoid numerical issues
    // (crbug.com/1154864).
    if (last->fEdgeType != SkEdge::Type::kLine || last->fDxDy || edge->fX != last->fX) {
        return kNo_Combine;
    }
    if (edge->fWinding == last->fWinding) {
        if (edge->fLastY + 1 == last->fFirstY) {
            last->fFirstY = edge->fFirstY;
            return kPartial_Combine;
        }
        if (edge->fFirstY == last->fLastY + 1) {
            last->fLastY = edge->fLastY;
            return kPartial_Combine;
        }
        return kNo_Combine;
    }
    if (!fCanCullToTheRight) {
        return kNo_Combine;
    }
    if (edge->fFirstY == last->fFirstY) {
        if (edge->fLastY == last->fLastY) {
            return kTotal_Combine;
        }
        if (edge->fLastY < last->fLastY) {
            last->fFirstY = edge->fLastY + 1;
            return kPartial_Combine;
        }
        last->fFirstY = last->fLastY + 1;
        last->fLastY = edge->fLastY;
        last->fWinding = edge->fWinding;
        return kPartial_Combine;
    }
    if (edge->fLastY == last->fLastY) {
        if (edge->fFirstY > last->fFirstY) {
            last->fLastY = edge->fFirstY - 1;
            return kPartial_Combine;
        }
        last->fLastY = last->fFirstY - 1;
        last->fFirstY = edge->fFirstY;
        last->fWinding = edge->fWinding;
        return kPartial_Combine;
    }
    return kNo_Combine;
}

SkEdgeBuilder::Combine SkAnalyticEdgeBuilder::combineVertical(const SkAnalyticEdge* edge,
                                                              SkAnalyticEdge* last) {
    auto approximately_equal = [](SkFixed a, SkFixed b) {
        return SkAbs32(a - b) < 0x100;
    };

    // We only consider edges that were originally lines to be vertical to avoid numerical issues
    // (crbug.com/1154864).
    if (last->fEdgeType != SkAnalyticEdge::Type::kLine || last->fDX || edge->fX != last->fX) {
        return kNo_Combine;
    }
    if (edge->fWinding == last->fWinding) {
        if (edge->fLowerY == last->fUpperY) {
            last->fUpperY = edge->fUpperY;
            last->fY = last->fUpperY;
            return kPartial_Combine;
        }
        if (approximately_equal(edge->fUpperY, last->fLowerY)) {
            last->fLowerY = edge->fLowerY;
            return kPartial_Combine;
        }
        return kNo_Combine;
    }
    if (!fCanCullToTheRight) {
        return kNo_Combine;
    }
    if (approximately_equal(edge->fUpperY, last->fUpperY)) {
        if (approximately_equal(edge->fLowerY, last->fLowerY)) {
            return kTotal_Combine;
        }
        if (edge->fLowerY < last->fLowerY) {
            last->fUpperY = edge->fLowerY;
            last->fY = last->fUpperY;
            return kPartial_Combine;
        }
        last->fUpperY = last->fLowerY;
        last->fY = last->fUpperY;
        last->fLowerY = edge->fLowerY;
        last->fWinding = edge->fWinding;
        return kPartial_Combine;
    }
    if (approximately_equal(edge->fLowerY, last->fLowerY)) {
        if (edge->fUpperY > last->fUpperY) {
            last->fLowerY = edge->fUpperY;
            return kPartial_Combine;
        }
        last->fLowerY = last->fUpperY;
        last->fUpperY = edge->fUpperY;
        last->fY = last->fUpperY;
        last->fWinding = edge->fWinding;
        return kPartial_Combine;
    }
    return kNo_Combine;
}

static bool is_vertical(const SkEdge* edge) {
    // We only consider edges that were originally lines to be vertical to avoid numerical issues
    // (crbug.com/1154864).
    return edge->fDxDy     == 0
        && edge->fEdgeType == SkEdge::Type::kLine;
}

static bool is_vertical(const SkAnalyticEdge* edge) {
    // We only consider edges that were originally lines to be vertical to avoid numerical issues
    // (crbug.com/1154864).
    return edge->fDX       == 0
        && edge->fEdgeType == SkAnalyticEdge::Type::kLine;
}

// TODO: we can deallocate the edge if edge->setFoo() fails
// or when we don't use it (kPartial_Combine or kTotal_Combine).

void SkBasicEdgeBuilder::addLine(const SkPoint pts[]) {
    SkEdge* edge = fAlloc.make<SkEdge>();
    if (edge->setLine(pts[0], pts[1])) {
        Combine combine = is_vertical(edge) && !fList.empty()
            ? this->combineVertical(edge, (SkEdge*)fList.back())
            : kNo_Combine;

        switch (combine) {
            case kTotal_Combine:    fList.pop_back();      break;
            case kPartial_Combine:                         break;
            case kNo_Combine:       fList.push_back(edge); break;
        }
    }
}
void SkAnalyticEdgeBuilder::addLine(const SkPoint pts[]) {
    SkAnalyticEdge* edge = fAlloc.make<SkAnalyticEdge>();
    if (edge->setLine(pts[0], pts[1])) {

        Combine combine = is_vertical(edge) && !fList.empty()
            ? this->combineVertical(edge, (SkAnalyticEdge*)fList.back())
            : kNo_Combine;

        switch (combine) {
            case kTotal_Combine:    fList.pop_back();      break;
            case kPartial_Combine:                         break;
            case kNo_Combine:       fList.push_back(edge); break;
        }
    }
}
void SkBasicEdgeBuilder::addQuad(const SkPoint pts[]) {
    SkQuadraticEdge* edge = fAlloc.make<SkQuadraticEdge>();
    if (edge->setQuadratic(pts)) {
        fList.push_back(edge);
    }
}
void SkAnalyticEdgeBuilder::addQuad(const SkPoint pts[]) {
    SkAnalyticQuadraticEdge* edge = fAlloc.make<SkAnalyticQuadraticEdge>();
    if (edge->setQuadratic(pts)) {
        fList.push_back(edge);
    }
}

void SkBasicEdgeBuilder::addCubic(const SkPoint pts[]) {
    SkCubicEdge* edge = fAlloc.make<SkCubicEdge>();
    if (edge->setCubic(pts)) {
        fList.push_back(edge);
    }
}
void SkAnalyticEdgeBuilder::addCubic(const SkPoint pts[]) {
    SkAnalyticCubicEdge* edge = fAlloc.make<SkAnalyticCubicEdge>();
    if (edge->setCubic(pts)) {
        fList.push_back(edge);
    }
}

// TODO: merge addLine() and addPolyLine()?

SkEdgeBuilder::Combine SkAnalyticEdgeBuilder::addPolyLine(const SkPoint pts[],
                                                          char* arg_edge, char** arg_edgePtr) {
    auto edge    = (SkAnalyticEdge*) arg_edge;
    auto edgePtr = (SkAnalyticEdge**)arg_edgePtr;

    if (edge->setLine(pts[0], pts[1])) {
        return is_vertical(edge) && edgePtr > (SkAnalyticEdge**)fEdgeList
            ? this->combineVertical(edge, edgePtr[-1])
            : kNo_Combine;
    }
    return SkEdgeBuilder::kPartial_Combine;  // As above.
}

SkRect SkBasicEdgeBuilder::recoverClip(const SkIRect& src) const {
    return SkRect::Make(src);
}
SkRect SkAnalyticEdgeBuilder::recoverClip(const SkIRect& src) const {
    return SkRect::Make(src);
}

char* SkAnalyticEdgeBuilder::allocEdges(size_t n, size_t* size) {
    *size = sizeof(SkAnalyticEdge);
    return (char*)fAlloc.makeArrayDefault<SkAnalyticEdge>(n);
}

// TODO: maybe get rid of buildPoly() entirely?
int SkEdgeBuilder::buildPoly(const SkPathRaw& raw, const SkIRect* iclip, bool canCullToTheRight) {
    size_t maxEdgeCount = raw.fPoints.size();
    if (iclip) {
        // clipping can turn 1 line into (up to) kMaxClippedLineSegments, since
        // we turn portions that are clipped out on the left/right into vertical
        // segments.
        SkSafeMath safe;
        maxEdgeCount = safe.mul(maxEdgeCount, SkLineClipper::kMaxClippedLineSegments);
        if (!safe) {
            return 0;
        }
    }

    SkPathEdgeIter iter(raw);
    if (iclip) {
        SkRect clip = this->recoverClip(*iclip);

        while (auto e = iter.next()) {
            switch (e.fEdge) {
                case SkPathEdgeIter::Edge::kLine: {
                    SkPoint lines[SkLineClipper::kMaxPoints];
                    int lineCount = SkLineClipper::ClipLine(e.fPts, clip, lines, canCullToTheRight);
                    SkASSERT(lineCount <= SkLineClipper::kMaxClippedLineSegments);
                    for (int i = 0; i < lineCount; i++) {
                        this->addLine(lines + i);
                    }
                    break;
                }
                default:
                    SkDEBUGFAIL("unexpected verb");
                    break;
            }
        }
    } else {
        while (auto e = iter.next()) {
            switch (e.fEdge) {
                case SkPathEdgeIter::Edge::kLine: {
                    this->addLine(e.fPts);
                    break;
                }
                default:
                    SkDEBUGFAIL("unexpected verb");
                    break;
            }
        }
    }
    fEdgeList = fList.begin();
    return fList.size();
}

int SkEdgeBuilder::build(const SkPathRaw& raw, const SkIRect* iclip, bool canCullToTheRight) {
    if (iclip) {
        SkRect clip = this->recoverClip(*iclip);
        struct Rec {
            SkEdgeBuilder* fBuilder;
            bool           fIsFinite;
        } rec = { this, true };

        SkEdgeClipper::ClipPath(raw, clip, canCullToTheRight,
                                [](SkEdgeClipper* clipper, bool, void* ctx) {
            Rec* rec = (Rec*)ctx;
            SkPoint      pts[4];

            while (auto verb = clipper->next(pts)) {
                const int count = SkPathPriv::PtsInIter(*verb);
                if (!SkIsFinite(&pts[0].fX, count*2)) {
                    rec->fIsFinite = false;
                    return;
                }
                switch (*verb) {
                    case SkPathVerb::kLine:  rec->fBuilder->addLine (pts); break;
                    case SkPathVerb::kQuad:  rec->fBuilder->addQuad (pts); break;
                    case SkPathVerb::kCubic: rec->fBuilder->addCubic(pts); break;
                    default: break;
                }
            }
        }, &rec);
        fEdgeList = fList.begin();
        return rec.fIsFinite ? fList.size() : 0;
    }

    SkPathEdgeIter iter(raw);
    SkAutoConicToQuads quadder;
    constexpr float kConicTol = 0.25f;
    SkPoint monoY[10];
    SkPoint monoX[5];
    auto handle_quad = [this, &monoX](const SkPoint pts[3]) {
        int n = SkChopQuadAtYExtrema(pts, monoX);
        for (int i = 0; i <= n; i++) {
            this->addQuad(&monoX[i * 2]);
        }
    };

    while (auto e = iter.next()) {
        switch (e.fEdge) {
            case SkPathEdgeIter::Edge::kLine:
                this->addLine(e.fPts);
                break;
            case SkPathEdgeIter::Edge::kQuad: {
                handle_quad(e.fPts);
                break;
            }
            case SkPathEdgeIter::Edge::kConic: {
                const SkPoint* quadPts =
                        quadder.computeQuads(e.fPts, iter.conicWeight(), kConicTol);
                for (int i = 0; i < quadder.countQuads(); ++i) {
                    handle_quad(quadPts);
                    quadPts += 2;
                }
            } break;
            case SkPathEdgeIter::Edge::kCubic: {
                int n = SkChopCubicAtYExtrema(e.fPts, monoY);
                for (int i = 0; i <= n; i++) {
                    this->addCubic(&monoY[i * 3]);
                }
                break;
            }
            default:
                SkDEBUGFAIL("Unknown edge type");
                break;
        }
    }
    fEdgeList = fList.begin();
    return fList.size();
}

int SkEdgeBuilder::buildEdges(const SkPathRaw& raw,
                              const SkIRect* shiftedClip) {
    // If we're convex, then we need both edges, even if the right edge is past the clip.
    const bool canCullToTheRight = !raw.isKnownToBeConvex();
    fCanCullToTheRight = canCullToTheRight;

    // We can use our buildPoly() optimization if all the segments are lines.
    // (Edges are homogeneous and stored contiguously in memory, no need for indirection.)
    const int count = SkPath::kLine_SegmentMask == raw.segmentMasks()
        ? this->buildPoly(raw, shiftedClip, canCullToTheRight)
        : this->build    (raw, shiftedClip, canCullToTheRight);

    SkASSERT(count >= 0);

    // If we can't cull to the right, we should have count > 1 (or 0).
    if (!canCullToTheRight) {
        SkASSERT(count != 1);
    }
    return count;
}

int SkEdgeBuilder::buildEdges(const SkPath& path, const SkIRect* shiftedClip) {
    if (auto raw = SkPathPriv::Raw(path, SkResolveConvexity::kYes)) {
        return buildEdges(*raw, shiftedClip);
    }
    return 0;   // no edges were built
}
