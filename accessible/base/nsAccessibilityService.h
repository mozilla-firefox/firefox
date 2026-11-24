/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef _nsAccessibilityService_h_
#define _nsAccessibilityService_h_

#include "mozilla/Preferences.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/a11y/CacheConstants.h"
#include "mozilla/a11y/DocManager.h"
#include "mozilla/a11y/FocusManager.h"
#include "mozilla/a11y/Platform.h"
#include "mozilla/a11y/Role.h"
#include "mozilla/a11y/SelectionManager.h"
#include "nsAtomHashKeys.h"
#include "nsIAccessibleEvent.h"
#include "nsIContent.h"
#include "nsIEventListenerService.h"
#include "nsIObserver.h"
#include "nsXULAppAPI.h"
#include "xpcAccessibilityService.h"

class nsImageFrame;
class nsIArray;
class nsITreeView;

namespace mozilla {

class PresShell;
class Monitor;
namespace dom {
class DOMStringList;
class Element;
}  // namespace dom

namespace a11y {

class AccAttributes;
class Accessible;
class ApplicationAccessible;
class xpcAccessibleApplication;

/**
 * Return focus manager.
 */
FocusManager* FocusMgr();

/**
 * Return selection manager.
 */
SelectionManager* SelectionMgr();

/**
 * Returns the application accessible.
 */
ApplicationAccessible* ApplicationAcc();
xpcAccessibleApplication* XPCApplicationAcc();

typedef already_AddRefed<LocalAccessible>(New_Accessible)(
    mozilla::dom::Element* aElement, LocalAccessible* aContext);

// These fields are not `nsStaticAtom* const` because MSVC doesn't like it.
struct MarkupAttrInfo {
  nsStaticAtom* name;
  nsStaticAtom* value;

  nsStaticAtom* DOMAttrName;
  nsStaticAtom* DOMAttrValue;
};

struct MarkupMapInfo {
  nsStaticAtom* const tag;
  New_Accessible* new_func;
  a11y::role role;
  MarkupAttrInfo attrs[4];
};

struct XULMarkupMapInfo {
  nsStaticAtom* const tag;
  New_Accessible* new_func;
};

/**
 * PREF_ACCESSIBILITY_FORCE_DISABLED preference change callback.
 */
void PrefChanged(const char* aPref, void* aClosure);

/**
 * Read and normalize PREF_ACCESSIBILITY_FORCE_DISABLED preference.
 */
EPlatformDisabledState ReadPlatformDisabledState();

/**
 * RAII class to prevent new cache domains from being requested. This is
 * necessary in some cases when code for an OS accessibility API requires
 * information in order to fire an event. We don't necessarily know that a
 * client is even interested in that event, so requesting data that the client
 * may never query doesn't make sense.
 */
class MOZ_RAII CacheDomainActivationBlocker {
 public:
  CacheDomainActivationBlocker();
  ~CacheDomainActivationBlocker();

 private:
  // Used to manage re-entry.
  static uint32_t sEntryCount;
};

}  // namespace a11y
}  // namespace mozilla

