/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* JS script descriptor. */

#ifndef vm_JSScript_h
#define vm_JSScript_h

#include "mozilla/Atomics.h"
#include "mozilla/Maybe.h"
#include "mozilla/MaybeOneOf.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Span.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Utf8.h"
#include "mozilla/Variant.h"

#include <type_traits>  // std::is_same
#include <utility>      // std::move

#include "jstypes.h"

#include "frontend/ScriptIndex.h"  // ScriptIndex
#include "gc/Allocator.h"
#include "gc/Barrier.h"
#include "js/ColumnNumber.h"  // JS::LimitedColumnNumberOneOrigin, JS::LimitedColumnNumberOneOrigin
#include "js/CompileOptions.h"
#include "js/Transcoding.h"
#include "js/UbiNode.h"
#include "js/UniquePtr.h"
#include "js/Utility.h"
#include "util/TrailingArray.h"
#include "vm/BytecodeIterator.h"
#include "vm/BytecodeLocation.h"
#include "vm/BytecodeUtil.h"
#include "vm/MutexIDs.h"  // mutexid
#include "vm/NativeObject.h"
#include "vm/SharedImmutableStringsCache.h"
#include "vm/SharedStencil.h"  // js::GCThingIndex, js::SourceExtent, js::SharedImmutableScriptData, MemberInitializers
#include "vm/StencilEnums.h"   // SourceRetrievable

namespace JS {
struct ScriptSourceInfo;
template <typename UnitT>
class SourceText;
}  // namespace JS

namespace js {

class Compressor;
class FrontendContext;
class ScriptSource;
class SourceLocationIterator;

class VarScope;
class LexicalScope;

class JS_PUBLIC_API Sprinter;

namespace coverage {
class LCovSource;
}  // namespace coverage

namespace gc {
class AllocSite;
}  // namespace gc

namespace jit {
class AutoKeepJitScripts;
class BaselineScript;
class IonScript;
struct IonScriptCounts;
class JitScript;
}  // namespace jit

class ModuleObject;
class RegExpObject;
class SourceCompressionTaskEntry;
class Shape;
class SrcNote;
class DebugScript;

namespace frontend {
struct CompilationStencil;
struct ExtensibleCompilationStencil;
struct CompilationGCOutput;
struct InitialStencilAndDelazifications;
struct CompilationStencilMerger;
class StencilXDR;
}  // namespace frontend

class ScriptCounts {
 public:
  using PCCountsVector = mozilla::Vector<PCCounts, 0, SystemAllocPolicy>;

  inline ScriptCounts();
  inline explicit ScriptCounts(PCCountsVector&& jumpTargets);
  inline ScriptCounts(ScriptCounts&& src);
  inline ~ScriptCounts();

  inline ScriptCounts& operator=(ScriptCounts&& src);

  // Return the counter used to count the number of visits. Returns null if
  // the element is not found.
  PCCounts* maybeGetPCCounts(size_t offset);
  const PCCounts* maybeGetPCCounts(size_t offset) const;

  // PCCounts are stored at jump-target offsets. This function looks for the
  // previous PCCount which is in the same basic block as the current offset.
  PCCounts* getImmediatePrecedingPCCounts(size_t offset);

  // Return the counter used to count the number of throws. Returns null if
  // the element is not found.
  const PCCounts* maybeGetThrowCounts(size_t offset) const;

  // Throw counts are stored at the location of each throwing
  // instruction. This function looks for the previous throw count.
  //
  // Note: if the offset of the returned count is higher than the offset of
  // the immediate preceding PCCount, then this throw happened in the same
  // basic block.
  const PCCounts* getImmediatePrecedingThrowCounts(size_t offset) const;

  // Return the counter used to count the number of throws. Allocate it if
  // none exists yet. Returns null if the allocation failed.
  PCCounts* getThrowCounts(size_t offset);

  size_t sizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf);

  bool traceWeak(JSTracer* trc) { return true; }

 private:
  friend class ::JSScript;
  friend struct ScriptAndCounts;

  // This sorted array is used to map an offset to the number of times a
  // branch got visited.
  PCCountsVector pcCounts_;

  // This sorted vector is used to map an offset to the number of times an
  // instruction throw.
  PCCountsVector throwCounts_;

  // Information about any Ion compilations for the script.
  jit::IonScriptCounts* ionCounts_;
};

// These side-tables are held as WeakCaches so that dead entries are removed as
// part of GC sweeping and entries are rekeyed automatically after a compacting
// GC. Entries are not traced during marking, so scripts are not kept alive by
// these tables alone. They are weak references and do not keep scripts alive.
//
// Note carefully, however, that there is an exceptional case for which we *do*
// want the JSScripts to be strong references (and thus traced): when the
// --dump-bytecode command line option or the PCCount JSFriend API is used,
// then the scripts for all counts must remain alive. See
// Zone::traceScriptTableRoots() for more details.
//
// TODO: Clean this up by either aggregating coverage results in some other
// way, or by tweaking sweep ordering.
using UniqueScriptCounts = js::UniquePtr<ScriptCounts>;
using ScriptCountsMap =
    GCRekeyableHashMap<HeapPtr<BaseScript*>, UniqueScriptCounts,
                       DefaultHasher<HeapPtr<BaseScript*>>, SystemAllocPolicy>;

// The 'const char*' for the function name is a pointer within the LCovSource's
// LifoAlloc and will be discarded at the same time.
using ScriptLCovEntry = std::tuple<coverage::LCovSource*, const char*>;
using ScriptLCovMap =
    GCRekeyableHashMap<HeapPtr<BaseScript*>, ScriptLCovEntry,
                       DefaultHasher<HeapPtr<BaseScript*>>, SystemAllocPolicy>;

#ifdef MOZ_VTUNE
using ScriptVTuneIdMap =
    GCRekeyableHashMap<HeapPtr<BaseScript*>, uint32_t,
                       DefaultHasher<HeapPtr<BaseScript*>>, SystemAllocPolicy>;
#endif
#ifdef JS_CACHEIR_SPEW
using ScriptFinalWarmUpCountEntry = std::tuple<uint32_t, SharedImmutableString>;
using ScriptFinalWarmUpCountMap =
    GCRekeyableHashMap<HeapPtr<BaseScript*>, ScriptFinalWarmUpCountEntry,
                       DefaultHasher<HeapPtr<BaseScript*>>, SystemAllocPolicy>;
#endif

// Map of strings used by the Gecko Profiler. See vm/GeckoProfiler.h for more
// details.
using ProfileStringMap =
    GCRekeyableHashMap<HeapPtr<BaseScript*>, JS::UniqueChars,
                       DefaultHasher<HeapPtr<BaseScript*>>, SystemAllocPolicy>;

struct ScriptSourceChunk {
  // The actual type is |const ScriptSource::ExclusiveSourceData*|, but given
  // that the nested class cannot be forward declared, we use |const void*|
  // instead.
  const void* sourceData = nullptr;
  uint32_t chunk = 0;

  ScriptSourceChunk() = default;

  ScriptSourceChunk(const void* sourceData, uint32_t chunk)
      : sourceData(sourceData), chunk(chunk) {
    MOZ_ASSERT(valid());
  }

  bool valid() const { return sourceData != nullptr; }

  bool operator==(const ScriptSourceChunk& other) const = default;
};

struct ScriptSourceChunkHasher {
  using Lookup = ScriptSourceChunk;

  static HashNumber hash(const ScriptSourceChunk& ssc) {
    return mozilla::AddToHash(DefaultHasher<const void*>::hash(ssc.sourceData),
                              ssc.chunk);
  }
  static bool match(const ScriptSourceChunk& c1, const ScriptSourceChunk& c2) {
    return c1 == c2;
  }
};

template <typename Unit>
using EntryUnits = mozilla::UniquePtr<Unit[], JS::FreePolicy>;

// The uncompressed source cache contains *either* UTF-8 source data *or*
// UTF-16 source data.  ScriptSourceChunk implies a ScriptSource that
// contains either UTF-8 data or UTF-16 data, so the nature of the key to
// Map below indicates how each SourceData ought to be interpreted.
using SourceData = mozilla::UniquePtr<void, JS::FreePolicy>;

template <typename Unit>
inline SourceData ToSourceData(EntryUnits<Unit> chars) {
  static_assert(std::is_same_v<SourceData::deleter_type,
                               typename EntryUnits<Unit>::deleter_type>,
                "EntryUnits and SourceData must share the same deleter "
                "type, that need not know the type of the data being freed, "
                "for the upcast below to be safe");
  return SourceData(chars.release());
}

class UncompressedSourceCache {
  using Map = HashMap<ScriptSourceChunk, SourceData, ScriptSourceChunkHasher,
                      SystemAllocPolicy>;

 public:
  // Hold an entry in the source data cache and prevent it from being purged on
  // GC.
  class AutoHoldEntry {
    UncompressedSourceCache* cache_ = nullptr;
    ScriptSourceChunk sourceChunk_ = {};
    SourceData data_ = nullptr;

   public:
    explicit AutoHoldEntry() = default;

    ~AutoHoldEntry() {
      if (cache_) {
        MOZ_ASSERT(sourceChunk_.valid());
        cache_->releaseEntry(*this);
      }
    }

    template <typename Unit>
    void holdUnits(EntryUnits<Unit> units) {
      MOZ_ASSERT(!cache_);
      MOZ_ASSERT(!sourceChunk_.valid());
      MOZ_ASSERT(!data_);

      data_ = ToSourceData(std::move(units));
    }

   private:
    void holdEntry(UncompressedSourceCache* cache,
                   const ScriptSourceChunk& sourceChunk) {
      // Initialise the holder for a specific cache and script source.
      // This will hold on to the cached source chars in the event that
      // the cache is purged.
      MOZ_ASSERT(!cache_);
      MOZ_ASSERT(!sourceChunk_.valid());
      MOZ_ASSERT(!data_);

      cache_ = cache;
      sourceChunk_ = sourceChunk;
    }

    void deferDelete(SourceData data) {
      // Take ownership of source chars now the cache is being purged. Remove
      // our reference to the ScriptSource which might soon be destroyed.
      MOZ_ASSERT(cache_);
      MOZ_ASSERT(sourceChunk_.valid());
      MOZ_ASSERT(!data_);

      cache_ = nullptr;
      sourceChunk_ = ScriptSourceChunk();

      data_ = std::move(data);
    }

    const ScriptSourceChunk& sourceChunk() const { return sourceChunk_; }
    friend class UncompressedSourceCache;
  };

 private:
  UniquePtr<Map> map_ = nullptr;
  AutoHoldEntry* holder_ = nullptr;

 public:
  UncompressedSourceCache() = default;

  template <typename Unit>
  const Unit* lookup(const ScriptSourceChunk& ssc, AutoHoldEntry& asp);

  bool put(const ScriptSourceChunk& ssc, SourceData data, AutoHoldEntry& asp);

  void purge();

  size_t sizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf);

 private:
  void holdEntry(AutoHoldEntry& holder, const ScriptSourceChunk& ssc);
  void releaseEntry(AutoHoldEntry& holder);
};

template <typename Unit>
struct SourceTypeTraits;

template <>
struct SourceTypeTraits<mozilla::Utf8Unit> {
  using CharT = char;
  using SharedImmutableString = js::SharedImmutableString;

  static const mozilla::Utf8Unit* units(const SharedImmutableString& string) {
    // Casting |char| data to |Utf8Unit| is safe because |Utf8Unit|
    // contains a |char|.  See the long comment in |Utf8Unit|'s definition.
    return reinterpret_cast<const mozilla::Utf8Unit*>(string.chars());
  }

