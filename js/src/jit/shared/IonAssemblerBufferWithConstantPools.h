/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_shared_IonAssemblerBufferWithConstantPools_h
#define jit_shared_IonAssemblerBufferWithConstantPools_h

#include "mozilla/Attributes.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/HashFunctions.h"

#include <algorithm>
#include <bit>
#include <deque>
#include <type_traits>

#include "jit/JitSpewer.h"
#include "jit/shared/IonAssemblerBuffer.h"
#include "js/HashTable.h"
#include "util/PolicyAllocator.h"

// [SMDOC] JIT AssemblerBuffer constant pooling (ARM/ARM64/RISCV64)
//
// This code extends the AssemblerBuffer to support the pooling of values loaded
// using program-counter relative addressing modes. This is necessary with the
// ARM instruction set because it has a fixed instruction size that can not
// encode all values as immediate arguments in instructions. Pooling the values
// allows the values to be placed in large chunks which minimizes the number of
// forced branches around them in the code. This is used for loading floating
// point constants, for loading 32 bit constants on the ARMv6, for absolute
// branch targets, and in future will be needed for large branches on the ARMv6.
//
// For simplicity of the implementation, the constant pools are always placed
// after the loads referencing them. When a new constant pool load is added to
// the assembler buffer, a corresponding pool entry is added to the current
// pending pool. The finishPool() method copies the current pending pool entries
// into the assembler buffer at the current offset and patches the pending
// constant pool load instructions.
//
// Before inserting instructions or pool entries, it is necessary to determine
// if doing so would place a pending pool entry out of reach of an instruction,
// and if so then the pool must firstly be dumped. With the allocation algorithm
// used below, the recalculation of all the distances between instructions and
// their pool entries can be avoided by noting that there will be a limiting
// instruction and pool entry pair that does not change when inserting more
// instructions. Adding more instructions makes the same increase to the
// distance, between instructions and their pool entries, for all such
// pairs. This pair is recorded as the limiter, and it is updated when new pool
// entries are added, see updateLimiter()
//
// The pools consist of: a guard instruction that branches around the pool, a
// header word that helps identify a pool in the instruction stream, and then
// the pool entries allocated in units of words. The guard instruction could be
// omitted if control does not reach the pool, and this is referred to as a
// natural guard below, but for simplicity the guard branch is always
// emitted. The pool header is an identifiable word that in combination with the
// guard uniquely identifies a pool in the instruction stream. The header also
// encodes the pool size and a flag indicating if the guard is natural. It is
// possible to iterate through the code instructions skipping or examining the
// pools. E.g. it might be necessary to skip pools when search for, or patching,
// an instruction sequence.
//
// Loads of equal values can share one pool entry. The caller of allocEntry()
// says whether an entry may be shared by passing a PoolEntryKind other than
// Unshared; only entries of the same kind and size with equal bytes share.
// Entries that are patched in place after emission must stay Unshared, and
// entries that the GC rewrites (traced pointers) must not share with entries
// that it does not, which is what the kinds are for.
//
// The code supports no-pool regions, and for these the size of the region, in
// instructions, must be supplied. This size is used to determine if inserting
// the instructions would place a pool entry out of range, and if so then a pool
// is firstly flushed. The DEBUG code checks that the emitted code is within the
// supplied size to detect programming errors. See enterNoPool() and
// leaveNoPool().

// The only planned instruction sets that require inline constant pools are the
// ARM, ARM64, RISCV64 (and historically MIPS), and these all have fixed 32-bit
// sized instructions so for simplicity the code below is specialized for fixed
// 32-bit sized instructions and makes no attempt to support variable length
// instructions. The base assembler buffer which supports variable width
// instruction is used by the x86 and x64 backends.

// The AssemblerBufferWithConstantPools template class uses static callbacks to
// the provided Asm template argument class:
//
// void Asm::InsertIndexIntoTag(uint8_t* load_, uint32_t index)
//
//   When allocEntry() is called to add a constant pool load with an associated
//   constant pool entry, this callback is called to encode the index of the
//   allocated constant pool entry into the load instruction.
//
//   After the constant pool has been placed, PatchConstantPoolLoad() is called
//   to update the load instruction with the right load offset.
//
// void Asm::WritePoolGuard(BufferOffset branch,
//                          Instruction* dest,
//                          BufferOffset afterPool)
//
//   Write out the constant pool guard branch before emitting the pool.
//
//   branch
//     Offset of the guard branch in the buffer.
//
//   dest
//     Pointer into the buffer where the guard branch should be emitted. (Same
//     as getInst(branch)). Space for guardSize_ instructions has been reserved.
//
//   afterPool
//     Offset of the first instruction after the constant pool. This includes
//     both pool entries and branch veneers added after the pool data.
//
// void Asm::WritePoolHeader(uint8_t* start, Pool* p, bool isNatural)
//
//   Write out the pool header which follows the guard branch.
//
// void Asm::PatchConstantPoolLoad(void* loadAddr, void* constPoolAddr)
//
//   Re-encode a load of a constant pool entry after the location of the
//   constant pool is known.
//
//   The load instruction at loadAddr was previously passed to
//   InsertIndexIntoTag(). The constPoolAddr is the final address of the
//   constant pool in the assembler buffer.
//
// void Asm::PatchShortRangeBranchToVeneer(AssemblerBufferWithConstantPools*,
//                                         unsigned rangeIdx,
//                                         BufferOffset deadline,
//                                         BufferOffset veneer)
//
//   Patch a short-range branch to jump through a veneer before it goes out of
//   range.
//
//   rangeIdx, deadline
//     These arguments were previously passed to registerBranchDeadline(). It is
//     assumed that PatchShortRangeBranchToVeneer() knows how to compute the
//     offset of the short-range branch from this information.
//
//   veneer
//     Space for a branch veneer, guaranteed to be <= deadline. At this
//     position, veneerSize_ * InstSize bytes are allocated. They should be
//     initialized to the proper unconditional branch instruction.
//
//   Unbound branches to the same unbound label are organized as a linked list:
//
//     Label::offset -> Branch1 -> Branch2 -> Branch3 -> nil
//
//   This callback should insert a new veneer branch into the list:
//
//     Label::offset -> Branch1 -> Branch2 -> Veneer -> Branch3 -> nil
//
//   When Assembler::bind() rewrites the branches with the real label offset, it
//   probably has to bind Branch2 to target the veneer branch instead of jumping
//   straight to the label.

