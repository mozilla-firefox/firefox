/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SharedFontList_impl_h
#define SharedFontList_impl_h

#include "SharedFontList.h"
#include "base/process.h"
#include "gfxFontUtils.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/ipc/SharedMemoryMapping.h"
#include "nsClassHashtable.h"
#include "nsTHashMap.h"
#include "nsXULAppAPI.h"

// This is split out from SharedFontList.h because that header is included
// quite widely (via gfxPlatformFontList.h, gfxTextRun.h, etc). This header,
// which defines the actual shared-memory FontList class, is included only by
// the .cpp files that implement or directly interface with the font list, to
// avoid polluting other headers.

namespace mozilla {
namespace fontlist {

/**
 * Data used to initialize a font family alias (a "virtual" family that refers
 * to some or all of the faces of another family, used when alternate family
 * names are found in the font resource for localization or for styled
 * subfamilies). AliasData records are collected incrementally while scanning
 * the fonts, and then used to set up the Aliases list in the shared font list.
 */
struct AliasData {
  nsTArray<Pointer> mFaces;
  nsCString mBaseFamily;
  uint32_t mIndex = 0;
  FontVisibility mVisibility = FontVisibility::Unknown;
  bool mBundled = false;
  bool mBadUnderline = false;
  bool mForceClassic = false;

  void InitFromFamily(const Family* aFamily, const nsCString& aBaseFamily) {
    mBaseFamily = aBaseFamily;
    mIndex = aFamily->Index();
    mVisibility = aFamily->Visibility();
    mBundled = aFamily->IsBundled();
    mBadUnderline = aFamily->IsBadUnderlineFamily();
    mForceClassic = aFamily->IsForceClassic();
  }
};

/**
 * The Shared Font List is a collection of data that lives in shared memory
 * so that all processes can use it, rather than maintaining their own copies,
 * and provides the metadata needed for CSS font-matching (a list of all the
 * available font families and their faces, style properties, etc, as well as
 * character coverage).
 *
 * An important assumption is that all processes see the same collection of
 * installed fonts; therefore it is valid for them all to share the same set
 * of font metadata. The data is updated only by the parent process; content
 * processes have read-only access to it.
 *
 * The total size of this data varies greatly depending on the user's installed
 * fonts; and it is not known at startup because we load a lot of the font data
 * on first use rather than preloading during initialization (because that's
 * too expensive/slow).
 *
 * Therefore, the shared memory area needs to be able to grow during the
 * session; we can't predict how much space will be needed, and we can't afford
 * to pre-allocate such a huge block that it could never overflow. To handle
 * this, we maintain a (generally short) array of blocks of shared memory,
 * and then allocate our Family, Face, etc. objects within these. Because we
 * only ever add data (never delete what we've already stored), we can use a
 * simplified allocator that doesn't ever need to free blocks; the only time
 * the memory is released during a session is the (rare) case where a font is
 * installed or deleted while the browser is running, and in this case we just
 * delete the entire shared font list and start afresh.
 */
class FontList {
 public:
  friend struct Pointer;

  explicit FontList(uint32_t aGeneration);
  ~FontList();

  /**
   * Initialize the master list of installed font families. This must be
   * set during font-list creation, before the list is shared with any
   * content processes. All installed font families known to the browser
   * appear in this list, although some may be marked as "hidden" so that
   * they are not exposed to the font-family property.
   *
   * The passed-in array may be modified (to eliminate duplicates of bundled
   * fonts, or restrict the available list to a specified subset), so if the
   * caller intends to make further use of it this should be kept in mind.
   *
   * Once initialized, the master family list is immutable; in the (rare)
   * event that the system's collection of installed fonts changes, we discard
   * the FontList and create a new one.
   *
   * In some cases, a font family may be known by multiple names (e.g.
   * localizations in multiple languages, or there may be legacy family names
   * that correspond to specific styled faces like "Arial Black"). Such names
   * do not appear in this master list, but are referred to as aliases (see
   * SetAliases below); the alias list need not be populated before the font
   * list is shared to content processes and used.
   *
   * Only used in the parent process.
   */
  void SetFamilyNames(nsTArray<Family::InitData>& aFamilies);

  /**
   * Aliases are Family records whose Face entries are already part of another
   * family (either because the family has multiple localized names, or because
   * the alias family is a legacy name like "Arial Narrow" that is a subset of
   * the faces in the main "Arial" family). The table of aliases is initialized
   * from a hash of alias family name -> array of Face records.
   *
   * Like the master family list, the list of family aliases is immutable once
   * initialized.
   *
   * Only used in the parent process.
   */
  void SetAliases(nsClassHashtable<nsCStringHashKey, AliasData>& aAliasTable);