  static char* toString(const mozilla::Utf8Unit* units) {
    auto asUnsigned =
        const_cast<unsigned char*>(mozilla::Utf8AsUnsignedChars(units));
    return reinterpret_cast<char*>(asUnsigned);
  }

  static UniqueChars toCacheable(EntryUnits<mozilla::Utf8Unit> str) {
    // The cache only stores strings of |char| or |char16_t|, and right now
    // it seems best not to gunk up the cache with |Utf8Unit| too.  So
    // cache |Utf8Unit| strings by interpreting them as |char| strings.
    char* chars = toString(str.release());
    return UniqueChars(chars);
  }
};

template <>
struct SourceTypeTraits<char16_t> {
  using CharT = char16_t;
  using SharedImmutableString = js::SharedImmutableTwoByteString;

  static const char16_t* units(const SharedImmutableString& string) {
    return string.chars();
  }

  static char16_t* toString(const char16_t* units) {
    return const_cast<char16_t*>(units);
  }

  static UniqueTwoByteChars toCacheable(EntryUnits<char16_t> str) {
    return UniqueTwoByteChars(std::move(str));
  }
};

// Synchronously compress the source of |script|, for testing purposes.
[[nodiscard]] extern bool SynchronouslyCompressSource(
    JSContext* cx, JS::Handle<BaseScript*> script);

// Variant return type for ScriptSource::substringChars to support both UTF-8
// and UTF-16.
using SubstringCharsResult =
    mozilla::Variant<JS::UniqueChars, JS::UniqueTwoByteChars>;

// [SMDOC] ScriptSource
//
// This class abstracts over the source we used to compile from. The current
// representation may transition to different modes in order to save memory.
// Abstractly the source may be one of UTF-8 or UTF-16. The data itself may be
// unavailable, retrieveable-using-source-hook, compressed, or uncompressed. If
// source is retrieved or decompressed for use, we may update the ScriptSource
// to hold the result.
class ScriptSource {
  // NOTE: While ScriptSources may be compressed off thread, they are only
  // modified by the main thread, and all members are always safe to access
  // on the main thread.

  friend class PendingSourceCompressionEntry;
  friend class SourceCompressionTaskEntry;
  friend bool SynchronouslyCompressSource(JSContext* cx,
                                          JS::Handle<BaseScript*> script);

  friend class frontend::StencilXDR;

 private:
  // Missing source text that isn't retrievable using the source hook.  (All
  // ScriptSources initially begin in this state.  Users that are compiling
  // source text will overwrite |data_| to store a different state.)
  struct Missing {};

  // Source that can be retrieved using the registered source hook.  |Unit|
  // records the source type so that source-text coordinates in functions and
  // scripts that depend on this |ScriptSource| are correct.
  template <typename Unit>
  struct Retrievable {
    // The source hook and script URL required to retrieve source are stored
    // elsewhere, so nothing is needed here.  It'd be better hygiene to store
    // something source-hook-like in each |ScriptSource| that needs it, but that
    // requires reimagining a source-hook API that currently depends on source
    // hooks being uniquely-owned pointers...
  };

  // Uncompressed source text. Templates distinguish if we are interconvertable
  // to |Retrievable| or not.
  template <typename Unit>
  class UncompressedData {
    typename SourceTypeTraits<Unit>::SharedImmutableString string_;

   public:
    explicit UncompressedData(
        typename SourceTypeTraits<Unit>::SharedImmutableString str)
        : string_(std::move(str)) {}

    const Unit* units() const { return SourceTypeTraits<Unit>::units(string_); }

    size_t length() const { return string_.length(); }
  };

  template <typename Unit, SourceRetrievable CanRetrieve>
  class Uncompressed : public UncompressedData<Unit> {
    using Base = UncompressedData<Unit>;

   public:
    using Base::Base;
  };

  // Compressed source text. Templates distinguish if we are interconvertable
  // to |Retrievable| or not.
  template <typename Unit>
  struct CompressedData {
    // Single-byte compressed text, regardless whether the original text
    // was single-byte or two-byte.
    SharedImmutableString raw;
    size_t uncompressedLength;

    CompressedData(SharedImmutableString raw, size_t uncompressedLength)
        : raw(std::move(raw)), uncompressedLength(uncompressedLength) {}
  };

  template <typename Unit, SourceRetrievable CanRetrieve>
  struct Compressed : public CompressedData<Unit> {
    using Base = CompressedData<Unit>;

   public:
    using Base::Base;
  };

  // The set of currently allowed encoding modes.
  //
  // The state transition happens in the following way, where the compress
  // transition can happen only when there's no DataReader instance with
  // read access.
  // See the comment for ExclusiveSourceDataLockState as well.
  //
  // Once the source is compressed, uncompressing the source data doesn't
  // modify the state, but the uncompressed string is stored into the
  // UncompressedSourceCache.
  //
  // +-----------+
  // |  Missing  |------------------------------+
  // +-----------+                              |
  //   |      |                                 |
  //   |      | compile with non-lazy source    |
  //   |      | or XDR decode                   | XDR decode
  //   |      v                                 v
  //   |    +--------------------+            +--------------------+
  //   |    |  Uncompressed      |  compress  |  Compressed        |
  //   |    |  Retrievable::No   |----------->|  Retrievable::No   |
  //   |    +--------------------+            +--------------------+
  //   |
  //   | compile with lazy source
  //   | or XDR decode
  //   v
  // +-------------+
  // | Retrievable |
  // +-------------+
  //          |
  //          | load
  //          v
  //        +--------------------+            +--------------------+
  //        |  Uncompressed      |  compress  |  Compressed        |
  //        |  Retrievable::Yes  |----------->|  Retrievable::Yes  |
  //        +--------------------+            +--------------------+
  //
  // When XDR encoding, Uncompressed and Compressed with Retrievable::Yes are
  // encoded as Retrievable, and thus decoding the buffer will create an
  // instance with the Retrievable state.
  using SourceType =
      mozilla::Variant<Compressed<mozilla::Utf8Unit, SourceRetrievable::Yes>,
                       Uncompressed<mozilla::Utf8Unit, SourceRetrievable::Yes>,
                       Compressed<mozilla::Utf8Unit, SourceRetrievable::No>,
                       Uncompressed<mozilla::Utf8Unit, SourceRetrievable::No>,
                       Compressed<char16_t, SourceRetrievable::Yes>,
                       Uncompressed<char16_t, SourceRetrievable::Yes>,
                       Compressed<char16_t, SourceRetrievable::No>,
                       Uncompressed<char16_t, SourceRetrievable::No>,
                       Retrievable<mozilla::Utf8Unit>, Retrievable<char16_t>,
                       Missing>;

 public:
  class DataReader;

 private:
  class ExclusiveSourceData;

  // Combined with ExclusiveData template (see ScriptSource::sourceDataState_
  // field), this provides multiple-readers / single-writer lock for
  // ExclusiveSourceData, with the delayed compression.
  //
  // See DataReader/DataWriter below also.
  //
  // If the GC calls triggerConvertToCompressedSourceFromTask with DataReader
  // present, the last DataReader instance will install the compressed chars
  // upon destruction.
  class ExclusiveSourceDataLockState {
   public:
    void addReader();
    void removeReader(ScriptSource* ss, DataReader& reader);

    // Remove reader when converting DataReader into DataWriter.
    void removeReaderForWrite();

    bool hasReaders() const { return readers_ > 0; }

    template <typename Unit>
    void setPendingCompressed(SharedImmutableString compressed,
                              size_t uncompressedLength);

   private:
    template <typename Unit>
    void performDelayedConvertToCompressedSource(DataReader& reader);

    // The number of existing readers.
    // If this is non-zero, ExclusiveSourceData is locked by readers,
    // and DataWriter cannot modify ExclusiveSourceData.
    size_t readers_ = 0;

    // The pending conversion to the compressed data.
    //
    // Retrievability isn't part of the type here because
    // uncompressed->compressed transitions must preserve existing
    // retrievability.
    mozilla::MaybeOneOf<CompressedData<mozilla::Utf8Unit>,
                        CompressedData<char16_t>>
        pendingCompressed_;
  };

  // The script source data, locked by the combination of the following:
  //   * ExclusiveSourceDataLockState,
  //   * RWLocked
  //   * DataReader
  //   * DataWriter
  //
  // All access to this class instance should be performed through either
  // DataReader or DataWriter, for GC safety.
  class ExclusiveSourceData {
    friend class frontend::StencilXDR;
    friend class ScriptSource;

    // ==== Read properties ====

    class SourcePropertiesGetter;

   public:
    // Returns the source data properties of the script source.
    //
    // *loaded indicates whether source text is available, *retrievable
    // indicates whether the source can be retrieved later via source hook, and
    // *isTwoByteString indicates if the underlying source data is
    // char16_t-typed.
    void getSourceProperties(bool* hasSourceText, bool* retrievable,
                             bool* isTwoByteString) const;

   private:
    struct HasUncompressedSource {
      template <typename Unit, SourceRetrievable CanRetrieve>
      bool operator()(const Uncompressed<Unit, CanRetrieve>&) {
        return true;
      }

      template <typename Unit, SourceRetrievable CanRetrieve>
      bool operator()(const Compressed<Unit, CanRetrieve>&) {
        return false;
      }

      template <typename Unit>
      bool operator()(const Retrievable<Unit>&) {
        return false;
      }

      bool operator()(const Missing&) { return false; }
    };

   public:
    bool hasUncompressedSource() const {
      return data_.match(HasUncompressedSource());
    }

   private:
    template <typename Unit>
    struct IsUncompressed {
      template <SourceRetrievable CanRetrieve>
      bool operator()(const Uncompressed<Unit, CanRetrieve>&) {
        return true;
      }

      template <typename T>
      bool operator()(const T&) {
        return false;
      }
    };

   public:
    template <typename Unit>
    bool isUncompressed() const {
      return data_.match(IsUncompressed<Unit>());
    }

   private:
    struct HasCompressedSource {
      template <typename Unit, SourceRetrievable CanRetrieve>
      bool operator()(const Compressed<Unit, CanRetrieve>&) {
        return true;
      }

      template <typename T>
      bool operator()(const T&) {
        return false;
      }
    };

   public:
    bool hasCompressedSource() const {
      return data_.match(HasCompressedSource());
    }

    bool hasSourceText() const {
      return hasUncompressedSource() || hasCompressedSource();
    }

   private:
    template <typename Unit>
    struct IsCompressed {
      template <SourceRetrievable CanRetrieve>
      bool operator()(const Compressed<Unit, CanRetrieve>&) {
        return true;
      }

      template <typename T>
      bool operator()(const T&) {
        return false;
      }
    };

   public:
    template <typename Unit>
    bool isCompressed() const {
      return data_.match(IsCompressed<Unit>());
    }

    template <template <typename U, SourceRetrievable CanRetrieve> class Data,
              typename Unit>
    bool isRetrievableData() const {
      return data_.is<Data<Unit, SourceRetrievable::Yes>>();
    }

    template <typename Unit>
    bool isRetrievable() const {
      return data_.is<Retrievable<Unit>>();
    }

    bool isMissing() const { return data_.is<Missing>(); }

   private:
    template <typename Unit>
    struct SourceTypeMatcher {
      template <template <typename C, SourceRetrievable R> class Data,
                SourceRetrievable CanRetrieve>
      bool operator()(const Data<Unit, CanRetrieve>&) {
        return true;
      }

      template <template <typename C, SourceRetrievable R> class Data,
                typename NotUnit, SourceRetrievable CanRetrieve>
      bool operator()(const Data<NotUnit, CanRetrieve>&) {
        return false;
      }