namespace js {
namespace jit {

// BranchDeadlineSet - Keep track of pending branch deadlines.
//
// Some architectures like arm and arm64 have branch instructions with limited
// range. When assembling a forward branch, it is not always known if the final
// target label will be in range of the branch instruction.
//
// The BranchDeadlineSet data structure is used to keep track of the set of
// pending forward branches. It supports the following fast operations:
//
// 1. Get the earliest deadline in the set.
// 2. Add a new branch deadline.
// 3. Remove a branch deadline.
//
// Architectures may have different branch encodings with different ranges. Each
// supported range is assigned a small integer starting at 0. This data
// structure does not care about the actual range of branch instructions, just
// the latest buffer offset that can be reached - the deadline offset.
//
// Branched are stored as (rangeIdx, deadline) tuples. The target-specific code
// can compute the location of the branch itself from this information. This
// data structure does not need to know.
//
template <unsigned NumRanges>
class BranchDeadlineSet {
  // Maintain a list of pending deadlines for each range separately.
  //
  // The offsets in each list are always kept in ascending order.
  //
  // Because we have a separate list for different ranges, as forward
  // branches are added to the assembler buffer, their deadlines will
  // always be appended to the list corresponding to their range.
  //
  // When binding labels, we expect a more-or-less LIFO order of branch
  // resolutions. This would always hold if we had strictly structured control
  // flow.
  //
  // Lists are implemented using a deque. This gives good performance when
  // removing from the beginning (because a deadline has been reached) or the
  // end (because of LIFO order), while still allowing for deadlines to be added
  // and removed in any order.
  //
  using LifoAllocator =
      PolicyAllocator<BufferOffset, LifoAllocPolicy<Fallible>>;
  using DeadlineList = std::deque<BufferOffset, LifoAllocator>;

  // We really just want "DeadlineList deadline_[NumRanges];", but each list
  // needs to be initialized with a LifoAlloc, and C++ doesn't bend that way.
  //
  // Use raw aligned storage instead and explicitly construct NumRanges
  // lists in our constructor.
  mozilla::AlignedStorage2<DeadlineList[NumRanges]> deadlineStorage_;

  // Always access the deadline lists through this method.
  DeadlineList& listForRange(unsigned rangeIdx) {
    MOZ_ASSERT(rangeIdx < NumRanges, "Invalid branch range index");
    return (*deadlineStorage_.addr())[rangeIdx];
  }

  const DeadlineList& listForRange(unsigned rangeIdx) const {
    MOZ_ASSERT(rangeIdx < NumRanges, "Invalid branch range index");
    return (*deadlineStorage_.addr())[rangeIdx];
  }

  // Maintain a precomputed earliest deadline at all times.
  // This is unassigned only when all deadline lists are empty.
  BufferOffset earliest_;

  // The deadline list owning earliest_. Uninitialized when empty.
  unsigned earliestRange_;

  // Recompute the earliest deadline after it's been invalidated.
  void recomputeEarliest() {
    earliest_ = BufferOffset();
    for (unsigned r = 0; r < NumRanges; r++) {
      auto& list = listForRange(r);
      if (!list.empty() && (!earliest_.assigned() || list[0] < earliest_)) {
        earliest_ = list[0];
        earliestRange_ = r;
      }
    }
  }

  // Update the earliest deadline if needed after inserting (rangeIdx,
  // deadline).
  void updateEarliest(unsigned rangeIdx, BufferOffset deadline) {
    if (!earliest_.assigned() || deadline < earliest_) {
      earliest_ = deadline;
      earliestRange_ = rangeIdx;
    }
  }

 public:
  explicit BranchDeadlineSet(LifoAlloc& alloc) : earliestRange_(0) {
    // Manually construct lists in the uninitialized aligned storage.
    // This is because C++ arrays can otherwise only be constructed with
    // the default constructor.
    for (unsigned r = 0; r < NumRanges; r++) {
      new (&listForRange(r)) DeadlineList(LifoAllocator(alloc));
    }
  }

  ~BranchDeadlineSet() {
    // Aligned storage doesn't destruct its contents automatically.
    for (unsigned r = 0; r < NumRanges; r++) {
      listForRange(r).~DeadlineList();
    }
  }

  // Is this set completely empty?
  bool empty() const { return !earliest_.assigned(); }

  // Get the total number of deadlines in the set.
  size_t size() const {
    size_t count = 0;
    for (unsigned r = 0; r < NumRanges; r++) {
      count += listForRange(r).size();
    }
    return count;
  }

  // Get the number of deadlines for the range with the most elements.
  size_t maxRangeSize() const {
    size_t count = 0;
    for (unsigned r = 0; r < NumRanges; r++) {
      count = std::max(count, listForRange(r).size());
    }
    return count;
  }

  // Get the first deadline that is still in the set.
  BufferOffset earliestDeadline() const {
    MOZ_ASSERT(!empty());
    return earliest_;
  }

  // Get the range index corresponding to earliestDeadlineRange().
  unsigned earliestDeadlineRange() const {
    MOZ_ASSERT(!empty());
    return earliestRange_;
  }

  // Add a (rangeIdx, deadline) tuple to the set.
  //
  // It is assumed that this tuple is not already in the set.
  // This function performs best id the added deadline is later than any
  // existing deadline for the same range index.
  void addDeadline(unsigned rangeIdx, BufferOffset deadline) {
    MOZ_ASSERT(deadline.assigned(), "Can only store assigned buffer offsets");
    // This is the list where deadline should be saved.
    auto& list = listForRange(rangeIdx);

    if (!list.empty() && list.back() < deadline) {
      // Fast case: Simple append to the relevant array. This never affects
      // the earliest deadline.
      list.push_back(deadline);
    } else if (list.empty()) {
      // Fast case: First entry to the list. We need to update earliest_.
      list.push_back(deadline);
      updateEarliest(rangeIdx, deadline);
    } else {
      addDeadlineSlow(rangeIdx, deadline);
    }
  }

