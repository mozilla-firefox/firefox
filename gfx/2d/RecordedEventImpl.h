/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RECORDEDEVENTIMPL_H_
#define MOZILLA_GFX_RECORDEDEVENTIMPL_H_

#include "RecordedEvent.h"

#include "PathRecording.h"
#include "RecordingTypes.h"
#include "Tools.h"
#include "Filters.h"
#include "Logging.h"
#include "ScaledFontBase.h"
#include "SFNTData.h"

#include "mozilla/dom/CanvasRenderingContextHelper.h"
#include "mozilla/layers/BuildConstants.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/ipc/SerializeToBytesUtil.h"

namespace mozilla {
namespace gfx {

class RecordedDrawTargetCreation
    : public RecordedEventDerived<RecordedDrawTargetCreation> {
 public:
  RecordedDrawTargetCreation(ReferencePtr aRefPtr, BackendType aType,
                             const IntRect& aRect, SurfaceFormat aFormat,
                             bool aHasExistingData = false,
                             SourceSurface* aExistingData = nullptr)
      : RecordedEventDerived(DRAWTARGETCREATION),
        mRefPtr(aRefPtr),
        mBackendType(aType),
        mRect(aRect),
        mFormat(aFormat),
        mHasExistingData(aHasExistingData),
        mExistingData(aExistingData) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  virtual void OutputSimpleEventInfo(
      std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "DrawTarget Creation"; }

  ReferencePtr mRefPtr;
  BackendType mBackendType;
  IntRect mRect;
  SurfaceFormat mFormat;
  bool mHasExistingData = false;
  RefPtr<SourceSurface> mExistingData;

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedDrawTargetCreation(S& aStream);
};

class RecordedDrawTargetDestruction
    : public RecordedEventDerived<RecordedDrawTargetDestruction> {
 public:
  MOZ_IMPLICIT RecordedDrawTargetDestruction(ReferencePtr aRefPtr)
      : RecordedEventDerived(DRAWTARGETDESTRUCTION),
        mRefPtr(aRefPtr),
        mBackendType(BackendType::NONE) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "DrawTarget Destruction"; }

  ReferencePtr mRefPtr;

  BackendType mBackendType = BackendType::NONE;

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedDrawTargetDestruction(S& aStream);
};

class RecordedSetCurrentDrawTarget
    : public RecordedEventDerived<RecordedSetCurrentDrawTarget> {
 public:
  MOZ_IMPLICIT RecordedSetCurrentDrawTarget(ReferencePtr aRefPtr)
      : RecordedEventDerived(SETCURRENTDRAWTARGET), mRefPtr(aRefPtr) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "SetCurrentDrawTarget"; }

  ReferencePtr mRefPtr;

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedSetCurrentDrawTarget(S& aStream);
};

class RecordedCreateSimilarDrawTarget
    : public RecordedEventDerived<RecordedCreateSimilarDrawTarget> {
 public:
  RecordedCreateSimilarDrawTarget(ReferencePtr aRefPtr, const IntSize& aSize,
                                  SurfaceFormat aFormat)
      : RecordedEventDerived(CREATESIMILARDRAWTARGET),
        mRefPtr(aRefPtr),
        mSize(aSize),
        mFormat(aFormat) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  virtual void OutputSimpleEventInfo(
      std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "CreateSimilarDrawTarget"; }

  ReferencePtr mRefPtr;
  IntSize mSize;
  SurfaceFormat mFormat;

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedCreateSimilarDrawTarget(S& aStream);
};

class RecordedCreateClippedDrawTarget
    : public RecordedEventDerived<RecordedCreateClippedDrawTarget> {
 public:
  RecordedCreateClippedDrawTarget(ReferencePtr aRefPtr, const Rect& aBounds,
                                  SurfaceFormat aFormat)
      : RecordedEventDerived(CREATECLIPPEDDRAWTARGET),
        mRefPtr(aRefPtr),
        mBounds(aBounds),
        mFormat(aFormat) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  virtual void OutputSimpleEventInfo(
      std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "CreateClippedDrawTarget"; }

  ReferencePtr mRefPtr;
  Rect mBounds;
  SurfaceFormat mFormat;

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedCreateClippedDrawTarget(S& aStream);
};

class RecordedCreateDrawTargetForFilter
    : public RecordedEventDerived<RecordedCreateDrawTargetForFilter> {
 public:
  RecordedCreateDrawTargetForFilter(ReferencePtr aRefPtr,
                                    const IntSize& aMaxSize,
                                    SurfaceFormat aFormat, FilterNode* aFilter,
                                    FilterNode* aSource,
                                    const Rect& aSourceRect,
                                    const Point& aDestPoint)
      : RecordedEventDerived(CREATEDRAWTARGETFORFILTER),
        mRefPtr(aRefPtr),
        mMaxSize(aMaxSize),
        mFormat(aFormat),
        mFilter(aFilter),
        mSource(aSource),
        mSourceRect(aSourceRect),
        mDestPoint(aDestPoint) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  virtual void OutputSimpleEventInfo(
      std::stringstream& aStringStream) const override;

  std::string GetName() const override {
    return "CreateSimilarDrawTargetForFilter";
  }

  ReferencePtr mRefPtr;
  IntSize mMaxSize;
  SurfaceFormat mFormat;
  ReferencePtr mFilter;
  ReferencePtr mSource;
  Rect mSourceRect;
  Point mDestPoint;

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedCreateDrawTargetForFilter(S& aStream);
};