      bool operator()(const Retrievable<Unit>&) {
        MOZ_CRASH("source type only applies where actual text is available");
        return false;
      }

      template <typename NotUnit>
      bool operator()(const Retrievable<NotUnit>&) {
        return false;
      }

      bool operator()(const Missing&) {
        MOZ_CRASH("doesn't make sense to ask source type when missing");
        return false;
      }
    };

   public:
    template <typename Unit>
    bool hasSourceType() const {
      return data_.match(SourceTypeMatcher<Unit>());
    }

   private:
    struct UncompressedLengthMatcher {
      template <typename Unit, SourceRetrievable CanRetrieve>
      size_t operator()(const Uncompressed<Unit, CanRetrieve>& u) {
        return u.length();
      }

      template <typename Unit, SourceRetrievable CanRetrieve>
      size_t operator()(const Compressed<Unit, CanRetrieve>& u) {
        return u.uncompressedLength;
      }

      template <typename Unit>
      size_t operator()(const Retrievable<Unit>&) {
        MOZ_CRASH("ScriptSource::length on a missing-but-retrievable source");
        return 0;
      }

      size_t operator()(const Missing& m) {
        MOZ_CRASH("ScriptSource::length on a missing source");
        return 0;
      }
    };

   public:
    size_t length() const {
      MOZ_ASSERT(hasSourceText());
      return data_.match(UncompressedLengthMatcher());
    }

   private:
    template <typename Unit>
    struct UncompressedDataMatcher {
      template <SourceRetrievable CanRetrieve>
      const UncompressedData<Unit>* operator()(
          const Uncompressed<Unit, CanRetrieve>& u) {
        return &u;
      }

      template <typename T>
      const UncompressedData<Unit>* operator()(const T&) {
        MOZ_CRASH(
            "attempting to access uncompressed data in a ScriptSource not "
            "containing it");
        return nullptr;
      }
    };

   public:
    template <typename Unit>
    const UncompressedData<Unit>* uncompressedData() const {
      return data_.match(UncompressedDataMatcher<Unit>());
    }

   private:
    template <typename Unit>
    struct CompressedDataMatcher {
      template <SourceRetrievable CanRetrieve>
      const CompressedData<Unit>* operator()(
          const Compressed<Unit, CanRetrieve>& c) {
        return &c;
      }

      template <typename T>
      const CompressedData<Unit>* operator()(const T&) {
        MOZ_CRASH(
            "attempting to access compressed data in a ScriptSource not "
            "containing it");
        return nullptr;
      }
    };

   public:
    template <typename Unit>
    const CompressedData<Unit>* compressedData() const {
      return data_.match(CompressedDataMatcher<Unit>());
    }

   private:
    // Decompress and return the specified chunk of source code.
    // If maybeCx is nullptr, decompression still works but the uncompressed
    // result will not be cached. This allows off-main-thread callers to
    // decompress source without a JSContext, at the cost of potentially
    // decompressing the same chunk multiple times.
    template <typename Unit>
    const Unit* chunkUnits(JSContext* maybeCx,
                           UncompressedSourceCache::AutoHoldEntry& holder,
                           size_t chunk) const;

   public:
    // Return a string containing the chars starting at |begin| and ending at
    // |begin + len|.
    //
    // If maybeCx is nullptr, compressed sources will still be decompressed but
    // the result will not be cached. See chunkUnits comment above.
    template <typename Unit>
    const Unit* units(JSContext* maybeCx,
                      UncompressedSourceCache::AutoHoldEntry& asp, size_t begin,
                      size_t len) const;

    // A variant of |units| above, which returns a char sequence.
    template <typename Unit>
    const typename SourceTypeTraits<Unit>::CharT* unitsChars(
        JSContext* maybeCx, UncompressedSourceCache::AutoHoldEntry& asp,
        size_t begin, size_t len) const;

    template <typename Unit>
    const Unit* uncompressedUnits(size_t begin, size_t len) const;

    JSLinearString* substring(JSContext* cx, size_t start, size_t stop) const;
    JSLinearString* substringDontDeflate(JSContext* cx, size_t start,
                                         size_t stop) const;

    // Get substring characters without creating a JSString. Returns a variant
    // containing either UniqueChars (UTF-8) or UniqueTwoByteChars (UTF-16).
    //
    // IMPORTANT: The returned buffer is NOT null-terminated. Callers must track
    // the length separately (stop - start). This is designed for consumers that
    // store length explicitly (e.g., ProfilerJSSourceData).
    //
    // Callers must handle empty sources before calling this (the function
    // asserts non-empty length). Returns nullptr only on allocation failures.
    // Designed for off-main-thread use where JSContext is not available for
    // error reporting.
    SubstringCharsResult substringChars(size_t start, size_t stop) const;

    [[nodiscard]] bool appendSubstring(JSContext* cx, js::StringBuilder& buf,
                                       size_t start, size_t stop) const;

    JSLinearString* functionBodyString(JSContext* cx,
                                       ScriptSource* source) const;

    // Returns the function body substring. Unlike substringChars, this can
    // return an empty result (nullptr with *outLength == 0) for empty function
    // bodies. The caller doesn't need to check the length before calling.
    SubstringCharsResult functionBodyStringChars(ScriptSource* source,
                                                 size_t* outLength) const;

    // ==== Write units ====

    // Assign source data from |srcBuf| to this recently-created |ScriptSource|.
    template <typename Unit>
    [[nodiscard]] bool assignSource(FrontendContext* fc,
                                    const JS::ReadOnlyCompileOptions& options,
                                    ScriptSource* ss,
                                    JS::SourceText<Unit>& srcBuf);

   private:
    // Overwrites |data_| with the uncompressed data from |source|.
    //
    // This function asserts nothing about |data_|.  Users should use assertions
    // to double-check their own understandings of the |data_| state transition
    // being performed.
    template <typename ContextT, typename Unit>
    [[nodiscard]] bool setUncompressedSourceHelper(
        ContextT* cx, EntryUnits<Unit>&& source, size_t length,
        SourceRetrievable retrievable);

   public:
    // Initialize a fresh |ScriptSource| with unretrievable, uncompressed
    // source.
    template <typename Unit>
    [[nodiscard]] bool initializeUnretrievableUncompressedSource(
        FrontendContext* fc, EntryUnits<Unit>&& source, size_t length);

    // Set the retrieved source for a |ScriptSource| whose source was recorded
    // as missing but retrievable.
    template <typename Unit>
    [[nodiscard]] bool setRetrievedSource(JSContext* cx,
                                          EntryUnits<Unit>&& source,
                                          size_t length);

    // Initialize a fresh ScriptSource as containing unretrievable compressed
    // source of the indicated original encoding.
    template <typename Unit>
    [[nodiscard]] bool initializeWithUnretrievableCompressedSource(
        FrontendContext* fc, UniqueChars&& raw, size_t rawLength,
        size_t sourceLength);

    // ==== Compression ====

    void performTaskWork(SourceCompressionTaskEntry* task,
                         Compressor& comp) const;

   private:
    struct TriggerConvertToCompressedSourceFromTask;
    friend struct TriggerConvertToCompressedSourceFromTask;

    template <typename Unit>
    void convertToCompressedSource(SharedImmutableString compressed,
                                   size_t uncompressedLength);

   public:
    void triggerConvertToCompressedSourceFromTask(
        SharedImmutableString compressed);

   private:
    struct SetPendingCompressedFor;

   public:
    void setPendingCompressedFor(
        ExclusiveData<ExclusiveSourceDataLockState>::Guard& guard,
        SharedImmutableString compressed) const;

   private:
    SourceType data_ = SourceType(Missing());
  };

 public:
  // Provides mutable access to ExclusiveSourceData.
  // This can fail to get the write access if there's any reader.
  //
  // The consumer should check hasWriteAccess() before mutating.
  //
  // If hasWriteAccess() fails, getConst() can be used to get read-only
  // access.
  class DataWriter {
    // For DataReader ctor which reuses the lock.
    friend class DataReader;

   public:
    explicit DataWriter(ScriptSource* source)
        : source_(source), guard_(source_->sourceDataState_.lock()) {
      hasWriteAccess_ = !guard_->hasReaders();
    }

    explicit DataWriter(mozilla::Maybe<DataReader>&& reader);

    ~DataWriter() = default;

    bool hasWriteAccess() const { return hasWriteAccess_; }

    const ExclusiveSourceData* getConst() const {
      return &source_->sourceData_.getConst(*this);
    }

    ExclusiveSourceData* getMutable() const {
      MOZ_ASSERT(hasWriteAccess_);
      return &source_->sourceData_.getMutable(*this);
    }

    ExclusiveSourceData* operator->() { return getMutable(); }

    void triggerConvertToCompressedSourceFromTask(
        SharedImmutableString compressed);

   private:
    bool hasWriteAccess_ = false;
    ScriptSource* source_;

    ExclusiveData<ExclusiveSourceDataLockState>::Guard guard_;
  };

  // Provides read-only access to ExclusiveSourceData.
  //
  // The consumer should first check hasSourceText().
  //
  // If hasSourceText() returns true, the properties read through the
  // DataReader is guaranteed to be consistent only while the DataReader is
  // alive.
  // Once the DataReader goes out of the scope, the underlying buffer can be
  // compressed, and the properties retrieved before that can go out of sync.
  //
  // If the consumer want to get the source length, and then access the units,
  // it should use single DataReader for both calls.
  //
  // If hasSourceText() returns false, the access to the ExclusiveSourceData
  // is denied from this DataReader instance. For this case, the underlying
  // ExclusiveSourceData is either Retrievable or Missing, and the Retrievable
  // state can make a transition to the Uncompressed state when the source is
  // loaded.  DataReader instances constructed after the transition will have
  // hasSourceText()==true.
  class DataReader {
    // For getMutableForDelayedConvertToCompressedSource.
    friend class ExclusiveSourceDataLockState;

    // For DataWriter ctor which takes over the implicit lock.
    friend class DataWriter;

   public:
    explicit DataReader(ScriptSource* source) : source_(source) {
      auto guard_ = source_->sourceDataState_.lock();
      getConstWithLock()->getSourceProperties(&hasSourceText_, &retrievable_,
                                              &isTwoByteString_);
      if (hasSourceText_) {
        guard_->addReader();
      }
    }

    explicit DataReader(mozilla::Maybe<DataReader>&& reader)
        : source_(reader->source_),
          hasSourceText_(reader->hasSourceText_),
          retrievable_(reader->retrievable_),
          isTwoByteString_(reader->isTwoByteString_) {
      reader->source_ = nullptr;
      reader.reset();
    }

    explicit DataReader(mozilla::Maybe<DataWriter>&& writer)
        : source_(writer->source_) {
      getConstWithLock()->getSourceProperties(&hasSourceText_, &retrievable_,
                                              &isTwoByteString_);
      if (hasSourceText_) {
        (*writer).guard_->addReader();
      }
      writer.reset();
    }

    ~DataReader() {
      if (!source_) {
        // Already converted into other DataReader or DataWriter.
        return;
      }
      if (!hasSourceText_) {
        // Constructor didn't add reader.
        return;
      }
      auto guard_ = source_->sourceDataState_.lock();
      guard_->removeReader(source_, *this);
    }

    bool hasSourceText() const { return hasSourceText_; }
    bool isRetrievable() const { return retrievable_; }
    bool isTwoByteString() const { return isTwoByteString_; }

    const ExclusiveSourceData* getConst() const {
      MOZ_ASSERT(hasSourceText());
      return &source_->sourceData_.getConst(*this);
    }

    const ExclusiveSourceData* operator->() const { return getConst(); }