 private:
  // General case of addDeadline. This is split into two functions such that
  // the common case in addDeadline can be inlined while this part probably
  // won't inline.
  void addDeadlineSlow(unsigned rangeIdx, BufferOffset deadline) {
    auto& list = listForRange(rangeIdx);

    // Inserting into the middle of the list. Use a log time binary search
    // and a linear time insert().
    // Is it worthwhile special-casing the empty list?
    auto at = std::lower_bound(list.begin(), list.end(), deadline);
    MOZ_ASSERT(at == list.end() || *at != deadline,
               "Cannot insert duplicate deadlines");
    list.insert(at, deadline);
    updateEarliest(rangeIdx, deadline);
  }

 public:
  // Remove a deadline from the set.
  // If (rangeIdx, deadline) is not in the set, nothing happens.
  void removeDeadline(unsigned rangeIdx, BufferOffset deadline) {
    auto& list = listForRange(rangeIdx);

    if (list.empty()) {
      return;
    }

    if (deadline == list.back()) {
      // Expected fast case: Structured control flow causes forward
      // branches to be bound in reverse order.
      list.pop_back();
    } else {
      // Slow case: Binary search + linear erase.
      auto where = std::lower_bound(list.begin(), list.end(), deadline);
      if (where == list.end() || *where != deadline) {
        return;
      }
      list.erase(where);
    }
    if (deadline == earliest_) {
      recomputeEarliest();
    }
  }
};

// Specialization for architectures that don't need to track short-range
// branches.
template <>
class BranchDeadlineSet<0u> {
 public:
  explicit BranchDeadlineSet(LifoAlloc& alloc) {}
  bool empty() const { return true; }
  size_t size() const { return 0; }
  size_t maxRangeSize() const { return 0; }
  BufferOffset earliestDeadline() const { MOZ_CRASH(); }
  unsigned earliestDeadlineRange() const { MOZ_CRASH(); }
  void addDeadline(unsigned rangeIdx, BufferOffset deadline) { MOZ_CRASH(); }
  void removeDeadline(unsigned rangeIdx, BufferOffset deadline) { MOZ_CRASH(); }
};

// The allocation unit size for pools.
using PoolAllocUnit = int32_t;

// Hysteresis given to short-range branches.
//
// If any short-range branches will go out of range in the next N bytes,
// generate a veneer for them in the current pool. The hysteresis prevents the
// creation of many tiny constant pools for branch veneers.
const size_t ShortRangeBranchHysteresis = 128;

// Which pool entries may share a slot with an equal entry; see the [SMDOC].
enum class PoolEntryKind : uint8_t {
  Unshared,
  // A pointer or boxed Value that TraceDataRelocations rewrites in place.
  GCThing,
  // A bit pattern that is never written after emission.
  Constant,
};

struct Pool {
  // The identity of a shareable pool entry: its kind, size and bytes.
  struct SharedEntryKey {
    static const unsigned MaxUnits = 4;
    PoolAllocUnit units[MaxUnits];
    unsigned num;
    PoolEntryKind kind;

    SharedEntryKey(unsigned num, const uint8_t* data, PoolEntryKind kind)
        : units{}, num(num), kind(kind) {
      MOZ_ASSERT(num <= MaxUnits);
      memcpy(units, data, num * sizeof(PoolAllocUnit));
    }

    using Lookup = SharedEntryKey;
    static HashNumber hash(const Lookup& l) {
      HashNumber h = mozilla::HashGeneric(l.num, unsigned(l.kind));
      for (unsigned i = 0; i < l.num; i++) {
        h = mozilla::AddToHash(h, l.units[i]);
      }
      return h;
    }
    static bool match(const SharedEntryKey& k, const Lookup& l) {
      return k.num == l.num && k.kind == l.kind &&
             memcmp(k.units, l.units, l.num * sizeof(PoolAllocUnit)) == 0;
    }
  };

 private:
  // The maximum pc relative offset encoded in instructions that reference
  // pool entries. This is generally set to the maximum offset that can be
  // encoded by the instructions, but for testing can be lowered to affect the
  // pool placement and frequency of pool placement.
  //
  // Different classes of instructions might support different ranges but for
  // simplicity the same maximum offset is applied to all instructions. In other
  // words the smallest maximum offset of all instructions is used.
  //
  // For ARM32 this is constrained to 1024 by the float load instruction VLDR.
  const size_t maxOffset_;

  // An offset to apply to program-counter relative offsets. The ARM has a
  // bias of 8.
  const unsigned bias_;

  // The limiting instruction and pool-entry pair. The instruction program
  // counter relative offset of this limiting instruction will go out of range
  // first as the pool position moves forward. It is more efficient to track
  // just this limiting pair than to recheck all offsets when testing if the
  // pool needs to be dumped.
  //
  // 1. The actual offset of the limiting instruction referencing the limiting
  // pool entry.
  BufferOffset limitingUser{};
  // 2. The pool entry index of the limiting pool entry.
  unsigned limitingUsee = INT_MIN;

  // Flag that tracks OOM conditions. This is set after any append failed.
  bool oom_ = false;

  // The content of the pool entries.
  Vector<PoolAllocUnit, 8, LifoAllocPolicy<Fallible>> poolData_;

  // Index of the entry holding each shareable value inserted so far.
  HashMap<SharedEntryKey, unsigned, SharedEntryKey, LifoAllocPolicy<Fallible>>
      sharedEntries_;

 public:
  // A record of the code offset of instructions that reference pool
  // entries. These instructions need to be patched when the actual position
  // of the instructions and pools are known, and for the code below this
  // occurs when each pool is finished, see finishPool().
  Vector<BufferOffset, 8, LifoAllocPolicy<Fallible>> loadOffsets;

  // Create a Pool. Don't allocate anything from lifoAlloc, just capture its
  // reference.
  Pool(size_t maxOffset, unsigned bias, LifoAlloc& lifoAlloc)
      : maxOffset_(maxOffset),
        bias_(bias),
        poolData_(lifoAlloc),
        sharedEntries_(lifoAlloc),
        loadOffsets(lifoAlloc) {}

  // If poolData() returns nullptr then oom_ will also be true.
  const PoolAllocUnit* poolData() const { return poolData_.begin(); }

  unsigned numEntries() const { return poolData_.length(); }

  size_t getPoolSize() const { return numEntries() * sizeof(PoolAllocUnit); }