class RecordedFillRect : public RecordedEventDerived<RecordedFillRect> {
 public:
  RecordedFillRect(const Rect& aRect, const Pattern& aPattern,
                   const DrawOptions& aOptions)
      : RecordedEventDerived(FILLRECT),
        mRect(aRect),
        mPattern(),
        mOptions(aOptions) {
    StorePattern(mPattern, aPattern);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "FillRect"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedFillRect(S& aStream);

  Rect mRect;
  PatternStorage mPattern;
  DrawOptions mOptions;
};

class RecordedStrokeRect : public RecordedEventDerived<RecordedStrokeRect>,
                           public RecordedStrokeOptionsMixin {
 public:
  RecordedStrokeRect(const Rect& aRect, const Pattern& aPattern,
                     const StrokeOptions& aStrokeOptions,
                     const DrawOptions& aOptions)
      : RecordedEventDerived(STROKERECT),
        mRect(aRect),
        mPattern(),
        mStrokeOptions(aStrokeOptions),
        mOptions(aOptions) {
    StorePattern(mPattern, aPattern);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "StrokeRect"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedStrokeRect(S& aStream);

  Rect mRect;
  PatternStorage mPattern;
  StrokeOptions mStrokeOptions;
  DrawOptions mOptions;
};

class RecordedStrokeLine : public RecordedEventDerived<RecordedStrokeLine>,
                           public RecordedStrokeOptionsMixin {
 public:
  RecordedStrokeLine(const Point& aBegin, const Point& aEnd,
                     const Pattern& aPattern,
                     const StrokeOptions& aStrokeOptions,
                     const DrawOptions& aOptions)
      : RecordedEventDerived(STROKELINE),
        mBegin(aBegin),
        mEnd(aEnd),
        mPattern(),
        mStrokeOptions(aStrokeOptions),
        mOptions(aOptions) {
    StorePattern(mPattern, aPattern);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "StrokeLine"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedStrokeLine(S& aStream);

  Point mBegin;
  Point mEnd;
  PatternStorage mPattern;
  StrokeOptions mStrokeOptions;
  DrawOptions mOptions;
};

class RecordedStrokeCircle : public RecordedEventDerived<RecordedStrokeCircle>,
                             public RecordedStrokeOptionsMixin {
 public:
  RecordedStrokeCircle(Circle aCircle, const Pattern& aPattern,
                       const StrokeOptions& aStrokeOptions,
                       const DrawOptions& aOptions)
      : RecordedEventDerived(STROKECIRCLE),
        mCircle(aCircle),
        mPattern(),
        mStrokeOptions(aStrokeOptions),
        mOptions(aOptions) {
    StorePattern(mPattern, aPattern);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "StrokeCircle"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedStrokeCircle(S& aStream);

  Circle mCircle;
  PatternStorage mPattern;
  StrokeOptions mStrokeOptions;
  DrawOptions mOptions;
};

class RecordedFill : public RecordedEventDerived<RecordedFill> {
 public:
  RecordedFill(ReferencePtr aPath, const Pattern& aPattern,
               const DrawOptions& aOptions)
      : RecordedEventDerived(FILL),
        mPath(aPath),
        mPattern(),
        mOptions(aOptions) {
    StorePattern(mPattern, aPattern);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "Fill"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedFill(S& aStream);

  ReferencePtr mPath;
  PatternStorage mPattern;
  DrawOptions mOptions;
};

class RecordedFillCircle : public RecordedEventDerived<RecordedFillCircle> {
 public:
  RecordedFillCircle(Circle aCircle, const Pattern& aPattern,
                     const DrawOptions& aOptions)
      : RecordedEventDerived(FILLCIRCLE),
        mCircle(aCircle),
        mPattern(),
        mOptions(aOptions) {
    StorePattern(mPattern, aPattern);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "FillCircle"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedFillCircle(S& aStream);

  Circle mCircle;
  PatternStorage mPattern;
  DrawOptions mOptions;
};

template <class Derived>
class RecordedDrawGlyphs : public RecordedEventDerived<Derived> {
 public:
  RecordedDrawGlyphs(RecordedEvent::EventType aType, ReferencePtr aScaledFont,
                     const Pattern& aPattern, const DrawOptions& aOptions,
                     const Glyph* aGlyphs, uint32_t aNumGlyphs)
      : RecordedEventDerived<Derived>(aType),
        mScaledFont(aScaledFont),
        mPattern(),
        mOptions(aOptions) {
    this->StorePattern(mPattern, aPattern);
    mGlyphs.Assign(aGlyphs, aNumGlyphs);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

 protected:
  friend class RecordedEvent;

  template <class S>
  RecordedDrawGlyphs(RecordedEvent::EventType aType, S& aStream);

  virtual void DrawGlyphs(DrawTarget* aDT, ScaledFont* aScaledFont,
                          const GlyphBuffer& aBuffer,
                          const Pattern& aPattern) const = 0;

  ReferencePtr mScaledFont;
  PatternStorage mPattern;
  DrawOptions mOptions;
  RecordedEventArray<Glyph, uint32_t> mGlyphs;
};

class RecordedFillGlyphs : public RecordedDrawGlyphs<RecordedFillGlyphs> {
 public:
  RecordedFillGlyphs(ReferencePtr aScaledFont, const Pattern& aPattern,
                     const DrawOptions& aOptions, const Glyph* aGlyphs,
                     uint32_t aNumGlyphs)
      : RecordedDrawGlyphs(FILLGLYPHS, aScaledFont, aPattern, aOptions, aGlyphs,
                           aNumGlyphs) {}

  std::string GetName() const override { return "FillGlyphs"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedFillGlyphs(S& aStream)
      : RecordedDrawGlyphs(FILLGLYPHS, aStream) {}

  void DrawGlyphs(DrawTarget* aDT, ScaledFont* aScaledFont,
                  const GlyphBuffer& aBuffer,
                  const Pattern& aPattern) const override {
    aDT->FillGlyphs(aScaledFont, aBuffer, aPattern, mOptions);
  }
};

class RecordedStrokeGlyphs : public RecordedDrawGlyphs<RecordedStrokeGlyphs>,
                             public RecordedStrokeOptionsMixin {
 public:
  RecordedStrokeGlyphs(ReferencePtr aScaledFont, const Pattern& aPattern,
                       const StrokeOptions& aStrokeOptions,
                       const DrawOptions& aOptions, const Glyph* aGlyphs,
                       uint32_t aNumGlyphs)
      : RecordedDrawGlyphs(STROKEGLYPHS, aScaledFont, aPattern, aOptions,
                           aGlyphs, aNumGlyphs),
        mStrokeOptions(aStrokeOptions) {}

  std::string GetName() const override { return "StrokeGlyphs"; }

  template <class S>
  void Record(S& aStream) const {
    RecordedDrawGlyphs::Record(aStream);
    RecordStrokeOptions(aStream, mStrokeOptions);
  }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedStrokeGlyphs(S& aStream)
      : RecordedDrawGlyphs(STROKEGLYPHS, aStream) {
    ReadStrokeOptions(aStream, mStrokeOptions);
  }

  void DrawGlyphs(DrawTarget* aDT, ScaledFont* aScaledFont,
                  const GlyphBuffer& aBuffer,
                  const Pattern& aPattern) const override {
    aDT->StrokeGlyphs(aScaledFont, aBuffer, aPattern, mStrokeOptions, mOptions);
  }

  StrokeOptions mStrokeOptions;
};

class RecordedMask : public RecordedEventDerived<RecordedMask> {
 public:
  RecordedMask(const Pattern& aSource, const Pattern& aMask,
               const DrawOptions& aOptions)
      : RecordedEventDerived(MASK), mSource(), mMask(), mOptions(aOptions) {
    StorePattern(mSource, aSource);
    StorePattern(mMask, aMask);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "Mask"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedMask(S& aStream);

  PatternStorage mSource;
  PatternStorage mMask;
  DrawOptions mOptions;
};

class RecordedStroke : public RecordedEventDerived<RecordedStroke>,
                       public RecordedStrokeOptionsMixin {
 public:
  RecordedStroke(ReferencePtr aPath, const Pattern& aPattern,
                 const StrokeOptions& aStrokeOptions,
                 const DrawOptions& aOptions)
      : RecordedEventDerived(STROKE),
        mPath(aPath),
        mPattern(),
        mStrokeOptions(aStrokeOptions),
        mOptions(aOptions) {
    StorePattern(mPattern, aPattern);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  virtual void OutputSimpleEventInfo(
      std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "Stroke"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedStroke(S& aStream);

  ReferencePtr mPath;
  PatternStorage mPattern;
  StrokeOptions mStrokeOptions;
  DrawOptions mOptions;
};

class RecordedClearRect : public RecordedEventDerived<RecordedClearRect> {
 public:
  explicit RecordedClearRect(const Rect& aRect)
      : RecordedEventDerived(CLEARRECT), mRect(aRect) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "ClearRect"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedClearRect(S& aStream);

  Rect mRect;
};

class RecordedCopySurface : public RecordedEventDerived<RecordedCopySurface> {
 public:
  RecordedCopySurface(ReferencePtr aSourceSurface, const IntRect& aSourceRect,
                      const IntPoint& aDest)
      : RecordedEventDerived(COPYSURFACE),
        mSourceSurface(aSourceSurface),
        mSourceRect(aSourceRect),
        mDest(aDest) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "CopySurface"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedCopySurface(S& aStream);

  ReferencePtr mSourceSurface;
  IntRect mSourceRect;
  IntPoint mDest;
};

class RecordedPushClip : public RecordedEventDerived<RecordedPushClip> {
 public:
  explicit RecordedPushClip(ReferencePtr aPath)
      : RecordedEventDerived(PUSHCLIP), mPath(aPath) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "PushClip"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedPushClip(S& aStream);

  ReferencePtr mPath;
};

class RecordedPushClipRect : public RecordedEventDerived<RecordedPushClipRect> {
 public:
  explicit RecordedPushClipRect(const Rect& aRect)
      : RecordedEventDerived(PUSHCLIPRECT), mRect(aRect) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "PushClipRect"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedPushClipRect(S& aStream);

  Rect mRect;
};

class RecordedPopClip : public RecordedEventDerived<RecordedPopClip> {
 public:
  MOZ_IMPLICIT RecordedPopClip() : RecordedEventDerived(POPCLIP) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "PopClip"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedPopClip(S& aStream);
};

class RecordedRemoveAllClips
    : public RecordedEventDerived<RecordedRemoveAllClips> {
 public:
  MOZ_IMPLICIT RecordedRemoveAllClips()
      : RecordedEventDerived(REMOVEALLCLIPS) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "RemoveAllClips"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedRemoveAllClips(S& aStream);
};

class RecordedPushLayer : public RecordedEventDerived<RecordedPushLayer> {
 public:
  RecordedPushLayer(bool aOpaque, Float aOpacity, SourceSurface* aMask,
                    const Matrix& aMaskTransform, const IntRect& aBounds,
                    bool aCopyBackground)
      : RecordedEventDerived(PUSHLAYER),
        mOpaque(aOpaque),
        mOpacity(aOpacity),
        mMask(aMask),
        mMaskTransform(aMaskTransform),
        mBounds(aBounds),
        mCopyBackground(aCopyBackground) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "PushLayer"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedPushLayer(S& aStream);

  bool mOpaque = false;
  Float mOpacity = 1.0f;
  ReferencePtr mMask;
  Matrix mMaskTransform;
  IntRect mBounds;
  bool mCopyBackground = false;
};

class RecordedPushLayerWithBlend
    : public RecordedEventDerived<RecordedPushLayerWithBlend> {
 public:
  RecordedPushLayerWithBlend(bool aOpaque, Float aOpacity, SourceSurface* aMask,
                             const Matrix& aMaskTransform,
                             const IntRect& aBounds, bool aCopyBackground,
                             CompositionOp aCompositionOp)
      : RecordedEventDerived(PUSHLAYERWITHBLEND),
        mOpaque(aOpaque),
        mOpacity(aOpacity),
        mMask(aMask),
        mMaskTransform(aMaskTransform),
        mBounds(aBounds),
        mCopyBackground(aCopyBackground),
        mCompositionOp(aCompositionOp) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  virtual void OutputSimpleEventInfo(
      std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "PushLayerWithBlend"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedPushLayerWithBlend(S& aStream);

  bool mOpaque = false;
  Float mOpacity = 1.0f;
  ReferencePtr mMask;
  Matrix mMaskTransform;
  IntRect mBounds;
  bool mCopyBackground = false;
  CompositionOp mCompositionOp = CompositionOp::OP_OVER;
};

class RecordedPopLayer : public RecordedEventDerived<RecordedPopLayer> {
 public:
  RecordedPopLayer() : RecordedEventDerived(POPLAYER) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "PopLayer"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedPopLayer(S& aStream);
};

class RecordedSetPermitSubpixelAA
    : public RecordedEventDerived<RecordedSetPermitSubpixelAA> {
 public:
  explicit RecordedSetPermitSubpixelAA(bool aPermitSubpixelAA)
      : RecordedEventDerived(SETPERMITSUBPIXELAA),
        mPermitSubpixelAA(aPermitSubpixelAA) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "SetPermitSubpixelAA"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedSetPermitSubpixelAA(S& aStream);

  bool mPermitSubpixelAA = false;
};

class RecordedSetTransform : public RecordedEventDerived<RecordedSetTransform> {
 public:
  explicit RecordedSetTransform(const Matrix& aTransform)
      : RecordedEventDerived(SETTRANSFORM), mTransform(aTransform) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "SetTransform"; }

  Matrix mTransform;

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedSetTransform(S& aStream);
};

class RecordedDrawSurface : public RecordedEventDerived<RecordedDrawSurface> {
 public:
  RecordedDrawSurface(ReferencePtr aRefSource, const Rect& aDest,
                      const Rect& aSource, const DrawSurfaceOptions& aDSOptions,
                      const DrawOptions& aOptions)
      : RecordedEventDerived(DRAWSURFACE),
        mRefSource(aRefSource),
        mDest(aDest),
        mSource(aSource),
        mDSOptions(aDSOptions),
        mOptions(aOptions) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "DrawSurface"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedDrawSurface(S& aStream);

  ReferencePtr mRefSource;
  Rect mDest;
  Rect mSource;
  DrawSurfaceOptions mDSOptions;
  DrawOptions mOptions;
};

class RecordedDrawSurfaceDescriptor
    : public RecordedEventDerived<RecordedDrawSurfaceDescriptor> {
 public:
  RecordedDrawSurfaceDescriptor(const layers::SurfaceDescriptor& aDesc,
                                const Rect& aDest, const Rect& aSource,
                                const DrawSurfaceOptions& aDSOptions,
                                const DrawOptions& aOptions)
      : RecordedEventDerived(DRAWSURFACEDESCRIPTOR),
        mDesc(aDesc),
        mDest(aDest),
        mSource(aSource),
        mDSOptions(aDSOptions),
        mOptions(aOptions) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "DrawSurfaceDescriptor"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedDrawSurfaceDescriptor(S& aStream);

  layers::SurfaceDescriptor mDesc;
  Rect mDest;
  Rect mSource;
  DrawSurfaceOptions mDSOptions;
  DrawOptions mOptions;
};

class RecordedDrawDependentSurface
    : public RecordedEventDerived<RecordedDrawDependentSurface> {
 public:
  RecordedDrawDependentSurface(uint64_t aId, const Rect& aDest)
      : RecordedEventDerived(DRAWDEPENDENTSURFACE), mId(aId), mDest(aDest) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "DrawDependentSurface"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedDrawDependentSurface(S& aStream);

  uint64_t mId;
  Rect mDest;
};

class RecordedDrawSurfaceWithShadow
    : public RecordedEventDerived<RecordedDrawSurfaceWithShadow> {
 public:
  RecordedDrawSurfaceWithShadow(ReferencePtr aRefSource, const Point& aDest,
                                const ShadowOptions& aShadow, CompositionOp aOp)
      : RecordedEventDerived(DRAWSURFACEWITHSHADOW),
        mRefSource(aRefSource),
        mDest(aDest),
        mShadow(aShadow),
        mOp(aOp) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "DrawSurfaceWithShadow"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedDrawSurfaceWithShadow(S& aStream);

  ReferencePtr mRefSource;
  Point mDest;
  ShadowOptions mShadow;
  CompositionOp mOp;
};

class RecordedDrawShadow : public RecordedEventDerived<RecordedDrawShadow>,
                           public RecordedStrokeOptionsMixin {
 public:
  RecordedDrawShadow(ReferencePtr aPath, const Pattern& aPattern,
                     const ShadowOptions& aShadow, const DrawOptions& aOptions,
                     const StrokeOptions* aStrokeOptions)
      : RecordedEventDerived(DRAWSHADOW),
        mPath(aPath),
        mPattern(),
        mShadow(aShadow),
        mOptions(aOptions),
        mHasStrokeOptions(!!aStrokeOptions),
        mStrokeOptions(aStrokeOptions ? *aStrokeOptions : StrokeOptions()) {
    StorePattern(mPattern, aPattern);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "DrawShadow"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedDrawShadow(S& aStream);

  ReferencePtr mPath;
  PatternStorage mPattern;
  ShadowOptions mShadow;
  DrawOptions mOptions;
  bool mHasStrokeOptions = false;
  StrokeOptions mStrokeOptions;
};

class RecordedDrawFilter : public RecordedEventDerived<RecordedDrawFilter> {
 public:
  RecordedDrawFilter(ReferencePtr aNode, const Rect& aSourceRect,
                     const Point& aDestPoint, const DrawOptions& aOptions)
      : RecordedEventDerived(DRAWFILTER),
        mNode(aNode),
        mSourceRect(aSourceRect),
        mDestPoint(aDestPoint),
        mOptions(aOptions) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "DrawFilter"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedDrawFilter(S& aStream);

  ReferencePtr mNode;
  Rect mSourceRect;
  Point mDestPoint;
  DrawOptions mOptions;
};

class RecordedPathCreation : public RecordedEventDerived<RecordedPathCreation> {
 public:
  MOZ_IMPLICIT RecordedPathCreation(PathRecording* aPath);

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "Path Creation"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mDT;
  ReferencePtr mRefPtr;
  FillRule mFillRule;
  RefPtr<PathRecording> mPath;
  UniquePtr<PathOps> mPathOps;

  template <class S>
  MOZ_IMPLICIT RecordedPathCreation(S& aStream);
};

class RecordedPathDestruction
    : public RecordedEventDerived<RecordedPathDestruction> {
 public:
  MOZ_IMPLICIT RecordedPathDestruction(PathRecording* aPath)
      : RecordedEventDerived(PATHDESTRUCTION), mRefPtr(aPath) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "Path Destruction"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;

  template <class S>
  MOZ_IMPLICIT RecordedPathDestruction(S& aStream);
};

class RecordedSourceSurfaceCreation
    : public RecordedEventDerived<RecordedSourceSurfaceCreation> {
 public:
  RecordedSourceSurfaceCreation(ReferencePtr aRefPtr,
                                const RefPtr<DataSourceSurface>& aDataSurface)
      : RecordedEventDerived(SOURCESURFACECREATION),
        mRefPtr(aRefPtr),
        mDataSurface(aDataSurface) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "SourceSurface Creation"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;
  RefPtr<DataSourceSurface> mDataSurface;

  template <class S>
  MOZ_IMPLICIT RecordedSourceSurfaceCreation(S& aStream);
};

class RecordedSourceSurfaceDestruction
    : public RecordedEventDerived<RecordedSourceSurfaceDestruction> {
 public:
  MOZ_IMPLICIT RecordedSourceSurfaceDestruction(ReferencePtr aRefPtr)
      : RecordedEventDerived(SOURCESURFACEDESTRUCTION), mRefPtr(aRefPtr) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "SourceSurface Destruction"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;

  template <class S>
  MOZ_IMPLICIT RecordedSourceSurfaceDestruction(S& aStream);
};

class RecordedOptimizeSourceSurface
    : public RecordedEventDerived<RecordedOptimizeSourceSurface> {
 public:
  RecordedOptimizeSourceSurface(ReferencePtr aSurface,
                                ReferencePtr aOptimizedSurface)
      : RecordedEventDerived(OPTIMIZESOURCESURFACE),
        mSurface(aSurface),
        mOptimizedSurface(aOptimizedSurface) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "OptimizeSourceSurface"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mSurface;
  ReferencePtr mOptimizedSurface;

  template <class S>
  MOZ_IMPLICIT RecordedOptimizeSourceSurface(S& aStream);
};

class RecordedExternalSurfaceCreation
    : public RecordedEventDerived<RecordedExternalSurfaceCreation> {
 public:
  RecordedExternalSurfaceCreation(ReferencePtr aRefPtr, const uint64_t aKey)
      : RecordedEventDerived(EXTERNALSURFACECREATION),
        mRefPtr(aRefPtr),
        mKey(aKey) {}

  ~RecordedExternalSurfaceCreation() = default;

  virtual bool PlayEvent(Translator* aTranslator) const;

  template <class S>
  void Record(S& aStream) const;
  virtual void OutputSimpleEventInfo(std::stringstream& aStringStream) const;

  virtual std::string GetName() const {
    return "SourceSurfaceSharedData Creation";
  }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;
  uint64_t mKey;

  template <class S>
  MOZ_IMPLICIT RecordedExternalSurfaceCreation(S& aStream);
};

class RecordedFilterNodeCreation
    : public RecordedEventDerived<RecordedFilterNodeCreation> {
 public:
  RecordedFilterNodeCreation(ReferencePtr aRefPtr, FilterType aType)
      : RecordedEventDerived(FILTERNODECREATION),
        mRefPtr(aRefPtr),
        mType(aType) {}

  ~RecordedFilterNodeCreation();

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "FilterNode Creation"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;
  FilterType mType;

  template <class S>
  MOZ_IMPLICIT RecordedFilterNodeCreation(S& aStream);
};

class RecordedDeferFilterInput
    : public RecordedEventDerived<RecordedDeferFilterInput>,
      public RecordedStrokeOptionsMixin {
 public:
  RecordedDeferFilterInput(ReferencePtr aRefPtr, ReferencePtr aPath,
                           const Pattern& aPattern, const IntRect& aSourceRect,
                           const IntPoint& aDestOffset,
                           const DrawOptions& aOptions,
                           const StrokeOptions* aStrokeOptions)
      : RecordedEventDerived(DEFERFILTERINPUT),
        mRefPtr(aRefPtr),
        mPath(aPath),
        mPattern(),
        mSourceRect(aSourceRect),
        mDestOffset(aDestOffset),
        mOptions(aOptions),
        mHasStrokeOptions(!!aStrokeOptions),
        mStrokeOptions(aStrokeOptions ? *aStrokeOptions : StrokeOptions()) {
    StorePattern(mPattern, aPattern);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "DeferFilterInput"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedDeferFilterInput(S& aStream);

  ReferencePtr mRefPtr;
  ReferencePtr mPath;
  PatternStorage mPattern;
  IntRect mSourceRect;
  IntPoint mDestOffset;
  DrawOptions mOptions;
  bool mHasStrokeOptions = false;
  StrokeOptions mStrokeOptions;
};

class RecordedFilterNodeDestruction
    : public RecordedEventDerived<RecordedFilterNodeDestruction> {
 public:
  MOZ_IMPLICIT RecordedFilterNodeDestruction(ReferencePtr aRefPtr)
      : RecordedEventDerived(FILTERNODEDESTRUCTION), mRefPtr(aRefPtr) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "FilterNode Destruction"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;

  template <class S>
  MOZ_IMPLICIT RecordedFilterNodeDestruction(S& aStream);
};

class RecordedGradientStopsCreation
    : public RecordedEventDerived<RecordedGradientStopsCreation> {
 public:
  RecordedGradientStopsCreation(ReferencePtr aRefPtr, GradientStop* aStops,
                                uint32_t aNumStops, ExtendMode aExtendMode)
      : RecordedEventDerived(GRADIENTSTOPSCREATION),
        mRefPtr(aRefPtr),
        mStops(aStops),
        mNumStops(aNumStops),
        mExtendMode(aExtendMode),
        mDataOwned(false) {}

  ~RecordedGradientStopsCreation();

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "GradientStops Creation"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;
  GradientStop* mStops = nullptr;
  uint32_t mNumStops = 0;
  ExtendMode mExtendMode;
  bool mDataOwned;

  template <class S>
  MOZ_IMPLICIT RecordedGradientStopsCreation(S& aStream);
};

class RecordedGradientStopsDestruction
    : public RecordedEventDerived<RecordedGradientStopsDestruction> {
 public:
  MOZ_IMPLICIT RecordedGradientStopsDestruction(ReferencePtr aRefPtr)
      : RecordedEventDerived(GRADIENTSTOPSDESTRUCTION), mRefPtr(aRefPtr) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "GradientStops Destruction"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;

  template <class S>
  MOZ_IMPLICIT RecordedGradientStopsDestruction(S& aStream);
};

class RecordedFlush : public RecordedEventDerived<RecordedFlush> {
 public:
  explicit RecordedFlush() : RecordedEventDerived(FLUSH) {}

  bool PlayEvent(Translator* aTranslator) const final;

  template <class S>
  void Record(S& aStream) const;
  virtual void OutputSimpleEventInfo(
      std::stringstream& aStringStream) const override;

  virtual std::string GetName() const override { return "Flush"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedFlush(S& aStream);
};

class RecordedDetachAllSnapshots
    : public RecordedEventDerived<RecordedDetachAllSnapshots> {
 public:
  explicit RecordedDetachAllSnapshots()
      : RecordedEventDerived(DETACHALLSNAPSHOTS) {}

  bool PlayEvent(Translator* aTranslator) const final;

  template <class S>
  void Record(S& aStream) const;
  virtual void OutputSimpleEventInfo(
      std::stringstream& aStringStream) const override;

  virtual std::string GetName() const override { return "DetachAllSnapshots"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedDetachAllSnapshots(S& aStream);
};

class RecordedSnapshot : public RecordedEventDerived<RecordedSnapshot> {
 public:
  explicit RecordedSnapshot(ReferencePtr aRefPtr)
      : RecordedEventDerived(SNAPSHOT), mRefPtr(aRefPtr) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "Snapshot"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;

  template <class S>
  MOZ_IMPLICIT RecordedSnapshot(S& aStream);
};

class RecordedIntoLuminanceSource
    : public RecordedEventDerived<RecordedIntoLuminanceSource> {
 public:
  RecordedIntoLuminanceSource(ReferencePtr aRefPtr,
                              LuminanceType aLuminanceType, float aOpacity)
      : RecordedEventDerived(INTOLUMINANCE),
        mRefPtr(aRefPtr),
        mLuminanceType(aLuminanceType),
        mOpacity(aOpacity) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "IntoLuminanceSource"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;
  LuminanceType mLuminanceType;
  float mOpacity;

  template <class S>
  MOZ_IMPLICIT RecordedIntoLuminanceSource(S& aStream);
};

class RecordedExtractSubrect
    : public RecordedEventDerived<RecordedExtractSubrect> {
 public:
  RecordedExtractSubrect(ReferencePtr aRefPtr, ReferencePtr aSourceSurface,
                         const IntRect& aSubrect)
      : RecordedEventDerived(EXTRACTSUBRECT),
        mRefPtr(aRefPtr),
        mSourceSurface(aSourceSurface),
        mSubrect(aSubrect) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "ExtractSubrect"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;
  ReferencePtr mSourceSurface;
  IntRect mSubrect;

  template <class S>
  MOZ_IMPLICIT RecordedExtractSubrect(S& aStream);
};

class RecordedFontData : public RecordedEventDerived<RecordedFontData> {
 public:
  static void FontDataProc(const uint8_t* aData, uint32_t aSize,
                           uint32_t aIndex, void* aBaton) {
    auto recordedFontData = static_cast<RecordedFontData*>(aBaton);
    recordedFontData->SetFontData(aData, aSize, aIndex);
  }

  explicit RecordedFontData(UnscaledFont* aUnscaledFont)
      : RecordedEventDerived(FONTDATA),
        mType(aUnscaledFont->GetType()),
        mFontDetails() {
    if (!aUnscaledFont->GetFontFileData(&FontDataProc, this)) {
      mGetFontFileDataSucceeded = false;
    }
  }

  bool IsValid() const { return mGetFontFileDataSucceeded; }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "Font Data"; }

  void SetFontData(const uint8_t* aData, uint32_t aSize, uint32_t aIndex);

  bool GetFontDetails(RecordedFontDetails& fontDetails);

 private:
  friend class RecordedEvent;

  FontType mType;
  RecordedEventArray<uint8_t, uint32_t> mData;
  RecordedFontDetails mFontDetails;

  bool mGetFontFileDataSucceeded = true;

  template <class S>
  MOZ_IMPLICIT RecordedFontData(S& aStream);
};

class RecordedFontDescriptor
    : public RecordedEventDerived<RecordedFontDescriptor> {
 public:
  static void FontDescCb(const uint8_t* aData, uint32_t aSize, uint32_t aIndex,
                         void* aBaton) {
    auto recordedFontDesc = static_cast<RecordedFontDescriptor*>(aBaton);
    recordedFontDesc->SetFontDescriptor(aData, aSize, aIndex);
  }

  explicit RecordedFontDescriptor(UnscaledFont* aUnscaledFont)
      : RecordedEventDerived(FONTDESC),
        mType(aUnscaledFont->GetType()),
        mIndex(0),
        mRefPtr(aUnscaledFont) {
    mHasDesc = aUnscaledFont->GetFontDescriptor(FontDescCb, this);
  }

  virtual ~RecordedFontDescriptor();

  bool IsValid() const { return mHasDesc; }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "Font Desc"; }

 private:
  friend class RecordedEvent;

  void SetFontDescriptor(const uint8_t* aData, uint32_t aSize, uint32_t aIndex);

  bool mHasDesc = false;

  FontType mType = FontType::UNKNOWN;
  RecordedEventArray<uint8_t> mData;
  uint32_t mIndex = 0;
  ReferencePtr mRefPtr;

  template <class S>
  MOZ_IMPLICIT RecordedFontDescriptor(S& aStream);
};

class RecordedUnscaledFontCreation
    : public RecordedEventDerived<RecordedUnscaledFontCreation> {
 public:
  static void FontInstanceDataProc(const uint8_t* aData, uint32_t aSize,
                                   void* aBaton) {
    auto recordedUnscaledFontCreation =
        static_cast<RecordedUnscaledFontCreation*>(aBaton);
    recordedUnscaledFontCreation->SetFontInstanceData(aData, aSize);
  }

  RecordedUnscaledFontCreation(UnscaledFont* aUnscaledFont,
                               RecordedFontDetails aFontDetails)
      : RecordedEventDerived(UNSCALEDFONTCREATION),
        mRefPtr(aUnscaledFont),
        mFontDataKey(aFontDetails.fontDataKey),
        mIndex(aFontDetails.index) {
    aUnscaledFont->GetFontInstanceData(FontInstanceDataProc, this);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "UnscaledFont Creation"; }

  void SetFontInstanceData(const uint8_t* aData, uint32_t aSize);

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;
  uint64_t mFontDataKey;
  uint32_t mIndex;
  RecordedEventArray<uint8_t> mInstanceData;

  template <class S>
  MOZ_IMPLICIT RecordedUnscaledFontCreation(S& aStream);
};

class RecordedUnscaledFontDestruction
    : public RecordedEventDerived<RecordedUnscaledFontDestruction> {
 public:
  MOZ_IMPLICIT RecordedUnscaledFontDestruction(ReferencePtr aRefPtr)
      : RecordedEventDerived(UNSCALEDFONTDESTRUCTION), mRefPtr(aRefPtr) {}

  bool PlayEvent(Translator* aTranslator) const override;
  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "UnscaledFont Destruction"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;

  template <class S>
  MOZ_IMPLICIT RecordedUnscaledFontDestruction(S& aStream);
};

class RecordedScaledFontCreation
    : public RecordedEventDerived<RecordedScaledFontCreation> {
 public:
  static void FontInstanceDataProc(const uint8_t* aData, uint32_t aSize,
                                   const FontVariation* aVariations,
                                   uint32_t aNumVariations, void* aBaton) {
    auto recordedScaledFontCreation =
        static_cast<RecordedScaledFontCreation*>(aBaton);
    recordedScaledFontCreation->SetFontInstanceData(aData, aSize, aVariations,
                                                    aNumVariations);
  }

  RecordedScaledFontCreation(ScaledFont* aScaledFont,
                             UnscaledFont* aUnscaledFont)
      : RecordedEventDerived(SCALEDFONTCREATION),
        mRefPtr(aScaledFont),
        mUnscaledFont(aUnscaledFont),
        mGlyphSize(aScaledFont->GetSize()) {
    aScaledFont->GetFontInstanceData(FontInstanceDataProc, this);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "ScaledFont Creation"; }

  void SetFontInstanceData(const uint8_t* aData, uint32_t aSize,
                           const FontVariation* aVariations,
                           uint32_t aNumVariations);

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;
  ReferencePtr mUnscaledFont;
  Float mGlyphSize;
  RecordedEventArray<uint8_t> mInstanceData;
  RecordedEventArray<FontVariation> mVariations;

  template <class S>
  MOZ_IMPLICIT RecordedScaledFontCreation(S& aStream);
};

class RecordedScaledFontDestruction
    : public RecordedEventDerived<RecordedScaledFontDestruction> {
 public:
  MOZ_IMPLICIT RecordedScaledFontDestruction(ReferencePtr aRefPtr)
      : RecordedEventDerived(SCALEDFONTDESTRUCTION), mRefPtr(aRefPtr) {}

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "ScaledFont Destruction"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mRefPtr;

  template <class S>
  MOZ_IMPLICIT RecordedScaledFontDestruction(S& aStream);
};

class RecordedMaskSurface : public RecordedEventDerived<RecordedMaskSurface> {
 public:
  RecordedMaskSurface(const Pattern& aPattern, ReferencePtr aRefMask,
                      const Point& aOffset, const DrawOptions& aOptions)
      : RecordedEventDerived(MASKSURFACE),
        mPattern(),
        mRefMask(aRefMask),
        mOffset(aOffset),
        mOptions(aOptions) {
    StorePattern(mPattern, aPattern);
  }

  bool PlayEvent(Translator* aTranslator) const override;

  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "MaskSurface"; }

 private:
  friend class RecordedEvent;

  template <class S>
  MOZ_IMPLICIT RecordedMaskSurface(S& aStream);

  PatternStorage mPattern;
  ReferencePtr mRefMask;
  Point mOffset;
  DrawOptions mOptions;
};

class RecordedFilterNodeSetAttribute
    : public RecordedEventDerived<RecordedFilterNodeSetAttribute> {
 public:
  enum ArgType {
    ARGTYPE_UINT32,
    ARGTYPE_BOOL,
    ARGTYPE_FLOAT,
    ARGTYPE_SIZE,
    ARGTYPE_INTSIZE,
    ARGTYPE_INTPOINT,
    ARGTYPE_RECT,
    ARGTYPE_INTRECT,
    ARGTYPE_POINT,
    ARGTYPE_MATRIX,
    ARGTYPE_MATRIX5X4,
    ARGTYPE_POINT3D,
    ARGTYPE_COLOR,
    ARGTYPE_FLOAT_ARRAY
  };

  template <typename T>
  void WritePayload(const T& aValue) {
    mPayload.Assign(reinterpret_cast<const uint8_t*>(&aValue), sizeof(T));
  }

  void WritePayload(const bool& aValue) {
    uint8_t data = aValue ? 1 : 0;
    mPayload.Assign(&data, 1);
  }

  template <typename T>
  RecordedFilterNodeSetAttribute(FilterNode* aNode, uint32_t aIndex,
                                 T aArgument, ArgType aArgType)
      : RecordedEventDerived(FILTERNODESETATTRIBUTE),
        mNode(aNode),
        mIndex(aIndex),
        mArgType(aArgType) {
    WritePayload(aArgument);
  }

  RecordedFilterNodeSetAttribute(FilterNode* aNode, uint32_t aIndex,
                                 const Float* aFloat, uint32_t aSize)
      : RecordedEventDerived(FILTERNODESETATTRIBUTE),
        mNode(aNode),
        mIndex(aIndex),
        mArgType(ARGTYPE_FLOAT_ARRAY) {
    mPayload.Assign(reinterpret_cast<const uint8_t*>(aFloat),
                    sizeof(Float) * aSize);
  }

  bool PlayEvent(Translator* aTranslator) const override;
  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "SetAttribute"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mNode;

  uint32_t mIndex;
  ArgType mArgType;
  RecordedEventArray<uint8_t> mPayload;

  template <class S>
  MOZ_IMPLICIT RecordedFilterNodeSetAttribute(S& aStream);
};

class RecordedFilterNodeSetInput
    : public RecordedEventDerived<RecordedFilterNodeSetInput> {
 public:
  RecordedFilterNodeSetInput(FilterNode* aNode, uint32_t aIndex,
                             FilterNode* aInputNode)
      : RecordedEventDerived(FILTERNODESETINPUT),
        mNode(aNode),
        mIndex(aIndex),
        mInputFilter(aInputNode),
        mInputSurface(nullptr) {}

  RecordedFilterNodeSetInput(FilterNode* aNode, uint32_t aIndex,
                             SourceSurface* aInputSurface)
      : RecordedEventDerived(FILTERNODESETINPUT),
        mNode(aNode),
        mIndex(aIndex),
        mInputFilter(nullptr),
        mInputSurface(aInputSurface) {}

  bool PlayEvent(Translator* aTranslator) const override;
  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "SetInput"; }

 private:
  friend class RecordedEvent;

  ReferencePtr mNode;
  uint32_t mIndex;
  ReferencePtr mInputFilter;
  ReferencePtr mInputSurface;

  template <class S>
  MOZ_IMPLICIT RecordedFilterNodeSetInput(S& aStream);
};

class RecordedLink : public RecordedEventDerived<RecordedLink> {
 public:
  RecordedLink(const char* aLocalDest, const char* aURI, const Rect& aRect)
      : RecordedEventDerived(LINK),
        mLocalDest(aLocalDest),
        mURI(aURI),
        mRect(aRect) {}

  bool PlayEvent(Translator* aTranslator) const override;
  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "Link"; }

 private:
  friend class RecordedEvent;

  RecordedEventCString mLocalDest;
  RecordedEventCString mURI;
  Rect mRect;

  template <class S>
  MOZ_IMPLICIT RecordedLink(S& aStream);
};

class RecordedDestination : public RecordedEventDerived<RecordedDestination> {
 public:
  RecordedDestination(const char* aDestination, const Point& aPoint)
      : RecordedEventDerived(DESTINATION),
        mDestination(aDestination),
        mPoint(aPoint) {}

  bool PlayEvent(Translator* aTranslator) const override;
  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "Destination"; }

 private:
  friend class RecordedEvent;

  RecordedEventCString mDestination;
  Point mPoint;

  template <class S>
  MOZ_IMPLICIT RecordedDestination(S& aStream);
};

class RecordedAccessibleId : public RecordedEventDerived<RecordedAccessibleId> {
 public:
  RecordedAccessibleId(uint64_t aBrowsingContextId, uint64_t aAccId)
      : RecordedEventDerived(ACCESSIBLEID),
        mBrowsingContextId(aBrowsingContextId),
        mAccId(aAccId) {}

  bool PlayEvent(Translator* aTranslator) const override;
  template <class S>
  void Record(S& aStream) const;
  void OutputSimpleEventInfo(std::stringstream& aStringStream) const override;

  std::string GetName() const override { return "AccessibleId"; }

 private:
  friend class RecordedEvent;

  uint64_t mBrowsingContextId = 0;
  uint64_t mAccId = 0;

  template <class S>
  MOZ_IMPLICIT RecordedAccessibleId(S& aStream);
};

static std::string NameFromBackend(BackendType aType) {
  switch (aType) {
    case BackendType::NONE:
      return "None";
    default:
      return "Unknown";
  }
}

template <class S>
void RecordedEvent::RecordPatternData(S& aStream,
                                      const PatternStorage& aPattern) const {
  WriteElement(aStream, aPattern.mType);

  switch (aPattern.mType) {
    case PatternType::COLOR: {
      WriteElement(aStream, *reinterpret_cast<const ColorPatternStorage*>(
                                &aPattern.mStorage));
      return;
    }
    case PatternType::LINEAR_GRADIENT: {
      WriteElement(aStream,
                   *reinterpret_cast<const LinearGradientPatternStorage*>(
                       &aPattern.mStorage));
      return;
    }
    case PatternType::RADIAL_GRADIENT: {
      WriteElement(aStream,
                   *reinterpret_cast<const RadialGradientPatternStorage*>(
                       &aPattern.mStorage));
      return;
    }
    case PatternType::CONIC_GRADIENT: {
      WriteElement(aStream,
                   *reinterpret_cast<const ConicGradientPatternStorage*>(
                       &aPattern.mStorage));
      return;
    }
    case PatternType::SURFACE: {
      WriteElement(aStream, *reinterpret_cast<const SurfacePatternStorage*>(
                                &aPattern.mStorage));
      return;
    }
    default:
      return;
  }
}

template <class S>
void RecordedEvent::ReadPatternData(S& aStream,
                                    PatternStorage& aPattern) const {
  ReadElementConstrained(aStream, aPattern.mType, PatternType::COLOR,
                         kHighestPatternType);

  switch (aPattern.mType) {
    case PatternType::COLOR: {
      ReadElement(aStream,
                  *reinterpret_cast<ColorPatternStorage*>(&aPattern.mStorage));
      return;
    }
    case PatternType::LINEAR_GRADIENT: {
      ReadElement(aStream, *reinterpret_cast<LinearGradientPatternStorage*>(
                               &aPattern.mStorage));
      return;
    }
    case PatternType::RADIAL_GRADIENT: {
      ReadElement(aStream, *reinterpret_cast<RadialGradientPatternStorage*>(
                               &aPattern.mStorage));
      return;
    }
    case PatternType::CONIC_GRADIENT: {
      ReadElement(aStream, *reinterpret_cast<ConicGradientPatternStorage*>(
                               &aPattern.mStorage));
      return;
    }
    case PatternType::SURFACE: {
      SurfacePatternStorage* sps =
          reinterpret_cast<SurfacePatternStorage*>(&aPattern.mStorage);
      ReadElement(aStream, *sps);
      if (sps->mExtend < ExtendMode::CLAMP ||
          sps->mExtend > ExtendMode::REFLECT) {
        aStream.SetIsBad();
        return;
      }

      if (sps->mSamplingFilter < SamplingFilter::GOOD ||
          sps->mSamplingFilter >= SamplingFilter::SENTINEL) {
        aStream.SetIsBad();
      }
      return;
    }
    default:
      return;
  }
}

inline void RecordedEvent::StorePattern(PatternStorage& aDestination,
                                        const Pattern& aSource) const {
  aDestination.mType = aSource.GetType();

  switch (aSource.GetType()) {
    case PatternType::COLOR: {
      reinterpret_cast<ColorPatternStorage*>(&aDestination.mStorage)->mColor =
          static_cast<const ColorPattern*>(&aSource)->mColor;
      return;
    }
    case PatternType::LINEAR_GRADIENT: {
      LinearGradientPatternStorage* store =
          reinterpret_cast<LinearGradientPatternStorage*>(
              &aDestination.mStorage);
      const LinearGradientPattern* pat =
          static_cast<const LinearGradientPattern*>(&aSource);
      store->mBegin = pat->mBegin;
      store->mEnd = pat->mEnd;
      store->mMatrix = pat->mMatrix;
      store->mStops = pat->mStops.get();
      return;
    }
    case PatternType::RADIAL_GRADIENT: {
      RadialGradientPatternStorage* store =
          reinterpret_cast<RadialGradientPatternStorage*>(
              &aDestination.mStorage);
      const RadialGradientPattern* pat =
          static_cast<const RadialGradientPattern*>(&aSource);
      store->mCenter1 = pat->mCenter1;
      store->mCenter2 = pat->mCenter2;
      store->mRadius1 = pat->mRadius1;
      store->mRadius2 = pat->mRadius2;
      store->mMatrix = pat->mMatrix;
      store->mStops = pat->mStops.get();
      return;
    }
    case PatternType::CONIC_GRADIENT: {
      ConicGradientPatternStorage* store =
          reinterpret_cast<ConicGradientPatternStorage*>(
              &aDestination.mStorage);
      const ConicGradientPattern* pat =
          static_cast<const ConicGradientPattern*>(&aSource);
      store->mCenter = pat->mCenter;
      store->mAngle = pat->mAngle;
      store->mStartOffset = pat->mStartOffset;
      store->mEndOffset = pat->mEndOffset;
      store->mMatrix = pat->mMatrix;
      store->mStops = pat->mStops.get();
      return;
    }
    case PatternType::SURFACE: {
      SurfacePatternStorage* store =
          reinterpret_cast<SurfacePatternStorage*>(&aDestination.mStorage);
      const SurfacePattern* pat = static_cast<const SurfacePattern*>(&aSource);
      store->mExtend = pat->mExtendMode;
      store->mSamplingFilter = pat->mSamplingFilter;
      store->mMatrix = pat->mMatrix;
      store->mSurface = pat->mSurface;
      store->mSamplingRect = pat->mSamplingRect;
      return;
    }
  }
}

template <class S>
void RecordedStrokeOptionsMixin::RecordStrokeOptions(
    S& aStream, const StrokeOptions& aStrokeOptions) const {
  JoinStyle joinStyle = aStrokeOptions.mLineJoin;
  CapStyle capStyle = aStrokeOptions.mLineCap;

  WriteElement(aStream, uint64_t(aStrokeOptions.mDashLength));
  WriteElement(aStream, aStrokeOptions.mLineWidth);
  WriteElement(aStream, aStrokeOptions.mMiterLimit);
  WriteElement(aStream, joinStyle);
  WriteElement(aStream, capStyle);

  if (!aStrokeOptions.mDashPattern) {
    return;
  }

  WriteElement(aStream, aStrokeOptions.mDashOffset);
  aStream.write((char*)aStrokeOptions.mDashPattern,
                sizeof(Float) * aStrokeOptions.mDashLength);
}

template <class S>
void RecordedStrokeOptionsMixin::ReadStrokeOptions(
    S& aStream, StrokeOptions& aStrokeOptions) {
  uint64_t dashLength64;
  JoinStyle joinStyle;
  CapStyle capStyle;

  ReadElement(aStream, dashLength64);
  ReadElement(aStream, aStrokeOptions.mLineWidth);
  ReadElement(aStream, aStrokeOptions.mMiterLimit);
  ReadElementConstrained(aStream, joinStyle, JoinStyle::BEVEL,
                         JoinStyle::MITER_OR_BEVEL);
  ReadElementConstrained(aStream, capStyle, CapStyle::BUTT, CapStyle::SQUARE);
  aStrokeOptions.mLineJoin = joinStyle;
  aStrokeOptions.mLineCap = capStyle;

  // On 32 bit we truncate the value of dashLength.
  // See also bug 811850 for history.
  size_t dashLength = size_t(dashLength64);
  if (!dashLength || !aStream.good()) {
    return;
  }

  ReadElement(aStream, aStrokeOptions.mDashOffset);

  mDashPatternStorage = MakeUniqueFallible<Float[]>(dashLength);
  if (!mDashPatternStorage) {
    aStream.SetIsBad();
    return;
  }
  if (!aStream.read((char*)mDashPatternStorage.get(),
                    sizeof(Float) * dashLength)) {
    return;
  }
  aStrokeOptions.mDashLength = dashLength;
  aStrokeOptions.mDashPattern = mDashPatternStorage.get();
}

template <class S>
static void ReadDrawOptions(S& aStream, DrawOptions& aDrawOptions) {
  ReadElement(aStream, aDrawOptions);
  if (aDrawOptions.mAntialiasMode < AntialiasMode::NONE ||
      aDrawOptions.mAntialiasMode > AntialiasMode::DEFAULT) {
    aStream.SetIsBad();
    return;
  }

  if (aDrawOptions.mCompositionOp < CompositionOp::OP_CLEAR ||
      aDrawOptions.mCompositionOp > CompositionOp::OP_COUNT) {
    aStream.SetIsBad();
  }
}

template <class S>
static void ReadDrawSurfaceOptions(S& aStream,
                                   DrawSurfaceOptions& aDrawSurfaceOptions) {
  ReadElement(aStream, aDrawSurfaceOptions);
  if (aDrawSurfaceOptions.mSamplingFilter < SamplingFilter::GOOD ||
      aDrawSurfaceOptions.mSamplingFilter >= SamplingFilter::SENTINEL) {
    aStream.SetIsBad();
    return;
  }

  if (aDrawSurfaceOptions.mSamplingBounds < SamplingBounds::UNBOUNDED ||
      aDrawSurfaceOptions.mSamplingBounds > SamplingBounds::BOUNDED) {
    aStream.SetIsBad();
  }
}

inline void RecordedEvent::OutputSimplePatternInfo(
    const PatternStorage& aStorage, std::stringstream& aOutput) const {
  switch (aStorage.mType) {
    case PatternType::COLOR: {
      const DeviceColor color =
          reinterpret_cast<const ColorPatternStorage*>(&aStorage.mStorage)
              ->mColor;
      aOutput << "DeviceColor: (" << color.r << ", " << color.g << ", "
              << color.b << ", " << color.a << ")";
      return;
    }
    case PatternType::LINEAR_GRADIENT: {
      const LinearGradientPatternStorage* store =
          reinterpret_cast<const LinearGradientPatternStorage*>(
              &aStorage.mStorage);

      aOutput << "LinearGradient (" << store->mBegin.x << ", "
              << store->mBegin.y << ") - (" << store->mEnd.x << ", "
              << store->mEnd.y << ") Stops: " << store->mStops;
      return;
    }
    case PatternType::RADIAL_GRADIENT: {
      const RadialGradientPatternStorage* store =
          reinterpret_cast<const RadialGradientPatternStorage*>(
              &aStorage.mStorage);
      aOutput << "RadialGradient (Center 1: (" << store->mCenter1.x << ", "
              << store->mCenter2.y << ") Radius 2: " << store->mRadius2;
      return;
    }
    case PatternType::CONIC_GRADIENT: {
      const ConicGradientPatternStorage* store =
          reinterpret_cast<const ConicGradientPatternStorage*>(
              &aStorage.mStorage);
      aOutput << "ConicGradient (Center: (" << store->mCenter.x << ", "
              << store->mCenter.y << ") Angle: " << store->mAngle
              << " Range:" << store->mStartOffset << " - " << store->mEndOffset;
      return;
    }
    case PatternType::SURFACE: {
      const SurfacePatternStorage* store =
          reinterpret_cast<const SurfacePatternStorage*>(&aStorage.mStorage);
      aOutput << "Surface (0x" << store->mSurface << ")";
      return;
    }
  }
}

inline bool RecordedDrawTargetCreation::PlayEvent(
    Translator* aTranslator) const {
  RefPtr<DrawTarget> newDT =
      aTranslator->CreateDrawTarget(mRefPtr, mRect.Size(), mFormat);

  // If we couldn't create a DrawTarget this will probably cause us to crash
  // with nullptr later in the playback, so return false to abort.
  if (!newDT) {
    return false;
  }

  if (mHasExistingData) {
    Rect dataRect(0, 0, mExistingData->GetSize().width,
                  mExistingData->GetSize().height);
    newDT->DrawSurface(mExistingData, dataRect, dataRect);
  }

  return true;
}

template <class S>
void RecordedDrawTargetCreation::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mBackendType);
  WriteElement(aStream, mRect);
  WriteElement(aStream, mFormat);
  WriteElement(aStream, mHasExistingData);

  if (mHasExistingData) {
    MOZ_ASSERT(mExistingData);
    MOZ_ASSERT(mExistingData->GetSize() == mRect.Size());
    RefPtr<DataSourceSurface> dataSurf = mExistingData->GetDataSurface();

    DataSourceSurface::ScopedMap map(dataSurf, DataSourceSurface::READ);
    for (int y = 0; y < mRect.height; y++) {
      aStream.write((const char*)map.GetData() + y * map.GetStride(),
                    BytesPerPixel(mFormat) * mRect.width);
    }
  }
}

template <class S>
RecordedDrawTargetCreation::RecordedDrawTargetCreation(S& aStream)
    : RecordedEventDerived(DRAWTARGETCREATION), mExistingData(nullptr) {
  ReadElement(aStream, mRefPtr);
  ReadElementConstrained(aStream, mBackendType, BackendType::NONE,
                         BackendType::WEBRENDER_TEXT);
  ReadElement(aStream, mRect);
  ReadElementConstrained(aStream, mFormat, SurfaceFormat::B8G8R8A8,
                         SurfaceFormat::UNKNOWN);
  ReadElement(aStream, mHasExistingData);

  if (mHasExistingData) {
    RefPtr<DataSourceSurface> dataSurf =
        Factory::CreateDataSourceSurface(mRect.Size(), mFormat);
    if (!dataSurf) {
      gfxWarning()
          << "RecordedDrawTargetCreation had to reset mHasExistingData";
      mHasExistingData = false;
      aStream.SetIsBad();
      return;
    }

    DataSourceSurface::ScopedMap map(dataSurf, DataSourceSurface::READ);
    for (int y = 0; y < mRect.height; y++) {
      if (!aStream.read((char*)map.GetData() + y * map.GetStride(),
                        BytesPerPixel(mFormat) * mRect.width)) {
        mHasExistingData = false;
        return;
      }
    }
    mExistingData = dataSurf;
  }
}

inline void RecordedDrawTargetCreation::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] DrawTarget Creation (Type: "
                << NameFromBackend(mBackendType) << ", Size: " << mRect.width
                << "x" << mRect.height << ")";
}

inline bool RecordedDrawTargetDestruction::PlayEvent(
    Translator* aTranslator) const {
  aTranslator->RemoveDrawTarget(mRefPtr);
  return true;
}

template <class S>
void RecordedDrawTargetDestruction::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
}

template <class S>
RecordedDrawTargetDestruction::RecordedDrawTargetDestruction(S& aStream)
    : RecordedEventDerived(DRAWTARGETDESTRUCTION) {
  ReadElement(aStream, mRefPtr);
}

inline void RecordedDrawTargetDestruction::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] DrawTarget Destruction";
}

inline bool RecordedSetCurrentDrawTarget::PlayEvent(
    Translator* aTranslator) const {
  return aTranslator->SetCurrentDrawTarget(mRefPtr);
}

template <class S>
void RecordedSetCurrentDrawTarget::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
}

template <class S>
RecordedSetCurrentDrawTarget::RecordedSetCurrentDrawTarget(S& aStream)
    : RecordedEventDerived(SETCURRENTDRAWTARGET) {
  ReadElement(aStream, mRefPtr);
}

inline void RecordedSetCurrentDrawTarget::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] SetCurrentDrawTarget";
}

