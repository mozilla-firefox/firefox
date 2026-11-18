/* clang-format off */
/* clang-format on */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import <Accessibility/Accessibility.h>
#import <ApplicationServices/ApplicationServices.h>

#import "mozAccessible.h"
#include "MOXAccessibleBase.h"

#import "MacUtils.h"
#import "mozView.h"
#import "MOXSearchInfo.h"
#import "MOXTextMarkerDelegate.h"
#import "MOXWebAreaAccessible.h"
#import "mozTextAccessible.h"

#include "LocalAccessible-inl.h"
#include "nsAccUtils.h"
#include "nsTextEquivUtils.h"
#include "DocAccessibleParent.h"
#include "Relation.h"
#include "mozilla/a11y/Role.h"
#include "RootAccessible.h"
#include "mozilla/a11y/PDocAccessible.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/Document.h"
#include "OuterDocAccessible.h"
#include "nsIAccessibleAnnouncementEvent.h"
#include "nsChildView.h"
#include "TextLeafRange.h"
#include "xpcAccessibleMacInterface.h"

#include "nsRect.h"
#include "nsCocoaUtils.h"
#include "nsCocoaWindow.h"
#include "nsCoord.h"
#include "nsObjCExceptions.h"
#include "nsWhitespaceTokenizer.h"
#include <prdtoa.h>

using namespace mozilla;
using namespace mozilla::a11y;

#pragma mark -

@interface mozAccessible ()
- (void)maybePostA11yUtilNotification;
@end

@implementation mozAccessible