  // Update the instruction/pool-entry pair that limits the position of the
  // pool. The nextInst is the actual offset of the new instruction being
  // allocated.
  //
  // This is comparing the offsets, see checkFull() below for the equation,
  // but common expressions on both sides have been canceled from the ranges
  // being compared. Notably, the poolOffset cancels out, so the limiting pair
  // does not depend on where the pool is placed.
  void updateLimiter(BufferOffset nextInst) {
    ptrdiff_t oldRange =
        limitingUsee * sizeof(PoolAllocUnit) - limitingUser.getOffset();
    ptrdiff_t newRange = getPoolSize() - nextInst.getOffset();
    if (!limitingUser.assigned() || newRange > oldRange) {
      // We have a new largest range!
      limitingUser = nextInst;
      limitingUsee = numEntries();
    }
  }

  // Check if inserting a pool at the actual offset poolOffset would place
  // pool entries out of reach. This is called before inserting instructions
  // to check that doing so would not push pool entries out of reach, and if
  // so then the pool would need to be firstly dumped. The poolOffset is the
  // first word of the pool, after the guard and header and alignment fill.
  bool checkFull(size_t poolOffset) const {
    // Not full if there are no uses.
    if (!limitingUser.assigned()) {
      return false;
    }
    size_t offset = poolOffset + limitingUsee * sizeof(PoolAllocUnit) -
                    (limitingUser.getOffset() + bias_);
    return offset >= maxOffset_;
  }

  static const unsigned OOM_FAIL = unsigned(-1);

  // Add a load at `off` of the `num` units at `data`, returning the index of
  // the entry it loads. A shareable entry equal to an earlier one of the same
  // kind reuses that entry's index.
  unsigned insertEntry(unsigned num, uint8_t* data, BufferOffset off,
                       PoolEntryKind kind) {
    if (oom_) {
      return OOM_FAIL;
    }
    if (!loadOffsets.append(off)) {
      oom_ = true;
      return OOM_FAIL;
    }
    if (kind == PoolEntryKind::Unshared) {
      unsigned ret = numEntries();
      if (!poolData_.append((PoolAllocUnit*)data, num)) {
        oom_ = true;
        return OOM_FAIL;
      }
      return ret;
    }
    SharedEntryKey key(num, data, kind);
    auto p = sharedEntries_.lookupForAdd(key);
    if (p) {
      return p->value();
    }
    unsigned ret = numEntries();
    if (!poolData_.append((PoolAllocUnit*)data, num) ||
        !sharedEntries_.add(p, key, ret)) {
      oom_ = true;
      return OOM_FAIL;
    }
    return ret;
  }

  void reset() {
    poolData_.clear();
    sharedEntries_.clear();
    loadOffsets.clear();

    limitingUser = BufferOffset();
    limitingUsee = -1;
  }
};

// Empty pool optimisation for assembler buffers which don't use constant pools.
//
// Implements only a subset of the Pool interface.
struct EmptyPool final {
  EmptyPool(size_t maxOffset, unsigned bias, LifoAlloc& lifoAlloc) {}

  unsigned numEntries() const {
    // Always zero entries.
    return 0;
  }

  size_t getPoolSize() const { return numEntries() * sizeof(PoolAllocUnit); }

  bool checkFull(size_t poolOffset) const {
    // Never full.
    return false;
  }
};

struct AssemblerBufferSettings {
  // Size in bytes of the fixed-size instructions. This should be equal to
  // sizeof(Inst). This is only needed here because the buffer is defined before
  // the Instruction.
  size_t instSize;

  // The size of a pool guard, in instructions. A branch around the pool.
  unsigned guardSize;

  // The size of the header that is put at the beginning of a full pool, in
  // instruction sized units.
  unsigned headerSize;

  // The size of a veneer branch, in instructions.
  unsigned veneerSize;

  // The bias on pc relative addressing mode offsets, in units of bytes. The
  // ARM has a bias of 8 bytes.
  unsigned pcBias;

  // Instruction to use for alignment fill.
  uint32_t alignFillInst;

  // Instruction to use for nop fill.
  uint32_t nopFillInst;

  // The number of short branch ranges to support. This can be 0 if no support
  // for tracking short range branches is needed. The
  // AssemblerBufferWithConstantPools class does not need to know what the range
  // of branches is - it deals in branch 'deadlines' which is the last buffer
  // position that a short-range forward branch can reach. It is assumed that
  // the Asm class is able to find the actual branch instruction given a
  // (range-index, deadline) pair.
  unsigned numShortBranchRanges = 0;

  // Hysteresis given to short-range branches.
  size_t shortRangeBranchHysteresis = jit::ShortRangeBranchHysteresis;
};

// Template arguments:
//
// Inst
//   The actual type used to represent instructions. This is only really used as
//   the return type of the getInst() method.
//
// Asm
//   Class defining the needed static callback functions. See documentation of
//   the Asm::* callbacks above.
//
// AssemblerBufferSettings
//   Assembler buffer settings object.
//
//
template <class Inst, class Asm, AssemblerBufferSettings settings>
struct AssemblerBufferWithConstantPools : public AssemblerBuffer<Inst> {
 private:
  static constexpr size_t InstSize = settings.instSize;
  static constexpr size_t NumShortBranchRanges = settings.numShortBranchRanges;
  static constexpr size_t ShortRangeBranchHysteresis =
      settings.shortRangeBranchHysteresis;

  // The size of a pool guard, in instructions. A branch around the pool.
  static constexpr unsigned GuardSize = settings.guardSize;

  // The size of a veneer branch, in instructions.
  static constexpr unsigned VeneerSize = settings.veneerSize;

  // The size of the header that is put at the beginning of a full pool, in
  // instruction sized units.
  static constexpr unsigned HeaderSize = settings.headerSize;

  // The bias on pc relative addressing mode offsets, in units of bytes. The
  // ARM has a bias of 8 bytes.
  static constexpr unsigned PcBias = settings.pcBias;

  // Constant pool support can be disabled by setting the header size to zero.
  static constexpr bool UseConstantPools = HeaderSize > 0;

  using PoolImpl = std::conditional_t<UseConstantPools, Pool, EmptyPool>;

  // The current working pool. Copied out as needed before resetting.
  MOZ_NO_UNIQUE_ADDRESS PoolImpl pool_;

  // Set of short-range forward branches that have not yet been bound.
  // We may need to insert veneers if the final label turns out to be out of
  // range.
  //
  // This set stores (rangeIdx, deadline) pairs instead of the actual branch
  // locations.
  BranchDeadlineSet<NumShortBranchRanges> branchDeadlines_;