inline bool RecordedCreateSimilarDrawTarget::PlayEvent(
    Translator* aTranslator) const {
  DrawTarget* drawTarget = aTranslator->GetCurrentDrawTarget();
  if (!drawTarget) {
    return false;
  }

  RefPtr<DrawTarget> newDT =
      drawTarget->CreateSimilarDrawTarget(mSize, mFormat);

  // If we couldn't create a DrawTarget this will probably cause us to crash
  // with nullptr later in the playback, so return false to abort.
  if (!newDT) {
    return false;
  }

  aTranslator->AddDrawTarget(mRefPtr, newDT);
  return true;
}

template <class S>
void RecordedCreateSimilarDrawTarget::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mSize);
  WriteElement(aStream, mFormat);
}

template <class S>
RecordedCreateSimilarDrawTarget::RecordedCreateSimilarDrawTarget(S& aStream)
    : RecordedEventDerived(CREATESIMILARDRAWTARGET) {
  ReadElement(aStream, mRefPtr);
  ReadElement(aStream, mSize);
  ReadElementConstrained(aStream, mFormat, SurfaceFormat::B8G8R8A8,
                         SurfaceFormat::UNKNOWN);
}

inline void RecordedCreateSimilarDrawTarget::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr
                << "] CreateSimilarDrawTarget (Size: " << mSize.width << "x"
                << mSize.height << ")";
}

