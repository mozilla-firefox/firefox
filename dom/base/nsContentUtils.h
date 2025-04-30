/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* A namespace class for static content utilities. */

#ifndef nsContentUtils_h___
#define nsContentUtils_h___

#if defined(XP_WIN)
#  include <float.h>
#endif

#if defined(SOLARIS)
#  include <ieeefp.h>
#endif

#include <cstddef>
#include <cstdint>
#include <functional>
#include <tuple>
#include <utility>
#include "ErrorList.h"
#include "Units.h"
#include "js/Id.h"
#include "js/RegExpFlags.h"
#include "js/RootingAPI.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/FunctionRef.h"
#include "mozilla/SourceLocation.h"
#include "mozilla/CORSMode.h"
#include "mozilla/CallState.h"
#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/CacheExpirationTime.h"
#include "mozilla/dom/FromParser.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/fallible.h"
#include "mozilla/gfx/Point.h"
#include "nsCOMPtr.h"
#include "nsIContentPolicy.h"
#include "nsINode.h"
#include "nsIScriptError.h"
#include "nsIThread.h"
#include "nsLiteralString.h"
#include "nsMargin.h"
#include "nsPIDOMWindow.h"
#include "nsRFPService.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsTLiteralString.h"
#include "prtime.h"

#if defined(XP_WIN)
// Undefine LoadImage to prevent naming conflict with Windows.
#  undef LoadImage
#endif

class JSObject;
class imgICache;
class imgIContainer;
class imgINotificationObserver;
class imgIRequest;
class imgLoader;
class imgRequestProxy;
class nsAtom;
class nsAttrValue;
class nsAutoScriptBlockerSuppressNodeRemoved;
class nsContentList;
class nsCycleCollectionTraversalCallback;
class nsDocShell;
class nsGlobalWindowInner;
class nsHtml5StringParser;
class nsIArray;
class nsIBidiKeyboard;
class nsIChannel;
class nsIConsoleService;
class nsIContent;
class nsIDocShell;
class nsIDocShellTreeItem;
class nsIDocumentLoaderFactory;
class nsIDragSession;
class nsIFile;
class nsIFragmentContentSink;
class nsIFrame;
class nsIHttpChannel;
class nsIIOService;
class nsIImageLoadingContent;
class nsIInterfaceRequestor;
class nsILoadGroup;
class nsILoadInfo;
class nsIObserver;
class nsIPrincipal;
class nsIReferrerInfo;
class nsIRequest;
class nsIRunnable;
class nsIScreen;
class nsIScriptContext;
class nsIScriptSecurityManager;
class nsISerialEventTarget;
class nsIStringBundle;
class nsIStringBundleService;
class nsISupports;
class nsITransferable;
class nsIURI;
class nsIWidget;
class nsIWritableVariant;
class nsIXPConnect;
class nsNodeInfoManager;
class nsParser;
class nsPIWindowRoot;
class nsPresContext;
class nsTextFragment;
class nsView;
class nsWrapperCache;

struct JSContext;
struct nsPoint;

namespace IPC {
class Message;
class MessageReader;
class MessageWriter;
}  // namespace IPC

namespace JS {
class Value;
class PropertyDescriptor;
}  // namespace JS

namespace mozilla {
class Dispatcher;
class EditorBase;
class ErrorResult;
class EventListenerManager;
class HTMLEditor;
class LazyLogModule;
class LogModule;
class PresShell;
class StringBuffer;
class TextEditor;
class WidgetDragEvent;
class WidgetKeyboardEvent;

struct InputEventOptions;

template <typename ParentType, typename RefType>
class RangeBoundaryBase;

template <typename T>
class NotNull;
template <class T>
class StaticRefPtr;

namespace dom {
class IPCImage;
struct AutocompleteInfo;
class BrowserChild;
class BrowserParent;
class BrowsingContext;
class BrowsingContextGroup;
class ContentChild;
class ContentFrameMessageManager;
class ContentParent;
struct CustomElementDefinition;
class CustomElementFormValue;
class CustomElementRegistry;
class DataTransfer;
class Document;
class DocumentFragment;
class DOMArena;
class Element;
class Event;
class EventTarget;
class FragmentOrElement;
class HTMLElement;
class HTMLInputElement;
class IPCTransferable;
class IPCTransferableData;
class IPCTransferableDataImageContainer;
class IPCTransferableDataItem;
struct LifecycleCallbackArgs;
class MessageBroadcaster;
class NodeInfo;
class OwningFileOrUSVStringOrFormData;
class Selection;
enum class ShadowRootMode : uint8_t;
class ShadowRoot;
struct StructuredSerializeOptions;
class TrustedHTMLOrString;
class WorkerPrivate;
enum class ElementCallbackType;
enum class ReferrerPolicy : uint8_t;
}  // namespace dom

namespace ipc {
class BigBuffer;
class IProtocol;
}  // namespace ipc

namespace gfx {
class DataSourceSurface;
enum class SurfaceFormat : int8_t;
}  // namespace gfx

class WindowRenderer;

}  // namespace mozilla

extern const char kLoadAsData[];

// Stolen from nsReadableUtils, but that's OK, since we can declare the same
// name multiple times.
const nsString& EmptyString();
const nsCString& EmptyCString();

enum EventNameType {
  EventNameType_None = 0x0000,
  EventNameType_HTML = 0x0001,
  EventNameType_XUL = 0x0002,
  EventNameType_SVGGraphic = 0x0004,  // svg graphic elements
  EventNameType_SVGSVG = 0x0008,      // the svg element
  EventNameType_SMIL = 0x0010,        // smil elements
  EventNameType_HTMLBodyOrFramesetOnly = 0x0020,
  EventNameType_HTMLMedia = 0x0040,

  EventNameType_HTMLXUL = 0x0003,
  EventNameType_All = 0xFFFF
};

enum class TreeKind : uint8_t { DOM, ShadowIncludingDOM, Flat };

enum class SerializeShadowRoots : uint8_t { Yes, No };

struct EventNameMapping {
  // This holds pointers to nsGkAtoms members, and is therefore safe as a
  // non-owning reference.
  nsAtom* MOZ_NON_OWNING_REF mAtom;
  int32_t mType;
  mozilla::EventMessage mMessage;
  mozilla::EventClassID mEventClassID;
};

namespace mozilla::dom {
enum JSONBehavior { UndefinedIsNullStringLiteral, UndefinedIsVoidString };
}  // namespace mozilla::dom

class nsContentUtils {
  friend class nsAutoScriptBlockerSuppressNodeRemoved;
  using Element = mozilla::dom::Element;
  using Document = mozilla::dom::Document;
  using Cancelable = mozilla::Cancelable;
  using CanBubble = mozilla::CanBubble;
  using Composed = mozilla::Composed;
  using ChromeOnlyDispatch = mozilla::ChromeOnlyDispatch;
  using EventMessage = mozilla::EventMessage;
  using TimeDuration = mozilla::TimeDuration;
  using Trusted = mozilla::Trusted;
  using JSONBehavior = mozilla::dom::JSONBehavior;
  using RFPTarget = mozilla::RFPTarget;
  using SystemGroupOnly = mozilla::SystemGroupOnly;

 public:
  static nsresult Init();

  static bool IsCallerChrome();
  static bool ThreadsafeIsCallerChrome();
  static bool IsCallerUAWidget();
  static bool IsFuzzingEnabled()
#ifndef FUZZING
  {
    return false;
  }
#else
      ;
#endif
  static bool IsErrorPage(nsIURI* aURI);

  static bool IsCallerChromeOrFuzzingEnabled(JSContext* aCx, JSObject*) {
    return ThreadsafeIsSystemCaller(aCx) || IsFuzzingEnabled();
  }

  static bool IsCallerChromeOrElementTransformGettersEnabled(JSContext* aCx,
                                                             JSObject*);

  // The APIs for checking whether the caller is system (in the sense of system
  // principal) should only be used when the JSContext is known to accurately
  // represent the caller.  In practice, that means you should only use them in
  // two situations at the moment:
  //
  // 1) Functions used in WebIDL Func annotations.
  // 2) Bindings code or other code called directly from the JS engine.
  //
  // Use pretty much anywhere else is almost certainly wrong and should be
  // replaced with [NeedsCallerType] annotations in bindings.

  // Check whether the caller is system if you know you're on the main thread.
  static bool IsSystemCaller(JSContext* aCx);

  // Check whether the caller is system if you might be on a worker or worklet
  // thread.
  static bool ThreadsafeIsSystemCaller(JSContext* aCx);

  // In the traditional Gecko architecture, both C++ code and untrusted JS code
  // needed to rely on the same XPCOM method/getter/setter to get work done.
  // This required lots of security checks in the various exposed methods, which
  // in turn created difficulty in determining whether the caller was script
  // (whose access needed to be checked) and internal C++ platform code (whose
  // access did not need to be checked). To address this problem, Gecko had a
  // convention whereby the absence of script on the stack was interpretted as
  // "System Caller" and always granted unfettered access.
  //
  // Unfortunately, this created a bunch of footguns. For example, when the
  // implementation of a DOM method wanted to perform a privileged
  // sub-operation, it needed to "hide" the presence of script on the stack in
  // order for that sub-operation to be allowed. Additionally, if script could
  // trigger an API entry point to be invoked in some asynchronous way without
  // script on the stack, it could potentially perform privilege escalation.
  //
  // In the modern world, untrusted script should interact with the platform
  // exclusively over WebIDL APIs, and platform code has a lot more flexibility
  // in deciding whether or not to use XPCOM. This gives us the flexibility to
  // do something better.
  //
  // Going forward, APIs should be designed such that any security checks that
  // ask the question "is my caller allowed to do this?" should live in WebIDL
  // API entry points, with a separate method provided for internal callers
  // that just want to get the job done.
  //
  // To enforce this and catch bugs, nsContentUtils::SubjectPrincipal will crash
  // if it is invoked without script on the stack. To land that transition, it
  // was necessary to go through and whitelist a bunch of callers that were
  // depending on the old behavior. Those callers should be fixed up, and these
  // methods should not be used by new code without review from bholley or bz.
  static bool LegacyIsCallerNativeCode() { return !GetCurrentJSContext(); }
  static bool LegacyIsCallerChromeOrNativeCode() {
    return LegacyIsCallerNativeCode() || IsCallerChrome();
  }
  static nsIPrincipal* SubjectPrincipalOrSystemIfNativeCaller() {
    if (!GetCurrentJSContext()) {
      return GetSystemPrincipal();
    }
    return SubjectPrincipal();
  }

  static bool LookupBindingMember(
      JSContext* aCx, nsIContent* aContent, JS::Handle<jsid> aId,
      JS::MutableHandle<JS::PropertyDescriptor> aDesc);

  // Check whether we should avoid leaking distinguishing information to JS/CSS.
  // This function can be called both in the main thread and worker threads.
  static bool ShouldResistFingerprinting(bool aIsPrivateMode,
                                         RFPTarget aTarget);
  static bool ShouldResistFingerprinting(nsIGlobalObject* aGlobalObject,
                                         RFPTarget aTarget);
  // Similar to the function above, but always allows CallerType::System
  // callers.
  static bool ShouldResistFingerprinting(mozilla::dom::CallerType aCallerType,
                                         nsIGlobalObject* aGlobalObject,
                                         RFPTarget aTarget);
  static bool ShouldResistFingerprinting(nsIDocShell* aDocShell,
                                         RFPTarget aTarget);
  // These functions are the new, nuanced functions
  static bool ShouldResistFingerprinting(nsIChannel* aChannel,
                                         RFPTarget aTarget);
  // These functions are labeled as dangerous because they will do the wrong
  // thing in _most_ cases. They should only be used if you don't have a fully
  // constructed LoadInfo or Document.
  // A constant string used as justification is required when calling them,
  // it should explain why a Document, Channel, LoadInfo, or CookieJarSettings
  // does not exist in this context.
  // (see below for more on justification strings.)
  static bool ShouldResistFingerprinting_dangerous(
      nsIURI* aURI, const mozilla::OriginAttributes& aOriginAttributes,
      const char* aJustification, RFPTarget aTarget);
  static bool ShouldResistFingerprinting_dangerous(nsIPrincipal* aPrincipal,
                                                   const char* aJustification,
                                                   RFPTarget aTarget);

  /**
   * Implement a RFP function that only checks the pref, and does not take
   * into account any additional context such as PBM mode or Web Extensions.
   *
   * It requires an explanation for why the coarse check is being used instead
   * of the nuanced check. While there is a gradual cut over of
   * ShouldResistFingerprinting calls to a nuanced API, some features still
   * require a legacy function. (Additionally, we sometimes use the coarse
   * check first, to avoid running additional code to support a nuanced check.)
   */
  static bool ShouldResistFingerprinting(const char* aJustification,
                                         RFPTarget aTarget);

  static bool ETPSaysShouldNotResistFingerprinting(
      nsICookieJarSettings* aCookieJarSettings, bool aIsPBM);

  static bool ETPSaysShouldNotResistFingerprinting(nsIChannel* aChannel,
                                                   nsILoadInfo* aLoadInfo);

  // A helper function to calculate the rounded window size for fingerprinting
  // resistance. The rounded size is based on the chrome UI size and available
  // screen size. If the inputWidth/Height is greater than the available content
  // size, this will report the available content size. Otherwise, it will
  // round the size to the nearest upper 200x100.
  static void CalcRoundedWindowSizeForResistingFingerprinting(
      int32_t aChromeWidth, int32_t aChromeHeight, int32_t aScreenWidth,
      int32_t aScreenHeight, int32_t aInputWidth, int32_t aInputHeight,
      bool aSetOuterWidth, bool aSetOuterHeight, int32_t* aOutputWidth,
      int32_t* aOutputHeight);

  /**
   * Returns the parent node of aChild crossing document boundaries, but skips
   * any cross-process parent frames and continues with the nearest in-process
   * frame in the hierarchy.
   *
   * Uses the parent node in the composed document.
   */
  static nsINode* GetNearestInProcessCrossDocParentNode(nsINode* aChild);

  /**
   * Similar to nsINode::IsInclusiveDescendantOf, except will treat an
   * HTMLTemplateElement or ShadowRoot as an ancestor of things in the
   * corresponding DocumentFragment. See the concept of "host-including
   * inclusive ancestor" in the DOM specification.
   */
  static bool ContentIsHostIncludingDescendantOf(
      const nsINode* aPossibleDescendant, const nsINode* aPossibleAncestor);

  /**
   * Similar to nsINode::IsInclusiveDescendantOf except it crosses document
   * boundaries, this function uses ancestor/descendant relations in the
   * composed document (see shadow DOM spec).
   */
  static bool ContentIsCrossDocDescendantOf(nsINode* aPossibleDescendant,
                                            nsINode* aPossibleAncestor);

  /**
   * As with ContentIsCrossDocDescendantOf but crosses shadow boundaries but not
   * cross document boundaries.
   *
   * @see nsINode::GetFlattenedTreeParentNode()
   */
  static bool ContentIsFlattenedTreeDescendantOf(
      const nsINode* aPossibleDescendant, const nsINode* aPossibleAncestor);

  /**
   * Same as `ContentIsFlattenedTreeDescendantOf`, but from the flattened tree
   * point of view of the style system
   *
   * @see nsINode::GetFlattenedTreeParentNodeForStyle()
   */
  static bool ContentIsFlattenedTreeDescendantOfForStyle(
      const nsINode* aPossibleDescendant, const nsINode* aPossibleAncestor);

  /**
   * Retarget an object A against an object B
   * @see https://dom.spec.whatwg.org/#retarget
   */
  static nsINode* Retarget(nsINode* aTargetA, nsINode* aTargetB);

  /**
   * @see https://wicg.github.io/element-timing/#get-an-element
   */
  static Element* GetAnElementForTiming(Element* aTarget,
                                        const Document* aDocument,
                                        nsIGlobalObject* aGlobal);

  /*
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor.
   *
   * This method fills the |aArray| with all ancestor nodes of |aNode|
   * including |aNode| at the zero index.
   *
   */
  static nsresult GetInclusiveAncestors(nsINode* aNode,
                                        nsTArray<nsINode*>& aArray);

  /*
   * https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor.
   *
   * This method fills |aAncestorNodes| with all ancestor nodes of |aNode|
   * including |aNode| (QI'd to nsIContent) at the zero index.
   * For each ancestor, there is a corresponding element in |aAncestorOffsets|
   * which is the ComputeIndexOf the child in relation to its parent.
   *
   * This method just sucks.
   */
  static nsresult GetInclusiveAncestorsAndOffsets(
      nsINode* aNode, uint32_t aOffset, nsTArray<nsIContent*>& aAncestorNodes,
      nsTArray<mozilla::Maybe<uint32_t>>& aAncestorOffsets);

  /*
   * https://dom.spec.whatwg.org/#concept-shadow-including-ancestor.
   *
   * Similar as the GetInclusiveAncestorsAndOffsets method, except this
   * will use host elements as the parent for shadow roots.
   *
   * When the current content is a ShadowRoot, the offset of it from
   * its ancestor (the host element) will be Nothing().
   */
  static nsresult GetShadowIncludingAncestorsAndOffsets(
      nsINode* aNode, uint32_t aOffset, nsTArray<nsIContent*>& aAncestorNodes,
      nsTArray<mozilla::Maybe<uint32_t>>& aAncestorOffsets);

  /**
   * Returns the closest common inclusive ancestor
   * (https://dom.spec.whatwg.org/#concept-tree-inclusive-ancestor) , if any,
   * for two nodes.
   *
   * Returns null if the nodes are disconnected.
   */
  static nsINode* GetClosestCommonInclusiveAncestor(nsINode* aNode1,
                                                    nsINode* aNode2) {
    if (aNode1 == aNode2) {
      return aNode1;
    }

    return GetCommonAncestorHelper(aNode1, aNode2);
  }

  static nsINode* GetClosestCommonShadowIncludingInclusiveAncestor(
      nsINode* aNode1, nsINode* aNode2);

  /**
   * Returns the common flattened tree ancestor, if any, for two given content
   * nodes.
   */
  static nsIContent* GetCommonFlattenedTreeAncestor(nsIContent* aContent1,
                                                    nsIContent* aContent2) {
    if (aContent1 == aContent2) {
      return aContent1;
    }

    return GetCommonFlattenedTreeAncestorHelper(aContent1, aContent2);
  }

  /**
   * Returns the common flattened tree ancestor from the point of view of
   * the selection system, if any, for two given content nodes.
   */
  static nsIContent* GetCommonFlattenedTreeAncestorForSelection(
      nsIContent* aContent1, nsIContent* aContent2);

  /**
   * Returns the common flattened tree ancestor from the point of view of the
   * style system, if any, for two given content nodes.
   */
  static Element* GetCommonFlattenedTreeAncestorForStyle(Element* aElement1,
                                                         Element* aElement2);

