/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_RECORDEDEVENT_H_
#define MOZILLA_GFX_RECORDEDEVENT_H_

#include <ostream>
#include <sstream>
#include <cstring>
#include <functional>

#include "RecordingTypes.h"
#include "mozilla/PodOperations.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/ipc/ByteBuf.h"
#include "nsRefPtrHashtable.h"

namespace mozilla {
namespace gfx {

const uint32_t kMagicInt = 0xc001feed;

// A change in major revision means a change in event binary format, causing
// loss of backwards compatibility. Old streams will not work in a player
// using a newer major revision. And new streams will not work in a player
// using an older major revision.
const uint16_t kMajorRevision = 10;
// A change in minor revision means additions of new events. New streams will
// not play in older players.
const uint16_t kMinorRevision = 3;

struct ReferencePtr {
  ReferencePtr() : mLongPtr(0) {}

  MOZ_IMPLICIT ReferencePtr(const void* aLongPtr)
      : mLongPtr(uint64_t(aLongPtr)) {}

  template <typename T>
  MOZ_IMPLICIT ReferencePtr(const RefPtr<T>& aPtr)
      : mLongPtr(uint64_t(aPtr.get())) {}

  explicit ReferencePtr(uintptr_t aLongPtr) : mLongPtr(uint64_t(aLongPtr)) {}

  ReferencePtr& operator=(const void* aLongPtr) {
    mLongPtr = uint64_t(aLongPtr);
    return *this;
  }

  template <typename T>
  ReferencePtr& operator=(const RefPtr<T>& aPtr) {
    mLongPtr = uint64_t(aPtr.get());
    return *this;
  }

  operator void*() const { return (void*)mLongPtr; }

  explicit operator uintptr_t() const { return uintptr_t(mLongPtr); }

  // Implement some operators so this class can be used as a key in
  // stdlib classes.
  bool operator<(const ReferencePtr& aOther) const {
    return mLongPtr < aOther.mLongPtr;
  }

  bool operator>(const ReferencePtr& aOther) const {
    return mLongPtr > aOther.mLongPtr;
  }

  bool operator==(const ReferencePtr& aOther) const {
    return mLongPtr == aOther.mLongPtr;
  }

  bool operator!=(const ReferencePtr& aOther) const {
    return !(*this == aOther);
  }

  bool operator>=(const ReferencePtr& aOther) const {
    return mLongPtr >= aOther.mLongPtr;
  }

  uint64_t mLongPtr;
};

struct RecordedFontDetails {
  uint64_t fontDataKey = 0;
  uint32_t index = 0;
};

struct RecordedDependentSurface {
  NS_INLINE_DECL_REFCOUNTING(RecordedDependentSurface);

  RecordedDependentSurface(const IntSize& aSize,
                           mozilla::ipc::ByteBuf&& aRecording)
      : mSize(aSize), mRecording(std::move(aRecording)) {}

  IntSize mSize;
  mozilla::ipc::ByteBuf mRecording;

 private:
  ~RecordedDependentSurface() = default;
};

// Used by the Azure drawing debugger (player2d)
inline std::string StringFromPtr(ReferencePtr aPtr) {
  std::stringstream stream;
  stream << aPtr;
  return stream.str();
}

class Translator {
 public:
  virtual ~Translator() = default;

  virtual DrawTarget* LookupDrawTarget(ReferencePtr aRefPtr) = 0;
  virtual Path* LookupPath(ReferencePtr aRefPtr) = 0;
  virtual SourceSurface* LookupSourceSurface(ReferencePtr aRefPtr) = 0;
  virtual FilterNode* LookupFilterNode(ReferencePtr aRefPtr) = 0;
  virtual already_AddRefed<GradientStops> LookupGradientStops(
      ReferencePtr aRefPtr) = 0;
  virtual ScaledFont* LookupScaledFont(ReferencePtr aRefPtr) = 0;
  virtual UnscaledFont* LookupUnscaledFont(ReferencePtr aRefPtr) = 0;
  virtual NativeFontResource* LookupNativeFontResource(uint64_t aKey) = 0;
  virtual already_AddRefed<SourceSurface> LookupExternalSurface(uint64_t aKey) {
    return nullptr;
  }
  virtual already_AddRefed<SourceSurface>
  LookupSourceSurfaceFromSurfaceDescriptor(
      DrawTarget* aDT, const layers::SurfaceDescriptor& aDesc) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }
  void DrawDependentSurface(uint64_t aKey, const Rect& aRect);
  virtual void AddDrawTarget(ReferencePtr aRefPtr, DrawTarget* aDT) = 0;
  virtual void RemoveDrawTarget(ReferencePtr aRefPtr) = 0;
  virtual bool SetCurrentDrawTarget(ReferencePtr aRefPtr) = 0;
  virtual void AddPath(ReferencePtr aRefPtr, Path* aPath) = 0;
  virtual void RemovePath(ReferencePtr aRefPtr) = 0;
  virtual void AddSourceSurface(ReferencePtr aRefPtr, SourceSurface* aPath) = 0;
  virtual void RemoveSourceSurface(ReferencePtr aRefPtr) = 0;
  virtual void AddFilterNode(mozilla::gfx::ReferencePtr aRefPtr,
                             FilterNode* aSurface) = 0;
  virtual void RemoveFilterNode(mozilla::gfx::ReferencePtr aRefPtr) = 0;