   private:
    const ExclusiveSourceData* getConstWithLock() const {
      return &source_->sourceData_.getConst(*this);
    }

    ExclusiveSourceData* getMutableForDelayedConvertToCompressedSource() {
      MOZ_ASSERT(hasSourceText());
      return &source_->sourceData_
                  .getMutableForDelayedConvertToCompressedSource(*this);
    }

    ScriptSource* source_;

    bool hasSourceText_;
    bool retrievable_;
    bool isTwoByteString_;
  };

 private:
  // Provides lock for ExclusiveSourceData.
  // See the comment above ExclusiveSourceData.
  template <typename T>
  class RWLocked {
    friend class DataReader;
    friend class DataWriter;

    const T& getConst(const DataReader&) const { return value; }

    T& getMutableForDelayedConvertToCompressedSource(const DataReader&) {
      return value;
    }

    const T& getConst(const DataWriter&) const { return value; }

    T& getMutable(const DataWriter&) { return value; }

    T value;
  };

  //
  // Start of fields.
  //

  mozilla::Atomic<uint32_t, mozilla::ReleaseAcquire> refs = {};

  // An id for this source that is unique across the process. This can be used
  // to refer to this source from places that don't want to hold a strong
  // reference on the source itself.
  //
  // This is a 32 bit ID and could overflow, in which case the ID will not be
  // unique anymore.
  uint32_t id_ = 0;

  RWLocked<ExclusiveSourceData> sourceData_;

  ExclusiveData<ExclusiveSourceDataLockState> sourceDataState_;

  // The UTF-8 encoded filename of this script.
  SharedImmutableString filename_;

  // Hash of the script filename;
  HashNumber filenameHash_ = 0;

  // If this ScriptSource was generated by a code-introduction mechanism such
  // as |eval| or |new Function|, the debugger needs access to the "raw"
  // filename of the top-level script that contains the eval-ing code.  To
  // keep track of this, we must preserve the original outermost filename (of
  // the original introducer script), so that instead of a filename of
  // "foo.js line 30 > eval line 10 > Function", we can obtain the original
  // raw filename of "foo.js".
  //
  // In the case described above, this field will be set to to the original raw
  // UTF-8 encoded filename from above, otherwise it will be mozilla::Nothing.
  SharedImmutableString introducerFilename_;

  SharedImmutableTwoByteString displayURL_;
  SharedImmutableTwoByteString sourceMapURL_;

  // A string indicating how this source code was introduced into the system.
  // This is a constant, statically allocated C string, so does not need memory
  // management.
  //
  // TODO: Document the various additional introduction type constants.
  const char* introductionType_ = nullptr;

  // Bytecode offset in caller script that generated this code.  This is
  // present for eval-ed code, as well as "new Function(...)"-introduced
  // scripts.
  mozilla::Maybe<uint32_t> introductionOffset_;

  // If this source is for Function constructor, the position of ")" after
  // parameter list in the source.  This is used to get function body.
  // 0 for other cases.
  uint32_t parameterListEnd_ = 0;

  // Line number within the file where this source starts (1-origin).
  uint32_t startLine_ = 0;
  // Column number within the file where this source starts,
  // in UTF-16 code units.
  JS::LimitedColumnNumberOneOrigin startColumn_;

  // See: CompileOptions::mutedErrors.
  bool mutedErrors_ = false;

  // Carry the delazification mode per source.
  JS::DelazificationOption delazificationMode_ =
      JS::DelazificationOption::OnDemandOnly;

  // True if an associated SourceCompressionTask was ever created.
  bool hadCompressionTask_ = false;

  //
  // End of fields.
  //

  // How many ids have been handed out to sources.
  static mozilla::Atomic<uint32_t, mozilla::SequentiallyConsistent> idCount_;

 public:
  // When creating a JSString* from TwoByte source characters, we don't try to
  // to deflate to Latin1 for longer strings, because this can be slow.
  static const size_t SourceDeflateLimit = 100;

  explicit ScriptSource()
      : id_(++idCount_), sourceDataState_(js::mutexid::SourceCompression) {}
  ~ScriptSource() { MOZ_ASSERT(refs == 0); }

  void AddRef() { refs++; }
  void Release() {
    MOZ_ASSERT(refs != 0);
    if (--refs == 0) {
      js_delete(this);
    }
  }
  [[nodiscard]] bool initFromOptions(FrontendContext* fc,
                                     const JS::ReadOnlyCompileOptions& options);

  /**
   * The minimum script length (in code units) necessary for a script to be
   * eligible to be compressed.
   */
  static constexpr size_t MinimumCompressibleLength = 256;

  SharedImmutableString getOrCreateStringZ(FrontendContext* fc,
                                           UniqueChars&& str);
  SharedImmutableTwoByteString getOrCreateStringZ(FrontendContext* fc,
                                                  UniqueTwoByteChars&& str);

 private:
  template <typename Unit>
  bool setRetrievedSource(JSContext* cx,
                          mozilla::Maybe<ScriptSource::DataReader>& readerIn,
                          mozilla::Maybe<ScriptSource::DataReader>& readerOut,
                          Unit* source, size_t length);

 public:
  // Attempt to load usable source -- source text on which substring operations
  // and the like can be performed.  On success return true and set |*loaded|
  // to indicate whether usable source could be loaded; otherwise return false.
  //
  // In both case, the reader is emplaced for this ScriptSource, and the
  // consumer should use the reader to operate on the loaded source.
  // The |*loaded| value is valid only while the reader is alive.
  //
  // Once the reader goes out of the scope, the source can be compressed.
  bool tryLoadSource(JSContext* cx,
                     mozilla::Maybe<ScriptSource::DataReader>& reader,
                     bool* loaded);

  void setParameterListEnd(uint32_t parameterListEnd) {
    parameterListEnd_ = parameterListEnd;
  }

  bool isFunctionBody() const { return parameterListEnd_ != 0; }

  // Returns true if this source should display only the function body.
  // In case of DOM event handler like <div onclick="foo()" the JS code is
  // wrapped into
  //   function onclick() {foo()}
  // We want to only return `foo()` here.
  // But only for event handlers, for `new Function("foo()")`, we want to
  // return:
  //   function anonymous() {foo()}
  bool shouldUnwrapEventHandlerBody() const {
    return hasIntroductionType() &&
           strcmp(introductionType(), "eventHandler") == 0 && isFunctionBody();
  }

  void addSizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf,
                              JS::ScriptSourceInfo* info) const;

  [[nodiscard]] bool tryCompressOffThread(JSContext* cx);

  // Called by the SourceCompressionTask constructor to indicate such a task was
  // ever created.
  void noteSourceCompressionTask() { hadCompressionTask_ = true; }

  HashNumber filenameHash() const { return filenameHash_; }
  const char* filename() const {
    return filename_ ? filename_.chars() : nullptr;
  }
  [[nodiscard]] bool setFilename(FrontendContext* fc, const char* filename);
  [[nodiscard]] bool setFilename(FrontendContext* fc, UniqueChars&& filename);

  bool hasIntroducerFilename() const {
    return introducerFilename_ ? true : false;
  }
  const char* introducerFilename() const {
    return introducerFilename_ ? introducerFilename_.chars() : filename();
  }
  [[nodiscard]] bool setIntroducerFilename(FrontendContext* fc,
                                           const char* filename);
  [[nodiscard]] bool setIntroducerFilename(FrontendContext* fc,
                                           UniqueChars&& filename);

  bool hasIntroductionType() const { return introductionType_; }
  const char* introductionType() const {
    MOZ_ASSERT(hasIntroductionType());
    return introductionType_;
  }

  uint32_t id() const { return id_; }

  // Display URLs
  [[nodiscard]] bool setDisplayURL(FrontendContext* fc, const char16_t* url);
  [[nodiscard]] bool setDisplayURL(FrontendContext* fc,
                                   UniqueTwoByteChars&& url);
  bool hasDisplayURL() const { return bool(displayURL_); }
  const char16_t* displayURL() { return displayURL_.chars(); }

  // Source maps
  [[nodiscard]] bool setSourceMapURL(FrontendContext* fc, const char16_t* url);
  [[nodiscard]] bool setSourceMapURL(FrontendContext* fc,
                                     UniqueTwoByteChars&& url);
  bool hasSourceMapURL() const { return bool(sourceMapURL_); }
  const char16_t* sourceMapURL() { return sourceMapURL_.chars(); }

  bool mutedErrors() const { return mutedErrors_; }

  uint32_t startLine() const { return startLine_; }
  JS::LimitedColumnNumberOneOrigin startColumn() const { return startColumn_; }

  JS::DelazificationOption delazificationMode() const {
    return delazificationMode_;
  }

  bool hasIntroductionOffset() const { return introductionOffset_.isSome(); }
  uint32_t introductionOffset() const { return introductionOffset_.value(); }
  void setIntroductionOffset(uint32_t offset) {
    MOZ_ASSERT(!hasIntroductionOffset());
    MOZ_ASSERT(offset <= (uint32_t)INT32_MAX);
    introductionOffset_.emplace(offset);
  }
};

// [SMDOC] ScriptSourceObject
//
// ScriptSourceObject stores the ScriptSource and GC pointers related to it.
class ScriptSourceObject : public NativeObject {
  static const JSClassOps classOps_;

 public:
  static const JSClass class_;

  static void finalize(JS::GCContext* gcx, JSObject* obj);

  static ScriptSourceObject* create(JSContext* cx, ScriptSource* source);

  static ScriptSourceObject* createForWasmModule(JSContext* cx);

  // Initialize those properties of this ScriptSourceObject whose values
  // are provided by |options|, re-wrapping as necessary.
  static bool initFromOptions(JSContext* cx,
                              JS::Handle<ScriptSourceObject*> source,
                              const JS::InstantiateOptions& options);

  static bool initElementProperties(JSContext* cx,
                                    JS::Handle<ScriptSourceObject*> source,
                                    HandleString elementAttrName);

  bool hasSource() const { return !getReservedSlot(SOURCE_SLOT).isUndefined(); }
  ScriptSource* source() const {
    MOZ_RELEASE_ASSERT(hasSource());
    return static_cast<ScriptSource*>(getReservedSlot(SOURCE_SLOT).toPrivate());
  }

  JSObject* unwrappedElement(JSContext* cx) const;

  const Value& unwrappedElementAttributeName() const {
    MOZ_ASSERT(isInitialized());
    const Value& v = getReservedSlot(ELEMENT_PROPERTY_SLOT);
    MOZ_ASSERT(!v.isMagic());
    return v;
  }
  BaseScript* unwrappedIntroductionScript() const {
    MOZ_ASSERT(isInitialized());
    Value value = getReservedSlot(INTRODUCTION_SCRIPT_SLOT);
    if (value.isUndefined()) {
      return nullptr;
    }
    return value.toGCThing()->as<BaseScript>();
  }

  void setPrivate(JSRuntime* rt, const Value& value);
  void clearPrivate(JSRuntime* rt);

  void setIntroductionScript(const Value& introductionScript) {
    setReservedSlot(INTRODUCTION_SCRIPT_SLOT, introductionScript);
  }

  Value getPrivate() const {
    MOZ_ASSERT(isInitialized());
    Value value = getReservedSlot(PRIVATE_SLOT);
    return value;
  }

 private:
#ifdef DEBUG
  bool isInitialized() const {
    Value element = getReservedSlot(ELEMENT_PROPERTY_SLOT);
    if (element.isMagic(JS_GENERIC_MAGIC)) {
      return false;
    }
    return !getReservedSlot(INTRODUCTION_SCRIPT_SLOT).isMagic(JS_GENERIC_MAGIC);
  }
#endif