  /**
   * Returns the common BrowserParent ancestor, if any, for two given
   * BrowserParent.
   */
  static mozilla::dom::BrowserParent* GetCommonBrowserParentAncestor(
      mozilla::dom::BrowserParent* aBrowserParent1,
      mozilla::dom::BrowserParent* aBrowserParent2);

  // https://html.spec.whatwg.org/#target-element
  // https://html.spec.whatwg.org/#find-a-potential-indicated-element
  static Element* GetTargetElement(Document* aDocument,
                                   const nsAString& aAnchorName);
  /** Returns true if aNode1 is before aNode2 in the same connected tree. */
  static bool PositionIsBefore(const nsINode* aNode1, const nsINode* aNode2) {
    return CompareTreePosition<TreeKind::DOM>(aNode1, aNode2, nullptr,
                                              nullptr) < 0;
  }

  /**
   * Cache implementation for ComparePoints().
   *
   * This cache keeps the last cache_size child/index combinations
   * in a stack-allocated array for fast lookup.
   * If the cache is full, the entries are overridden,
   * starting from the oldest entry.
   *
   * Note: This cache does not observe invalidation. As soon as script has
   * run, this cache must not be used anymore.
   * Also, this cache uses raw pointers. Beware!
   */
  template <size_t cache_size = 100>
  struct ResizableNodeIndexCache {
    /**
     * Looks up or computes two indices in one loop.
     */
    template <TreeKind aTreeKind>
    void ComputeIndicesOf(const nsINode* aParent, const nsINode* aChild1,
                          const nsINode* aChild2,
                          mozilla::Maybe<int32_t>& aChild1Index,
                          mozilla::Maybe<int32_t>& aChild2Index) {
      AssertTreeKind(aTreeKind);
      bool foundChild1 = false;
      bool foundChild2 = false;
      for (size_t cacheIndex = 0; cacheIndex < cache_size; ++cacheIndex) {
        if (foundChild1 && foundChild2) {
          return;
        }
        const nsINode* node = mNodes[cacheIndex];
        if (!node) {
          // reached the end of not-fully-populated cache.
          break;
        }
        if (!foundChild1 && node == aChild1) {
          aChild1Index = mIndices[cacheIndex];
          foundChild1 = true;
          continue;
        }
        if (!foundChild2 && node == aChild2) {
          aChild2Index = mIndices[cacheIndex];
          foundChild2 = true;
          continue;
        }
      }
      if (!foundChild1) {
        aChild1Index =
            ComputeAndInsertIndexIntoCache<aTreeKind>(aParent, aChild1);
      }
      if (!foundChild2) {
        aChild2Index =
            ComputeAndInsertIndexIntoCache<aTreeKind>(aParent, aChild2);
      }
    }
    /**
     * Looks up or computes child index.
     */
    template <TreeKind aTreeKind>
    mozilla::Maybe<int32_t> ComputeIndexOf(const nsINode* aParent,
                                           const nsINode* aChild) {
      AssertTreeKind(aTreeKind);
      for (size_t cacheIndex = 0; cacheIndex < cache_size; ++cacheIndex) {
        const nsINode* node = mNodes[cacheIndex];
        if (!node) {
          break;
        }
        if (node == aChild) {
          return mIndices[cacheIndex];
        }
      }
      return ComputeAndInsertIndexIntoCache<aTreeKind>(aParent, aChild);
    }

   private:
    /**
     * Computes the index of aChild in aParent, inserts the index into the
     * cache, and returns the index.
     */
    template <TreeKind aTreeKind>
    mozilla::Maybe<int32_t> ComputeAndInsertIndexIntoCache(
        const nsINode* aParent, const nsINode* aChild) {
      AssertTreeKind(aTreeKind);
      mozilla::Maybe<int32_t> childIndex =
          nsContentUtils::GetIndexInParent<aTreeKind>(aParent, aChild);

      mNodes[mNext] = aChild;
      mIndices[mNext] = childIndex;

      ++mNext;
      if (mNext == cache_size) {
        // the last element of the cache has been reached.
        // set mNext to 0 to start overriding the oldest cache entries.
        mNext = 0;
      }
      return childIndex;
    }

    /// Node storage. The array is initialized to null
    /// by the empty initializer list.
    const nsINode* mNodes[cache_size]{};

    mozilla::Maybe<int32_t> mIndices[cache_size];

    /// The next element in the cache that will be written to.
    /// If the cache is full (mNext == cache_size),
    /// the oldest entries in the cache will be overridden,
    /// ie. mNext will be set to 0.
    size_t mNext{0};

#ifdef DEBUG
    mozilla::Maybe<TreeKind> mTreeKind;
#endif

    void AssertTreeKind(TreeKind aKind) {
#ifdef DEBUG
      MOZ_ASSERT(!mTreeKind || mTreeKind.value() == aKind, "Mixing queries");
      mTreeKind = mozilla::Some(aKind);
#endif
    }
  };

  /**
   * Typedef with a reasonable default cache size.
   * If Caches of different sizes are needed,
   * ComparePoints would need to become templated.
   */
  using NodeIndexCache = ResizableNodeIndexCache<100>;

  /**
   *  Utility routine to compare two "points", where a point is a node/offset
   *  pair.
   *  Pass a cache object as aIndexCache if you expect to repeatedly
   *  call this function with the same value as aParent1 or aParent2.
   *
   *  @return -1 if point1 < point2,
   *          1 if point1 > point2,
   *          0 if point1 == point2.
   *          `Nothing` if the two nodes aren't in the same connected subtree.
   */
  static mozilla::Maybe<int32_t> ComparePointsWithIndices(
      const nsINode* aParent1, uint32_t aOffset1, const nsINode* aParent2,
      uint32_t aOffset2, NodeIndexCache* aIndexCache = nullptr);

  /**
   *  Utility routine to compare two "points", where a point is a RangeBoundary.
   *  Pass a cache object as aIndexCache if you expect to repeatedly call this
   * function with the same value as aBoundary1 or aBoundary2.
   *
   *  @return -1 if point1 < point2,
   *          1 if point1 > point2,
   *          0 if point1 == point2.
   *          `Nothing` if the two nodes aren't in the same connected subtree.
   */
  template <typename PT1, typename RT1, typename PT2, typename RT2>
  static mozilla::Maybe<int32_t> ComparePoints(
      const mozilla::RangeBoundaryBase<PT1, RT1>& aBoundary1,
      const mozilla::RangeBoundaryBase<PT2, RT2>& aBoundary2,
      NodeIndexCache* aIndexCache = nullptr);

  /**
   * DO NOT USE this method for comparing the points in new code.  this method
   * emulates same result as `ComparePoints` before bug 1741148.
   * When the old `ComparePoints` was called with offset value over `INT32_MAX`
   * or `-1` which is used as "not found" by some API, they were treated as-is
   * without checking whether the negative value or valid value.  Thus, this
   * handles the negative offset cases in the special paths to keep the
   * traditional behavior. If you want to use this in new code, it means that
   * you **should** check the offset values and call `ComparePoints` instead.
   */
  static mozilla::Maybe<int32_t> ComparePoints_AllowNegativeOffsets(
      const nsINode* aParent1, int64_t aOffset1, const nsINode* aParent2,
      int64_t aOffset2) {
    if (MOZ_UNLIKELY(aOffset1 < 0 || aOffset2 < 0)) {
      // If in same container, just the offset is compared.
      if (aParent1 == aParent2) {
        const int32_t compOffsets =
            aOffset1 == aOffset2 ? 0 : (aOffset1 < aOffset2 ? -1 : 1);
        return mozilla::Some(compOffsets);
      }
      // Otherwise, aOffset1 is referred only when aParent2 is a descendant of
      // aParent1.
      if (aOffset1 < 0 && aParent2->IsInclusiveDescendantOf(aParent1)) {
        return mozilla::Some(-1);
      }
      // And also aOffset2 is referred only when aParent1 is a descendant of
      // aParent2.
      if (aOffset2 < 0 && aParent1->IsInclusiveDescendantOf(aParent2)) {
        return mozilla::Some(1);
      }
      // Otherwise, aOffset1 nor aOffset2 is referred so that any value is fine
      // if negative.
      return ComparePointsWithIndices(
          aParent1,
          // Avoid warnings.
          aOffset1 < 0 ? aParent1->GetChildCount()
                       : std::min(static_cast<uint32_t>(aOffset1),
                                  aParent1->GetChildCount()),
          aParent2,
          // Avoid warnings.
          aOffset2 < 0 ? aParent2->GetChildCount()
                       : std::min(static_cast<uint32_t>(aOffset2),
                                  aParent2->GetChildCount()));
    }
    return ComparePointsWithIndices(aParent1, aOffset1, aParent2, aOffset2);
  }

  /**
   * Brute-force search of the element subtree rooted at aContent for
   * an element with the given id.  aId must be nonempty, otherwise
   * this method may return nodes even if they have no id!
   */
  static Element* MatchElementId(nsIContent* aContent, const nsAString& aId);

  /**
   * Similar to above, but to be used if one already has an atom for the ID
   */
  static Element* MatchElementId(nsIContent* aContent, const nsAtom* aId);

  /**
   * Reverses the document position flags passed in.
   *
   * @param   aDocumentPosition   The document position flags to be reversed.
   *
   * @return  The reversed document position flags.
   *
   * @see Node
   */
  static uint16_t ReverseDocumentPosition(uint16_t aDocumentPosition);

  static const nsDependentSubstring TrimCharsInSet(const char* aSet,
                                                   const nsAString& aValue);

  template <bool IsWhitespace(char16_t)>
  static const nsDependentSubstring TrimWhitespace(const nsAString& aStr,
                                                   bool aTrimTrailing = true);

  /**
   * Returns true if aChar is of class Lu, Ll, Lt, Lm, Lo, Nd, Nl or No
   */
  static bool IsAlphanumeric(uint32_t aChar);
  /**
   * Returns true if aChar is of class L*, N* or S* (for first-letter).
   */
  static bool IsAlphanumericOrSymbol(uint32_t aChar);
  /**
   * Returns true if aChar is a kind of hyphen.
   */
  static bool IsHyphen(uint32_t aChar);

  /*
   * Is the character an HTML whitespace character?
   *
   * We define whitespace using the list in HTML5 and css3-selectors:
   * U+0009, U+000A, U+000C, U+000D, U+0020
   *
   * HTML 4.01 also lists U+200B (zero-width space).
   */
  static bool IsHTMLWhitespace(char16_t aChar);

  /*
   * Returns whether the character is an HTML whitespace (see IsHTMLWhitespace)
   * or a nbsp character (U+00A0).
   */
  static bool IsHTMLWhitespaceOrNBSP(char16_t aChar);

  /**
   * https://developer.mozilla.org/en-US/docs/Web/HTML/Block-level_elements
   */
  static bool IsHTMLBlockLevelElement(nsIContent* aContent);

  enum ParseHTMLIntegerResultFlags {
    eParseHTMLInteger_NoFlags = 0,
    // eParseHTMLInteger_NonStandard is set if the string representation of the
    // integer was not the canonical one, but matches at least one of the
    // following:
    //   * had leading whitespaces
    //   * had '+' sign
    //   * had leading '0'
    //   * was '-0'
    eParseHTMLInteger_NonStandard = 1 << 0,
    eParseHTMLInteger_DidNotConsumeAllInput = 1 << 1,
    // Set if one or more error flags were set.
    eParseHTMLInteger_Error = 1 << 2,
    eParseHTMLInteger_ErrorNoValue = 1 << 3,
    eParseHTMLInteger_ErrorOverflow = 1 << 4,
    // Use this flag to detect the difference between overflow and underflow
    eParseHTMLInteger_Negative = 1 << 5,
  };
  static int32_t ParseHTMLInteger(const nsAString& aValue,
                                  ParseHTMLIntegerResultFlags* aResult) {
    return ParseHTMLInteger(aValue.BeginReading(), aValue.EndReading(),
                            aResult);
  }
  static int32_t ParseHTMLInteger(const char16_t* aStart, const char16_t* aEnd,
                                  ParseHTMLIntegerResultFlags* aResult);
  static int32_t ParseHTMLInteger(const nsACString& aValue,
                                  ParseHTMLIntegerResultFlags* aResult) {
    return ParseHTMLInteger(aValue.BeginReading(), aValue.EndReading(),
                            aResult);
  }
  static int32_t ParseHTMLInteger(const char* aStart, const char* aEnd,
                                  ParseHTMLIntegerResultFlags* aResult);

 private:
  template <class CharT>
  static int32_t ParseHTMLIntegerImpl(const CharT* aStart, const CharT* aEnd,
                                      ParseHTMLIntegerResultFlags* aResult);

 public:
  /**
   * Parse the value of the <font size=""> attribute according to the HTML5
   * spec as of April 16, 2012.
   *
   * @param aValue the value to parse
   * @return 1 to 7, or 0 if the value couldn't be parsed
   */
  static int32_t ParseLegacyFontSize(const nsAString& aValue);

  static void Shutdown();

  /**
   * Checks whether two nodes come from the same origin.
   */
  static nsresult CheckSameOrigin(const nsINode* aTrustedNode,
                                  const nsINode* unTrustedNode);

  // Check if the (JS) caller can access aNode.
  static bool CanCallerAccess(const nsINode* aNode);

  // Check if the (JS) caller can access aWindow.
  // aWindow can be either outer or inner window.
  static bool CanCallerAccess(nsPIDOMWindowInner* aWindow);

  // Check if the principal is chrome or an addon with the permission.
  static bool PrincipalHasPermission(nsIPrincipal& aPrincipal,
                                     const nsAtom* aPerm);

  // Check if the JS caller is chrome or an addon with the permission.
  static bool CallerHasPermission(JSContext* aCx, const nsAtom* aPerm);

  /**
   * Returns the triggering principal which should be used for the given URL
   * attribute value with the given subject principal.
   *
   * If the attribute value is not an absolute URL, the subject principal will
   * be ignored, and the node principal of aContent will be used instead.
   * If aContent is non-null, this function will always return a principal.
   * Otherewise, it may return null if aSubjectPrincipal is null or is rejected
   * based on the attribute value.
   *
   * @param aContent The content on which the attribute is being set.
   * @param aAttrValue The URL value of the attribute. For parsed attribute
   *        values, such as `srcset`, this function should be called separately
   *        for each URL value it contains.
   * @param aSubjectPrincipal The subject principal of the scripted caller
   *        responsible for setting the attribute, or null if no scripted caller
   *        can be determined.
   */
  static nsIPrincipal* GetAttrTriggeringPrincipal(
      nsIContent* aContent, const nsAString& aAttrValue,
      nsIPrincipal* aSubjectPrincipal);

  /**
   * Returns true if the given string is guaranteed to be treated as an absolute
   * URL, rather than a relative URL. In practice, this means any complete URL
   * as supported by nsStandardURL, or any string beginning with a valid scheme
   * which is known to the IO service, and has the URI_NORELATIVE flag.
   *
   * If the URL may be treated as absolute in some cases, but relative in others
   * (for instance, "http:foo", which can be either an absolute or relative URL,
   * depending on the context), this function returns false.
   */
  static bool IsAbsoluteURL(const nsACString& aURL);

  // Check if a node is in the document prolog, i.e. before the document
  // element.
  static bool InProlog(nsINode* aNode);

  static nsIBidiKeyboard* GetBidiKeyboard();

  /**
   * Get the cache security manager service. Can return null if the layout
   * module has been shut down.
   */
  static nsIScriptSecurityManager* GetSecurityManager() {
    return sSecurityManager;
  }

  // Returns the subject principal from the JSContext. May only be called
  // from the main thread and assumes an existing compartment.
  static nsIPrincipal* SubjectPrincipal(JSContext* aCx);

  // Returns the subject principal. Guaranteed to return non-null. May only
  // be called when nsContentUtils is initialized.
  static nsIPrincipal* SubjectPrincipal();

  // Returns the prinipal of the given JS object. This may only be called on
  // the main thread for objects from the main thread's JSRuntime. The object
  // must not be a cross-compartment wrapper, because CCWs are not associated
  // with a single realm.
  static nsIPrincipal* ObjectPrincipal(JSObject* aObj);

  static void GenerateStateKey(nsIContent* aContent, Document* aDocument,
                               nsACString& aKey);

  /**
   * Create a new nsIURI from aSpec, using aBaseURI as the base.  The
   * origin charset of the new nsIURI will be the document charset of
   * aDocument.
   */
  static nsresult NewURIWithDocumentCharset(nsIURI** aResult,
                                            const nsAString& aSpec,
                                            Document* aDocument,
                                            nsIURI* aBaseURI);

  /**
   * Returns true if |aAtom| contains at least one |aChar|.
   */
  static bool ContainsChar(nsAtom* aAtom, char aChar);

  /**
   * Returns true if |aName| is a name with dashes.
   */
  static bool IsNameWithDash(nsAtom* aName);

  /**
   * Returns true if |aName| is a valid name to be registered via
   * customElements.define.
   */
  static bool IsCustomElementName(nsAtom* aName, uint32_t aNameSpaceID);

  static nsresult CheckQName(const nsAString& aQualifiedName,
                             bool aNamespaceAware = true,
                             const char16_t** aColon = nullptr);

  static nsresult SplitQName(const nsIContent* aNamespaceResolver,
                             const nsString& aQName, int32_t* aNamespace,
                             nsAtom** aLocalName);

  static nsresult GetNodeInfoFromQName(const nsAString& aNamespaceURI,
                                       const nsAString& aQualifiedName,
                                       nsNodeInfoManager* aNodeInfoManager,
                                       uint16_t aNodeType,
                                       mozilla::dom::NodeInfo** aNodeInfo);

  static void SplitExpatName(const char16_t* aExpatName, nsAtom** aPrefix,
                             nsAtom** aTagName, int32_t* aNameSpaceID);

  // Get a permission-manager setting for the given principal and type.
  // If the pref doesn't exist or if it isn't ALLOW_ACTION, false is
  // returned, otherwise true is returned. Always returns true for the
  // system principal, and false for a null principal.
  static bool IsSitePermAllow(nsIPrincipal* aPrincipal,
                              const nsACString& aType);

  // Get a permission-manager setting for the given principal and type.
  // If the pref doesn't exist or if it isn't DENY_ACTION, false is
  // returned, otherwise true is returned. Always returns false for the
  // system principal, and true for a null principal.
  static bool IsSitePermDeny(nsIPrincipal* aPrincipal, const nsACString& aType);

  // Get a permission-manager setting for the given principal and type.
  // If the pref doesn't exist or if it isn't ALLOW_ACTION, false is
  // returned, otherwise true is returned. Always returns true for the
  // system principal, and false for a null principal.
  // This version checks the permission for an exact host match on
  // the principal
  static bool IsExactSitePermAllow(nsIPrincipal* aPrincipal,
                                   const nsACString& aType);