inline bool RecordedCreateDrawTargetForFilter::PlayEvent(
    Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  IntRect baseRect = dt->GetRect();

  auto maxRect = IntRect(IntPoint(0, 0), mMaxSize);

  auto clone = dt->GetTransform();
  bool invertible = clone.Invert();
  // mSourceRect is in filter space. The filter outputs from mSourceRect need
  // to be drawn at mDestPoint in user space.
  Rect userSpaceSource = Rect(mDestPoint, mSourceRect.Size());
  if (invertible) {
    // Try to reduce the source rect so that it's not much bigger
    // than the draw target. The result is not minimal. Examples
    // are left as an exercise for the reader.
    auto destRect = IntRectToRect(baseRect);
    Rect userSpaceBounds = clone.TransformBounds(destRect);
    userSpaceSource = userSpaceSource.Intersect(userSpaceBounds);
  }

  // Compute how much we moved the top-left of the source rect by, and use that
  // to compute the new dest point, and move our intersected source rect back
  // into the (new) filter space.
  Point shift = userSpaceSource.TopLeft() - mDestPoint;
  Rect filterSpaceSource =
      Rect(mSourceRect.TopLeft() + shift, userSpaceSource.Size());

  baseRect = RoundedOut(filterSpaceSource);
  FilterNode* filter = aTranslator->LookupFilterNode(mFilter);
  if (!filter) {
    return false;
  }

  IntRect transformedRect = filter->MapRectToSource(
      baseRect, maxRect, aTranslator->LookupFilterNode(mSource));

  // Intersect with maxRect to make sure we didn't end up with something bigger
  transformedRect = transformedRect.Intersect(maxRect);

  // If we end up with an empty rect make it 1x1 so that things don't break.
  if (transformedRect.IsEmpty()) {
    transformedRect = IntRect(0, 0, 1, 1);
  }

  RefPtr<DrawTarget> newDT =
      dt->CreateSimilarDrawTarget(transformedRect.Size(), mFormat);
  if (!newDT) {
    return false;
  }
  newDT =
      gfx::Factory::CreateOffsetDrawTarget(newDT, transformedRect.TopLeft());

  // If we couldn't create a DrawTarget this will probably cause us to crash
  // with nullptr later in the playback, so return false to abort.
  if (!newDT) {
    return false;
  }

  aTranslator->AddDrawTarget(mRefPtr, newDT);
  return true;
}

inline bool RecordedCreateClippedDrawTarget::PlayEvent(
    Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  RefPtr<DrawTarget> newDT = dt->CreateClippedDrawTarget(mBounds, mFormat);

  // If we couldn't create a DrawTarget this will probably cause us to crash
  // with nullptr later in the playback, so return false to abort.
  if (!newDT) {
    return false;
  }

  aTranslator->AddDrawTarget(mRefPtr, newDT);
  return true;
}

template <class S>
void RecordedCreateClippedDrawTarget::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mBounds);
  WriteElement(aStream, mFormat);
}

template <class S>
RecordedCreateClippedDrawTarget::RecordedCreateClippedDrawTarget(S& aStream)
    : RecordedEventDerived(CREATECLIPPEDDRAWTARGET) {
  ReadElement(aStream, mRefPtr);
  ReadElement(aStream, mBounds);
  ReadElementConstrained(aStream, mFormat, SurfaceFormat::B8G8R8A8,
                         SurfaceFormat::UNKNOWN);
}

inline void RecordedCreateClippedDrawTarget::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] CreateClippedDrawTarget ()";
}

template <class S>
void RecordedCreateDrawTargetForFilter::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mMaxSize);
  WriteElement(aStream, mFormat);
  WriteElement(aStream, mFilter);
  WriteElement(aStream, mSource);
  WriteElement(aStream, mSourceRect);
  WriteElement(aStream, mDestPoint);
}

template <class S>
RecordedCreateDrawTargetForFilter::RecordedCreateDrawTargetForFilter(S& aStream)
    : RecordedEventDerived(CREATEDRAWTARGETFORFILTER) {
  ReadElement(aStream, mRefPtr);
  ReadElement(aStream, mMaxSize);
  ReadElementConstrained(aStream, mFormat, SurfaceFormat::B8G8R8A8,
                         SurfaceFormat::UNKNOWN);
  ReadElement(aStream, mFilter);
  ReadElement(aStream, mSource);
  ReadElement(aStream, mSourceRect);
  ReadElement(aStream, mDestPoint);
}

inline void RecordedCreateDrawTargetForFilter::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] CreateDrawTargetForFilter ()";
}

struct GenericPattern {
  GenericPattern(const PatternStorage& aStorage, Translator* aTranslator)
      : mPattern(nullptr), mTranslator(aTranslator) {
    mStorage = const_cast<PatternStorage*>(&aStorage);
  }

  ~GenericPattern() {
    if (mPattern) {
      mPattern->~Pattern();
    }
  }