  enum {
    PRIVATE_SLOT = 0,  // Must be first slot for JSCLASS_SLOT0_IS_NSISUPPORTS.
    SOURCE_SLOT,
    ELEMENT_PROPERTY_SLOT,
    INTRODUCTION_SCRIPT_SLOT,
    STENCILS_SLOT,
    RESERVED_SLOTS
  };

  // Delazification stencils can be aggregated in
  // InitialStencilAndDelazification, this structure might be used for
  // different purposes.
  //  - Collecting: The goal is to aggregate all delazified functions in order
  //    to aggregate them for serialization.
  //  - Sharing: The goal is to use the InitialStencilAndDelazification as a way
  //    to share multiple threads efforts towards parsing a Script Source
  //    content.
  //
  // See setCollectingDelazifications and setSharingDelazifications for details.
  static constexpr uintptr_t STENCILS_COLLECTING_DELAZIFICATIONS_FLAG = 0x1;
  static constexpr uintptr_t STENCILS_SHARING_DELAZIFICATIONS_FLAG = 0x2;
  static constexpr uintptr_t STENCILS_MASK = 0x3;

  void clearStencils();

  template <uintptr_t flag>
  void setStencilsFlag();

  template <uintptr_t flag>
  void unsetStencilsFlag();

  template <uintptr_t flag>
  bool isStencilsFlagSet() const;

 public:
  // Associate stencils to this ScriptSourceObject.
  // The consumer should call setCollectingDelazifications or
  // setSharingDelazifications after this.
  void setStencils(
      already_AddRefed<frontend::InitialStencilAndDelazifications> stencils);

  // Start collecting delazifications.
  // This is a temporary state until unsetCollectingDelazifications is called,
  // and this expects a pair of set/unset call.
  //
  // The caller should check isCollectingDelazifications before calling this.
  void setCollectingDelazifications();

  // Clear the flag for collecting delazifications.
  //
  // If setSharingDelazifications wasn't called, this clears the association
  // with the stencils.
  void unsetCollectingDelazifications();

  // Returns true if setCollectingDelazifications was called and
  // unsetCollectingDelazifications is not yet called.
  bool isCollectingDelazifications() const;

  // Start sharing delazifications with others.
  // This is a permanent state.
  //
  // The flag is orthogonal to setCollectingDelazifications.
  void setSharingDelazifications();

  // Returns true if setSharingDelazifications was called.
  bool isSharingDelazifications() const;

  // Return the associated stencils if any.
  // Returns nullptr if stencils is not associated
  frontend::InitialStencilAndDelazifications* maybeGetStencils();
};

// ScriptWarmUpData represents a pointer-sized field in BaseScript that stores
// one of the following using low-bit tags:
//
// * The enclosing BaseScript. This is only used while this script is lazy and
//   its containing script is also lazy. This outer script must be compiled
//   before the current script can in order to correctly build the scope chain.
//
// * The enclosing Scope. This is only used while this script is lazy and its
//   containing script is compiled. This is the outer scope chain that will be
//   used to compile this scipt.
//
// * The script's warm-up count. This is only used until the script has a
//   JitScript. The Baseline Interpreter and JITs use the warm-up count stored
//   in JitScript.
//
// * A pointer to the JitScript, when the script is warm enough for the Baseline
//   Interpreter.
//
class ScriptWarmUpData {
  GCData<uintptr_t> data_{ResetState()};

 private:
  static constexpr uintptr_t NumTagBits = 2;
  static constexpr uint32_t MaxWarmUpCount = UINT32_MAX >> NumTagBits;

 public:
  // Public only for the JITs.
  static constexpr uintptr_t TagMask = (1 << NumTagBits) - 1;
  static constexpr uintptr_t JitScriptTag = 0;
  static constexpr uintptr_t EnclosingScriptTag = 1;
  static constexpr uintptr_t EnclosingScopeTag = 2;
  static constexpr uintptr_t WarmUpCountTag = 3;

 private:
  explicit ScriptWarmUpData(uintptr_t data) : data_(data) {}

  // A gc-safe value to clear to.
  constexpr uintptr_t ResetState() { return 0 | WarmUpCountTag; }

  template <uintptr_t Tag>
  inline void setTaggedPtr(void* ptr) {
    static_assert(Tag <= TagMask, "Tag must fit in TagMask");
    MOZ_ASSERT((uintptr_t(ptr) & TagMask) == 0);
    data_ = uintptr_t(ptr) | Tag;
  }

  template <typename T, uintptr_t Tag>
  inline T getTaggedPtr() const {
    static_assert(Tag <= TagMask, "Tag must fit in TagMask");
    MOZ_ASSERT((data_ & TagMask) == Tag);
    return reinterpret_cast<T>(data_ & ~TagMask);
  }

  void setWarmUpCount(uint32_t count) {
    if (count > MaxWarmUpCount) {
      count = MaxWarmUpCount;
    }
    data_ = (uintptr_t(count) << NumTagBits) | WarmUpCountTag;
  }

 public:
  ScriptWarmUpData() = default;

  void trace(JSTracer* trc);

  bool isEnclosingScript() const {
    return (data_ & TagMask) == EnclosingScriptTag;
  }
  bool isEnclosingScope() const {
    return (data_ & TagMask) == EnclosingScopeTag;
  }
  bool isWarmUpCount() const { return (data_ & TagMask) == WarmUpCountTag; }
  bool isJitScript() const { return (data_ & TagMask) == JitScriptTag; }

  // NOTE: To change type safely, 'clear' the old tagged value and then 'init'
  //       the new one. This will notify the GC appropriately.

  BaseScript* toEnclosingScript() const {
    return getTaggedPtr<BaseScript*, EnclosingScriptTag>();
  }
  inline void initEnclosingScript(BaseScript* enclosingScript);
  inline void clearEnclosingScript();

  Scope* toEnclosingScope() const {
    return getTaggedPtr<Scope*, EnclosingScopeTag>();
  }
  inline void initEnclosingScope(Scope* enclosingScope);
  inline void clearEnclosingScope();

  uint32_t toWarmUpCount() const {
    MOZ_ASSERT(isWarmUpCount());
    return data_ >> NumTagBits;
  }
  void resetWarmUpCount(uint32_t count) {
    MOZ_ASSERT(isWarmUpCount());
    setWarmUpCount(count);
  }
  void incWarmUpCount() {
    MOZ_ASSERT(isWarmUpCount());
    data_ += uintptr_t(1) << NumTagBits;
  }

  jit::JitScript* toJitScript() const {
    return getTaggedPtr<jit::JitScript*, JitScriptTag>();
  }
  void initJitScript(jit::JitScript* jitScript) {
    MOZ_ASSERT(isWarmUpCount());
    setTaggedPtr<JitScriptTag>(jitScript);
  }
  void clearJitScript() {
    MOZ_ASSERT(isJitScript());
    data_ = ResetState();
  }
} JS_HAZ_GC_POINTER;

static_assert(sizeof(ScriptWarmUpData) == sizeof(uintptr_t),
              "JIT code depends on ScriptWarmUpData being pointer-sized");

// [SMDOC] - JSScript data layout (unshared)
//
// PrivateScriptData stores variable-length data associated with a script.
// Abstractly a PrivateScriptData consists of the following:
//
//   * A non-empty array of GCCellPtr in gcthings()
//
// Accessing this array just requires calling the appropriate public
// Span-computing function.
class MOZ_EMPTY_BASES alignas(uintptr_t) PrivateScriptData final
    : public TrailingArray<PrivateScriptData>,
      public gc::Buffer {
  uint32_t ngcthings = 0;

  // Note: This is only defined for scripts with an enclosing scope. This
  // excludes lazy scripts with lazy parents.
  js::MemberInitializers memberInitializers_ =
      js::MemberInitializers::Invalid();

  // End of fields.

  // Layout helpers
  Offset gcThingsOffset() { return offsetOfGCThings(); }
  Offset endOffset() const {
    uintptr_t size = ngcthings * sizeof(JS::GCCellPtr);
    return offsetOfGCThings() + size;
  }

 public:
  // Initialize header and PackedSpans
  explicit PrivateScriptData(uint32_t ngcthings);

  static constexpr size_t offsetOfGCThings() {
    return sizeof(PrivateScriptData);
  }

  // Accessors for typed array spans.
  mozilla::Span<JS::GCCellPtr> gcthings() {
    Offset offset = offsetOfGCThings();
    return mozilla::Span{offsetToPointer<JS::GCCellPtr>(offset), ngcthings};
  }

  void setMemberInitializers(MemberInitializers memberInitializers) {
    MOZ_ASSERT(memberInitializers_.valid == false,
               "Only init MemberInitializers once");
    memberInitializers_ = memberInitializers;
  }
  const MemberInitializers& getMemberInitializers() {
    return memberInitializers_;
  }

  // Allocate a new PrivateScriptData. Headers and GCCellPtrs are initialized.
  static PrivateScriptData* new_(JSContext* cx, uint32_t ngcthings);

  static bool InitFromStencil(
      JSContext* cx, js::HandleScript script,
      const js::frontend::CompilationAtomCache& atomCache,
      const js::frontend::CompilationStencil& stencil,
      js::frontend::CompilationGCOutput& gcOutput,
      const js::frontend::ScriptIndex scriptIndex);

  void trace(JSTracer* trc);

  size_t allocationSize() const;

  // PrivateScriptData has trailing data so isn't copyable or movable.
  PrivateScriptData(const PrivateScriptData&) = delete;
  PrivateScriptData& operator=(const PrivateScriptData&) = delete;
};

// An entry in the runtime's pendingCompressions_ list for a single
// ScriptSource.
//
// It is not desirable to eagerly compress: if lazy functions that are tied to
// the ScriptSource were to be executed relatively soon after parsing, they
// would need to block on decompression, which hurts responsiveness.
//
// To this end, script sources are enqueued in a pending list by
// ScriptSource::tryCompressOffThread. When a major GC occurs, we allocate and
// submit SourceCompressionTasks for them. Currently, a script source is
// considered ready 2 major GCs after being enqueued.
class PendingSourceCompressionEntry {
  // The major GC number of the runtime when the entry was enqueued.
  uint64_t majorGCNumber_;

  // The source to be compressed.
  RefPtr<ScriptSource> source_;

 public:
  PendingSourceCompressionEntry(JSRuntime* rt, ScriptSource* source);

  ScriptSource* source() const { return source_.get(); }
  uint64_t majorGCNumber() const { return majorGCNumber_; }
  bool shouldCancel() const {
    // If the refcount is exactly 1, then nothing else is holding on to the
    // ScriptSource, so no reason to compress it and we should cancel the
    // compression.
    return source_->refs == 1;
  }
};