  // Get a permission-manager setting for the given principal and type.
  // If the pref doesn't exist or if it isn't DENY_ACTION, false is
  // returned, otherwise true is returned. Always returns false for the
  // system principal, and true for a null principal.
  // This version checks the permission for an exact host match on
  // the principal
  static bool IsExactSitePermDeny(nsIPrincipal* aPrincipal,
                                  const nsACString& aType);

  // Returns true if the pref exists and is not UNKNOWN_ACTION.
  static bool HasSitePerm(nsIPrincipal* aPrincipal, const nsACString& aType);

  // Returns true if aDoc1 and aDoc2 have equal NodePrincipal()s.
  static bool HaveEqualPrincipals(Document* aDoc1, Document* aDoc2);

  /**
   * Regster aObserver as a shutdown observer. A strong reference is held
   * to aObserver until UnregisterShutdownObserver is called.
   */
  static void RegisterShutdownObserver(nsIObserver* aObserver);
  static void UnregisterShutdownObserver(nsIObserver* aObserver);

  /**
   * @return true if aContent has an attribute aName in namespace aNameSpaceID,
   * and the attribute value is non-empty.
   */
  static bool HasNonEmptyAttr(const nsIContent* aContent, int32_t aNameSpaceID,
                              nsAtom* aName);

  /**
   * Method that gets the primary presContext for the node.
   *
   * @param aContent The content node.
   * @return the presContext, or nullptr if the content is not in a document
   *         (if GetComposedDoc returns nullptr)
   */
  static nsPresContext* GetContextForContent(const nsIContent* aContent);

  /**
   * Method that gets the pres shell for the node.
   *
   * @param aContent The content node.
   * @return the pres shell, or nullptr if the content is not in a document
   *         (if GetComposedDoc returns nullptr)
   */
  static mozilla::PresShell* GetPresShellForContent(const nsIContent* aContent);

  /**
   * Returns true if objects in aDocument shouldn't initiate image loads.
   */
  static bool DocumentInactiveForImageLoads(Document* aDocument);

  /**
   * Convert a CORSMode into the corresponding imgILoader flags for
   * passing to LoadImage.
   * @param aMode CORS mode to convert
   * @return a bitfield suitable to bitwise OR with other nsIRequest flags
   */
  static int32_t CORSModeToLoadImageFlags(mozilla::CORSMode aMode);

  /**
   * Method to start an image load.  This does not do any security checks.
   * This method will attempt to make aURI immutable; a caller that wants to
   * keep a mutable version around should pass in a clone.
   *
   * @param aURI uri of the image to be loaded
   * @param aContext element of document where the result of this request
   *                 will be used.
   * @param aLoadingDocument the document we belong to
   * @param aLoadingPrincipal the principal doing the load
   * @param aReferrerInfo the referrerInfo use on channel creation
   * @param aObserver the observer for the image load
   * @param aLoadFlags the load flags to use.  See nsIRequest
   * @param [aContentPolicyType=nsIContentPolicy::TYPE_INTERNAL_IMAGE]
   * (Optional) The CP content type to use
   * @param aUseUrgentStartForChannel,(Optional) a flag to mark on channel if it
   *        is triggered by user input events.
   * @return the imgIRequest for the image load
   */
  static nsresult LoadImage(
      nsIURI* aURI, nsINode* aContext, Document* aLoadingDocument,
      nsIPrincipal* aLoadingPrincipal, uint64_t aRequestContextID,
      nsIReferrerInfo* aReferrerInfo, imgINotificationObserver* aObserver,
      int32_t aLoadFlags, const nsAString& initiatorType,
      imgRequestProxy** aRequest,
      nsContentPolicyType aContentPolicyType =
          nsIContentPolicy::TYPE_INTERNAL_IMAGE,
      bool aUseUrgentStartForChannel = false, bool aLinkPreload = false,
      uint64_t aEarlyHintPreloaderId = 0,
      mozilla::dom::FetchPriority aFetchPriority =
          mozilla::dom::FetchPriority::Auto);

  /**
   * Obtain an image loader that respects the given document/channel's privacy
   * status. Null document/channel arguments return the public image loader.
   */
  static imgLoader* GetImgLoaderForDocument(Document* aDoc);
  static imgLoader* GetImgLoaderForChannel(nsIChannel* aChannel,
                                           Document* aContext);

  /**
   * Method to get an imgIContainer from an image loading content
   *
   * @param aContent The image loading content.  Must not be null.
   * @param aRequest The image request [out]
   * @return the imgIContainer corresponding to the first frame of the image
   */
  static already_AddRefed<imgIContainer> GetImageFromContent(
      nsIImageLoadingContent* aContent, imgIRequest** aRequest = nullptr);

  /**
   * Method that decides whether a content node is draggable
   *
   * @param aContent The content node to test.
   * @return whether it's draggable
   */
  static bool ContentIsDraggable(nsIContent* aContent);

  /**
   * Method that decides whether a content node is a draggable image
   *
   * @param aContent The content node to test.
   * @return whether it's a draggable image
   */
  static bool IsDraggableImage(nsIContent* aContent);

  /**
   * Method that decides whether a content node is a draggable link
   *
   * @param aContent The content node to test.
   * @return whether it's a draggable link
   */
  static bool IsDraggableLink(const nsIContent* aContent);

  /**
   * Convenience method to create a new nodeinfo that differs only by prefix and
   * name from aNodeInfo. The new nodeinfo's name is set to aName, and prefix is
   * set to null.
   */
  static nsresult QNameChanged(mozilla::dom::NodeInfo* aNodeInfo, nsAtom* aName,
                               mozilla::dom::NodeInfo** aResult);

  /**
   * Returns the appropriate event argument names for the specified
   * namespace and event name.  Added because we need to switch between
   * SVG's "evt" and the rest of the world's "event", and because onerror
   * on window takes 5 args.
   */
  static void GetEventArgNames(int32_t aNameSpaceID, nsAtom* aEventName,
                               bool aIsForWindow, uint32_t* aArgCount,
                               const char*** aArgNames);

  /**
   * Returns true if this loadGroup uses Private Browsing.
   */
  static bool IsInPrivateBrowsing(nsILoadGroup* aLoadGroup);

  /**
   * Returns whether a node is in the same tree as another one, accounting for
   * anonymous roots.
   *
   * This method is particularly useful for callers who are trying to ensure
   * that they are working with a non-anonymous descendant of a given node.  If
   * aContent is a descendant of aNode, a return value of false from this
   * method means that it's an anonymous descendant from aNode's point of view.
   *
   * Both arguments to this method must be non-null.
   */
  static bool IsInSameAnonymousTree(const nsINode* aNode,
                                    const nsINode* aOtherNode);

  /*
   * Traverse the parent chain from aElement up to aStop, and return true if
   * there's an interactive html content; false otherwise.
   *
   * Note: This crosses shadow boundaries but not document boundaries.
   */
  static bool IsInInteractiveHTMLContent(const Element* aElement,
                                         const Element* aStop);

  /**
   * Return the nsIXPConnect service.
   */
  static nsIXPConnect* XPConnect() { return sXPConnect; }

  /**
   * Report simple error message to the browser console
   *   @param aErrorText the error message
   *   @param aCategory Name of the module reporting error
   *   @param aFromPrivateWindow Whether from private window or not
   *   @param aFromChromeContext Whether from chrome context or not
   *   @param [aErrorFlags] See nsIScriptError.
   */
  static void LogSimpleConsoleError(
      const nsAString& aErrorText, const nsACString& aCategory,
      bool aFromPrivateWindow, bool aFromChromeContext,
      uint32_t aErrorFlags = nsIScriptError::errorFlag);

  /**
   * Report a non-localized error message to the error console.
   *   @param aErrorText the error message
   *   @param aErrorFlags See nsIScriptError.
   *   @param aCategory Name of module reporting error.
   *   @param aDocument Reference to the document which triggered the message.
   *   @param aLocation message location. Pass the empty location to omit it.
   */
  static nsresult ReportToConsoleNonLocalized(
      const nsAString& aErrorText, uint32_t aErrorFlags,
      const nsACString& aCategory, const Document* aDocument,
      const mozilla::SourceLocation& aLocation =
          mozilla::JSCallingLocation::Get());

  /**
   * Report a non-localized error message to the error console base on the
   * innerWindowID.
   *   @param aErrorText the error message
   *   @param aErrorFlags See nsIScriptError.
   *   @param aCategory Name of module reporting error.
   *   @param [aInnerWindowID] Inner window ID for document which triggered the
   *          message.
   *   @param aLocation message location. Pass the empty location to omit it.
   */
  static nsresult ReportToConsoleByWindowID(
      const nsAString& aErrorText, uint32_t aErrorFlags,
      const nsACString& aCategory, uint64_t aInnerWindowID,
      const mozilla::SourceLocation& aLocation =
          mozilla::JSCallingLocation::Get());

  /**
   * Report a localized error message to the error console.
   *   @param aErrorFlags See nsIScriptError.
   *   @param aCategory Name of module reporting error.
   *   @param aDocument Reference to the document which triggered the message.
   *   @param aFile Properties file containing localized message.
   *   @param aMessageName Name of localized message.
   *   @param [aParams=empty-array] (Optional) Parameters to be substituted into
              localized message.
   *   @param aLocation message location. Pass the empty location to omit it.
   */
  enum PropertiesFile {
    eCSS_PROPERTIES,
    eXUL_PROPERTIES,
    eLAYOUT_PROPERTIES,
    eFORMS_PROPERTIES,
    ePRINTING_PROPERTIES,
    eDOM_PROPERTIES,
    eHTMLPARSER_PROPERTIES,
    eSVG_PROPERTIES,
    eBRAND_PROPERTIES,
    eCOMMON_DIALOG_PROPERTIES,
    eMATHML_PROPERTIES,
    eSECURITY_PROPERTIES,
    eNECKO_PROPERTIES,
    eFORMS_PROPERTIES_en_US,
    eDOM_PROPERTIES_en_US,
    PropertiesFile_COUNT
  };
  static nsresult ReportToConsole(
      uint32_t aErrorFlags, const nsACString& aCategory,
      const Document* aDocument, PropertiesFile aFile, const char* aMessageName,
      const nsTArray<nsString>& aParams = nsTArray<nsString>(),
      const mozilla::SourceLocation& aLocation =
          mozilla::JSCallingLocation::Get());

  static void ReportEmptyGetElementByIdArg(const Document* aDoc);

  static void LogMessageToConsole(const char* aMsg);

  static bool SpoofLocaleEnglish();
  static bool SpoofLocaleEnglish(const Document* aDocument);

  /**
   * Get the localized string named |aKey| in properties file |aFile|.
   */
  static nsresult GetLocalizedString(PropertiesFile aFile, const char* aKey,
                                     nsAString& aResult);

  /**
   * Same as GetLocalizedString, except that it might use en-US locale depending
   * on SpoofLocaleEnglish() and whether the document is a built-in browser
   * page.
   */
  static nsresult GetMaybeLocalizedString(PropertiesFile aFile,
                                          const char* aKey, Document* aDocument,
                                          nsAString& aResult);

  /**
   * A helper function that parses a sandbox attribute (of an <iframe> or a CSP
   * directive) and converts it to the set of flags used internally.
   *
   * @param aSandboxAttr  the sandbox attribute
   * @return              the set of flags (SANDBOXED_NONE if aSandboxAttr is
   *                      null)
   */
  static uint32_t ParseSandboxAttributeToFlags(const nsAttrValue* aSandboxAttr);

  /**
   * A helper function that checks if a string matches a valid sandbox flag.
   *
   * @param aFlag   the potential sandbox flag.
   * @return        true if the flag is a sandbox flag.
   */
  static bool IsValidSandboxFlag(const nsAString& aFlag);

  /**
   * A helper function that returns a string attribute corresponding to the
   * sandbox flags.
   *
   * @param aFlags    the sandbox flags
   * @param aString   the attribute corresponding to the flags (null if aFlags
   *                  is zero)
   */
  static void SandboxFlagsToString(uint32_t aFlags, nsAString& aString);

  static bool PrefetchPreloadEnabled(nsIDocShell* aDocShell);

  static void ExtractErrorValues(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                 nsACString& aSourceSpecOut, uint32_t* aLineOut,
                                 uint32_t* aColumnOut, nsString& aMessageOut);

  static nsresult CalculateBufferSizeForImage(
      const uint32_t& aStride, const mozilla::gfx::IntSize& aImageSize,
      const mozilla::gfx::SurfaceFormat& aFormat, size_t* aMaxBufferSize,
      size_t* aUsedBufferSize);

  // Returns true if the URI's host is contained in a list which is a comma
  // separated domain list.  Each item may start with "*.".  If starts with
  // "*.", it matches any sub-domains.
  // The aList argument must be a lower-case string.
  static bool IsURIInList(nsIURI* aURI, const nsCString& aList);

  // Returns true if the URI's host is contained in a pref list which is a comma
  // separated domain list.  Each item may start with "*.".  If starts with
  // "*.", it matches any sub-domains.
  static bool IsURIInPrefList(nsIURI* aURI, const char* aPrefName);

  /*&
   * A convenience version of FormatLocalizedString that can be used if all the
   * params are in same-typed strings.  The variadic template args need to come
   * at the end, so we put aResult at the beginning to make sure it's clear
   * which is the output and which are the inputs.
   */
  template <typename... T>
  static nsresult FormatLocalizedString(nsAString& aResult,
                                        PropertiesFile aFile, const char* aKey,
                                        const T&... aParams) {
    static_assert(sizeof...(aParams) != 0, "Use GetLocalizedString()");
    AutoTArray<nsString, sizeof...(aParams)> params = {
        aParams...,
    };
    return FormatLocalizedString(aFile, aKey, params, aResult);
  }

  /**
   * Same as FormatLocalizedString template version, except that it might use
   * en-US locale depending on SpoofLocaleEnglish() and whether the document is
   * a built-in browser page.
   */
  template <typename... T>
  static nsresult FormatMaybeLocalizedString(nsAString& aResult,
                                             PropertiesFile aFile,
                                             const char* aKey,
                                             Document* aDocument,
                                             const T&... aParams) {
    static_assert(sizeof...(aParams) != 0, "Use GetMaybeLocalizedString()");
    AutoTArray<nsString, sizeof...(aParams)> params = {
        aParams...,
    };
    return FormatMaybeLocalizedString(aFile, aKey, aDocument, params, aResult);
  }

  /**
   * Fill (with the parameters given) the localized string named |aKey| in
   * properties file |aFile| consuming an nsTArray of nsString parameters rather
   * than a char16_t** for the sake of avoiding use-after-free errors involving
   * temporaries.
   */
  static nsresult FormatLocalizedString(PropertiesFile aFile, const char* aKey,
                                        const nsTArray<nsString>& aParamArray,
                                        nsAString& aResult);

  /**
   * Same as FormatLocalizedString, except that it might use en-US locale
   * depending on SpoofLocaleEnglish() and whether the document is a built-in
   * browser page.
   */
  static nsresult FormatMaybeLocalizedString(
      PropertiesFile aFile, const char* aKey, Document* aDocument,
      const nsTArray<nsString>& aParamArray, nsAString& aResult);

  /**
   * Returns true if aDocument is a chrome document
   */
  static bool IsChromeDoc(const Document* aDocument);

  /**
   * Returns true if aDocument is an addon document
   */
  static bool IsAddonDoc(const Document* aDocument);

  /**
   * Returns true if aDocument is in a docshell whose parent is the same type
   */
  static bool IsChildOfSameType(Document* aDoc);

  /**
   * Returns true if the content-type will be rendered as plain-text.
   */
  static bool IsPlainTextType(const nsACString& aContentType);

  /**
   * Returns true iff the type is rendered as plain text and doesn't support
   * non-UTF-8 encodings.
   */
  static bool IsUtf8OnlyPlainTextType(const nsACString& aContentType);

  /**
   * Returns true if aDocument belongs to a chrome docshell for
   * display purposes.  Returns false for null documents or documents
   * which do not belong to a docshell.
   */
  static bool IsInChromeDocshell(const Document* aDocument);

  /**
   * Return the content policy service
   */
  static nsIContentPolicy* GetContentPolicy();

  /**
   * Map internal content policy types to external ones.
   */
  static inline ExtContentPolicyType InternalContentPolicyTypeToExternal(
      nsContentPolicyType aType);

  /**
   * check whether the Link header field applies to the context resource
   * see <http://tools.ietf.org/html/rfc5988#section-5.2>
   */
  static bool LinkContextIsURI(const nsAString& aAnchor, nsIURI* aDocURI);

  /**
   * Returns true if the content policy type is any of:
   *   * TYPE_INTERNAL_SCRIPT_PRELOAD
   *   * TYPE_INTERNAL_IMAGE_PRELOAD
   *   * TYPE_INTERNAL_STYLESHEET_PRELOAD
   */
  static bool IsPreloadType(nsContentPolicyType aType);

  /**
   * Quick helper to determine whether mutation events are enabled and there are
   * any mutation listeners of a given type that apply to this content or any of
   * its ancestors.
   * The method has the side effect to call document's MayDispatchMutationEvent
   * using aTargetForSubtreeModified as the parameter.
   *
   * @param aNode  The node to search for listeners
   * @param aType  The type of listener (NS_EVENT_BITS_MUTATION_*)
   * @param aTargetForSubtreeModified The node which is the target of the
   *                                  possible DOMSubtreeModified event.
   *
   * @return true if there are mutation listeners of the specified type
   */
  static bool WantMutationEvents(nsINode* aNode, uint32_t aType,
                                 nsINode* aTargetForSubtreeModified);

  /**
   * Quick helper to determine whether there are any mutation listeners
   * of a given type that apply to any content in this document. It is valid
   * to pass null for aDocument here, in which case this function always
   * returns true.
   *
   * @param aDocument The document to search for listeners
   * @param aType     The type of listener (NS_EVENT_BITS_MUTATION_*)
   *
   * @return true if there are mutation listeners of the specified type
   */
  static bool HasMutationListeners(Document* aDocument, uint32_t aType);
  /**
   * Synchronously fire DOMNodeRemoved on aChild. Only fires the event if
   * there really are listeners by checking using the HasMutationListeners
   * function above. The function makes sure to hold the relevant objects alive
   * for the duration of the event firing. However there are no guarantees
   * that any of the objects are alive by the time the function returns.
   * If you depend on that you need to hold references yourself.
   *
   * @param aChild    The node to fire DOMNodeRemoved at.
   * @param aParent   The parent of aChild.
   */
  MOZ_CAN_RUN_SCRIPT static void MaybeFireNodeRemoved(nsINode* aChild,
                                                      nsINode* aParent);