  // When true dumping pools is inhibited.  Any value above zero indicates
  // inhibition of pool dumping.  These is no significance to different
  // above-zero values; this is a counter and not a boolean only so as to
  // facilitate correctly tracking nested enterNoPools/leaveNoPools calls.
  unsigned int inhibitPools_ = 0;

#ifdef DEBUG
  // State for validating the 'maxInst' argument to enterNoPool() in the case
  // `inhibitPools_` was zero before the call (that is, when we enter the
  // outermost nesting level).
  //
  // The buffer offset at the start of the outermost nesting level no-pool
  // region.  Set to all-ones (0xFF..FF) to mean "invalid".
  size_t inhibitPoolsStartOffset_ = ~size_t(0) /*"invalid"*/;
  // The maximum number of word sized instructions declared for the outermost
  // nesting level no-pool region.  Set to zero when invalid.
  size_t inhibitPoolsMaxInst_ = 0;
  // The maximum number of new deadlines that are allowed to register in the
  // no-pool region.
  size_t inhibitPoolsMaxNewDeadlines_ = 0;
  // The actual number of new deadlines registered in the no-pool region.
  size_t inhibitPoolsActualNewDeadlines_ = 0;
#endif

  // Instruction to use for alignment fill.
  static constexpr uint32_t AlignFillInst = settings.alignFillInst;

  // Insert a number of NOP instructions between each requested instruction at
  // all locations at which a pool can potentially spill. This is useful for
  // checking that instruction locations are correctly referenced and/or
  // followed.
  static constexpr uint32_t NopFillInst = settings.nopFillInst;
  const unsigned nopFill_;

  // For inhibiting the insertion of fill NOPs in the dynamic context in which
  // they are being inserted.  The zero-vs-nonzero meaning is the same as that
  // documented for `inhibitPools_` above.
  unsigned int inhibitNops_ = 0;

 public:
  AssemblerBufferWithConstantPools(size_t poolMaxOffset, unsigned nopFill)
      : pool_(poolMaxOffset, PcBias, this->lifoAlloc_),
        branchDeadlines_(this->lifoAlloc_),
        nopFill_(nopFill) {}

  size_t sizeExcludingCurrentPool() const {
    // Return the actual size of the buffer, excluding the current pending
    // pool.
    return this->nextOffset().getOffset();
  }

  size_t size() const {
    // Return the current actual size of the buffer. This is only accurate
    // if there are no pending pool entries to dump, check.
    MOZ_ASSERT_IF(!this->oom(), pool_.numEntries() == 0);
    return sizeExcludingCurrentPool();
  }

 private:
  void insertNopFill() {
    // Insert fill for testing.
    if (nopFill_ > 0 && inhibitNops_ == 0 && inhibitPools_ == 0) {
      inhibitNops_++;

      // Fill using a branch-nop rather than a NOP so this can be
      // distinguished and skipped.
      for (size_t i = 0; i < nopFill_; i++) {
        putInt(NopFillInst);
      }

      inhibitNops_--;
    }
  }

  static const unsigned OOM_FAIL = unsigned(-1);
  static const unsigned DUMMY_INDEX = unsigned(-2);

  size_t sizeOfPrimaryVeneers() const {
    // When VeneerSize > 1, it is possible for branch deadlines to
    // expire faster than we can insert veneers. Suppose branches are 4 bytes
    // each and veneers are 8 bytes each, we could have the following deadline
    // set:
    //
    //   Range 0: 40, 44, 48
    //
    // It is not good enough to start inserting veneers at the 40 deadline; we
    // would not be able to create veneers for the 44 deadline.
    // Instead, we need to start at 32:
    //
    //   32: veneer(40)
    //   40: veneer(44)
    //   48: veneer(48)
    //
    // This is a pretty conservative solution to the problem: We reserve space
    // for all deadlines in the range with the most pending deadlines, assuming
    // worst case deadlines.
    return (VeneerSize - 1) * branchDeadlines_.maxRangeSize() * InstSize;
  }

  size_t sizeOfSecondaryVeneers(unsigned numNewDeadlines = 0) const {
    // When NumShortBranchRanges > 1, it is possible for branch deadlines to
    // expire faster than we can insert veneers. Suppose branches are 4 bytes
    // each, we could have the following deadline set:
    //
    //   Range 0: 40, 44, 48
    //   Range 1: 44, 48
    //
    // It is not good enough to start inserting veneers at the 40 deadline; we
    // would not be able to create veneers for the second 44 deadline.
    // Instead, we need to start at 32:
    //
    //   32: veneer(40)
    //   36: veneer(44)
    //   40: veneer(44)
    //   44: veneer(48)
    //   48: veneer(48)
    //
    // This is a pretty conservative solution to the problem: If we begin at
    // the earliest deadline, we can always emit all veneers for the range
    // that currently has the most pending deadlines. That may not leave room
    // for veneers for the remaining ranges, so reserve space for those
    // secondary range veneers assuming the worst case deadlines.

    // Total pending secondary range veneer size.
    return VeneerSize *
           (branchDeadlines_.size() - branchDeadlines_.maxRangeSize() +
            numNewDeadlines) *
           InstSize;
  }

  // Check if it is possible to add numInst instructions and numPoolEntries
  // constant pool entries without needing to flush the current pool.
  bool hasSpaceForInsts(unsigned numInsts, unsigned numPoolEntries,
                        unsigned numNewDeadlines = 0) const {
    size_t nextOffset = sizeExcludingCurrentPool();
    // Earliest starting offset for the current pool after adding numInsts.
    // This is the beginning of the pool entries proper, after inserting a
    // guard branch + pool header.
    size_t poolOffset =
        nextOffset + (numInsts + GuardSize + HeaderSize) * InstSize;

    // Any constant pool loads that would go out of range?
    if (pool_.checkFull(poolOffset)) {
      return false;
    }

    // Any short-range branch that would go out of range?
    //
    // NOTE: Must be kept in sync with hasExpirableShortRangeBranches.
    if (!branchDeadlines_.empty()) {
      size_t deadline = branchDeadlines_.earliestDeadline().getOffset();
      size_t poolEnd = poolOffset + pool_.getPoolSize() +
                       numPoolEntries * sizeof(PoolAllocUnit);
      size_t primaryVeneers = sizeOfPrimaryVeneers();
      size_t secondaryVeneers = sizeOfSecondaryVeneers(numNewDeadlines);

      if (deadline < poolEnd + primaryVeneers + secondaryVeneers) {
        return false;
      }
    }

    return true;
  }