// [SMDOC] Script Representation (js::BaseScript)
//
// A "script" corresponds to a JavaScript function or a top-level (global, eval,
// module) body that will be executed using SpiderMonkey bytecode.
//
// BaseScript may be generated directly from the parser/emitter, or by cloning
// or deserializing another script. Cloning is typically used when a script is
// needed in multiple realms and we would like to avoid re-compiling.
//
// A single script may be shared by multiple JSFunctions in a realm when those
// function objects are used as closure. In this case, a single JSFunction is
// considered canonical (and often does not escape to script directly).
//
// A BaseScript may be in "lazy" form where the parser performs a syntax-only
// parse and saves minimal information. These lazy scripts must be recompiled
// from the source (generating bytecode) before they can execute in a process
// called "delazification". On GC memory pressure, a fully-compiled script may
// be converted back into lazy form by "relazification".
//
// A fully-initialized BaseScript can be identified with `hasBytecode()` and
// will have bytecode and set of GC-things such as scopes, inner-functions, and
// object/string literals. This is referred to as a "non-lazy" script.
//
// A lazy script has either an enclosing script or scope. Each script needs to
// know its enclosing scope in order to be fully compiled. If the parent is
// still lazy we track that script and will need to compile it first to know our
// own enclosing scope. This is because scope objects are not created until full
// compilation and bytecode generation.
//
//
// # Script Warm-Up #
//
// A script evolves its representation over time. As it becomes "hotter" we
// attach a stack of additional data-structures generated by the JITs to
// speed-up execution. This evolution may also be run in reverse, in order to
// reduce memory usage.
//
//              +-------------------------------------+
//              | ScriptSource                        |
//              |   Provides:   Source                |
//              |   Engine:     Parser                |
//              +-------------------------------------+
//                                v
//              +-----------------------------------------------+
//              | BaseScript                                    |
//              |   Provides:   SourceExtent/Bindings           |
//              |   Engine:     CompileLazyFunctionToStencil    |
//              |               /InstantiateStencilsForDelazify |
//              +-----------------------------------------------+
//                                v
//              +-------------------------------------+
//              | ImmutableScriptData                 |
//              |   Provides:   Bytecode              |
//              |   Engine:     Interpreter           |
//              +-------------------------------------+
//                                v
//              +-------------------------------------+
//              | JitScript                           |
//              |   Provides:   Inline Caches (ICs)   |
//              |   Engine:     BaselineInterpreter   |
//              +-------------------------------------+
//                                v
//              +-------------------------------------+
//              | BaselineScript                      |
//              |   Provides:   Native Code           |
//              |   Engine:     Baseline              |
//              +-------------------------------------+
//                                v
//              +-------------------------------------+
//              | IonScript                           |
//              |   Provides:   Optimized Native Code |
//              |   Engine:     IonMonkey             |
//              +-------------------------------------+
//
// NOTE: Scripts may be directly created with bytecode and skip the lazy script
//       form. This is always the case for top-level scripts.
class BaseScript : public gc::TenuredCellWithNonGCPointer<uint8_t> {
  friend class js::gc::CellAllocator;

 public:
  // Pointer to baseline->method()->raw(), ion->method()->raw(), a wasm jit
  // entry, the JIT's EnterInterpreter stub, or the lazy link stub. Must be
  // non-null (except on no-jit builds). This is stored in the cell header.
  uint8_t* jitCodeRaw() const { return headerPtr(); }

 protected:
  // Multi-purpose value that changes type as the script warms up from lazy form
  // to interpreted-bytecode to JITs. See: ScriptWarmUpData type for more info.
  ScriptWarmUpData warmUpData_ = {};

  // For function scripts this is the canonical function, otherwise nullptr.
  const GCPtr<JSFunction*> function_ = {};

  // The ScriptSourceObject for this script. This is always same-compartment and
  // same-realm with this script.
  const GCPtr<ScriptSourceObject*> sourceObject_ = {};

  // Position of the function in the source buffer. Both in terms of line/column
  // and code-unit offset.
  const SourceExtent extent_ = {};

  // Immutable flags are a combination of parser options and bytecode
  // characteristics. These flags are preserved when serializing or copying this
  // script.
  const ImmutableScriptFlags immutableFlags_ = {};

  // Mutable flags store transient information used by subsystems such as the
  // debugger and the JITs. These flags are *not* preserved when serializing or
  // cloning since they are based on runtime state.
  MutableScriptFlags mutableFlags_ = {};

  // Variable-length data owned by this script. This stores one of:
  //    - GC pointers that bytecode references.
  //    - Inner-functions and bindings generated by syntax parse.
  //    - Nullptr, if no bytecode or inner functions.
  // This is updated as script is delazified and relazified.
  GCBuffer<PrivateScriptData*> data_;

  // Shareable script data. This includes runtime-wide atom pointers, bytecode,
  // and various script note structures. If the script is currently lazy, this
  // will be nullptr.
  RefPtr<js::SharedImmutableScriptData> sharedData_ = {};

  // End of fields.

  BaseScript(uint8_t* stubEntry, JSFunction* function,
             ScriptSourceObject* sourceObject, const SourceExtent& extent,
             uint32_t immutableFlags);

  void setJitCodeRaw(uint8_t* code) { setHeaderPtr(code); }

 public:
  static BaseScript* New(JSContext* cx, JS::Handle<JSFunction*> function,
                         JS::Handle<js::ScriptSourceObject*> sourceObject,
                         const js::SourceExtent& extent,
                         uint32_t immutableFlags);

  // Create a lazy BaseScript without initializing any gc-things.
  static BaseScript* CreateRawLazy(JSContext* cx, uint32_t ngcthings,
                                   HandleFunction fun,
                                   JS::Handle<ScriptSourceObject*> sourceObject,
                                   const SourceExtent& extent,
                                   uint32_t immutableFlags);

  bool isUsingInterpreterTrampoline(JSRuntime* rt) const;

  // Canonical function for the script, if it has a function. For top-level
  // scripts this is nullptr.
  JSFunction* function() const { return function_; }

  JS::Realm* realm() const { return sourceObject()->realm(); }
  JS::Compartment* compartment() const { return sourceObject()->compartment(); }
  JS::Compartment* maybeCompartment() const { return compartment(); }
  inline JSPrincipals* principals() const;

  ScriptSourceObject* sourceObject() const { return sourceObject_; }
  ScriptSource* scriptSource() const { return sourceObject()->source(); }
  ScriptSource* maybeForwardedScriptSource() const;

  bool mutedErrors() const { return scriptSource()->mutedErrors(); }

  const char* filename() const { return scriptSource()->filename(); }
  HashNumber filenameHash() const { return scriptSource()->filenameHash(); }
  const char* maybeForwardedFilename() const {
    return maybeForwardedScriptSource()->filename();
  }

  uint32_t sourceStart() const { return extent_.sourceStart; }
  uint32_t sourceEnd() const { return extent_.sourceEnd; }
  uint32_t sourceLength() const {
    return extent_.sourceEnd - extent_.sourceStart;
  }
  uint32_t toStringStart() const { return extent_.toStringStart; }
  uint32_t toStringEnd() const { return extent_.toStringEnd; }
  SourceExtent extent() const { return extent_; }

  [[nodiscard]] bool appendSourceDataForToString(JSContext* cx,
                                                 js::StringBuilder& buf);

  // Line number (1-origin)
  uint32_t lineno() const { return extent_.lineno; }
  // Column number in UTF-16 code units
  JS::LimitedColumnNumberOneOrigin column() const { return extent_.column; }

  JS::DelazificationOption delazificationMode() const {
    return scriptSource()->delazificationMode();
  }

 public:
  ImmutableScriptFlags immutableFlags() const { return immutableFlags_; }
  RO_IMMUTABLE_SCRIPT_FLAGS(immutableFlags_)
  RW_MUTABLE_SCRIPT_FLAGS(mutableFlags_)

  bool hasEnclosingScript() const { return warmUpData_.isEnclosingScript(); }
  BaseScript* enclosingScript() const {
    return warmUpData_.toEnclosingScript();
  }
  void setEnclosingScript(BaseScript* enclosingScript);

  // Returns true is the script has an enclosing scope but no bytecode. It is
  // ready for delazification.
  // NOTE: The enclosing script must have been successfully compiled at some
  // point for the enclosing scope to exist. That script may have since been
  // GC'd, but we kept the scope live so we can still compile ourselves.
  bool isReadyForDelazification() const {
    return warmUpData_.isEnclosingScope();
  }

  Scope* enclosingScope() const;
  void setEnclosingScope(Scope* enclosingScope);
  Scope* releaseEnclosingScope();

  bool hasJitScript() const { return warmUpData_.isJitScript(); }
  jit::JitScript* jitScript() const {
    MOZ_ASSERT(hasJitScript());
    return warmUpData_.toJitScript();
  }
  jit::JitScript* maybeJitScript() const {
    return hasJitScript() ? jitScript() : nullptr;
  }

  inline bool hasBaselineScript() const;
  inline bool hasIonScript() const;

  bool hasPrivateScriptData() const { return data_ != nullptr; }

  // Update data_ pointer and trigger barriers.
  void swapData(MutableHandleBuffer<PrivateScriptData> other);

  void freeData();

  mozilla::Span<const JS::GCCellPtr> gcthings() const {
    return data_ ? data_->gcthings() : mozilla::Span<JS::GCCellPtr>();
  }

  // NOTE: This is only used to initialize a fresh script.
  mozilla::Span<JS::GCCellPtr> gcthingsForInit() {
    MOZ_ASSERT(!hasBytecode());
    return data_ ? data_->gcthings() : mozilla::Span<JS::GCCellPtr>();
  }

  void setMemberInitializers(MemberInitializers memberInitializers) {
    MOZ_ASSERT(useMemberInitializers());
    MOZ_ASSERT(data_);
    data_->setMemberInitializers(memberInitializers);
  }
  const MemberInitializers& getMemberInitializers() const {
    MOZ_ASSERT(data_);
    return data_->getMemberInitializers();
  }

  SharedImmutableScriptData* sharedData() const { return sharedData_; }
  void initSharedData(SharedImmutableScriptData* data) {
    MOZ_ASSERT(sharedData_ == nullptr);
    sharedData_ = data;
  }
  void freeSharedData() { sharedData_ = nullptr; }

  // NOTE: Script only has bytecode if JSScript::fullyInitFromStencil completes
  // successfully.
  bool hasBytecode() const {
    if (sharedData_) {
      MOZ_ASSERT(data_);
      MOZ_ASSERT(warmUpData_.isWarmUpCount() || warmUpData_.isJitScript());
      return true;
    }
    return false;
  }

 public:
  static const JS::TraceKind TraceKind = JS::TraceKind::Script;

  void traceChildren(JSTracer* trc);
  void finalize(JS::GCContext* gcx);

  size_t sizeOfExcludingThis();

  inline JSScript* asJSScript();

  // JIT accessors
  static constexpr size_t offsetOfJitCodeRaw() { return offsetOfHeaderPtr(); }
  static constexpr size_t offsetOfPrivateData() {
    return offsetof(BaseScript, data_);
  }
  static constexpr size_t offsetOfSharedData() {
    return offsetof(BaseScript, sharedData_);
  }
  static size_t offsetOfImmutableFlags() {
    static_assert(sizeof(ImmutableScriptFlags) == sizeof(uint32_t));
    return offsetof(BaseScript, immutableFlags_);
  }
  static constexpr size_t offsetOfMutableFlags() {
    static_assert(sizeof(MutableScriptFlags) == sizeof(uint32_t));
    return offsetof(BaseScript, mutableFlags_);
  }
  static constexpr size_t offsetOfWarmUpData() {
    return offsetof(BaseScript, warmUpData_);
  }

#if defined(DEBUG) || defined(JS_JITSPEW)
  void dumpStringContent(js::GenericPrinter& out) const;
#endif
};

extern void SweepScriptData(JSRuntime* rt);

} /* namespace js */

class JSScript : public js::BaseScript {
 private:
  friend bool js::PrivateScriptData::InitFromStencil(
      JSContext* cx, js::HandleScript script,
      const js::frontend::CompilationAtomCache& atomCache,
      const js::frontend::CompilationStencil& stencil,
      js::frontend::CompilationGCOutput& gcOutput,
      const js::frontend::ScriptIndex scriptIndex);

 private:
  using js::BaseScript::BaseScript;