  /**
   * These methods create and dispatch a trusted event.
   * Works only with events which can be created by calling
   * Document::CreateEvent() with parameter "Events".
   * Note that don't use these methods for "input" event.  Use
   * DispatchInputEvent() instead.
   *
   * @param aDoc           The document which will be used to create the event.
   * @param aTarget        The target of the event.
   * @param aEventName     The name of the event.
   * @param aCanBubble     Whether the event can bubble.
   * @param aCancelable    Is the event cancelable.
   * @param aCopmosed      Is the event composed.
   * @param aDefaultAction Set to true if default action should be taken,
   *                       see EventTarget::DispatchEvent.
   */
  // TODO: annotate with `MOZ_CAN_RUN_SCRIPT`
  // (https://bugzilla.mozilla.org/show_bug.cgi?id=1625902).
  static nsresult DispatchTrustedEvent(
      Document* aDoc, mozilla::dom::EventTarget* aTarget,
      const nsAString& aEventName, CanBubble, Cancelable,
      Composed aComposed = Composed::eDefault, bool* aDefaultAction = nullptr,
      SystemGroupOnly aSystemGroupOnly = SystemGroupOnly::eNo);

  // TODO: annotate with `MOZ_CAN_RUN_SCRIPT`
  // (https://bugzilla.mozilla.org/show_bug.cgi?id=1625902).
  static nsresult DispatchTrustedEvent(
      Document* aDoc, mozilla::dom::EventTarget* aTarget,
      const nsAString& aEventName, CanBubble aCanBubble, Cancelable aCancelable,
      bool* aDefaultAction,
      SystemGroupOnly aSystemGroupOnly = SystemGroupOnly::eNo) {
    return DispatchTrustedEvent(aDoc, aTarget, aEventName, aCanBubble,
                                aCancelable, Composed::eDefault, aDefaultAction,
                                aSystemGroupOnly);
  }

  /**
   * This method creates and dispatches a trusted event using an event message.
   * @param aDoc           The document which will be used to create the event.
   * @param aTarget        The target of the event.
   * @param aEventMessage  The event message.
   * @param aCanBubble     Whether the event can bubble.
   * @param aCancelable    Is the event cancelable.
   * @param aDefaultAction Set to true if default action should be taken,
   *                       see EventTarget::DispatchEvent.
   */
  template <class WidgetEventType>
  static nsresult DispatchTrustedEvent(
      Document* aDoc, mozilla::dom::EventTarget* aTarget,
      EventMessage aEventMessage, CanBubble aCanBubble, Cancelable aCancelable,
      bool* aDefaultAction = nullptr,
      ChromeOnlyDispatch aOnlyChromeDispatch = ChromeOnlyDispatch::eNo) {
    WidgetEventType event(true, aEventMessage);
    MOZ_ASSERT(GetEventClassIDFromMessage(aEventMessage) == event.mClass);
    return DispatchEvent(aDoc, aTarget, event, aEventMessage, aCanBubble,
                         aCancelable, Trusted::eYes, aDefaultAction,
                         aOnlyChromeDispatch);
  }

  /**
   * This method dispatches "beforeinput" event with EditorInputEvent or
   * "input" event with proper event class.  If it's unsafe to dispatch,
   * this put the event into the script runner queue.  In such case, the
   * event becomes not cancelable even if it's defined as cancelable by
   * the spec.
   * Input Events spec defines as:
   *   Input events are dispatched on elements that act as editing hosts,
   *   including elements with the contenteditable attribute set, textarea
   *   elements, and input elements that permit text input.
   *
   * @param aEventTarget        The event target element of the "beforeinput"
   *                            or "input" event.  Must not be nullptr.
   * @param aEventMessage       Muse be eEditorBeforeInput or eEditorInput.
   * @param aEditorInputType    The inputType value of InputEvent.
   *                            If aEventTarget won't dispatch "input" event
   *                            with InputEvent, set EditorInputType::eUnknown.
   * @param aEditorBase         Optional.  If this is called by editor,
   *                            editor should set this.  Otherwise, leave
   *                            nullptr.
   * @param aOptions            Optional.  If aEditorInputType value requires
   *                            some additional data, they should be properly
   *                            set with this argument.
   * @param aEventStatus        Returns nsEventStatus_eConsumeNoDefault if
   *                            the dispatching event is cancelable and the
   *                            event was canceled by script (including
   *                            chrome script).  Otherwise, returns given
   *                            value.  Note that this can be nullptr only
   *                            when the dispatching event is not cancelable.
   */
  MOZ_CAN_RUN_SCRIPT static nsresult DispatchInputEvent(Element* aEventTarget);
  MOZ_CAN_RUN_SCRIPT static nsresult DispatchInputEvent(
      Element* aEventTarget, mozilla::EventMessage aEventMessage,
      mozilla::EditorInputType aEditorInputType,
      mozilla::EditorBase* aEditorBase, mozilla::InputEventOptions&& aOptions,
      nsEventStatus* aEventStatus = nullptr);

  /**
   * This method creates and dispatches a untrusted event.
   * Works only with events which can be created by calling
   * Document::CreateEvent() with parameter "Events".
   * @param aDoc           The document which will be used to create the event.
   * @param aTarget        The target of the event.
   * @param aEventName     The name of the event.
   * @param aCanBubble     Whether the event can bubble.
   * @param aCancelable    Is the event cancelable.
   * @param aDefaultAction Set to true if default action should be taken,
   *                       see EventTarget::DispatchEvent.
   */
  static nsresult DispatchUntrustedEvent(Document* aDoc,
                                         mozilla::dom::EventTarget* aTarget,
                                         const nsAString& aEventName, CanBubble,
                                         Cancelable,
                                         bool* aDefaultAction = nullptr);

  /**
   * This method creates and dispatches a untrusted event using an event
   * message.
   * @param aDoc           The document which will be used to create the event.
   * @param aTarget        The target of the event.
   * @param aEventMessage  The event message.
   * @param aCanBubble     Whether the event can bubble.
   * @param aCancelable    Is the event cancelable.
   * @param aDefaultAction Set to true if default action should be taken,
   *                       see EventTarget::DispatchEvent.
   */
  template <class WidgetEventType>
  static nsresult DispatchUntrustedEvent(
      Document* aDoc, mozilla::dom::EventTarget* aTarget,
      EventMessage aEventMessage, CanBubble aCanBubble, Cancelable aCancelable,
      bool* aDefaultAction = nullptr,
      ChromeOnlyDispatch aOnlyChromeDispatch = ChromeOnlyDispatch::eNo) {
    WidgetEventType event(false, aEventMessage);
    MOZ_ASSERT(GetEventClassIDFromMessage(aEventMessage) == event.mClass);
    return DispatchEvent(aDoc, aTarget, event, aEventMessage, aCanBubble,
                         aCancelable, Trusted::eNo, aDefaultAction,
                         aOnlyChromeDispatch);
  }

  /**
   * This method creates and dispatches a trusted event to the chrome
   * event handler (the parent object of the DOM Window in the event target
   * chain). Note, chrome event handler is used even if aTarget is a chrome
   * object. Use DispatchEventOnlyToChrome if the normal event dispatching is
   * wanted in case aTarget is a chrome object.
   * Works only with events which can be created by calling
   * Document::CreateEvent() with parameter "Events".
   * @param aDocument      The document which will be used to create the event,
   *                       and whose window's chrome handler will be used to
   *                       dispatch the event.
   * @param aTarget        The target of the event, used for event->SetTarget()
   * @param aEventName     The name of the event.
   * @param aCanBubble     Whether the event can bubble.
   * @param aCancelable    Is the event cancelable.
   * @param aDefaultAction Set to true if default action should be taken,
   *                       see EventTarget::DispatchEvent.
   */
  static nsresult DispatchChromeEvent(Document* aDoc,
                                      mozilla::dom::EventTarget* aTarget,
                                      const nsAString& aEventName, CanBubble,
                                      Cancelable,
                                      bool* aDefaultAction = nullptr);

  /**
   * Helper to dispatch a "framefocusrequested" event to chrome, which will only
   * bring the window to the foreground and switch tabs if aCanRaise is true.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY static void RequestFrameFocus(
      Element& aFrameElement, bool aCanRaise,
      mozilla::dom::CallerType aCallerType);

  /**
   * This method creates and dispatches a trusted event.
   * If aTarget is not a chrome object, the nearest chrome object in the
   * propagation path will be used as the start of the event target chain.
   * This method is different than DispatchChromeEvent, which always dispatches
   * events to chrome event handler. DispatchEventOnlyToChrome works like
   * DispatchTrustedEvent in the case aTarget is a chrome object.
   * Works only with events which can be created by calling
   * Document::CreateEvent() with parameter "Events".
   * @param aDoc           The document which will be used to create the event.
   * @param aTarget        The target of the event.
   * @param aEventName     The name of the event.
   * @param aCanBubble     Whether the event can bubble.
   * @param aCancelable    Is the event cancelable.
   * @param aComposed      Is the event composed.
   * @param aDefaultAction Set to true if default action should be taken,
   *                       see EventTarget::DispatchEvent.
   */
  static nsresult DispatchEventOnlyToChrome(
      Document* aDoc, mozilla::dom::EventTarget* aTarget,
      const nsAString& aEventName, CanBubble, Cancelable,
      Composed aComposed = Composed::eDefault, bool* aDefaultAction = nullptr);

  static nsresult DispatchEventOnlyToChrome(Document* aDoc,
                                            mozilla::dom::EventTarget* aTarget,
                                            const nsAString& aEventName,
                                            CanBubble aCanBubble,
                                            Cancelable aCancelable,
                                            bool* aDefaultAction) {
    return DispatchEventOnlyToChrome(aDoc, aTarget, aEventName, aCanBubble,
                                     aCancelable, Composed::eDefault,
                                     aDefaultAction);
  }

  /**
   * Determines if an event attribute name (such as onclick) is valid for
   * a given element type. Types are from the EventNameType enumeration
   * defined above.
   *
   * @param aName the event name to look up
   * @param aType the type of content
   */
  static bool IsEventAttributeName(nsAtom* aName, int32_t aType);

  /**
   * Return the event message for the event with the given name. The name is
   * the event name with the 'on' prefix. Returns eUnidentifiedEvent if the
   * event doesn't match a known event name.
   *
   * @param aName the event name to look up
   */
  static EventMessage GetEventMessage(nsAtom* aName);

  /**
   * Iterate through all event attribute names (such as onclick) that
   * are valid for a given element type. Types are from the EventNameType
   * enumeration defined above.
   *
   * @param aType the type of content
   * @param aFunc iterator functor
   */
  static void ForEachEventAttributeName(
      int32_t aType, const mozilla::FunctionRef<void(nsAtom*)> aFunc);

  /**
   * Return the event type atom for a given event message.
   */
  static nsAtom* GetEventTypeFromMessage(EventMessage aEventMessage);

  /**
   * Return the event type atom from a given event.
   */
  static already_AddRefed<nsAtom> GetEventType(
      const mozilla::WidgetEvent* aEvent);

  /**
   * Returns the EventMessage and nsAtom to be used for event listener
   * registration.
   */
  static EventMessage GetEventMessageAndAtomForListener(const nsAString& aName,
                                                        nsAtom** aOnName);

  /**
   * Return the EventClassID for the event with the given name. The name is the
   * event name *without* the 'on' prefix. Returns eBasicEventClass if the event
   * is not known to be of any particular event class.
   *
   * @param aName the event name to look up
   */
  static mozilla::EventClassID GetEventClassID(const nsAString& aName);

  /**
   * Return the event message and atom for the event with the given name.
   * The name is the event name *without* the 'on' prefix.
   * Returns eUnidentifiedEvent on the aEventID if the
   * event doesn't match a known event name in the category.
   *
   * @param aName the event name to look up
   * @param aEventClassID only return event id for aEventClassID
   */
  static nsAtom* GetEventMessageAndAtom(const nsAString& aName,
                                        mozilla::EventClassID aEventClassID,
                                        EventMessage* aEventMessage);

  /**
   * Used only during traversal of the XPCOM graph by the cycle
   * collector: push a pointer to the listener manager onto the
   * children deque, if it exists. Do nothing if there is no listener
   * manager.
   *
   * Crucially: does not perform any refcounting operations.
   *
   * @param aNode The node to traverse.
   * @param children The buffer to push a listener manager pointer into.
   */
  static void TraverseListenerManager(nsINode* aNode,
                                      nsCycleCollectionTraversalCallback& cb);

  /**
   * Get the eventlistener manager for aNode, creating it if it does not
   * already exist.
   *
   * @param aNode The node for which to get the eventlistener manager.
   */
  static mozilla::EventListenerManager* GetListenerManagerForNode(
      nsINode* aNode);
  /**
   * Get the eventlistener manager for aNode, returning null if it does not
   * already exist.
   *
   * @param aNode The node for which to get the eventlistener manager.
   */
  static mozilla::EventListenerManager* GetExistingListenerManagerForNode(
      const nsINode* aNode);

  static void AddEntryToDOMArenaTable(nsINode* aNode,
                                      mozilla::dom::DOMArena* aDOMArena);

  static already_AddRefed<mozilla::dom::DOMArena> TakeEntryFromDOMArenaTable(
      const nsINode* aNode);

  static void UnmarkGrayJSListenersInCCGenerationDocuments();

  /**
   * Remove the eventlistener manager for aNode.
   *
   * @param aNode The node for which to remove the eventlistener manager.
   */
  static void RemoveListenerManager(nsINode* aNode);

  static bool IsInitialized() { return sInitialized; }

  /**
   * Checks if the localname/prefix/namespace triple is valid wrt prefix
   * and namespace according to the Namespaces in XML and DOM Code
   * specfications.
   *
   * @param aLocalname localname of the node
   * @param aPrefix prefix of the node
   * @param aNamespaceID namespace of the node
   */
  static bool IsValidNodeName(nsAtom* aLocalName, nsAtom* aPrefix,
                              int32_t aNamespaceID);