class nsAccessibilityService final : public mozilla::a11y::DocManager,
                                     public mozilla::a11y::FocusManager,
                                     public mozilla::a11y::SelectionManager,
                                     public nsIListenerChangeListener,
                                     public nsIObserver {
 public:
  typedef mozilla::a11y::LocalAccessible LocalAccessible;
  typedef mozilla::a11y::DocAccessible DocAccessible;

  static const uint64_t kDefaultCacheDomains =
      mozilla::a11y::CacheDomain::NameAndDescription |
      mozilla::a11y::CacheDomain::State;

  // nsIListenerChangeListener
  NS_IMETHOD ListenersChanged(nsIArray* aEventChanges) override;

 protected:
  ~nsAccessibilityService();

 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIOBSERVER

  nsAccessibilityService(const nsAccessibilityService&) = delete;
  nsAccessibilityService& operator=(const nsAccessibilityService&) = delete;

  LocalAccessible* GetRootDocumentAccessible(mozilla::PresShell* aPresShell,
                                             bool aCanCreate);

  /**
   * Adds/remove ATK root accessible for gtk+ native window to/from children
   * of the application accessible.
   */
  LocalAccessible* AddNativeRootAccessible(void* aAtkAccessible);
  void RemoveNativeRootAccessible(LocalAccessible* aRootAccessible);

  bool HasAccessible(nsINode* aDOMNode);

  /**
   * Get a string equivalent for an accessible role value.
   */
  void GetStringRole(uint32_t aRole, nsAString& aString);

  /**
   * Get a string equivalent for an accessible state/extra state.
   */
  already_AddRefed<mozilla::dom::DOMStringList> GetStringStates(
      uint64_t aStates) const;
  void GetStringStates(uint32_t aState, uint32_t aExtraState,
                       nsISupports** aStringStates);

  /**
   * Get a string equivalent for an accessible event value.
   */
  void GetStringEventType(uint32_t aEventType, nsAString& aString);

  /**
   * Get a string equivalent for an accessible event value.
   */
  void GetStringEventType(uint32_t aEventType, nsACString& aString);

  /**
   * Get a string equivalent for an accessible relation type.
   */
  void GetStringRelationType(uint32_t aRelationType, nsAString& aString);

  // nsAccesibilityService
  /**
   * Notification used to update the accessible tree when new content is
   * inserted.
   */
  void ContentRangeInserted(mozilla::PresShell* aPresShell,
                            nsIContent* aStartChild, nsIContent* aEndChild);

  /**
   * Triggers a re-evaluation of the a11y tree of aContent after the next
   * refresh. This is important because whether we create accessibles may
   * depend on the frame tree / style.
   */
  void ScheduleAccessibilitySubtreeUpdate(mozilla::PresShell* aPresShell,
                                          nsIContent* aStartChild);

  /**
   * Notification used to update the accessible tree when content is removed.
   */
  void ContentRemoved(mozilla::PresShell* aPresShell, nsIContent* aChild);

  /**
   * Notification used to invalidate the isLayoutTable cache.
   */
  void TableLayoutGuessMaybeChanged(mozilla::PresShell* aPresShell,
                                    nsIContent* aContent);

  /**
   * Notifies when a combobox's <option> text or label changes.
   */
  void ComboboxOptionMaybeChanged(mozilla::PresShell*,
                                  nsIContent* aMutatingNode);
  // Notifies when a combobox's selected index changes.
  void ComboboxValueChanged(nsIContent*);

  void UpdateText(mozilla::PresShell* aPresShell, nsIContent* aContent);

  /**
   * Update XUL:tree accessible tree when treeview is changed.
   */
  void TreeViewChanged(mozilla::PresShell* aPresShell, nsIContent* aContent,
                       nsITreeView* aView);

  /**
   * Notify of input@type="element" value change.
   */
  void RangeValueChanged(mozilla::PresShell* aPresShell, nsIContent* aContent);

  /**
   * Notify accessibility that the value of an <input type="color"> has changed.
   */
  void ColorValueChanged(mozilla::PresShell* aPresShell, nsIContent* aContent);

  /**
   * Update the image map.
   */
  void UpdateImageMap(nsImageFrame* aImageFrame);

  /**
   * Notify accessibility that anchor jump has been accomplished to the given
   * target. Used by layout.
   */
  void NotifyOfAnchorJumpTo(nsIContent* aTarget);

  /**
   * Notify that presshell is activated.
   */
  void PresShellActivated(mozilla::PresShell* aPresShell);

  /**
   * Recreate an accessible for the given content node in the presshell.
   */
  void RecreateAccessible(mozilla::PresShell* aPresShell, nsIContent* aContent);

  void FireAccessibleEvent(uint32_t aEvent, LocalAccessible* aTarget);

  void NotifyOfPossibleBoundsChange(mozilla::PresShell* aPresShell,
                                    nsIContent* aContent);

  void NotifyOfComputedStyleChange(mozilla::PresShell* aPresShell,
                                   nsIContent* aContent);

  void NotifyOfTabPanelVisibilityChange(mozilla::PresShell* aPresShell,
                                        mozilla::dom::Element* aPanel,
                                        bool aVisible);

  void NotifyOfResolutionChange(mozilla::PresShell* aPresShell,
                                float aResolution);

  void NotifyOfDevPixelRatioChange(mozilla::PresShell* aPresShell,
                                   int32_t aAppUnitsPerDevPixel);

  /**
   * Notify accessibility that an anchor positioned frame is
   * about to be removed. This gives us a chance to update cached relations
   * before the reflow where we will lose references to the anchor and won't be
   * able to refresh its accessible's cache.
   */
  void NotifyAnchorPositionedRemoved(mozilla::PresShell* aPresShell,
                                     nsIFrame* aFrame);

  /**
   * Notify accessibility that an anchor frame is about to be removed. This
   * gives us a chance to update cached relations before the reflow where the
   * anchor will be lost and we won't be able to refresh the accessible cache of
   * prior relations.
   */
  void NotifyAnchorRemoved(mozilla::PresShell* aPresShell, nsIFrame* aFrame);

  /**
   * Notify accessibility that an anchor positioned frame has
   * been marked for reflow because of a scroll change for one of its
   * anchors. A fallback anchor may be activated or deactivated.
   */
  void NotifyAnchorPositionedScrollUpdate(mozilla::PresShell* aPresShell,
                                          nsIFrame* aFrame);

  /**
   * Notify accessibility that an element explicitly set for an attribute is
   * about to change. See dom::Element::ExplicitlySetAttrElement.
   */
  void NotifyAttrElementWillChange(mozilla::dom::Element* aElement,
                                   nsAtom* aAttr);

  /**
   * Notify accessibility that an element explicitly set for an attribute has
   * changed. See dom::Element::ExplicitlySetAttrElement.
   */
  void NotifyAttrElementChanged(mozilla::dom::Element* aElement, nsAtom* aAttr);

  /**
   * Notify accessibility that an ARIA attribute reflected from ElementInternals
   * is about to change / has changed. See dom::ElementInternals.
   */
  void NotifyARIAAttributeDefaultWillChange(mozilla::dom::Element* aElement,
                                            nsAtom* aAttribute,
                                            AttrModType aModType);
  void NotifyARIAAttributeDefaultChanged(mozilla::dom::Element* aElement,
                                         nsAtom* aAttribute,
                                         AttrModType aModType);

  /**
   * Notify accessibility that an EditContext has been attached to or
   * detached from aElement. Deliberately not called when aElement's EditContext
   * is replaced with a different EditContext.
   */
  void NotifyOfEditContextAttachmentChange(mozilla::dom::Element* aElement);

  void AriaNotify(nsINode* aNode, const nsAString& aAnnouncement,
                  const mozilla::dom::AriaNotificationOptions& aOptions);

  // nsAccessibiltiyService

  /**
   * Return true if accessibility service has been shutdown.
   */
  static bool IsShutdown() { return gConsumers == 0; };

  /**
   * Return true if there should be an image accessible for the given element.
   */
  static bool ShouldCreateImgAccessible(mozilla::dom::Element* aElement,
                                        DocAccessible* aDocument);

  /*
   * Set the currently-active cache domains.
   */
  void SetCacheDomains(uint64_t aCacheDomains);

  bool CacheDomainIsActive(uint64_t aCacheDomain) const {
    return (gCacheDomains & aCacheDomain) != mozilla::a11y::CacheDomain::None;
  }

  /**
   * Creates an accessible for the given DOM node.
   *
   * @param  aNode             [in] the given node
   * @param  aContext          [in] context the accessible is created in
   * @param  aIsSubtreeHidden  [out, optional] indicates whether the node's
   *                             frame and its subtree is hidden
   */
  LocalAccessible* CreateAccessible(nsINode* aNode, LocalAccessible* aContext,
                                    bool* aIsSubtreeHidden = nullptr);

  mozilla::a11y::role MarkupRole(const nsIContent* aContent) const {
    const mozilla::a11y::MarkupMapInfo* markupMap =
        GetMarkupMapInfoFor(aContent);
    return markupMap ? markupMap->role : mozilla::a11y::roles::NOTHING;
  }

  /**
   * Return the associated value for a given attribute if
   * it appears in the MarkupMap. Otherwise, it returns null. This can be
   * called with either an nsIContent or an Accessible.
   */
  template <typename T>
  nsStaticAtom* MarkupAttribute(T aSource, nsStaticAtom* aAtom) const {
    const mozilla::a11y::MarkupMapInfo* markupMap =
        GetMarkupMapInfoFor(aSource);
    if (markupMap) {
      for (size_t i = 0; i < std::size(markupMap->attrs); i++) {
        const mozilla::a11y::MarkupAttrInfo* info = markupMap->attrs + i;
        if (info->name == aAtom) {
          return info->value;
        }
      }
    }
    return nullptr;
  }

  /**
   * Set the object attribute defined by markup for the given element.
   */
  void MarkupAttributes(mozilla::a11y::Accessible* aAcc,
                        mozilla::a11y::AccAttributes* aAttributes) const;

  /**
   * A list of possible accessibility service consumers. Accessibility service
   * can only be shut down when there are no remaining consumers.
   *
   * eXPCOM       - accessibility service is used by XPCOM.
   *
   * eMainProcess - accessibility service was started by main process in the
   *                content process.
   *
   * ePlatformAPI - accessibility service is used by the platform api in the
   *                main process.
   *
   * ePdfOutput   - accessibility service is used to generate a tagged PDF for
   *                a document being printed. While this is the only active
   *                consumer, accessibility is suppressed for everything except
   *                the document(s) being printed.
   */
  enum ServiceConsumer {
    eXPCOM = 1 << 0,
    eMainProcess = 1 << 1,
    ePlatformAPI = 1 << 2,
    ePdfOutput = 1 << 3,
  };

  /**
   * Return true if the only active service consumer is ePdfOutput. In this
   * mode the service is alive purely to build the accessibility tree for a
   * document being printed and must not do work for any other document.
   */
  static bool IsOnlyForPdfOutput() { return gConsumers == ePdfOutput; }

  static bool IsRunningInParentProcess() { return gConsumers & eMainProcess; }

  static uint64_t GetActiveCacheDomains() { return gCacheDomains; }
  bool ShouldAllowNewCacheDomains() { return mShouldAllowNewCacheDomains; }

#if defined(ANDROID)
  static mozilla::Monitor& GetAndroidMonitor();
#endif

 private:
  // nsAccessibilityService creation is controlled by friend
  // GetOrCreateAccService, keep constructors private.
  nsAccessibilityService();

 private:
  /**
   * Initialize accessibility service.
   * @param aConsumer The consumer requesting initialization. When this is
   *        ePdfOutput, work that is unnecessary for tagged PDF generation
   *        will be skipped.
   */
  bool Init(uint64_t aCacheDomains = kDefaultCacheDomains,
            uint32_t aConsumer = ePlatformAPI);

  /**
   * Shutdowns accessibility service.
   */
  void Shutdown();

  /**
   * Run init steps specific to a full (non-PDF) consumer: create initial docs,
   * initialize the platform, and set cache domains.
   */
  void FullInit(uint64_t aCacheDomains, uint32_t aConsumer);

  /**
   * Run the init steps that Init skipped because the original consumer was
   * ePdfOutput. Called from GetOrCreateAccService when a non-PDF consumer
   * arrives while the service is still only for PDF output.
   */
  void PromoteFromPdfOutput(uint64_t aCacheDomains, uint32_t aConsumer);

  /**
   * Create an accessible whose type depends on the given frame.
   */
  already_AddRefed<LocalAccessible> CreateAccessibleByFrameType(
      nsIFrame* aFrame, nsIContent* aContent, LocalAccessible* aContext);

  /**
   * Notify observers about change of the accessibility service's consumers.
   */
  void NotifyOfConsumersChange();

  /**
   * Get a JSON string representing the accessibility service consumers.
   */
  void GetConsumers(nsAString& aString);

  /**
   * Set accessibility service consumers.
   */
  void SetConsumers(uint32_t aConsumers, bool aNotify = true);

  /**
   * Unset accessibility service consumers.
   */
  void UnsetConsumers(uint32_t aConsumers);

  /**
   * Reference for accessibility service instance.
   */
  static mozilla::StaticRefPtr<nsAccessibilityService> gAccessibilityService;

  /**
   * Reference for application accessible instance.
   */
  static mozilla::StaticRefPtr<mozilla::a11y::ApplicationAccessible>
      gApplicationAccessible;
  static mozilla::StaticRefPtr<mozilla::a11y::xpcAccessibleApplication>
      gXPCApplicationAccessible;

  /**
   * Contains a set of accessibility service consumers.
   */
  static uint32_t gConsumers;

  /**
   * Contains the currently active cache domains.
   */
  static uint64_t gCacheDomains;
  // True if requesting new cache domains should be allowed, false if this
  // should be disallowed. This should only be changed by
  // CacheDomainActivationBlocker.
  bool mShouldAllowNewCacheDomains = true;

  // Can be weak because all atoms are known static
  using MarkupMap = nsTHashMap<nsAtom*, const mozilla::a11y::MarkupMapInfo*>;
  MarkupMap mHTMLMarkupMap;
  MarkupMap mMathMLMarkupMap;

  const mozilla::a11y::MarkupMapInfo* GetMarkupMapInfoFor(
      const nsIContent* aContent) const {
    if (aContent->IsHTMLElement()) {
      return mHTMLMarkupMap.Get(aContent->NodeInfo()->NameAtom());
    }
    if (aContent->IsMathMLElement()) {
      return mMathMLMarkupMap.Get(aContent->NodeInfo()->NameAtom());
    }
    // This function can be called by MarkupAttribute, etc. which might in turn
    // be called on a XUL, SVG, etc. element. For example, this can happen
    // with nsAccUtils::SetLiveContainerAttributes.
    return nullptr;
  }

  const mozilla::a11y::MarkupMapInfo* GetMarkupMapInfoFor(
      mozilla::a11y::Accessible* aAcc) const;

  nsTHashMap<nsAtom*, const mozilla::a11y::XULMarkupMapInfo*> mXULMarkupMap;

  friend nsAccessibilityService* GetAccService();
  friend nsAccessibilityService* GetOrCreateAccService(uint32_t, uint64_t);
  friend void MaybeShutdownAccService(uint32_t, bool);
  friend void mozilla::a11y::PrefChanged(const char*, void*);
  friend mozilla::a11y::FocusManager* mozilla::a11y::FocusMgr();
  friend mozilla::a11y::SelectionManager* mozilla::a11y::SelectionMgr();
  friend mozilla::a11y::ApplicationAccessible* mozilla::a11y::ApplicationAcc();
  friend mozilla::a11y::xpcAccessibleApplication*
  mozilla::a11y::XPCApplicationAcc();
  friend class xpcAccessibilityService;
  friend class mozilla::a11y::CacheDomainActivationBlocker;
};

