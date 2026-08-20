/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HeapSnapshot.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/gzip_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include "js/Array.h"  // JS::NewArrayObject
#include "js/ColumnNumber.h"  // JS::LimitedColumnNumberOneOrigin, JS::TaggedColumnNumberOneOrigin
#include "js/Debug.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "js/TypeDecls.h"
#include "js/UbiNodeBreadthFirst.h"
#include "js/UbiNodeCensus.h"
#include "js/UbiNodeDominatorTree.h"
#include "js/UbiNodeShortestPaths.h"
#include "mozilla/Attributes.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/devtools/AutoMemMap.h"
#include "mozilla/devtools/CoreDump.pb.h"
#include "mozilla/devtools/DeserializedNode.h"
#include "mozilla/devtools/DominatorTree.h"
#include "mozilla/devtools/FileDescriptorOutputStream.h"
#include "mozilla/devtools/HeapSnapshotTempFileHelperChild.h"
#include "mozilla/devtools/ZeroCopyNSIOutputStream.h"
#include "mozilla/dom/ChromeUtils.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/HeapSnapshotBinding.h"
#include "mozilla/RangedPtr.h"
#include "mozilla/glean/DevtoolsSharedHeapsnapshotMetrics.h"

#include "jsapi.h"
#include "jsfriendapi.h"
#include "js/GCVector.h"
#include "js/MapAndSet.h"
#include "js/Object.h"                // JS::GetCompartment
#include "nsComponentManagerUtils.h"  // do_CreateInstance
#include "nsCycleCollectionParticipant.h"
#include "nsCRTGlue.h"
#include "nsIFile.h"
#include "nsIOutputStream.h"
#include "nsISupportsImpl.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "prerror.h"
#include "prio.h"
#include "prtypes.h"
#include "SpecialSystemDirectory.h"

namespace mozilla {
namespace devtools {

using namespace JS;
using namespace dom;

using ::google::protobuf::io::ArrayInputStream;
using ::google::protobuf::io::CodedInputStream;
using ::google::protobuf::io::GzipInputStream;
using ::google::protobuf::io::ZeroCopyInputStream;

using JS::ubi::AtomOrTwoByteChars;
using JS::ubi::ShortestPaths;

MallocSizeOf GetCurrentThreadDebuggerMallocSizeOf() {
  auto ccjscx = CycleCollectedJSContext::Get();
  MOZ_ASSERT(ccjscx);
  auto cx = ccjscx->Context();
  MOZ_ASSERT(cx);
  auto mallocSizeOf = JS::dbg::GetDebuggerMallocSizeOf(cx);
  MOZ_ASSERT(mallocSizeOf);
  return mallocSizeOf;
}

/*** Cycle Collection Boilerplate *********************************************/

NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(HeapSnapshot, mParent)

NS_IMPL_CYCLE_COLLECTING_ADDREF(HeapSnapshot)
NS_IMPL_CYCLE_COLLECTING_RELEASE(HeapSnapshot)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(HeapSnapshot)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

/* virtual */
JSObject* HeapSnapshot::WrapObject(JSContext* aCx,
                                   JS::Handle<JSObject*> aGivenProto) {
  return HeapSnapshot_Binding::Wrap(aCx, this, aGivenProto);
}

/*** Reading Heap Snapshots ***************************************************/

/* static */
already_AddRefed<HeapSnapshot> HeapSnapshot::Create(JSContext* cx,
                                                    GlobalObject& global,
                                                    const uint8_t* buffer,
                                                    uint32_t size,
                                                    ErrorResult& rv) {
  RefPtr<HeapSnapshot> snapshot = new HeapSnapshot(cx, global.GetAsSupports());
  if (!snapshot->init(cx, buffer, size)) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }
  return snapshot.forget();
}

template <typename MessageType>
static bool parseMessage(ZeroCopyInputStream& stream, uint32_t sizeOfMessage,
                         MessageType& message) {
  // We need to create a new `CodedInputStream` for each message so that the
  // 64MB limit is applied per-message rather than to the whole stream.
  CodedInputStream codedStream(&stream);

  // The protobuf message nesting that core dumps exhibit is dominated by
  // allocation stacks' frames. In the most deeply nested case, each frame has
  // two messages: a StackFrame message and a StackFrame::Data message. These
  // frames are on top of a small constant of other messages. There are a
  // MAX_STACK_DEPTH number of frames, so we multiply this by 3 to make room for
  // the two messages per frame plus some head room for the constant number of
  // non-dominating messages.
  codedStream.SetRecursionLimit(HeapSnapshot::MAX_STACK_DEPTH * 3);

  auto limit = codedStream.PushLimit(sizeOfMessage);
  if (NS_WARN_IF(!message.ParseFromCodedStream(&codedStream)) ||
      NS_WARN_IF(!codedStream.ConsumedEntireMessage()) ||
      NS_WARN_IF(codedStream.BytesUntilLimit() != 0)) {
    return false;
  }

  codedStream.PopLimit(limit);
  return true;
}

template <typename CharT, typename InternedStringSet>
struct GetOrInternStringMatcher {
  InternedStringSet& internedStrings;

  explicit GetOrInternStringMatcher(InternedStringSet& strings)
      : internedStrings(strings) {}

  const CharT* operator()(const std::string* str) {
    MOZ_ASSERT(str);
    size_t length = str->length() / sizeof(CharT);
    auto tempString = reinterpret_cast<const CharT*>(str->data());

    UniqueFreePtr<CharT[]> owned(NS_xstrndup(tempString, length));
    if (!internedStrings.append(std::move(owned))) return nullptr;

    return internedStrings.back().get();
  }

  const CharT* operator()(uint64_t ref) {
    if (MOZ_LIKELY(ref < internedStrings.length())) {
      auto& string = internedStrings[ref];
      MOZ_ASSERT(string);
      return string.get();
    }

    return nullptr;
  }
};

template <
    // Either char or char16_t.
    typename CharT,
    // A reference to either `internedOneByteStrings` or
    // `internedTwoByteStrings` if CharT is char or char16_t respectively.
    typename InternedStringSet>
const CharT* HeapSnapshot::getOrInternString(
    InternedStringSet& internedStrings, Maybe<StringOrRef>& maybeStrOrRef) {
  // Incomplete message: has neither a string nor a reference to an already
  // interned string.
  if (MOZ_UNLIKELY(maybeStrOrRef.isNothing())) return nullptr;

  GetOrInternStringMatcher<CharT, InternedStringSet> m(internedStrings);
  return maybeStrOrRef->match(m);
}

// Get a de-duplicated string as a Maybe<StringOrRef> from the given `msg`.
#define GET_STRING_OR_REF_WITH_PROP_NAMES(msg, strPropertyName,              \
                                          refPropertyName)                   \
  (msg.has_##refPropertyName()   ? Some(StringOrRef(msg.refPropertyName()))  \
   : msg.has_##strPropertyName() ? Some(StringOrRef(&msg.strPropertyName())) \
                                 : Nothing())

#define GET_STRING_OR_REF(msg, property)                              \
  (msg.has_##property##ref() ? Some(StringOrRef(msg.property##ref())) \
   : msg.has_##property()    ? Some(StringOrRef(&msg.property()))     \
                             : Nothing())

