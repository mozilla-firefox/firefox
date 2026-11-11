/* clang-format off */
/* clang-format on */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>

#import "MOXTextMarkerDelegate.h"

#include "Platform.h"
#include "RemoteAccessible.h"
#include "DocAccessibleParent.h"
#include "mozTableAccessible.h"
#include "mozTextAccessible.h"
#include "MOXOuterDoc.h"
#include "MOXWebAreaAccessible.h"
#include "nsAccUtils.h"
#include "TextRange.h"

#include "nsAppShell.h"
#include "nsCocoaUtils.h"
#include "mozilla/EnumSet.h"
#include "mozilla/glean/AccessibleMetrics.h"

// Available from 10.13 onwards; test availability at runtime before using
@interface NSWorkspace (AvailableSinceHighSierra)
@property(readonly) BOOL isVoiceOverEnabled;
@property(readonly) BOOL isSwitchControlEnabled;
@end

namespace mozilla {
namespace a11y {

// Mac a11y whitelisting
static bool sA11yShouldBeEnabled = false;

bool ShouldA11yBeEnabled() {
  EPlatformDisabledState disabledState = PlatformDisabledState();
  return (disabledState == ePlatformIsForceEnabled) ||
         ((disabledState == ePlatformIsEnabled) && sA11yShouldBeEnabled);
}

void PlatformInit() {}

void PlatformShutdown() {}

void ProxyCreated(RemoteAccessible* aProxy) {
  if (aProxy->Role() == roles::WHITESPACE) {
    // We don't create a native object if we're child of a "flat" accessible;
    // for example, on OS X buttons shouldn't have any children, because that
    // makes the OS confused. We also don't create accessibles for <br>
    // (whitespace) elements.
    return;
  }

  // Pass in dummy state for now as retrieving proxy state requires IPC.
  // Note that we can use RemoteAccessible::IsTable* functions here because they
  // do not use IPC calls but that might change after bug 1210477.
  Class type;
  if (aProxy->IsTable()) {
    type = [mozTableAccessible class];
  } else if (aProxy->IsTableRow()) {
    type = [mozTableRowAccessible class];
  } else if (aProxy->IsTableCell()) {
    type = [mozTableCellAccessible class];
  } else if (aProxy->IsDoc()) {
    type = [MOXWebAreaAccessible class];
  } else if (aProxy->IsOuterDoc()) {
    type = [MOXOuterDoc class];
  } else {
    type = GetTypeFromRole(aProxy->Role());
  }

  mozAccessible* mozWrapper = [[type alloc] initWithAccessible:aProxy];
  aProxy->SetWrapper(reinterpret_cast<uintptr_t>(mozWrapper));
}

void ProxyDestroyed(RemoteAccessible* aProxy) {
  mozAccessible* wrapper = GetNativeFromGeckoAccessible(aProxy);
  [wrapper expire];
  [wrapper release];
  aProxy->SetWrapper(0);

  if (aProxy->IsDoc()) {
    [MOXTextMarkerDelegate destroyForDoc:aProxy];
  }
}

void PlatformEvent(Accessible* aTarget, uint32_t aEventType) {
  // Ignore event that we don't escape below, they aren't yet supported.
  if (aEventType != nsIAccessibleEvent::EVENT_ALERT &&
      aEventType != nsIAccessibleEvent::EVENT_VALUE_CHANGE &&
      aEventType != nsIAccessibleEvent::EVENT_TEXT_VALUE_CHANGE &&
      aEventType != nsIAccessibleEvent::EVENT_DOCUMENT_LOAD_COMPLETE &&
      aEventType != nsIAccessibleEvent::EVENT_REORDER &&
      aEventType != nsIAccessibleEvent::EVENT_LIVE_REGION_ADDED &&
      aEventType != nsIAccessibleEvent::EVENT_LIVE_REGION_REMOVED &&
      aEventType != nsIAccessibleEvent::EVENT_LIVE_REGION_CHANGED &&
      aEventType != nsIAccessibleEvent::EVENT_NAME_CHANGE &&
      aEventType != nsIAccessibleEvent::EVENT_DESCRIPTION_CHANGE &&
      aEventType != nsIAccessibleEvent::EVENT_OBJECT_ATTRIBUTE_CHANGED &&
      aEventType != nsIAccessibleEvent::EVENT_ERRORMESSAGE_CHANGED) {
    return;
  }

  mozAccessible* wrapper = GetNativeFromGeckoAccessible(aTarget);
  if (wrapper) {
    [wrapper handleAccessibleEvent:aEventType];
  }
}

void PlatformStateChangeEvent(Accessible* aTarget, uint64_t aState,
                              bool aEnabled) {
  mozAccessible* wrapper = GetNativeFromGeckoAccessible(aTarget);
  if (wrapper) {
    [wrapper stateChanged:aState isEnabled:aEnabled];
  }
}

void PlatformFocusEvent(Accessible* aTarget) {
  if (mozAccessible* wrapper = GetNativeFromGeckoAccessible(aTarget)) {
    [wrapper handleAccessibleEvent:nsIAccessibleEvent::EVENT_FOCUS];
  }
}

void PlatformFocusedAccLocationChanged(Accessible* aFocusedAcc) {
  // XXX: This function is called when CacheDomain::Viewport is updated on the
  // document and the focused acc's bounds have changed. We redirect here
  // instead of doing this work in maybeFireUAZoomChangeFocusEvent because
  // other platforms will want this hook, too. See bug 1469779.
  mozAccessible* nativeFocused = GetNativeFromGeckoAccessible(aFocusedAcc);
  if (nativeFocused) {
    int focusType = aFocusedAcc->IsEditableRoot()
                        ? kUAZoomFocusTypeInsertionPoint
                        : kUAZoomFocusTypeOther;
    [nativeFocused maybeFireUAZoomChangeFocusEvent:focusType];
  }
}

bool PlatformShouldTrackFocusedAccLocation() {
  // We want to track location changes for the focused acc if the
  // macOS magnifier is enabled, or we're running tests that have
  // the bounds domain enabled. We can't just check that we're in a
  // testing environment, because this will break our caching granularity
  // tests.
  return UAZoomEnabled() ||
         (xpc::IsInAutomation() && DomainsAreActive(CacheDomain::Bounds));
}

void PlatformCaretMoveEvent(Accessible* aTarget, int32_t aOffset,
                            bool aIsSelectionCollapsed, int32_t aGranularity,
                            bool aFromUser) {
  mozAccessible* wrapper = GetNativeFromGeckoAccessible(aTarget);
  MOXTextMarkerDelegate* delegate = [MOXTextMarkerDelegate
      getOrCreateForDoc:nsAccUtils::DocumentFor(aTarget)];
  [delegate setCaretOffset:aTarget at:aOffset moveGranularity:aGranularity];
  if (aIsSelectionCollapsed) {
    // If selection is collapsed, invalidate selection.
    [delegate setSelectionFrom:aTarget at:aOffset to:aTarget at:aOffset];
  }

  if (wrapper) {
    if (mozAccessible* editable = [wrapper moxEditableAncestor]) {
      [editable
          handleAccessibleEvent:nsIAccessibleEvent::EVENT_TEXT_CARET_MOVED];
    } else {
      [wrapper
          handleAccessibleEvent:nsIAccessibleEvent::EVENT_TEXT_CARET_MOVED];
    }
  }
}

void PlatformTextChangeEvent(Accessible* aTarget, const nsAString& aStr,
                             int32_t aStart, uint32_t aLen, bool aIsInsert,
                             bool aFromUser) {
  mozAccessible* wrapper = GetNativeFromGeckoAccessible(aTarget);
  if (wrapper) {
    if (mozAccessible* editable = [wrapper moxEditableAncestor]) {
      [editable handleAccessibleTextChangeEvent:nsCocoaUtils::ToNSString(aStr)
                                       inserted:aIsInsert
                                    inContainer:aTarget
                                             at:aStart];
    } else {
      [wrapper maybePostValidationErrorChanged];
    }
  }
}

void PlatformShowHideEvent(Accessible* aTarget, Accessible* aParent,
                           bool aIsInsert, bool aFromUser) {
  mozAccessible* wrapper = GetNativeFromGeckoAccessible(aTarget);
  if (wrapper) {
    [wrapper
        handleAccessibleEvent:(aIsInsert ? nsIAccessibleEvent::EVENT_SHOW
                                         : nsIAccessibleEvent::EVENT_HIDE)];
  }
}

void PlatformSelectionEvent(Accessible* aTarget, Accessible* aWidget,
                            uint32_t aEventType) {
  mozAccessible* wrapper = GetNativeFromGeckoAccessible(aWidget);
  if (wrapper) {
    [wrapper handleAccessibleEvent:aEventType];
  }
}

void PlatformTextSelectionChangeEvent(Accessible* aTarget,
                                      const nsTArray<TextRange>& aSelection) {
  if (aSelection.Length()) {
    MOXTextMarkerDelegate* delegate = [MOXTextMarkerDelegate
        getOrCreateForDoc:nsAccUtils::DocumentFor(aTarget)];
    // Cache the selection.
    [delegate setSelectionFrom:aSelection[0].StartContainer()
                            at:aSelection[0].StartOffset()
                            to:aSelection[0].EndContainer()
                            at:aSelection[0].EndOffset()];
  }

  mozAccessible* wrapper = GetNativeFromGeckoAccessible(aTarget);
  if (wrapper) {
    [wrapper
        handleAccessibleEvent:nsIAccessibleEvent::EVENT_TEXT_SELECTION_CHANGED];
  }
}

void PlatformRoleChangedEvent(Accessible* aTarget, const a11y::role& aRole,
                              uint8_t aRoleMapEntryIndex) {
  if (mozAccessible* wrapper = GetNativeFromGeckoAccessible(aTarget)) {
    [wrapper handleRoleChanged:aRole];
  }
}

void PlatformAnnouncementEvent(Accessible* aTarget,
                               const nsAString& aAnnouncement,
                               uint16_t aPriority) {
  mozAccessible* wrapper = GetNativeFromGeckoAccessible(aTarget);
  if (wrapper) {
    [wrapper handleAnnouncementEvent:nsCocoaUtils::ToNSString(aAnnouncement)
                            priority:aPriority];
  }
}

// This enum lists possible assistive technology clients. It's intended for use
// in an EnumSet since there can be multiple ATs active at once.
enum class Client : uint64_t {
  Unknown,
  VoiceOver,
  SwitchControl,
  UAZoom,
  FullKeyboardAccess,
  VoiceControl,
  SpeakSelection,
  SpeakItemUnderMouse,
  SpeakTypingFeedback,
  HoverText
};

// Get the set of currently-active clients and the client to log.
// XXX: We should log all clients, but default to the first one encountered.
// (bug 2021479)
std::pair<EnumSet<Client>, Client> GetClients() {
  EnumSet<Client> clients;
  std::optional<Client> clientToLog;
  auto AddClient = [&clients, &clientToLog](Client client) {
    clients += client;
    if (!clientToLog.has_value()) {
      clientToLog = client;
    }
  };
  if ([[NSWorkspace sharedWorkspace]
          respondsToSelector:@selector(isVoiceOverEnabled)] &&
      [[NSWorkspace sharedWorkspace] isVoiceOverEnabled]) {
    AddClient(Client::VoiceOver);
  }

  if ([[NSWorkspace sharedWorkspace]
          respondsToSelector:@selector(isSwitchControlEnabled)] &&
      [[NSWorkspace sharedWorkspace] isSwitchControlEnabled]) {
    AddClient(Client::SwitchControl);
  }

  if (UAZoomEnabled()) {
    AddClient(Client::UAZoom);
  }

  // This is more complicated than the NSWorkspace queries above
  // because (a) there is no "full keyboard access" query for NSWorkspace
  // and (b) the [NSApplication fullKeyboardAccessEnabled] query checks
  // the pre-Monterey version of full keyboard access, which is not what
  // we're looking for here. For more info, see bug 1772375 comment 7.
  Boolean exists;
  long val = CFPreferencesGetAppIntegerValue(CFSTR("FullKeyboardAccessEnabled"),
                                             CFSTR("com.apple.Accessibility"),
                                             &exists);
  if (exists && val == 1) {
    AddClient(Client::FullKeyboardAccess);
  }

  val = CFPreferencesGetAppIntegerValue(CFSTR("CommandAndControlEnabled"),
                                        CFSTR("com.apple.Accessibility"),
                                        &exists);
  if (exists && val == 1) {
    AddClient(Client::VoiceControl);
  }

  val = CFPreferencesGetAppIntegerValue(
      CFSTR("SpeakThisEnabled"), CFSTR("com.apple.universalaccess"), &exists);
  if (exists && val == 1) {
    AddClient(Client::SpeakSelection);
  }

  val = CFPreferencesGetAppIntegerValue(CFSTR("speakItemUnderMouseEnabled"),
                                        CFSTR("com.apple.universalaccess"),
                                        &exists);
  if (exists && val == 1) {
    AddClient(Client::SpeakItemUnderMouse);
  }

  val = CFPreferencesGetAppIntegerValue(
      CFSTR("typingEchoEnabled"), CFSTR("com.apple.universalaccess"), &exists);
  if (exists && val == 1) {
    AddClient(Client::SpeakTypingFeedback);
  }

  val = CFPreferencesGetAppIntegerValue(
      CFSTR("hoverTextEnabled"), CFSTR("com.apple.universalaccess"), &exists);
  if (exists && val == 1) {
    AddClient(Client::HoverText);
  }

  if (clients.isEmpty()) {
    AddClient(Client::Unknown);
  }
  return std::make_pair(clients, clientToLog.value());
}

// Expects a single client, returns a string representation of that client.
constexpr const char* GetStringForClient(Client aClient) {
  switch (aClient) {
    case Client::Unknown:
      return "Unknown";
    case Client::VoiceOver:
      return "VoiceOver";
    case Client::SwitchControl:
      return "SwitchControl";
    case Client::UAZoom:
      return "UAZoom";
    case Client::FullKeyboardAccess:
      return "FullKeyboardAccess";
    case Client::VoiceControl:
      return "VoiceControl";
    case Client::SpeakSelection:
      return "SpeakSelection";
    case Client::SpeakItemUnderMouse:
      return "SpeakItemUnderMouse";
    case Client::SpeakTypingFeedback:
      return "SpeakTypingFeedback";
    case Client::HoverText:
      return "HoverText";
    default:
      break;
  }
  MOZ_ASSERT_UNREACHABLE("Unknown Client enum value!");
  return "";
}

uint64_t GetCacheDomainsForKnownClients(uint64_t aCacheDomains) {
  auto [clients, _] = GetClients();
  // We expect VoiceOver will require all information we have.
  if (clients.contains(Client::VoiceOver)) {
    return CacheDomain::All;
  }
  if (clients.contains(Client::FullKeyboardAccess)) {
    aCacheDomains |= CacheDomain::Bounds;
  }
  if (clients.contains(Client::SwitchControl)) {
    // XXX: Find minimum set of domains required for SwitchControl.
    // SwitchControl can give up if we don't furnish it certain information.
    return CacheDomain::All;
  }
  if (clients.contains(Client::VoiceControl)) {
    // XXX: Find minimum set of domains required for VoiceControl.
    return CacheDomain::All;
  }
  if (clients.contains(Client::UAZoom)) {
    // XXX bug 2021474: Find minimum set of domains required for UAZoom.
    return CacheDomain::All;
  }
  return aCacheDomains;
}

void GetHumanReadableInstantiatorStr(nsAString& aResult) {
  auto [clients, _] = GetClients();
  nsAutoString result;
  for (Client client : clients) {
    if (!result.IsEmpty()) {
      result.AppendLiteral(",");
    }
    result.AppendASCII(GetStringForClient(client));
  }
  aResult = result;
}

static void EnableA11y() {
  sA11yShouldBeEnabled = true;
  // If accessibility should be enabled, log the appropriate client
  auto [_, clientToLog] = GetClients();
  const char* client = GetStringForClient(clientToLog);

#if defined(MOZ_TELEMETRY_REPORTING)
  mozilla::glean::a11y::instantiators.Set(nsDependentCString(client));
#endif  // defined(MOZ_TELEMETRY_REPORTING)
  CrashReporter::RecordAnnotationCString(
      CrashReporter::Annotation::AccessibilityClient, client);
}

}  // namespace a11y
}  // namespace mozilla

@interface GeckoNSApplication (a11y)
- (void)accessibilitySetValue:(id)value forAttribute:(NSString*)attribute;
@end

@implementation GeckoNSApplication (a11y)

- (NSAccessibilityRole)accessibilityRole {
  // Clients such as Voice Control need Gecko accessibility on a role query,
  // without enabling AppKit's Enhanced UI window animations.
  if (!mozilla::a11y::sA11yShouldBeEnabled) {
    mozilla::a11y::EnableA11y();
  }
  return [super accessibilityRole];
}

- (void)accessibilitySetValue:(id)value forAttribute:(NSString*)attribute {
  if ([attribute isEqualToString:@"AXEnhancedUserInterface"]) {
    if ([value intValue] == 1) {
      mozilla::a11y::EnableA11y();
    } else {
      mozilla::a11y::sA11yShouldBeEnabled = false;
    }
  }

  return [super accessibilitySetValue:value forAttribute:attribute];
}

@end