  /**
   * Local names are PostScript or Full font names of individual faces, used
   * to look up faces for @font-face { src: local(...) } rules. Some platforms
   * (e.g. macOS) can look up local names directly using platform font APIs,
   * in which case the local names table here is unused.
   *
   * The list of local names is immutable once initialized. Local font name
   * lookups may occur before this list has been set up, in which case they
   * will use the SearchForLocalFace method.
   *
   * Only used in the parent process.
   */
  void SetLocalNames(
      nsTHashMap<nsCStringHashKey, LocalFaceRec::InitData>& aLocalNameTable);

  /**
   * Look up a Family record by name, typically to satisfy the font-family
   * property or a font family listed in preferences.
   */
  Family* FindFamily(const nsCString& aName, bool aPrimaryNameOnly = false);

  /**
   * Look up an individual Face by PostScript or Full name, for @font-face
   * rules using src:local(...). This requires the local names list to have
   * been initialized.
   */
  LocalFaceRec* FindLocalFace(const nsCString& aName);

  /**
   * Search families for a face with local name aName; should only be used if
   * the mLocalFaces array has not yet been set up, as this will be a more
   * expensive search than FindLocalFace.
   */
  void SearchForLocalFace(const nsACString& aName, Family** aFamily,
                          Face** aFace);

  /**
   * Return the localized name for the given family in the current system
   * locale (if multiple localizations are available).
   */
  nsCString LocalizedFamilyName(const Family* aFamily);

  bool Initialized() { return mBlocks.Length() > 0 && NumFamilies() > 0; }

  uint32_t NumFamilies() { return GetHeader().mFamilyCount; }
  Family* Families() {
    return GetHeader().mFamilies.ToArray<Family>(this, NumFamilies());
  }

  uint32_t NumAliases() { return GetHeader().mAliasCount; }
  Family* AliasFamilies() {
    return GetHeader().mAliases.ToArray<Family>(this, NumAliases());
  }

  uint32_t NumLocalFaces() { return GetHeader().mLocalFaceCount; }
  LocalFaceRec* LocalFaces() {
    return GetHeader().mLocalFaces.ToArray<LocalFaceRec>(this, NumLocalFaces());
  }

  /**
   * Ask the font list to initialize the character map for a given face.
   */
  void LoadCharMapFor(Face& aFace, const Family* aFamily);

  /**
   * Allocate shared-memory space for a record of aSize bytes. The returned
   * pointer will be 32-bit aligned. (This method may trigger the allocation of
   * a new shared memory block, if required.)
   *
   * Only used in the parent process.
   */
  Pointer Alloc(uint32_t aSize);

  uint32_t GetGeneration() { return GetHeader().mGeneration; }

  /**
   * Header fields present in every shared-memory block. The mBlockSize field
   * is not modified after initial block creation (before the block has been
   * shared to any other process), so does not need to be std::atomic<>.
   * The mAllocated field is checked during Pointer::ToPtr(), so we make that
   * atomic to avoid data races.
   */
  struct BlockHeader {
    std::atomic<uint32_t> mAllocated;  // Space allocated from this block.
    uint32_t mBlockSize;               // Total size of this block.
  };

  /**
   * Header info that is stored at the beginning of the first shared-memory
   * block for the font list.
   * (Subsequent blocks have only the mBlockHeader.)
   * The mGeneration and mFamilyCount fields are set by the parent process
   * during font-list construction, before the list has been shared with any
   * other process, and subsequently never change; therefore, we don't need
   * to use std::atomic<> for these.
   */
  struct Header {
    BlockHeader mBlockHeader;
    uint32_t mGeneration;               // Font-list generation ID
    uint32_t mFamilyCount;              // Number of font families in the list
    std::atomic<uint32_t> mBlockCount;  // Total number of blocks that exist
    std::atomic<uint32_t> mAliasCount;  // Number of family aliases
    std::atomic<uint32_t> mLocalFaceCount;  // Number of local face names
    Pointer mFamilies;    // Pointer to array of |mFamilyCount| families
    Pointer mAliases;     // Pointer to array of |mAliasCount| aliases
    Pointer mLocalFaces;  // Pointer to array of |mLocalFaceCount| face records
  };

  /**
   * Used by the parent process to pass a handle to a shared block to a
   * specific child process. This is used when a child process requests
   * an additional block that was not already passed to it (because the
   * list has changed/grown since the child was first initialized).
   */
  void ShareShmBlockToProcess(uint32_t aIndex,
                              ipc::ReadOnlySharedMemoryHandle* aOut) {
    MOZ_RELEASE_ASSERT(mReadOnlyShmems.Length() == mBlocks.Length());
    if (aIndex >= mReadOnlyShmems.Length()) {
      // Block index out of range
      *aOut = nullptr;
      return;
    }
    *aOut = mReadOnlyShmems[aIndex].Clone();
    if (!*aOut) {
      MOZ_CRASH("failed to share block");
    }
  }