  /**
   * Get GradientStops compatible with the translation DrawTarget type.
   * @param aRawStops array of raw gradient stops required
   * @param aNumStops length of aRawStops
   * @param aExtendMode extend mode required
   * @return an already addrefed GradientStops for our DrawTarget type
   */
  virtual already_AddRefed<GradientStops> GetOrCreateGradientStops(
      DrawTarget* aDrawTarget, GradientStop* aRawStops, uint32_t aNumStops,
      ExtendMode aExtendMode) {
    return aDrawTarget->CreateGradientStops(aRawStops, aNumStops, aExtendMode);
  }
  virtual void AddGradientStops(ReferencePtr aRefPtr, GradientStops* aPath) = 0;
  virtual void RemoveGradientStops(ReferencePtr aRefPtr) = 0;
  virtual void AddScaledFont(ReferencePtr aRefPtr, ScaledFont* aScaledFont) = 0;
  virtual void RemoveScaledFont(ReferencePtr aRefPtr) = 0;
  virtual void AddUnscaledFont(ReferencePtr aRefPtr,
                               UnscaledFont* aUnscaledFont) = 0;
  virtual void RemoveUnscaledFont(ReferencePtr aRefPtr) = 0;
  virtual void AddNativeFontResource(
      uint64_t aKey, NativeFontResource* aNativeFontResource) = 0;

  virtual already_AddRefed<DrawTarget> CreateDrawTarget(ReferencePtr aRefPtr,
                                                        const IntSize& aSize,
                                                        SurfaceFormat aFormat);
  virtual DrawTarget* GetReferenceDrawTarget() = 0;
  virtual Matrix GetReferenceDrawTargetTransform() { return Matrix(); }
  virtual void* GetFontContext() { return nullptr; }

  void SetDependentSurfaces(
      nsRefPtrHashtable<nsUint64HashKey, RecordedDependentSurface>*
          aDependentSurfaces) {
    mDependentSurfaces = aDependentSurfaces;
  }

  // NOTE that the returned DrawTarget may be in an error state!
  DrawTarget* GetCurrentDrawTarget() const { return mCurrentDT; }

  nsRefPtrHashtable<nsUint64HashKey, RecordedDependentSurface>*
      mDependentSurfaces = nullptr;
  DrawTarget* mCurrentDT = nullptr;
};

struct ColorPatternStorage {
  DeviceColor mColor;
};

struct LinearGradientPatternStorage {
  Point mBegin;
  Point mEnd;
  ReferencePtr mStops;
  Matrix mMatrix;
};

struct RadialGradientPatternStorage {
  Point mCenter1;
  Point mCenter2;
  Float mRadius1;
  Float mRadius2;
  ReferencePtr mStops;
  Matrix mMatrix;
};

struct ConicGradientPatternStorage {
  Point mCenter;
  Float mAngle;
  Float mStartOffset;
  Float mEndOffset;
  ReferencePtr mStops;
  Matrix mMatrix;
};

struct SurfacePatternStorage {
  ExtendMode mExtend;
  SamplingFilter mSamplingFilter;
  ReferencePtr mSurface;
  Matrix mMatrix;
  IntRect mSamplingRect;
};

struct PatternStorage {
  PatternType mType;
  union {
    char* mStorage;
    char mColor[sizeof(ColorPatternStorage)];
    char mLinear[sizeof(LinearGradientPatternStorage)];
    char mRadial[sizeof(RadialGradientPatternStorage)];
    char mConic[sizeof(ConicGradientPatternStorage)];
    char mSurface[sizeof(SurfacePatternStorage)];
  };
};

/* SizeCollector and MemWriter are used
 * in a pair to first collect the size of the
 * event that we're going to write and then
 * to write it without checking each individual
 * size. */
struct SizeCollector {
  SizeCollector() : mTotalSize(0) {}
  void write(const char*, size_t s) { mTotalSize += s; }
  size_t mTotalSize;
};

struct MemWriter {
  constexpr explicit MemWriter(char* aPtr) : mPtr(aPtr) {}
  void write(const char* aData, size_t aSize) {
    memcpy(mPtr, aData, aSize);
    mPtr += aSize;
  }
  char* mPtr;
};

// An istream like class for reading from memory
struct MemReader {
  constexpr MemReader(const char* aData, size_t aLen)
      : mData(aData), mEnd(aData + aLen) {}
  [[nodiscard]] bool read(char* s, std::streamsize n) {
    if (n <= (mEnd - mData)) {
      memcpy(s, mData, n);
      mData += n;
      return true;
    }
    // We've requested more data than is available
    // set the Reader into an eof state
    SetIsBad();
    return false;
  }
  bool eof() { return mData > mEnd; }
  bool good() { return !eof(); }
  void SetIsBad() { mData = mEnd + 1; }