  unsigned insertEntryForwards(unsigned numInst, unsigned numPoolEntries,
                               uint8_t* data, PoolEntryKind kind) {
    if constexpr (UseConstantPools) {
      // If inserting pool entries then find a new limiter before we do the
      // range check. A shared entry sits at a lower index than assumed here,
      // so the limiter is conservative for it.
      if (numPoolEntries) {
        pool_.updateLimiter(BufferOffset(sizeExcludingCurrentPool()));
      }
    } else {
      MOZ_ASSERT(numPoolEntries == 0);
    }

    if (!hasSpaceForInsts(numInst, numPoolEntries)) {
      if (numPoolEntries) {
        JitSpew(JitSpew_Pools, "Inserting pool entry caused a spill");
      } else {
        JitSpew(JitSpew_Pools, "Inserting instruction(%zu) caused a spill",
                sizeExcludingCurrentPool());
      }

      finishPool(numInst * InstSize);
      if (this->oom()) {
        return OOM_FAIL;
      }
      return insertEntryForwards(numInst, numPoolEntries, data, kind);
    }

    if constexpr (UseConstantPools) {
      if (numPoolEntries) {
        unsigned result =
            pool_.insertEntry(numPoolEntries, data, this->nextOffset(), kind);
        if (result == Pool::OOM_FAIL) {
          this->fail_oom();
          return OOM_FAIL;
        }
        return result;
      }
    }

    // The pool entry index is returned above when allocating an entry, but
    // when not allocating an entry a dummy value is returned - it is not
    // expected to be used by the caller.
    return DUMMY_INDEX;
  }

 public:
  // Get the next buffer offset where an instruction would be inserted.
  // This may flush the current constant pool before returning nextOffset().
  BufferOffset nextInstrOffset(unsigned numInsts, unsigned numNewDeadlines) {
    if (!hasSpaceForInsts(numInsts, /* numPoolEntries= */ 0, numNewDeadlines)) {
      JitSpew(JitSpew_Pools,
              "nextInstrOffset @ %d caused a constant pool spill",
              this->nextOffset().getOffset());
      finishPool(ShortRangeBranchHysteresis);
      MOZ_ASSERT_IF(
          !this->oom(),
          hasSpaceForInsts(numInsts, /* numPoolEntries= */ 0, numNewDeadlines));
    }
    return this->nextOffset();
  }

  MOZ_NEVER_INLINE
  BufferOffset allocEntry(size_t numInst, unsigned numPoolEntries,
                          uint8_t* inst, uint8_t* data,
                          PoolEntryKind kind = PoolEntryKind::Unshared) {
    // The allocation of pool entries is not supported in a no-pool region,
    // check.
    MOZ_ASSERT_IF(numPoolEntries > 0, inhibitPools_ == 0);

    if (this->oom()) {
      return BufferOffset();
    }

    insertNopFill();

#ifdef JS_JITSPEW
    if (numPoolEntries && JitSpewEnabled(JitSpew_Pools)) {
      JitSpew(JitSpew_Pools, "Inserting %d entries into pool", numPoolEntries);
      AutoJitSpewMessage msg(JitSpew_Pools, "data is: 0x");
      size_t length = numPoolEntries * sizeof(PoolAllocUnit);
      for (unsigned idx = 0; idx < length; idx++) {
        msg.append("%02x", data[length - idx - 1]);
        if (((idx & 3) == 3) && (idx + 1 != length)) {
          msg.append("_");
        }
      }
    }
#endif

    // Insert the pool value.
    unsigned index = insertEntryForwards(numInst, numPoolEntries, data, kind);
    if (this->oom()) {
      return BufferOffset();
    }

    // Now to get an instruction to write.
    if constexpr (UseConstantPools) {
      if (numPoolEntries) {
        JitSpew(JitSpew_Pools, "Entry has index %u, offset %zu", index,
                sizeExcludingCurrentPool());
        Asm::InsertIndexIntoTag(inst, index);
      }
    }

    // Now inst is a valid thing to insert into the instruction stream.
    return this->putBytes(numInst * InstSize, inst);
  }

  // putInt is the workhorse for the assembler and higher-level buffer
  // abstractions: it places one instruction into the instruction stream.
  // Under normal circumstances putInt should just check that the constant
  // pool does not need to be flushed, that there is space for the single word
  // of the instruction, and write that word and update the buffer pointer.
  //
  // To do better here we need a status variable that handles both nopFill_
  // and capacity, so that we can quickly know whether to go the slow path.
  // That could be a variable that has the remaining number of simple
  // instructions that can be inserted before a more expensive check,
  // which is set to zero when nopFill_ is set.
  //
  // We assume that we don't have to check this->oom() if there is space to
  // insert a plain instruction; there will always come a later time when it
  // will be checked anyway.

  MOZ_ALWAYS_INLINE
  BufferOffset putInt(uint32_t value) {
    if (nopFill_ ||
        !hasSpaceForInsts(/* numInsts= */ 1, /* numPoolEntries= */ 0)) {
      return allocEntry(1, 0, (uint8_t*)&value, nullptr);
    }

#if defined(JS_CODEGEN_ARM) || defined(JS_CODEGEN_ARM64) ||      \
    defined(JS_CODEGEN_MIPS64) || defined(JS_CODEGEN_LOONG64) || \
    defined(JS_CODEGEN_RISCV64)
    return this->putU32Aligned(value);
#else
    return this->AssemblerBuffer<Inst>::putInt(value);
#endif
  }

  // Register a short-range branch deadline.
  //
  // After inserting a short-range forward branch, call this method to
  // register the branch 'deadline' which is the last buffer offset that the
  // branch instruction can reach.
  //
  // When the branch is bound to a destination label, call
  // unregisterBranchDeadline() to stop tracking this branch,
  //
  // If the assembled code is about to exceed the registered branch deadline,
  // and unregisterBranchDeadline() has not yet been called, an
  // instruction-sized constant pool entry is allocated before the branch
  // deadline.
  //
  // rangeIdx
  //   A number < NumShortBranchRanges identifying the range of the branch.
  //
  // deadline
  //   The highest buffer offset the the short-range branch can reach
  //   directly.
  //
  void registerBranchDeadline(unsigned rangeIdx, BufferOffset deadline) {
    if (!this->oom()) {
      branchDeadlines_.addDeadline(rangeIdx, deadline);
    }
#ifdef DEBUG
    if (inhibitPools_ > 0) {
      inhibitPoolsActualNewDeadlines_++;
      MOZ_ASSERT(inhibitPoolsActualNewDeadlines_ <=
                 inhibitPoolsMaxNewDeadlines_);
    }
#endif
  }