bool HeapSnapshot::saveNode(const protobuf::Node& node,
                            NodeIdSet& edgeReferents) {
  // NB: de-duplicated string properties must be read back and interned in the
  // same order here as they are written and serialized in
  // `CoreDumpWriter::writeNode` or else indices in references to already
  // serialized strings will be off.

  if (NS_WARN_IF(!node.has_id())) return false;
  NodeId id = node.id();

  // NodeIds are derived from pointers (at most 48 bits) and we rely on them
  // fitting into JS numbers (IEEE 754 doubles, can precisely store 53 bit
  // integers) despite storing them on disk as 64 bit integers.
  if (NS_WARN_IF(!JS::Value::isNumberRepresentable(id))) return false;

  // Should only deserialize each node once.
  if (NS_WARN_IF(nodes.has(id))) return false;

  if (NS_WARN_IF(!JS::ubi::Uint32IsValidCoarseType(node.coarsetype())))
    return false;
  auto coarseType = JS::ubi::Uint32ToCoarseType(node.coarsetype());

  Maybe<StringOrRef> typeNameOrRef =
      GET_STRING_OR_REF_WITH_PROP_NAMES(node, typename_, typenameref);
  auto typeName =
      getOrInternString<char16_t>(internedTwoByteStrings, typeNameOrRef);
  if (NS_WARN_IF(!typeName)) return false;

  if (NS_WARN_IF(!node.has_size())) return false;
  uint64_t size = node.size();

  auto edgesLength = node.edges_size();
  DeserializedNode::EdgeVector edges;
  if (NS_WARN_IF(!edges.reserve(edgesLength))) return false;
  for (decltype(edgesLength) i = 0; i < edgesLength; i++) {
    auto& protoEdge = node.edges(i);

    if (NS_WARN_IF(!protoEdge.has_referent())) return false;
    NodeId referent = protoEdge.referent();

    if (NS_WARN_IF(!edgeReferents.put(referent))) return false;

    const char16_t* edgeName = nullptr;
    if (protoEdge.EdgeNameOrRef_case() !=
        protobuf::Edge::EDGENAMEORREF_NOT_SET) {
      Maybe<StringOrRef> edgeNameOrRef = GET_STRING_OR_REF(protoEdge, name);
      edgeName =
          getOrInternString<char16_t>(internedTwoByteStrings, edgeNameOrRef);
      if (NS_WARN_IF(!edgeName)) return false;
    }

    edges.infallibleAppend(DeserializedEdge(referent, edgeName));
  }

  Maybe<StackFrameId> allocationStack;
  if (node.has_allocationstack()) {
    StackFrameId id = 0;
    if (NS_WARN_IF(!saveStackFrame(node.allocationstack(), id))) return false;
    allocationStack.emplace(id);
  }
  MOZ_ASSERT(allocationStack.isSome() == node.has_allocationstack());

  const char* jsObjectClassName = nullptr;
  if (node.JSObjectClassNameOrRef_case() !=
      protobuf::Node::JSOBJECTCLASSNAMEORREF_NOT_SET) {
    Maybe<StringOrRef> clsNameOrRef =
        GET_STRING_OR_REF(node, jsobjectclassname);
    jsObjectClassName =
        getOrInternString<char>(internedOneByteStrings, clsNameOrRef);
    if (NS_WARN_IF(!jsObjectClassName)) return false;
  }

  const char* scriptFilename = nullptr;
  if (node.ScriptFilenameOrRef_case() !=
      protobuf::Node::SCRIPTFILENAMEORREF_NOT_SET) {
    Maybe<StringOrRef> scriptFilenameOrRef =
        GET_STRING_OR_REF(node, scriptfilename);
    scriptFilename =
        getOrInternString<char>(internedOneByteStrings, scriptFilenameOrRef);
    if (NS_WARN_IF(!scriptFilename)) return false;
  }

  const char16_t* descriptiveTypeName = nullptr;
  if (node.descriptiveTypeNameOrRef_case() !=
      protobuf::Node::DESCRIPTIVETYPENAMEORREF_NOT_SET) {
    Maybe<StringOrRef> descriptiveTypeNameOrRef =
        GET_STRING_OR_REF(node, descriptivetypename);
    descriptiveTypeName = getOrInternString<char16_t>(internedTwoByteStrings,
                                                      descriptiveTypeNameOrRef);
    if (NS_WARN_IF(!descriptiveTypeName)) return false;
  }

  if (NS_WARN_IF(!nodes.putNew(
          id, DeserializedNode(id, coarseType, typeName, size, std::move(edges),
                               allocationStack, jsObjectClassName,
                               scriptFilename, descriptiveTypeName, *this)))) {
    return false;
  };

  return true;
}

bool HeapSnapshot::saveStackFrame(const protobuf::StackFrame& frame,
                                  StackFrameId& outFrameId) {
  // NB: de-duplicated string properties must be read in the same order here as
  // they are written in `CoreDumpWriter::getProtobufStackFrame` or else indices
  // in references to already serialized strings will be off.

  if (frame.has_ref()) {
    // We should only get a reference to the previous frame if we have already
    // seen the previous frame.
    if (!frames.has(frame.ref())) return false;

    outFrameId = frame.ref();
    return true;
  }

  // Incomplete message.
  if (!frame.has_data()) return false;

  const auto& data = frame.data();

  if (!data.has_id()) return false;
  StackFrameId id = data.id();

  // This should be the first and only time we see this frame.
  if (frames.has(id)) return false;

  if (!data.has_line()) return false;
  uint32_t line = data.line();

  if (!data.has_column()) return false;
  JS::TaggedColumnNumberOneOrigin column(
      JS::LimitedColumnNumberOneOrigin(data.column()));

  if (!data.has_issystem()) return false;
  bool isSystem = data.issystem();

  if (!data.has_isselfhosted()) return false;
  bool isSelfHosted = data.isselfhosted();

  Maybe<StringOrRef> sourceOrRef = GET_STRING_OR_REF(data, source);
  auto source =
      getOrInternString<char16_t>(internedTwoByteStrings, sourceOrRef);
  if (!source) return false;

  const char16_t* functionDisplayName = nullptr;
  if (data.FunctionDisplayNameOrRef_case() !=
      protobuf::StackFrame_Data::FUNCTIONDISPLAYNAMEORREF_NOT_SET) {
    Maybe<StringOrRef> nameOrRef = GET_STRING_OR_REF(data, functiondisplayname);
    functionDisplayName =
        getOrInternString<char16_t>(internedTwoByteStrings, nameOrRef);
    if (!functionDisplayName) return false;
  }

  Maybe<StackFrameId> parent;
  if (data.has_parent()) {
    StackFrameId parentId = 0;
    if (!saveStackFrame(data.parent(), parentId)) return false;
    parent = Some(parentId);
  }

  if (!frames.putNew(id,
                     DeserializedStackFrame(id, parent, line, column, source,
                                            functionDisplayName, isSystem,
                                            isSelfHosted, *this))) {
    return false;
  }

  outFrameId = id;
  return true;
}

#undef GET_STRING_OR_REF_WITH_PROP_NAMES
#undef GET_STRING_OR_REF