  /**
   * Creates a DocumentFragment from text using a context node to resolve
   * namespaces.
   *
   * Please note that for safety reasons, if the node principal of
   * aContextNode is the system principal, this function will automatically
   * sanitize its input using nsTreeSanitizer.
   *
   * Note! In the HTML case with the HTML5 parser enabled, this is only called
   * from Range.createContextualFragment() and the implementation here is
   * quirky accordingly (html context node behaves like a body context node).
   * If you don't want that quirky behavior, don't use this method as-is!
   *
   * @param aContextNode the node which is used to resolve namespaces
   * @param aFragment the string which is parsed to a DocumentFragment
   * @param aReturn the resulting fragment
   * @param aPreventScriptExecution whether to mark scripts as already started
   */
  static already_AddRefed<mozilla::dom::DocumentFragment>
  CreateContextualFragment(nsINode* aContextNode, const nsAString& aFragment,
                           bool aPreventScriptExecution,
                           mozilla::ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT
  static void SetHTMLUnsafe(mozilla::dom::FragmentOrElement* aTarget,
                            Element* aContext,
                            const mozilla::dom::TrustedHTMLOrString& aSource,
                            bool aIsShadowRoot, mozilla::ErrorResult& aError);
  /**
   * Invoke the fragment parsing algorithm (innerHTML) using the HTML parser.
   *
   * Please note that for safety reasons, if the node principal of aTargetNode
   * is the system principal, this function will automatically sanitize its
   * input using nsTreeSanitizer.
   *
   * @param aSourceBuffer the string being set as innerHTML
   * @param aTargetNode the target container
   * @param aContextLocalName local name of context node
   * @param aContextNamespace namespace of context node
   * @param aQuirks true to make <table> not close <p>
   * @param aPreventScriptExecution true to prevent scripts from executing;
   *        don't set to false when parsing into a target node that has been
   *        bound to tree.
   * @return NS_ERROR_DOM_INVALID_STATE_ERR if a re-entrant attempt to parse
   *         fragments is made, NS_ERROR_OUT_OF_MEMORY if aSourceBuffer is too
   *         long and NS_OK otherwise.
   * @param aFlags defaults to -1 indicating that ParseFragmentHTML will do
   *        default sanitization for system privileged calls to it. Only
   *        ParserUtils::ParseFragment() should ever pass explicit aFlags
   *        which will then used for sanitization of the fragment.
   *        To pass explicit aFlags use any of the sanitization flags
   *        listed in nsIParserUtils.idl.
   */
  static nsresult ParseFragmentHTML(const nsAString& aSourceBuffer,
                                    nsIContent* aTargetNode,
                                    nsAtom* aContextLocalName,
                                    int32_t aContextNamespace, bool aQuirks,
                                    bool aPreventScriptExecution,
                                    int32_t aFlags = -1);

  /**
   * Invoke the fragment parsing algorithm (innerHTML) using the XML parser.
   *
   * Please note that for safety reasons, if the node principal of aDocument
   * is the system principal, this function will automatically sanitize its
   * input using nsTreeSanitizer.
   *
   * @param aSourceBuffer the string being set as innerHTML
   * @param aDocument the target document
   * @param aTagStack the namespace mapping context
   * @param aPreventExecution whether to mark scripts as already started
   * @param aFlags, pass -1 and ParseFragmentXML will do default
   *        sanitization for system privileged calls to it. Only
   *        ParserUtils::ParseFragment() should ever pass explicit aFlags
   *        which will then used for sanitization of the fragment.
   *        To pass explicit aFlags use any of the sanitization flags
   *        listed in nsIParserUtils.idl.
   * @param aReturn the result fragment
   * @return NS_ERROR_DOM_INVALID_STATE_ERR if a re-entrant attempt to parse
   *         fragments is made, a return code from the XML parser.
   */
  static nsresult ParseFragmentXML(const nsAString& aSourceBuffer,
                                   Document* aDocument,
                                   nsTArray<nsString>& aTagStack,
                                   bool aPreventScriptExecution, int32_t aFlags,
                                   mozilla::dom::DocumentFragment** aReturn);

  /**
   * Parse a string into a document using the HTML parser.
   * Script elements are marked unexecutable.
   *
   * @param aSourceBuffer the string to parse as an HTML document
   * @param aTargetDocument the document object to parse into. Must not have
   *                        child nodes.
   * @param aScriptingEnabledForNoscriptParsing whether <noscript> is parsed
   *                                            as if scripting was enabled
   * @return NS_ERROR_DOM_INVALID_STATE_ERR if a re-entrant attempt to parse
   *         fragments is made, NS_ERROR_OUT_OF_MEMORY if aSourceBuffer is too
   *         long and NS_OK otherwise.
   */
  static nsresult ParseDocumentHTML(const nsAString& aSourceBuffer,
                                    Document* aTargetDocument,
                                    bool aScriptingEnabledForNoscriptParsing);

  /**
   * Converts HTML source to plain text by parsing the source and using the
   * plain text serializer on the resulting tree.
   *
   * @param aSourceBuffer the string to parse as an HTML document
   * @param aResultBuffer the string where the plain text result appears;
   *                      may be the same string as aSourceBuffer
   * @param aFlags Flags from nsIDocumentEncoder.
   * @param aWrapCol Number of columns after which to line wrap; 0 for no
   *                 auto-wrapping
   * @return NS_ERROR_DOM_INVALID_STATE_ERR if a re-entrant attempt to parse
   *         fragments is made, NS_ERROR_OUT_OF_MEMORY if aSourceBuffer is too
   *         long and NS_OK otherwise.
   */
  static nsresult ConvertToPlainText(const nsAString& aSourceBuffer,
                                     nsAString& aResultBuffer, uint32_t aFlags,
                                     uint32_t aWrapCol);

  /**
   * Creates a 'loaded-as-data' HTML document that takes that principal,
   * script global, and URL from the argument, which may be null.
   */
  static already_AddRefed<Document> CreateInertHTMLDocument(
      const Document* aTemplate);

  /**
   * Creates a 'loaded-as-data' XML document that takes that principal,
   * script global, and URL from the argument, which may be null.
   */
  static already_AddRefed<Document> CreateInertXMLDocument(
      const Document* aTemplate);

 public:
  /**
   * Sets the text contents of a node by replacing all existing children
   * with a single text child.
   *
   * The function always notifies.
   *
   * Will reuse the first text child if one is available. Will not reuse
   * existing cdata children.
   *
   * TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
   *
   * @param aContent Node to set contents of.
   * @param aValue   Value to set contents to.
   * @param aTryReuse When true, the function will try to reuse an existing
   *                  textnodes rather than always creating a new one.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY static nsresult SetNodeTextContent(
      nsIContent* aContent, const nsAString& aValue, bool aTryReuse);

  /**
   * Get the textual contents of a node. This is a concatenation of all
   * textnodes that are direct or (depending on aDeep) indirect children
   * of the node.
   *
   * NOTE! No serialization takes place and <br> elements
   * are not converted into newlines. Only textnodes and cdata nodes are
   * added to the result.
   *
   * @see nsLayoutUtils::GetFrameTextContent
   *
   * @param aNode Node to get textual contents of.
   * @param aDeep If true child elements of aNode are recursivly descended
   *              into to find text children.
   * @param aResult the result. Out param.
   * @return false on out of memory errors, true otherwise.
   */
  [[nodiscard]] static bool GetNodeTextContent(const nsINode* aNode, bool aDeep,
                                               nsAString& aResult,
                                               const mozilla::fallible_t&);

  static void GetNodeTextContent(const nsINode* aNode, bool aDeep,
                                 nsAString& aResult);

  /**
   * Same as GetNodeTextContents but appends the result rather than sets it.
   */
  static bool AppendNodeTextContent(const nsINode* aNode, bool aDeep,
                                    nsAString& aResult,
                                    const mozilla::fallible_t&);

  /**
   * Utility method that checks if a given node has any non-empty children. This
   * method does not descend recursively into children by default.
   *
   * @param aDiscoverMode Set to eRecurseIntoChildren to descend recursively
   * into children.
   */
  enum TextContentDiscoverMode : uint8_t {
    eRecurseIntoChildren,
    eDontRecurseIntoChildren
  };

  static bool HasNonEmptyTextContent(
      nsINode* aNode,
      TextContentDiscoverMode aDiscoverMode = eDontRecurseIntoChildren);

  /**
   * Delete strings allocated for nsContentList matches
   */
  static void DestroyMatchString(void* aData);

  /*
   * Notify when the first XUL menu is opened and when the all XUL menus are
   * closed. At opening, aInstalling should be TRUE, otherwise, it should be
   * FALSE.
   */
  MOZ_CAN_RUN_SCRIPT static void NotifyInstalledMenuKeyboardListener(
      bool aInstalling);

  /**
   * Check whether the nsIURI uses the given scheme.
   *
   * Note that this will check the innermost URI rather than that of
   * the nsIURI itself.
   */
  static bool SchemeIs(nsIURI* aURI, const char* aScheme);

  /**
   * Returns true if aPrincipal is an ExpandedPrincipal.
   */
  static bool IsExpandedPrincipal(nsIPrincipal* aPrincipal);

  /**
   * Returns true if aPrincipal is the system or an ExpandedPrincipal.
   */
  static bool IsSystemOrExpandedPrincipal(nsIPrincipal* aPrincipal);

  /**
   * Gets the system principal from the security manager.
   */
  static nsIPrincipal* GetSystemPrincipal();

  /**
   * Gets the null subject principal singleton. This is only useful for
   * assertions.
   */
  static nsIPrincipal* GetNullSubjectPrincipal() {
    return sNullSubjectPrincipal;
  }

  /**
   * Gets the about:fingerprintingprotection principal.
   */
  static nsIPrincipal* GetFingerprintingProtectionPrincipal() {
    return sFingerprintingProtectionPrincipal;
  }

  /**
   * *aResourcePrincipal is a principal describing who may access the contents
   * of a resource. The resource can only be consumed by a principal that
   * subsumes *aResourcePrincipal. MAKE SURE THAT NOTHING EVER ACTS WITH THE
   * AUTHORITY OF *aResourcePrincipal.
   * It may be null to indicate that the resource has no data from any origin
   * in it yet and anything may access the resource.
   * Additional data is being mixed into the resource from aExtraPrincipal
   * (which may be null; if null, no data is being mixed in and this function
   * will do nothing). Update *aResourcePrincipal to reflect the new data.
   * If *aResourcePrincipal subsumes aExtraPrincipal, nothing needs to change,
   * otherwise *aResourcePrincipal is replaced with the system principal.
   * Returns true if *aResourcePrincipal changed.
   */
  static bool CombineResourcePrincipals(
      nsCOMPtr<nsIPrincipal>* aResourcePrincipal,
      nsIPrincipal* aExtraPrincipal);

  /**
   * Trigger a link with uri aLinkURI. If aClick is false, this triggers a
   * mouseover on the link, otherwise it triggers a load after doing a
   * security check using aContent's principal.
   *
   * @param aContent the node on which a link was triggered.
   * @param aLinkURI the URI of the link, must be non-null.
   * @param aTargetSpec the target (like target=, may be empty).
   * @param aClick whether this was a click or not (if false, this method
   *               assumes you just hovered over the link).
   */
  static void TriggerLink(nsIContent* aContent, nsIURI* aLinkURI,
                          const nsString& aTargetSpec, bool aClick);

  /**
   * Get the link location.
   */
  static void GetLinkLocation(mozilla::dom::Element* aElement,
                              nsString& aLocationString);

  /**
   * Return top-level widget in the parent chain.
   */
  static nsIWidget* GetTopLevelWidget(nsIWidget* aWidget);

  /**
   * Return the localized ellipsis for UI.
   */
  static const nsDependentString GetLocalizedEllipsis();

  /**
   * Hide any XUL popups associated with aDocument, including any documents
   * displayed in child frames. Does nothing if aDocument is null.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY static void HidePopupsInDocument(
      Document* aDocument);

  /**
   * Retrieve the current drag session, or null if no drag is currently occuring
   */
  static already_AddRefed<nsIDragSession> GetDragSession(nsIWidget* aWidget);

  static already_AddRefed<nsIDragSession> GetDragSession(nsPresContext* aPC);

  /*
   * Initialize and set the dataTransfer field of an WidgetDragEvent.
   */
  static nsresult SetDataTransferInEvent(mozilla::WidgetDragEvent* aDragEvent);

  // filters the drag and drop action to fit within the effects allowed and
  // returns it.
  static uint32_t FilterDropEffect(uint32_t aAction, uint32_t aEffectAllowed);

  /*
   * Return true if the target of a drop event is a content document that is
   * an ancestor of the document for the source of the drag.
   */
  static bool CheckForSubFrameDrop(nsIDragSession* aDragSession,
                                   mozilla::WidgetDragEvent* aDropEvent);

  /**
   * Return true if aURI is a local file URI (i.e. file://).
   */
  static bool URIIsLocalFile(nsIURI* aURI);

  /**
   * Get the application manifest URI for this document.  The manifest URI
   * is specified in the manifest= attribute of the root element of the
   * document.
   *
   * @param aDocument The document that lists the manifest.
   * @param aURI The manifest URI.
   */
  static void GetOfflineAppManifest(Document* aDocument, nsIURI** aURI);

  /**
   * Check whether an application should be allowed to use offline APIs.
   */
  static bool OfflineAppAllowed(nsIURI* aURI);

  /**
   * Check whether an application should be allowed to use offline APIs.
   */
  static bool OfflineAppAllowed(nsIPrincipal* aPrincipal);

  /**
   * Increases the count of blockers preventing scripts from running.
   * NOTE: You might want to use nsAutoScriptBlocker rather than calling
   * this directly
   */
  static void AddScriptBlocker();

  /**
   * Decreases the count of blockers preventing scripts from running.
   * NOTE: You might want to use nsAutoScriptBlocker rather than calling
   * this directly
   *
   * WARNING! Calling this function could synchronously execute scripts.
   */
  static void RemoveScriptBlocker();

  /**
   * Add a runnable that is to be executed as soon as it's safe to execute
   * scripts.
   * NOTE: If it's currently safe to execute scripts, aRunnable will be run
   *       synchronously before the function returns.
   *
   * @param aRunnable  The nsIRunnable to run as soon as it's safe to execute
   *                   scripts. Passing null is allowed and results in nothing
   *                   happening. It is also allowed to pass an object that
   *                   has not yet been AddRefed.
   */
  static void AddScriptRunner(already_AddRefed<nsIRunnable> aRunnable);
  static void AddScriptRunner(nsIRunnable* aRunnable);

  /**
   * Returns true if it's safe to execute content script and false otherwise.
   *
   * The only known case where this lies is mutation events. They run, and can
   * run anything else, when this function returns false, but this is ok.
   */
  static bool IsSafeToRunScript();

  // Returns the browser window with the most recent time stamp that is
  // not in private browsing mode.
  static already_AddRefed<nsPIDOMWindowOuter> GetMostRecentNonPBWindow();

  /**
   * Call this function if !IsSafeToRunScript() and we fail to run the script
   * (rather than using AddScriptRunner as we usually do). |aDocument| is
   * optional as it is only used for showing the URL in the console.
   */
  static void WarnScriptWasIgnored(Document* aDocument);

  /**
   * Add a "synchronous section", in the form of an nsIRunnable run once the
   * event loop has reached a "stable state". |aRunnable| must not cause any
   * queued events to be processed (i.e. must not spin the event loop).
   * We've reached a stable state when the currently executing task/event has
   * finished, see
   * http://www.whatwg.org/specs/web-apps/current-work/multipage/webappapis.html#synchronous-section
   * In practice this runs aRunnable once the currently executing event
   * finishes. If called multiple times per task/event, all the runnables will
   * be executed, in the order in which RunInStableState() was called.
   */
  static void RunInStableState(already_AddRefed<nsIRunnable> aRunnable);

  /* Add a pending IDBTransaction to be cleaned up at the end of performing a
   * microtask checkpoint.
   * See the step of "Cleanup Indexed Database Transactions" in
   * https://html.spec.whatwg.org/multipage/webappapis.html#perform-a-microtask-checkpoint
   */
  static void AddPendingIDBTransaction(
      already_AddRefed<nsIRunnable> aTransaction);

  /**
   * Returns true if we are doing StableState/MetastableState.
   */
  static bool IsInStableOrMetaStableState();

  static JSContext* GetCurrentJSContext();

  /**
   * Case insensitive comparison between two atoms.
   */
  static bool EqualsIgnoreASCIICase(nsAtom* aAtom1, nsAtom* aAtom2);

  /**
   * Case insensitive comparison between two strings. However it only ignores
   * case for ASCII characters a-z.
   */
  static bool EqualsIgnoreASCIICase(const nsAString& aStr1,
                                    const nsAString& aStr2);

  /**
   * Convert ASCII A-Z to a-z.
   */
  static void ASCIIToLower(nsAString& aStr);
  static void ASCIIToLower(nsACString& aStr);
  static void ASCIIToLower(const nsAString& aSource, nsAString& aDest);
  static void ASCIIToLower(const nsACString& aSource, nsACString& aDest);

  /**
   * Convert ASCII a-z to A-Z.
   */
  static void ASCIIToUpper(nsAString& aStr);
  static void ASCIIToUpper(nsACString& aStr);
  static void ASCIIToUpper(const nsAString& aSource, nsAString& aDest);
  static void ASCIIToUpper(const nsACString& aSource, nsACString& aDest);

  /**
   * Return whether aStr contains an ASCII uppercase character.
   */
  static bool StringContainsASCIIUpper(const nsAString& aStr);

  // Returns NS_OK for same origin, error (NS_ERROR_DOM_BAD_URI) if not.
  static nsresult CheckSameOrigin(nsIChannel* aOldChannel,
                                  nsIChannel* aNewChannel);
  static nsIInterfaceRequestor* SameOriginChecker();

  /**
   * Returns an ASCII compatible serialization of the nsIPrincipal or nsIURI's
   * origin, as specified by the whatwg HTML specification.  If the principal
   * does not have a host, the origin will be "null".
   *
   * https://html.spec.whatwg.org/multipage/browsers.html#ascii-serialisation-of-an-origin
   *
   * Note that this is different from nsIPrincipal::GetOrigin, does not contain
   * gecko-specific metadata like origin attributes, and should not be used for
   * permissions or security checks.
   *
   * See also `nsIPrincipal::GetWebExposedOriginSerialization`.
   *
   * These methods are thread-safe.
   *
   * @pre aPrincipal/aURI must not be null.
   *
   * @note this should be used for HTML5 origin determination.
   */
  static nsresult GetWebExposedOriginSerialization(nsIURI* aURI,
                                                   nsACString& aOrigin);
  static nsresult GetWebExposedOriginSerialization(nsIPrincipal* aPrincipal,
                                                   nsAString& aOrigin);
  static nsresult GetWebExposedOriginSerialization(nsIURI* aURI,
                                                   nsAString& aOrigin);

  /**
   * This method creates and dispatches "command" event, which implements
   * XULCommandEvent.
   * If aPresShell is not null, dispatching goes via
   * PresShell::HandleDOMEventWithTarget().
   */
  MOZ_CAN_RUN_SCRIPT
  static nsresult DispatchXULCommand(
      nsIContent* aTarget, bool aTrusted,
      mozilla::dom::Event* aSourceEvent = nullptr,
      mozilla::PresShell* aPresShell = nullptr, bool aCtrl = false,
      bool aAlt = false, bool aShift = false, bool aMeta = false,
      // Including MouseEventBinding here leads
      // to incude loops, unfortunately.
      uint16_t inputSource = 0 /* MouseEvent_Binding::MOZ_SOURCE_UNKNOWN */,
      int16_t aButton = 0);

  static bool CheckMayLoad(nsIPrincipal* aPrincipal, nsIChannel* aChannel,
                           bool aAllowIfInheritsPrincipal);

  /**
   * The method checks whether the caller can access native anonymous content.
   * If there is no JS in the stack or privileged JS is running, this
   * method returns true, otherwise false.
   */
  static bool CanAccessNativeAnon();

  [[nodiscard]] static nsresult WrapNative(JSContext* cx, nsISupports* native,
                                           const nsIID* aIID,
                                           JS::MutableHandle<JS::Value> vp,
                                           bool aAllowWrapping = true) {
    return WrapNative(cx, native, nullptr, aIID, vp, aAllowWrapping);
  }

  // Same as the WrapNative above, but use this one if aIID is nsISupports' IID.
  [[nodiscard]] static nsresult WrapNative(JSContext* cx, nsISupports* native,
                                           JS::MutableHandle<JS::Value> vp,
                                           bool aAllowWrapping = true) {
    return WrapNative(cx, native, nullptr, nullptr, vp, aAllowWrapping);
  }

  [[nodiscard]] static nsresult WrapNative(JSContext* cx, nsISupports* native,
                                           nsWrapperCache* cache,
                                           JS::MutableHandle<JS::Value> vp,
                                           bool aAllowWrapping = true) {
    return WrapNative(cx, native, cache, nullptr, vp, aAllowWrapping);
  }

  static void StripNullChars(const nsAString& aInStr, nsAString& aOutStr);

  /**
   * Strip all \n, \r and nulls from the given string
   * @param aString the string to remove newlines from [in/out]
   */
  static void RemoveNewlines(nsString& aString);

  /**
   * Convert Windows and Mac platform linebreaks to \n.
   * @param aString the string to convert the newlines inside [in/out]
   */
  static void PlatformToDOMLineBreaks(nsString& aString);
  [[nodiscard]] static bool PlatformToDOMLineBreaks(nsString& aString,
                                                    const mozilla::fallible_t&);

  static bool IsHandlingKeyBoardEvent() { return sIsHandlingKeyBoardEvent; }

  static void SetIsHandlingKeyBoardEvent(bool aHandling) {
    sIsHandlingKeyBoardEvent = aHandling;
  }

  /**
   * Utility method for getElementsByClassName.  aRootNode is the node (either
   * document or element), which getElementsByClassName was called on.
   */
  static already_AddRefed<nsContentList> GetElementsByClassName(
      nsINode* aRootNode, const nsAString& aClasses);

  /**
   * Returns a presshell for this document, if there is one. This will be
   * aDoc's direct presshell if there is one, otherwise we'll look at all
   * ancestor documents to try to find a presshell, so for example this can
   * still find a presshell for documents in display:none frames that have
   * no presentation. So you have to be careful how you use this presshell ---
   * getting generic data like a device context or widget from it is OK, but it
   * might not be this document's actual presentation.
   */
  static mozilla::PresShell* FindPresShellForDocument(
      const Document* aDocument);

  /**
   * Like FindPresShellForDocument, but returns the shell's PresContext instead.
   */
  static nsPresContext* FindPresContextForDocument(const Document* aDocument);

  /**
   * Returns the widget for this document if there is one. Looks at all ancestor
   * documents to try to find a widget, so for example this can still find a
   * widget for documents in display:none frames that have no presentation.
   *
   * You should probably use WidgetForContent() instead of this, unless you have
   * a good reason to do otherwise.
   */
  static nsIWidget* WidgetForDocument(const Document* aDocument);

  /**
   * Returns the appropriate widget for this element, if there is one. Unlike
   * WidgetForDocument(), this returns the correct widget for content in popups.
   *
   * You should probably use this instead of WidgetForDocument().
   */
  static nsIWidget* WidgetForContent(const nsIContent* aContent);

  /**
   * Returns a window renderer to use for the given document. Basically we
   * look up the document hierarchy for the first document which has
   * a presentation with an associated widget, and use that widget's
   * window renderer.
   *
   * You should probably use WindowRendererForContent() instead of this, unless
   * you have a good reason to do otherwise.
   *
   * @param aDoc the document for which to return a window renderer.
   * @param aAllowRetaining an outparam that states whether the returned
   * layer manager should be used for retained layers
   */
  static mozilla::WindowRenderer* WindowRendererForDocument(
      const Document* aDoc);

  /**
   * Returns a window renderer to use for the given content. Unlike
   * WindowRendererForDocument(), this returns the correct window renderer for
   * content in popups.
   *
   * You should probably use this instead of WindowRendererForDocument().
   */
  static mozilla::WindowRenderer* WindowRendererForContent(
      const nsIContent* aContent);

  /**
   * Returns true if calling execCommand with 'cut' or 'copy' arguments is
   * allowed for the given subject principal. These are only allowed if the user
   * initiated them (like with a mouse-click or key press).
   */
  static bool IsCutCopyAllowed(Document* aDocument,
                               nsIPrincipal& aSubjectPrincipal);

  /**
   * Returns true if CSSOM origin check should be skipped for WebDriver
   * based crawl to be able to collect data from cross-origin CSS style
   * sheets. This can be enabled by setting environment variable
   * MOZ_BYPASS_CSSOM_ORIGIN_CHECK.
   */
  static bool BypassCSSOMOriginCheck() {
#ifdef RELEASE_OR_BETA
    return false;
#else
    return sBypassCSSOMOriginCheck;
#endif
  }

  /**
   * Fire mutation events for changes caused by parsing directly into a
   * context node.
   *
   * @param aDoc the document of the node
   * @param aDest the destination node that got stuff appended to it
   * @param aOldChildCount the number of children the node had before parsing
   */
  static void FireMutationEventsForDirectParsing(Document* aDoc,
                                                 nsIContent* aDest,
                                                 int32_t aOldChildCount);

  /**
   * Returns the in-process subtree root document in a document hierarchy.
   * This could be a chrome document.
   */
  static Document* GetInProcessSubtreeRootDocument(Document* aDoc) {
    return const_cast<Document*>(
        GetInProcessSubtreeRootDocument(const_cast<const Document*>(aDoc)));
  }
  static const Document* GetInProcessSubtreeRootDocument(const Document* aDoc);

  static void GetShiftText(nsAString& text);
  static void GetControlText(nsAString& text);
  static void GetCommandOrWinText(nsAString& text);
  static void GetAltText(nsAString& text);
  static void GetModifierSeparatorText(nsAString& text);

  /**
   * Flushes the layout tree (recursively)
   *
   * @param aWindow the window the flush should start at
   *
   */
  static void FlushLayoutForTree(nsPIDOMWindowOuter* aWindow);

  /**
   * Returns true if content with the given principal is allowed to use XUL
   * and XBL and false otherwise.
   */
  static bool AllowXULXBLForPrincipal(nsIPrincipal* aPrincipal);

  /**
   * Perform cleanup that's appropriate for XPCOM shutdown.
   */
  static void XPCOMShutdown();

  /**
   * Checks if internal PDF viewer is enabled.
   */
  static bool IsPDFJSEnabled();

  /**
   * Checks to see whether the given principal is the internal PDF
   * viewer principal.
   */
  static bool IsPDFJS(nsIPrincipal* aPrincipal);
  /**
   * Same, but from WebIDL bindings. Checks whether the subject principal is for
   * the internal PDF viewer or system JS.
   */
  static bool IsSystemOrPDFJS(JSContext*, JSObject*);

  /**
   * Checks if the given JSContext is secure or if the subject principal is
   * either an addon principal or an expanded principal, which contains at least
   * one addon principal.
   */
  static bool IsSecureContextOrWebExtension(JSContext*, JSObject*);

  enum DocumentViewerType {
    TYPE_UNSUPPORTED,
    TYPE_CONTENT,
    TYPE_FALLBACK,
    TYPE_UNKNOWN
  };

  static already_AddRefed<nsIDocumentLoaderFactory> FindInternalDocumentViewer(
      const nsACString& aType, DocumentViewerType* aLoaderType = nullptr);

  /**
   * This helper method returns true if the aPattern pattern matches aValue.
   * aPattern should not contain leading and trailing slashes (/).
   * The pattern has to match the entire value not just a subset.
   * aDocument must be a valid pointer (not null).
   *
   * This is following the HTML5 specification:
   * http://dev.w3.org/html5/spec/forms.html#attr-input-pattern
   *
   * WARNING: This method mutates aPattern!
   *
   * @param aValue       the string to check.
   * @param aPattern     the string defining the pattern.
   * @param aDocument    the owner document of the element.
   * @param aHasMultiple whether or not there are multiple values.
   * @param aFlags       the flags to use for creating the regexp object.
   * @result             whether the given string is matches the pattern, or
   *                     Nothing() if the pattern couldn't be evaluated.
   */
  static mozilla::Maybe<bool> IsPatternMatching(
      const nsAString& aValue, nsString&& aPattern, const Document* aDocument,
      bool aHasMultiple = false,
      JS::RegExpFlags aFlags = JS::RegExpFlag::UnicodeSets);

  /**
   * Calling this adds support for
   * ontouch* event handler DOM attributes.
   */
  static void InitializeTouchEventTable();

  /**
   * Test whether the given URI always inherits a security context
   * from the document it comes from.
   */
  static nsresult URIInheritsSecurityContext(nsIURI* aURI, bool* aResult);

  /**
   * Called before a channel is created to query whether the new
   * channel should inherit the principal.
   *
   * The argument aLoadingPrincipal must not be null. The argument
   * aURI must be the URI of the new channel. If aInheritForAboutBlank
   * is true, then about:blank will be told to inherit the principal.
   * If aForceInherit is true, the new channel will be told to inherit
   * the principal no matter what.
   *
   * The return value is whether the new channel should inherit
   * the principal.
   */
  static bool ChannelShouldInheritPrincipal(nsIPrincipal* aLoadingPrincipal,
                                            nsIURI* aURI,
                                            bool aInheritForAboutBlank,
                                            bool aForceInherit);

  static nsresult Btoa(const nsAString& aBinaryData,
                       nsAString& aAsciiBase64String);

  static nsresult Atob(const nsAString& aAsciiString, nsAString& aBinaryData);

  /**
   * Returns whether the input element passed in parameter has the autocomplete
   * functionality enabled. It is taking into account the form owner.
   * NOTE: the caller has to make sure autocomplete makes sense for the
   * element's type.
   *
   * @param aElement the input or textarea element to check. NOTE: aElement
   * can't be null.
   * @return whether the input element has autocomplete enabled.
   */
  static bool IsAutocompleteEnabled(mozilla::dom::Element* aElement);

  enum AutocompleteAttrState : uint8_t {
    eAutocompleteAttrState_Unknown = 1,
    eAutocompleteAttrState_Invalid,
    eAutocompleteAttrState_Valid,
  };
  /**
   * Parses the value of the autocomplete attribute into aResult, ensuring it's
   * composed of valid tokens, otherwise the value "" is used.
   * Note that this method is used for form fields, not on a <form> itself.
   *
   * @return whether aAttr was valid and can be cached.
   */
  static AutocompleteAttrState SerializeAutocompleteAttribute(
      const nsAttrValue* aAttr, nsAString& aResult,
      AutocompleteAttrState aCachedState = eAutocompleteAttrState_Unknown);

  /* Variation that is used to retrieve a dictionary of the parts of the
   * autocomplete attribute.
   *
   * @return whether aAttr was valid and can be cached.
   */
  static AutocompleteAttrState SerializeAutocompleteAttribute(
      const nsAttrValue* aAttr, mozilla::dom::AutocompleteInfo& aInfo,
      AutocompleteAttrState aCachedState = eAutocompleteAttrState_Unknown,
      bool aGrantAllValidValue = false);

  /**
   * This will parse aSource, to extract the value of the pseudo attribute
   * with the name specified in aName. See
   * http://www.w3.org/TR/xml-stylesheet/#NT-StyleSheetPI for the specification
   * which is used to parse aSource.
   *
   * @param aSource the string to parse
   * @param aName the name of the attribute to get the value for
   * @param aValue [out] the value for the attribute with name specified in
   *                     aAttribute. Empty if the attribute isn't present.
   * @return true     if the attribute exists and was successfully parsed.
   *         false if the attribute doesn't exist, or has a malformed
   *                  value, such as an unknown or unterminated entity.
   */
  static bool GetPseudoAttributeValue(const nsString& aSource, nsAtom* aName,
                                      nsAString& aValue);

  /**
   * Returns true if the language name is a version of JavaScript and
   * false otherwise
   */
  static bool IsJavaScriptLanguage(const nsString& aName);

  static bool IsJavascriptMIMEType(const nsAString& aMIMEType);
  static bool IsJavascriptMIMEType(const nsACString& aMIMEType);

  /**
   * Returns true if the given MIME type string is a valid JSON MIME type,
   * otherwise false.
   */
  static bool IsJsonMimeType(const nsAString& aMimeType);

  static void SplitMimeType(const nsAString& aValue, nsString& aType,
                            nsString& aParams);

  /**
   * Check whether aContent and aOffsetInContent points in a selection range of
   * one of ranges in aSelection.  If aSelection is collapsed, this always
   * return false even if aContent and aOffsetInContent is same as the collapsed
   * position.
   *
   * @param aSelection  The selection you want to check whether point is in a
   *                    range of it.
   * @param aNode       The container node of the point which you want to check.
   * @param aOffset     The offset in aNode of the point which you want to
   *                    check.  aNode and aOffset can be computed with
   *                    UIEvent::GetRangeParentContentAndOffset() if you want to
   *                    check the click point.
   * @param aAllowCrossShadowBoundary If true, this method allows the selection
   *                                  to have boundaries that cross shadow
   *                                  boundaries.
   */
  static bool IsPointInSelection(const mozilla::dom::Selection& aSelection,
                                 const nsINode& aNode, const uint32_t aOffset,
                                 const bool aAllowCrossShadowBoundary = false);

  /**
   * Takes a selection, and a text control element (<input> or <textarea>), and
   * returns the offsets in the text content corresponding to the selection.
   * The selection's anchor and focus must both be in the root node passed or a
   * descendant.
   *
   * @param aSelection      Selection to check
   * @param aRoot           Root <input> or <textarea> element
   * @param aOutStartOffset Output start offset
   * @param aOutEndOffset   Output end offset
   */
  static void GetSelectionInTextControl(mozilla::dom::Selection* aSelection,
                                        Element* aRoot,
                                        uint32_t& aOutStartOffset,
                                        uint32_t& aOutEndOffset);

  /**
   * Takes a frame for anonymous content within a text control (<input> or
   * <textarea>), and returns an offset in the text content, adjusted for a
   * trailing <br> frame.
   *
   * @param aOffsetFrame      Frame for the text content in which the offset
   *                          lies
   * @param aOffset           Offset as calculated by GetContentOffsetsFromPoint
   * @param aOutOffset        Output adjusted offset
   *
   * @see GetSelectionInTextControl for the original basis of this function.
   */
  static int32_t GetAdjustedOffsetInTextControl(nsIFrame* aOffsetFrame,
                                                int32_t aOffset);

  /**
   * Returns pointer to HTML editor instance for the aPresContext when there is.
   * The HTML editor is shared by contenteditable elements or used in
   * designMode.  When there are no contenteditable elements and the document
   * is not in designMode, this returns nullptr.
   */
  static mozilla::HTMLEditor* GetHTMLEditor(nsPresContext* aPresContext);
  static mozilla::HTMLEditor* GetHTMLEditor(nsDocShell* aDocShell);

  /**
   * Returns pointer to a text editor if <input> or <textarea> element is
   * active element in the document for aPresContext, or pointer to HTML
   * editor if there is (i.e., even if non-editable element has focus or
   * nobody has focus).  The reason is, HTML editor may handle some input
   * even if there is no active editing host.
   * Note that this does not return editor in descendant documents.
   */
  static mozilla::EditorBase* GetActiveEditor(nsPresContext* aPresContext);
  static mozilla::EditorBase* GetActiveEditor(nsPIDOMWindowOuter* aWindow);

  /**
   * Returns `TextEditor` which manages `aAnonymousContent` if there is.
   * Note that this method returns `nullptr` if `TextEditor` for the
   * `aAnonymousContent` hasn't been created yet.
   */
  static mozilla::TextEditor* GetExtantTextEditorFromAnonymousNode(
      const nsIContent* aAnonymousContent);

  /**
   * Returns whether a node has an editable ancestor.
   *
   * @param aNode The node to test.
   */
  static bool IsNodeInEditableRegion(nsINode* aNode);

  /**
   * Returns a LogModule that logs debugging info from RFP functions.
   */
  static mozilla::LogModule* ResistFingerprintingLog();

  /**
   * Returns a LogModule that dump calls from content script are logged to.
   * This can be enabled with the 'Dump' module, and is useful for synchronizing
   * content JS to other logging modules.
   */
  static mozilla::LogModule* DOMDumpLog();

  /**
   * Returns whether a given header is forbidden for an XHR or fetch
   * request.
   */
  static bool IsForbiddenRequestHeader(const nsACString& aHeader,
                                       const nsACString& aValue);

  /**
   * Returns whether a given header is forbidden for a system XHR
   * request.
   */
  static bool IsForbiddenSystemRequestHeader(const nsACString& aHeader);

  /**
   * Checks whether the header overrides any http methods
   */
  static bool IsOverrideMethodHeader(const nsACString& headerName);
  /**
   * Checks whether the  header value contains any forbidden method
   */
  static bool ContainsForbiddenMethod(const nsACString& headerValue);

  class ParsedRange {
   public:
    explicit ParsedRange(mozilla::Maybe<uint64_t> aStart,
                         mozilla::Maybe<uint64_t> aEnd)
        : mStart(aStart), mEnd(aEnd) {}

    mozilla::Maybe<uint64_t> Start() const { return mStart; }
    mozilla::Maybe<uint64_t> End() const { return mEnd; }

    bool operator==(const ParsedRange& aOther) const {
      return Start() == aOther.Start() && End() == aOther.End();
    }

   private:
    mozilla::Maybe<uint64_t> mStart;
    mozilla::Maybe<uint64_t> mEnd;
  };

  /**
   * Parse a single range request and return a pair containing the resulting
   * start and end of the range.
   *
   * See https://fetch.spec.whatwg.org/#simple-range-header-value
   */
  static mozilla::Maybe<ParsedRange> ParseSingleRangeRequest(
      const nsACString& aHeaderValue, bool aAllowWhitespace);

  /**
   * Returns whether a given header has characters that aren't permitted
   */
  static bool IsCorsUnsafeRequestHeaderValue(const nsACString& aHeaderValue);

  /**
   * Returns whether a given Accept header value is allowed
   * for a non-CORS XHR or fetch request.
   */
  static bool IsAllowedNonCorsAccept(const nsACString& aHeaderValue);

  /**
   * Returns whether a given Content-Type header value is allowed
   * for a non-CORS XHR or fetch request.
   */
  static bool IsAllowedNonCorsContentType(const nsACString& aHeaderValue);

  /**
   * Returns whether a given Content-Language or accept-language header value is
   * allowed for a non-CORS XHR or fetch request.
   */
  static bool IsAllowedNonCorsLanguage(const nsACString& aHeaderValue);

  /**
   * Returns whether a given Range header value is allowed for a non-CORS XHR or
   * fetch request.
   */
  static bool IsAllowedNonCorsRange(const nsACString& aHeaderValue);

  /**
   * Returns whether a given header and value is a CORS-safelisted request
   * header per https://fetch.spec.whatwg.org/#cors-safelisted-request-header
   */
  static bool IsCORSSafelistedRequestHeader(const nsACString& aName,
                                            const nsACString& aValue);

  /**
   * Returns whether a given header is forbidden for an XHR or fetch
   * response.
   */
  static bool IsForbiddenResponseHeader(const nsACString& aHeader);

  /**
   * Returns the inner window ID for the window associated with a request.
   */
  static uint64_t GetInnerWindowID(nsIRequest* aRequest);

  /**
   * Returns the inner window ID for the window associated with a load group.
   */
  static uint64_t GetInnerWindowID(nsILoadGroup* aLoadGroup);

  /**
   * Encloses aHost in brackets if it is an IPv6 address.
   */
  static void MaybeFixIPv6Host(nsACString& aHost);

  /**
   * If the hostname for aURI is an IPv6 it encloses it in brackets,
   * otherwise it just outputs the hostname in aHost.
   */
  static nsresult GetHostOrIPv6WithBrackets(nsIURI* aURI, nsAString& aHost);
  static nsresult GetHostOrIPv6WithBrackets(nsIURI* aURI, nsACString& aHost);
  static nsresult GetHostOrIPv6WithBrackets(nsIPrincipal* aPrincipal,
                                            nsACString& aHost);

  /*
   * Call the given callback on all remote children of the given top-level
   * window. Return Callstate::Stop from the callback to stop calling further
   * children.
   */
  static void CallOnAllRemoteChildren(
      nsPIDOMWindowOuter* aWindow,
      const std::function<mozilla::CallState(mozilla::dom::BrowserParent*)>&
          aCallback);

  /**
   * Given an IPCDataTransferImageContainer construct an imgIContainer for the
   * image encoded by the transfer item.
   */
  static nsresult DeserializeTransferableDataImageContainer(
      const mozilla::dom::IPCTransferableDataImageContainer& aData,
      imgIContainer** aContainer);

  /**
   * Given a flavor obtained from an IPCDataTransferItem or nsITransferable,
   * returns true if we should treat the data as an image.
   */
  static bool IsFlavorImage(const nsACString& aFlavor);

  static bool IPCTransferableDataItemHasKnownFlavor(
      const mozilla::dom::IPCTransferableDataItem& aItem);

  static nsresult IPCTransferableDataToTransferable(
      const mozilla::dom::IPCTransferableData& aTransferableData,
      bool aAddDataFlavor, nsITransferable* aTransferable,
      const bool aFilterUnknownFlavors);

  static nsresult IPCTransferableToTransferable(
      const mozilla::dom::IPCTransferable& aIPCTransferable,
      bool aAddDataFlavor, nsITransferable* aTransferable,
      const bool aFilterUnknownFlavors);

  static nsresult IPCTransferableDataItemToVariant(
      const mozilla::dom::IPCTransferableDataItem& aItem,
      nsIWritableVariant* aVariant);

  static void TransferablesToIPCTransferableDatas(
      nsIArray* aTransferables,
      nsTArray<mozilla::dom::IPCTransferableData>& aIPC, bool aInSyncMessage,
      mozilla::dom::ContentParent* aParent);

  static void TransferableToIPCTransferableData(
      nsITransferable* aTransferable,
      mozilla::dom::IPCTransferableData* aTransferableData, bool aInSyncMessage,
      mozilla::dom::ContentParent* aParent);

  static void TransferableToIPCTransferable(
      nsITransferable* aTransferable,
      mozilla::dom::IPCTransferable* aIPCTransferable, bool aInSyncMessage,
      mozilla::dom::ContentParent* aParent);

  /*
   * Get the pixel data from the given source surface and return it as a
   * BigBuffer. The length and stride will be assigned from the surface.
   */
  static mozilla::Maybe<mozilla::ipc::BigBuffer> GetSurfaceData(
      mozilla::gfx::DataSourceSurface&, size_t* aLength, int32_t* aStride);

  static mozilla::Maybe<mozilla::dom::IPCImage> SurfaceToIPCImage(
      mozilla::gfx::DataSourceSurface&);
  static already_AddRefed<mozilla::gfx::DataSourceSurface> IPCImageToSurface(
      const mozilla::dom::IPCImage&);

  // Helpers shared by the implementations of nsContentUtils methods and
  // nsIDOMWindowUtils methods.
  static mozilla::Modifiers GetWidgetModifiers(int32_t aModifiers);
  static nsIWidget* GetWidget(mozilla::PresShell* aPresShell, nsPoint* aOffset);
  static int16_t GetButtonsFlagForButton(int32_t aButton);
  static mozilla::LayoutDeviceIntPoint ToWidgetPoint(
      const mozilla::CSSPoint& aPoint, const nsPoint& aOffset,
      nsPresContext* aPresContext);
  static nsView* GetViewToDispatchEvent(nsPresContext* aPresContext,
                                        mozilla::PresShell** aPresShell);

  /**
   * Synthesize a mouse event to the given widget
   * (see nsIDOMWindowUtils.sendMouseEvent).
   */
  MOZ_CAN_RUN_SCRIPT
  static nsresult SendMouseEvent(
      mozilla::PresShell* aPresShell, nsIWidget* aWidget,
      const nsAString& aType, mozilla::LayoutDeviceIntPoint& aRefPoint,
      int32_t aButton, int32_t aButtons, int32_t aClickCount,
      int32_t aModifiers, bool aIgnoreRootScrollFrame, float aPressure,
      unsigned short aInputSourceArg, uint32_t aIdentifier, bool aToWindow,
      bool* aPreventDefault, bool aIsDOMEventSynthesized,
      bool aIsWidgetEventSynthesized);

  static void FirePageShowEventForFrameLoaderSwap(
      nsIDocShellTreeItem* aItem,
      mozilla::dom::EventTarget* aChromeEventHandler, bool aFireIfShowing,
      bool aOnlySystemGroup = false);

  static void FirePageHideEventForFrameLoaderSwap(
      nsIDocShellTreeItem* aItem,
      mozilla::dom::EventTarget* aChromeEventHandler,
      bool aOnlySystemGroup = false);

  static already_AddRefed<nsPIWindowRoot> GetWindowRoot(Document* aDoc);

  /*
   * If there is a Referrer-Policy response header in |aChannel|, parse a
   * referrer policy from the header.
   *
   * @param the channel from which to get the Referrer-Policy header
   * @return referrer policy from the response header in aChannel
   */
  static mozilla::dom::ReferrerPolicy GetReferrerPolicyFromChannel(
      nsIChannel* aChannel);

  static bool IsNonSubresourceRequest(nsIChannel* aChannel);

  static bool IsNonSubresourceInternalPolicyType(nsContentPolicyType aType);

 public:
  /*
   * Returns true if this window's channel has been marked as a third-party
   * tracking resource.
   */
  static bool IsThirdPartyTrackingResourceWindow(nsPIDOMWindowInner* aWindow);

  /*
   * Returns true if this window's channel has been marked as a first-party
   * tracking resource.
   */
  static bool IsFirstPartyTrackingResourceWindow(nsPIDOMWindowInner* aWindow);

  /*
   * Serializes a HTML nsINode into its markup representation.
   */
  template <SerializeShadowRoots ShouldSerializeShadowRoots =
                SerializeShadowRoots::No>
  static bool SerializeNodeToMarkup(
      nsINode* aRoot, bool aDescendantsOnly, nsAString& aOut,
      bool aSerializableShadowRoots,
      const mozilla::dom::Sequence<
          mozilla::OwningNonNull<mozilla::dom::ShadowRoot>>& aShadowRoots);

  /*
   * Returns true iff the provided JSObject is a global, and its URI matches
   * the provided about: URI.
   * @param aGlobal the JSObject whose URI to check, if it is a global.
   * @param aUri the URI to match, e.g. "about:feeds"
   */
  static bool IsSpecificAboutPage(JSObject* aGlobal, const char* aUri);

  static void SetScrollbarsVisibility(nsIDocShell* aDocShell, bool aVisible);

  /*
   * Try to find the docshell corresponding to the given event target.
   */
  static nsIDocShell* GetDocShellForEventTarget(
      mozilla::dom::EventTarget* aTarget);

  /**
   * Returns true if the "HTTPS state" of the document should be "modern". See:
   *
   * https://html.spec.whatwg.org/#concept-document-https-state
   * https://fetch.spec.whatwg.org/#concept-response-https-state
   */
  static bool HttpsStateIsModern(Document* aDocument);

  /**
   * Returns true if the channel is for top-level window and is over secure
   * context.
   * https://github.com/whatwg/html/issues/4930 tracks the spec side of this.
   */
  static bool ComputeIsSecureContext(nsIChannel* aChannel);

  /**
   * Try to upgrade an element.
   * https://html.spec.whatwg.org/multipage/custom-elements.html#concept-try-upgrade
   */
  static void TryToUpgradeElement(Element* aElement);

  /**
   * Creates a new XUL or XHTML element applying any appropriate custom element
   * definition.
   *
   * If aFromParser != FROM_PARSER_FRAGMENT, a nested event loop permits
   * arbitrary changes to the world before this function returns.  This should
   * probably just be MOZ_CAN_RUN_SCRIPT - bug 1543259.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY static nsresult NewXULOrHTMLElement(
      Element** aResult, mozilla::dom::NodeInfo* aNodeInfo,
      mozilla::dom::FromParser aFromParser, nsAtom* aIsAtom,
      mozilla::dom::CustomElementDefinition* aDefinition);

  static mozilla::dom::CustomElementRegistry* GetCustomElementRegistry(
      Document*);

  /**
   * Looking up a custom element definition.
   * https://html.spec.whatwg.org/#look-up-a-custom-element-definition
   */
  static mozilla::dom::CustomElementDefinition* LookupCustomElementDefinition(
      Document* aDoc, nsAtom* aNameAtom, uint32_t aNameSpaceID,
      nsAtom* aTypeAtom);

  static void RegisterCallbackUpgradeElement(Element* aElement,
                                             nsAtom* aTypeName);

  static void RegisterUnresolvedElement(Element* aElement, nsAtom* aTypeName);
  static void UnregisterUnresolvedElement(Element* aElement);

  static void EnqueueUpgradeReaction(
      Element* aElement, mozilla::dom::CustomElementDefinition* aDefinition);

  static void EnqueueLifecycleCallback(
      mozilla::dom::ElementCallbackType aType, Element* aCustomElement,
      const mozilla::dom::LifecycleCallbackArgs& aArgs,
      mozilla::dom::CustomElementDefinition* aDefinition = nullptr);

  static mozilla::dom::CustomElementFormValue ConvertToCustomElementFormValue(
      const mozilla::dom::Nullable<
          mozilla::dom::OwningFileOrUSVStringOrFormData>& aState);

  static mozilla::dom::Nullable<mozilla::dom::OwningFileOrUSVStringOrFormData>
  ExtractFormAssociatedCustomElementValue(
      nsIGlobalObject* aGlobal,
      const mozilla::dom::CustomElementFormValue& aCEValue);

  /**
   * Appends all "document level" native anonymous content subtree roots for
   * aDocument to aElements.  Document level NAC subtrees are those created
   * by ancestor frames of the document element's primary frame, such as
   * the scrollbar elements created by the root scroll frame.
   */
  static void AppendDocumentLevelNativeAnonymousContentTo(
      Document* aDocument, nsTArray<nsIContent*>& aElements);

  /**
   * Appends all native anonymous content subtree roots generated by `aContent`
   * to `aKids`.
   *
   * See `AllChildrenIterator` for the description of the `aFlags` parameter.
   */
  static void AppendNativeAnonymousChildren(const nsIContent* aContent,
                                            nsTArray<nsIContent*>& aKids,
                                            uint32_t aFlags);

  /**
   * Query triggeringPrincipal if there's a 'triggeringprincipal' attribute on
   * aLoadingNode, if no such attribute is specified, aDefaultPrincipal is
   * returned if it is provided, otherwise the NodePrincipal of aLoadingNode is
   * returned.
   *
   * Return true if aLoadingNode has a 'triggeringprincipal' attribute, and
   * the value 'triggeringprincipal' is also successfully deserialized,
   * otherwise return false.
   */
  static bool QueryTriggeringPrincipal(nsIContent* aLoadingNode,
                                       nsIPrincipal* aDefaultPrincipal,
                                       nsIPrincipal** aTriggeringPrincipal);

  static bool QueryTriggeringPrincipal(nsIContent* aLoadingNode,
                                       nsIPrincipal** aTriggeringPrincipal) {
    return QueryTriggeringPrincipal(aLoadingNode, nullptr,
                                    aTriggeringPrincipal);
  }

  // Returns whether the image for the given URI and triggering principal is
  // already available. Ideally this should exactly match the "list of available
  // images" in the HTML spec, but our implementation of that at best only
  // resembles it.
  static bool IsImageAvailable(nsIContent*, nsIURI*,
                               nsIPrincipal* aDefaultTriggeringPrincipal,
                               mozilla::CORSMode);
  static bool IsImageAvailable(nsIURI*, nsIPrincipal* aTriggeringPrincipal,
                               mozilla::CORSMode, Document*);

  /**
   * Returns the content policy type that should be used for loading images
   * for displaying in the UI.  The sources of such images can be <xul:image>,
   * <xul:menuitem> on OSX where we load the image through nsMenuItemIconX, etc.
   */
  static void GetContentPolicyTypeForUIImageLoading(
      nsIContent* aLoadingNode, nsIPrincipal** aTriggeringPrincipal,
      nsContentPolicyType& aContentPolicyType, uint64_t* aRequestContextID);

  static nsresult CreateJSValueFromSequenceOfObject(
      JSContext* aCx, const mozilla::dom::Sequence<JSObject*>& aTransfer,
      JS::MutableHandle<JS::Value> aValue);

  /**
   * This implements the structured cloning algorithm as described by
   * https://html.spec.whatwg.org/#structured-cloning.
   */
  static void StructuredClone(
      JSContext* aCx, nsIGlobalObject* aGlobal, JS::Handle<JS::Value> aValue,
      const mozilla::dom::StructuredSerializeOptions& aOptions,
      JS::MutableHandle<JS::Value> aRetval, mozilla::ErrorResult& aError);

  /**
   * Returns true if reserved key events should be prevented from being sent
   * to their target. Instead, the key event should be handled by chrome only.
   */
  static bool ShouldBlockReservedKeys(mozilla::WidgetKeyboardEvent* aKeyEvent);

  /**
   * Returns one of the nsIObjectLoadingContent::TYPE_ values describing the
   * content type which will be used for the given MIME type when loaded within
   * an nsObjectLoadingContent.
   *
   * NOTE: This method doesn't take capabilities into account. The caller must
   * take that into account.
   *
   * @param aMIMEType  The MIME type of the document being loaded.
   * @param aIsSandboxed  If the document is loaded in an iframe sandbox.
   */
  static uint32_t HtmlObjectContentTypeForMIMEType(const nsCString& aMIMEType,
                                                   bool aIsSandboxed);

  /**
   * Detect whether a string is a local-url.
   * https://drafts.csswg.org/css-values/#local-urls
   */
  static bool IsLocalRefURL(const nsAString& aString);

  /**
   * Compose a tab id with process id and a serial number.
   */
  static uint64_t GenerateTabId();

  /**
   * Compose a browser id with process id and a serial number.
   */
  static uint64_t GenerateBrowserId();

  /**
   * Generate an id for a BrowsingContext using a range of serial
   * numbers reserved for the current process.
   */
  static uint64_t GenerateBrowsingContextId();

  /**
   * Generate an id using a range of serial numbers reserved for the current
   * process. aId should be a counter that's incremented every time
   * GenerateProcessSpecificId is called.
   */
  static uint64_t GenerateProcessSpecificId(uint64_t aId);

  static std::tuple<uint64_t, uint64_t> SplitProcessSpecificId(uint64_t aId);

  /**
   * Generate a window ID which is unique across processes and will never be
   * recycled.
   */
  static uint64_t GenerateWindowId();

  /**
   * Generate an ID for a load which is unique across processes and will never
   * be recycled.
   */
  static uint64_t GenerateLoadIdentifier();

  /**
   * Determine whether or not the user is currently interacting with the web
   * browser. This method is safe to call from off of the main thread.
   */
  static bool GetUserIsInteracting();

  // Alternate data MIME type used by the ScriptLoader to register and read
  // bytecode out of the nsCacheInfoChannel.
  [[nodiscard]] static bool InitJSBytecodeMimeType();
  static nsCString& JSScriptBytecodeMimeType() {
    MOZ_ASSERT(sJSScriptBytecodeMimeType);
    return *sJSScriptBytecodeMimeType;
  }
  static nsCString& JSModuleBytecodeMimeType() {
    MOZ_ASSERT(sJSModuleBytecodeMimeType);
    return *sJSModuleBytecodeMimeType;
  }

  /**
   * Checks if the passed-in name is one of the special names: "_blank", "_top",
   * "_parent" or "_self".
   */
  static bool IsSpecialName(const nsAString& aName);

  /**
   * Checks if the passed-in name should override an existing name on the
   * window. Values which should not override include: "", "_blank", "_top",
   * "_parent" and "_self".
   */
  static bool IsOverridingWindowName(const nsAString& aName);

  /**
   * If there is a SourceMap (higher precedence) or X-SourceMap (lower
   * precedence) response header in |aChannel|, set |aResult| to the
   * header's value and return true.  Otherwise, return false.
   *
   * @param aChannel The HTTP channel
   * @param aResult The string result.
   */
  static bool GetSourceMapURL(nsIHttpChannel* aChannel, nsACString& aResult);

  /**
   * Returns true if the passed-in mesasge is a pending InputEvent.
   *
   * @param aMsg  The message to check
   */
  static bool IsMessageInputEvent(const IPC::Message& aMsg);

  /**
   * Returns true if the passed-in message is a critical InputEvent.
   *
   * @param aMsg  The message to check
   */
  static bool IsMessageCriticalInputEvent(const IPC::Message& aMsg);

  static void AsyncPrecreateStringBundles();

  static bool ContentIsLink(nsIContent* aContent);

  static already_AddRefed<mozilla::dom::ContentFrameMessageManager>
  TryGetBrowserChildGlobal(nsISupports* aFrom);

  // Get a serial number for a newly created inner or outer window.
  static uint32_t InnerOrOuterWindowCreated();
  // Record that an inner or outer window has been destroyed.
  static void InnerOrOuterWindowDestroyed();
  // Get the current number of inner or outer windows.
  static int32_t GetCurrentInnerOrOuterWindowCount() {
    return sInnerOrOuterWindowCount;
  }

  // Return an anonymized URI so that it can be safely exposed publicly.
  static nsresult AnonymizeURI(nsIURI* aURI, nsCString& aAnonymizedURI);

  /**
   * Serializes a JSON-like JS::Value into a string.
   * Cases where JSON.stringify would return undefined are handled according to
   * the |aBehavior| argument:
   *
   * - If it is |UndefinedIsNullStringLiteral|, the string "null" is returned.
   * - If it is |UndefinedIsVoidString|, the void string is returned.
   *
   * The |UndefinedIsNullStringLiteral| case is likely not desirable, but is
   * retained for now for historical reasons.
   * Usage:
   *   nsAutoString serializedValue;
   *   nsContentUtils::StringifyJSON(cx, value, serializedValue, behavior);
   */
  static bool StringifyJSON(JSContext* aCx, JS::Handle<JS::Value> aValue,
                            nsAString& aOutStr, JSONBehavior aBehavior);

  /**
   * Returns true if the top level ancestor content document of aDocument hasn't
   * yet had the first contentful paint and there is a high priority event
   * pending in the main thread.
   */
  static bool HighPriorityEventPendingForTopLevelDocumentBeforeContentfulPaint(
      Document* aDocument);

  /**
   * Get the inner window corresponding to the incumbent global, including
   * mapping extension content script globals to the attached window.
   *
   * Returns null if the incumbent global doesn't correspond to an inner window.
   */
  static nsGlobalWindowInner* IncumbentInnerWindow();

  /**
   * Get the inner window corresponding to the entry global, including mapping
   * extension content script globals to the attached window.
   *
   * Returns null if the entry global doesn't correspond to an inner window.
   */
  static nsGlobalWindowInner* EntryInnerWindow();

  /*
   * Return safe area insets of window that defines as
   * https://drafts.csswg.org/css-env-1/#safe-area-insets.
   */
  static mozilla::LayoutDeviceIntMargin GetWindowSafeAreaInsets(
      nsIScreen* aScreen, const mozilla::LayoutDeviceIntMargin& aSafeareaInsets,
      const mozilla::LayoutDeviceIntRect& aWindowRect);

  struct SubresourceCacheValidationInfo {
    // The expiration time, in seconds, if known.
    mozilla::Maybe<CacheExpirationTime> mExpirationTime;
    bool mMustRevalidate = false;
  };

  /**
   * Gets cache validation info for subresources such as images, CSS
   * stylesheets, or JS.
   */
  static SubresourceCacheValidationInfo GetSubresourceCacheValidationInfo(
      nsIRequest*, nsIURI*);

  /**
   * Gets cache expiration time for subresources such as images, CSS
   * stylesheets, or JS.
   */
  static CacheExpirationTime GetSubresourceCacheExpirationTime(nsIRequest*,
                                                               nsIURI*);

  /**
   * Returns true if the request associated with the document should bypass the
   * shared sub resource cache.
   */
  static bool ShouldBypassSubResourceCache(Document* aDoc);

  static uint32_t SecondsFromPRTime(PRTime aTime) {
    return uint32_t(int64_t(aTime) / int64_t(PR_USEC_PER_SEC));
  }

  /**
   * Converts the given URL to a string and truncates it to the given length.
   *
   * Returns an empty string if aURL is null.
   */
  static nsCString TruncatedURLForDisplay(nsIURI* aURL, size_t aMaxLen = 128);

  /**
   * Anonymize the given id by hashing it with the provided origin. The
   * resulting id will have the same length as the one that was passed in.
   */
  enum class OriginFormat {
    Base64,
    Plain,
  };

  static nsresult AnonymizeId(nsAString& aId, const nsACString& aOriginKey,
                              OriginFormat aFormat = OriginFormat::Base64);

  /**
   * Create and load the string bundle for the 'aFile'.
   * This API is used to preload the string bundle on the main thread so later
   * other thread could access it.
   */
  static nsresult EnsureAndLoadStringBundle(PropertiesFile aFile);

  /**
   * The method asks nsIAppShell to prioritize Gecko's internal tasks over
   * the OS level tasks for a short period of time.
   */
  static void RequestGeckoTaskBurst();

  static void SetMayHaveFormCheckboxStateChangeListeners() {
    sMayHaveFormCheckboxStateChangeListeners = true;
  }

  static bool MayHaveFormCheckboxStateChangeListeners() {
    return sMayHaveFormCheckboxStateChangeListeners;
  }

  static void SetMayHaveFormRadioStateChangeListeners() {
    sMayHaveFormRadioStateChangeListeners = true;
  }

  static bool MayHaveFormRadioStateChangeListeners() {
    return sMayHaveFormRadioStateChangeListeners;
  }

  /**
   * Returns the closest link element in the flat tree of aContent if there's
   * one, otherwise returns nullptr.
   */
  static nsIContent* GetClosestLinkInFlatTree(nsIContent* aContent);

  static bool IsExternalProtocol(nsIURI* aURI);

  /**
   * Compares the position of aNode1 and aNode2 in the document
   * @param aNode1 First content to compare.
   * @param aNode2 Second content to compare.
   * @param aCommonAncestor Potential ancestor of the contents, if one exists.
   *                        This is only a hint; if it's not an ancestor of
   *                        aNode1 or aNode2, this function will still
   *                        work, but it will be slower.
   * @return < 0 if aNode1 is before aNode2,
   *         > 0 if aNode1 is after aNode2,
   *         0 otherwise
   */
  template <TreeKind aKind>
  static int32_t CompareTreePosition(const nsINode* aNode1,
                                     const nsINode* aNode2,
                                     const nsINode* aCommonAncestor,
                                     NodeIndexCache* = nullptr);

  // Get the index of a kid in its parent, including anonymous content, in
  // either the flat tree or the dom tree.
  //
  // The order goes as follows:
  //   ::marker (-3)
  //   ::before (-2)
  //   ShadowRoot (-1, only if TreeKind is not Flat)
  //   non-anonymous kids (0..n)
  //   anonymous content (n..m)
  //   ::after (m + 1)
  template <TreeKind>
  static mozilla::Maybe<int32_t> GetIndexInParent(const nsINode* aParent,
                                                  const nsINode* aNode);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  static nsIContent* AttachDeclarativeShadowRoot(
      nsIContent* aHost, mozilla::dom::ShadowRootMode aMode, bool aIsClonable,
      bool aIsSerializable, bool aDelegatesFocus);

 private:
  static bool InitializeEventTable();

  static nsresult EnsureStringBundle(PropertiesFile aFile);

  static bool CanCallerAccess(nsIPrincipal* aSubjectPrincipal,
                              nsIPrincipal* aPrincipal);

  static nsresult WrapNative(JSContext* cx, nsISupports* native,
                             nsWrapperCache* cache, const nsIID* aIID,
                             JS::MutableHandle<JS::Value> vp,
                             bool aAllowWrapping);

  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY static nsresult DispatchEvent(
      Document* aDoc, mozilla::dom::EventTarget* aTarget,
      const nsAString& aEventName, CanBubble, Cancelable, Composed, Trusted,
      bool* aDefaultAction = nullptr,
      ChromeOnlyDispatch = ChromeOnlyDispatch::eNo,
      SystemGroupOnly = SystemGroupOnly::eNo);

  // TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
  MOZ_CAN_RUN_SCRIPT_BOUNDARY static nsresult DispatchEvent(
      Document* aDoc, mozilla::dom::EventTarget* aTarget,
      mozilla::WidgetEvent& aWidgetEvent, EventMessage aEventMessage, CanBubble,
      Cancelable, Trusted, bool* aDefaultAction = nullptr,
      ChromeOnlyDispatch = ChromeOnlyDispatch::eNo);

  static void InitializeModifierStrings();

  static void DropFragmentParsers();

  static bool MatchClassNames(mozilla::dom::Element* aElement,
                              int32_t aNamespaceID, nsAtom* aAtom, void* aData);
  static void DestroyClassNameArray(void* aData);
  static void* AllocClassMatchingInfo(nsINode* aRootNode,
                                      const nsString* aClasses);

  static mozilla::EventClassID GetEventClassIDFromMessage(
      EventMessage aEventMessage);

  // Fills in aInfo with the tokens from the supplied autocomplete attribute.
  static AutocompleteAttrState InternalSerializeAutocompleteAttribute(
      const nsAttrValue* aAttrVal, mozilla::dom::AutocompleteInfo& aInfo,
      bool aGrantAllValidValue = false);

  static mozilla::CallState CallOnAllRemoteChildren(
      mozilla::dom::MessageBroadcaster* aManager,
      const std::function<mozilla::CallState(mozilla::dom::BrowserParent*)>&
          aCallback);

  static nsINode* GetCommonAncestorHelper(nsINode* aNode1, nsINode* aNode2);
  static nsIContent* GetCommonFlattenedTreeAncestorHelper(
      nsIContent* aContent1, nsIContent* aContent2);

  /**
   * Return 0 if aChild1 is same as aChild2.
   * Return -1 if aChild1 is a preceding sibling of aChild2.
   * Return 1 if aChild1 is a following sibling of aChild2.
   * If aChild1 and/or aChild2 is nullptr, it's treated as end of the parent
   * node.
   * Return Nothing if aChild1 is a root of the native anonymous subtree.
   */
  static mozilla::Maybe<int32_t> CompareChildNodes(
      const nsINode* aChild1, const nsINode* aChild2,
      NodeIndexCache* aIndexCache = nullptr);

  /**
   * Return 0 if aChild2 is at aOffset1.
   * Return -1 if aChild2 is a following sibling of a child at aOffset1
   * Return 1 if aChild2 is a preceding sibling of a child at aOffset1.
   * Return Nothing if aChild2 is a root of the native anonymous subtree.
   */
  static mozilla::Maybe<int32_t> CompareChildOffsetAndChildNode(
      uint32_t aOffset1, const nsINode& aChild2,
      NodeIndexCache* aIndexCache = nullptr);

  /**
   * Return 0 if aChild1 is at aOffset2.
   * Return -1 if aChild1 is a preceding sibling of a child at aOffset2.
   * Return 1 if aChild1 is a following sibling of a child at aOffset2.
   * Return Nothing if aChild1 is a root of the native anonymous subtree.
   */
  static mozilla::Maybe<int32_t> CompareChildNodeAndChildOffset(
      const nsINode& aChild1, uint32_t aOffset2,
      NodeIndexCache* aIndexCache = nullptr);

  /**
   * Helper method for ComparePoints() and ComparePointsWithIndices(). This
   * includes odd traditional behavior. Therefore, do not use this method as a
   * utility method.
   */
  static mozilla::Maybe<int32_t> CompareClosestCommonAncestorChildren(
      const nsINode&, const nsINode*, const nsINode*, NodeIndexCache*);

  static nsIXPConnect* sXPConnect;

  static nsIScriptSecurityManager* sSecurityManager;
  static nsIPrincipal* sSystemPrincipal;
  static nsIPrincipal* sNullSubjectPrincipal;
  static nsIPrincipal* sFingerprintingProtectionPrincipal;

  static nsIConsoleService* sConsoleService;

  static nsIStringBundleService* sStringBundleService;
  class nsContentUtilsReporter;

  static nsIContentPolicy* sContentPolicyService;
  static bool sTriedToGetContentPolicy;

  static mozilla::StaticRefPtr<nsIBidiKeyboard> sBidiKeyboard;

  static bool sInitialized;
  static uint32_t sScriptBlockerCount;
  static uint32_t sDOMNodeRemovedSuppressCount;

  // Not an nsCOMArray because removing elements from those is slower
  static AutoTArray<nsCOMPtr<nsIRunnable>, 8>* sBlockedScriptRunners;
  static uint32_t sRunnersCountAtFirstBlocker;
  static uint32_t sScriptBlockerCountWhereRunnersPrevented;

  static nsIInterfaceRequestor* sSameOriginChecker;

  static bool sIsHandlingKeyBoardEvent;
#ifndef RELEASE_OR_BETA
  static bool sBypassCSSOMOriginCheck;
#endif

  class UserInteractionObserver;
  static UserInteractionObserver* sUserInteractionObserver;

  static nsHtml5StringParser* sHTMLFragmentParser;
  static nsParser* sXMLFragmentParser;
  static nsIFragmentContentSink* sXMLFragmentSink;

  /**
   * True if there's a fragment parser activation on the stack.
   */
  static bool sFragmentParsingActive;

  static nsString* sShiftText;
  static nsString* sControlText;
  static nsString* sCommandOrWinText;
  static nsString* sAltText;
  static nsString* sModifierSeparator;

  // Alternate data mime types, used by the ScriptLoader to register and read
  // the bytecode out of the nsCacheInfoChannel.
  static nsCString* sJSScriptBytecodeMimeType;
  static nsCString* sJSModuleBytecodeMimeType;

  static mozilla::LazyLogModule gResistFingerprintingLog;
  static mozilla::LazyLogModule sDOMDumpLog;

  static int32_t sInnerOrOuterWindowCount;
  static uint32_t sInnerOrOuterWindowSerialCounter;

  static bool sMayHaveFormCheckboxStateChangeListeners;
  static bool sMayHaveFormRadioStateChangeListeners;
};

/* static */ inline ExtContentPolicyType
nsContentUtils::InternalContentPolicyTypeToExternal(nsContentPolicyType aType) {
  switch (aType) {
    case nsIContentPolicy::TYPE_INTERNAL_SCRIPT:
    case nsIContentPolicy::TYPE_INTERNAL_SCRIPT_PRELOAD:
    case nsIContentPolicy::TYPE_INTERNAL_MODULE:
    case nsIContentPolicy::TYPE_INTERNAL_MODULE_PRELOAD:
    case nsIContentPolicy::TYPE_INTERNAL_WORKER:
    case nsIContentPolicy::TYPE_INTERNAL_SHARED_WORKER:
    case nsIContentPolicy::TYPE_INTERNAL_SERVICE_WORKER:
    case nsIContentPolicy::TYPE_INTERNAL_WORKER_IMPORT_SCRIPTS:
    case nsIContentPolicy::TYPE_INTERNAL_WORKER_STATIC_MODULE:
    case nsIContentPolicy::TYPE_INTERNAL_AUDIOWORKLET:
    case nsIContentPolicy::TYPE_INTERNAL_PAINTWORKLET:
    case nsIContentPolicy::TYPE_INTERNAL_CHROMEUTILS_COMPILED_SCRIPT:
    case nsIContentPolicy::TYPE_INTERNAL_FRAME_MESSAGEMANAGER_SCRIPT:
      return ExtContentPolicy::TYPE_SCRIPT;

    case nsIContentPolicy::TYPE_INTERNAL_EMBED:
    case nsIContentPolicy::TYPE_INTERNAL_OBJECT:
      return ExtContentPolicy::TYPE_OBJECT;

    case nsIContentPolicy::TYPE_INTERNAL_FRAME:
    case nsIContentPolicy::TYPE_INTERNAL_IFRAME:
      return ExtContentPolicy::TYPE_SUBDOCUMENT;

    case nsIContentPolicy::TYPE_INTERNAL_AUDIO:
    case nsIContentPolicy::TYPE_INTERNAL_VIDEO:
    case nsIContentPolicy::TYPE_INTERNAL_TRACK:
      return ExtContentPolicy::TYPE_MEDIA;

    case nsIContentPolicy::TYPE_INTERNAL_XMLHTTPREQUEST_ASYNC:
    case nsIContentPolicy::TYPE_INTERNAL_XMLHTTPREQUEST_SYNC:
    case nsIContentPolicy::TYPE_INTERNAL_EVENTSOURCE:
      return ExtContentPolicy::TYPE_XMLHTTPREQUEST;

    case nsIContentPolicy::TYPE_INTERNAL_IMAGE:
    case nsIContentPolicy::TYPE_INTERNAL_IMAGE_PRELOAD:
    case nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON:
      return ExtContentPolicy::TYPE_IMAGE;

    case nsIContentPolicy::TYPE_INTERNAL_STYLESHEET:
    case nsIContentPolicy::TYPE_INTERNAL_STYLESHEET_PRELOAD:
      return ExtContentPolicy::TYPE_STYLESHEET;

    case nsIContentPolicy::TYPE_INTERNAL_DTD:
    case nsIContentPolicy::TYPE_INTERNAL_FORCE_ALLOWED_DTD:
      return ExtContentPolicy::TYPE_DTD;

    case nsIContentPolicy::TYPE_INTERNAL_FONT_PRELOAD:
      return ExtContentPolicy::TYPE_FONT;

    case nsIContentPolicy::TYPE_INTERNAL_FETCH_PRELOAD:
      return ExtContentPolicy::TYPE_FETCH;

    case nsIContentPolicy::TYPE_INTERNAL_EXTERNAL_RESOURCE:
      return ExtContentPolicy::TYPE_OTHER;

    case nsIContentPolicy::TYPE_INTERNAL_JSON_PRELOAD:
      return ExtContentPolicy::TYPE_JSON;

    case nsIContentPolicy::TYPE_INVALID:
    case nsIContentPolicy::TYPE_OTHER:
    case nsIContentPolicy::TYPE_SCRIPT:
    case nsIContentPolicy::TYPE_IMAGE:
    case nsIContentPolicy::TYPE_STYLESHEET:
    case nsIContentPolicy::TYPE_OBJECT:
    case nsIContentPolicy::TYPE_DOCUMENT:
    case nsIContentPolicy::TYPE_SUBDOCUMENT:
    case nsIContentPolicy::TYPE_PING:
    case nsIContentPolicy::TYPE_XMLHTTPREQUEST:
    case nsIContentPolicy::TYPE_OBJECT_SUBREQUEST:
    case nsIContentPolicy::TYPE_DTD:
    case nsIContentPolicy::TYPE_FONT:
    case nsIContentPolicy::TYPE_MEDIA:
    case nsIContentPolicy::TYPE_WEBSOCKET:
    case nsIContentPolicy::TYPE_CSP_REPORT:
    case nsIContentPolicy::TYPE_XSLT:
    case nsIContentPolicy::TYPE_BEACON:
    case nsIContentPolicy::TYPE_FETCH:
    case nsIContentPolicy::TYPE_IMAGESET:
    case nsIContentPolicy::TYPE_WEB_MANIFEST:
    case nsIContentPolicy::TYPE_SAVEAS_DOWNLOAD:
    case nsIContentPolicy::TYPE_SPECULATIVE:
    case nsIContentPolicy::TYPE_UA_FONT:
    case nsIContentPolicy::TYPE_PROXIED_WEBRTC_MEDIA:
    case nsIContentPolicy::TYPE_WEB_IDENTITY:
    case nsIContentPolicy::TYPE_WEB_TRANSPORT:
    case nsIContentPolicy::TYPE_JSON:
      // NOTE: When adding something here make sure the enumerator is defined!
      return static_cast<ExtContentPolicyType>(aType);

    case nsIContentPolicy::TYPE_END:
      break;
      // Do not add default: so that compilers can catch the missing case.
  }

  MOZ_ASSERT(false, "Unhandled nsContentPolicyType value");
  return ExtContentPolicy::TYPE_INVALID;
}

class MOZ_RAII nsAutoScriptBlocker {
 public:
  explicit nsAutoScriptBlocker() { nsContentUtils::AddScriptBlocker(); }
  ~nsAutoScriptBlocker() { nsContentUtils::RemoveScriptBlocker(); }