 public:
  static JSScript* Create(JSContext* cx, JS::Handle<JSFunction*> function,
                          JS::Handle<js::ScriptSourceObject*> sourceObject,
                          const js::SourceExtent& extent,
                          js::ImmutableScriptFlags flags);

  // NOTE: This should only be used while delazifying.
  static JSScript* CastFromLazy(js::BaseScript* lazy) {
    return static_cast<JSScript*>(lazy);
  }

 public:
  static bool fullyInitFromStencil(
      JSContext* cx, const js::frontend::CompilationAtomCache& atomCache,
      const js::frontend::CompilationStencil& stencil,
      js::frontend::CompilationGCOutput& gcOutput, js::HandleScript script,
      const js::frontend::ScriptIndex scriptIndex);

  // Allocate a JSScript and initialize it with bytecode. This consumes
  // allocations within the stencil.
  static JSScript* fromStencil(JSContext* cx,
                               js::frontend::CompilationAtomCache& atomCache,
                               const js::frontend::CompilationStencil& stencil,
                               js::frontend::CompilationGCOutput& gcOutput,
                               js::frontend::ScriptIndex scriptIndex);

#ifdef DEBUG
 private:
  // Assert that jump targets are within the code array of the script.
  void assertValidJumpTargets() const;
#endif

 public:
  js::ImmutableScriptData* immutableScriptData() const {
    return sharedData_->get();
  }

  // Script bytecode is immutable after creation.
  jsbytecode* code() const {
    if (!sharedData_) {
      return nullptr;
    }
    return immutableScriptData()->code();
  }

  js::AllBytecodesIterable allLocations() {
    return js::AllBytecodesIterable(this);
  }

  js::BytecodeLocation location() { return js::BytecodeLocation(this, code()); }

  size_t length() const {
    MOZ_ASSERT(sharedData_);
    return immutableScriptData()->codeLength();
  }

  jsbytecode* codeEnd() const { return code() + length(); }

  jsbytecode* lastPC() const {
    jsbytecode* pc = codeEnd() - js::JSOpLength_RetRval;
    MOZ_ASSERT(JSOp(*pc) == JSOp::RetRval || JSOp(*pc) == JSOp::Return);
    return pc;
  }

  // Note: ArgBytes is optional, but if specified then containsPC will also
  //       check that the opcode arguments are in bounds.
  template <size_t ArgBytes = 0>
  bool containsPC(const jsbytecode* pc) const {
    MOZ_ASSERT_IF(ArgBytes,
                  js::GetBytecodeLength(pc) == sizeof(jsbytecode) + ArgBytes);
    const jsbytecode* lastByte = pc + ArgBytes;
    return pc >= code() && lastByte < codeEnd();
  }
  template <typename ArgType>
  bool containsPC(const jsbytecode* pc) const {
    return containsPC<sizeof(ArgType)>(pc);
  }

  bool contains(const js::BytecodeLocation& loc) const {
    return containsPC(loc.toRawBytecode());
  }

  size_t pcToOffset(const jsbytecode* pc) const {
    MOZ_ASSERT(containsPC(pc));
    return size_t(pc - code());
  }

  jsbytecode* offsetToPC(size_t offset) const {
    MOZ_ASSERT(offset < length());
    return code() + offset;
  }

  size_t mainOffset() const { return immutableScriptData()->mainOffset; }

  // The fixed part of a stack frame is comprised of vars (in function and
  // module code) and block-scoped locals (in all kinds of code).
  size_t nfixed() const { return immutableScriptData()->nfixed; }

  // Number of fixed slots reserved for slots that are always live. Only
  // nonzero for function or module code.
  size_t numAlwaysLiveFixedSlots() const;

  // Calculate the number of fixed slots that are live at a particular bytecode.
  size_t calculateLiveFixed(jsbytecode* pc);

  size_t nslots() const { return immutableScriptData()->nslots; }

  unsigned numArgs() const;

  inline js::Shape* initialEnvironmentShape() const;

  bool functionHasParameterExprs() const;

  bool functionAllowsParameterRedeclaration() const {
    // Parameter redeclaration is only allowed for non-strict functions with
    // simple parameter lists, which are neither arrow nor method functions. We
    // don't have a flag at hand to test the function kind, but we can still
    // test if the function is non-strict and has a simple parameter list by
    // checking |hasMappedArgsObj()|. (Mapped arguments objects are only
    // created for non-strict functions with simple parameter lists.)
    return hasMappedArgsObj();
  }

  size_t numICEntries() const { return immutableScriptData()->numICEntries; }

  size_t funLength() const { return immutableScriptData()->funLength; }

  void cacheForEval() {
    MOZ_ASSERT(isForEval());
    // IsEvalCacheCandidate will make sure that there's nothing in this
    // script that would prevent reexecution even if isRunOnce is
    // true.  So just pretend like we never ran this script.
    clearFlag(MutableFlags::HasRunOnce);
  }

  /*
   * Arguments access (via JSOp::*Arg* opcodes) must access the canonical
   * location for the argument. If an arguments object exists AND it's mapped
   * ('arguments' aliases formals), then all access must go through the
   * arguments object. Otherwise, the local slot is the canonical location for
   * the arguments. Note: if a formal is aliased through the scope chain, then
   * script->formalIsAliased and JSOp::*Arg* opcodes won't be emitted at all.
   */
  bool argsObjAliasesFormals() const {
    return needsArgsObj() && hasMappedArgsObj();
  }

  void updateJitCodeRaw(JSRuntime* rt);

  bool isModule() const;
  js::ModuleObject* module() const;

  bool isGlobalCode() const;

  // Returns true if the script may read formal arguments on the stack
  // directly, via lazy arguments or a rest parameter.
  bool mayReadFrameArgsDirectly();

#ifdef MOZ_VTUNE
  // Unique Method ID passed to the VTune profiler. Allows attribution of
  // different jitcode to the same source script.
  uint32_t vtuneMethodID();
#endif

 public:
  /* Return whether this is a 'direct eval' script in a function scope. */
  bool isDirectEvalInFunction() const;

  /*
   * Return whether this script is a top-level script.
   *
   * If we evaluate some code which contains a syntax error, then we might
   * produce a JSScript which has no associated bytecode. Testing with
   * |code()| filters out this kind of scripts.
   *
   * If this script has a function associated to it, then it is not the
   * top-level of a file.
   */
  bool isTopLevel() { return code() && !isFunction(); }

  /* Ensure the script has a JitScript. */
  inline bool ensureHasJitScript(JSContext* cx, js::jit::AutoKeepJitScripts&);

  void maybeReleaseJitScript(JS::GCContext* gcx);
  void releaseJitScript(JS::GCContext* gcx);
  void releaseJitScriptOnFinalize(JS::GCContext* gcx);

  inline js::jit::BaselineScript* baselineScript() const;
  inline js::jit::IonScript* ionScript() const;

  inline bool isIonCompilingOffThread() const;
  inline bool canIonCompile() const;
  inline void disableIon();

  inline bool isBaselineCompilingOffThread() const;
  inline bool canBaselineCompile() const;
  inline void disableBaselineCompile();

  inline js::GlobalObject& global() const;
  inline bool hasGlobal(const js::GlobalObject* global) const;
  js::GlobalObject& uninlinedGlobal() const;

  js::GCThingIndex bodyScopeIndex() const {
    return immutableScriptData()->bodyScopeIndex;
  }

  js::Scope* bodyScope() const { return getScope(bodyScopeIndex()); }

  js::Scope* outermostScope() const {
    // The body scope may not be the outermost scope in the script when
    // the decl env scope is present.
    return getScope(js::GCThingIndex::outermostScopeIndex());
  }

  bool functionHasExtraBodyVarScope() const {
    bool res = BaseScript::functionHasExtraBodyVarScope();
    MOZ_ASSERT_IF(res, functionHasParameterExprs());
    return res;
  }

  js::VarScope* functionExtraBodyVarScope() const;

  bool needsBodyEnvironment() const;

  inline js::LexicalScope* maybeNamedLambdaScope() const;

  // Drop script data and reset warmUpData to reference enclosing scope.
  void relazify(JSRuntime* rt);

 private:
  bool createJitScript(JSContext* cx);

  bool shareScriptData(JSContext* cx);

 public:
  inline uint32_t getWarmUpCount() const;
  inline void incWarmUpCounter();
  inline void resetWarmUpCounterForGC();

  inline void updateLastICStubCounter();
  inline uint32_t warmUpCountAtLastICStub() const;

  void resetWarmUpCounterToDelayIonCompilation();

  unsigned getWarmUpResetCount() const {
    constexpr uint32_t MASK = uint32_t(MutableFlags::WarmupResets_MASK);
    return mutableFlags_ & MASK;
  }
  void incWarmUpResetCounter() {
    constexpr uint32_t MASK = uint32_t(MutableFlags::WarmupResets_MASK);
    uint32_t newCount = getWarmUpResetCount() + 1;
    if (newCount <= MASK) {
      mutableFlags_ &= ~MASK;
      mutableFlags_ |= newCount;
    }
  }
  void resetWarmUpResetCounter() {
    constexpr uint32_t MASK = uint32_t(MutableFlags::WarmupResets_MASK);
    mutableFlags_ &= ~MASK;
  }

 public:
  bool initScriptCounts(JSContext* cx);
  js::ScriptCounts& getScriptCounts();
  js::PCCounts* maybeGetPCCounts(jsbytecode* pc);
  const js::PCCounts* maybeGetThrowCounts(jsbytecode* pc);
  js::PCCounts* getThrowCounts(jsbytecode* pc);
  uint64_t getHitCount(jsbytecode* pc);
  void addIonCounts(js::jit::IonScriptCounts* ionCounts);
  js::jit::IonScriptCounts* getIonCounts();
  void releaseScriptCounts(js::ScriptCounts* counts);
  void destroyScriptCounts();
  void resetScriptCounts();

  jsbytecode* main() const { return code() + mainOffset(); }

  js::BytecodeLocation mainLocation() const {
    return js::BytecodeLocation(this, main());
  }

  js::BytecodeLocation endLocation() const {
    return js::BytecodeLocation(this, codeEnd());
  }

  js::BytecodeLocation offsetToLocation(uint32_t offset) const {
    return js::BytecodeLocation(this, offsetToPC(offset));
  }

  void addSizeOfJitScript(mozilla::MallocSizeOf mallocSizeOf,
                          size_t* sizeOfJitScript,
                          size_t* sizeOfAllocSites) const;

  mozilla::Span<const js::TryNote> trynotes() const {
    return immutableScriptData()->tryNotes();
  }

  mozilla::Span<const js::ScopeNote> scopeNotes() const {
    return immutableScriptData()->scopeNotes();
  }

  mozilla::Span<const uint32_t> resumeOffsets() const {
    return immutableScriptData()->resumeOffsets();
  }

  uint32_t tableSwitchCaseOffset(jsbytecode* pc, uint32_t caseIndex) const {
    MOZ_ASSERT(containsPC(pc));
    MOZ_ASSERT(JSOp(*pc) == JSOp::TableSwitch);
    uint32_t firstResumeIndex = GET_RESUMEINDEX(pc + 3 * JUMP_OFFSET_LEN);
    return resumeOffsets()[firstResumeIndex + caseIndex];
  }
  jsbytecode* tableSwitchCasePC(jsbytecode* pc, uint32_t caseIndex) const {
    return offsetToPC(tableSwitchCaseOffset(pc, caseIndex));
  }

  bool hasLoops();

  uint32_t numNotes() const {
    MOZ_ASSERT(sharedData_);
    return immutableScriptData()->noteLength();
  }
  js::SrcNote* notes() const {
    MOZ_ASSERT(sharedData_);
    return immutableScriptData()->notes();
  }
  js::SrcNote* notesEnd() const {
    MOZ_ASSERT(sharedData_);
    return immutableScriptData()->notes() + numNotes();
  }

