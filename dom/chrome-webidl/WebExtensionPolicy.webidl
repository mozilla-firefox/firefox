/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

interface Principal;
interface URI;
interface WindowProxy;

callback WebExtensionLocalizeCallback = DOMString (DOMString unlocalizedText);

/**
 * Defines the platform-level policies for a WebExtension, including its
 * permissions and the characteristics of its moz-extension: URLs.
 */
[ChromeOnly, Exposed=Window]
interface WebExtensionPolicy {
  [Throws]
  constructor(WebExtensionInit options);

  /**
   * The add-on's internal ID, as specified in its manifest.json file or its
   * XPI signature.
   */
  [Constant, StoreInSlot]
  readonly attribute DOMString id;

  /**
   * The hostname part of the extension's moz-extension: URLs. This value is
   * generated randomly at install time.
   */
  [Constant, StoreInSlot]
  readonly attribute ByteString mozExtensionHostname;

  /**
   * The file: or jar: URL to use for the base of the extension's
   * moz-extension: URL root.
   */
  [Constant]
  readonly attribute ByteString baseURL;

  /**
   * The extension's user-visible name.
   */
  [Constant]
  readonly attribute DOMString name;

  /**
   * The extension's version string.
   */
  [Constant]
  readonly attribute DOMString version;

  /**
   * The add-on's internal type as determined by parsing the manifest.json file.
   */
  [Constant]
  readonly attribute DOMString type;

  /**
   * Whether the extension has access to privileged features
   */
  [Constant]
  readonly attribute boolean isPrivileged;

  /**
   * Whether the extension is installed temporarily
   */
  [Constant]
  readonly attribute boolean temporarilyInstalled;

  /**
   * The manifest version in use by the extension.
   */
  [Constant]
  readonly attribute unsigned long manifestVersion;

  /**
   * The base content security policy string to apply on extension
   * pages for this extension.  The baseCSP is specific to the
   * manifest version.  If the manifest version is 3 or higher it
   * is also applied to content scripts.
   */
  [Constant]
  readonly attribute DOMString baseCSP;

  /**
   * The content security policy string to apply to all pages loaded from the
   * extension's moz-extension: protocol.  If one is not provided by the
   * extension the default value from preferences is used.
   * See extensions.webextensions.default-content-security-policy.
   */
  [Constant]
  readonly attribute DOMString extensionPageCSP;

  /**
   * The content security policy string to apply to all sandboxed pages loaded from the
   * extension. This is set in the extension manifest.
   * If one is not provided by the extension it falls back to:
   * "sandbox allow-scripts; script-src 'self';".
   */
  [Constant]
  readonly attribute DOMString sandboxPageCSP;

  /**
   * The list of currently-active permissions for the extension, as specified
   * in its manifest.json file. May be updated to reflect changes in the
   * extension's optional permissions.
   */
  [Cached, Frozen, Pure]
  attribute sequence<DOMString> permissions;

  /**
   * Match patterns for the set of web origins to which the extension is
   * currently allowed access. May be updated to reflect changes in the
   * extension's optional permissions.
   */
  [Pure]
  attribute MatchPatternSet allowedOrigins;

  /**
   * Whether access to the file scheme is allowed, independently of whether
   * the extension's host permissions permit it.
   * If false, file access is never allowed. If true, file access may be
   * allowed if allowedOrigins contains `<all_urls>` or a `file:`-permission.
   * The value may change at runtime through changes to permissions.
   */
  [Pure]
  readonly attribute boolean fileSchemeAllowed;

  /**
   * An ordered list of guards matching URLs this extension can't access.
   */
  [Cached, Frozen, Pure]
  attribute sequence<ExtensionGuardSet> guardSets;

  /**
   * Returns the source of the first matching guard or null if none.
   */
  ExtensionGuardSource? checkGuarded(URI uri);

  /**
   * The set of content scripts active for this extension.
   */
  [Cached, Frozen, Pure]
  readonly attribute sequence<WebExtensionContentScript> contentScripts;

  /**
   * True if the extension is currently active, false otherwise. When active,
   * the extension's moz-extension: protocol will point to the given baseURI,
   * and the set of policies for this object will be active for its ID.
   *
   * Only one extension policy with a given ID or hostname may be active at a
   * time. Attempting to activate a policy while a conflicting policy is
   * active will raise an error.
   */
  [Affects=Everything, SetterThrows]
  attribute boolean active;

  /**
   * True if this extension is exempt from quarantine.
   */
  [Cached, Pure]
  attribute boolean ignoreQuarantine;

  /**
   * True if this extension has recommended state.
   */
  [Cached, Pure]
  readonly attribute boolean hasRecommendedState;

  /**
   * True if both e10s and webextensions.remote are enabled.  This must be
   * used instead of checking the remote pref directly since remote extensions
   * require both to be enabled.
   */
  static readonly attribute boolean useRemoteWebExtensions;

  /**
   * True if the calling process is an extension process.
   */
  static readonly attribute boolean isExtensionProcess;