  operator Pattern*() {
    switch (mStorage->mType) {
      case PatternType::COLOR:
        return new (mColPat) ColorPattern(
            reinterpret_cast<ColorPatternStorage*>(&mStorage->mStorage)
                ->mColor);
      case PatternType::SURFACE: {
        SurfacePatternStorage* storage =
            reinterpret_cast<SurfacePatternStorage*>(&mStorage->mStorage);
        mPattern = new (mSurfPat)
            SurfacePattern(mTranslator->LookupSourceSurface(storage->mSurface),
                           storage->mExtend, storage->mMatrix,
                           storage->mSamplingFilter, storage->mSamplingRect);
        return mPattern;
      }
      case PatternType::LINEAR_GRADIENT: {
        LinearGradientPatternStorage* storage =
            reinterpret_cast<LinearGradientPatternStorage*>(
                &mStorage->mStorage);
        mPattern = new (mLinGradPat) LinearGradientPattern(
            storage->mBegin, storage->mEnd,
            storage->mStops ? mTranslator->LookupGradientStops(storage->mStops)
                            : nullptr,
            storage->mMatrix);
        return mPattern;
      }
      case PatternType::RADIAL_GRADIENT: {
        RadialGradientPatternStorage* storage =
            reinterpret_cast<RadialGradientPatternStorage*>(
                &mStorage->mStorage);
        mPattern = new (mRadGradPat) RadialGradientPattern(
            storage->mCenter1, storage->mCenter2, storage->mRadius1,
            storage->mRadius2,
            storage->mStops ? mTranslator->LookupGradientStops(storage->mStops)
                            : nullptr,
            storage->mMatrix);
        return mPattern;
      }
      case PatternType::CONIC_GRADIENT: {
        ConicGradientPatternStorage* storage =
            reinterpret_cast<ConicGradientPatternStorage*>(&mStorage->mStorage);
        mPattern = new (mConGradPat) ConicGradientPattern(
            storage->mCenter, storage->mAngle, storage->mStartOffset,
            storage->mEndOffset,
            storage->mStops ? mTranslator->LookupGradientStops(storage->mStops)
                            : nullptr,
            storage->mMatrix);
        return mPattern;
      }
      default:
        return new (mColPat) ColorPattern(DeviceColor());
    }

    return mPattern;
  }

  union {
    char mColPat[sizeof(ColorPattern)];
    char mLinGradPat[sizeof(LinearGradientPattern)];
    char mRadGradPat[sizeof(RadialGradientPattern)];
    char mConGradPat[sizeof(ConicGradientPattern)];
    char mSurfPat[sizeof(SurfacePattern)];
  };

  PatternStorage* mStorage;
  Pattern* mPattern;
  Translator* mTranslator;
};

inline bool RecordedFillRect::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->FillRect(mRect, *GenericPattern(mPattern, aTranslator), mOptions);
  return true;
}

template <class S>
void RecordedFillRect::Record(S& aStream) const {
  WriteElement(aStream, mRect);
  WriteElement(aStream, mOptions);
  RecordPatternData(aStream, mPattern);
}

template <class S>
RecordedFillRect::RecordedFillRect(S& aStream)
    : RecordedEventDerived(FILLRECT) {
  ReadElement(aStream, mRect);
  ReadDrawOptions(aStream, mOptions);
  ReadPatternData(aStream, mPattern);
}

inline void RecordedFillRect::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "FillRect (" << mRect.X() << ", " << mRect.Y() << " - "
                << mRect.Width() << " x " << mRect.Height() << ") ";
  OutputSimplePatternInfo(mPattern, aStringStream);
}

inline bool RecordedStrokeRect::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->StrokeRect(mRect, *GenericPattern(mPattern, aTranslator), mStrokeOptions,
                 mOptions);
  return true;
}

template <class S>
void RecordedStrokeRect::Record(S& aStream) const {
  WriteElement(aStream, mRect);
  WriteElement(aStream, mOptions);
  RecordPatternData(aStream, mPattern);
  RecordStrokeOptions(aStream, mStrokeOptions);
}

template <class S>
RecordedStrokeRect::RecordedStrokeRect(S& aStream)
    : RecordedEventDerived(STROKERECT) {
  ReadElement(aStream, mRect);
  ReadDrawOptions(aStream, mOptions);
  ReadPatternData(aStream, mPattern);
  ReadStrokeOptions(aStream, mStrokeOptions);
}

inline void RecordedStrokeRect::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "StrokeRect (" << mRect.X() << ", " << mRect.Y() << " - "
                << mRect.Width() << " x " << mRect.Height()
                << ") LineWidth: " << mStrokeOptions.mLineWidth << "px ";
  OutputSimplePatternInfo(mPattern, aStringStream);
}

inline bool RecordedStrokeLine::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->StrokeLine(mBegin, mEnd, *GenericPattern(mPattern, aTranslator),
                 mStrokeOptions, mOptions);
  return true;
}

template <class S>
void RecordedStrokeLine::Record(S& aStream) const {
  WriteElement(aStream, mBegin);
  WriteElement(aStream, mEnd);
  WriteElement(aStream, mOptions);
  RecordPatternData(aStream, mPattern);
  RecordStrokeOptions(aStream, mStrokeOptions);
}

template <class S>
RecordedStrokeLine::RecordedStrokeLine(S& aStream)
    : RecordedEventDerived(STROKELINE) {
  ReadElement(aStream, mBegin);
  ReadElement(aStream, mEnd);
  ReadDrawOptions(aStream, mOptions);
  ReadPatternData(aStream, mPattern);
  ReadStrokeOptions(aStream, mStrokeOptions);
}

inline void RecordedStrokeLine::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "StrokeLine (" << mBegin.x << ", " << mBegin.y << " - "
                << mEnd.x << ", " << mEnd.y
                << ") LineWidth: " << mStrokeOptions.mLineWidth << "px ";
  OutputSimplePatternInfo(mPattern, aStringStream);
}

inline bool RecordedStrokeCircle::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->StrokeCircle(mCircle.origin, mCircle.radius,
                   *GenericPattern(mPattern, aTranslator), mStrokeOptions,
                   mOptions);
  return true;
}

template <class S>
void RecordedStrokeCircle::Record(S& aStream) const {
  WriteElement(aStream, mCircle);
  WriteElement(aStream, mOptions);
  RecordPatternData(aStream, mPattern);
  RecordStrokeOptions(aStream, mStrokeOptions);
}

template <class S>
RecordedStrokeCircle::RecordedStrokeCircle(S& aStream)
    : RecordedEventDerived(STROKECIRCLE) {
  ReadElement(aStream, mCircle);
  ReadDrawOptions(aStream, mOptions);
  ReadPatternData(aStream, mPattern);
  ReadStrokeOptions(aStream, mStrokeOptions);
}

inline void RecordedStrokeCircle::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "StrokeCircle (" << mCircle.origin.x << ", "
                << mCircle.origin.y << " - " << mCircle.radius
                << ") LineWidth: " << mStrokeOptions.mLineWidth << "px ";
  OutputSimplePatternInfo(mPattern, aStringStream);
}

inline bool RecordedFill::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->Fill(aTranslator->LookupPath(mPath),
           *GenericPattern(mPattern, aTranslator), mOptions);
  return true;
}

template <class S>
RecordedFill::RecordedFill(S& aStream) : RecordedEventDerived(FILL) {
  ReadElement(aStream, mPath);
  ReadDrawOptions(aStream, mOptions);
  ReadPatternData(aStream, mPattern);
}

template <class S>
void RecordedFill::Record(S& aStream) const {
  WriteElement(aStream, mPath);
  WriteElement(aStream, mOptions);
  RecordPatternData(aStream, mPattern);
}

inline void RecordedFill::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "Fill (" << mPath << ") ";
  OutputSimplePatternInfo(mPattern, aStringStream);
}

inline bool RecordedFillCircle::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->FillCircle(mCircle.origin, mCircle.radius,
                 *GenericPattern(mPattern, aTranslator), mOptions);
  return true;
}

template <class S>
void RecordedFillCircle::Record(S& aStream) const {
  WriteElement(aStream, mCircle);
  WriteElement(aStream, mOptions);
  RecordPatternData(aStream, mPattern);
}

template <class S>
RecordedFillCircle::RecordedFillCircle(S& aStream)
    : RecordedEventDerived(FILLCIRCLE) {
  ReadElement(aStream, mCircle);
  ReadDrawOptions(aStream, mOptions);
  ReadPatternData(aStream, mPattern);
}

inline void RecordedFillCircle::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "FillCircle (" << mCircle.origin.x << ", "
                << mCircle.origin.y << " - " << mCircle.radius << ")";
  OutputSimplePatternInfo(mPattern, aStringStream);
}

template <class T>
inline bool RecordedDrawGlyphs<T>::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  ScaledFont* scaledFont = aTranslator->LookupScaledFont(mScaledFont);
  if (!scaledFont) {
    return false;
  }

  GlyphBuffer buffer;
  buffer.mGlyphs = mGlyphs.data();
  buffer.mNumGlyphs = mGlyphs.size();
  DrawGlyphs(dt, scaledFont, buffer, *GenericPattern(mPattern, aTranslator));
  return true;
}

template <class T>
template <class S>
RecordedDrawGlyphs<T>::RecordedDrawGlyphs(RecordedEvent::EventType aType,
                                          S& aStream)
    : RecordedEventDerived<T>(aType) {
  ReadElement(aStream, mScaledFont);
  ReadDrawOptions(aStream, mOptions);
  this->ReadPatternData(aStream, mPattern);
  uint32_t numGlyphs;
  ReadElement(aStream, numGlyphs);
  if (!aStream.good() || numGlyphs <= 0) {
    return;
  }

  if (!mGlyphs.Read(aStream, numGlyphs)) {
    gfxCriticalNote << "RecordedDrawGlyphs failed to allocate glyphs of size "
                    << numGlyphs;
    aStream.SetIsBad();
  }
}

template <class T>
template <class S>
void RecordedDrawGlyphs<T>::Record(S& aStream) const {
  WriteElement(aStream, mScaledFont);
  WriteElement(aStream, mOptions);
  this->RecordPatternData(aStream, mPattern);
  WriteElement(aStream, mGlyphs.size());
  mGlyphs.Write(aStream);
}

template <class T>
inline void RecordedDrawGlyphs<T>::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << this->GetName() << " (" << mScaledFont << ") ";
  this->OutputSimplePatternInfo(mPattern, aStringStream);
}

inline bool RecordedMask::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->Mask(*GenericPattern(mSource, aTranslator),
           *GenericPattern(mMask, aTranslator), mOptions);
  return true;
}

template <class S>
RecordedMask::RecordedMask(S& aStream) : RecordedEventDerived(MASK) {
  ReadDrawOptions(aStream, mOptions);
  ReadPatternData(aStream, mSource);
  ReadPatternData(aStream, mMask);
}

template <class S>
void RecordedMask::Record(S& aStream) const {
  WriteElement(aStream, mOptions);
  RecordPatternData(aStream, mSource);
  RecordPatternData(aStream, mMask);
}

inline void RecordedMask::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "Mask (Source: ";
  OutputSimplePatternInfo(mSource, aStringStream);
  aStringStream << " Mask: ";
  OutputSimplePatternInfo(mMask, aStringStream);
}

inline bool RecordedStroke::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  Path* path = aTranslator->LookupPath(mPath);
  if (!path) {
    return false;
  }

  dt->Stroke(path, *GenericPattern(mPattern, aTranslator), mStrokeOptions,
             mOptions);
  return true;
}

template <class S>
void RecordedStroke::Record(S& aStream) const {
  WriteElement(aStream, mPath);
  WriteElement(aStream, mOptions);
  RecordPatternData(aStream, mPattern);
  RecordStrokeOptions(aStream, mStrokeOptions);
}

template <class S>
RecordedStroke::RecordedStroke(S& aStream) : RecordedEventDerived(STROKE) {
  ReadElement(aStream, mPath);
  ReadDrawOptions(aStream, mOptions);
  ReadPatternData(aStream, mPattern);
  ReadStrokeOptions(aStream, mStrokeOptions);
}

inline void RecordedStroke::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "Stroke (" << mPath
                << ") LineWidth: " << mStrokeOptions.mLineWidth << "px ";
  OutputSimplePatternInfo(mPattern, aStringStream);
}

inline bool RecordedClearRect::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->ClearRect(mRect);
  return true;
}

template <class S>
void RecordedClearRect::Record(S& aStream) const {
  WriteElement(aStream, mRect);
}

template <class S>
RecordedClearRect::RecordedClearRect(S& aStream)
    : RecordedEventDerived(CLEARRECT) {
  ReadElement(aStream, mRect);
}

inline void RecordedClearRect::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "ClearRect (" << mRect.X() << ", " << mRect.Y() << " - "
                << mRect.Width() << " x " << mRect.Height() << ") ";
}

inline bool RecordedCopySurface::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  SourceSurface* surface = aTranslator->LookupSourceSurface(mSourceSurface);
  if (!surface) {
    return false;
  }

  dt->CopySurface(surface, mSourceRect, mDest);
  return true;
}

template <class S>
void RecordedCopySurface::Record(S& aStream) const {
  WriteElement(aStream, mSourceSurface);
  WriteElement(aStream, mSourceRect);
  WriteElement(aStream, mDest);
}

template <class S>
RecordedCopySurface::RecordedCopySurface(S& aStream)
    : RecordedEventDerived(COPYSURFACE) {
  ReadElement(aStream, mSourceSurface);
  ReadElement(aStream, mSourceRect);
  ReadElement(aStream, mDest);
}

inline void RecordedCopySurface::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "CopySurface (" << mSourceSurface << ")";
}

inline bool RecordedPushClip::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  Path* path = aTranslator->LookupPath(mPath);
  if (!path) {
    return false;
  }

  dt->PushClip(path);
  return true;
}

template <class S>
void RecordedPushClip::Record(S& aStream) const {
  WriteElement(aStream, mPath);
}

template <class S>
RecordedPushClip::RecordedPushClip(S& aStream)
    : RecordedEventDerived(PUSHCLIP) {
  ReadElement(aStream, mPath);
}

inline void RecordedPushClip::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "PushClip (" << mPath << ") ";
}

inline bool RecordedPushClipRect::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->PushClipRect(mRect);
  return true;
}

template <class S>
void RecordedPushClipRect::Record(S& aStream) const {
  WriteElement(aStream, mRect);
}

template <class S>
RecordedPushClipRect::RecordedPushClipRect(S& aStream)
    : RecordedEventDerived(PUSHCLIPRECT) {
  ReadElement(aStream, mRect);
}

inline void RecordedPushClipRect::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "PushClipRect (" << mRect.X() << ", " << mRect.Y() << " - "
                << mRect.Width() << " x " << mRect.Height() << ") ";
}

inline bool RecordedPopClip::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->PopClip();
  return true;
}

template <class S>
void RecordedPopClip::Record(S& aStream) const {}

template <class S>
RecordedPopClip::RecordedPopClip(S& aStream) : RecordedEventDerived(POPCLIP) {}

inline void RecordedPopClip::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "PopClip";
}

inline bool RecordedRemoveAllClips::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->RemoveAllClips();
  return true;
}

template <class S>
void RecordedRemoveAllClips::Record(S& aStream) const {}

template <class S>
RecordedRemoveAllClips::RecordedRemoveAllClips(S& aStream)
    : RecordedEventDerived(REMOVEALLCLIPS) {}

inline void RecordedRemoveAllClips::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "RemoveAllClips";
}

inline bool RecordedPushLayer::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  SourceSurface* mask =
      mMask ? aTranslator->LookupSourceSurface(mMask) : nullptr;
  dt->PushLayer(mOpaque, mOpacity, mask, mMaskTransform, mBounds,
                mCopyBackground);
  return true;
}

template <class S>
void RecordedPushLayer::Record(S& aStream) const {
  WriteElement(aStream, mOpaque);
  WriteElement(aStream, mOpacity);
  WriteElement(aStream, mMask);
  WriteElement(aStream, mMaskTransform);
  WriteElement(aStream, mBounds);
  WriteElement(aStream, mCopyBackground);
}

template <class S>
RecordedPushLayer::RecordedPushLayer(S& aStream)
    : RecordedEventDerived(PUSHLAYER) {
  ReadElement(aStream, mOpaque);
  ReadElement(aStream, mOpacity);
  ReadElement(aStream, mMask);
  ReadElement(aStream, mMaskTransform);
  ReadElement(aStream, mBounds);
  ReadElement(aStream, mCopyBackground);
}

inline void RecordedPushLayer::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "PushPLayer (Opaque=" << mOpaque << ", Opacity=" << mOpacity
                << ", Mask Ref=" << mMask << ") ";
}

inline bool RecordedPushLayerWithBlend::PlayEvent(
    Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  SourceSurface* mask =
      mMask ? aTranslator->LookupSourceSurface(mMask) : nullptr;
  dt->PushLayerWithBlend(mOpaque, mOpacity, mask, mMaskTransform, mBounds,
                         mCopyBackground, mCompositionOp);
  return true;
}

template <class S>
void RecordedPushLayerWithBlend::Record(S& aStream) const {
  WriteElement(aStream, mOpaque);
  WriteElement(aStream, mOpacity);
  WriteElement(aStream, mMask);
  WriteElement(aStream, mMaskTransform);
  WriteElement(aStream, mBounds);
  WriteElement(aStream, mCopyBackground);
  WriteElement(aStream, mCompositionOp);
}

template <class S>
RecordedPushLayerWithBlend::RecordedPushLayerWithBlend(S& aStream)
    : RecordedEventDerived(PUSHLAYERWITHBLEND) {
  ReadElement(aStream, mOpaque);
  ReadElement(aStream, mOpacity);
  ReadElement(aStream, mMask);
  ReadElement(aStream, mMaskTransform);
  ReadElement(aStream, mBounds);
  ReadElement(aStream, mCopyBackground);
  ReadElementConstrained(aStream, mCompositionOp, CompositionOp::OP_OVER,
                         CompositionOp::OP_COUNT);
}