  const char* mData;
  const char* mEnd;
};

class ContiguousBuffer {
 public:
  ContiguousBuffer(char* aStart, size_t aSize)
      : mWriter(aStart), mEnd(aStart + aSize) {}

  constexpr MOZ_IMPLICIT ContiguousBuffer(std::nullptr_t) : mWriter(nullptr) {}

  MemWriter& Writer() { return mWriter; }

  size_t SizeRemaining() { return mWriter.mPtr ? mEnd - mWriter.mPtr : 0; }

  bool IsValid() { return !!mWriter.mPtr; }

 private:
  MemWriter mWriter;
  char* mEnd = nullptr;
};

// Allows a derived class to provide guaranteed contiguous buffer.
class ContiguousBufferStream {
 public:
  /**
   * Templated RecordEvent function so that we can record into the buffer
   * quickly using MemWriter.
   *
   * @param aRecordedEvent the event to record
   */
  template <class RE>
  void RecordEvent(const RE* aRecordedEvent) {
    SizeCollector size;
    WriteElement(size, aRecordedEvent->GetType());
    aRecordedEvent->Record(size);
    auto& buffer = GetContiguousBuffer(size.mTotalSize);
    if (!buffer.IsValid()) {
      return;
    }

    MOZ_ASSERT(size.mTotalSize <= buffer.SizeRemaining());

    WriteElement(buffer.Writer(), aRecordedEvent->GetType());
    aRecordedEvent->Record(buffer.Writer());
    IncrementEventCount();
  }

 protected:
  /**
   * Provide a contiguous buffer with at least aSize remaining.
   */
  virtual ContiguousBuffer& GetContiguousBuffer(size_t aSize) = 0;

  virtual void IncrementEventCount() = 0;
};

struct MemStream {
  char* mData;
  size_t mLength;
  size_t mCapacity;
  bool mValid = true;
  bool Resize(size_t aSize) {
    if (!mValid) {
      return false;
    }
    mLength = aSize;
    if (mLength > mCapacity) {
      mCapacity = mCapacity * 2;
      // check if the doubled capacity is enough
      // otherwise use double mLength
      if (mLength > mCapacity) {
        mCapacity = mLength * 2;
      }
      char* data = (char*)realloc(mData, mCapacity);
      if (!data) {
        free(mData);
      }
      mData = data;
    }
    if (mData) {
      return true;
    }
    NS_ERROR("Failed to allocate MemStream!");
    mValid = false;
    mLength = 0;
    mCapacity = 0;
    return false;
  }

  void reset() {
    free(mData);
    mData = nullptr;
    mValid = true;
    mLength = 0;
    mCapacity = 0;
  }

  MemStream(const MemStream&) = delete;
  MemStream(MemStream&&) = delete;
  MemStream& operator=(const MemStream&) = delete;
  MemStream& operator=(MemStream&&) = delete;

  void write(const char* aData, size_t aSize) {
    if (Resize(mLength + aSize)) {
      memcpy(mData + mLength - aSize, aData, aSize);
    }
  }