/**
 * Return the accessibility service instance. (Handy global function)
 */
inline nsAccessibilityService* GetAccService() {
  return nsAccessibilityService::gAccessibilityService;
}

/**
 * Return accessibility service instance; creating one if necessary.
 */
nsAccessibilityService* GetOrCreateAccService(
    uint32_t aNewConsumer = nsAccessibilityService::ePlatformAPI,
    uint64_t aCacheDomains = nsAccessibilityService::GetActiveCacheDomains());

/**
 * Shutdown accessibility service if needed.
 * @param aFormerConsumer The ServiceConsumer that is no longer using the
 *        service.
 * @param aAsync True to shut down the service asynchronously using a runnable.
 *        This should be used to avoid reentry if this is called during the
 *        shutdown of a document.
 */
void MaybeShutdownAccService(uint32_t aFormerConsumer, bool aAsync = false);

/**
 * Return true if we're in a content process and not B2G.
 */
inline bool IPCAccessibilityActive() { return XRE_IsContentProcess(); }

/**
 * Map nsIAccessibleEvents constants to strings. Used by
 * nsAccessibilityService::GetStringEventType() method.
 */
static const char kEventTypeNames[][40] = {
    "unknown",                   //
    "show",                      // EVENT_SHOW
    "hide",                      // EVENT_HIDE
    "reorder",                   // EVENT_REORDER
    "focus",                     // EVENT_FOCUS
    "state change",              // EVENT_STATE_CHANGE
    "name changed",              // EVENT_NAME_CHANGE
    "description change",        // EVENT_DESCRIPTION_CHANGE
    "value change",              // EVENT_VALUE_CHANGE
    "selection",                 // EVENT_SELECTION
    "selection add",             // EVENT_SELECTION_ADD
    "selection remove",          // EVENT_SELECTION_REMOVE
    "selection within",          // EVENT_SELECTION_WITHIN
    "alert",                     // EVENT_ALERT
    "menu start",                // EVENT_MENU_START
    "menu end",                  // EVENT_MENU_END
    "menupopup start",           // EVENT_MENUPOPUP_START
    "menupopup end",             // EVENT_MENUPOPUP_END
    "dragdrop start",            // EVENT_DRAGDROP_START
    "scrolling start",           // EVENT_SCROLLING_START
    "scrolling end",             // EVENT_SCROLLING_END
    "document load complete",    // EVENT_DOCUMENT_LOAD_COMPLETE
    "document reload",           // EVENT_DOCUMENT_RELOAD
    "document load stopped",     // EVENT_DOCUMENT_LOAD_STOPPED
    "text attribute changed",    // EVENT_TEXT_ATTRIBUTE_CHANGED
    "text caret moved",          // EVENT_TEXT_CARET_MOVED
    "text inserted",             // EVENT_TEXT_INSERTED
    "text removed",              // EVENT_TEXT_REMOVED
    "text selection changed",    // EVENT_TEXT_SELECTION_CHANGED
    "window activate",           // EVENT_WINDOW_ACTIVATE
    "window deactivate",         // EVENT_WINDOW_DEACTIVATE
    "window maximize",           // EVENT_WINDOW_MAXIMIZE
    "window minimize",           // EVENT_WINDOW_MINIMIZE
    "window restore",            // EVENT_WINDOW_RESTORE
    "object attribute changed",  // EVENT_OBJECT_ATTRIBUTE_CHANGED
    "text value change",         // EVENT_TEXT_VALUE_CHANGE
    "scrolling",                 // EVENT_SCROLLING
    "announcement",              // EVENT_ANNOUNCEMENT
    "live region added",         // EVENT_LIVE_REGION_ADDED
    "live region removed",       // EVENT_LIVE_REGION_REMOVED
    "inner reorder",             // EVENT_INNER_REORDER
    "live region changed",       // EVENT_LIVE_REGION_CHANGED
    "errormessage changed",      // EVENT_ERRORMESSAGE_CHANGED
};

#endif
