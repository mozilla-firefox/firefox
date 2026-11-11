/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/*
 * Use this file to add tests to policies that are
 * simple pref flips.
 *
 * It's best to make a test to actually test the feature
 * instead of the pref flip, but if that feature is well
 * covered by tests, including that its pref actually works,
 * it's OK to have the policy test here just to ensure
 * that the right pref values are set.
 */

const POLICIES_TESTS = [
  /*
   * Example:
   * {
   *   // Policies to be set at once through the engine
   *   policies: { "DisableFoo": true, "ConfigureBar": 42 },
   *
   *   // Locked prefs to check
   *   lockedPrefs: { "feature.foo": false },
   *
   *   // Unlocked prefs to check
   *   unlockedPrefs: { "bar.baz": 42 }
   * },
   */

  // POLICY: DisableSecurityBypass
  {
    policies: {
      DisableSecurityBypass: {
        InvalidCertificate: true,
        SafeBrowsing: true,
      },
    },
    lockedPrefs: {
      "security.certerror.hideAddException": true,
      "browser.safebrowsing.allowOverride": false,
    },
  },

  // POLICY: DisableFormHistory
  {
    policies: { DisableFormHistory: true },
    lockedPrefs: { "browser.formfill.enable": false },
  },

  // POLICY: EnableTrackingProtection
  {
    policies: {
      EnableTrackingProtection: {
        Value: true,
      },
    },
    unlockedPrefs: {
      "privacy.trackingprotection.enabled": true,
      "privacy.trackingprotection.pbmode.enabled": true,
    },
  },
  {
    policies: {
      EnableTrackingProtection: {
        Value: false,
        Locked: true,
      },
    },
    lockedPrefs: {
      "privacy.trackingprotection.enabled": false,
      "privacy.trackingprotection.pbmode.enabled": false,
    },
  },

  {
    policies: {
      EnableTrackingProtection: {
        Cryptomining: true,
        Fingerprinting: true,
        EmailTracking: true,
        SuspectedFingerprinting: true,
        BaselineExceptions: true,
        ConvenienceExceptions: true,
        Locked: true,
      },
    },
    lockedPrefs: {
      "privacy.trackingprotection.cryptomining.enabled": true,
      "privacy.trackingprotection.fingerprinting.enabled": true,
      "privacy.trackingprotection.emailtracking.enabled": true,
      "privacy.trackingprotection.emailtracking.pbmode.enabled": true,
      "privacy.fingerprintingProtection": true,
      "privacy.fingerprintingProtection.pbmode": true,
      "privacy.trackingprotection.allow_list.baseline.enabled": true,
      "privacy.trackingprotection.allow_list.convenience.enabled": true,
    },
  },

  // POLICY: GoToIntranetSiteForSingleWordEntryInAddressBar
  {
    policies: {
      GoToIntranetSiteForSingleWordEntryInAddressBar: true,
    },
    lockedPrefs: {
      "browser.fixup.dns_first_for_single_words": true,
    },
  },

  // POLICY: OverrideFirstRunPage
  {
    policies: { OverrideFirstRunPage: "https://www.example.com/" },
    lockedPrefs: { "startup.homepage_welcome_url": "https://www.example.com/" },
  },

  // POLICY: Authentication
  {
    policies: {
      Authentication: {
        SPNEGO: ["a.com", "b.com"],
        Delegated: ["a.com", "b.com"],
        NTLM: ["a.com", "b.com"],
        AllowNonFQDN: {
          SPNEGO: true,
          NTLM: true,
        },
        AllowProxies: {
          SPNEGO: false,
          NTLM: false,
        },
        PrivateBrowsing: true,
      },
    },
    lockedPrefs: {
      "network.negotiate-auth.trusted-uris": "a.com, b.com",
      "network.negotiate-auth.delegation-uris": "a.com, b.com",
      "network.automatic-ntlm-auth.trusted-uris": "a.com, b.com",
      "network.automatic-ntlm-auth.allow-non-fqdn": true,
      "network.negotiate-auth.allow-non-fqdn": true,
      "network.automatic-ntlm-auth.allow-proxies": false,
      "network.negotiate-auth.allow-proxies": false,
      "network.auth.private-browsing-sso": true,
    },
  },

  // POLICY: Authentication (unlocked)
  {
    policies: {
      Authentication: {
        SPNEGO: ["a.com", "b.com"],
        Delegated: ["a.com", "b.com"],
        NTLM: ["a.com", "b.com"],
        AllowNonFQDN: {
          SPNEGO: true,
          NTLM: true,
        },
        AllowProxies: {
          SPNEGO: false,
          NTLM: false,
        },
        PrivateBrowsing: true,
        Locked: false,
      },
    },
    unlockedPrefs: {
      "network.negotiate-auth.trusted-uris": "a.com, b.com",
      "network.negotiate-auth.delegation-uris": "a.com, b.com",
      "network.automatic-ntlm-auth.trusted-uris": "a.com, b.com",
      "network.automatic-ntlm-auth.allow-non-fqdn": true,
      "network.negotiate-auth.allow-non-fqdn": true,
      "network.automatic-ntlm-auth.allow-proxies": false,
      "network.negotiate-auth.allow-proxies": false,
      "network.auth.private-browsing-sso": true,
    },
  },

  // POLICY: Certificates (false)
  {
    policies: {
      Certificates: {
        ImportEnterpriseRoots: false,
      },
    },
    lockedPrefs: {
      "security.enterprise_roots.enabled": false,
    },
  },

  // POLICY: InstallAddons.Default (block addon installs)
  {
    policies: {
      InstallAddonsPermission: {
        Default: false,
      },
    },
    lockedPrefs: {
      "xpinstall.enabled": false,
      "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons": false,
      "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features": false,
      "extensions.getAddons.showPane": false,
      "extensions.htmlaboutaddons.recommendations.enabled": false,
    },
  },

  // POLICY: SanitizeOnShutdown
  {
    policies: {
      SanitizeOnShutdown: true,
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": true,
      "privacy.clearOnShutdown.cookies": true,
      "privacy.clearOnShutdown.downloads": true,
      "privacy.clearOnShutdown.formdata": true,
      "privacy.clearOnShutdown.history": true,
      "privacy.clearOnShutdown.sessions": true,
      "privacy.clearOnShutdown.siteSettings": true,
      "privacy.clearOnShutdown.offlineApps": true,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": true,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": true,
      "privacy.clearOnShutdown_v2.cache": true,
      "privacy.clearOnShutdown_v2.siteSettings": true,
      "privacy.clearOnShutdown_v2.formdata": true,
    },
  },

  {
    policies: {
      SanitizeOnShutdown: false,
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": false,
      "privacy.clearOnShutdown.cache": false,
      "privacy.clearOnShutdown.cookies": false,
      "privacy.clearOnShutdown.downloads": false,
      "privacy.clearOnShutdown.formdata": false,
      "privacy.clearOnShutdown.history": false,
      "privacy.clearOnShutdown.sessions": false,
      "privacy.clearOnShutdown.siteSettings": false,
      "privacy.clearOnShutdown.offlineApps": false,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": false,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": false,
      "privacy.clearOnShutdown_v2.cache": false,
      "privacy.clearOnShutdown_v2.siteSettings": false,
      "privacy.clearOnShutdown_v2.formdata": false,
    },
  },

  {
    policies: {
      SanitizeOnShutdown: {
        Cache: true,
      },
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": true,
      "privacy.clearOnShutdown.cookies": false,
      "privacy.clearOnShutdown.downloads": false,
      "privacy.clearOnShutdown.formdata": false,
      "privacy.clearOnShutdown.history": false,
      "privacy.clearOnShutdown.sessions": false,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": false,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": false,
      "privacy.clearOnShutdown_v2.cache": true,
      "privacy.clearOnShutdown_v2.formdata": false,
    },
  },

  {
    policies: {
      SanitizeOnShutdown: {
        Cookies: true,
      },
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": false,
      "privacy.clearOnShutdown.cookies": true,
      "privacy.clearOnShutdown.downloads": false,
      "privacy.clearOnShutdown.formdata": false,
      "privacy.clearOnShutdown.history": false,
      "privacy.clearOnShutdown.sessions": false,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": false,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": true,
      "privacy.clearOnShutdown_v2.cache": false,
      "privacy.clearOnShutdown_v2.formdata": false,
    },
  },

  {
    policies: {
      SanitizeOnShutdown: {
        Downloads: true,
      },
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": false,
      "privacy.clearOnShutdown.cookies": false,
      "privacy.clearOnShutdown.downloads": true,
      "privacy.clearOnShutdown.formdata": false,
      "privacy.clearOnShutdown.history": false,
      "privacy.clearOnShutdown.sessions": false,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": false,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": false,
      "privacy.clearOnShutdown_v2.cache": false,
      "privacy.clearOnShutdown_v2.formdata": false,
    },
  },

  {
    policies: {
      SanitizeOnShutdown: {
        FormData: true,
      },
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": false,
      "privacy.clearOnShutdown.cookies": false,
      "privacy.clearOnShutdown.downloads": false,
      "privacy.clearOnShutdown.formdata": true,
      "privacy.clearOnShutdown.history": false,
      "privacy.clearOnShutdown.sessions": false,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": false,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": false,
      "privacy.clearOnShutdown_v2.cache": false,
      "privacy.clearOnShutdown_v2.formdata": true,
    },
  },

  {
    policies: {
      SanitizeOnShutdown: {
        History: true,
      },
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": false,
      "privacy.clearOnShutdown.cookies": false,
      "privacy.clearOnShutdown.downloads": false,
      "privacy.clearOnShutdown.formdata": false,
      "privacy.clearOnShutdown.history": true,
      "privacy.clearOnShutdown.sessions": false,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": true,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": false,
      "privacy.clearOnShutdown_v2.cache": false,
      "privacy.clearOnShutdown_v2.formdata": false,
    },
  },

  {
    policies: {
      SanitizeOnShutdown: {
        Sessions: true,
      },
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": false,
      "privacy.clearOnShutdown.cookies": false,
      "privacy.clearOnShutdown.downloads": false,
      "privacy.clearOnShutdown.formdata": false,
      "privacy.clearOnShutdown.history": false,
      "privacy.clearOnShutdown.sessions": true,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": false,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": false,
      "privacy.clearOnShutdown_v2.cache": false,
      "privacy.clearOnShutdown_v2.formdata": false,
    },
  },

  {
    policies: {
      SanitizeOnShutdown: {
        SiteSettings: true,
      },
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": false,
      "privacy.clearOnShutdown.cookies": false,
      "privacy.clearOnShutdown.downloads": false,
      "privacy.clearOnShutdown.formdata": false,
      "privacy.clearOnShutdown.history": false,
      "privacy.clearOnShutdown.sessions": false,
      "privacy.clearOnShutdown.siteSettings": true,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": false,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": false,
      "privacy.clearOnShutdown_v2.cache": false,
      "privacy.clearOnShutdown_v2.siteSettings": true,
      "privacy.clearOnShutdown_v2.formdata": false,
    },
  },

  {
    policies: {
      SanitizeOnShutdown: {
        OfflineApps: true,
      },
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": false,
      "privacy.clearOnShutdown.cookies": false,
      "privacy.clearOnShutdown.downloads": false,
      "privacy.clearOnShutdown.formdata": false,
      "privacy.clearOnShutdown.history": false,
      "privacy.clearOnShutdown.sessions": false,
      "privacy.clearOnShutdown.offlineApps": true,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": false,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": false,
      "privacy.clearOnShutdown_v2.cache": false,
    },
  },

  // POLICY: SanitizeOnShutdown using Locked
  {
    policies: {
      SanitizeOnShutdown: {
        Cache: true,
        Locked: true,
      },
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": true,
      "privacy.clearOnShutdown_v2.cache": true,
    },
    unlockedPrefs: {
      "privacy.clearOnShutdown.cookies": false,
      "privacy.clearOnShutdown.downloads": false,
      "privacy.clearOnShutdown.formdata": false,
      "privacy.clearOnShutdown.history": false,
      "privacy.clearOnShutdown.sessions": false,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": false,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": false,
      "privacy.clearOnShutdown_v2.formdata": false,
    },
  },

  {
    policies: {
      SanitizeOnShutdown: {
        Cache: true,
        Cookies: false,
        Locked: true,
      },
    },
    lockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": true,
      "privacy.clearOnShutdown.cookies": false,
      "privacy.clearOnShutdown_v2.cache": true,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": false,
    },
    unlockedPrefs: {
      "privacy.clearOnShutdown.downloads": false,
      "privacy.clearOnShutdown.formdata": false,
      "privacy.clearOnShutdown.history": false,
      "privacy.clearOnShutdown.sessions": false,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": false,
      "privacy.clearOnShutdown_v2.formdata": false,
    },
  },

  {
    policies: {
      SanitizeOnShutdown: {
        Cache: true,
        Locked: false,
      },
    },
    unlockedPrefs: {
      "privacy.sanitize.sanitizeOnShutdown": true,
      "privacy.clearOnShutdown.cache": true,
      "privacy.clearOnShutdown.cookies": false,
      "privacy.clearOnShutdown.downloads": false,
      "privacy.clearOnShutdown.formdata": false,
      "privacy.clearOnShutdown.history": false,
      "privacy.clearOnShutdown.sessions": false,
      "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads": false,
      "privacy.clearOnShutdown_v2.cookiesAndStorage": false,
      "privacy.clearOnShutdown_v2.cache": true,
      "privacy.clearOnShutdown_v2.formdata": false,
    },
  },

  // POLICY: DNSOverHTTPS Unlocked
  {
    policies: {
      DNSOverHTTPS: {
        Enabled: false,
        ProviderURL: "https://example.com/provider",
        ExcludedDomains: ["example.com", "example.org"],
      },
    },
    unlockedPrefs: {
      "network.trr.mode": 5,
      "network.trr.uri": "https://example.com/provider",
      "network.trr.excluded-domains": "example.com,example.org",
    },
  },

  // POLICY: DNSOverHTTPS Fallback off
  {
    policies: {
      DNSOverHTTPS: {
        Enabled: true,
        Fallback: false,
      },
    },
    unlockedPrefs: {
      "network.trr.mode": 3,
    },
  },

  // POLICY: DNSOverHTTPS Locked
  {
    policies: {
      DNSOverHTTPS: {
        Enabled: true,
        ProviderURL: "https://example.com/provider",
        ExcludedDomains: ["example.com", "example.org"],
        Locked: true,
      },
    },
    lockedPrefs: {
      "network.trr.mode": 2,
      "network.trr.uri": "https://example.com/provider",
      "network.trr.excluded-domains": "example.com,example.org",
    },
  },

  // POLICY: SSLVersionMin/SSLVersionMax (1)
  {
    policies: {
      SSLVersionMin: "tls1",
      SSLVersionMax: "tls1.1",
    },
    lockedPrefs: {
      "security.tls.version.min": 1,
      "security.tls.version.max": 2,
    },
  },

  // POLICY: SSLVersionMin/SSLVersionMax (2)
  {
    policies: {
      SSLVersionMin: "tls1.2",
      SSLVersionMax: "tls1.3",
    },
    lockedPrefs: {
      "security.tls.version.min": 3,
      "security.tls.version.max": 4,
    },
  },

  // POLICY: CaptivePortal
  {
    policies: {
      CaptivePortal: false,
    },
    lockedPrefs: {
      "network.captive-portal-service.enabled": false,
    },
  },

  // POLICY: NetworkPrediction
  {
    policies: {
      NetworkPrediction: false,
    },
    lockedPrefs: {
      "network.dns.disablePrefetch": true,
      "network.dns.disablePrefetchFromHTTPS": true,
    },
  },

  // POLICY: ExtensionUpdate
  {
    policies: {
      ExtensionUpdate: false,
    },
    lockedPrefs: {
      "extensions.update.enabled": false,
    },
  },

  // POLICY: DisableShield
  {
    policies: {
      DisableFirefoxStudies: true,
    },
    lockedPrefs: {
      "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons": false,
      "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features": false,
    },
  },

  // POLICY: NewTabPage
  {
    policies: {
      NewTabPage: false,
    },
    lockedPrefs: {
      "browser.newtabpage.enabled": false,
    },
  },

  // POLICY: SearchSuggestEnabled
  {
    policies: {
      SearchSuggestEnabled: false,
    },
    lockedPrefs: {
      "browser.urlbar.suggest.searches": false,
      "browser.search.suggest.enabled": false,
    },
  },

  // POLICY: FirefoxHome
  {
    policies: {
      FirefoxHome: {
        Stories: false,
        SponsoredStories: false,
        Locked: true,
      },
    },
    lockedPrefs: {
      "browser.newtabpage.activity-stream.feeds.section.topstories": false,
      "browser.newtabpage.activity-stream.showSponsored": false,
    },
  },

  // POLICY: FirefoxHome (Weather sets both the legacy and Nova prefs)
  {
    policies: {
      FirefoxHome: {
        Weather: false,
        Locked: true,
      },
    },
    lockedPrefs: {
      "browser.newtabpage.activity-stream.showWeather": false,
      "browser.newtabpage.activity-stream.widgets.weather.enabled": false,
    },
  },

  // POLICY: FirefoxHome (locking both sponsored settings locks the parent
  // "Support Firefox" toggle to their combined value)
  {
    policies: {
      FirefoxHome: {
        SponsoredTopSites: false,
        SponsoredStories: false,
        Locked: true,
      },
    },
    lockedPrefs: {
      "browser.newtabpage.activity-stream.showSponsoredCheckboxes": false,
    },
  },

  // POLICY: FirefoxHome->Widgets (Enabled honors Locked)
  // Must run before the Locked entry below, which locks the same prefs.
  {
    policies: {
      FirefoxHome: {
        Widgets: {
          Enabled: false,
        },
      },
    },
    unlockedPrefs: {
      "browser.newtabpage.activity-stream.widgets.enabled": false,
    },
  },

  // POLICY: FirefoxHome->Widgets
  {
    policies: {
      FirefoxHome: {
        Widgets: {
          Enabled: true,
          Blocked: ["crossword"],
        },
        Locked: true,
      },
    },
    lockedPrefs: {
      "browser.newtabpage.activity-stream.widgets.enabled": true,
      "browser.newtabpage.activity-stream.widgets.crossword.enabled": false,
    },
  },

  // POLICY: FirefoxHome->Widgets (Blocked locks even without Locked)
  {
    policies: {
      FirefoxHome: {
        Widgets: {
          Blocked: ["stocks"],
        },
      },
    },
    lockedPrefs: {
      "browser.newtabpage.activity-stream.widgets.stocks.enabled": false,
    },
  },

  // POLICY: OfferToSaveLoginsDefault
  {
    policies: {
      OfferToSaveLoginsDefault: false,
    },
    unlockedPrefs: {
      "signon.rememberSignons": false,
    },
  },

  // POLICY: RememberPasswords
  {
    policies: { OfferToSaveLogins: false },
    lockedPrefs: { "signon.rememberSignons": false },
  },
  {
    policies: { OfferToSaveLogins: true },
    lockedPrefs: { "signon.rememberSignons": true },
  },

  // POLICY: UserMessaging
  {
    policies: {
      UserMessaging: {
        SkipOnboarding: true,
        Locked: true,
      },
    },
    lockedPrefs: {
      "browser.aboutwelcome.enabled": false,
    },
  },

  // POLICY: UserMessaging->SkipOnboarding false (bug 1697566)
  {
    policies: {
      UserMessaging: {
        SkipOnboarding: false,
        Locked: false,
      },
    },
    unlockedPrefs: {
      "browser.aboutwelcome.enabled": true,
    },
  },

  {
    policies: {
      UserMessaging: {
        ExtensionRecommendations: false,
        Locked: false,
      },
    },
    unlockedPrefs: {
      "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons": false,
    },
  },

  {
    policies: {
      UserMessaging: {
        FeatureRecommendations: false,
        Locked: false,
      },
    },
    unlockedPrefs: {
      "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features": false,
    },
  },

  // POLICY: Permissions->Autoplay
  {
    policies: {
      Permissions: {
        Autoplay: {
          Default: "block-audio-video",
        },
      },
    },
    unlockedPrefs: {
      "media.autoplay.default": 5,
    },
  },

  {
    policies: {
      Permissions: {
        Autoplay: {
          Default: "allow-audio-video",
          Locked: true,
        },
      },
    },
    lockedPrefs: {
      "media.autoplay.default": 0,
    },
  },

  {
    policies: {
      Permissions: {
        Autoplay: {
          Default: "block-audio",
          Locked: false,
        },
      },
    },
    unlockedPrefs: {
      "media.autoplay.default": 1,
    },
  },

  // POLICY: LegacySameSiteCookieBehaviorEnabled

  {
    policies: {
      LegacySameSiteCookieBehaviorEnabled: true,
    },
    unlockedPrefs: {
      "network.cookie.sameSite.laxByDefault": false,
    },
  },

  // POLICY: LegacySameSiteCookieBehaviorEnabledForDomainList

  {
    policies: {
      LegacySameSiteCookieBehaviorEnabledForDomainList: [
        "example.com",
        "example.org",
      ],
    },
    unlockedPrefs: {
      "network.cookie.sameSite.laxByDefault.disabledHosts":
        "example.com,example.org",
    },
  },

  // POLICY: LocalNetworkAccess
  {
    policies: {
      LocalNetworkAccess: {
        Enabled: true,
      },
    },
    unlockedPrefs: {
      "network.lna.enabled": true,
    },
  },
  {
    policies: {
      LocalNetworkAccess: {
        Enabled: false,
        Locked: true,
      },
    },
    lockedPrefs: {
      "network.lna.enabled": false,
    },
  },

  // POLICY: EncryptedMediaExtensions

  {
    policies: {
      EncryptedMediaExtensions: {
        Enabled: false,
        Locked: true,
      },
    },
    lockedPrefs: {
      "media.eme.enabled": false,
    },
  },

  // POLICY: PDFjs

  {
    policies: {
      PDFjs: {
        Enabled: false,
      },
    },
    lockedPrefs: {
      "pdfjs.disabled": true,
    },
  },

  {
    policies: {
      PDFjs: {
        Enabled: true,
        EnablePermissions: true,
      },
    },
    lockedPrefs: {
      "pdfjs.disabled": false,
      "pdfjs.enablePermissions": true,
    },
  },

  {
    policies: {
      PDFjs: {
        Enabled: true,
        EnablePermissions: false,
      },
    },
    lockedPrefs: {
      "pdfjs.disabled": false,
      "pdfjs.enablePermissions": false,
    },
  },

  // POLICY: PictureInPicture

  {
    policies: {
      PictureInPicture: {
        Enabled: false,
        Locked: true,
      },
    },
    lockedPrefs: {
      "media.videocontrols.picture-in-picture.enabled": false,
      "media.videocontrols.picture-in-picture.video-toggle.enabled": false,
    },
  },

  // POLICY: DisabledCiphers
  {
    policies: {
      DisabledCiphers: {
        TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256: false,
        TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256: false,
        TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256: false,
        TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256: false,
        TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384: false,
        TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384: false,
        TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA: false,
        TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA: false,
        TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA: false,
        TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA: false,
        TLS_DHE_RSA_WITH_AES_128_CBC_SHA: false,
        TLS_DHE_RSA_WITH_AES_256_CBC_SHA: false,
        TLS_RSA_WITH_AES_128_GCM_SHA256: false,
        TLS_RSA_WITH_AES_256_GCM_SHA384: false,
        TLS_RSA_WITH_AES_128_CBC_SHA: false,
        TLS_RSA_WITH_AES_256_CBC_SHA: false,
        TLS_RSA_WITH_3DES_EDE_CBC_SHA: false,
        TLS_CHACHA20_POLY1305_SHA256: false,
        TLS_AES_128_GCM_SHA256: false,
        TLS_AES_256_GCM_SHA384: false,
      },
    },
    lockedPrefs: {
      "security.ssl3.ecdhe_rsa_aes_128_gcm_sha256": true,
      "security.ssl3.ecdhe_ecdsa_aes_128_gcm_sha256": true,
      "security.ssl3.ecdhe_ecdsa_chacha20_poly1305_sha256": true,
      "security.ssl3.ecdhe_rsa_chacha20_poly1305_sha256": true,
      "security.ssl3.ecdhe_ecdsa_aes_256_gcm_sha384": true,
      "security.ssl3.ecdhe_rsa_aes_256_gcm_sha384": true,
      "security.ssl3.ecdhe_rsa_aes_128_sha": true,
      "security.ssl3.ecdhe_ecdsa_aes_128_sha": true,
      "security.ssl3.ecdhe_rsa_aes_256_sha": true,
      "security.ssl3.ecdhe_ecdsa_aes_256_sha": true,
      "security.ssl3.dhe_rsa_aes_128_sha": true,
      "security.ssl3.dhe_rsa_aes_256_sha": true,
      "security.ssl3.rsa_aes_128_gcm_sha256": true,
      "security.ssl3.rsa_aes_256_gcm_sha384": true,
      "security.ssl3.rsa_aes_128_sha": true,
      "security.ssl3.rsa_aes_256_sha": true,
      "security.ssl3.deprecated.rsa_des_ede3_sha": true,
      "security.tls13.chacha20_poly1305_sha256": true,
      "security.tls13.aes_128_gcm_sha256": true,
      "security.tls13.aes_256_gcm_sha384": true,
    },
  },

  {
    policies: {
      DisabledCiphers: {
        TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256: true,
        TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256: true,
        TLS_ECDHE_ECDSA_WITH_CHACHA20_POLY1305_SHA256: true,
        TLS_ECDHE_RSA_WITH_CHACHA20_POLY1305_SHA256: true,
        TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384: true,
        TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384: true,
        TLS_ECDHE_RSA_WITH_AES_128_CBC_SHA: true,
        TLS_ECDHE_ECDSA_WITH_AES_128_CBC_SHA: true,
        TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA: true,
        TLS_ECDHE_ECDSA_WITH_AES_256_CBC_SHA: true,
        TLS_DHE_RSA_WITH_AES_128_CBC_SHA: true,
        TLS_DHE_RSA_WITH_AES_256_CBC_SHA: true,
        TLS_RSA_WITH_AES_128_GCM_SHA256: true,
        TLS_RSA_WITH_AES_256_GCM_SHA384: true,
        TLS_RSA_WITH_AES_128_CBC_SHA: true,
        TLS_RSA_WITH_AES_256_CBC_SHA: true,
        TLS_RSA_WITH_3DES_EDE_CBC_SHA: true,
        TLS_CHACHA20_POLY1305_SHA256: true,
        TLS_AES_128_GCM_SHA256: true,
        TLS_AES_256_GCM_SHA384: true,
      },
    },
    lockedPrefs: {
      "security.ssl3.ecdhe_rsa_aes_128_gcm_sha256": false,
      "security.ssl3.ecdhe_ecdsa_aes_128_gcm_sha256": false,
      "security.ssl3.ecdhe_ecdsa_chacha20_poly1305_sha256": false,
      "security.ssl3.ecdhe_rsa_chacha20_poly1305_sha256": false,
      "security.ssl3.ecdhe_ecdsa_aes_256_gcm_sha384": false,
      "security.ssl3.ecdhe_rsa_aes_256_gcm_sha384": false,
      "security.ssl3.ecdhe_rsa_aes_128_sha": false,
      "security.ssl3.ecdhe_ecdsa_aes_128_sha": false,
      "security.ssl3.ecdhe_rsa_aes_256_sha": false,
      "security.ssl3.ecdhe_ecdsa_aes_256_sha": false,
      "security.ssl3.dhe_rsa_aes_128_sha": false,
      "security.ssl3.dhe_rsa_aes_256_sha": false,
      "security.ssl3.rsa_aes_128_gcm_sha256": false,
      "security.ssl3.rsa_aes_256_gcm_sha384": false,
      "security.ssl3.rsa_aes_128_sha": false,
      "security.ssl3.rsa_aes_256_sha": false,
      "security.ssl3.deprecated.rsa_des_ede3_sha": false,
      "security.tls13.chacha20_poly1305_sha256": false,
      "security.tls13.aes_128_gcm_sha256": false,
      "security.tls13.aes_256_gcm_sha384": false,
    },
  },

  {
    policies: {
      WindowsSSO: true,
    },
    lockedPrefs: {
      "network.http.windows-sso.enabled": true,
    },
  },

  {
    policies: {
      Cookies: {
        Behavior: "accept",
        BehaviorPrivateBrowsing: "reject-foreign",
        Locked: true,
      },
    },
    lockedPrefs: {
      "network.cookie.cookieBehavior": 0,
      "network.cookie.cookieBehavior.pbmode": 1,
    },
  },

  {
    policies: {
      Cookies: {
        Behavior: "reject-foreign",
        BehaviorPrivateBrowsing: "reject",
        Locked: true,
      },
    },
    lockedPrefs: {
      "network.cookie.cookieBehavior": 1,
      "network.cookie.cookieBehavior.pbmode": 2,
    },
  },

  {
    policies: {
      Cookies: {
        Behavior: "reject",
        BehaviorPrivateBrowsing: "limit-foreign",
        Locked: true,
      },
    },
    lockedPrefs: {
      "network.cookie.cookieBehavior": 2,
      "network.cookie.cookieBehavior.pbmode": 3,
    },
  },

  {
    policies: {
      Cookies: {
        Behavior: "limit-foreign",
        BehaviorPrivateBrowsing: "reject-tracker",
        Locked: true,
      },
    },
    lockedPrefs: {
      "network.cookie.cookieBehavior": 3,
      "network.cookie.cookieBehavior.pbmode": 4,
    },
  },

  {
    policies: {
      Cookies: {
        Behavior: "reject-tracker",
        BehaviorPrivateBrowsing: "reject-tracker-and-partition-foreign",
        Locked: true,
      },
    },
    lockedPrefs: {
      "network.cookie.cookieBehavior": 4,
      "network.cookie.cookieBehavior.pbmode": 5,
    },
  },
  {
    policies: {
      Cookies: {
        Behavior: "reject-tracker-and-partition-foreign",
        BehaviorPrivateBrowsing: "accept",
        Locked: true,
      },
    },
    lockedPrefs: {
      "network.cookie.cookieBehavior": 5,
      "network.cookie.cookieBehavior.pbmode": 0,
    },
  },

  {
    policies: {
      Cookies: {
        Behavior: "reject-tracker",
        BehaviorPrivateBrowsing: "partition-foreign",
        Locked: true,
      },
    },
    lockedPrefs: {
      "network.cookie.cookieBehavior": 4,
      "network.cookie.cookieBehavior.pbmode": 5,
    },
  },
  {
    policies: {
      Cookies: {
        Behavior: "partition-foreign",
        BehaviorPrivateBrowsing: "accept",
        Locked: true,
      },
    },
    lockedPrefs: {
      "network.cookie.cookieBehavior": 5,
      "network.cookie.cookieBehavior.pbmode": 0,
    },
  },

  {
    policies: {
      UseSystemPrintDialog: true,
    },
    lockedPrefs: {
      "print.prefer_system_dialog": true,
    },
  },

  // Bug 1820195
  {
    policies: {
      Preferences: {
        "pdfjs.cursorToolOnLoad": {
          Value: 1,
          Status: "default",
        },
        "pdfjs.sidebarViewOnLoad": {
          Value: 0,
          Status: "default",
        },
      },
    },
    unlockedPrefs: {
      "pdfjs.cursorToolOnLoad": 1,
      "pdfjs.sidebarViewOnLoad": 0,
    },
  },

  // Bug 1772503
  {
    policies: {
      DisableFirefoxStudies: true,
    },
    lockedPrefs: {
      "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons": false,
      "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features": false,
    },
  },
  {
    policies: {
      Preferences: {
        "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons": {
          Value: true,
        },
        "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features": {
          Value: true,
        },
      },
    },
    lockedPrefs: {
      "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.addons": true,
      "browser.newtabpage.activity-stream.asrouter.userprefs.cfr.features": true,
    },
  },

  // POLICY: AutofillAddressEnabled, AutofillCreditCardEnabled

  {
    policies: {
      AutofillAddressEnabled: false,
      AutofillCreditCardEnabled: false,
    },
    lockedPrefs: {
      "extensions.formautofill.addresses.enabled": false,
      "extensions.formautofill.creditCards.enabled": false,
    },
  },

  // POLICY: Proxy - locking if no values are set
  {
    policies: {
      Proxy: {
        Locked: true,
      },
    },
    lockedPrefs: {
      "network.proxy.type": 5,
    },
  },

  // POLICY: DisableEncryptedClientHello
  {
    policies: {
      DisableEncryptedClientHello: true,
    },
    lockedPrefs: {
      "network.dns.echconfig.enabled": false,
      "network.dns.http3_echconfig.enabled": false,
    },
  },

  // POLICY: PostQuantumKeyAgreementEnabled
  {
    policies: {
      PostQuantumKeyAgreementEnabled: false,
    },
    lockedPrefs: {
      "network.http.http3.enable_kyber": false,
      "security.tls.enable_kyber": false,
    },
  },

  // POLICY: CNSA2KeyAgreementEnabled
  {
    policies: {
      CNSA2KeyAgreementEnabled: true,
    },
    lockedPrefs: {
      "security.tls.enable_mlkem1024": true,
    },
  },

  // POLICY: HttpsOnlyMode
  {
    policies: {
      HttpsOnlyMode: "enabled",
    },
    unlockedPrefs: {
      "dom.security.https_only_mode": true,
    },
  },

  {
    policies: {
      HttpsOnlyMode: "disallowed",
    },
    lockedPrefs: {
      "dom.security.https_only_mode": false,
    },
  },

  {
    policies: {
      HttpsOnlyMode: "force_enabled",
    },
    lockedPrefs: {
      "dom.security.https_only_mode": true,
    },
  },

  // POLICY: SkipTermsOfUse
  {
    policies: {
      SkipTermsOfUse: true,
    },
    lockedPrefs: {
      "termsofuse.acceptedVersion": 999,
      // "termsofuse.acceptedVersion" is a string of
      // the timestamp at which the policy was set
    },
  },

  // POLICY: VisualSearchEnabled
  {
    policies: {
      VisualSearchEnabled: false,
    },
    lockedPrefs: {
      "browser.search.visualSearch.featureGate": false,
    },
  },

  // Bug 1981587
  {
    policies: {
      Preferences: {
        "security.webauthn.always_allow_direct_attestation": {
          Value: true,
          Status: "locked",
        },
      },
    },
    lockedPrefs: {
      "security.webauthn.always_allow_direct_attestation": true,
    },
  },

  // POLICY: XSLTEnabled
  {
    policies: {
      XSLTEnabled: true,
    },
    lockedPrefs: {
      "dom.xslt.enabled": true,
    },
  },
  {
    policies: {
      XSLTEnabled: false,
    },
    lockedPrefs: {
      "dom.xslt.enabled": false,
    },
  },

  // AIControls - all features locked via Default.Locked
  {
    policies: {
      AIControls: {
        Default: { Value: "blocked", Locked: true },
        Translations: { Value: "available" },
        PDFAltText: { Value: "available" },
        SmartTabGroups: { Value: "blocked" },
        LinkPreviewKeyPoints: { Value: "available" },
        SidebarChatbot: { Value: "blocked" },
        SpeechRecognition: { Value: "available" },
        CustomWidgets: { Value: "blocked" },
      },
    },
    lockedPrefs: {
      "browser.ai.control.default": "blocked",
      "browser.ai.control.translations": "available",
      "browser.translations.enable": true,
      "browser.ai.control.pdfjsAltText": "available",
      "pdfjs.enableAltText": true,
      "browser.ai.control.smartTabGroups": "blocked",
      "browser.tabs.groups.smart.userEnabled": false,
      "browser.ai.control.linkPreviewKeyPoints": "available",
      "browser.ml.linkPreview.enabled": true,
      "browser.ai.control.sidebarChatbot": "blocked",
      "browser.ml.chat.enabled": false,
      "browser.ml.chat.page": false,
      "browser.ai.control.speechRecognition": "available",
      "browser.ai.control.generatedWidgets": "blocked",
    },
  },

  // AIControls - per-feature locked override
  {
    policies: {
      AIControls: {
        Default: { Value: "blocked", Locked: true },
        Translations: { Value: "available", Locked: false },
      },
    },
    lockedPrefs: {
      "browser.ai.control.default": "blocked",
      "browser.ai.control.pdfjsAltText": "blocked",
      "pdfjs.enableAltText": false,
      "browser.ai.control.smartTabGroups": "blocked",
      "browser.tabs.groups.smart.userEnabled": false,
      "browser.ai.control.linkPreviewKeyPoints": "blocked",
      "browser.ml.linkPreview.enabled": false,
      "browser.ai.control.sidebarChatbot": "blocked",
      "browser.ml.chat.enabled": false,
      "browser.ml.chat.page": false,
      "browser.ai.control.speechRecognition": "blocked",
      "browser.ai.control.generatedWidgets": "blocked",
    },
    unlockedPrefs: {
      "browser.ai.control.translations": "available",
      "browser.translations.enable": true,
    },
  },

  // GenerativeAI
  {
    policies: {
      GenerativeAI: {
        Enabled: false,
        Chatbot: true,
        Locked: true,
      },
    },
    lockedPrefs: {
      "browser.ml.chat.enabled": true,
      "browser.ml.chat.page": true,
      "browser.ml.linkPreview.optin": false,
      "browser.tabs.groups.smart.userEnabled": false,
    },
  },
];

add_task(async function test_policy_simple_prefs() {
  for (let test of POLICIES_TESTS) {
    await setupPolicyEngineWithJson({
      policies: test.policies,
    });

    info("Checking policy: " + Object.keys(test.policies)[0]);

    for (let [prefName, prefValue] of Object.entries(test.lockedPrefs || {})) {
      checkLockedPref(prefName, prefValue);
    }

    for (let [prefName, prefValue] of Object.entries(
      test.unlockedPrefs || {}
    )) {
      checkUnlockedPref(prefName, prefValue);
    }
  }
});
