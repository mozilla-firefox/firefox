/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CocoaFileUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIImageLoadingContent.h"
#include "mozilla/dom/Document.h"
#include "nsComponentManagerUtils.h"
#include "nsIContent.h"
#include "nsICookieJarSettings.h"
#include "nsIObserverService.h"
#include "nsIWebBrowserPersist.h"
#include "nsMacShellService.h"
#include "nsIProperties.h"
#include "nsServiceManagerUtils.h"
#include "nsShellService.h"
#include "nsString.h"
#include "nsURLHelper.h"
#include "nsIDocShell.h"
#include "nsILoadContext.h"
#include "nsIPrefService.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "DesktopBackgroundImage.h"

#include <Carbon/Carbon.h>
#include <CoreFoundation/CFArray.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ApplicationServices/ApplicationServices.h>

#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <ServiceManagement/ServiceManagement.h>

#include "mozilla/ErrorResult.h"
#include "mozilla/dom/Promise.h"
#include "nsProxyRelease.h"
#include "nsThreadUtils.h"
#include "xpcpublic.h"

using mozilla::dom::Element;
using mozilla::widget::SetDesktopImage;

#define NETWORK_PREFPANE "/System/Library/PreferencePanes/Network.prefPane"_ns
#define DESKTOP_PREFPANE \
  nsLiteralCString(      \
      "/System/Library/PreferencePanes/DesktopScreenEffectsPref.prefPane")

#define SAFARI_BUNDLE_IDENTIFIER "com.apple.Safari"

NS_IMPL_ISUPPORTS(nsMacShellService, nsIMacShellService, nsIShellService,
                  nsIToolkitShellService, nsIWebProgressListener)