  MemStream() : mData(nullptr), mLength(0), mCapacity(0) {}
  ~MemStream() { free(mData); }
};

class EventStream {
 public:
  virtual void write(const char* aData, size_t aSize) = 0;
  [[nodiscard]] virtual bool read(char* aOut, size_t aSize) = 0;
  virtual bool good() = 0;
  virtual void SetIsBad() = 0;
};

class RecordedEvent {
 public:
  enum EventType : uint8_t {
    INVALID = 0,
    DRAWTARGETCREATION,
    DRAWTARGETDESTRUCTION,
    SETCURRENTDRAWTARGET,
    FILLRECT,
    STROKERECT,
    STROKELINE,
    STROKECIRCLE,
    CLEARRECT,
    COPYSURFACE,
    SETPERMITSUBPIXELAA,
    SETTRANSFORM,
    PUSHCLIP,
    PUSHCLIPRECT,
    POPCLIP,
    REMOVEALLCLIPS,
    FILL,
    FILLCIRCLE,
    FILLGLYPHS,
    STROKEGLYPHS,
    MASK,
    STROKE,
    DRAWSURFACE,
    DRAWSURFACEDESCRIPTOR,
    DRAWDEPENDENTSURFACE,
    DRAWSURFACEWITHSHADOW,
    DRAWSHADOW,
    PATHCREATION,
    PATHDESTRUCTION,
    SOURCESURFACECREATION,
    SOURCESURFACEDESTRUCTION,
    GRADIENTSTOPSCREATION,
    GRADIENTSTOPSDESTRUCTION,
    SNAPSHOT,
    SCALEDFONTCREATION,
    SCALEDFONTDESTRUCTION,
    MASKSURFACE,
    FILTERNODECREATION,
    DEFERFILTERINPUT,
    FILTERNODEDESTRUCTION,
    DRAWFILTER,
    FILTERNODESETATTRIBUTE,
    FILTERNODESETINPUT,
    CREATESIMILARDRAWTARGET,
    CREATECLIPPEDDRAWTARGET,
    CREATEDRAWTARGETFORFILTER,
    FONTDATA,
    FONTDESC,
    PUSHLAYER,
    PUSHLAYERWITHBLEND,
    POPLAYER,
    UNSCALEDFONTCREATION,
    UNSCALEDFONTDESTRUCTION,
    INTOLUMINANCE,
    EXTRACTSUBRECT,
    EXTERNALSURFACECREATION,
    FLUSH,
    DETACHALLSNAPSHOTS,
    OPTIMIZESOURCESURFACE,
    LINK,
    DESTINATION,
    ACCESSIBLEID,
    LAST,
  };

  virtual ~RecordedEvent() = default;

  static std::string GetEventName(EventType aType);

  /**
   * Play back this event using the translator. Note that derived classes
   * should
   * only return false when there is a fatal error, as it will probably mean
   * the
   * translation will abort.
   * @param aTranslator Translator to be used for retrieving other referenced
   *                    objects and making playback decisions.
   * @return true unless a fatal problem has occurred and playback should
   * abort.
   */
  virtual bool PlayEvent(Translator* aTranslator) const { return true; }

  virtual void RecordToStream(std::ostream& aStream) const = 0;
  virtual void RecordToStream(EventStream& aStream) const = 0;
  virtual void RecordToStream(ContiguousBufferStream& aStream) const = 0;
  virtual void RecordToStream(MemStream& aStream) const = 0;

  virtual void OutputSimpleEventInfo(std::stringstream& aStringStream) const {}

  template <class S>
  void RecordPatternData(S& aStream,
                         const PatternStorage& aPatternStorage) const;
  template <class S>
  void ReadPatternData(S& aStream, PatternStorage& aPatternStorage) const;
  void StorePattern(PatternStorage& aDestination, const Pattern& aSource) const;

  virtual std::string GetName() const = 0;

  virtual ReferencePtr GetDestinedDT() { return nullptr; }

  void OutputSimplePatternInfo(const PatternStorage& aStorage,
                               std::stringstream& aOutput) const;

  template <class S>
  static bool DoWithEvent(S& aStream, EventType aType,
                          const std::function<bool(RecordedEvent*)>& aAction);
  static bool DoWithEventFromStream(
      EventStream& aStream, EventType aType,
      const std::function<bool(RecordedEvent*)>& aAction);
  static bool DoWithEventFromReader(
      MemReader& aReader, EventType aType,
      const std::function<bool(RecordedEvent*)>& aAction);

  EventType GetType() const { return (EventType)mType; }