  // Un-register a short-range branch deadline.
  //
  // When a short-range branch has been successfully bound to its destination
  // label, call this function to stop traching the branch.
  //
  // The (rangeIdx, deadline) pair must be previously registered.
  //
  void unregisterBranchDeadline(unsigned rangeIdx, BufferOffset deadline) {
    if (!this->oom()) {
      branchDeadlines_.removeDeadline(rangeIdx, deadline);
    }
#ifdef DEBUG
    if (inhibitPools_ > 0) {
      MOZ_ASSERT(inhibitPoolsMaxNewDeadlines_ > 0);
      inhibitPoolsActualNewDeadlines_--;
    }
#endif
  }

 private:
  // Are any short-range branches about to expire?
  //
  // NOTE: Must be kept in sync with hasSpaceForInsts.
  bool hasExpirableShortRangeBranches(size_t reservedBytes) const {
    if (branchDeadlines_.empty()) {
      return false;
    }

    // Include branches that would expire in the next N bytes. The reservedBytes
    // argument avoids the needless creation of many tiny constant pools.
    //
    // As the reservedBytes could be of any sizes such as SIZE_MAX, in the case
    // of flushPool, we have to check for overflow when comparing the deadline
    // with our expected reserved bytes.
    size_t deadline = branchDeadlines_.earliestDeadline().getOffset();
    size_t nextOffset = sizeExcludingCurrentPool();
    size_t poolOffset = nextOffset + (GuardSize + HeaderSize) * InstSize;
    mozilla::CheckedInt<size_t> poolFreeSpace(reservedBytes);
    auto future =
        (poolOffset + sizeOfPrimaryVeneers() + sizeOfSecondaryVeneers()) +
        poolFreeSpace;
    return !future.isValid() || deadline < future.value();
  }

  bool isPoolEmptyFor(size_t bytes) const {
    return pool_.numEntries() == 0 && !hasExpirableShortRangeBranches(bytes);
  }
  void finishPool(size_t reservedBytes) {
    JitSpew(JitSpew_Pools, "Attempting to finish pool with %u entries.",
            pool_.numEntries());

    if (reservedBytes < ShortRangeBranchHysteresis) {
      reservedBytes = ShortRangeBranchHysteresis;
    }

    if (isPoolEmptyFor(reservedBytes)) {
      // If there is no data in the pool being dumped, don't dump anything.
      JitSpew(JitSpew_Pools, "Aborting because the pool is empty");
      return;
    }

    // Should not be placing a pool in a no-pool region, check.
    MOZ_ASSERT(inhibitPools_ == 0);

    // Dump the pool with a guard branch around the pool.
    BufferOffset guard = this->putBytes(GuardSize * InstSize, nullptr);
    BufferOffset header;
    BufferOffset data;
    if constexpr (UseConstantPools) {
      header = this->putBytes(HeaderSize * InstSize, nullptr);
      data =
          this->putBytes(pool_.getPoolSize(), (const uint8_t*)pool_.poolData());
    }
    if (this->oom()) {
      return;
    }

    // Now generate branch veneers for any short-range branches that are
    // about to expire.
    while (hasExpirableShortRangeBranches(reservedBytes)) {
      unsigned rangeIdx = branchDeadlines_.earliestDeadlineRange();
      BufferOffset deadline = branchDeadlines_.earliestDeadline();

      // Stop tracking this branch. The Asm callback below may register
      // new branches to track.
      branchDeadlines_.removeDeadline(rangeIdx, deadline);

      // Make room for the veneer.
      BufferOffset veneer = this->putBytes(VeneerSize * InstSize, nullptr);
      if (this->oom()) {
        return;
      }

      // Fix the branch so it targets the veneer.
      // The Asm class knows how to find the original branch given the
      // (rangeIdx, deadline) pair.
      Asm::PatchShortRangeBranchToVeneer(this, rangeIdx, deadline, veneer);
    }

    // We only reserved space for the guard branch and pool header.
    // Fill them in.
    BufferOffset afterPool = this->nextOffset();
    Asm::WritePoolGuard(guard, this->getInst(guard), afterPool);
    if constexpr (UseConstantPools) {
      Asm::WritePoolHeader((uint8_t*)this->getInst(header), &pool_, false);

      // With the pool's final position determined it is now possible to patch
      // the instructions that reference entries in this pool, and this is
      // done incrementally as each pool is finished.
      size_t poolOffset = data.getOffset();

      unsigned idx = 0;
      for (BufferOffset* iter = pool_.loadOffsets.begin();
           iter != pool_.loadOffsets.end(); ++iter, ++idx) {
        // All entries should be before the pool.
        MOZ_ASSERT(iter->getOffset() < guard.getOffset());

        // Everything here is known so we can safely do the necessary
        // substitutions.
        Inst* inst = this->getInst(*iter);
        size_t codeOffset = poolOffset - iter->getOffset();

        // That is, PatchConstantPoolLoad wants to be handed the address of
        // the pool entry that is being loaded.  We need to do a non-trivial
        // amount of math here, since the pool that we've made does not
        // actually reside there in memory.
        JitSpew(JitSpew_Pools, "Fixing entry %d offset to %zu", idx,
                codeOffset);
        Asm::PatchConstantPoolLoad(inst, (uint8_t*)inst + codeOffset);
      }

      // Reset everything to the state that it was in when we started.
      pool_.reset();
    }
  }

 public:
  void flushPool() {
    if (this->oom()) {
      return;
    }
    JitSpew(JitSpew_Pools, "Requesting a pool flush");
    finishPool(SIZE_MAX);
  }