  js::SourceLocationIterator sourceLocationIter() const;

  JSString* getString(js::GCThingIndex index) const {
    return &gcthings()[index].as<JSString>();
  }

  JSString* getString(jsbytecode* pc) const {
    MOZ_ASSERT(containsPC<js::GCThingIndex>(pc));
    MOZ_ASSERT(js::JOF_OPTYPE((JSOp)*pc) == JOF_STRING);
    return getString(GET_GCTHING_INDEX(pc));
  }

  bool atomizeString(JSContext* cx, jsbytecode* pc) {
    MOZ_ASSERT(containsPC<js::GCThingIndex>(pc));
    MOZ_ASSERT(js::JOF_OPTYPE((JSOp)*pc) == JOF_STRING);
    js::GCThingIndex index = GET_GCTHING_INDEX(pc);
    JSString* str = getString(index);
    if (str->isAtom()) {
      return true;
    }
    JSAtom* atom = js::AtomizeString(cx, str);
    if (!atom) {
      return false;
    }
    JS::GCCellPtr& ref = data_->gcthings()[index];
    js::gc::CellPtrPreWriteBarrier(ref);
#ifdef JS_GC_CONCURRENT_MARKING
    ref.atomicSet(JS::GCCellPtr(atom));
#else
    ref = JS::GCCellPtr(atom);
#endif
    return true;
  }

  JSAtom* getAtom(js::GCThingIndex index) const {
    return &gcthings()[index].as<JSString>().asAtom();
  }

  JSAtom* getAtom(jsbytecode* pc) const {
    MOZ_ASSERT(containsPC<js::GCThingIndex>(pc));
    MOZ_ASSERT(js::JOF_OPTYPE((JSOp)*pc) == JOF_ATOM);
    return getAtom(GET_GCTHING_INDEX(pc));
  }

  js::PropertyName* getName(js::GCThingIndex index) {
    return getAtom(index)->asPropertyName();
  }

  js::PropertyName* getName(jsbytecode* pc) const {
    return getAtom(pc)->asPropertyName();
  }

  JSObject* getObject(js::GCThingIndex index) const {
    MOZ_ASSERT(gcthings()[index].asCell()->isTenured());
    return &gcthings()[index].as<JSObject>();
  }

  JSObject* getObject(const jsbytecode* pc) const {
    MOZ_ASSERT(containsPC<js::GCThingIndex>(pc));
    return getObject(GET_GCTHING_INDEX(pc));
  }

  js::SharedShape* getShape(js::GCThingIndex index) const {
    return &gcthings()[index].as<js::Shape>().asShared();
  }

  js::SharedShape* getShape(const jsbytecode* pc) const {
    MOZ_ASSERT(containsPC<js::GCThingIndex>(pc));
    return getShape(GET_GCTHING_INDEX(pc));
  }

  js::Scope* getScope(js::GCThingIndex index) const {
    return &gcthings()[index].as<js::Scope>();
  }

  js::Scope* getScope(jsbytecode* pc) const {
    // This method is used to get a scope directly using a JSOp with an
    // index. To search through ScopeNotes to look for a Scope using pc,
    // use lookupScope.
    MOZ_ASSERT(containsPC<js::GCThingIndex>(pc));
    MOZ_ASSERT(js::JOF_OPTYPE(JSOp(*pc)) == JOF_SCOPE,
               "Did you mean to use lookupScope(pc)?");
    return getScope(GET_GCTHING_INDEX(pc));
  }

  inline JSFunction* getFunction(js::GCThingIndex index) const;
  inline JSFunction* getFunction(jsbytecode* pc) const;

  inline js::RegExpObject* getRegExp(js::GCThingIndex index) const;
  inline js::RegExpObject* getRegExp(jsbytecode* pc) const;

  js::BigInt* getBigInt(js::GCThingIndex index) const {
    MOZ_ASSERT(gcthings()[index].asCell()->isTenured());
    return &gcthings()[index].as<js::BigInt>();
  }

  js::BigInt* getBigInt(jsbytecode* pc) const {
    MOZ_ASSERT(containsPC<js::GCThingIndex>(pc));
    MOZ_ASSERT(js::JOF_OPTYPE(JSOp(*pc)) == JOF_BIGINT);
    return getBigInt(GET_GCTHING_INDEX(pc));
  }

  // The following 3 functions find the static scope just before the
  // execution of the instruction pointed to by pc.

  js::Scope* lookupScope(const jsbytecode* pc) const;

  js::Scope* innermostScope(const jsbytecode* pc) const;
  js::Scope* innermostScope() const { return innermostScope(main()); }

  /*
   * The isEmpty method tells whether this script has code that computes any
   * result (not return value, result AKA normal completion value) other than
   * JSVAL_VOID, or any other effects.
   */
  bool isEmpty() const {
    if (length() > 3) {
      return false;
    }

    jsbytecode* pc = code();
    if (noScriptRval() && JSOp(*pc) == JSOp::False) {
      ++pc;
    }
    return JSOp(*pc) == JSOp::RetRval;
  }

  bool formalIsAliased(unsigned argSlot);
  bool anyFormalIsForwarded();
  bool formalLivesInArgumentsObject(unsigned argSlot);

  // See comment above 'debugMode' in Realm.h for explanation of
  // invariants of debuggee compartments, scripts, and frames.
  inline bool isDebuggee() const;

  // A helper class to prevent relazification of the given script while it's
  // holding on to it.  This class automatically roots the script.
  class AutoKeepDelazified;
  friend class AutoKeepDelazified;

  class MOZ_RAII AutoKeepDelazified {
    JS::Rooted<JSScript*> script_;
    bool oldAllowRelazify_ = false;

   public:
    AutoKeepDelazified(JSContext* cx, JSScript* script) : script_(cx, script) {
      MOZ_ASSERT(script_->hasBytecode());
      oldAllowRelazify_ = script_->allowRelazify();
      script_->clearAllowRelazify();
    }

    ~AutoKeepDelazified() { script_->setAllowRelazify(oldAllowRelazify_); }

    JSScript* script() const { return script_; }
  };

#if defined(DEBUG) || defined(JS_JITSPEW)
 public:
  struct DumpOptions {
    bool recursive = false;
    bool runtimeData = false;
  };

  void dump(JSContext* cx);
  void dumpRecursive(JSContext* cx);

  static bool dump(JSContext* cx, JS::Handle<JSScript*> script,
                   DumpOptions& options, js::StringPrinter* sp);
  static bool dumpSrcNotes(JSContext* cx, JS::Handle<JSScript*> script,
                           js::GenericPrinter* sp);
  static bool dumpTryNotes(JSContext* cx, JS::Handle<JSScript*> script,
                           js::GenericPrinter* sp);
  static bool dumpScopeNotes(JSContext* cx, JS::Handle<JSScript*> script,
                             js::GenericPrinter* sp);
  static bool dumpGCThings(JSContext* cx, JS::Handle<JSScript*> script,
                           js::GenericPrinter* sp);
#endif
};

namespace js {

struct ScriptAndCounts {
  /* This structure is stored and marked from the JSRuntime. */
  JSScript* script;
  ScriptCounts scriptCounts;

  inline explicit ScriptAndCounts(JSScript* script);
  inline ScriptAndCounts(ScriptAndCounts&& sac);

  const PCCounts* maybeGetPCCounts(jsbytecode* pc) const {
    return scriptCounts.maybeGetPCCounts(script->pcToOffset(pc));
  }
  const PCCounts* maybeGetThrowCounts(jsbytecode* pc) const {
    return scriptCounts.maybeGetThrowCounts(script->pcToOffset(pc));
  }

  jit::IonScriptCounts* getIonCounts() const { return scriptCounts.ionCounts_; }

  void trace(JSTracer* trc) {
    TraceRoot(trc, &script, "ScriptAndCounts::script");
  }
};

extern JS::UniqueChars FormatIntroducedFilename(const char* filename,
                                                uint32_t lineno,
                                                const char* introducer);

extern jsbytecode* LineNumberToPC(JSScript* script, unsigned lineno);

extern JS_PUBLIC_API unsigned GetScriptLineExtent(
    JSScript* script, JS::LimitedColumnNumberOneOrigin* columnp = nullptr);

#ifdef JS_CACHEIR_SPEW
void maybeUpdateWarmUpCount(JSScript* script);
void maybeSpewScriptFinalWarmUpCount(JSScript* script);
#endif

} /* namespace js */

namespace js {

extern unsigned PCToLineNumber(
    JSScript* script, jsbytecode* pc,
    JS::LimitedColumnNumberOneOrigin* columnp = nullptr);

extern unsigned PCToLineNumber(
    unsigned startLine, JS::LimitedColumnNumberOneOrigin startCol,
    SrcNote* notes, SrcNote* notesEnd, jsbytecode* code, jsbytecode* pc,
    JS::LimitedColumnNumberOneOrigin* columnp = nullptr);

// Iterator over SrcNote array that tracks bytecode offset and line/column.
class SourceLocationIterator {
  SrcNoteIterator iter_;
  ptrdiff_t offset_;
  unsigned line_;
  JS::LimitedColumnNumberOneOrigin column_;
  unsigned startLine_;
  jsbytecode* code_;

 public:
  SourceLocationIterator(unsigned startLine,
                         JS::LimitedColumnNumberOneOrigin startCol,
                         SrcNote* notes, SrcNote* notesEnd, jsbytecode* code);

  // Advance the iterator to the given PC, updating line and column.
  void advanceToPC(const jsbytecode* pc);

  unsigned line() const { return line_; }
  JS::LimitedColumnNumberOneOrigin column() const { return column_; }
};

/*
 * This function returns the file and line number of the script currently
 * executing on cx. If there is no current script executing on cx (e.g., a
 * native called directly through JSAPI (e.g., by setTimeout)), nullptr and 0
 * are returned as the file and line.
 */
extern void DescribeScriptedCallerForCompilation(
    JSContext* cx, MutableHandleScript maybeScript, const char** file,
    uint32_t* linenop, uint32_t* pcOffset, bool* mutedErrors);

/*
 * Like DescribeScriptedCallerForCompilation, but this function avoids looking
 * up the script/pc and the full linear scan to compute line number.
 */
extern void DescribeScriptedCallerForDirectEval(
    JSContext* cx, HandleScript script, jsbytecode* pc, const char** file,
    uint32_t* linenop, uint32_t* pcOffset, bool* mutedErrors);

bool CheckCompileOptionsMatch(const JS::ReadOnlyCompileOptions& options,
                              js::ImmutableScriptFlags flags);

void FillImmutableFlagsFromCompileOptionsForTopLevel(
    const JS::ReadOnlyCompileOptions& options, js::ImmutableScriptFlags& flags);

void FillImmutableFlagsFromCompileOptionsForFunction(
    const JS::ReadOnlyCompileOptions& options, js::ImmutableScriptFlags& flags);

} /* namespace js */

namespace JS {

template <>
struct GCPolicy<js::ScriptLCovEntry>
    : public IgnoreGCPolicy<js::ScriptLCovEntry> {};

#ifdef JS_CACHEIR_SPEW
template <>
struct GCPolicy<js::ScriptFinalWarmUpCountEntry>
    : public IgnoreGCPolicy<js::ScriptFinalWarmUpCountEntry> {};
#endif

namespace ubi {

template <>
class Concrete<JSScript> : public Concrete<js::BaseScript> {};

}  // namespace ubi
}  // namespace JS

#endif /* vm_JSScript_h */