 protected:
  friend class DrawEventRecorderPrivate;
  friend class DrawEventRecorderMemory;
  static void RecordUnscaledFont(UnscaledFont* aUnscaledFont,
                                 std::ostream* aOutput);
  static void RecordUnscaledFont(UnscaledFont* aUnscaledFont,
                                 MemStream& aOutput);
  template <class S>
  static void RecordUnscaledFontImpl(UnscaledFont* aUnscaledFont, S& aOutput);

  MOZ_IMPLICIT RecordedEvent(EventType aType) : mType(aType) {}

  EventType mType;
};

class RecordedStrokeOptionsMixin {
 public:
  template <class S>
  void RecordStrokeOptions(S& aStream,
                           const StrokeOptions& aStrokeOptions) const;
  template <class S>
  void ReadStrokeOptions(S& aStream, StrokeOptions& aStrokeOptions);

 protected:
  UniquePtr<Float[]> mDashPatternStorage;
};

template <typename T, typename Z = size_t>
class RecordedEventArray {
 public:
  T* data() { return mData.get(); }
  const T* data() const { return mData.get(); }
  Z size() const { return mSize; }
  bool empty() const { return !mSize; }

  void Assign(const T* aData, Z aSize) {
    mSize = aSize;
    mData = MakeUnique<T[]>(aSize);
    PodCopy(mData.get(), aData, aSize);
  }

  bool TryAlloc(Z aSize) {
    if (mSize > 0) {
      MOZ_ASSERT_UNREACHABLE();
      return false;
    }
    mData = MakeUniqueFallible<T[]>(aSize);
    if (!mData) {
      return false;
    }
    mSize = aSize;
    return true;
  }

  bool TryAssign(const T* aData, Z aSize) {
    if (!TryAlloc(aSize)) {
      return false;
    }
    PodCopy(mData.get(), aData, aSize);
    return true;
  }

  template <typename S>
  void Write(S& aStream) const {
    if (mSize) {
      aStream.write(reinterpret_cast<const char*>(mData.get()),
                    sizeof(T) * mSize);
    }
  }

  template <typename S>
  bool Read(S& aStream, Z aSize) {
    if (!aStream.good() || !TryAlloc(aSize)) {
      return false;
    }
    if (!aStream.read(reinterpret_cast<char*>(mData.get()),
                      sizeof(T) * mSize)) {
      Clear();
      return false;
    }
    return true;
  }

  void Clear() {
    mSize = 0;
    mData.reset();
  }

 protected:
  Z mSize = 0;
  UniquePtr<T[]> mData;
};

class RecordedEventCString : public RecordedEventArray<char> {
 public:
  explicit RecordedEventCString(const char* aStr = nullptr) {
    if (aStr) {
      if (size_t len = strlen(aStr)) {
        Assign(aStr, len + 1);
      }
    }
  }

  template <typename S>
  bool Read(S& aStream, size_t aSize) {
    if (!RecordedEventArray<char>::Read(aStream, aSize) ||
        (size() > 0 && !memchr(data(), '\0', size()))) {
      Clear();
      return false;
    }
    return true;
  }
};

template <class Derived>
class RecordedEventDerived : public RecordedEvent {
  using RecordedEvent::RecordedEvent;

 public:
  void RecordToStream(std::ostream& aStream) const override {
    WriteElement(aStream, this->mType);
    static_cast<const Derived*>(this)->Record(aStream);
  }
  void RecordToStream(EventStream& aStream) const override {
    WriteElement(aStream, this->mType);
    static_cast<const Derived*>(this)->Record(aStream);
  }
  void RecordToStream(ContiguousBufferStream& aStream) const final {
    aStream.RecordEvent(static_cast<const Derived*>(this));
  }
  void RecordToStream(MemStream& aStream) const override {
    SizeCollector size;
    WriteElement(size, this->mType);
    static_cast<const Derived*>(this)->Record(size);

    if (!aStream.Resize(aStream.mLength + size.mTotalSize)) {
      return;
    }

    MemWriter writer(aStream.mData + aStream.mLength - size.mTotalSize);
    WriteElement(writer, this->mType);
    static_cast<const Derived*>(this)->Record(writer);
  }
};

}  // namespace gfx
}  // namespace mozilla

// Helper struct that allows ReferencePtr to be used as a key in
// std::unordered_map
template <>
struct std::hash<mozilla::gfx::ReferencePtr> {
  std::size_t operator()(const mozilla::gfx::ReferencePtr& aRef) const {
    return std::hash<uint64_t>{}(aRef.mLongPtr);
  }
};

#endif