inline void RecordedPushLayerWithBlend::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "PushLayerWithBlend (Opaque=" << mOpaque
                << ", Opacity=" << mOpacity << ", Mask Ref=" << mMask << ") ";
}

inline bool RecordedPopLayer::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->PopLayer();
  return true;
}

template <class S>
void RecordedPopLayer::Record(S& aStream) const {}

template <class S>
RecordedPopLayer::RecordedPopLayer(S& aStream)
    : RecordedEventDerived(POPLAYER) {}

inline void RecordedPopLayer::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "PopLayer";
}

inline bool RecordedSetPermitSubpixelAA::PlayEvent(
    Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->SetPermitSubpixelAA(mPermitSubpixelAA);
  return true;
}

template <class S>
void RecordedSetPermitSubpixelAA::Record(S& aStream) const {
  WriteElement(aStream, mPermitSubpixelAA);
}

template <class S>
RecordedSetPermitSubpixelAA::RecordedSetPermitSubpixelAA(S& aStream)
    : RecordedEventDerived(SETPERMITSUBPIXELAA) {
  ReadElement(aStream, mPermitSubpixelAA);
}

inline void RecordedSetPermitSubpixelAA::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "SetPermitSubpixelAA (" << mPermitSubpixelAA << ")";
}

inline bool RecordedSetTransform::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  // If we're drawing to the reference DT, then we need to manually apply
  // its initial transform, otherwise we'll just clobber it with only the
  // the transform that was visible to the code doing the recording.
  if (dt == aTranslator->GetReferenceDrawTarget()) {
    dt->SetTransform(mTransform *
                     aTranslator->GetReferenceDrawTargetTransform());
  } else {
    dt->SetTransform(mTransform);
  }

  return true;
}

template <class S>
void RecordedSetTransform::Record(S& aStream) const {
  WriteElement(aStream, mTransform);
}

template <class S>
RecordedSetTransform::RecordedSetTransform(S& aStream)
    : RecordedEventDerived(SETTRANSFORM) {
  ReadElement(aStream, mTransform);
}

inline void RecordedSetTransform::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "SetTransform [ " << mTransform._11 << " " << mTransform._12
                << " ; " << mTransform._21 << " " << mTransform._22 << " ; "
                << mTransform._31 << " " << mTransform._32 << " ]";
}

inline bool RecordedDrawSurface::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  SourceSurface* surface = aTranslator->LookupSourceSurface(mRefSource);
  if (!surface) {
    return false;
  }

  dt->DrawSurface(surface, mDest, mSource, mDSOptions, mOptions);
  return true;
}

template <class S>
void RecordedDrawSurface::Record(S& aStream) const {
  WriteElement(aStream, mRefSource);
  WriteElement(aStream, mDest);
  WriteElement(aStream, mSource);
  WriteElement(aStream, mDSOptions);
  WriteElement(aStream, mOptions);
}

template <class S>
RecordedDrawSurface::RecordedDrawSurface(S& aStream)
    : RecordedEventDerived(DRAWSURFACE) {
  ReadElement(aStream, mRefSource);
  ReadElement(aStream, mDest);
  ReadElement(aStream, mSource);
  ReadDrawSurfaceOptions(aStream, mDSOptions);
  ReadDrawOptions(aStream, mOptions);
}

inline void RecordedDrawSurface::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "DrawSurface (" << mRefSource << ")";
}

inline bool RecordedDrawSurfaceDescriptor::PlayEvent(
    Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  RefPtr<SourceSurface> surface =
      aTranslator->LookupSourceSurfaceFromSurfaceDescriptor(dt, mDesc);
  if (!surface) {
    return false;
  }

  RefPtr<SourceSurface> opt = dt->OptimizeSourceSurface(surface);
  if (opt) {
    surface = opt;
  }

  dt->DrawSurface(surface, mDest, mSource, mDSOptions, mOptions);
  return true;
}

template <class S>
struct ElementStreamFormat<S, layers::SurfaceDescriptor> {
  using T = layers::SurfaceDescriptor;

  static void Write(S& s, const T& t) {
    Maybe<T> valid;
    if (!dom::ValidSurfaceDescriptorForRemoteCanvas2d(t, &valid)) {
      MOZ_CRASH("Invalid surface descriptor for write");
    }
    MOZ_RELEASE_ASSERT(valid && *valid == t);

    nsTArray<char> buffer;
    mozilla::ipc::SerializeToBytesUtil(*valid, buffer);

    uint32_t size = buffer.Length();
    s.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (size > 0) {
      s.write(buffer.Elements(), size);
    }
  }

  [[nodiscard]] static bool Read(S& s, T& t) {
    uint32_t size;
    if (!s.read(reinterpret_cast<char*>(&size), sizeof(size))) {
      return false;
    }

    if (size == 0 || size > 1024 * 16) {
      s.SetIsBad();
      return false;
    }

    // TODO: We should revise/extend the API to avoid copying here.
    nsTArray<char> buffer;
    buffer.SetLength(size);
    if (!s.read(buffer.Elements(), size)) {
      return false;
    }

    auto result = mozilla::ipc::DeserializeFromBytesUtil<T>(buffer);
    if (!result) {
      s.SetIsBad();
      return false;
    }

    t = std::move(*result);

    if (!dom::ValidSurfaceDescriptorForRemoteCanvas2d(t)) {
      s.SetIsBad();
      return false;
    }

    return true;
  }
};

template <class S>
void RecordedDrawSurfaceDescriptor::Record(S& aStream) const {
  WriteElement(aStream, mDesc);
  WriteElement(aStream, mDest);
  WriteElement(aStream, mSource);
  WriteElement(aStream, mDSOptions);
  WriteElement(aStream, mOptions);
}

template <class S>
RecordedDrawSurfaceDescriptor::RecordedDrawSurfaceDescriptor(S& aStream)
    : RecordedEventDerived(DRAWSURFACEDESCRIPTOR) {
  ReadElement(aStream, mDesc);
  ReadElement(aStream, mDest);
  ReadElement(aStream, mSource);
  ReadDrawSurfaceOptions(aStream, mDSOptions);
  ReadDrawOptions(aStream, mOptions);
}

inline void RecordedDrawSurfaceDescriptor::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "DrawSurfaceDescriptor (" << mDesc.type() << ")";
}

inline bool RecordedDrawDependentSurface::PlayEvent(
    Translator* aTranslator) const {
  aTranslator->DrawDependentSurface(mId, mDest);
  return true;
}

template <class S>
void RecordedDrawDependentSurface::Record(S& aStream) const {
  WriteElement(aStream, mId);
  WriteElement(aStream, mDest);
}

template <class S>
RecordedDrawDependentSurface::RecordedDrawDependentSurface(S& aStream)
    : RecordedEventDerived(DRAWDEPENDENTSURFACE) {
  ReadElement(aStream, mId);
  ReadElement(aStream, mDest);
}

inline void RecordedDrawDependentSurface::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "DrawDependentSurface (" << mId << ")";
}

inline bool RecordedDrawFilter::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  FilterNode* filter = aTranslator->LookupFilterNode(mNode);
  if (!filter) {
    return false;
  }

  dt->DrawFilter(filter, mSourceRect, mDestPoint, mOptions);
  return true;
}

template <class S>
void RecordedDrawFilter::Record(S& aStream) const {
  WriteElement(aStream, mNode);
  WriteElement(aStream, mSourceRect);
  WriteElement(aStream, mDestPoint);
  WriteElement(aStream, mOptions);
}

template <class S>
RecordedDrawFilter::RecordedDrawFilter(S& aStream)
    : RecordedEventDerived(DRAWFILTER) {
  ReadElement(aStream, mNode);
  ReadElement(aStream, mSourceRect);
  ReadElement(aStream, mDestPoint);
  ReadDrawOptions(aStream, mOptions);
}

inline void RecordedDrawFilter::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "DrawFilter (" << mNode << ")";
}

inline bool RecordedDrawSurfaceWithShadow::PlayEvent(
    Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  SourceSurface* surface = aTranslator->LookupSourceSurface(mRefSource);
  if (!surface) {
    return false;
  }

  dt->DrawSurfaceWithShadow(surface, mDest, mShadow, mOp);
  return true;
}

template <class S>
void RecordedDrawSurfaceWithShadow::Record(S& aStream) const {
  WriteElement(aStream, mRefSource);
  WriteElement(aStream, mDest);
  WriteElement(aStream, mShadow);
  WriteElement(aStream, mOp);
}

template <class S>
RecordedDrawSurfaceWithShadow::RecordedDrawSurfaceWithShadow(S& aStream)
    : RecordedEventDerived(DRAWSURFACEWITHSHADOW) {
  ReadElement(aStream, mRefSource);
  ReadElement(aStream, mDest);
  ReadElement(aStream, mShadow);
  ReadElementConstrained(aStream, mOp, CompositionOp::OP_OVER,
                         CompositionOp::OP_COUNT);
}

inline void RecordedDrawSurfaceWithShadow::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "DrawSurfaceWithShadow (" << mRefSource << ") DeviceColor: ("
                << mShadow.mColor.r << ", " << mShadow.mColor.g << ", "
                << mShadow.mColor.b << ", " << mShadow.mColor.a << ")";
}

inline bool RecordedDrawShadow::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  Path* path = aTranslator->LookupPath(mPath);
  if (!path) {
    return false;
  }

  dt->DrawShadow(path, *GenericPattern(mPattern, aTranslator), mShadow,
                 mOptions, mHasStrokeOptions ? &mStrokeOptions : nullptr);
  return true;
}

template <class S>
void RecordedDrawShadow::Record(S& aStream) const {
  WriteElement(aStream, mPath);
  RecordPatternData(aStream, mPattern);
  WriteElement(aStream, mShadow);
  WriteElement(aStream, mOptions);
  WriteElement(aStream, mHasStrokeOptions);
  if (mHasStrokeOptions) {
    RecordStrokeOptions(aStream, mStrokeOptions);
  }
}

template <class S>
RecordedDrawShadow::RecordedDrawShadow(S& aStream)
    : RecordedEventDerived(DRAWSHADOW) {
  ReadElement(aStream, mPath);
  ReadPatternData(aStream, mPattern);
  ReadElement(aStream, mShadow);
  ReadDrawOptions(aStream, mOptions);
  ReadElement(aStream, mHasStrokeOptions);
  if (mHasStrokeOptions) {
    ReadStrokeOptions(aStream, mStrokeOptions);
  }
}

inline void RecordedDrawShadow::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "DrawShadow (" << mPath << ") DeviceColor: ("
                << mShadow.mColor.r << ", " << mShadow.mColor.g << ", "
                << mShadow.mColor.b << ", " << mShadow.mColor.a << ")";
}

inline RecordedPathCreation::RecordedPathCreation(PathRecording* aPath)
    : RecordedEventDerived(PATHCREATION),
      mRefPtr(aPath),
      mFillRule(aPath->mFillRule),
      mPath(aPath) {}

inline bool RecordedPathCreation::PlayEvent(Translator* aTranslator) const {
  DrawTarget* drawTarget = aTranslator->GetCurrentDrawTarget();
  if (!drawTarget) {
    return false;
  }

  RefPtr<PathBuilder> builder = drawTarget->CreatePathBuilder(mFillRule);
  if (!mPathOps->CheckedStreamToSink(*builder)) {
    return false;
  }

  RefPtr<Path> path = builder->Finish();
  aTranslator->AddPath(mRefPtr, path);
  return true;
}

template <class S>
void RecordedPathCreation::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mFillRule);
  mPath->mPathOps.Record(aStream);
}

template <class S>
RecordedPathCreation::RecordedPathCreation(S& aStream)
    : RecordedEventDerived(PATHCREATION) {
  ReadElement(aStream, mRefPtr);
  ReadElementConstrained(aStream, mFillRule, FillRule::FILL_WINDING,
                         FillRule::FILL_EVEN_ODD);
  mPathOps = MakeUnique<PathOps>(aStream);
}

inline void RecordedPathCreation::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  size_t numberOfOps =
      mPath ? mPath->mPathOps.NumberOfOps() : mPathOps->NumberOfOps();
  aStringStream << "[" << mRefPtr << "] Path created (OpCount: " << numberOfOps
                << ")";
}
inline bool RecordedPathDestruction::PlayEvent(Translator* aTranslator) const {
  aTranslator->RemovePath(mRefPtr);
  return true;
}

template <class S>
void RecordedPathDestruction::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
}

template <class S>
RecordedPathDestruction::RecordedPathDestruction(S& aStream)
    : RecordedEventDerived(PATHDESTRUCTION) {
  ReadElement(aStream, mRefPtr);
}

inline void RecordedPathDestruction::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] Path Destroyed";
}

inline bool RecordedSourceSurfaceCreation::PlayEvent(
    Translator* aTranslator) const {
  if (!mDataSurface) {
    return false;
  }

  aTranslator->AddSourceSurface(mRefPtr, mDataSurface);
  return true;
}

template <class S>
void RecordedSourceSurfaceCreation::Record(S& aStream) const {
  MOZ_ASSERT(mDataSurface);
  IntSize size = mDataSurface->GetSize();
  SurfaceFormat format = mDataSurface->GetFormat();
  WriteElement(aStream, mRefPtr);
  DataSourceSurface::ScopedMap map(mDataSurface, DataSourceSurface::READ);
  if (map.IsMapped()) {
    WriteElement(aStream, size);
    WriteElement(aStream, format);

    size_t dataFormatWidth = BytesPerPixel(format) * size.width;
    size_t stride = map.GetStride();
    const char* src = (const char*)map.GetData();
    const char* endSrc = src + stride * size.height;
    while (src < endSrc) {
      aStream.write(src, dataFormatWidth);
      src += stride;
    }
  } else {
    gfxWarning()
        << "RecordedSourceSurfaceCreation::Record failed to map surface";
    WriteElement(aStream, IntSize(1, 1));
    int bpp = BytesPerPixel(format);
    WriteElement(aStream, bpp <= 4 ? format : SurfaceFormat::B8G8R8A8);
    static const char zeroData[4] = {0, 0, 0, 0};
    aStream.write(zeroData, BytesPerPixel(format));
  }
}

template <class S>
RecordedSourceSurfaceCreation::RecordedSourceSurfaceCreation(S& aStream)
    : RecordedEventDerived(SOURCESURFACECREATION) {
  ReadElement(aStream, mRefPtr);
  IntSize size;
  ReadElement(aStream, size);
  SurfaceFormat format = SurfaceFormat::UNKNOWN;
  ReadElementConstrained(aStream, format, SurfaceFormat::B8G8R8A8,
                         SurfaceFormat::UNKNOWN);

  if (!Factory::AllowedSurfaceSize(size)) {
    gfxCriticalNote << "RecordedSourceSurfaceCreation read invalid size "
                    << size;
    aStream.SetIsBad();
  }

  if (!aStream.good()) {
    return;
  }

  mDataSurface = Factory::CreateDataSourceSurface(size, format);
  const char* failedAction = nullptr;
  if (mDataSurface) {
    DataSourceSurface::ScopedMap map(mDataSurface, DataSourceSurface::WRITE);
    if (!map.IsMapped()) {
      mDataSurface = nullptr;
      failedAction = "map";
    } else {
      char* data = (char*)map.GetData();
      size_t stride = map.GetStride();
      size_t dataFormatWidth = BytesPerPixel(format) * size.width;
      if (stride == dataFormatWidth) {
        if (!aStream.read(data, stride * size.height)) {
          failedAction = "read";
          mDataSurface = nullptr;
        }

      } else {
        for (int y = 0; y < size.height; y++) {
          if (!aStream.read(data, dataFormatWidth)) {
            failedAction = "read";
            mDataSurface = nullptr;
            break;
          }
          data += stride;
        }
      }
    }
  } else {
    failedAction = "allocate";
  }
  if (failedAction) {
    MOZ_ASSERT(!mDataSurface);
    gfxCriticalNote << "RecordedSourceSurfaceCreation failed to "
                    << failedAction << " data of size " << size.width << " x "
                    << size.height;
    aStream.SetIsBad();
  }
}

inline void RecordedSourceSurfaceCreation::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  if (mDataSurface) {
    IntSize size = mDataSurface->GetSize();
    aStringStream << "[" << mRefPtr
                  << "] SourceSurface created (Size: " << size.width << "x"
                  << size.height << ")";
  } else {
    aStringStream << "[" << mRefPtr << "] SourceSurface created (no data)";
  }
}

inline bool RecordedSourceSurfaceDestruction::PlayEvent(
    Translator* aTranslator) const {
  aTranslator->RemoveSourceSurface(mRefPtr);
  return true;
}

template <class S>
void RecordedSourceSurfaceDestruction::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
}

template <class S>
RecordedSourceSurfaceDestruction::RecordedSourceSurfaceDestruction(S& aStream)
    : RecordedEventDerived(SOURCESURFACEDESTRUCTION) {
  ReadElement(aStream, mRefPtr);
}

