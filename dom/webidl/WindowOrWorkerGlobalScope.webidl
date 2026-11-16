/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is:
 * https://html.spec.whatwg.org/multipage/webappapis.html#windoworworkerglobalscope-mixin
 * https://fetch.spec.whatwg.org/#fetch-method
 * https://w3c.github.io/webappsec-secure-contexts/#monkey-patching-global-object
 * https://w3c.github.io/ServiceWorker/#self-caches
 */

typedef (Function or TrustedScript or DOMString) TimerHandler;

// https://html.spec.whatwg.org/multipage/webappapis.html#windoworworkerglobalscope-mixin
[Exposed=(Window,Worker)]
interface mixin WindowOrWorkerGlobalScope {
  [Replaceable] readonly attribute USVString origin;
  readonly attribute boolean crossOriginIsolated;

  [Throws, NeedsCallerType]
  undefined reportError(any e);

  // base64 utility methods
  [Throws]
  DOMString btoa(DOMString btoa);
  [Throws]
  DOMString atob(DOMString atob);

  // timers
  [Throws, NeedsSubjectPrincipal=NonSystem]
  long setTimeout(TimerHandler handler, optional long timeout = 0, any... arguments);
  undefined clearTimeout(optional long handle = 0);
  [Throws, NeedsSubjectPrincipal=NonSystem]
  long setInterval(TimerHandler handler, optional long timeout = 0, any... unused);
  undefined clearInterval(optional long handle = 0);

  // microtask queuing
  undefined queueMicrotask(VoidFunction callback);

  // ImageBitmap
  [Throws]
  Promise<ImageBitmap> createImageBitmap(ImageBitmapSource aImage,
                                         optional ImageBitmapOptions aOptions = {});
  [Throws]
  Promise<ImageBitmap> createImageBitmap(ImageBitmapSource aImage,
                                         long aSx, long aSy, long aSw, long aSh,
                                         optional ImageBitmapOptions aOptions = {});

  // structured cloning
  [Throws]
  any structuredClone(any value, optional StructuredSerializeOptions options = {});
};

// https://fetch.spec.whatwg.org/#fetch-method
partial interface mixin WindowOrWorkerGlobalScope {
  [NewObject, NeedsCallerType]
  Promise<Response> fetch(RequestInfo input, optional RequestInit init = {});
};

// https://w3c.github.io/webappsec-secure-contexts/#monkey-patching-global-object
partial interface mixin WindowOrWorkerGlobalScope {
  readonly attribute boolean isSecureContext;
};

// http://w3c.github.io/IndexedDB/#factory-interface
partial interface mixin WindowOrWorkerGlobalScope {
  // readonly attribute IDBFactory indexedDB; // bug 1776789
  [Throws]
  readonly attribute IDBFactory? indexedDB;
};

// https://w3c.github.io/ServiceWorker/#self-caches
partial interface mixin WindowOrWorkerGlobalScope {
  [Throws, Func="cache::CacheStorage::CachesEnabled", SameObject]
  readonly attribute CacheStorage caches;
};

// https://wicg.github.io/scheduling-apis/#ref-for-windoworworkerglobalscope-scheduler
partial interface mixin WindowOrWorkerGlobalScope {
  [Replaceable, Pref="dom.enable_web_task_scheduling", SameObject]
  readonly attribute Scheduler scheduler;
};


// https://w3c.github.io/trusted-types/dist/spec/#extensions-to-the-windoworworkerglobalscope-interface
partial interface mixin WindowOrWorkerGlobalScope {
  readonly attribute TrustedTypePolicyFactory trustedTypes;
};

partial interface mixin WindowOrWorkerGlobalScope {
  // A testing function to check if the given target is active in the current context.
  // See valid targets at toolkit/components/resistfingerprinting/RFPTargets.inc
  [Throws, Pref="privacy.fingerprintingProtection.testing"]
  boolean isRFPTargetActive(DOMString aTargetName);
};