  /**
   * Collect an array of handles to all the shmem blocks, ready to be
   * shared to a child process. This is used at child process startup
   * to pass the complete list at once.
   */
  void ShareBlocksToProcess(nsTArray<ipc::ReadOnlySharedMemoryHandle>* aBlocks);

  ipc::ReadOnlySharedMemoryHandle ShareBlockToProcess(uint32_t aIndex);

  void ShmBlockAdded(uint32_t aGeneration, uint32_t aIndex,
                     ipc::ReadOnlySharedMemoryHandle aHandle);
  /**
   * Support for memory reporter.
   */
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;
  size_t AllocatedShmemSize() const;

  /**
   * Using a larger block size will speed up allocation, at the cost of more
   * wasted space in the shared memory (on average).
   */
#if ANDROID
  // Android devices usually have a much smaller number of fonts than desktop
  // systems, and memory is more constrained, so use a smaller default block
  // size.
  static constexpr uint32_t SHM_BLOCK_SIZE = 64 * 1024;
#elif XP_LINUX
  // On Linux, font face descriptors are rather large (serialized FcPatterns),
  // so use a larger block size for efficiency.
  static constexpr uint32_t SHM_BLOCK_SIZE = 1024 * 1024;
#else
  // Default block size for Windows and macOS.
  static constexpr uint32_t SHM_BLOCK_SIZE = 256 * 1024;
#endif
  static_assert(SHM_BLOCK_SIZE <= (1 << Pointer::kBlockShift),
                "SHM_BLOCK_SIZE too large");

 private:
  struct ShmBlock {
    // Takes ownership of aShmem. In a child process, aShmem will be mapped as
    // read-only.
    explicit ShmBlock(ipc::ReadOnlySharedMemoryMapping&& aShmem)
        : mShmem(std::move(aShmem)) {
      MOZ_ASSERT(!XRE_IsParentProcess());
    }

    explicit ShmBlock(ipc::SharedMemoryMapping&& aShmem)
        : mShmem(std::move(aShmem)) {
      MOZ_ASSERT(XRE_IsParentProcess());
    }

    // Get pointer to the mapped memory.
    void* Memory() const { return mShmem.Address(); }

    void Clear() { mShmem = nullptr; }

    // Only the parent process does allocation, so only it will update this
    // field. Content processes read the value when checking Pointer validity.
    uint32_t Allocated() const {
      return static_cast<BlockHeader*>(Memory())->mAllocated;
    }

    void StoreAllocated(uint32_t aSize) {
      MOZ_ASSERT(XRE_IsParentProcess());
      static_cast<BlockHeader*>(Memory())->mAllocated.store(aSize);
    }

    // This is stored by the parent process during block creation and never
    // changes, so does not need to be atomic.
    // Note that some blocks may be larger than SHM_BLOCK_SIZE, if needed for
    // individual large allocations.
    uint32_t& BlockSize() const {
      MOZ_ASSERT(XRE_IsParentProcess());
      return static_cast<BlockHeader*>(Memory())->mBlockSize;
    }

   private:
    ipc::MutableOrReadOnlySharedMemoryMapping mShmem;
  };

  Header& GetHeader() const;

  /**
   * Create a new shared memory block and append to the FontList's list
   * of blocks.
   *
   * Only used in the parent process.
   */
  bool AppendShmBlock(uint32_t aSizeNeeded);

  /**
   * Used by child processes to ensure all the blocks are registered.
   * Returns false on failure.
   * Pass aMustLock=true to take the gfxPlatformFontList lock during the
   * update (not required when calling from the constructor).
   */
  [[nodiscard]] bool UpdateShmBlocks(bool aMustLock);

  /**
   * This makes a *sync* IPC call to get a shared block from the parent.
   * As such, it may block for a while if the parent is busy; fortunately,
   * we'll generally only call this a handful of times in the course of an
   * entire session. If the requested block does not yet exist (because the
   * child is wanting to allocate an object, and there wasn't room in any
   * existing block), the parent will create a new shared block and return it.
   * This may (in rare cases) return null, if the parent has recreated the
   * font list and we actually need to reinitialize.
   */
  ShmBlock* GetBlockFromParent(uint32_t aIndex);

  void DetachShmBlocks();

  /**
   * Array of pointers to the shared-memory block records.
   * NOTE: if mBlocks.Length() < GetHeader().mBlockCount, then the parent has
   * added a block (or blocks) to the list, and we need to update!
   */
  nsTArray<mozilla::UniquePtr<ShmBlock>> mBlocks;

  /**
   * Auxiliary array, used only in the parent process; holds read-only handles
   * for the shmem blocks; these are what will be shared to child processes.
   */
  nsTArray<ipc::ReadOnlySharedMemoryHandle> mReadOnlyShmems;

#ifdef XP_WIN
  // Bool array to track whether we have read face names from the name table.
  nsTArray<bool> mFaceNamesRead;
#endif
};

}  // namespace fontlist
}  // namespace mozilla

#endif /* SharedFontList_impl_h */