 private:
};

class MOZ_STACK_CLASS nsAutoScriptBlockerSuppressNodeRemoved
    : public nsAutoScriptBlocker {
 public:
  nsAutoScriptBlockerSuppressNodeRemoved() {
    ++nsContentUtils::sDOMNodeRemovedSuppressCount;
  }
  ~nsAutoScriptBlockerSuppressNodeRemoved() {
    --nsContentUtils::sDOMNodeRemovedSuppressCount;
  }
};

namespace mozilla::dom {

class TreeOrderComparator {
 public:
  bool Equals(nsINode* aElem1, nsINode* aElem2) const {
    return aElem1 == aElem2;
  }
  bool LessThan(nsINode* aElem1, nsINode* aElem2) const {
    return nsContentUtils::PositionIsBefore(aElem1, aElem2);
  }
};

}  // namespace mozilla::dom

#define NS_INTERFACE_MAP_ENTRY_TEAROFF(_interface, _allocator) \
  NS_INTERFACE_MAP_ENTRY_TEAROFF_AMBIGUOUS(_interface, _interface, _allocator)

#define NS_INTERFACE_MAP_ENTRY_TEAROFF_AMBIGUOUS(_interface, _implClass, \
                                                 _allocator)             \
  if (aIID.Equals(NS_GET_IID(_interface))) {                             \
    foundInterface = static_cast<_implClass*>(_allocator);               \
    if (!foundInterface) {                                               \
      *aInstancePtr = nullptr;                                           \
      return NS_ERROR_OUT_OF_MEMORY;                                     \
    }                                                                    \
  } else