NS_IMETHODIMP
nsMacShellService::IsDefaultBrowser(bool aForAllTypes,
                                    bool* aIsDefaultBrowser) {
  *aIsDefaultBrowser = false;

  CFStringRef firefoxID = ::CFBundleGetIdentifier(::CFBundleGetMainBundle());
  if (!firefoxID) {
    // CFBundleGetIdentifier is expected to return nullptr only if the specified
    // bundle doesn't have a bundle identifier in its plist. In this case, that
    // means a failure, since our bundle does have an identifier.
    return NS_ERROR_FAILURE;
  }

  // Get the default http handler's bundle ID (or nullptr if it has not been
  // explicitly set)
  CFStringRef defaultBrowserID =
      ::LSCopyDefaultHandlerForURLScheme(CFSTR("http"));
  if (defaultBrowserID) {
    *aIsDefaultBrowser =
        ::CFStringCompare(firefoxID, defaultBrowserID, 0) == kCFCompareEqualTo;
    ::CFRelease(defaultBrowserID);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::SetDefaultBrowser(bool aForAllUsers) {
  // Note: We don't support aForAllUsers on Mac OS X.

  if (@available(macOS 27.0, *)) {
    // macOS 27 raises a separate prompt for each LSSetDefault* call below
    // resulting in multiple default browser change confirmation prompts.
    // The NSWorkspace API, which avoids this problem, is available on macOS
    // 12+, but limit its use here to macOS 27+ for now to be conservative.
    // We only set the default application for the http URL scheme.
    // Empirically, that has been shown to set the default browser and the
    // default handler for https/http schemes and html files.
    NSURL* appURL = [[NSBundle mainBundle] bundleURL];
    if (!appURL) {
      return NS_ERROR_FAILURE;
    }
    [[NSWorkspace sharedWorkspace] setDefaultApplicationAtURL:appURL
                                         toOpenURLsWithScheme:@"http"
                                            completionHandler:nil];
  } else {
    CFStringRef firefoxID = ::CFBundleGetIdentifier(::CFBundleGetMainBundle());
    if (!firefoxID) {
      return NS_ERROR_FAILURE;
    }

    if (::LSSetDefaultHandlerForURLScheme(CFSTR("http"), firefoxID) != noErr) {
      return NS_ERROR_FAILURE;
    }
    if (::LSSetDefaultHandlerForURLScheme(CFSTR("https"), firefoxID) != noErr) {
      return NS_ERROR_FAILURE;
    }

    if (::LSSetDefaultRoleHandlerForContentType(kUTTypeHTML, kLSRolesAll,
                                                firefoxID) != noErr) {
      return NS_ERROR_FAILURE;
    }
  }

  nsCOMPtr<nsIPrefBranch> prefs(do_GetService(NS_PREFSERVICE_CONTRACTID));
  if (prefs) {
    (void)prefs->SetBoolPref(PREF_CHECKDEFAULTBROWSER, true);
    // Reset the number of times the dialog should be shown
    // before it is silenced.
    (void)prefs->SetIntPref(PREF_DEFAULTBROWSERCHECKCOUNT, 0);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::SetDesktopBackground(Element* aElement, int32_t aPosition,
                                        const nsACString& aImageName) {
  // Note: We don't support aPosition on OS X.

  // Get the image URI:
  nsresult rv;
  nsCOMPtr<nsIImageLoadingContent> imageContent =
      do_QueryInterface(aElement, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIURI> imageURI;
  rv = imageContent->GetCurrentURI(getter_AddRefs(imageURI));
  NS_ENSURE_SUCCESS(rv, rv);

  nsIURI* docURI = aElement->OwnerDoc()->GetDocumentURI();
  if (!docURI) return NS_ERROR_FAILURE;

  nsCOMPtr<nsIProperties> fileLocator(
      do_GetService("@mozilla.org/file/directory_service;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the current user's "Pictures" folder (That's ~/Pictures):
  fileLocator->Get(NS_OSX_PICTURE_DOCUMENTS_DIR, NS_GET_IID(nsIFile),
                   getter_AddRefs(mBackgroundFile));
  if (!mBackgroundFile) return NS_ERROR_OUT_OF_MEMORY;

  nsAutoString fileNameUnicode;
  CopyUTF8toUTF16(aImageName, fileNameUnicode);

  // and add the imgage file name itself:
  mBackgroundFile->Append(fileNameUnicode);

  // Download the image; the desktop background will be set in OnStateChange()
  nsCOMPtr<nsIWebBrowserPersist> wbp(do_CreateInstance(
      "@mozilla.org/embedding/browser/nsWebBrowserPersist;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t flags = nsIWebBrowserPersist::PERSIST_FLAGS_NO_CONVERSION |
                   nsIWebBrowserPersist::PERSIST_FLAGS_REPLACE_EXISTING_FILES |
                   nsIWebBrowserPersist::PERSIST_FLAGS_FROM_CACHE;

  wbp->SetPersistFlags(flags);
  wbp->SetProgressListener(this);

  nsCOMPtr<nsILoadContext> loadContext;
  nsCOMPtr<nsISupports> container = aElement->OwnerDoc()->GetContainer();
  nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(container);
  if (docShell) {
    loadContext = do_QueryInterface(docShell);
  }

  auto referrerInfo =
      mozilla::MakeRefPtr<mozilla::dom::ReferrerInfo>(*aElement);

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings =
      aElement->OwnerDoc()->CookieJarSettings();
  return wbp->SaveURI(imageURI, aElement->NodePrincipal(), 0, referrerInfo,
                      cookieJarSettings, nullptr, nullptr, mBackgroundFile,
                      nsIContentPolicy::TYPE_IMAGE,
                      loadContext->UsePrivateBrowsing());
}

NS_IMETHODIMP
nsMacShellService::OnProgressChange(nsIWebProgress* aWebProgress,
                                    nsIRequest* aRequest,
                                    int32_t aCurSelfProgress,
                                    int32_t aMaxSelfProgress,
                                    int32_t aCurTotalProgress,
                                    int32_t aMaxTotalProgress) {
  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::OnLocationChange(nsIWebProgress* aWebProgress,
                                    nsIRequest* aRequest, nsIURI* aLocation,
                                    uint32_t aFlags) {
  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::OnStatusChange(nsIWebProgress* aWebProgress,
                                  nsIRequest* aRequest, nsresult aStatus,
                                  const char16_t* aMessage) {
  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::OnSecurityChange(nsIWebProgress* aWebProgress,
                                    nsIRequest* aRequest, uint32_t aState) {
  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::OnContentBlockingEvent(nsIWebProgress* aWebProgress,
                                          nsIRequest* aRequest,
                                          uint32_t aEvent) {
  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::OnStateChange(nsIWebProgress* aWebProgress,
                                 nsIRequest* aRequest, uint32_t aStateFlags,
                                 nsresult aStatus) {
  if (NS_SUCCEEDED(aStatus) && (aStateFlags & STATE_STOP) &&
      (aRequest == nullptr)) {
    nsCOMPtr<nsIObserverService> os(
        do_GetService("@mozilla.org/observer-service;1"));
    if (os)
      os->NotifyObservers(nullptr, "shell:desktop-background-changed", nullptr);

    bool exists = false;
    nsresult rv = mBackgroundFile->Exists(&exists);
    if (NS_FAILED(rv) || !exists) {
      return NS_OK;
    }

    SetDesktopImage(mBackgroundFile);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::ShowDesktopPreferences() {
  nsCOMPtr<nsIFile> lf;
  nsresult rv = NS_NewNativeLocalFile(DESKTOP_PREFPANE, getter_AddRefs(lf));
  NS_ENSURE_SUCCESS(rv, rv);
  bool exists;
  lf->Exists(&exists);
  if (!exists) return NS_ERROR_FILE_NOT_FOUND;
  return lf->Launch();
}

NS_IMETHODIMP
nsMacShellService::GetDesktopBackgroundColor(uint32_t* aColor) {
  // This method and |SetDesktopBackgroundColor| has no meaning on Mac OS X.
  // The mac desktop preferences UI uses pictures for the few solid colors it
  // supports.
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsMacShellService::SetDesktopBackgroundColor(uint32_t aColor) {
  // This method and |GetDesktopBackgroundColor| has no meaning on Mac OS X.
  // The mac desktop preferences UI uses pictures for the few solid colors it
  // supports.
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsMacShellService::ShowSecurityPreferences(const nsACString& aPaneID) {
  nsresult rv = NS_ERROR_NOT_AVAILABLE;

  CFStringRef paneID = ::CFStringCreateWithBytes(
      kCFAllocatorDefault, (const UInt8*)PromiseFlatCString(aPaneID).get(),
      aPaneID.Length(), kCFStringEncodingUTF8, false);

  if (paneID) {
    CFStringRef format =
        CFSTR("x-apple.systempreferences:com.apple.preference.security?%@");
    if (format) {
      CFStringRef urlStr = CFStringCreateWithFormat(kCFAllocatorDefault,
                                                    nullptr, format, paneID);
      if (urlStr) {
        CFURLRef url = ::CFURLCreateWithString(nullptr, urlStr, nullptr);
        rv = CocoaFileUtils::OpenURL(url);

        ::CFRelease(urlStr);
      }

      ::CFRelease(format);
    }

    ::CFRelease(paneID);
  }
  return rv;
}

nsString ConvertCFStringToNSString(CFStringRef aSrc) {
  nsString aDest;
  auto len = ::CFStringGetLength(aSrc);
  aDest.SetLength(len);
  ::CFStringGetCharacters(aSrc, ::CFRangeMake(0, len),
                          (UniChar*)aDest.BeginWriting());
  return aDest;
}

static NSString* ToNSString(const nsAString& aStr) {
  return [NSString
      stringWithCharacters:reinterpret_cast<const unichar*>(aStr.BeginReading())
                    length:aStr.Length()];
}

static bool IsFileExtension(const nsAString& aFileExtensionOrProtocol) {
  return !aFileExtensionOrProtocol.IsEmpty() &&
         aFileExtensionOrProtocol.First() == u'.';
}

static NSString* URLSchemeForProtocol(const nsAString& aProtocol) {
  if (!net_IsValidScheme(NS_ConvertUTF16toUTF8(aProtocol))) {
    return nil;
  }
  return ToNSString(aProtocol);
}

static NSURL* URLForProtocol(const nsAString& aProtocol) {
  NSString* scheme = URLSchemeForProtocol(aProtocol);
  if (!scheme) {
    return nil;
  }
  return [NSURL URLWithString:[scheme stringByAppendingString:@"://"]];
}

API_AVAILABLE(macos(11.0))
static UTType* UTTypeForFileExtension(const nsAString& aFileExtension) {
  NSString* extension = ToNSString(aFileExtension);
  if ([extension hasPrefix:@"."]) {
    extension = [extension substringFromIndex:1];
  }
  return extension.length ? [UTType typeWithFilenameExtension:extension] : nil;
}

// The application currently registered as the default handler for the given
// file extension (a leading ".", e.g. ".pdf") or URL scheme (e.g. "https"), or
// nil when there is none or the lookup API is unavailable (macOS older than
// 12).
static NSURL* DefaultApplicationForHandler(
    const nsAString& aFileExtensionOrProtocol) {
  if (@available(macOS 12.0, *)) {
    NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
    if (IsFileExtension(aFileExtensionOrProtocol)) {
      UTType* type = UTTypeForFileExtension(aFileExtensionOrProtocol);
      return type ? [workspace URLForApplicationToOpenContentType:type] : nil;
    }
    NSURL* schemeURL = URLForProtocol(aFileExtensionOrProtocol);
    if (schemeURL) {
      return [workspace URLForApplicationToOpenURL:schemeURL];
    }
  }
  return nil;
}

NS_IMETHODIMP
nsMacShellService::GetAvailableApplicationsForProtocol(
    const nsACString& protocol, nsTArray<nsTArray<nsString>>& aHandlerPaths) {
  class CFTypeRefAutoDeleter {
   public:
    explicit CFTypeRefAutoDeleter(CFTypeRef ref) : mRef(ref) {}
    ~CFTypeRefAutoDeleter() {
      if (mRef != nullptr) ::CFRelease(mRef);
    }

   private:
    CFTypeRef mRef;
  };

  aHandlerPaths.Clear();
  nsCString protocolSep = protocol + "://"_ns;
  CFStringRef cfProtocol = ::CFStringCreateWithBytes(
      kCFAllocatorDefault, (const UInt8*)protocolSep.BeginReading(),
      protocolSep.Length(), kCFStringEncodingUTF8, false);
  CFTypeRefAutoDeleter cfProtocolAuto((CFTypeRef)cfProtocol);
  if (cfProtocol == nullptr) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  CFURLRef protocolURL =
      ::CFURLCreateWithString(kCFAllocatorDefault, cfProtocol, nullptr);
  CFTypeRefAutoDeleter cfProtocolURLAuto((CFTypeRef)protocolURL);
  if (protocolURL == nullptr) {
    return NS_ERROR_MALFORMED_URI;
  }
  CFArrayRef appURLs = ::LSCopyApplicationURLsForURL(protocolURL, kLSRolesAll);
  CFTypeRefAutoDeleter cfAppURLsAuto((CFTypeRef)appURLs);
  if (appURLs == nullptr) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  for (CFIndex i = 0; i < ::CFArrayGetCount(appURLs); i++) {
    CFURLRef appURL = (CFURLRef)::CFArrayGetValueAtIndex(appURLs, i);
    CFBundleRef appBundle = ::CFBundleCreate(kCFAllocatorDefault, appURL);
    CFTypeRefAutoDeleter cfAppBundleAuto((CFTypeRef)appBundle);
    if (appBundle == nullptr) {
      continue;
    }
    CFDictionaryRef appInfo = ::CFBundleGetInfoDictionary(appBundle);
    if (appInfo == nullptr) {
      continue;
    }
    CFStringRef displayName =
        (CFStringRef)::CFDictionaryGetValue(appInfo, kCFBundleNameKey);
    if (displayName == nullptr) {
      continue;
    }
    CFStringRef appPath = ::CFURLGetString(appURL);
    nsTArray<nsString> handlerPath = {ConvertCFStringToNSString(displayName),
                                      ConvertCFStringToNSString(appPath)};
    aHandlerPaths.AppendElement(handlerPath.Clone());
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::GetCanSetAsDefaultHandler(bool* aResult) {
  // The NSWorkspace default-application API used below requires macOS 12.
  if (@available(macOS 12.0, *)) {
    *aResult = true;
  } else {
    *aResult = false;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::SetAsDefaultHandlerFor(
    const nsAString& aFileExtensionOrProtocol, JSContext* aCx,
    mozilla::dom::Promise** aPromise) {
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  mozilla::ErrorResult rv;
  RefPtr<mozilla::dom::Promise> promise =
      mozilla::dom::Promise::Create(xpc::CurrentNativeGlobal(aCx), rv);
  if (MOZ_UNLIKELY(rv.Failed())) {
    return rv.StealNSResult();
  }

  if (@available(macOS 12.0, *)) {
    NSURL* appURL = [[NSBundle mainBundle] bundleURL];
    if (appURL) {
      // NSWorkspace invokes the completion handler after any user-consent
      // prompt resolves, possibly off the main thread. nsMainThreadPtrHolder
      // guarantees the Promise is only touched and released on the main thread.
      // As with other macOS consent prompts resolved from an OS callback (e.g.
      // nsCocoaUtils::RequestCapturePermission), there is no timeout: if the OS
      // never invokes the handler the promise simply stays pending.
      auto promiseHolder =
          MakeRefPtr<nsMainThreadPtrHolder<mozilla::dom::Promise>>(
              "nsMacShellService::SetAsDefaultHandlerFor promise", promise);

      // A nil error means the change was applied (the user accepted any consent
      // prompt). This follows the NSWorkspace completion-handler convention
      // used elsewhere in the tree (e.g. nsMacWebAppUtils::TrashApp) and avoids
      // depending on the new default being immediately observable.
      void (^completionHandler)(NSError* _Nullable) =
          ^(NSError* _Nullable error) {
            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "nsMacShellService::SetAsDefaultHandlerFor completion",
                [succeeded = (error == nil), promiseHolder] {
                  promiseHolder->get()->MaybeResolve(succeeded);
                }));
          };

      NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
      if (IsFileExtension(aFileExtensionOrProtocol)) {
        UTType* type = UTTypeForFileExtension(aFileExtensionOrProtocol);
        if (type) {
          [workspace setDefaultApplicationAtURL:appURL
                              toOpenContentType:type
                              completionHandler:completionHandler];
        } else {
          promise->MaybeResolve(false);
        }
      } else {
        NSString* scheme = URLSchemeForProtocol(aFileExtensionOrProtocol);
        if (scheme) {
          [workspace setDefaultApplicationAtURL:appURL
                           toOpenURLsWithScheme:scheme
                              completionHandler:completionHandler];
        } else {
          promise->MaybeResolve(false);
        }
      }
    } else {
      // Couldn't resolve our own bundle URL; treat as failure.
      promise->MaybeResolve(false);
    }
  } else {
    // Unsupported on macOS older than 12.
    promise->MaybeResolve(false);
  }

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::IsDefaultHandlerFor(
    const nsAString& aFileExtensionOrProtocol, bool* aResult) {
  NSURL* selfURL = [[NSBundle mainBundle] bundleURL];
  NSURL* defaultApp = DefaultApplicationForHandler(aFileExtensionOrProtocol);
  *aResult = selfURL && defaultApp && [defaultApp isEqual:selfURL];
  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::IsDefaultHandlerAWebBrowserFor(
    const nsAString& aFileExtensionOrProtocol, bool* aResult) {
  *aResult = false;

  if (@available(macOS 12.0, *)) {
    // The application currently registered as the default handler.
    NSURL* handlerApp = DefaultApplicationForHandler(aFileExtensionOrProtocol);
    if (!handlerApp) {
      return NS_OK;
    }

    // The handler is a web browser if and only if it is among the applications
    // registered to open "https" URLs. This avoids hardcoding a list of known
    // browser identifiers.
    NSArray<NSURL*>* browsers = [[NSWorkspace sharedWorkspace]
        URLsForApplicationsToOpenURL:
            [NSURL URLWithString:@"https://example.com/"]];
    for (NSURL* browser in browsers) {
      if ([browser isEqual:handlerApp]) {
        *aResult = true;
        break;
      }
    }
  }

  return NS_OK;
}

// For OS versions >= 13.0 we use the newest available API (SMAppService)
// but for earlier OS versions we use the old LSSharedFileList API which was
// deprecated in 10.11 but was still working up to version 12 (indeed it
// still works in 26.0). The API that wasn't deprecated between 10.11 and 12
// is much more complex and requires a helper application so using this older
// API felt like an ok trade off for versions before 13.
static bool EnableLaunchOnLoginBefore13() {
  LSSharedFileListRef loginItemsListRef = LSSharedFileListCreate(
      nullptr, kLSSharedFileListSessionLoginItems, nullptr);
  if (!loginItemsListRef) {
    return false;
  }
  NSURL* bundleURL = [[NSBundle mainBundle] bundleURL];

  LSSharedFileListItemRef itemRef;
  itemRef = LSSharedFileListInsertItemURL(
      loginItemsListRef, kLSSharedFileListItemLast, nullptr, nullptr,
      (__bridge CFURLRef)bundleURL, nullptr, nullptr);

  CFRelease(loginItemsListRef);

  if (itemRef) {
    CFRelease(itemRef);
  } else {
    return false;
  }

  return true;
}

NS_IMETHODIMP
nsMacShellService::EnableLaunchOnLogin(bool* aResult) {
  if (@available(macOS 13.0, *)) {
    *aResult = [[SMAppService mainAppService] registerAndReturnError:nil];
  } else {
    *aResult = EnableLaunchOnLoginBefore13();
  }

  return NS_OK;
}

#define kLSSharesFileListEmptyOptions 0

/**
 *
 * Find the bundle in the Login Items shared file list and
 * maybe remove it. Returns true if it was found (and removed).
 *
 * @param {bool} alsoRemove - Whether the item should be removed
 * when it is found.
 *
 * @returns {bool}
 *          Whether item was found in the Login Items.
 */
static bool FindAndMaybeRemoveBundleBefore13(bool aAlsoRemove) {
  LSSharedFileListRef loginItemsListRef = LSSharedFileListCreate(
      nullptr, kLSSharedFileListSessionLoginItems, nullptr);
  if (!loginItemsListRef) {
    return false;
  }

  CFArrayRef snapshot =
      LSSharedFileListCopySnapshot(loginItemsListRef, nullptr);
  if (!snapshot) {
    CFRelease(loginItemsListRef);
    return false;
  }

  NSArray* loginItems = (NSArray*)snapshot;
  NSURL* bundleURL = [[NSBundle mainBundle] bundleURL];

  bool found = false;

  for (id item in loginItems) {
    // itemRef doesn't need released as it is owned by snapshot
    LSSharedFileListItemRef itemRef = (LSSharedFileListItemRef)item;
    CFURLRef itemURLRef;

    itemURLRef = LSSharedFileListItemCopyResolvedURL(
        itemRef, kLSSharesFileListEmptyOptions, nullptr);
    if (!itemURLRef) {
      continue;
    }

    NSURL* itemURL = (NSURL*)itemURLRef;

    if ([itemURL isEqual:bundleURL]) {
      if (aAlsoRemove) {
        LSSharedFileListItemRemove(loginItemsListRef, itemRef);
      }

      found = true;
    }

    CFRelease(itemURLRef);

    if (found) {
      break;
    }
  }

  CFRelease(snapshot);
  CFRelease(loginItemsListRef);

  return found;
}

NS_IMETHODIMP
nsMacShellService::DisableLaunchOnLogin(bool* aResult) {
  if (@available(macOS 13.0, *)) {
    *aResult = [[SMAppService mainAppService] unregisterAndReturnError:nil];
  } else {
    *aResult = FindAndMaybeRemoveBundleBefore13(true);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsMacShellService::GetLaunchOnLoginEnabled(bool* aResult) {
  if (@available(macOS 13.0, *)) {
    SMAppServiceStatus status = [[SMAppService mainAppService] status];
    *aResult = (status == SMAppServiceStatusEnabled);
  } else {
    *aResult = FindAndMaybeRemoveBundleBefore13(false);
  }
  return NS_OK;
}