  /**
   * Whether the background.service_worker in the extension manifest.json file
   * is enabled.
   *
   * NOTE: **do not use Services.prefs to retrieve the value of the undelying pref**
   *
   * It is defined in StaticPrefList.yaml as `mirror: once` and so checking
   * its current value using Services.prefs doesn't guarantee that it does
   * match the value as accessible from the C++ layers, and unexpected issue
   * may be possible if different code has a different idea of its value.
   */
  static readonly attribute boolean backgroundServiceWorkerEnabled;

  /**
   * Whether the Quarantined Domains feature is enabled.  Use this as a single
   * source of truth instead of checking extensions.QuarantinedDomains.enabled
   * pref directly because the logic might change.
   */
  static readonly attribute boolean quarantinedDomainsEnabled;

  /**
   * Set based on the manifest.incognito value:
   * If "spanning" or "split" will be true.
   * If "not_allowed" will be false.
   */
  [Pure]
  readonly attribute boolean privateBrowsingAllowed;

  /**
   * Returns true if the extension can access a window.  Access is
   * determined by matching the windows private browsing context
   * with privateBrowsingMode.  This does not, and is not meant to
   * handle specific differences between spanning and split mode.
   */
  [Affects=Nothing]
  boolean canAccessWindow(WindowProxy window);

  /**
   * Flags with "ACCESS_" prefix for canAccessURI and canAccessDocument.
   * Keep list in sync with the shorthands in WebExtensionPolicy.h.
   */
  // Match iff any host permission has a matching non-wildcard domain.
  const unsigned long ACCESS_REQUIRE_EXPLICIT_DOMAIN = 0x01;
  // Skip host permission check; require URL to match `<all_urls>` instead.
  const unsigned long ACCESS_WITHOUT_HOST_PERMISSIONS = 0x02;
  // Opt out of enforcement of restricted/quarantined domains and guard sets.
  const unsigned long ACCESS_IGNORE_RESTRICTIONS = 0x04;
  // Allow "file:" match patterns to match file:-URLs iff fileSchemeAllowed.
  const unsigned long ACCESS_FILE_WITH_OPT_IN = 0x08;
  // Allow "file:" match patterns to match file:-URLs, unconditionally.
  const unsigned long ACCESS_FILE_ALWAYS = 0x10;
  // Allow match of own moz-extension:-URL (host permissions are not checked).
  const unsigned long ACCESS_OWN_EXTENSION = 0x20;

  /**
   * Returns true if the extension has cross-origin access to the given URI.
   * See canAccessDocument, which is preferred for documents.
   *
   * Flags can be a combination of the ACCESS_-prefixed flags above.
   */
  boolean canAccessURI(URI uri, optional unsigned long flags = 0);

  /**
   * Returns true if the extension has access to the document of the given
   * window global, based on host permissions, restrictions and the given
   * flags, like canAccessURI. getURLForMatching resolves the URL to check.
   * The window global is not required to be current.
   *
   * Extensions can only access documents when this method returns true, with
   * the following exceptions (of situations that return false but can still
   * be accessed by extensions):
   * - A top-level about:blank document from a direct navigation has a null
   *   principal without precursor. Content scripts may run there (see
   *   https://bugzilla.mozilla.org/show_bug.cgi?id=2034938#c10), but this
   *   method returns false unless ACCESS_WITHOUT_HOST_PERMISSIONS is set.
   * - Privileged extensions can use experiment_apis to run code with system
   *   privileges, bypassing any checks enforced by WebExtensionPolicy.
   *
   * canAccessURI can return true where canAccessDocument returns false:
   * - data-URL iframe has a null principal with precursor from parent page.
   *   canAccessURI returns true because `<all_urls>` contains `data:`.
   *   canAccessDocument returns false if web page is not in host permissions.
   * - JSON documents may have URLs that match a permitted scheme (e.g. https)
   *   but if opened with jsonview they have the resource://devtools principal.
   * - PDF documents may have URLs that match a permitted scheme (e.g. https)
   *   but if opened with PDF.js have the resource://pdf.js principal.
   *   Extensions are currently not allowed to script PDF.js (bug 1454760).
   *
   * canAccessURI can return false where canAccessDocument returns true:
   * - about:blank iframe inherits the principal from the web page.
   *   canAccessURI returns false because `about:` is not a permitted scheme.
   *   canAccessDocument may return true if web page is in host permissions.
   *
   * Flags can be a combination of the ACCESS_-prefixed flags above.
   */
  boolean canAccessDocument(WindowGlobalParent windowGlobal,
                            optional unsigned long flags = 0);
  boolean canAccessDocument(WindowGlobalChild windowGlobal,
                            optional unsigned long flags = 0);

  /**
   * Returns the URL used for matching in canAccessDocument, given a
   * document described by the given principal and URL:
   * - For a content principal, its URI is returned. This is the common case.
   * - For a null principal, its precursor URI or the documentURI is returned.
   * - For a system principal or expanded principal, null is returned.
   *
   * In short: principal.URI is usually returned, except for null principals.
   * When a null principal is passed:
   * - https/http/view-source/file-URLs: documentURI is returned.
   * - data:-URLs: precursor URI if available, documentURI otherwise.
   * - everything else: precursor URI if available, documentURI otherwise.
   */
  static URI? getURLForMatching(Principal principal, URI documentURI);