// Because protobuf messages aren't self-delimiting, we serialize each message
// preceded by its size in bytes. When deserializing, we read this size and then
// limit reading from the stream to the given byte size. If we didn't, then the
// first message would consume the entire stream.
static bool readSizeOfNextMessage(ZeroCopyInputStream& stream,
                                  uint32_t* sizep) {
  MOZ_ASSERT(sizep);
  CodedInputStream codedStream(&stream);
  return codedStream.ReadVarint32(sizep) && *sizep > 0;
}

bool HeapSnapshot::init(JSContext* cx, const uint8_t* buffer, uint32_t size) {
  ArrayInputStream stream(buffer, size);
  GzipInputStream gzipStream(&stream);
  uint32_t sizeOfMessage = 0;

  // First is the metadata.

  protobuf::Metadata metadata;
  if (NS_WARN_IF(!readSizeOfNextMessage(gzipStream, &sizeOfMessage)))
    return false;
  if (!parseMessage(gzipStream, sizeOfMessage, metadata)) return false;
  if (metadata.has_timestamp()) timestamp.emplace(metadata.timestamp());

  // Next is the root node.

  protobuf::Node root;
  if (NS_WARN_IF(!readSizeOfNextMessage(gzipStream, &sizeOfMessage)))
    return false;
  if (!parseMessage(gzipStream, sizeOfMessage, root)) return false;

  // Although the id is optional in the protobuf format for future proofing, we
  // can't currently do anything without it.
  if (NS_WARN_IF(!root.has_id())) return false;
  rootId = root.id();

  // The set of all node ids we've found edges pointing to.
  NodeIdSet edgeReferents(cx);

  if (NS_WARN_IF(!saveNode(root, edgeReferents))) return false;

  // Finally, the rest of the nodes in the core dump.

  // Test for the end of the stream. The protobuf library gives no way to tell
  // the difference between an underlying read error and the stream being
  // done. All we can do is attempt to read the size of the next message and
  // extrapolate guestimations from the result of that operation.
  while (readSizeOfNextMessage(gzipStream, &sizeOfMessage)) {
    protobuf::Node node;
    if (!parseMessage(gzipStream, sizeOfMessage, node)) return false;
    if (NS_WARN_IF(!saveNode(node, edgeReferents))) return false;
  }

  // Check the set of node ids referred to by edges we found and ensure that we
  // have the node corresponding to each id. If we don't have all of them, it is
  // unsafe to perform analyses of this heap snapshot.
  for (auto iter = edgeReferents.iter(); !iter.done(); iter.next()) {
    if (NS_WARN_IF(!nodes.has(iter.get()))) return false;
  }

  return true;
}

/*** Heap Snapshot Analyses ***************************************************/

