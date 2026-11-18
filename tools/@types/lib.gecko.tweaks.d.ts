/**
 * Gecko generic/specialized adjustments for xpcom and webidl types.
 */

// More specific types for parent process browsing contexts.
interface CanonicalBrowsingContext extends LoadContextMixin {
  embedderElement: MozBrowser;
  currentWindowContext: WindowGlobalParent;
  // Set by tabbrowser.
  nonWebControlledLoadingURI?: nsIURI;
  parent: CanonicalBrowsingContext;
  parentWindowContext: WindowGlobalParent;
  top: CanonicalBrowsingContext;
  topWindowContext: WindowGlobalParent;
}

declare namespace ChromeUtils {
  type Modules = import("./generated/lib.gecko.modules").Modules;

  function importESModule<T extends keyof Modules>(
    aResourceURI: T,
    aOptions?: ImportESModuleOptionsDictionary
  ): Modules[T];
}

// Defined by SystemGlobal, see XPCRuntimeService.cpp.
declare var locks: LockManager;

interface ChromeWindow extends Window {
  isChromeWindow: true;
}

// Defined by the <menu> custom element in toolkit/content/widgets/menu.js.
interface XULMenuElement {
  readonly menupopup: XULPopupElement;
}

interface XULElementTagNameMap {
  browser: MozBrowser;
  iframe: XULFrameElement;
  label: XULTextElement;
  menu: XULMenuElement;
  menupopup: XULPopupElement;
  tree: XULTreeElement;
}

interface Document {
  // Map elements created in the SVG namespace to the relevant type.
  createElementNS(
    namespace: "http://www.w3.org/2000/svg",
    qualifiedName: string,
    options?: string | ElementCreationOptions
  ): SVGElement;
  createXULElement<K extends keyof XULElementTagNameMap>(
    localName: K,
    options?: string | ElementCreationOptions
  ): XULElementTagNameMap[K];
  createXULElement(
    localName: string,
    options?: string | ElementCreationOptions
  ): XULElement;
}

interface CustomElementRegistry {
  // Accept XUL custom elements, which the generated CustomElementConstructor
  // leaves out by requiring an HTMLElement.
  define(
    name: string,
    constructor: new () => XULElement,
    options?: ElementDefinitionOptions
  ): void;
}

type nsIGleanPingNoReason = {
  [K in keyof nsIGleanPing]: K extends "submit"
    ? (_?: never) => void
    : nsIGleanPing[K];
};

type nsIGleanPingWithReason<T> = {
  [K in keyof nsIGleanPing]: K extends "submit"
    ? (reason: T) => void
    : nsIGleanPing[K];
};

interface MessageListenerManagerMixin {
  // Overloads that define `data` arg as required, since it's ~always expected.
  addMessageListener(
    msg: string,
    listener: { receiveMessage(_: ReceiveMessageArgument & { data }) }
  );
  removeMessageListener(
    msg: string,
    listener: { receiveMessage(_: ReceiveMessageArgument & { data }) }
  );
}

// @typescript/dom-lib-generator drops `matches` unless it's Element-shaped.
interface MatchPattern {
  matches(uri: URI, explicit?: boolean): boolean;
  matches(url: string, explicit?: boolean): boolean;
}

interface MatchPatternSet {
  matches(uri: URI, explicit?: boolean): boolean;
  matches(url: string, explicit?: boolean): boolean;
}

interface ChannelWrapper {
  matches(
    filter?: MozRequestFilter,
    extension?: WebExtensionPolicy | null,
    options?: MozRequestMatchOptions
  ): boolean;
}

interface MozQueryInterface {
  <T>(iid: T): nsQIResult<T>;
}

interface nsICryptoHash extends nsISupports {
  // Accepts a TypedArray.
  update(aData: ArrayLike<number>, aLen: number): void;
}

// The type generator drops UTF8String dictionary members from IOUtils options.
interface ReadUTF8Options {
  decrypt?: string;
}

interface WriteOptions {
  encrypt?: string;
}

interface nsIDOMWindow extends Window {}

interface nsISimpleEnumerator extends Iterable<any> {}

interface nsISupports {
  wrappedJSObject?: object;
}

interface nsIXPCComponents_Constructor {
  <const T, IIDs = nsIXPCComponents_Interfaces>(
    cid,
    id: T,
    init?
  ): {
    new (...any): nsQIResult<T extends keyof IIDs ? IIDs[T] : T>;
    (...any): nsQIResult<T extends keyof IIDs ? IIDs[T] : T>;
  };
}

interface ComponentsExceptionOptions {
  result?: number;
  stack?: nsIStackFrame;
  data?: object;
}

interface nsIException extends Exception {}

interface nsIXPCComponents_Exception {
  (
    message?: string,
    resultOrOptions?: number | ComponentsExceptionOptions,
    stack?: nsIStackFrame,
    data?: object
  ): nsIException;
}

interface nsIXPCComponents_ID {
  (uuid: string): nsID;
}

interface nsIXPCComponents_utils_Sandbox {
  (principal: nsIPrincipal | nsIPrincipal[], options: object): Sandbox;
}

interface nsXPCComponents_Classes {
  [cid: string]: {
    createInstance<T>(aID: T): nsQIResult<T>;
    getService<T>(aID?: T): unknown extends T ? nsISupports : nsQIResult<T>;
  };
}

// Generic overloads.
interface nsXPCComponents_Utils {
  cloneInto<T>(value: T, ...any): T;
  createObjectIn<T = object>(_, object?: T): T;
  exportFunction<T>(func: T, ...any): T;
  getWeakReference<T>(value: T): { get(): T };
  waiveXrays<T>(object: T): T;
}

type Sandbox = typeof globalThis & nsISupports;

interface WindowGlobalParent extends WindowContext {
  readonly browsingContext: CanonicalBrowsingContext;
}

// https://github.com/microsoft/TypeScript-DOM-lib-generator/issues/1736
interface Localization {
  formatValuesSync(aKeys: L10nKey[]): (string | null)[];
}

/**
 * Redefine the DOMStringMap interface to match its implementation.
 * xref Bug 1965336.
 *
 * [MDN Reference](https://developer.mozilla.org/docs/Web/API/DOMStringMap)
 */
interface DOMStringMap {
  [name: string]: string | undefined;
}

/**
 * Define base64/hex methods for Uint8Array
 * https://github.com/microsoft/TypeScript/issues/61695
 */
interface Uint8Array<TArrayBuffer extends ArrayBufferLike> {
  setFromBase64(
    string: string,
    options?: { alphabet?: string; lastChunkHandling: string }
  ): { read: number; written: number };
  setFromHex(string: string): { read: number; written: number };
  toBase64(options?: { alphabet?: string; omitPadding: boolean }): string;
  toHex(): string;
}