inline void RecordedSourceSurfaceDestruction::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] SourceSurface Destroyed";
}

inline bool RecordedOptimizeSourceSurface::PlayEvent(
    Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  SourceSurface* surface = aTranslator->LookupSourceSurface(mSurface);
  if (!surface) {
    return false;
  }

  RefPtr<SourceSurface> optimizedSurface = dt->OptimizeSourceSurface(surface);
  aTranslator->AddSourceSurface(mOptimizedSurface, optimizedSurface);
  return true;
}

template <class S>
void RecordedOptimizeSourceSurface::Record(S& aStream) const {
  WriteElement(aStream, mSurface);
  WriteElement(aStream, mOptimizedSurface);
}

template <class S>
RecordedOptimizeSourceSurface::RecordedOptimizeSourceSurface(S& aStream)
    : RecordedEventDerived(OPTIMIZESOURCESURFACE) {
  ReadElement(aStream, mSurface);
  ReadElement(aStream, mOptimizedSurface);
}

inline void RecordedOptimizeSourceSurface::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mSurface << "] Surface Optimized";
}

inline bool RecordedExternalSurfaceCreation::PlayEvent(
    Translator* aTranslator) const {
  RefPtr<SourceSurface> surface = aTranslator->LookupExternalSurface(mKey);
  if (!surface) {
    return false;
  }

  aTranslator->AddSourceSurface(mRefPtr, surface);
  return true;
}

template <class S>
void RecordedExternalSurfaceCreation::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mKey);
}

template <class S>
RecordedExternalSurfaceCreation::RecordedExternalSurfaceCreation(S& aStream)
    : RecordedEventDerived(EXTERNALSURFACECREATION) {
  ReadElement(aStream, mRefPtr);
  ReadElement(aStream, mKey);
}

inline void RecordedExternalSurfaceCreation::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr
                << "] SourceSurfaceSharedData created (Key: " << mKey << ")";
}

inline RecordedFilterNodeCreation::~RecordedFilterNodeCreation() = default;

inline bool RecordedFilterNodeCreation::PlayEvent(
    Translator* aTranslator) const {
  DrawTarget* drawTarget = aTranslator->GetCurrentDrawTarget();
  if (!drawTarget) {
    return false;
  }

  RefPtr<FilterNode> node = drawTarget->CreateFilter(mType);
  aTranslator->AddFilterNode(mRefPtr, node);
  return true;
}

template <class S>
void RecordedFilterNodeCreation::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mType);
}

template <class S>
RecordedFilterNodeCreation::RecordedFilterNodeCreation(S& aStream)
    : RecordedEventDerived(FILTERNODECREATION) {
  ReadElement(aStream, mRefPtr);
  ReadElementConstrained(aStream, mType, FilterType::BLEND,
                         FilterType::OPACITY);
}

inline void RecordedFilterNodeCreation::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "CreateFilter [" << mRefPtr
                << "] FilterNode created (Type: " << int(mType) << ")";
}

inline bool RecordedDeferFilterInput::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  Path* path = aTranslator->LookupPath(mPath);
  if (!path) {
    return false;
  }

  RefPtr<FilterNode> node = dt->DeferFilterInput(
      path, *GenericPattern(mPattern, aTranslator), mSourceRect, mDestOffset,
      mOptions, mHasStrokeOptions ? &mStrokeOptions : nullptr);
  aTranslator->AddFilterNode(mRefPtr, node);
  return true;
}

template <class S>
void RecordedDeferFilterInput::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mPath);
  RecordPatternData(aStream, mPattern);
  WriteElement(aStream, mSourceRect);
  WriteElement(aStream, mDestOffset);
  WriteElement(aStream, mOptions);
  WriteElement(aStream, mHasStrokeOptions);
  if (mHasStrokeOptions) {
    RecordStrokeOptions(aStream, mStrokeOptions);
  }
}

template <class S>
RecordedDeferFilterInput::RecordedDeferFilterInput(S& aStream)
    : RecordedEventDerived(DEFERFILTERINPUT) {
  ReadElement(aStream, mRefPtr);
  ReadElement(aStream, mPath);
  ReadPatternData(aStream, mPattern);
  ReadElement(aStream, mSourceRect);
  ReadElement(aStream, mDestOffset);
  ReadDrawOptions(aStream, mOptions);
  ReadElement(aStream, mHasStrokeOptions);
  if (mHasStrokeOptions) {
    ReadStrokeOptions(aStream, mStrokeOptions);
  }
}

inline void RecordedDeferFilterInput::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "DeferFilterInput[" << mRefPtr << "] (" << mPath << ") ";
  OutputSimplePatternInfo(mPattern, aStringStream);
}

inline bool RecordedFilterNodeDestruction::PlayEvent(
    Translator* aTranslator) const {
  aTranslator->RemoveFilterNode(mRefPtr);
  return true;
}

template <class S>
void RecordedFilterNodeDestruction::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
}

template <class S>
RecordedFilterNodeDestruction::RecordedFilterNodeDestruction(S& aStream)
    : RecordedEventDerived(FILTERNODEDESTRUCTION) {
  ReadElement(aStream, mRefPtr);
}

inline void RecordedFilterNodeDestruction::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] FilterNode Destroyed";
}

inline RecordedGradientStopsCreation::~RecordedGradientStopsCreation() {
  if (mDataOwned) {
    delete[] mStops;
  }
}

inline bool RecordedGradientStopsCreation::PlayEvent(
    Translator* aTranslator) const {
  if (mNumStops > 0 && !mStops) {
    // Stops allocation failed
    return false;
  }

  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  RefPtr<GradientStops> src =
      aTranslator->GetOrCreateGradientStops(dt, mStops, mNumStops, mExtendMode);
  aTranslator->AddGradientStops(mRefPtr, src);
  return true;
}

template <class S>
void RecordedGradientStopsCreation::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mExtendMode);
  WriteElement(aStream, mNumStops);
  aStream.write((const char*)mStops, mNumStops * sizeof(GradientStop));
}

template <class S>
RecordedGradientStopsCreation::RecordedGradientStopsCreation(S& aStream)
    : RecordedEventDerived(GRADIENTSTOPSCREATION), mDataOwned(true) {
  ReadElement(aStream, mRefPtr);
  ReadElementConstrained(aStream, mExtendMode, ExtendMode::CLAMP,
                         ExtendMode::REFLECT);
  ReadElement(aStream, mNumStops);
  if (!aStream.good() || mNumStops <= 0) {
    return;
  }

  mStops = new (fallible) GradientStop[mNumStops];
  if (!mStops) {
    gfxCriticalNote
        << "RecordedGradientStopsCreation failed to allocate stops of size "
        << mNumStops;
    aStream.SetIsBad();
  } else {
    (void)aStream.read((char*)mStops, mNumStops * sizeof(GradientStop));
    // GradientStop is a non-scalar type, it's default initialized, it's safe
    // to ignore the deserialization here.
  }
}

inline void RecordedGradientStopsCreation::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr
                << "] GradientStops created (Stops: " << mNumStops << ")";
}

inline bool RecordedGradientStopsDestruction::PlayEvent(
    Translator* aTranslator) const {
  aTranslator->RemoveGradientStops(mRefPtr);
  return true;
}

template <class S>
void RecordedGradientStopsDestruction::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
}

template <class S>
RecordedGradientStopsDestruction::RecordedGradientStopsDestruction(S& aStream)
    : RecordedEventDerived(GRADIENTSTOPSDESTRUCTION) {
  ReadElement(aStream, mRefPtr);
}

inline void RecordedGradientStopsDestruction::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] GradientStops Destroyed";
}

inline bool RecordedIntoLuminanceSource::PlayEvent(
    Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  RefPtr<SourceSurface> src = dt->IntoLuminanceSource(mLuminanceType, mOpacity);
  aTranslator->AddSourceSurface(mRefPtr, src);
  return true;
}

template <class S>
void RecordedIntoLuminanceSource::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mLuminanceType);
  WriteElement(aStream, mOpacity);
}

template <class S>
RecordedIntoLuminanceSource::RecordedIntoLuminanceSource(S& aStream)
    : RecordedEventDerived(INTOLUMINANCE) {
  ReadElement(aStream, mRefPtr);
  ReadElementConstrained(aStream, mLuminanceType, LuminanceType::LUMINANCE,
                         LuminanceType::LINEARRGB);
  ReadElement(aStream, mOpacity);
}

inline void RecordedIntoLuminanceSource::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] Into Luminance Source";
}

inline bool RecordedExtractSubrect::PlayEvent(Translator* aTranslator) const {
  SourceSurface* sourceSurf = aTranslator->LookupSourceSurface(mSourceSurface);
  if (!sourceSurf) {
    return false;
  }

  RefPtr<SourceSurface> subSurf = sourceSurf->ExtractSubrect(mSubrect);
  if (!subSurf) {
    RefPtr<DrawTarget> dt =
        aTranslator->GetReferenceDrawTarget()->CreateSimilarDrawTarget(
            mSubrect.Size(), sourceSurf->GetFormat());
    if (dt) {
      dt->CopySurface(sourceSurf, mSubrect, IntPoint());
      subSurf = dt->Snapshot();
    }
  }
  if (!subSurf) {
    return false;
  }

  aTranslator->AddSourceSurface(mRefPtr, subSurf);
  return true;
}

template <class S>
void RecordedExtractSubrect::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mSourceSurface);
  WriteElement(aStream, mSubrect);
}

template <class S>
RecordedExtractSubrect::RecordedExtractSubrect(S& aStream)
    : RecordedEventDerived(EXTRACTSUBRECT) {
  ReadElement(aStream, mRefPtr);
  ReadElement(aStream, mSourceSurface);
  ReadElement(aStream, mSubrect);
}

inline void RecordedExtractSubrect::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] Exract Subrect";
}

inline bool RecordedFlush::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->Flush();
  return true;
}

template <class S>
void RecordedFlush::Record(S& aStream) const {}

template <class S>
RecordedFlush::RecordedFlush(S& aStream) : RecordedEventDerived(FLUSH) {}

inline void RecordedFlush::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "Flush";
}

inline bool RecordedDetachAllSnapshots::PlayEvent(
    Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  dt->DetachAllSnapshots();
  return true;
}

template <class S>
void RecordedDetachAllSnapshots::Record(S& aStream) const {}

template <class S>
RecordedDetachAllSnapshots::RecordedDetachAllSnapshots(S& aStream)
    : RecordedEventDerived(DETACHALLSNAPSHOTS) {}

inline void RecordedDetachAllSnapshots::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "DetachAllSnapshots";
}

inline bool RecordedSnapshot::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  RefPtr<SourceSurface> src = dt->Snapshot();
  aTranslator->AddSourceSurface(mRefPtr, src);
  return true;
}

template <class S>
void RecordedSnapshot::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
}

template <class S>
RecordedSnapshot::RecordedSnapshot(S& aStream)
    : RecordedEventDerived(SNAPSHOT) {
  ReadElement(aStream, mRefPtr);
}

inline void RecordedSnapshot::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] Snapshot Created";
}

inline bool RecordedFontData::PlayEvent(Translator* aTranslator) const {
  if (mData.empty()) {
    return false;
  }

  RefPtr<NativeFontResource> fontResource = Factory::CreateNativeFontResource(
      mData.data(), mData.size(), mType, aTranslator->GetFontContext());
  if (!fontResource) {
    return false;
  }

  aTranslator->AddNativeFontResource(mFontDetails.fontDataKey, fontResource);
  return true;
}

template <class S>
void RecordedFontData::Record(S& aStream) const {
  MOZ_ASSERT(mGetFontFileDataSucceeded);

  WriteElement(aStream, mType);
  WriteElement(aStream, mFontDetails.fontDataKey);
  WriteElement(aStream, mData.size());
  mData.Write(aStream);
}

inline void RecordedFontData::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "Font Data of size " << mData.size();
}

inline void RecordedFontData::SetFontData(const uint8_t* aData, uint32_t aSize,
                                          uint32_t aIndex) {
  if (!mData.TryAssign(aData, aSize)) {
    mGetFontFileDataSucceeded = false;
    gfxCriticalNote
        << "RecordedFontData failed to allocate data for recording of size "
        << aSize;
  }
  mFontDetails.fontDataKey = SFNTData::GetUniqueKey(aData, aSize, 0, nullptr);
  mFontDetails.index = aIndex;
}

inline bool RecordedFontData::GetFontDetails(RecordedFontDetails& fontDetails) {
  if (!mGetFontFileDataSucceeded) {
    return false;
  }

  fontDetails.fontDataKey = mFontDetails.fontDataKey;
  fontDetails.index = mFontDetails.index;
  return true;
}

template <class S>
RecordedFontData::RecordedFontData(S& aStream)
    : RecordedEventDerived(FONTDATA), mType(FontType::UNKNOWN) {
  ReadElementConstrained(aStream, mType, FontType::DWRITE, FontType::UNKNOWN);
  ReadElement(aStream, mFontDetails.fontDataKey);
  uint32_t dataSize;
  ReadElement(aStream, dataSize);
  if (!dataSize || !aStream.good()) {
    return;
  }

  if (!mData.Read(aStream, dataSize)) {
    gfxCriticalNote
        << "RecordedFontData failed to allocate data for playback of size "
        << dataSize;
    aStream.SetIsBad();
  }
}

inline RecordedFontDescriptor::~RecordedFontDescriptor() = default;

inline bool RecordedFontDescriptor::PlayEvent(Translator* aTranslator) const {
  RefPtr<UnscaledFont> font = Factory::CreateUnscaledFontFromFontDescriptor(
      mType, mData.data(), mData.size(), mIndex);
  if (!font) {
    gfxCriticalNote << "Failed creating UnscaledFont of type " << int(mType)
                    << " from font descriptor";
    return false;
  }

  aTranslator->AddUnscaledFont(mRefPtr, font);
  return true;
}

template <class S>
void RecordedFontDescriptor::Record(S& aStream) const {
  MOZ_ASSERT(mHasDesc);
  WriteElement(aStream, mType);
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mIndex);
  WriteElement(aStream, mData.size());
  mData.Write(aStream);
}

inline void RecordedFontDescriptor::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] Font Descriptor";
}

inline void RecordedFontDescriptor::SetFontDescriptor(const uint8_t* aData,
                                                      uint32_t aSize,
                                                      uint32_t aIndex) {
  mData.Assign(aData, aSize);
  mIndex = aIndex;
}

template <class S>
RecordedFontDescriptor::RecordedFontDescriptor(S& aStream)
    : RecordedEventDerived(FONTDESC) {
  ReadElementConstrained(aStream, mType, FontType::DWRITE, FontType::UNKNOWN);
  ReadElement(aStream, mRefPtr);
  ReadElement(aStream, mIndex);

  size_t size;
  ReadElement(aStream, size);
  if (!aStream.good()) {
    return;
  }
  if (size && !mData.Read(aStream, size)) {
    aStream.SetIsBad();
    return;
  }
}

inline bool RecordedUnscaledFontCreation::PlayEvent(
    Translator* aTranslator) const {
  NativeFontResource* fontResource =
      aTranslator->LookupNativeFontResource(mFontDataKey);
  if (!fontResource) {
    gfxDevCrash(LogReason::NativeFontResourceNotFound)
        << "NativeFontResource lookup failed for key |" << hexa(mFontDataKey)
        << "|.";
    return false;
  }

  RefPtr<UnscaledFont> unscaledFont = fontResource->CreateUnscaledFont(
      mIndex, mInstanceData.data(), mInstanceData.size());
  aTranslator->AddUnscaledFont(mRefPtr, unscaledFont);
  return true;
}

template <class S>
void RecordedUnscaledFontCreation::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mFontDataKey);
  WriteElement(aStream, mIndex);
  WriteElement(aStream, mInstanceData.size());
  mInstanceData.Write(aStream);
}

inline void RecordedUnscaledFontCreation::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] UnscaledFont Created";
}

inline void RecordedUnscaledFontCreation::SetFontInstanceData(
    const uint8_t* aData, uint32_t aSize) {
  if (aSize) {
    mInstanceData.Assign(aData, aSize);
  }
}

template <class S>
RecordedUnscaledFontCreation::RecordedUnscaledFontCreation(S& aStream)
    : RecordedEventDerived(UNSCALEDFONTCREATION) {
  ReadElement(aStream, mRefPtr);
  ReadElement(aStream, mFontDataKey);
  ReadElement(aStream, mIndex);

  size_t size;
  ReadElement(aStream, size);
  if (!aStream.good()) {
    return;
  }
  if (size && !mInstanceData.Read(aStream, size)) {
    aStream.SetIsBad();
    return;
  }
}

inline bool RecordedUnscaledFontDestruction::PlayEvent(
    Translator* aTranslator) const {
  aTranslator->RemoveUnscaledFont(mRefPtr);
  return true;
}

template <class S>
void RecordedUnscaledFontDestruction::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
}