void HeapSnapshot::TakeCensus(JSContext* cx, JS::Handle<JSObject*> options,
                              JS::MutableHandle<JS::Value> rval,
                              ErrorResult& rv) {
  JS::ubi::Census census(cx);

  JS::ubi::CountTypePtr rootType;
  if (NS_WARN_IF(!JS::ubi::ParseCensusOptions(cx, census, options, rootType))) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  JS::ubi::RootedCount rootCount(cx, rootType->makeCount());
  if (NS_WARN_IF(!rootCount)) {
    rv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  JS::ubi::CensusHandler handler(census, rootCount,
                                 GetCurrentThreadDebuggerMallocSizeOf());

  {
    JS::AutoCheckCannotGC nogc;

    JS::ubi::CensusTraversal traversal(cx, handler, nogc);

    if (NS_WARN_IF(!traversal.addStart(getRoot()))) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }

    if (NS_WARN_IF(!traversal.traverse())) {
      rv.Throw(NS_ERROR_UNEXPECTED);
      return;
    }
  }

  if (NS_WARN_IF(!handler.report(cx, rval))) {
    rv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
}

void HeapSnapshot::DescribeNode(JSContext* cx, JS::Handle<JSObject*> breakdown,
                                uint64_t nodeId,
                                JS::MutableHandle<JS::Value> rval,
                                ErrorResult& rv) {
  MOZ_ASSERT(breakdown);
  JS::Rooted<JS::Value> breakdownVal(cx, JS::ObjectValue(*breakdown));
  JS::Rooted<JS::GCVector<JSLinearString*>> seen(cx, cx);
  JS::ubi::CountTypePtr rootType =
      JS::ubi::ParseBreakdown(cx, breakdownVal, &seen);
  if (NS_WARN_IF(!rootType)) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  JS::ubi::RootedCount rootCount(cx, rootType->makeCount());
  if (NS_WARN_IF(!rootCount)) {
    rv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  JS::ubi::Node::Id id(nodeId);
  Maybe<JS::ubi::Node> node = getNodeById(id);
  if (NS_WARN_IF(node.isNothing())) {
    rv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  MallocSizeOf mallocSizeOf = GetCurrentThreadDebuggerMallocSizeOf();
  if (NS_WARN_IF(!rootCount->count(mallocSizeOf, *node))) {
    rv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  if (NS_WARN_IF(!rootCount->report(cx, rval))) {
    rv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }
}

already_AddRefed<DominatorTree> HeapSnapshot::ComputeDominatorTree(
    ErrorResult& rv) {
  Maybe<JS::ubi::DominatorTree> maybeTree;
  {
    auto ccjscx = CycleCollectedJSContext::Get();
    MOZ_ASSERT(ccjscx);
    auto cx = ccjscx->Context();
    MOZ_ASSERT(cx);
    JS::AutoCheckCannotGC nogc(cx);
    maybeTree = JS::ubi::DominatorTree::Create(cx, nogc, getRoot());
  }

  if (NS_WARN_IF(maybeTree.isNothing())) {
    rv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }

  return MakeAndAddRef<DominatorTree>(std::move(*maybeTree), this, mParent);
}

void HeapSnapshot::ComputeShortestPaths(JSContext* cx, uint64_t start,
                                        const Sequence<uint64_t>& targets,
                                        uint64_t maxNumPaths,
                                        JS::MutableHandle<JSObject*> results,
                                        ErrorResult& rv) {
  // First ensure that our inputs are valid.

  if (NS_WARN_IF(maxNumPaths == 0)) {
    rv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  Maybe<JS::ubi::Node> startNode = getNodeById(start);
  if (NS_WARN_IF(startNode.isNothing())) {
    rv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  if (NS_WARN_IF(targets.Length() == 0)) {
    rv.Throw(NS_ERROR_INVALID_ARG);
    return;
  }

  // Aggregate the targets into a set and make sure that they exist in the heap
  // snapshot.

  JS::ubi::NodeSet targetsSet;

  for (const auto& target : targets) {
    Maybe<JS::ubi::Node> targetNode = getNodeById(target);
    if (NS_WARN_IF(targetNode.isNothing())) {
      rv.Throw(NS_ERROR_INVALID_ARG);
      return;
    }

    if (NS_WARN_IF(!targetsSet.put(*targetNode))) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
  }

  // Walk the heap graph and find the shortest paths.

  Maybe<ShortestPaths> maybeShortestPaths;
  {
    JS::AutoCheckCannotGC nogc(cx);
    maybeShortestPaths = ShortestPaths::Create(
        cx, nogc, maxNumPaths, *startNode, std::move(targetsSet));
  }

  if (NS_WARN_IF(maybeShortestPaths.isNothing())) {
    rv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  auto& shortestPaths = *maybeShortestPaths;

  // Convert the results into a Map object mapping target node IDs to arrays of
  // paths found.

  JS::Rooted<JSObject*> resultsMap(cx, JS::NewMapObject(cx));
  if (NS_WARN_IF(!resultsMap)) {
    rv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return;
  }

  for (auto iter = shortestPaths.targetIter(); !iter.done(); iter.next()) {
    JS::Rooted<JS::Value> key(cx, JS::NumberValue(iter.get().identifier()));
    JS::RootedVector<JS::Value> paths(cx);

    bool ok = shortestPaths.forEachPath(iter.get(), [&](JS::ubi::Path& path) {
      JS::RootedVector<JS::Value> pathValues(cx);

      for (JS::ubi::BackEdge* edge : path) {
        JS::Rooted<JSObject*> pathPart(cx, JS_NewPlainObject(cx));
        if (!pathPart) {
          return false;
        }

        JS::Rooted<JS::Value> predecessor(
            cx, NumberValue(edge->predecessor().identifier()));
        if (!JS_DefineProperty(cx, pathPart, "predecessor", predecessor,
                               JSPROP_ENUMERATE)) {
          return false;
        }

        JS::Rooted<JS::Value> edgeNameVal(cx, NullValue());
        if (edge->name()) {
          JS::Rooted<JSString*> edgeName(
              cx, JS_AtomizeUCString(cx, edge->name().get()));
          if (!edgeName) {
            return false;
          }
          edgeNameVal = StringValue(edgeName);
        }

        if (!JS_DefineProperty(cx, pathPart, "edge", edgeNameVal,
                               JSPROP_ENUMERATE)) {
          return false;
        }

        if (!pathValues.append(ObjectValue(*pathPart))) {
          return false;
        }
      }

      JS::Rooted<JSObject*> pathObj(cx, JS::NewArrayObject(cx, pathValues));
      return pathObj && paths.append(ObjectValue(*pathObj));
    });

    if (NS_WARN_IF(!ok)) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }

    JS::Rooted<JSObject*> pathsArray(cx, JS::NewArrayObject(cx, paths));
    if (NS_WARN_IF(!pathsArray)) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }

    JS::Rooted<JS::Value> pathsVal(cx, ObjectValue(*pathsArray));
    if (NS_WARN_IF(!JS::MapSet(cx, resultsMap, key, pathsVal))) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
  }

  results.set(resultsMap);
}

/*** Saving Heap Snapshots ****************************************************/

// If we are only taking a snapshot of the heap affected by the given set of
// globals, find the set of compartments the globals are allocated
// within. Returns false on OOM failure.
static bool PopulateCompartmentsWithGlobals(
    CompartmentSet& compartments, JS::HandleVector<JSObject*> globals) {
  unsigned length = globals.length();
  for (unsigned i = 0; i < length; i++) {
    if (!compartments.put(JS::GetCompartment(globals[i]))) return false;
  }

  return true;
}

// Add the given set of globals as explicit roots in the given roots
// list. Returns false on OOM failure.
static bool AddGlobalsAsRoots(JS::HandleVector<JSObject*> globals,
                              ubi::RootList& roots) {
  unsigned length = globals.length();
  for (unsigned i = 0; i < length; i++) {
    if (!roots.addRoot(ubi::Node(globals[i].get()), u"heap snapshot global")) {
      return false;
    }
  }
  return true;
}

// Choose roots and limits for a traversal, given `boundaries`. Set `roots` to
// the set of nodes within the boundaries that are referred to by nodes
// outside. If `boundaries` does not include all JS compartments, initialize
// `compartments` to the set of included compartments; otherwise, leave
// `compartments` uninitialized. (You can use compartments.initialized() to
// check.)
//
// If `boundaries` is incoherent, or we encounter an error while trying to
// handle it, or we run out of memory, set `rv` appropriately and return
// `false`.
//
// Return value is a pair of the status and an AutoCheckCannotGC token,
// forwarded from ubi::RootList::init(), to ensure that the caller does
// not GC while the RootList is live and initialized.
static std::pair<bool, AutoCheckCannotGC> EstablishBoundaries(
    JSContext* cx, ErrorResult& rv, const HeapSnapshotBoundaries& boundaries,
    ubi::RootList& roots, CompartmentSet& compartments) {
  MOZ_ASSERT(!roots.initialized());
  MOZ_ASSERT(compartments.empty());

  bool foundBoundaryProperty = false;

  if (boundaries.mRuntime.WasPassed()) {
    foundBoundaryProperty = true;

    if (!boundaries.mRuntime.Value()) {
      rv.Throw(NS_ERROR_INVALID_ARG);
      return {false, AutoCheckCannotGC(cx)};
    }

    auto [ok, nogc] = roots.init();
    if (!ok) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return {false, nogc};
    }
  }

  if (boundaries.mDebugger.WasPassed()) {
    if (foundBoundaryProperty) {
      rv.Throw(NS_ERROR_INVALID_ARG);
      return {false, AutoCheckCannotGC(cx)};
    }
    foundBoundaryProperty = true;

    JSObject* dbgObj = boundaries.mDebugger.Value();
    if (!dbgObj || !dbg::IsDebugger(*dbgObj)) {
      rv.Throw(NS_ERROR_INVALID_ARG);
      return {false, AutoCheckCannotGC(cx)};
    }

    JS::RootedVector<JSObject*> globals(cx);
    if (!dbg::GetDebuggeeGlobals(cx, *dbgObj, &globals) ||
        !PopulateCompartmentsWithGlobals(compartments, globals) ||
        !roots.init(compartments).first || !AddGlobalsAsRoots(globals, roots)) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return {false, AutoCheckCannotGC(cx)};
    }
  }

  if (boundaries.mGlobals.WasPassed()) {
    if (foundBoundaryProperty) {
      rv.Throw(NS_ERROR_INVALID_ARG);
      return {false, AutoCheckCannotGC(cx)};
    }
    foundBoundaryProperty = true;

    uint32_t length = boundaries.mGlobals.Value().Length();
    if (length == 0) {
      rv.Throw(NS_ERROR_INVALID_ARG);
      return {false, AutoCheckCannotGC(cx)};
    }

    JS::RootedVector<JSObject*> globals(cx);
    for (uint32_t i = 0; i < length; i++) {
      JSObject* global = boundaries.mGlobals.Value().ElementAt(i);
      if (!JS_IsGlobalObject(global)) {
        rv.Throw(NS_ERROR_INVALID_ARG);
        return {false, AutoCheckCannotGC(cx)};
      }
      if (!globals.append(global)) {
        rv.Throw(NS_ERROR_OUT_OF_MEMORY);
        return {false, AutoCheckCannotGC(cx)};
      }
    }

    if (!PopulateCompartmentsWithGlobals(compartments, globals) ||
        !roots.init(compartments).first || !AddGlobalsAsRoots(globals, roots)) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return {false, AutoCheckCannotGC(cx)};
    }
  }
  AutoCheckCannotGC nogc(cx);

  if (!foundBoundaryProperty) {
    rv.Throw(NS_ERROR_INVALID_ARG);
    return {false, nogc};
  }

  MOZ_ASSERT(roots.initialized());
  return {true, nogc};
}

// A variant covering all the various two-byte strings that we can get from the
// ubi::Node API.
class TwoByteString
    : public Variant<JSAtom*, const char16_t*, JS::ubi::EdgeName> {
  using Base = Variant<JSAtom*, const char16_t*, JS::ubi::EdgeName>;

  struct CopyToBufferMatcher {
    RangedPtr<char16_t> destination;
    size_t maxLength;

    CopyToBufferMatcher(RangedPtr<char16_t> destination, size_t maxLength)
        : destination(destination), maxLength(maxLength) {}

    size_t operator()(JS::ubi::EdgeName& ptr) {
      return ptr ? operator()(ptr.get()) : 0;
    }

    size_t operator()(JSAtom* atom) {
      MOZ_ASSERT(atom);
      JS::ubi::AtomOrTwoByteChars s(atom);
      return s.copyToBuffer(destination, maxLength);
    }

    size_t operator()(const char16_t* chars) {
      MOZ_ASSERT(chars);
      JS::ubi::AtomOrTwoByteChars s(chars);
      return s.copyToBuffer(destination, maxLength);
    }
  };

 public:
  // NOTE: The type requirement prevents this constructor to shadow default
  // copy/move constructor, see
  // https://clang.llvm.org/extra/clang-tidy/checks/bugprone/forwarding-reference-overload.html
  template <typename T>
    requires(std::is_constructible_v<Base, T>)
  MOZ_IMPLICIT TwoByteString(T&& rhs) : Base(std::forward<T>(rhs)) {}

  template <typename T>
  TwoByteString& operator=(T&& rhs) {
    MOZ_ASSERT(this != &rhs, "self-move disallowed");
    this->~TwoByteString();
    new (this) TwoByteString(std::forward<T>(rhs));
    return *this;
  }

  TwoByteString(TwoByteString&&) = default;
  TwoByteString& operator=(TwoByteString&&) = default;

  TwoByteString(const TwoByteString&) = delete;
  TwoByteString& operator=(const TwoByteString&) = delete;

  // Rewrap the inner value of a JS::ubi::AtomOrTwoByteChars as a TwoByteString.
  static TwoByteString from(JS::ubi::AtomOrTwoByteChars&& s) {
    return s.match([](auto* a) { return TwoByteString(a); });
  }

  // Returns true if the given TwoByteString is non-null, false otherwise.
  bool isNonNull() const {
    return match([](auto& t) { return t != nullptr; });
  }

  // Return the length of the string, 0 if it is null.
  size_t length() const {
    return match(
        [](JSAtom* atom) -> size_t {
          MOZ_ASSERT(atom);
          JS::ubi::AtomOrTwoByteChars s(atom);
          return s.length();
        },
        [](const char16_t* chars) -> size_t {
          MOZ_ASSERT(chars);
          return NS_strlen(chars);
        },
        [](const JS::ubi::EdgeName& ptr) -> size_t {
          MOZ_ASSERT(ptr);
          return NS_strlen(ptr.get());
        });
  }

  // Copy the contents of a TwoByteString into the provided buffer. The buffer
  // is NOT null terminated. The number of characters written is returned.
  size_t copyToBuffer(RangedPtr<char16_t> destination, size_t maxLength) {
    CopyToBufferMatcher m(destination, maxLength);
    return match(m);
  }

  struct HashPolicy;
};

// A hashing policy for TwoByteString.
//
// Atoms are pointer hashed and use pointer equality, which means that we
// tolerate some duplication across atoms and the other two types of two-byte
// strings. In practice, we expect the amount of this duplication to be very low
// because each type is generally a different semantic thing in addition to
// having a slightly different representation. For example, the set of edge
// names and the set stack frames' source names naturally tend not to overlap
// very much if at all.
struct TwoByteString::HashPolicy {
  using Lookup = TwoByteString;

  static js::HashNumber hash(const Lookup& l) {
    return l.match(
        [](const JSAtom* atom) {
          return js::DefaultHasher<const JSAtom*>::hash(atom);
        },
        [](const char16_t* chars) {
          MOZ_ASSERT(chars);
          auto length = NS_strlen(chars);
          return HashString(chars, length);
        },
        [](const JS::ubi::EdgeName& ptr) {
          const char16_t* chars = ptr.get();
          MOZ_ASSERT(chars);
          auto length = NS_strlen(chars);
          return HashString(chars, length);
        });
  }

  struct EqualityMatcher {
    const TwoByteString& rhs;
    explicit EqualityMatcher(const TwoByteString& rhs) : rhs(rhs) {}

    bool operator()(const JSAtom* atom) {
      return rhs.is<JSAtom*>() && rhs.as<JSAtom*>() == atom;
    }

    bool operator()(const char16_t* chars) {
      MOZ_ASSERT(chars);

      const char16_t* rhsChars = nullptr;
      if (rhs.is<const char16_t*>())
        rhsChars = rhs.as<const char16_t*>();
      else if (rhs.is<JS::ubi::EdgeName>())
        rhsChars = rhs.as<JS::ubi::EdgeName>().get();
      else
        return false;
      MOZ_ASSERT(rhsChars);

      auto length = NS_strlen(chars);
      if (NS_strlen(rhsChars) != length) return false;

      return memcmp(chars, rhsChars, length * sizeof(char16_t)) == 0;
    }

    bool operator()(const JS::ubi::EdgeName& ptr) {
      MOZ_ASSERT(ptr);
      return operator()(ptr.get());
    }
  };

  static bool match(const TwoByteString& k, const Lookup& l) {
    EqualityMatcher eq(l);
    return k.match(eq);
  }

  static void rekey(TwoByteString& k, TwoByteString&& newKey) {
    k = std::move(newKey);
  }
};

// Returns whether `edge` should be included in a heap snapshot of
// `compartments`. The optional `policy` out-param is set to INCLUDE_EDGES
// if we want to include the referent's edges, or EXCLUDE_EDGES if we don't
// want to include them.
static bool ShouldIncludeEdge(JS::CompartmentSet* compartments,
                              const ubi::Node& origin, const ubi::Edge& edge,
                              CoreDumpWriter::EdgePolicy* policy = nullptr) {
  if (policy) {
    *policy = CoreDumpWriter::INCLUDE_EDGES;
  }

  if (!compartments) {
    // We aren't targeting a particular set of compartments, so serialize all
    // the things!
    return true;
  }

  // We are targeting a particular set of compartments. If this node is in our
  // target set, serialize it and all of its edges. If this node is _not_ in our
  // target set, we also serialize under the assumption that it is a shared
  // resource being used by something in our target compartments since we
  // reached it by traversing the heap graph. However, we do not serialize its
  // outgoing edges and we abandon further traversal from this node.
  //
  // If the node does not belong to any compartment, we also serialize its
  // outgoing edges. This case is relevant for Shapes: they don't belong to a
  // specific compartment and contain edges to parent/kids Shapes we want to
  // include. Note that these Shapes may contain pointers into our target
  // compartment (the Shape's getter/setter JSObjects). However, we do not
  // serialize nodes in other compartments that are reachable from these
  // non-compartment nodes.

  JS::Compartment* compartment = edge.referent.compartment();

  if (!compartment || compartments->has(compartment)) {
    return true;
  }

  if (policy) {
    *policy = CoreDumpWriter::EXCLUDE_EDGES;
  }

  return !!origin.compartment();
}

// A `CoreDumpWriter` that serializes nodes to protobufs and writes them to the
// given `ZeroCopyOutputStream`.
class MOZ_STACK_CLASS StreamWriter : public CoreDumpWriter {
  using FrameSet = js::HashSet<uint64_t>;
  using TwoByteStringMap =
      js::HashMap<TwoByteString, uint64_t, TwoByteString::HashPolicy>;
  using OneByteStringMap = js::HashMap<const char*, uint64_t>;

  JSContext* cx;
  bool wantNames;
  // The set of |JS::ubi::StackFrame::identifier()|s that have already been
  // serialized and written to the core dump.
  FrameSet framesAlreadySerialized;
  // The set of two-byte strings that have already been serialized and written
  // to the core dump.
  TwoByteStringMap twoByteStringsAlreadySerialized;
  // The set of one-byte strings that have already been serialized and written
  // to the core dump.
  OneByteStringMap oneByteStringsAlreadySerialized;

  ::google::protobuf::io::ZeroCopyOutputStream& stream;

  JS::CompartmentSet* compartments;

  bool writeMessage(const ::google::protobuf::MessageLite& message) {
    // We have to create a new CodedOutputStream when writing each message so
    // that the 64MB size limit used by Coded{Output,Input}Stream to prevent
    // integer overflow is enforced per message rather than on the whole stream.
    ::google::protobuf::io::CodedOutputStream codedStream(&stream);
    codedStream.WriteVarint32(message.ByteSizeLong());
    message.SerializeWithCachedSizes(&codedStream);
    return !codedStream.HadError();
  }

  // Attach the full two-byte string or a reference to a two-byte string that
  // has already been serialized to a protobuf message.
  template <typename SetStringFunction, typename SetRefFunction>
  bool attachTwoByteString(TwoByteString& string, SetStringFunction setString,
                           SetRefFunction setRef) {
    auto ptr = twoByteStringsAlreadySerialized.lookupForAdd(string);
    if (ptr) {
      setRef(ptr->value());
      return true;
    }

    auto length = string.length();
    auto stringData = MakeUnique<std::string>(length * sizeof(char16_t), '\0');
    if (!stringData) return false;

    auto buf = const_cast<char16_t*>(
        reinterpret_cast<const char16_t*>(stringData->data()));
    string.copyToBuffer(RangedPtr<char16_t>(buf, length), length);

    uint64_t ref = twoByteStringsAlreadySerialized.count();
    if (!twoByteStringsAlreadySerialized.add(ptr, std::move(string), ref))
      return false;

    setString(stringData.release());
    return true;
  }

  // Attach the full one-byte string or a reference to a one-byte string that
  // has already been serialized to a protobuf message.
  template <typename SetStringFunction, typename SetRefFunction>
  bool attachOneByteString(const char* string, SetStringFunction setString,
                           SetRefFunction setRef) {
    auto ptr = oneByteStringsAlreadySerialized.lookupForAdd(string);
    if (ptr) {
      setRef(ptr->value());
      return true;
    }

    auto length = strlen(string);
    auto stringData = MakeUnique<std::string>(string, length);
    if (!stringData) return false;

    uint64_t ref = oneByteStringsAlreadySerialized.count();
    if (!oneByteStringsAlreadySerialized.add(ptr, string, ref)) return false;

    setString(stringData.release());
    return true;
  }

  protobuf::StackFrame* getProtobufStackFrame(JS::ubi::StackFrame& frame,
                                              size_t depth = 1) {
    // NB: de-duplicated string properties must be written in the same order
    // here as they are read in `HeapSnapshot::saveStackFrame` or else indices
    // in references to already serialized strings will be off.

    MOZ_ASSERT(frame,
               "null frames should be represented as the lack of a serialized "
               "stack frame");

    auto id = frame.identifier();
    auto protobufStackFrame = MakeUnique<protobuf::StackFrame>();
    if (!protobufStackFrame) return nullptr;

    if (framesAlreadySerialized.has(id)) {
      protobufStackFrame->set_ref(id);
      return protobufStackFrame.release();
    }

    auto data = MakeUnique<protobuf::StackFrame_Data>();
    if (!data) return nullptr;

    data->set_id(id);
    data->set_line(frame.line());
    data->set_column(frame.column().oneOriginValue());
    data->set_issystem(frame.isSystem());
    data->set_isselfhosted(frame.isSelfHosted(cx));

    auto dupeSource = TwoByteString::from(frame.source());
    if (!attachTwoByteString(
            dupeSource,
            [&](std::string* source) { data->set_allocated_source(source); },
            [&](uint64_t ref) { data->set_sourceref(ref); })) {
      return nullptr;
    }

    auto dupeName = TwoByteString::from(frame.functionDisplayName());
    if (dupeName.isNonNull()) {
      if (!attachTwoByteString(
              dupeName,
              [&](std::string* name) {
                data->set_allocated_functiondisplayname(name);
              },
              [&](uint64_t ref) { data->set_functiondisplaynameref(ref); })) {
        return nullptr;
      }
    }

    auto parent = frame.parent();
    if (parent && depth < HeapSnapshot::MAX_STACK_DEPTH) {
      auto protobufParent = getProtobufStackFrame(parent, depth + 1);
      if (!protobufParent) return nullptr;
      data->set_allocated_parent(protobufParent);
    }

    protobufStackFrame->set_allocated_data(data.release());

    if (!framesAlreadySerialized.put(id)) return nullptr;

    return protobufStackFrame.release();
  }

 public:
  StreamWriter(JSContext* cx,
               ::google::protobuf::io::ZeroCopyOutputStream& stream,
               bool wantNames, JS::CompartmentSet* compartments)
      : cx(cx),
        wantNames(wantNames),
        framesAlreadySerialized(cx),
        twoByteStringsAlreadySerialized(cx),
        oneByteStringsAlreadySerialized(cx),
        stream(stream),
        compartments(compartments) {}

  ~StreamWriter() override = default;

  bool writeMetadata(uint64_t timestamp) final {
    protobuf::Metadata metadata;
    metadata.set_timestamp(timestamp);
    return writeMessage(metadata);
  }

  bool writeNode(const JS::ubi::Node& ubiNode, EdgePolicy includeEdges) final {
    // NB: de-duplicated string properties must be written in the same order
    // here as they are read in `HeapSnapshot::saveNode` or else indices in
    // references to already serialized strings will be off.

    protobuf::Node protobufNode;
    protobufNode.set_id(ubiNode.identifier());

    protobufNode.set_coarsetype(
        JS::ubi::CoarseTypeToUint32(ubiNode.coarseType()));

    auto typeName = TwoByteString(ubiNode.typeName());
    if (NS_WARN_IF(!attachTwoByteString(
            typeName,
            [&](std::string* name) {
              protobufNode.set_allocated_typename_(name);
            },
            [&](uint64_t ref) { protobufNode.set_typenameref(ref); }))) {
      return false;
    }

    mozilla::MallocSizeOf mallocSizeOf = dbg::GetDebuggerMallocSizeOf(cx);
    MOZ_ASSERT(mallocSizeOf);
    protobufNode.set_size(ubiNode.size(mallocSizeOf));

    if (includeEdges) {
      auto edges = ubiNode.edges(cx, wantNames);
      if (NS_WARN_IF(!edges)) return false;

      for (; !edges->empty(); edges->popFront()) {
        ubi::Edge& ubiEdge = edges->front();
        if (!ShouldIncludeEdge(compartments, ubiNode, ubiEdge)) {
          continue;
        }

        protobuf::Edge* protobufEdge = protobufNode.add_edges();
        if (NS_WARN_IF(!protobufEdge)) {
          return false;
        }

        protobufEdge->set_referent(ubiEdge.referent.identifier());

        if (wantNames && ubiEdge.name) {
          TwoByteString edgeName(std::move(ubiEdge.name));
          if (NS_WARN_IF(!attachTwoByteString(
                  edgeName,
                  [&](std::string* name) {
                    protobufEdge->set_allocated_name(name);
                  },
                  [&](uint64_t ref) { protobufEdge->set_nameref(ref); }))) {
            return false;
          }
        }
      }
    }

    if (ubiNode.hasAllocationStack()) {
      auto ubiStackFrame = ubiNode.allocationStack();
      auto protoStackFrame = getProtobufStackFrame(ubiStackFrame);
      if (NS_WARN_IF(!protoStackFrame)) return false;
      protobufNode.set_allocated_allocationstack(protoStackFrame);
    }

    if (auto className = ubiNode.jsObjectClassName()) {
      if (NS_WARN_IF(!attachOneByteString(
              className,
              [&](std::string* name) {
                protobufNode.set_allocated_jsobjectclassname(name);
              },
              [&](uint64_t ref) {
                protobufNode.set_jsobjectclassnameref(ref);
              }))) {
        return false;
      }
    }

    if (auto scriptFilename = ubiNode.scriptFilename()) {
      if (NS_WARN_IF(!attachOneByteString(
              scriptFilename,
              [&](std::string* name) {
                protobufNode.set_allocated_scriptfilename(name);
              },
              [&](uint64_t ref) {
                protobufNode.set_scriptfilenameref(ref);
              }))) {
        return false;
      }
    }

    if (ubiNode.descriptiveTypeName()) {
      auto descriptiveTypeName = TwoByteString(ubiNode.descriptiveTypeName());
      if (NS_WARN_IF(!attachTwoByteString(
              descriptiveTypeName,
              [&](std::string* name) {
                protobufNode.set_allocated_descriptivetypename(name);
              },
              [&](uint64_t ref) {
                protobufNode.set_descriptivetypenameref(ref);
              }))) {
        return false;
      }
    }

    return writeMessage(protobufNode);
  }
};

// A JS::ubi::BreadthFirst handler that serializes a snapshot of the heap into a
// core dump.
class MOZ_STACK_CLASS HeapSnapshotHandler {
  CoreDumpWriter& writer;
  JS::CompartmentSet* compartments;

 public:
  // For telemetry.
  uint32_t nodeCount;
  uint32_t edgeCount;

  HeapSnapshotHandler(CoreDumpWriter& writer, JS::CompartmentSet* compartments)
      : writer(writer),
        compartments(compartments),
        nodeCount(0),
        edgeCount(0) {}

  // JS::ubi::BreadthFirst handler interface.

  class NodeData {};
  typedef JS::ubi::BreadthFirst<HeapSnapshotHandler> Traversal;
  bool operator()(Traversal& traversal, JS::ubi::Node origin,
                  const JS::ubi::Edge& edge, NodeData*, bool first) {
    edgeCount++;

    // We're only interested in the first time we reach edge.referent, not in
    // every edge arriving at that node. "But, don't we want to serialize every
    // edge in the heap graph?" you ask. Don't worry! This edge is still
    // serialized into the core dump. Serializing a node also serializes each of
    // its edges, and if we are traversing a given edge, we must have already
    // visited and serialized the origin node and its edges.
    if (!first) return true;

    CoreDumpWriter::EdgePolicy policy;
    if (!ShouldIncludeEdge(compartments, origin, edge, &policy)) {
      // Because ShouldIncludeEdge considers the |origin| node as well, we don't
      // want to consider this node 'visited' until we write it to the core
      // dump.
      traversal.doNotMarkReferentAsVisited();
      return true;
    }

    nodeCount++;

    if (policy == CoreDumpWriter::EXCLUDE_EDGES) traversal.abandonReferent();

    return writer.writeNode(edge.referent, policy);
  }
};

bool WriteHeapGraph(JSContext* cx, const JS::ubi::Node& node,
                    CoreDumpWriter& writer, bool wantNames,
                    JS::CompartmentSet* compartments,
                    JS::AutoCheckCannotGC& noGC, uint32_t& outNodeCount,
                    uint32_t& outEdgeCount) {
  // Serialize the starting node to the core dump.

  if (NS_WARN_IF(!writer.writeNode(node, CoreDumpWriter::INCLUDE_EDGES))) {
    return false;
  }

  // Walk the heap graph starting from the given node and serialize it into the
  // core dump.

  HeapSnapshotHandler handler(writer, compartments);
  HeapSnapshotHandler::Traversal traversal(cx, handler, noGC);
  traversal.wantNames = wantNames;

  bool ok = traversal.addStartVisited(node) && traversal.traverse();

  if (ok) {
    outNodeCount = handler.nodeCount;
    outEdgeCount = handler.edgeCount;
  }

  return ok;
}

static unsigned long msSinceProcessCreation(const TimeStamp& now) {
  auto duration = now - TimeStamp::ProcessCreation();
  return (unsigned long)duration.ToMilliseconds();
}

/* static */
already_AddRefed<nsIFile> HeapSnapshot::CreateUniqueCoreDumpFile(
    ErrorResult& rv, const TimeStamp& now, nsAString& outFilePath,
    nsAString& outSnapshotId) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  nsCOMPtr<nsIFile> file;
  rv = GetSpecialSystemDirectory(OS_TemporaryDirectory, getter_AddRefs(file));
  if (NS_WARN_IF(rv.Failed())) return nullptr;

  nsAutoString tempPath;
  rv = file->GetPath(tempPath);
  if (NS_WARN_IF(rv.Failed())) return nullptr;

  auto ms = msSinceProcessCreation(now);
  rv = file->AppendNative(nsPrintfCString("%lu.fxsnapshot", ms));
  if (NS_WARN_IF(rv.Failed())) return nullptr;

  rv = file->CreateUnique(nsIFile::NORMAL_FILE_TYPE, 0666);
  if (NS_WARN_IF(rv.Failed())) return nullptr;

  rv = file->GetPath(outFilePath);
  if (NS_WARN_IF(rv.Failed())) return nullptr;

  // The snapshot ID must be computed in the process that created the
  // temp file, because TmpD may not be the same in all processes.
  outSnapshotId.Assign(Substring(
      outFilePath, tempPath.Length() + 1,
      outFilePath.Length() - tempPath.Length() - sizeof(".fxsnapshot")));

  return file.forget();
}

// Deletion policy for cleaning up PHeapSnapshotTempFileHelperChild pointers.
class DeleteHeapSnapshotTempFileHelperChild {
 public:
  constexpr DeleteHeapSnapshotTempFileHelperChild() = default;

  void operator()(PHeapSnapshotTempFileHelperChild* ptr) const {
    (void)NS_WARN_IF(!HeapSnapshotTempFileHelperChild::Send__delete__(ptr));
  }
};

// A UniquePtr alias to automatically manage PHeapSnapshotTempFileHelperChild
// pointers.
using UniqueHeapSnapshotTempFileHelperChild =
    UniquePtr<PHeapSnapshotTempFileHelperChild,
              DeleteHeapSnapshotTempFileHelperChild>;

// Get an nsIOutputStream that we can write the heap snapshot to. In non-e10s
// and in the e10s parent process, open a file directly and create an output
// stream for it. In e10s child processes, we are sandboxed without access to
// the filesystem. Use IPDL to request a file descriptor from the parent
// process.
static already_AddRefed<nsIOutputStream> getCoreDumpOutputStream(
    ErrorResult& rv, TimeStamp& start, nsAString& outFilePath,
    nsAString& outSnapshotId) {
  if (XRE_IsParentProcess()) {
    // Create the file and open the output stream directly.

    nsCOMPtr<nsIFile> file = HeapSnapshot::CreateUniqueCoreDumpFile(
        rv, start, outFilePath, outSnapshotId);
    if (NS_WARN_IF(rv.Failed())) return nullptr;

    nsCOMPtr<nsIOutputStream> outputStream;
    rv = NS_NewLocalFileOutputStream(getter_AddRefs(outputStream), file,
                                     PR_WRONLY, -1, 0);
    if (NS_WARN_IF(rv.Failed())) return nullptr;

    return outputStream.forget();
  }
  // Request a file descriptor from the parent process over IPDL.

  auto cc = ContentChild::GetSingleton();
  if (!cc) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  UniqueHeapSnapshotTempFileHelperChild helper(
      cc->SendPHeapSnapshotTempFileHelperConstructor());
  if (NS_WARN_IF(!helper)) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  OpenHeapSnapshotTempFileResponse response;
  if (!helper->SendOpenHeapSnapshotTempFile(&response)) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }
  if (response.type() == OpenHeapSnapshotTempFileResponse::Tnsresult) {
    rv.Throw(response.get_nsresult());
    return nullptr;
  }

  auto opened = response.get_OpenedFile();
  outFilePath = opened.path();
  outSnapshotId = opened.snapshotId();
  nsCOMPtr<nsIOutputStream> outputStream =
      FileDescriptorOutputStream::Create(opened.descriptor());
  if (NS_WARN_IF(!outputStream)) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  return outputStream.forget();
}

}  // namespace devtools

