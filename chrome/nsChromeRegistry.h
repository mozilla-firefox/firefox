/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsChromeRegistry_h
#define nsChromeRegistry_h

#include "nsIToolkitChromeRegistry.h"
#include "nsIObserver.h"
#include "nsWeakReference.h"

#include "nsString.h"
#include "nsURIHashKey.h"
#include "nsInterfaceHashtable.h"
#include "nsXULAppAPI.h"

#include "mozilla/FileLocation.h"
#include "mozilla/intl/LocaleService.h"

class nsPIDOMWindowOuter;
class nsIPrefBranch;
class nsIURL;

// The chrome registry is actually split between nsChromeRegistryChrome and
// nsChromeRegistryContent. The work/data that is common to both resides in
// the shared nsChromeRegistry implementation, with operations that only make
// sense for one side erroring out in the other.

// for component registration
// {47049e42-1d87-482a-984d-56ae185e367a}
#define NS_CHROMEREGISTRY_CID \
  {0x47049e42, 0x1d87, 0x482a, {0x98, 0x4d, 0x56, 0xae, 0x18, 0x5e, 0x36, 0x7a}}

class nsChromeRegistry : public nsIToolkitChromeRegistry,
                         public nsIObserver,
                         public nsSupportsWeakReference {
 public:
  NS_DECL_ISUPPORTS

  // nsIXULChromeRegistry methods:
  NS_IMETHOD AllowScriptsForPackage(nsIURI* url, bool* _retval) override;
  NS_IMETHOD AllowContentToAccess(nsIURI* url, bool* _retval) override;
  NS_IMETHOD CanLoadURLRemotely(nsIURI* url, bool* _retval) override;
  NS_IMETHOD MustLoadURLRemotely(nsIURI* url, bool* _retval) override;

  NS_IMETHOD ConvertChromeURL(nsIURI* aChromeURI, nsIURI** aResult) override;

  // nsChromeRegistry methods:
  nsChromeRegistry() : mInitialized(false) {}

  virtual nsresult Init();

  static already_AddRefed<nsIChromeRegistry> GetService();

  static nsChromeRegistry* gChromeRegistry;

  // This method can change its parameter, so due to thread safety issues
  // it should only be called for nsCOMPtr<nsIURI> that is on the stack,
  // unless you know what you are doing.
  static nsresult Canonify(nsCOMPtr<nsIURI>& aChromeURL);

 protected:
  virtual ~nsChromeRegistry();

  void FlushSkinCaches();
  void FlushAllCaches();

  static void LogMessage(const char* aMsg, ...) MOZ_FORMAT_PRINTF(1, 2);
  static void LogMessageWithContext(nsIURI* aURL, uint32_t aLineNumber,
                                    uint32_t flags, const char* aMsg, ...)
      MOZ_FORMAT_PRINTF(4, 5);

  virtual nsIURI* GetBaseURIFromPackage(const nsCString& aPackage,
                                        const nsCString& aProvider,
                                        const nsCString& aPath) = 0;
  virtual nsresult GetFlagsFromPackage(const nsCString& aPackage,
                                       uint32_t* aFlags) = 0;

  static nsresult RefreshWindow(nsPIDOMWindowOuter* aWindow);
  static nsresult GetProviderAndPath(nsIURI* aChromeURL, nsACString& aProvider,
                                     nsACString& aPath);

 public:
  static already_AddRefed<nsChromeRegistry> GetSingleton();

  struct ManifestProcessingContext {
    ManifestProcessingContext(NSLocationType aType,
                              mozilla::FileLocation& aFile)
        : mType(aType), mFile(aFile) {}

    ~ManifestProcessingContext() = default;

    nsIURI* GetManifestURI();
    already_AddRefed<nsIURI> ResolveURI(const char* uri);

    NSLocationType mType;
    mozilla::FileLocation mFile;
    nsCOMPtr<nsIURI> mManifestURI;
  };

  virtual void ManifestContent(ManifestProcessingContext& cx, int lineno,
                               char* const* argv, int flags) = 0;
  virtual void ManifestLocale(ManifestProcessingContext& cx, int lineno,
                              char* const* argv, int flags) = 0;
  virtual void ManifestSkin(ManifestProcessingContext& cx, int lineno,
                            char* const* argv, int flags) = 0;
  virtual void ManifestOverride(ManifestProcessingContext& cx, int lineno,
                                char* const* argv, int flags) = 0;
  virtual void ManifestResource(ManifestProcessingContext& cx, int lineno,
                                char* const* argv, int flags) = 0;

  // Available flags
  enum {
    // This package should use the new XPCNativeWrappers to separate
    // content from chrome. This flag is currently unused (because we call
    // into xpconnect at registration time).
    XPCNATIVEWRAPPERS = 1 << 1,

    // Content script may access files in this package
    CONTENT_ACCESSIBLE = 1 << 2,

    // Package may be loaded remotely
    REMOTE_ALLOWED = 1 << 3,

    // Package must be loaded remotely
    REMOTE_REQUIRED = 1 << 4,
  };

  bool mInitialized;

  // "Override" table (chrome URI string -> real URI)
  nsInterfaceHashtable<nsURIHashKey, nsIURI> mOverrideTable;
};

#endif  // nsChromeRegistry_h