  /**
   * Returns true if the extension currently has the given permission.
   */
  boolean hasPermission(DOMString permission);

  /**
   * Returns true if the domain is on the Quarantined Domains list.
   */
  static boolean isQuarantinedURI(URI uri);

  /**
   * Returns true if this extension is quarantined from the URI.
   */
  boolean quarantinedFromURI(URI uri);

  /**
   * Returns true if the given path relative to the extension's moz-extension:
   * URL root is listed as a web accessible path. Access checks on a path, such
   * as performed in nsScriptSecurityManager, use sourceMayAccessPath below.
   */
  boolean isWebAccessiblePath(UTF8String pathname);

  /**
   * Returns true if the given path relative to the extension's moz-extension:
   * URL root may be accessed by web content at sourceURI.  For Manifest V2,
   * sourceURI is ignored and the path must merely be listed as web accessible.
   */
  boolean sourceMayAccessPath(URI sourceURI, UTF8String pathname);

  /**
   * Replaces localization placeholders in the given string with localized
   * text from the extension's currently active locale.
   */
  DOMString localize(DOMString unlocalizedText);

  /**
   * Returns the moz-extension: URL for the given path.
   */
  [Throws]
  DOMString getURL(optional DOMString path = "");

  /**
   * Register a new content script programmatically.
   */
  [Throws]
  undefined registerContentScript(WebExtensionContentScript script);

  /**
   * Unregister a content script.
   */
  [Throws]
  undefined unregisterContentScript(WebExtensionContentScript script);

  /**
   * Injects the extension's content script into all existing matching windows.
   */
  [Throws]
  undefined injectContentScripts();

  /**
   * Returns the list of currently active extension policies.
   */
  static sequence<WebExtensionPolicy> getActiveExtensions();

  /**
   * Returns the currently-active policy for the extension with the given ID,
   * or null if no policy is active for that ID.
   */
  static WebExtensionPolicy? getByID(DOMString id);

  /**
   * Returns the currently-active policy for the extension with the given
   * moz-extension: hostname, or null if no policy is active for that
   * hostname.
   */
  static WebExtensionPolicy? getByHostname(ByteString hostname);

  /**
   * Returns the currently-active policy for the extension URI, or
   * null if the URI is not an extension URI, or no policy is currently active
   * for it.
   */
  static WebExtensionPolicy? getByURI(URI uri);

  /**
   * Returns true if the URI is restricted for any extension.
   */
  static boolean isRestrictedURI(URI uri);

  /**
   * When present, the extension is not yet ready to load URLs. In that case,
   * this policy object is a stub, and the attribute contains a promise which
   * resolves to a new, non-stub policy object when the extension is ready.
   *
   * This may be used to delay operations, such as loading extension pages,
   * which depend on extensions being fully initialized.
   *
   * Note: This will always be either a Promise<WebExtensionPolicy?> or null,
   * but the WebIDL grammar does not allow us to specify a nullable Promise
   * type.
   *
   * Note: This could resolve to null when the startup was interrupted.
   */
  readonly attribute object? readyPromise;

  /**
   * Returns true if the given worker script URL matches the background
   * service worker url declared in the extension manifest.json file.
   */
  boolean isManifestBackgroundWorker(DOMString workerURL);

  /**
   * Get the unique BrowsingContextGroup ID which will be used for toplevel
   * page loads from this extension.
   *
   * This method will raise an exception if called from outside of the parent
   * process, or if the extension is inactive.
   */
  [Throws]
  readonly attribute unsigned long long browsingContextGroupId;
};

dictionary WebAccessibleResourceInit {
  required sequence<MatchGlobOrString> resources;
  MatchPatternSetOrStringSequence? matches = null;
  sequence<DOMString>? extension_ids = null;
};

dictionary WebExtensionInit {
  required DOMString id;

  required ByteString mozExtensionHostname;

  required DOMString baseURL;

  DOMString name = "";

  DOMString version = "";

  DOMString type = "";

  boolean isPrivileged = false;

  boolean ignoreQuarantine = false;

  boolean hasRecommendedState = false;

  boolean temporarilyInstalled = false;

  required WebExtensionLocalizeCallback localizeCallback;

  required MatchPatternSetOrStringSequence allowedOrigins;

  sequence<DOMString> permissions = [];

  sequence<WebAccessibleResourceInit> webAccessibleResources = [];

  sequence<WebExtensionContentScriptInit> contentScripts = [];

  // The use of a content script csp is determined by the manifest version.
  unsigned long manifestVersion = 2;
  DOMString? extensionPageCSP = null;

  DOMString? sandboxPageCSP = null;
  sequence<MatchGlobOrString>? sandboxPages = null;

  sequence<DOMString>? backgroundScripts = null;
  DOMString? backgroundWorkerScript = null;

  // Whether the background scripts should be loaded as ES modules.
  boolean backgroundTypeModule = false;

  Promise<WebExtensionPolicy?> readyPromise;
};