namespace dom {

using namespace JS;
using namespace devtools;

/* static */
void ChromeUtils::SaveHeapSnapshotShared(
    GlobalObject& global, const HeapSnapshotBoundaries& boundaries,
    nsAString& outFilePath, nsAString& outSnapshotId, ErrorResult& rv) {
  auto start = TimeStamp::Now();

  bool wantNames = true;
  CompartmentSet compartments;
  uint32_t nodeCount = 0;
  uint32_t edgeCount = 0;

  nsCOMPtr<nsIOutputStream> outputStream =
      getCoreDumpOutputStream(rv, start, outFilePath, outSnapshotId);
  if (NS_WARN_IF(rv.Failed())) return;

  ZeroCopyNSIOutputStream zeroCopyStream(outputStream);
  ::google::protobuf::io::GzipOutputStream gzipStream(&zeroCopyStream);

  JSContext* cx = global.Context();

  {
    ubi::RootList rootList(cx, wantNames);
    auto [ok, nogc] =
        EstablishBoundaries(cx, rv, boundaries, rootList, compartments);
    if (!ok) {
      return;
    }

    StreamWriter writer(cx, gzipStream, wantNames,
                        !compartments.empty() ? &compartments : nullptr);

    ubi::Node roots(&rootList);

    // Serialize the initial heap snapshot metadata to the core dump.
    if (!writer.writeMetadata(PR_Now()) ||
        // Serialize the heap graph to the core dump, starting from our list of
        // roots.
        !WriteHeapGraph(cx, roots, writer, wantNames,
                        !compartments.empty() ? &compartments : nullptr, nogc,
                        nodeCount, edgeCount)) {
      rv.Throw(zeroCopyStream.failed() ? zeroCopyStream.result()
                                       : NS_ERROR_UNEXPECTED);
      return;
    }
  }

  glean::devtools::save_heap_snapshot.AccumulateRawDuration(TimeStamp::Now() -
                                                            start);
  glean::devtools::heap_snapshot_node_count.AccumulateSingleSample(nodeCount);
  glean::devtools::heap_snapshot_edge_count.AccumulateSingleSample(edgeCount);
}

/* static */
uint64_t ChromeUtils::GetObjectNodeId(GlobalObject& global,
                                      JS::Handle<JSObject*> val) {
  JS::Rooted<JSObject*> obj(global.Context(), val);

  JS::ubi::Node node(obj);
  return node.identifier();
}

/* static */
void ChromeUtils::SaveHeapSnapshot(GlobalObject& global,
                                   const HeapSnapshotBoundaries& boundaries,
                                   nsAString& outFilePath, ErrorResult& rv) {
  nsAutoString snapshotId;
  SaveHeapSnapshotShared(global, boundaries, outFilePath, snapshotId, rv);
}

/* static */
void ChromeUtils::SaveHeapSnapshotGetId(
    GlobalObject& global, const HeapSnapshotBoundaries& boundaries,
    nsAString& outSnapshotId, ErrorResult& rv) {
  nsAutoString filePath;
  SaveHeapSnapshotShared(global, boundaries, filePath, outSnapshotId, rv);
}

/* static */
already_AddRefed<HeapSnapshot> ChromeUtils::ReadHeapSnapshot(
    GlobalObject& global, const nsAString& filePath, ErrorResult& rv) {
  auto start = TimeStamp::Now();

  nsCOMPtr<nsIFile> snapshotFile;
  rv = NS_NewLocalFile(filePath, getter_AddRefs(snapshotFile));
  if (rv.Failed()) {
    return nullptr;
  }

  AutoMemMap mm;
  rv = mm.init(snapshotFile);
  if (rv.Failed()) return nullptr;

  RefPtr<HeapSnapshot> snapshot = HeapSnapshot::Create(
      global.Context(), global, reinterpret_cast<const uint8_t*>(mm.address()),
      mm.size(), rv);

  if (!rv.Failed())
    glean::devtools::read_heap_snapshot.AccumulateRawDuration(TimeStamp::Now() -
                                                              start);

  return snapshot.forget();
}

}  // namespace dom
}  // namespace mozilla
