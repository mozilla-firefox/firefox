/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CollectorLogAnalyzerBackground_h
#define mozilla_CollectorLogAnalyzerBackground_h

#include "CollectorLogAnalyzer.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/dom/CollectorLogAnalyzerBinding.h"

namespace mozilla {

enum class CCLogSection {
  Graph,
  Results,
};

enum class GCLogSection {
  BlackRoots,
  GrayRoots,
  Graph,
};

enum class WeakMapEdgeKind : int8_t {
  None = -1,
  WeakMapKey = 0,
  WeakMapKeyDelegate = 1,
};

// A node's original memory address from the log, used as a unique identifier.
using NodeId = uint64_t;
using NodeTableIndex = size_t;
using EdgeTableIndex = size_t;
using WeakMapEdgeTableIndex = size_t;
using StringBufferIndex = size_t;

static constexpr NodeTableIndex INVALID_NODE =
    std::numeric_limits<NodeTableIndex>::max();

static constexpr NodeTableIndex INVALID_STRING =
    std::numeric_limits<StringBufferIndex>::max();

struct WeakMapEdge {
  NodeTableIndex mKey;
  NodeTableIndex mMap;
  NodeTableIndex mValue;
  WeakMapEdgeKind mKind;
  bool mKeyIsSource;

  NodeTableIndex source() const { return mKeyIsSource ? mKey : mMap; }
  NodeTableIndex other() const { return mKeyIsSource ? mMap : mKey; }
};

struct NodeEdgesDescriptor {
  EdgeTableIndex mCC;
  size_t mCCCount;
  EdgeTableIndex mGC;
  size_t mGCCount;
  WeakMapEdgeTableIndex mWeakMap;
  size_t mWeakMapCount;
};

struct WeakMapEntry {
  NodeId mMap;
  NodeId mKey;
  NodeId mKeyDelegate;
  NodeId mValue;
};

using LogError = CollectorLogAnalyzer::LogError;

class CollectorLogAnalyzerBackground {
  ~CollectorLogAnalyzerBackground() = default;

 public:
  Result<Ok, LogError> EnsureInitialized();

  double GetInitProgress() {
    double totalSize = double(mCCFileSize.load()) + double(mGCFileSize.load()) +
                       double(mQuerySize.load());
    double totalProgress = double(mCCFileProgress.load()) +
                           double(mGCFileProgress.load()) +
                           double(mQueryProgress.load());
    return totalSize > 0 ? totalProgress / totalSize : 0;
  }

  double GetQueryProgress() {
    if (mQuerySize == 0) {
      return 0;
    }
    return double(mQueryProgress) / double(mQuerySize);
  }

  Result<StringBufferIndex, LogError> InternString(const nsCString& aStr);
  Result<NodeTableIndex, LogError> EnsureNode(NodeId aNodeId);
  Result<Ok, LogError> AddWeakMapEdge(NodeTableIndex aKey, NodeTableIndex aMap,
                                      NodeTableIndex aValue, bool aKeyDelegate,
                                      bool aKeyIsSource);
  Result<Ok, LogError> AddWeakMapEntry(const WeakMapEntry& aEntry);
  Result<Ok, LogError> AddCCEdge(NodeTableIndex aCurrentNode,
                                 NodeTableIndex aEdge,
                                 StringBufferIndex aLabel);
  Result<Ok, LogError> AddGCEdge(NodeTableIndex aCurrentNode,
                                 NodeTableIndex aEdge,
                                 StringBufferIndex aLabel);
  dom::CollectorLogNode MakeResultNode(NodeTableIndex aIndex);

  // Returns the number of bytes consumed from the buffer.
  Result<size_t, LogError> IngestCycleCollectorLog(const nsACString& aBuf,
                                                   bool aContainsFileEnd);
  // Returns the number of bytes consumed from the buffer.
  Result<size_t, LogError> IngestGarbageCollectorLog(const nsACString& aBuf,
                                                     bool aContainsFileEnd);