/*
 * In the following helper macros we exploit the fact that the result of a
 * series of additions will not be finite if any one of the operands in the
 * series is not finite.
 */
#define NS_ENSURE_FINITE(f, rv) \
  if (!std::isfinite(f)) {      \
    return (rv);                \
  }

#define NS_ENSURE_FINITE2(f1, f2, rv) \
  if (!std::isfinite((f1) + (f2))) {  \
    return (rv);                      \
  }

#define NS_ENSURE_FINITE4(f1, f2, f3, f4, rv)      \
  if (!std::isfinite((f1) + (f2) + (f3) + (f4))) { \
    return (rv);                                   \
  }

#define NS_ENSURE_FINITE5(f1, f2, f3, f4, f5, rv)         \
  if (!std::isfinite((f1) + (f2) + (f3) + (f4) + (f5))) { \
    return (rv);                                          \
  }

#define NS_ENSURE_FINITE6(f1, f2, f3, f4, f5, f6, rv)            \
  if (!std::isfinite((f1) + (f2) + (f3) + (f4) + (f5) + (f6))) { \
    return (rv);                                                 \
  }

// Deletes a linked list iteratively to avoid blowing up the stack (bug 460444).
#define NS_CONTENT_DELETE_LIST_MEMBER(type_, ptr_, member_) \
  {                                                         \
    type_* cur = (ptr_)->member_;                           \
    (ptr_)->member_ = nullptr;                              \
    while (cur) {                                           \
      type_* next = cur->member_;                           \
      cur->member_ = nullptr;                               \
      delete cur;                                           \
      cur = next;                                           \
    }                                                       \
  }

#endif /* nsContentUtils_h___ */