template <class S>
RecordedUnscaledFontDestruction::RecordedUnscaledFontDestruction(S& aStream)
    : RecordedEventDerived(UNSCALEDFONTDESTRUCTION) {
  ReadElement(aStream, mRefPtr);
}

inline void RecordedUnscaledFontDestruction::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] UnscaledFont Destroyed";
}

inline bool RecordedScaledFontCreation::PlayEvent(
    Translator* aTranslator) const {
  UnscaledFont* unscaledFont = aTranslator->LookupUnscaledFont(mUnscaledFont);
  if (!unscaledFont) {
    gfxDevCrash(LogReason::UnscaledFontNotFound)
        << "UnscaledFont lookup failed for key |" << hexa(mUnscaledFont)
        << "|.";
    return false;
  }

  RefPtr<ScaledFont> scaledFont = unscaledFont->CreateScaledFont(
      mGlyphSize, mInstanceData.data(), mInstanceData.size(),
      mVariations.data(), mVariations.size());

  aTranslator->AddScaledFont(mRefPtr, scaledFont);
  return true;
}

template <class S>
void RecordedScaledFontCreation::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
  WriteElement(aStream, mUnscaledFont);
  WriteElement(aStream, mGlyphSize);
  WriteElement(aStream, mInstanceData.size());
  mInstanceData.Write(aStream);
  WriteElement(aStream, mVariations.size());
  mVariations.Write(aStream);
}

inline void RecordedScaledFontCreation::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] ScaledFont Created";
}

inline void RecordedScaledFontCreation::SetFontInstanceData(
    const uint8_t* aData, uint32_t aSize, const FontVariation* aVariations,
    uint32_t aNumVariations) {
  if (aSize) {
    mInstanceData.Assign(aData, aSize);
  }
  if (aNumVariations) {
    mVariations.Assign(aVariations, aNumVariations);
  }
}

template <class S>
RecordedScaledFontCreation::RecordedScaledFontCreation(S& aStream)
    : RecordedEventDerived(SCALEDFONTCREATION) {
  ReadElement(aStream, mRefPtr);
  ReadElement(aStream, mUnscaledFont);
  ReadElement(aStream, mGlyphSize);

  size_t size;
  ReadElement(aStream, size);
  if (!aStream.good()) {
    return;
  }
  if (size && !mInstanceData.Read(aStream, size)) {
    aStream.SetIsBad();
    return;
  }

  size_t numVariations;
  ReadElement(aStream, numVariations);
  if (!aStream.good()) {
    return;
  }
  if (numVariations && !mVariations.Read(aStream, numVariations)) {
    aStream.SetIsBad();
    return;
  }
}

inline bool RecordedScaledFontDestruction::PlayEvent(
    Translator* aTranslator) const {
  aTranslator->RemoveScaledFont(mRefPtr);
  return true;
}

template <class S>
void RecordedScaledFontDestruction::Record(S& aStream) const {
  WriteElement(aStream, mRefPtr);
}

template <class S>
RecordedScaledFontDestruction::RecordedScaledFontDestruction(S& aStream)
    : RecordedEventDerived(SCALEDFONTDESTRUCTION) {
  ReadElement(aStream, mRefPtr);
}

inline void RecordedScaledFontDestruction::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mRefPtr << "] ScaledFont Destroyed";
}

inline bool RecordedMaskSurface::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }

  SourceSurface* surface = aTranslator->LookupSourceSurface(mRefMask);
  if (!surface) {
    return false;
  }

  dt->MaskSurface(*GenericPattern(mPattern, aTranslator), surface, mOffset,
                  mOptions);
  return true;
}

template <class S>
void RecordedMaskSurface::Record(S& aStream) const {
  RecordPatternData(aStream, mPattern);
  WriteElement(aStream, mRefMask);
  WriteElement(aStream, mOffset);
  WriteElement(aStream, mOptions);
}

template <class S>
RecordedMaskSurface::RecordedMaskSurface(S& aStream)
    : RecordedEventDerived(MASKSURFACE) {
  ReadPatternData(aStream, mPattern);
  ReadElement(aStream, mRefMask);
  ReadElement(aStream, mOffset);
  ReadDrawOptions(aStream, mOptions);
}

inline void RecordedMaskSurface::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "MaskSurface (" << mRefMask << ")  Offset: (" << mOffset.x
                << "x" << mOffset.y << ") Pattern: ";
  OutputSimplePatternInfo(mPattern, aStringStream);
}

inline bool RecordedFilterNodeSetAttribute::PlayEvent(
    Translator* aTranslator) const {
  FilterNode* node = aTranslator->LookupFilterNode(mNode);
  if (!node) {
    return false;
  }

#define REPLAY_SET_ATTRIBUTE_CAST(type, cast, argtype)         \
  case ARGTYPE_##argtype:                                      \
    if (mPayload.size() < sizeof(type)) {                      \
      return false;                                            \
    }                                                          \
    node->SetAttribute(mIndex, cast(*(type*)mPayload.data())); \
    break
#define REPLAY_SET_ATTRIBUTE(type, argtype) \
  REPLAY_SET_ATTRIBUTE_CAST(type, , argtype)

  switch (mArgType) {
    REPLAY_SET_ATTRIBUTE_CAST(uint8_t, bool, BOOL);
    REPLAY_SET_ATTRIBUTE(uint32_t, UINT32);
    REPLAY_SET_ATTRIBUTE(Float, FLOAT);
    REPLAY_SET_ATTRIBUTE(Size, SIZE);
    REPLAY_SET_ATTRIBUTE(IntSize, INTSIZE);
    REPLAY_SET_ATTRIBUTE(IntPoint, INTPOINT);
    REPLAY_SET_ATTRIBUTE(Rect, RECT);
    REPLAY_SET_ATTRIBUTE(IntRect, INTRECT);
    REPLAY_SET_ATTRIBUTE(Point, POINT);
    REPLAY_SET_ATTRIBUTE(Matrix, MATRIX);
    REPLAY_SET_ATTRIBUTE(Matrix5x4, MATRIX5X4);
    REPLAY_SET_ATTRIBUTE(Point3D, POINT3D);
    REPLAY_SET_ATTRIBUTE(DeviceColor, COLOR);
    case ARGTYPE_FLOAT_ARRAY:
      node->SetAttribute(mIndex,
                         reinterpret_cast<const Float*>(mPayload.data()),
                         mPayload.size() / sizeof(Float));
      break;
  }

  return true;
}

template <class S>
void RecordedFilterNodeSetAttribute::Record(S& aStream) const {
  WriteElement(aStream, mNode);
  WriteElement(aStream, mIndex);
  WriteElement(aStream, mArgType);
  WriteElement(aStream, mPayload.size());
  mPayload.Write(aStream);
}

template <class S>
RecordedFilterNodeSetAttribute::RecordedFilterNodeSetAttribute(S& aStream)
    : RecordedEventDerived(FILTERNODESETATTRIBUTE) {
  ReadElement(aStream, mNode);
  ReadElement(aStream, mIndex);
  ReadElementConstrained(aStream, mArgType, ArgType::ARGTYPE_UINT32,
                         ArgType::ARGTYPE_FLOAT_ARRAY);
  size_t size;
  ReadElement(aStream, size);
  if (!aStream.good()) {
    return;
  }

  if (size && !mPayload.Read(aStream, size)) {
    aStream.SetIsBad();
    return;
  }
}

inline void RecordedFilterNodeSetAttribute::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mNode << "] SetAttribute (" << mIndex << ")";
}

inline bool RecordedFilterNodeSetInput::PlayEvent(
    Translator* aTranslator) const {
  FilterNode* node = aTranslator->LookupFilterNode(mNode);
  if (!node) {
    return false;
  }

  if (mInputFilter) {
    node->SetInput(mIndex, aTranslator->LookupFilterNode(mInputFilter));
  } else {
    node->SetInput(mIndex, aTranslator->LookupSourceSurface(mInputSurface));
  }

  return true;
}

template <class S>
void RecordedFilterNodeSetInput::Record(S& aStream) const {
  WriteElement(aStream, mNode);
  WriteElement(aStream, mIndex);
  WriteElement(aStream, mInputFilter);
  WriteElement(aStream, mInputSurface);
}

template <class S>
RecordedFilterNodeSetInput::RecordedFilterNodeSetInput(S& aStream)
    : RecordedEventDerived(FILTERNODESETINPUT) {
  ReadElement(aStream, mNode);
  ReadElement(aStream, mIndex);
  ReadElement(aStream, mInputFilter);
  ReadElement(aStream, mInputSurface);
}

inline void RecordedFilterNodeSetInput::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "[" << mNode << "] SetAttribute (" << mIndex << ", ";

  if (mInputFilter) {
    aStringStream << "Filter: " << mInputFilter;
  } else {
    aStringStream << "Surface: " << mInputSurface;
  }

  aStringStream << ")";
}

inline bool RecordedLink::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }
  dt->Link(mLocalDest.data(), mURI.data(), mRect);
  return true;
}

template <class S>
void RecordedLink::Record(S& aStream) const {
  WriteElement(aStream, mRect);
  WriteElement(aStream, mLocalDest.size());
  mLocalDest.Write(aStream);
  WriteElement(aStream, mURI.size());
  mURI.Write(aStream);
}

template <class S>
RecordedLink::RecordedLink(S& aStream) : RecordedEventDerived(LINK) {
  ReadElement(aStream, mRect);
  size_t localDestLen;
  ReadElement(aStream, localDestLen);
  if (!aStream.good() ||
      (localDestLen && !mLocalDest.Read(aStream, localDestLen))) {
    aStream.SetIsBad();
    return;
  }
  size_t uriLen;
  ReadElement(aStream, uriLen);
  if (!aStream.good() || (uriLen && !mURI.Read(aStream, uriLen))) {
    aStream.SetIsBad();
    return;
  }
}

inline void RecordedLink::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  if (mLocalDest.empty()) {
    aStringStream << "Link [" << mURI.data() << " @ " << mRect << "]";
  } else {
    aStringStream << "Link [" << mLocalDest.data() << " / " << mURI.data()
                  << " @ " << mRect << "]";
  }
}

inline bool RecordedDestination::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }
  dt->Destination(mDestination.data(), mPoint);
  return true;
}

template <class S>
void RecordedDestination::Record(S& aStream) const {
  WriteElement(aStream, mPoint);
  WriteElement(aStream, mDestination.size());
  mDestination.Write(aStream);
}

template <class S>
RecordedDestination::RecordedDestination(S& aStream)
    : RecordedEventDerived(DESTINATION) {
  ReadElement(aStream, mPoint);
  size_t len;
  ReadElement(aStream, len);
  if (!aStream.good() || (len && !mDestination.Read(aStream, len))) {
    aStream.SetIsBad();
    return;
  }
}

inline void RecordedDestination::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "Destination [" << mDestination.data() << " @ " << mPoint
                << "]";
}

inline bool RecordedAccessibleId::PlayEvent(Translator* aTranslator) const {
  DrawTarget* dt = aTranslator->GetCurrentDrawTarget();
  if (!dt) {
    return false;
  }
  dt->AccessibleId(mBrowsingContextId, mAccId);
  return true;
}

template <class S>
void RecordedAccessibleId::Record(S& aStream) const {
  WriteElement(aStream, mBrowsingContextId);
  WriteElement(aStream, mAccId);
}

template <class S>
RecordedAccessibleId::RecordedAccessibleId(S& aStream)
    : RecordedEventDerived(ACCESSIBLEID) {
  ReadElement(aStream, mBrowsingContextId);
  ReadElement(aStream, mAccId);
}

inline void RecordedAccessibleId::OutputSimpleEventInfo(
    std::stringstream& aStringStream) const {
  aStringStream << "AccessibleId [" << mBrowsingContextId << ", " << mAccId
                << "]";
}

#define FOR_EACH_EVENT(f)                                          \
  f(DRAWTARGETCREATION, RecordedDrawTargetCreation);               \
  f(DRAWTARGETDESTRUCTION, RecordedDrawTargetDestruction);         \
  f(SETCURRENTDRAWTARGET, RecordedSetCurrentDrawTarget);           \
  f(FILLRECT, RecordedFillRect);                                   \
  f(STROKERECT, RecordedStrokeRect);                               \
  f(STROKELINE, RecordedStrokeLine);                               \
  f(STROKECIRCLE, RecordedStrokeCircle);                           \
  f(CLEARRECT, RecordedClearRect);                                 \
  f(COPYSURFACE, RecordedCopySurface);                             \
  f(SETPERMITSUBPIXELAA, RecordedSetPermitSubpixelAA);             \
  f(SETTRANSFORM, RecordedSetTransform);                           \
  f(PUSHCLIPRECT, RecordedPushClipRect);                           \
  f(PUSHCLIP, RecordedPushClip);                                   \
  f(POPCLIP, RecordedPopClip);                                     \
  f(REMOVEALLCLIPS, RecordedRemoveAllClips);                       \
  f(FILL, RecordedFill);                                           \
  f(FILLCIRCLE, RecordedFillCircle);                               \
  f(FILLGLYPHS, RecordedFillGlyphs);                               \
  f(STROKEGLYPHS, RecordedStrokeGlyphs);                           \
  f(MASK, RecordedMask);                                           \
  f(STROKE, RecordedStroke);                                       \
  f(DRAWSURFACE, RecordedDrawSurface);                             \
  f(DRAWSURFACEDESCRIPTOR, RecordedDrawSurfaceDescriptor);         \
  f(DRAWDEPENDENTSURFACE, RecordedDrawDependentSurface);           \
  f(DRAWSURFACEWITHSHADOW, RecordedDrawSurfaceWithShadow);         \
  f(DRAWSHADOW, RecordedDrawShadow);                               \
  f(DRAWFILTER, RecordedDrawFilter);                               \
  f(PATHCREATION, RecordedPathCreation);                           \
  f(PATHDESTRUCTION, RecordedPathDestruction);                     \
  f(SOURCESURFACECREATION, RecordedSourceSurfaceCreation);         \
  f(SOURCESURFACEDESTRUCTION, RecordedSourceSurfaceDestruction);   \
  f(FILTERNODECREATION, RecordedFilterNodeCreation);               \
  f(DEFERFILTERINPUT, RecordedDeferFilterInput);                   \
  f(FILTERNODEDESTRUCTION, RecordedFilterNodeDestruction);         \
  f(GRADIENTSTOPSCREATION, RecordedGradientStopsCreation);         \
  f(GRADIENTSTOPSDESTRUCTION, RecordedGradientStopsDestruction);   \
  f(SNAPSHOT, RecordedSnapshot);                                   \
  f(SCALEDFONTCREATION, RecordedScaledFontCreation);               \
  f(SCALEDFONTDESTRUCTION, RecordedScaledFontDestruction);         \
  f(MASKSURFACE, RecordedMaskSurface);                             \
  f(FILTERNODESETATTRIBUTE, RecordedFilterNodeSetAttribute);       \
  f(FILTERNODESETINPUT, RecordedFilterNodeSetInput);               \
  f(CREATESIMILARDRAWTARGET, RecordedCreateSimilarDrawTarget);     \
  f(CREATECLIPPEDDRAWTARGET, RecordedCreateClippedDrawTarget);     \
  f(CREATEDRAWTARGETFORFILTER, RecordedCreateDrawTargetForFilter); \
  f(FONTDATA, RecordedFontData);                                   \
  f(FONTDESC, RecordedFontDescriptor);                             \
  f(PUSHLAYER, RecordedPushLayer);                                 \
  f(PUSHLAYERWITHBLEND, RecordedPushLayerWithBlend);               \
  f(POPLAYER, RecordedPopLayer);                                   \
  f(UNSCALEDFONTCREATION, RecordedUnscaledFontCreation);           \
  f(UNSCALEDFONTDESTRUCTION, RecordedUnscaledFontDestruction);     \
  f(INTOLUMINANCE, RecordedIntoLuminanceSource);                   \
  f(EXTRACTSUBRECT, RecordedExtractSubrect);                       \
  f(EXTERNALSURFACECREATION, RecordedExternalSurfaceCreation);     \
  f(FLUSH, RecordedFlush);                                         \
  f(DETACHALLSNAPSHOTS, RecordedDetachAllSnapshots);               \
  f(OPTIMIZESOURCESURFACE, RecordedOptimizeSourceSurface);         \
  f(LINK, RecordedLink);                                           \
  f(ACCESSIBLEID, RecordedAccessibleId);                           \
  f(DESTINATION, RecordedDestination);

#define DO_WITH_EVENT_TYPE(_typeenum, _class) \
  case _typeenum: {                           \
    auto e = _class(aStream);                 \
    return aAction(&e);                       \
  }

template <class S>
bool RecordedEvent::DoWithEvent(
    S& aStream, EventType aType,
    const std::function<bool(RecordedEvent*)>& aAction) {
  switch (aType) {
    FOR_EACH_EVENT(DO_WITH_EVENT_TYPE)
    default:
      return false;
  }
}

}  // namespace gfx
}  // namespace mozilla

#endif