  Result<Ok, LogError> InitImpl(const nsAString& aCCLogPath,
                                const nsAString& aGCLogPath);
  Result<nsTArray<dom::CollectorLogNode>, LogError> QueryNodesImpl(
      const nsCString& aQuery);
  Result<nsTArray<dom::CollectorLogNode>, LogError> SampleNodesImpl();
  Result<dom::CollectorLogNodeAdjacents, LogError> GetNodeAdjacentsImpl(
      NodeTableIndex aNodeIndex);

  Result<dom::CollectorLogRootPath, LogError> GetPathToRootInner(
      NodeTableIndex aNodeIndex, bool aOnlyUseSoftRoots);

  Result<dom::CollectorLogRootPath, LogError> GetPathToRootImpl(
      NodeTableIndex aNodeIndex) {
    mQueryProgress = 0;
    return GetPathToRootInner(aNodeIndex, /* aOnlyUseSoftRoots = */ false);
  }

#ifdef ENABLE_TESTS
  friend class CollectorLogAnalyzerTestHelper;
#endif

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CollectorLogAnalyzerBackground)
 private:
  Result<Ok, LogError> FinishInitialization();
  // Progress tracking (atomic for cross-thread reads from main thread)
  Atomic<size_t, MemoryOrdering::Relaxed> mCCFileSize;
  Atomic<size_t, MemoryOrdering::Relaxed> mCCFileProgress;
  Atomic<size_t, MemoryOrdering::Relaxed> mGCFileSize;
  Atomic<size_t, MemoryOrdering::Relaxed> mGCFileProgress;
  Atomic<size_t, MemoryOrdering::Relaxed> mQuerySize;
  Atomic<size_t, MemoryOrdering::Relaxed> mQueryProgress;

  // Parser state for error reporting
  size_t mCCLineNumber = 0;
  size_t mGCLineNumber = 0;

  // Temporary tables used during parsing, cleared after init
  HashMap<nsCString, StringBufferIndex> mStringTable;  // Deduplicates strings
  HashMap<NodeId, NodeTableIndex> mNodeIdsToIndices;   // Maps address to index

  // Parser state machine
  CCLogSection mCurrentCCSection = CCLogSection::Graph;
  NodeTableIndex mCurrentCCNode = INVALID_NODE;
  GCLogSection mCurrentGCSection = GCLogSection::BlackRoots;
  NodeTableIndex mCurrentGCNode = INVALID_NODE;

  // Node data (parallel arrays indexed by NodeTableIndex)
  Vector<NodeId> mNodeIds;                 // Original memory address
  Vector<StringBufferIndex> mNodeLabels;   // Index into mStrings for node label
  Vector<uint8_t> mNodeFlags;              // CollectorNodeFlags bit field
  Vector<NodeEdgesDescriptor> mNodeEdges;  // Offsets into edge arrays

  // Reference counting
  HashMap<NodeTableIndex, uint32_t> mCCReferenceCounts;  // Declared RC from log
  Vector<uint32_t>
      mObservedReferenceCounts;  // Incoming edges counted post-parse

  // Edge data (indexed by EdgeTableIndex from NodeEdgesDescriptor)
  Vector<NodeTableIndex> mEdges;          // Target node of each edge
  Vector<StringBufferIndex> mEdgeLabels;  // Index into mStrings for field label
  Vector<WeakMapEdge> mWeakMapEdges;      // WeakMap edges (sorted by source)

  // Root lists for path-finding (hard roots searched first, then soft)
  Vector<NodeTableIndex> mCCRoots;      // CC nodes with external references
  Vector<NodeTableIndex> mCCSoftRoots;  // CC nodes fully accounted in graph
  Vector<NodeTableIndex> mGCRoots;      // GC black roots
  Vector<NodeTableIndex> mGCGrayRoots;  // GC gray roots
  Vector<NodeTableIndex> mIncrementalRoots;  // Incremental CC roots (unused)

  // Interned string storage (null-terminated, concatenated)
  Vector<char> mStrings;

  bool mHaveCC = false;
  bool mHaveGC = false;
  bool mInitialized = false;
};

}  // namespace mozilla

#endif  // mozilla_CollectorLogAnalyzer_h