  void enterNoPool(size_t maxInst, size_t maxNewDeadlines = 0) {
    // Calling this with a zero arg is pointless.
    MOZ_ASSERT(maxInst > 0);

    if (this->oom()) {
      return;
    }

    if (inhibitPools_ > 0) {
      // This is a nested call to enterNoPool.  Assert that the reserved area
      // fits within that of the outermost call, but otherwise don't do
      // anything.
      //
      // Assert that the outermost call set these.
      MOZ_ASSERT(inhibitPoolsStartOffset_ != ~size_t(0));
      MOZ_ASSERT(inhibitPoolsMaxInst_ > 0);
      // Check inner area fits within that of the outermost.
      MOZ_ASSERT(size_t(this->nextOffset().getOffset()) >=
                 inhibitPoolsStartOffset_);
      MOZ_ASSERT(size_t(this->nextOffset().getOffset()) + maxInst * InstSize <=
                 inhibitPoolsStartOffset_ + inhibitPoolsMaxInst_ * InstSize);
      MOZ_ASSERT(inhibitPoolsActualNewDeadlines_ + maxNewDeadlines <=
                 inhibitPoolsMaxNewDeadlines_);
      inhibitPools_++;
      return;
    }

    // This is an outermost level call to enterNoPool.
    MOZ_ASSERT(inhibitPools_ == 0);
    MOZ_ASSERT(inhibitPoolsStartOffset_ == ~size_t(0));
    MOZ_ASSERT(inhibitPoolsMaxInst_ == 0);
    MOZ_ASSERT(inhibitPoolsMaxNewDeadlines_ == 0);
    MOZ_ASSERT(inhibitPoolsActualNewDeadlines_ == 0);

    insertNopFill();

    // Check if the pool will spill by adding maxInst instructions, and if
    // so then finish the pool before entering the no-pool region. It is
    // assumed that no pool entries are allocated in a no-pool region and
    // this is asserted when allocating entries.
    if (!hasSpaceForInsts(maxInst, 0, maxNewDeadlines)) {
      JitSpew(JitSpew_Pools, "No-Pool instruction(%zu) caused a spill.",
              sizeExcludingCurrentPool());
      finishPool(maxInst * InstSize);
      if (this->oom()) {
        return;
      }
      MOZ_ASSERT(hasSpaceForInsts(maxInst, 0, maxNewDeadlines));
    }

#ifdef DEBUG
    // Record the buffer position to allow validating maxInst when leaving
    // the region.
    inhibitPoolsStartOffset_ = this->nextOffset().getOffset();
    inhibitPoolsMaxInst_ = maxInst;
    inhibitPoolsMaxNewDeadlines_ = maxNewDeadlines;
    inhibitPoolsActualNewDeadlines_ = 0;
    MOZ_ASSERT(inhibitPoolsStartOffset_ != ~size_t(0));
#endif

    inhibitPools_ = 1;
  }

  void leaveNoPool() {
    if (this->oom()) {
      inhibitPools_ = 0;
      return;
    }
    MOZ_ASSERT(inhibitPools_ > 0);

    if (inhibitPools_ > 1) {
      // We're leaving a non-outermost nesting level.  Note that fact, but
      // otherwise do nothing.
      inhibitPools_--;
      return;
    }

    // This is an outermost level call to leaveNoPool.
    MOZ_ASSERT(inhibitPools_ == 1);
    MOZ_ASSERT(inhibitPoolsStartOffset_ != ~size_t(0));
    MOZ_ASSERT(inhibitPoolsMaxInst_ > 0);

    // Validate the maxInst argument supplied to enterNoPool(), in the case
    // where we are leaving the outermost nesting level.
    MOZ_ASSERT(this->nextOffset().getOffset() - inhibitPoolsStartOffset_ <=
               inhibitPoolsMaxInst_ * InstSize);
    MOZ_ASSERT(inhibitPoolsActualNewDeadlines_ <= inhibitPoolsMaxNewDeadlines_);

#ifdef DEBUG
    inhibitPoolsStartOffset_ = ~size_t(0);
    inhibitPoolsMaxInst_ = 0;
    inhibitPoolsMaxNewDeadlines_ = 0;
    inhibitPoolsActualNewDeadlines_ = 0;
#endif

    inhibitPools_ = 0;
  }

  void enterNoNops() { inhibitNops_++; }
  void leaveNoNops() {
    MOZ_ASSERT(inhibitNops_ > 0);
    inhibitNops_--;
  }
  void assertNoPoolAndNoNops() {
    MOZ_ASSERT(inhibitNops_ > 0);
    MOZ_ASSERT_IF(!this->oom(), isPoolEmptyFor(InstSize) || inhibitPools_ > 0);
  }

  void align(unsigned alignment) { align(alignment, AlignFillInst); }

  void align(unsigned alignment, uint32_t pattern) {
    MOZ_ASSERT(std::has_single_bit(alignment));
    MOZ_ASSERT(alignment >= InstSize);

    // A pool many need to be dumped at this point, so insert NOP fill here.
    insertNopFill();

    // Check if the code position can be aligned without dumping a pool.
    unsigned requiredFill = sizeExcludingCurrentPool() & (alignment - 1);
    if (requiredFill == 0) {
      return;
    }
    requiredFill = alignment - requiredFill;

    // Add an InstSize because it is probably not useful for a pool to be
    // dumped at the aligned code position.
    if (!hasSpaceForInsts(requiredFill / InstSize + 1, 0)) {
      // Alignment would cause a pool dump, so dump the pool now.
      JitSpew(JitSpew_Pools, "Alignment of %d at %zu caused a spill.",
              alignment, sizeExcludingCurrentPool());
      finishPool(requiredFill);
    }

    inhibitNops_++;
    while ((sizeExcludingCurrentPool() & (alignment - 1)) && !this->oom()) {
      putInt(pattern);
    }
    inhibitNops_--;
  }

 public:
  void executableCopy(uint8_t* dest) {
    if (this->oom()) {
      return;
    }
    // The pools should have all been flushed, check.
    MOZ_ASSERT(pool_.numEntries() == 0);
    memcpy(dest, this->data(), this->size());
  }

  bool appendRawCode(const uint8_t* code, size_t numBytes) {
    if (this->oom()) {
      return false;
    }
    MOZ_ASSERT(pool_.numEntries() == 0);
    this->putBytes(numBytes, code);
    return !this->oom();
  }
};

}  // namespace jit
}  // namespace js

#endif  // jit_shared_IonAssemblerBufferWithConstantPools_h