- (id)initWithAccessible:(Accessible*)aAcc {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;
  MOZ_ASSERT(aAcc, "Cannot init mozAccessible with null");
  if ((self = [super init])) {
    mGeckoAccessible = aAcc;
    mRole = aAcc->Role();
  }

  return self;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (void)dealloc {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  [super dealloc];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

#pragma mark - mozAccessible widget

- (BOOL)hasRepresentedView {
  return [self representedView] != nil;
}

- (id)representedView {
  if (!mGeckoAccessible || !mGeckoAccessible->IsLocal()) {
    // We only support representedView on local accessibles that
    // might have associated native widgets.
    return nil;
  }

  id view = static_cast<AccessibleWrap*>(mGeckoAccessible->AsLocal())
                ->GetNativeWidget();

  if (![view hasMozAccessible]) {
    // The NSView does not have a reciprocal relationship.
    return nil;
  }

  return view;
}

- (BOOL)isRoot {
  return mGeckoAccessible && mGeckoAccessible->IsLocal() &&
         mGeckoAccessible->IsRoot();
}

#pragma mark -

- (BOOL)moxIgnoreWithParent:(mozAccessible*)parent {
  if (LocalAccessible* acc = mGeckoAccessible->AsLocal()) {
    if (acc->IsContent() && acc->GetContent()->IsXULElement()) {
      if (acc->VisibilityState() & states::INVISIBLE) {
        return YES;
      }
    }
  }

  return [parent moxIgnoreChild:self];
}

- (BOOL)moxIgnoreChild:(mozAccessible*)child {
  return nsAccUtils::MustPrune(mGeckoAccessible);
}

- (id)childAt:(uint32_t)i {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  Accessible* child = mGeckoAccessible->ChildAt(i);
  return child ? GetNativeFromGeckoAccessible(child) : nil;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (uint64_t)state {
  return mGeckoAccessible->State();
}

- (uint64_t)stateWithMask:(uint64_t)mask {
  return [self state] & mask;
}

- (void)stateChanged:(uint64_t)state isEnabled:(BOOL)enabled {
  if (state == states::BUSY) {
    [self moxPostNotification:@"AXElementBusyChanged"];
  }

  if (state == states::EXPANDED) {
    [self moxPostNotification:@"AXExpandedChanged"];
  }
}

- (mozilla::a11y::Accessible*)geckoAccessible {
  return mGeckoAccessible;
}

#pragma mark - MOXAccessible protocol

- (BOOL)moxBlockSelector:(SEL)selector {
  if (selector == @selector(moxPerformPress)) {
    uint8_t actionCount = mGeckoAccessible->ActionCount();

    // If we have no action, we don't support press, so return YES.
    return actionCount == 0;
  }

  if (selector == @selector(moxSetFocused:)) {
    return [self stateWithMask:states::FOCUSABLE] == 0;
  }

  if (selector == @selector(moxARIALive) ||
      selector == @selector(moxARIAAtomic) ||
      selector == @selector(moxARIARelevant)) {
    return ![self moxIsLiveRegion];
  }

  if (selector == @selector(moxARIAPosInSet) || selector == @selector
                                                    (moxARIASetSize)) {
    GroupPos groupPos = mGeckoAccessible->GroupPosition();
    return groupPos.setSize == 0;
  }

  if (selector == @selector(moxARIABrailleRoleDescription)) {
    NSString* brailleRoleDescription =
        utils::GetAccAttr(self, nsGkAtoms::aria_brailleroledescription);
    return [brailleRoleDescription length] == 0;
  }

  if (selector == @selector(moxARIABrailleLabel)) {
    NSString* brailleLabel =
        utils::GetAccAttr(self, nsGkAtoms::aria_braillelabel);
    return [brailleLabel length] == 0;
  }

  if (selector == @selector(moxExpanded)) {
    return [self stateWithMask:states::EXPANDABLE] == 0;
  }

  if ([self blockTextFieldMethod:selector]) {
    return YES;
  }

  return [super moxBlockSelector:selector];
}

- (id)moxFocusedUIElement {
  MOZ_ASSERT(mGeckoAccessible);
  // This only gets queried on the web area or the root group
  // so just use the doc's focused child instead of trying to get
  // the focused child of mGeckoAccessible.
  Accessible* doc = nsAccUtils::DocumentFor(mGeckoAccessible);
  mozAccessible* focusedChild =
      GetNativeFromGeckoAccessible(doc->FocusedChild());

  if ([focusedChild isAccessibilityElement]) {
    return focusedChild;
  }

  // return ourself if we can't get a native focused child.
  return self;
}

- (id<MOXTextMarkerSupport>)moxTextMarkerDelegate {
  MOZ_ASSERT(mGeckoAccessible);

  return [MOXTextMarkerDelegate
      getOrCreateForDoc:nsAccUtils::DocumentFor(mGeckoAccessible)];
}

- (BOOL)moxIsLiveRegion {
  return mIsLiveRegion;
}

- (id)moxHitTest:(NSPoint)point {
  MOZ_ASSERT(mGeckoAccessible);

  // Convert the given screen-global point in the cocoa coordinate system (with
  // origin in the bottom-left corner of the screen) into point in the Gecko
  // coordinate system (with origin in a top-left screen point).
  NSScreen* scalingView = utils::GetNSScreenForAcc(self);
  // Regardless of screen selected above, VO is only happy if we use the
  // main screen height for Y coordinate conversion. This is consistent with
  // moxFrame and GeckoTextMarkerRange::Bounds().
  NSScreen* mainView = [[NSScreen screens] objectAtIndex:0];
  NSPoint tmpPoint =
      NSMakePoint(point.x, [mainView frame].size.height - point.y);
  LayoutDeviceIntPoint geckoPoint = nsCocoaUtils::CocoaPointsToDevPixels(
      tmpPoint, nsCocoaUtils::GetBackingScaleFactor(scalingView));

  Accessible* child = mGeckoAccessible->ChildAtPoint(
      geckoPoint.x, geckoPoint.y, Accessible::EWhichChildAtPoint::DeepestChild);

  if (child) {
    mozAccessible* nativeChild = GetNativeFromGeckoAccessible(child);
    return [nativeChild isAccessibilityElement]
               ? nativeChild
               : [nativeChild moxUnignoredParent];
  }

  // if we didn't find anything, return ourself or child view.
  return self;
}

- (id<mozAccessible>)moxParent {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;
  if ([self isExpired]) {
    return nil;
  }

  Accessible* parent = mGeckoAccessible->Parent();

  if (!parent) {
    return nil;
  }

  id nativeParent = GetNativeFromGeckoAccessible(parent);
  if ([nativeParent isKindOfClass:[MOXWebAreaAccessible class]]) {
    // Before returning a WebArea as parent, check to see if
    // there is a generated root group that is an intermediate container.
    if (id<mozAccessible> rootGroup = [nativeParent rootGroup]) {
      nativeParent = rootGroup;
    }
  }

  if (!nativeParent && mGeckoAccessible->IsLocal()) {
    // Return native of root accessible if we have no direct parent.
    // XXX: need to return a sensible fallback in proxy case as well
    nativeParent = GetNativeFromGeckoAccessible(
        mGeckoAccessible->AsLocal()->RootAccessible());
  }

  return GetObjectOrRepresentedView(nativeParent);

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

// gets all our native children lazily, including those that are ignored.
- (NSArray*)moxChildren {
  MOZ_ASSERT(mGeckoAccessible);

  NSMutableArray* children = [[[NSMutableArray alloc]
      initWithCapacity:mGeckoAccessible->ChildCount()] autorelease];

  for (uint32_t childIdx = 0; childIdx < mGeckoAccessible->ChildCount();
       childIdx++) {
    Accessible* child = mGeckoAccessible->ChildAt(childIdx);
    mozAccessible* nativeChild = GetNativeFromGeckoAccessible(child);
    if (!nativeChild) {
      continue;
    }

    [children addObject:nativeChild];
  }

  return children;
}

- (NSValue*)moxPosition {
  CGRect frame = [[self moxFrame] rectValue];

  return [NSValue valueWithPoint:NSMakePoint(frame.origin.x, frame.origin.y)];
}

- (NSValue*)moxSize {
  CGRect frame = [[self moxFrame] rectValue];

  return
      [NSValue valueWithSize:NSMakeSize(frame.size.width, frame.size.height)];
}

static bool IsNonNativePopover(Accessible* aAccessible) {
  if (!aAccessible->IsLocal() || aAccessible->TagName() != nsGkAtoms::panel) {
    return false;
  }

  NSView* view =
      static_cast<AccessibleWrap*>(aAccessible->AsLocal())->GetNativeWidget();
  if (!view) {
    return false;
  }

  NSWindow* window = [view window];
  if ([window isKindOfClass:[PopupWindow class]] &&
      ![(PopupWindow*)window usePopover]) {
    return true;
  }

  return false;
}

- (NSString*)moxRole {
  if (mRole == roles::ENTRY ||
      (mGeckoAccessible->IsGeneric() && mGeckoAccessible->IsEditableRoot())) {
    if ([self stateWithMask:states::MULTI_LINE]) {
      // This is a special case where we have a separate role when an entry is a
      // multiline text area.
      return NSAccessibilityTextAreaRole;
    }

    return NSAccessibilityTextFieldRole;
  }

  if (IsNonNativePopover(mGeckoAccessible)) {
    return NSAccessibilityPopoverRole;
  }

#define ROLE(geckoRole, stringRole, ariaRole, atkRole, macRole, macSubrole, \
             msaaRole, ia2Role, androidClass, iosIsElement, uiaControlType, \
             nameRule)                                                      \
  case roles::geckoRole:                                                    \
    return macRole;

  switch (mRole) {
#include "RoleMap.inc"
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown role.");
      return NSAccessibilityUnknownRole;
  }

#undef ROLE
}

- (nsStaticAtom*)ARIARole {
  MOZ_ASSERT(mGeckoAccessible);

  if (mGeckoAccessible->HasARIARole()) {
    const nsRoleMapEntry* roleMap = mGeckoAccessible->ARIARoleMap();
    return roleMap->roleAtom;
  }

  return nsGkAtoms::_empty;
}

- (NSString*)moxSubrole {
  MOZ_ASSERT(mGeckoAccessible);

  // Deal with landmarks first
  // macOS groups the specific landmark types of DPub ARIA into two broad
  // categories with corresponding subroles: Navigation and region/container.
  if (mRole == roles::LANDMARK) {
    nsAtom* landmark = mGeckoAccessible->LandmarkRole();
    // HTML Elements treated as landmarks, and ARIA landmarks.
    if (landmark) {
      if (landmark == nsGkAtoms::banner) return @"AXLandmarkBanner";
      if (landmark == nsGkAtoms::complementary)
        return @"AXLandmarkComplementary";
      if (landmark == nsGkAtoms::contentinfo) return @"AXLandmarkContentInfo";
      if (landmark == nsGkAtoms::main) return @"AXLandmarkMain";
      if (landmark == nsGkAtoms::navigation) return @"AXLandmarkNavigation";
      if (landmark == nsGkAtoms::search) return @"AXLandmarkSearch";
    }

    // None of the above, so assume DPub ARIA.
    return @"AXLandmarkRegion";
  }

  // Now, deal with widget roles
  nsStaticAtom* roleAtom = nullptr;

  if (mRole == roles::DIALOG) {
    roleAtom = [self ARIARole];

    if (roleAtom == nsGkAtoms::alertdialog) {
      return @"AXApplicationAlertDialog";
    }
    if (roleAtom == nsGkAtoms::dialog) {
      return @"AXApplicationDialog";
    }
  }

  // `macSubrole` may be the literal `nil` specified in RoleMap. We cast it to
  // NSString* before dispatching on it because `nil` expands to `nullptr`
  // during macro substitution, per a typedef in the SDK. The compiler won't
  // allow an obj cpp message send on a `nullptr`, though dispatcing on `nil` is
  // traditionally supported and safe in obj cpp.
#define ROLE(geckoRole, stringRole, ariaRole, atkRole, macRole, macSubrole, \
             msaaRole, ia2Role, androidClass, iosIsElement, uiaControlType, \
             nameRule)                                                      \
  case roles::geckoRole:                                                    \
    if (![(NSString*)macSubrole                                             \
            isEqualToString:NSAccessibilityUnknownSubrole]) {               \
      return macSubrole;                                                    \
    } else {                                                                \
      break;                                                                \
    }

  switch (mRole) {
#include "RoleMap.inc"
  }

  // These are special. They map to roles::NOTHING
  // and are instructed by the ARIA map to use the native host role.
  roleAtom = [self ARIARole];

  if (roleAtom == nsGkAtoms::log) {
    return @"AXApplicationLog";
  }

  if (roleAtom == nsGkAtoms::timer) {
    return @"AXApplicationTimer";
  }
  // macOS added an AXSubrole value to distinguish generic AXGroup objects
  // from those which are AXGroups as a result of an explicit ARIA role,
  // such as the non-landmark, non-listitem text containers in DPub ARIA.
  if (mRole == roles::FOOTNOTE || mRole == roles::SECTION) {
    return @"AXApplicationGroup";
  }

  return NSAccessibilityUnknownSubrole;

#undef ROLE
}

struct RoleDescrMap {
  NSString* role;
  const nsLiteralString description;
};

static constexpr RoleDescrMap sRoleDescrMap[] = {
    {@"AXApplicationAlert", u"alert"_ns},
    {@"AXApplicationAlertDialog", u"alertDialog"_ns},
    {@"AXApplicationDialog", u"dialog"_ns},
    {@"AXApplicationLog", u"log"_ns},
    {@"AXApplicationMarquee", u"marquee"_ns},
    {@"AXApplicationStatus", u"status"_ns},
    {@"AXApplicationTimer", u"timer"_ns},
    {@"AXContentSeparator", u"separator"_ns},
    {@"AXDefinition", u"definition"_ns},
    {@"AXDetails", u"details"_ns},
    {@"AXDocument", u"document"_ns},
    {@"AXDocumentArticle", u"article"_ns},
    {@"AXDocumentMath", u"math"_ns},
    {@"AXDocumentNote", u"note"_ns},
    {@"AXLandmarkApplication", u"application"_ns},
    {@"AXLandmarkBanner", u"banner"_ns},
    {@"AXLandmarkComplementary", u"complementary"_ns},
    {@"AXLandmarkContentInfo", u"content"_ns},
    {@"AXLandmarkMain", u"main"_ns},
    {@"AXLandmarkNavigation", u"navigation"_ns},
    {@"AXLandmarkRegion", u"region"_ns},
    {@"AXLandmarkSearch", u"search"_ns},
    {@"AXSearchField", u"searchTextField"_ns},
    {@"AXSummary", u"summary"_ns},
    {@"AXTabPanel", u"tabPanel"_ns},
    {@"AXTerm", u"term"_ns},
    {@"AXUserInterfaceTooltip", u"tooltip"_ns}};

struct RoleDescrComparator {
  const NSString* mRole;
  explicit RoleDescrComparator(const NSString* aRole) : mRole(aRole) {}
  int operator()(const RoleDescrMap& aEntry) const {
    return [mRole compare:aEntry.role];
  }
};

- (NSString*)moxRoleDescription {
  if (NSString* ariaRoleDescription =
          utils::GetAccAttr(self, nsGkAtoms::aria_roledescription)) {
    if ([ariaRoleDescription length]) {
      return ariaRoleDescription;
    }
  }

  if (mRole == roles::FIGURE) return utils::LocalizedString(u"figure"_ns);

  if (mRole == roles::HEADING) return utils::LocalizedString(u"heading"_ns);

  if (mRole == roles::MARK) {
    return utils::LocalizedString(u"highlight"_ns);
  }

  NSString* subrole = [self moxSubrole];

  if (subrole) {
    size_t idx = 0;
    if (BinarySearchIf(sRoleDescrMap, 0, std::size(sRoleDescrMap),
                       RoleDescrComparator(subrole), &idx)) {
      return utils::LocalizedString(sRoleDescrMap[idx].description);
    }
  }

  return NSAccessibilityRoleDescription([self moxRole], subrole);
}

static bool ProvidesTitle(const Accessible* aAccessible, nsString& aName) {
  ENameValueFlag flag = aAccessible->Name(aName);

  switch (aAccessible->Role()) {
    case roles::PAGETAB:
    case roles::COMBOBOX_OPTION:
    case roles::OPTION:
    case roles::PARENT_MENUITEM:
    case roles::MENUITEM:
      // These roles always supply a title.
      return true;
    case roles::GROUPING:
    case roles::RADIO_GROUP:
    case roles::DOCUMENT:
    case roles::OUTLINE:
    case roles::ARTICLE:
    case roles::FIGURE:
      // These roles never supply a title.
      return false;
    default:
      break;
  }

  // If the name was calculated from visible text (eg. label or subtree), we
  // supply a title.
  return flag != eNameOK;
}

- (NSString*)moxLabel {
  if ([self isExpired]) {
    return nil;
  }

  nsAutoString name;

  if (ProvidesTitle(mGeckoAccessible, name)) {
    // If it provides title, it is not a description.
    return nil;
  }

  return nsCocoaUtils::ToNSString(name);
}

- (NSString*)moxTitle {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  nsAutoString name;
  if (!ProvidesTitle(mGeckoAccessible, name)) {
    return @"";
  }

  if (nsCoreUtils::IsWhitespaceString(name)) {
    return @"";
  }

  return nsCocoaUtils::ToNSString(name);

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (id)moxValue {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  nsAutoString value;
  mGeckoAccessible->Value(value);

  return nsCocoaUtils::ToNSString(value);

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (NSString*)moxHelp {
  nsAutoString desc;
  EDescriptionValueFlag descFlag = mGeckoAccessible->Description(desc);

  if (@available(macOS 11.0, *)) {
    if (descFlag == eDescriptionFromARIA) {
      return nil;
    }
  }

  return nsCocoaUtils::ToNSString(desc);
}

- (NSArray*)moxCustomContent {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (@available(macOS 11.0, *)) {
    nsAutoString desc;
    EDescriptionValueFlag descFlag = mGeckoAccessible->Description(desc);

    if (!desc.IsEmpty() && descFlag == eDescriptionFromARIA) {
      AXCustomContent* contentItem = [AXCustomContent
          customContentWithLabel:@"description"
                           value:nsCocoaUtils::ToNSString(desc)];
      contentItem.importance = AXCustomContentImportanceHigh;
      return @[ contentItem ];
    }
  }

  return nil;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (NSArray*)moxCustomActions {
  if (@available(macOS 13.0, *)) {
    NSMutableArray<NSAccessibilityCustomAction*>* customActions =
        [[[NSMutableArray alloc] init] autorelease];
    Relation relatedActions(
        mGeckoAccessible->RelationByType(RelationType::ACTION));
    while (Accessible* target = relatedActions.Next()) {
      if (target->HasPrimaryAction()) {
        // Any ACTION related accesibles should be considered a custom action.
        mozAccessible* nativeTarget = GetNativeFromGeckoAccessible(target);
        if (nativeTarget) {
          nsAutoString name;
          // Use the name of the target as the action name.
          target->Name(name);
          NSAccessibilityCustomAction* action =
              [[NSAccessibilityCustomAction alloc]
                  initWithName:nsCocoaUtils::ToNSString(name)
                        target:nativeTarget
                      selector:@selector(moxPerformPress)];
          [customActions addObject:action];
        }
      }
    }
    return customActions;
  }

  return nil;
}

- (NSWindow*)moxWindow {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  // Get a pointer to the native window (NSWindow) we reside in.
  NSWindow* nativeWindow = nil;
  DocAccessible* docAcc = nullptr;
  if (LocalAccessible* acc = mGeckoAccessible->AsLocal()) {
    docAcc = acc->Document();
  } else {
    RemoteAccessible* proxy = mGeckoAccessible->AsRemote();
    LocalAccessible* outerDoc = proxy->OuterDocOfRemoteBrowser();
    if (outerDoc) docAcc = outerDoc->Document();
  }

  if (docAcc) nativeWindow = static_cast<NSWindow*>(docAcc->GetNativeWindow());

  // We won't be able to fetch a native window if we're running in a headless
  // environment, or if the AccService hasn't been started. The latter can
  // happen in tests that enable a11y in the content but not parent processes,
  // like devtools.
  if (NS_WARN_IF(!nativeWindow && !gfxPlatform::IsHeadless() &&
                 GetAccService())) {
    // Sometimes we fail to get a native window for other reasons. We should
    // investigate why this happens.
    return nil;
  }

  return nativeWindow;

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}

- (NSNumber*)moxEnabled {
  if ([self stateWithMask:states::UNAVAILABLE]) {
    return @NO;
  }

  if (![self isRoot]) {
    mozAccessible* parent = (mozAccessible*)[self moxUnignoredParent];
    if (![parent isRoot] &&
        [parent respondsToSelector:@selector(disableChild:)]) {
      // We may end up with an item in an NSChildView that we did not create.
      // Verify `disableChild` is a valid dispatch, else this will throw an
      // exception.
      return @(![parent disableChild:self]);
    }
  }

  return @YES;
}

- (NSString*)moxInvalid {
  // For controls that support text input, we will expose
  // the string value of `aria-invalid` when it exists.
  // See mozTextAccessible::moxInvalid for that work.
  // Unfortunately, NSBools do not autoconvert to usable
  // NSStrings, so we expose "true" and "false" manually.
  return ([self stateWithMask:states::INVALID] != 0) ? @"true" : @"false";
}

- (NSArray*)moxErrorMessageElements {
  if (![[self moxInvalid] isEqualToString:@"false"]) {
    NSArray* relations = [self getRelationsByType:RelationType::ERRORMSG];
    if ([relations count] > 0) {
      return relations;
    }
  }

  return nil;
}

- (NSNumber*)moxFocused {
  return @([self stateWithMask:states::FOCUSED] != 0);
}

- (NSNumber*)moxSelected {
  return @NO;
}

- (NSNumber*)moxGrabbed {
  NSString* grabbed = utils::GetAccAttr(self, nsGkAtoms::aria_grabbed);
  return @([grabbed isEqualToString:@"true"]);
}

- (NSNumber*)moxExpanded {
  return @([self stateWithMask:states::EXPANDED] != 0);
}

- (NSValue*)moxFrame {
  MOZ_ASSERT(mGeckoAccessible);
  auto rect = mGeckoAccessible->Bounds();
  return
      [NSValue valueWithRect:utils::GetCocoaScreenRectForAcc(
                                 self, rect, /*aShouldUseCocoaCoords*/ true)];
}

- (NSString*)moxARIACurrent {
  if (![self stateWithMask:states::CURRENT]) {
    return nil;
  }

  return utils::GetAccAttr(self, nsGkAtoms::aria_current);
}

- (NSNumber*)moxARIAAtomic {
  return @(utils::GetAccAttr(self, nsGkAtoms::aria_atomic) != nil);
}

- (NSString*)moxARIALive {
  return utils::GetAccAttr(self, nsGkAtoms::aria_live);
}

- (NSNumber*)moxARIAPosInSet {
  GroupPos groupPos = mGeckoAccessible->GroupPosition();
  return @(groupPos.posInSet);
}

- (NSNumber*)moxARIASetSize {
  GroupPos groupPos = mGeckoAccessible->GroupPosition();
  return @(groupPos.setSize);
}

- (NSString*)moxARIABrailleRoleDescription {
  return utils::GetAccAttr(self, nsGkAtoms::aria_brailleroledescription);
}

- (NSString*)moxARIABrailleLabel {
  return utils::GetAccAttr(self, nsGkAtoms::aria_braillelabel);
}

- (NSString*)moxARIARelevant {
  if (NSString* relevant =
          utils::GetAccAttr(self, nsGkAtoms::containerRelevant)) {
    return relevant;
  }

  // Default aria-relevant value
  return @"additions text";
}

- (NSString*)moxPlaceholderValue {
  // First, check for plaecholder HTML attribute
  if (NSString* placeholder = utils::GetAccAttr(self, nsGkAtoms::placeholder)) {
    return placeholder;
  }

  // If no placeholder HTML attribute, check for the aria version.
  return utils::GetAccAttr(self, nsGkAtoms::aria_placeholder);
}

- (id)moxTitleUIElement {
  MOZ_ASSERT(mGeckoAccessible);

  nsAutoString unused;
  if (mGeckoAccessible->Name(unused) != eNameFromRelations) {
    return nil;
  }

  Relation rel = mGeckoAccessible->RelationByType(RelationType::LABELLED_BY);
  Accessible* label = rel.Next();
  if (!label || rel.Next()) {
    // Zero or more than one relation.
    return nil;
  }

  if (label->IsAncestorOf(mGeckoAccessible)) {
    // Don't support labelling for a relation that references an ancestor.
    // VO walks the label's subtree and tries to construct the name for the
    // control. It does not strip whitespace from the text leaf children, and
    // since the calculated name of the control is stripped, it sees it as two
    // different names and inclues both.
    return nil;
  }

  if (RefPtr<nsAtom>(label->DisplayStyle()) == nsGkAtoms::block) {
    TextLeafPoint endPoint =
        TextLeafPoint(label, nsIAccessibleText::TEXT_OFFSET_END_OF_TEXT)
            .FindBoundary(nsIAccessibleText::BOUNDARY_CHAR, eDirPrevious);
    if (endPoint.IsSpace()) {
      // A label that is a block element with trailing space causes VO be
      // unhappy.
      return nil;
    }
  }

  return GetNativeFromGeckoAccessible(label);
}

- (NSString*)moxDOMIdentifier {
  MOZ_ASSERT(mGeckoAccessible);

  nsAutoString id;
  mGeckoAccessible->DOMNodeID(id);

  return nsCocoaUtils::ToNSString(id);
}

- (NSNumber*)moxRequired {
  return @([self stateWithMask:states::REQUIRED] != 0);
}

- (NSNumber*)moxElementBusy {
  return @([self stateWithMask:states::BUSY] != 0);
}

- (NSArray*)moxLinkedUIElements {
  // AAM says to use this method to expose both flows-to and controller-for
  // relations.
  NSArray* controls = [self getRelationsByType:RelationType::CONTROLLER_FOR];
  NSArray* flows = [self getRelationsByType:RelationType::FLOWS_TO];

  if ([controls count] && [flows count]) {
    // If both are used, construct an array that holds all targets.
    NSArray* allLinkedElements = [controls arrayByAddingObjectsFromArray:flows];
    // Remove duplicates before returning
    return [[NSSet setWithArray:allLinkedElements] allObjects];
  }
  // If we only have one relation, return only that rel's targets.
  return [controls count] ? controls : flows;
}

- (NSArray*)moxDetailsElements {
  return [self getRelationsByType:RelationType::DETAILS];
}

- (mozAccessible*)topWebArea {
  Accessible* doc = nsAccUtils::DocumentFor(mGeckoAccessible);
  while (doc) {
    if (doc->IsLocal()) {
      DocAccessible* docAcc = doc->AsLocal()->AsDoc();
      if (docAcc->DocumentNode()->GetBrowsingContext()->IsTopContent()) {
        return GetNativeFromGeckoAccessible(docAcc);
      }

      doc = docAcc->ParentDocument();
    } else {
      DocAccessibleParent* docProxy = doc->AsRemote()->AsDoc();
      if (docProxy->IsTopLevel()) {
        return GetNativeFromGeckoAccessible(docProxy);
      }
      doc = docProxy->ParentDoc();
    }
  }

  return nil;
}

- (void)handleRoleChanged:(mozilla::a11y::role)newRole {
  mRole = newRole;
  mARIARole = nullptr;

  // For testing purposes
  [self moxPostNotification:@"AXMozRoleChanged"];
}

- (id)moxEditableAncestor {
  return [self moxFindAncestor:^BOOL(id<MOXAccessible> moxAcc, BOOL* stop) {
    return [moxAcc moxIsTextField];
  }];
}

- (id)moxHighestEditableAncestor {
  id highestAncestor = [self moxEditableAncestor];
  while ([highestAncestor conformsToProtocol:@protocol(MOXAccessible)]) {
    id ancestorParent = [highestAncestor moxUnignoredParent];
    if (![ancestorParent conformsToProtocol:@protocol(MOXAccessible)]) {
      break;
    }

    id higherAncestor = [ancestorParent moxEditableAncestor];

    if (!higherAncestor) {
      break;
    }

    highestAncestor = higherAncestor;
  }

  return highestAncestor;
}

- (id)moxFocusableAncestor {
  // XXX: Checking focusable state up the chain can be expensive. For now,
  // we can just return AXEditableAncestor since the main use case for this
  // is rich text editing with links.
  return [self moxEditableAncestor];
}

- (NSString*)moxLanguage {
  MOZ_ASSERT(mGeckoAccessible);

  nsAutoString lang;
  mGeckoAccessible->Language(lang);

  return nsCocoaUtils::ToNSString(lang);
}

- (NSString*)moxKeyShortcutsValue {
  MOZ_ASSERT(mGeckoAccessible);

  nsAutoString shortcut;

  if (!mGeckoAccessible->GetStringARIAAttr(nsGkAtoms::aria_keyshortcuts,
                                           shortcut)) {
    return nil;
  }

  // Rename the Meta and Alt keys to the labels printed on Apple keyboards.
  nsAutoString platformShortcut;
  auto processAndAppendKey = [&](const nsAString& aKey) {
    if (aKey.LowerCaseEqualsLiteral("meta")) {
      platformShortcut.AppendLiteral("Command");
    } else if (aKey.LowerCaseEqualsLiteral("alt")) {
      platformShortcut.AppendLiteral("Option");
    } else {
      platformShortcut.Append(aKey);
    }
  };
  uint32_t keyStart = 0;
  for (uint32_t i = 0; i < shortcut.Length(); i++) {
    char16_t currChar = shortcut[i];
    if (currChar == '+' || nsCoreUtils::IsWhitespace(currChar)) {
      // If we've hit a delimiter, fetch the preceding key, convert it, and
      // append it.
      processAndAppendKey(Substring(shortcut, keyStart, i - keyStart));
      platformShortcut.Append(currChar);
      keyStart = i + 1;
    }
  }
  // Handle the final key
  processAndAppendKey(Substring(shortcut, keyStart));

  return nsCocoaUtils::ToNSString(platformShortcut);
}

#ifndef RELEASE_OR_BETA
- (NSString*)moxMozDebugDescription {
  NS_OBJC_BEGIN_TRY_BLOCK_RETURN;

  if (!mGeckoAccessible) {
    return [NSString stringWithFormat:@"<%@: %p mGeckoAccessible=null>",
                                      NSStringFromClass([self class]), self];
  }

  NSMutableString* domInfo = [NSMutableString string];
  if (NSString* tagName = utils::GetAccAttr(self, nsGkAtoms::tag)) {
    [domInfo appendFormat:@" %@", tagName];
    NSString* domID = [self moxDOMIdentifier];
    if ([domID length]) {
      [domInfo appendFormat:@"#%@", domID];
    }
    if (NSString* className = utils::GetAccAttr(self, nsGkAtoms::_class)) {
      [domInfo
          appendFormat:@".%@",
                       [className stringByReplacingOccurrencesOfString:@" "
                                                            withString:@"."]];
    }
  }

  return [NSString stringWithFormat:@"<%@: %p %@%@>",
                                    NSStringFromClass([self class]), self,
                                    [self moxRole], domInfo];

  NS_OBJC_END_TRY_BLOCK_RETURN(nil);
}
#endif

- (NSArray*)moxUIElementsForSearchPredicate:(NSDictionary*)searchPredicate {
  // Create our search object and set it up with the searchPredicate
  // params. The init function does additional parsing. We pass a
  // reference to the web area to use as a start element if one is not
  // specified.
  MOXSearchInfo* search =
      [[[MOXSearchInfo alloc] initWithParameters:searchPredicate
                                         andRoot:self] autorelease];

  return [search performSearch];
}

- (NSNumber*)moxUIElementCountForSearchPredicate:
    (NSDictionary*)searchPredicate {
  return [NSNumber
      numberWithDouble:[[self moxUIElementsForSearchPredicate:searchPredicate]
                           count]];
}

- (void)moxSetFocused:(NSNumber*)focused {
  MOZ_ASSERT(mGeckoAccessible);

  if ([focused boolValue]) {
    mGeckoAccessible->TakeFocus();
  }
}

- (void)moxPerformScrollToVisible {
  MOZ_ASSERT(mGeckoAccessible);
  if (RefPtr<Accessible> acc = mGeckoAccessible) {
    acc->ScrollTo(nsIAccessibleScrollType::SCROLL_TYPE_ANYWHERE);
  }
}

- (void)moxPerformShowMenu {
  MOZ_ASSERT(mGeckoAccessible);

  // We don't need to convert this rect into mac coordinates because the
  // mouse event synthesizer expects layout (gecko) coordinates.
  LayoutDeviceIntRect bounds = mGeckoAccessible->Bounds();

  LocalAccessible* rootAcc = mGeckoAccessible->IsLocal()
                                 ? mGeckoAccessible->AsLocal()->RootAccessible()
                                 : mGeckoAccessible->AsRemote()
                                       ->OuterDocOfRemoteBrowser()
                                       ->RootAccessible();
  id objOrView =
      GetObjectOrRepresentedView(GetNativeFromGeckoAccessible(rootAcc));

  LayoutDeviceIntPoint p = LayoutDeviceIntPoint(
      bounds.X() + (bounds.Width() / 2), bounds.Y() + (bounds.Height() / 2));
  nsIWidget* widget = [objOrView widget];
  widget->SynthesizeNativeMouseEvent(
      p, nsIWidget::NativeMouseMessage::ButtonDown, MouseButton::eSecondary,
      nsIWidget::NativeModifiers::NO_MODIFIERS, nullptr);
}

- (void)moxPerformPress {
  MOZ_ASSERT(mGeckoAccessible);

  mGeckoAccessible->DoAction(0);
}

#pragma mark -

- (BOOL)disableChild:(mozAccessible*)child {
  return NO;
}

- (void)maybePostA11yUtilNotification {
  MOZ_ASSERT(mGeckoAccessible);
  // Sometimes we use a special live region to make announcements to the user.
  // This region is a child of the root document, but doesn't contain any
  // content. If we try to fire regular AXLiveRegion changed events through it,
  // VoiceOver clips the notifications because it (rightfully) doesn't detect
  // focus within the region. We get around this by firing an
  // AXAnnouncementRequested notification here instead.
  // Verify we're trying to send a notification for the a11yUtils alert (and not
  // a random acc with the same ID) by checking:
  //  - The gecko acc is local, our a11y-announcement lives in browser.xhtml
  //  - The ID of the gecko acc is "a11y-announcement"
  //  - The native acc is a direct descendent of the chrome window.
  DocAccessible* maybeRoot = mGeckoAccessible->IsLocal()
                                 ? mGeckoAccessible->AsLocal()->Document()
                                 : nullptr;
  if (maybeRoot && maybeRoot->IsRoot() &&
      [[self moxDOMIdentifier] isEqualToString:@"a11y-announcement"]) {
    nsAutoString name;
    // Our actual announcement should be stored as a child of the alert.
    if (Accessible* announcement = mGeckoAccessible->FirstChild()) {
      announcement->Name(name);
    } else {
      // This can happen if a modal dialog is opened, which removes everything
      // else from the accessibility tree, and then the modal is dismissed,
      // which inserts everything else again. This causes Gecko to fire an alert
      // event on a11y-announcement (even though it's empty) since it was just
      // shown.
      NS_WARNING("A11yUtil event received, but no announcement found");
    }

    NSDictionary* info = @{
      NSAccessibilityAnnouncementKey : name.IsEmpty()
          ? @("")
          : nsCocoaUtils::ToNSString(name),
      // High priority means VO will stop what it is currently speaking
      // to speak our announcement.
      NSAccessibilityPriorityKey : @(NSAccessibilityPriorityHigh)
    };

    // This sends events via nsIObserverService to be consumed by our
    // mochitests. Normally we'd fire these events through moxPostNotification
    // which takes care of this, but because NSApp isn't derived
    // from MOXAccessibleBase, we do this (and post the notification) manually.
    // We used to fire this on the window, but per Chrome and Safari these
    // notifs get dropped if fired on any non-main window. We now fire on NSApp
    // to avoid this.
    xpcAccessibleMacEvent::FireEvent(
        GetNativeFromGeckoAccessible(maybeRoot),
        NSAccessibilityAnnouncementRequestedNotification, info);
    NSAccessibilityPostNotificationWithUserInfo(
        NSApp, NSAccessibilityAnnouncementRequestedNotification, info);
  }
}

- (NSArray<mozAccessible*>*)getRelationsByType:(RelationType)relationType {
  NSMutableArray<mozAccessible*>* relations =
      [[[NSMutableArray alloc] init] autorelease];
  Relation rel = mGeckoAccessible->RelationByType(relationType);
  while (Accessible* relAcc = rel.Next()) {
    if (mozAccessible* relNative = GetNativeFromGeckoAccessible(relAcc)) {
      [relations addObject:relNative];
    }
  }

  return relations;
}
- (void)maybeFireUAZoomChangeFocusEvent:(int)focusType {
  if (!mGeckoAccessible) {
    return;
  }

  auto bounds = mGeckoAccessible->Bounds();
  if (bounds.IsEmpty()) {
    if (auto parent = mGeckoAccessible->Parent()) {
      auto parentBounds = parent->Bounds();
      bounds.width = parentBounds.width;
      bounds.height = parentBounds.height;
    }
    // UAZoom drops all events with rects that have height=0 or width=0. Ensure
    // we don't send something empty by clamping to 1x1.
    bounds.width = MAX(bounds.width, 1.0);
    bounds.height = MAX(bounds.height, 1.0);
  }

  NSRect objectRect = utils::GetCocoaScreenRectForAcc(
      self, bounds, /*aShouldUseCocoaCoords*/ false);

  NSRect highlightRect = NSZeroRect;
  if (auto* htb = mGeckoAccessible->AsHyperTextBase()) {
    auto caretRect = htb->GetCaretRect().first;
    highlightRect = utils::GetCocoaScreenRectForAcc(
        self, caretRect, /*aShouldUseCocoaCoords*/ false);
  }

  NSValue* objValue = [NSValue valueWithRect:objectRect];
  NSMutableDictionary* info =
      [[@{@"AXRect" : objValue, @"AXFocusType" : @(focusType)} mutableCopy]
          autorelease];

  if (focusType == kUAZoomFocusTypeInsertionPoint) {
    info[@"AXHighlightRect"] = [NSValue valueWithRect:highlightRect];
  }

  // This sends events via nsIObserverService to be consumed by our mochitests.
  // We provide the acc to re-use the existing xpc logic and to more easily
  // verify the event is correct, without diff'ing rects.
  // Note the UAZoomChangeFocus call below is not associated with a
  // mozAccessible; it takes raw rects.
  xpcAccessibleMacEvent::FireEvent(self, @"MozUAZoomChangeFocus", info);

  // Check if our desired AT is enabled before firing the
  // event. Always fire the event for tests.
  if (UAZoomEnabled()) {
    UAZoomChangeFocus(
        &objectRect,
        focusType == kUAZoomFocusTypeInsertionPoint ? &highlightRect : nullptr,
        focusType);
  }
}

- (void)handleAccessibleEvent:(uint32_t)eventType {
  switch (eventType) {
    case nsIAccessibleEvent::EVENT_ALERT:
      [self maybePostA11yUtilNotification];
      break;
    case nsIAccessibleEvent::EVENT_FOCUS: {
      [self moxPostNotification:
                NSAccessibilityFocusedUIElementChangedNotification];

      // Notify macOS zoom/magnifier about focus change, if we're monitoring
      if (PlatformShouldTrackFocusedAccLocation()) {
        if (mGeckoAccessible->IsEditableRoot()) {
          // If this acc is an editable root, use kUAZoomFocusTypeInsertionPoint
          // to ensure the caret rect is sent, too.
          [self maybeFireUAZoomChangeFocusEvent:kUAZoomFocusTypeInsertionPoint];
        } else {
          [self maybeFireUAZoomChangeFocusEvent:kUAZoomFocusTypeOther];
        }
      }
      break;
    }
    case nsIAccessibleEvent::EVENT_MENUPOPUP_START:
      [self moxPostNotification:@"AXMenuOpened"];
      break;
    case nsIAccessibleEvent::EVENT_MENUPOPUP_END:
      [self moxPostNotification:@"AXMenuClosed"];
      break;
    case nsIAccessibleEvent::EVENT_SELECTION:
    case nsIAccessibleEvent::EVENT_SELECTION_ADD:
    case nsIAccessibleEvent::EVENT_SELECTION_REMOVE:
    case nsIAccessibleEvent::EVENT_SELECTION_WITHIN:
      [self moxPostNotification:
                NSAccessibilitySelectedChildrenChangedNotification];
      break;
    case nsIAccessibleEvent::EVENT_TEXT_CARET_MOVED: {
      if (![self stateWithMask:states::SELECTABLE_TEXT]) {
        break;
      }
      // We consider any caret move event to be a selected text change event.
      // So dispatching an event for EVENT_TEXT_SELECTION_CHANGED would be
      // reduntant.
      MOXTextMarkerDelegate* delegate =
          static_cast<MOXTextMarkerDelegate*>([self moxTextMarkerDelegate]);
      NSMutableDictionary* userInfo =
          [[[delegate selectionChangeInfo] mutableCopy] autorelease];
      userInfo[@"AXTextChangeElement"] = self;

      mozAccessible* webArea = [self topWebArea];
      [webArea
          moxPostNotification:NSAccessibilitySelectedTextChangedNotification
                 withUserInfo:userInfo];
      [self moxPostNotification:NSAccessibilitySelectedTextChangedNotification
                   withUserInfo:userInfo];

      // Notify macOS zoom/magnifier about caret position, if we're tracking
      if (PlatformShouldTrackFocusedAccLocation()) {
        [self maybeFireUAZoomChangeFocusEvent:kUAZoomFocusTypeInsertionPoint];
      }
      break;
    }
    case nsIAccessibleEvent::EVENT_LIVE_REGION_ADDED:
      mIsLiveRegion = true;
      [self moxPostNotification:@"AXLiveRegionCreated"];
      break;
    case nsIAccessibleEvent::EVENT_LIVE_REGION_REMOVED:
      mIsLiveRegion = false;
      break;
    case nsIAccessibleEvent::EVENT_NAME_CHANGE: {
      // Don't want to passively activate the cache because a name changed.
      CacheDomainActivationBlocker cacheBlocker;
      nsAutoString nameNotUsed;
      if (ProvidesTitle(mGeckoAccessible, nameNotUsed)) {
        [self moxPostNotification:NSAccessibilityTitleChangedNotification];
      }
      break;
    }
    case nsIAccessibleEvent::EVENT_DESCRIPTION_CHANGE: {
      // There is no specific description-change event on macOS, so we use the
      // announcement requested notification to expose this change manually.
      nsAutoString description;
      mGeckoAccessible->Description(description);
      if (!description.IsEmpty()) {
        [self handleAnnouncementEvent:nsCocoaUtils::ToNSString(description)
                             priority:nsIAccessibleAnnouncementEvent::POLITE];
      }
      break;
    }
    case nsIAccessibleEvent::EVENT_LIVE_REGION_CHANGED: {
      MOZ_ASSERT(mIsLiveRegion);
      [self moxPostNotification:@"AXLiveRegionChanged"];
      // For live region events originating from local accs, we also want to
      // post an AXAnnouncementRequested notification. This is because:
      // - The AXLiveRegionChanged notif above does not support an announcement
      // string; we have to depend on VO's tree walking to compose the string
      // correctly, and it often comes up empty which results in no
      // announcement.
      // - WebKit posts this notification for _every_ live region change,
      // indicating perhaps it is necessary for VO to function properly. Chrome
      // does not.
      if (mGeckoAccessible->IsLocal()) {
        if (NSString* announcementText =
                [self composeAnnouncementMessageFromSubtree]) {
          nsAutoString live;
          nsAccUtils::GetLiveRegionSetting(mGeckoAccessible->AsLocal(), live);
          uint16_t priority = live.EqualsLiteral("assertive")
                                  ? nsIAccessibleAnnouncementEvent::ASSERTIVE
                                  : nsIAccessibleAnnouncementEvent::POLITE;
          [self handleAnnouncementEvent:announcementText priority:priority];
        }
      }
      break;
    }
    case nsIAccessibleEvent::EVENT_ERRORMESSAGE_CHANGED: {
      // aria-errormessage was changed. If aria-invalid != "true", it means that
      // VoiceOver should (a) expose a new message or (b) remove an
      // old message
      if (![[self moxInvalid] isEqualToString:@"false"]) {
        [self moxPostNotification:@"AXValidationErrorChanged"];
      }

      break;
    }
    case nsIAccessibleEvent::EVENT_SHOW: {
      if (IsNonNativePopover(mGeckoAccessible)) {
        [self moxPostNotification:@"AXCreated"];
      }
      break;
    }
  }
}

- (void)maybePostValidationErrorChanged {
  CacheDomainActivationBlocker cacheBlocker;
  NSArray* relations =
      [self getRelationsByType:(mozilla::a11y::RelationType::ERRORMSG_FOR)];
  if ([relations count] > 0) {
    // only fire AXValidationErrorChanged if related node is not
    // `aria-invalid="false"`
    for (mozAccessible* related : relations) {
      NSString* invalidStr = [related moxInvalid];
      if (![invalidStr isEqualToString:@"false"]) {
        [self moxPostNotification:@"AXValidationErrorChanged"];
        break;
      }
    }
  }
}

- (NSString*)composeAnnouncementMessageFromSubtree {
  nsAutoString text;
  nsTextEquivUtils::GetTextEquivFromSubtree(mGeckoAccessible, text);
  if (!text.IsEmpty()) {
    return nsCocoaUtils::ToNSString(text);
  }
  NSString* label = [self moxLabel];
  return [label length] ? label : nil;
}

- (void)handleAnnouncementEvent:(NSString*)announcement
                       priority:(uint16_t)priority {
  NSDictionary* info = @{
    NSAccessibilityAnnouncementKey : announcement,
    NSAccessibilityPriorityKey :
                priority == nsIAccessibleAnnouncementEvent::ASSERTIVE
        ? @(NSAccessibilityPriorityHigh)
        : @(NSAccessibilityPriorityMedium)
  };
  [self moxPostNotification:NSAccessibilityAnnouncementRequestedNotification
               withUserInfo:info];
}

- (void)expire {
  NS_OBJC_BEGIN_TRY_IGNORE_BLOCK;

  mGeckoAccessible = nullptr;

  [self moxPostNotification:NSAccessibilityUIElementDestroyedNotification];

  NS_OBJC_END_TRY_IGNORE_BLOCK;
}

- (BOOL)isExpired {
  return !mGeckoAccessible;
}

@end
