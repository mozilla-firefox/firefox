/**
 * NOTE: Do not modify this file by hand.
 * Content was generated from source XPCOM .idl files.
 * If you're updating some of the sources, see README for instructions.
 */

declare global {

// https://searchfox.org/firefox-main/source/accessible/interfaces/nsIAccessibleMacInterface.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIAccessibleMacNSObjectWrapper) --> */
interface nsIAccessibleMacNSObjectWrapper extends nsISupports {
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsIAccessibleMacInterface) --> */
interface nsIAccessibleMacInterface extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIAccessibleMacInterface_attributeNames) --> */
  readonly attributeNames: string[];
  /** <!-- binding_to(idl, attribute, XPIDL_nsIAccessibleMacInterface_parameterizedAttributeNames) --> */
  readonly parameterizedAttributeNames: string[];
  /** <!-- binding_to(idl, attribute, XPIDL_nsIAccessibleMacInterface_actionNames) --> */
  readonly actionNames: string[];
  /** <!-- binding_to(idl, method, XPIDL_nsIAccessibleMacInterface_getAttributeValue) --> */
  getAttributeValue(attributeName: string): any;
  /** <!-- binding_to(idl, method, XPIDL_nsIAccessibleMacInterface_getParameterizedAttributeValue) --> */
  getParameterizedAttributeValue(attributeName: string, parameter: any): any;
  /** <!-- binding_to(idl, method, XPIDL_nsIAccessibleMacInterface_getActionDescription) --> */
  getActionDescription(actionName: string): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIAccessibleMacInterface_performAction) --> */
  performAction(actionName: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIAccessibleMacInterface_isAttributeSettable) --> */
  isAttributeSettable(attributeName: string): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIAccessibleMacInterface_setAttributeValue) --> */
  setAttributeValue(attributeName: string, attributeValue: any): void;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsIAccessibleMacEvent) --> */
interface nsIAccessibleMacEvent extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIAccessibleMacEvent_macIface) --> */
  readonly macIface: nsIAccessibleMacInterface;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIAccessibleMacEvent_data) --> */
  readonly data: any;
}

// https://searchfox.org/firefox-main/source/browser/components/migration/nsIKeychainMigrationUtils.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIKeychainMigrationUtils) --> */
interface nsIKeychainMigrationUtils extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIKeychainMigrationUtils_getGenericPassword) --> */
  getGenericPassword(aServiceName: string, aAccountName: string): string;
}

// https://searchfox.org/firefox-main/source/browser/components/shell/nsIMacShellService.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIMacShellService) --> */
interface nsIMacShellService extends nsIShellService {
  /** <!-- binding_to(idl, method, XPIDL_nsIMacShellService_showDesktopPreferences) --> */
  showDesktopPreferences(): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacShellService_showSecurityPreferences) --> */
  showSecurityPreferences(aPaneID: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacShellService_getAvailableApplicationsForProtocol) --> */
  getAvailableApplicationsForProtocol(protocol: string): string[][];
  /** <!-- binding_to(idl, attribute, XPIDL_nsIMacShellService_canSetAsDefaultHandler) --> */
  readonly canSetAsDefaultHandler: boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacShellService_setAsDefaultHandlerFor) --> */
  setAsDefaultHandlerFor(aFileExtensionOrProtocol: string): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacShellService_isDefaultHandlerFor) --> */
  isDefaultHandlerFor(aFileExtensionOrProtocol: string): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacShellService_isDefaultHandlerAWebBrowserFor) --> */
  isDefaultHandlerAWebBrowserFor(aFileExtensionOrProtocol: string): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacShellService_enableLaunchOnLogin) --> */
  enableLaunchOnLogin(): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacShellService_disableLaunchOnLogin) --> */
  disableLaunchOnLogin(): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacShellService_getLaunchOnLoginEnabled) --> */
  getLaunchOnLoginEnabled(): boolean;
}

// https://searchfox.org/firefox-main/source/toolkit/xre/nsIASWebAuthSessionRequest.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIASWebAuthSessionRequest) --> */
interface nsIASWebAuthSessionRequest extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIASWebAuthSessionRequest_uuid) --> */
  readonly uuid: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIASWebAuthSessionRequest_url) --> */
  readonly url: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIASWebAuthSessionRequest_callbackScheme) --> */
  readonly callbackScheme: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIASWebAuthSessionRequest_hasCallback) --> */
  readonly hasCallback: boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIASWebAuthSessionRequest_useEphemeralSession) --> */
  readonly useEphemeralSession: boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIASWebAuthSessionRequest_additionalHeaderNames) --> */
  readonly additionalHeaderNames: string[];
  /** <!-- binding_to(idl, method, XPIDL_nsIASWebAuthSessionRequest_getAdditionalHeader) --> */
  getAdditionalHeader(name: string): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIASWebAuthSessionRequest_matchesCallbackURL) --> */
  matchesCallbackURL(url: string): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIASWebAuthSessionRequest_complete) --> */
  complete(callbackURL: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIASWebAuthSessionRequest_cancel) --> */
  cancel(): void;
}

// https://searchfox.org/firefox-main/source/widget/nsICocoaAppShellTestUtils.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsICocoaAppShellTestUtils) --> */
interface nsICocoaAppShellTestUtils extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsICocoaAppShellTestUtils_postNativeTestEvent) --> */
  postNativeTestEvent(): void;
  /** <!-- binding_to(idl, attribute, XPIDL_nsICocoaAppShellTestUtils_lostNativeWakeups) --> */
  readonly lostNativeWakeups: u32;
  /** <!-- binding_to(idl, method, XPIDL_nsICocoaAppShellTestUtils_getMainMenuItemTitles) --> */
  getMainMenuItemTitles(): string[];
}

// https://searchfox.org/firefox-main/source/widget/nsIMacDockSupport.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIAppBundleLaunchOptions) --> */
interface nsIAppBundleLaunchOptions extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIAppBundleLaunchOptions_addsToRecentItems) --> */
  readonly addsToRecentItems: boolean;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsIMacDockSupport) --> */
interface nsIMacDockSupport extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIMacDockSupport_dockMenu) --> */
  dockMenu: nsIStandaloneNativeMenu;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacDockSupport_activateApplication) --> */
  activateApplication(aIgnoreOtherApplications: boolean): void;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIMacDockSupport_badgeText) --> */
  badgeText: string;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacDockSupport_setBadgeImage) --> */
  setBadgeImage(aBadgeImage: imgIContainer, aPaintContext?: nsISVGPaintContext): void;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIMacDockSupport_isAppInDock) --> */
  readonly isAppInDock: boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacDockSupport_ensureAppIsPinnedToDock) --> */
  ensureAppIsPinnedToDock(aAppPath?: string, aAppToReplacePath?: string): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacDockSupport_launchAppBundle) --> */
  launchAppBundle(aAppBundle: nsIFile, aArgs: string[], aLaunchOptions?: nsIAppBundleLaunchOptions): void;
}

// https://searchfox.org/firefox-main/source/widget/nsIMacFinderProgress.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIMacFinderProgressCanceledCallback) --> */
type nsIMacFinderProgressCanceledCallback = Callable<{
  /** <!-- binding_to(idl, method, XPIDL_nsIMacFinderProgressCanceledCallback_canceled) --> */
  canceled(): void;
}>

/** <!-- binding_to(idl, interface_name, XPIDL_nsIMacFinderProgress) --> */
interface nsIMacFinderProgress extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIMacFinderProgress_init) --> */
  init(path: string, canceledCallback: nsIMacFinderProgressCanceledCallback): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacFinderProgress_updateProgress) --> */
  updateProgress(currentProgress: u64, totalProgress: u64): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacFinderProgress_end) --> */
  end(): void;
}

// https://searchfox.org/firefox-main/source/widget/nsIMacSharingService.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIMacShareCustomItemHandler) --> */
type nsIMacShareCustomItemHandler = Callable<{
  /** <!-- binding_to(idl, method, XPIDL_nsIMacShareCustomItemHandler_handle) --> */
  handle(): void;
}>

/** <!-- binding_to(idl, interface_name, XPIDL_nsIMacShareCustomItem) --> */
interface nsIMacShareCustomItem extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIMacShareCustomItem_label) --> */
  readonly label: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIMacShareCustomItem_icon) --> */
  readonly icon: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIMacShareCustomItem_handler) --> */
  readonly handler: nsIMacShareCustomItemHandler;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsIMacSharingService) --> */
interface nsIMacSharingService extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIMacSharingService_shareUrlWithPicker) --> */
  shareUrlWithPicker(anchor: Element, urls: string[], titles: string[], shareTitle: string, customItems: nsIMacShareCustomItem[], copyItem: nsIMacShareCustomItem): void;
}

// https://searchfox.org/firefox-main/source/widget/nsIMacUserActivityUpdater.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIMacUserActivityUpdater) --> */
interface nsIMacUserActivityUpdater extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIMacUserActivityUpdater_updateLocation) --> */
  updateLocation(pageUrl: string, pageTitle: string, window: nsIBaseWindow): void;
}

// https://searchfox.org/firefox-main/source/widget/nsIMacWebAppUtils.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsITrashAppCallback) --> */
type nsITrashAppCallback = Callable<{
  /** <!-- binding_to(idl, method, XPIDL_nsITrashAppCallback_trashAppFinished) --> */
  trashAppFinished(rv: nsresult): void;
}>

/** <!-- binding_to(idl, interface_name, XPIDL_nsIMacWebAppUtils) --> */
interface nsIMacWebAppUtils extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIMacWebAppUtils_pathForAppWithIdentifier) --> */
  pathForAppWithIdentifier(bundleIdentifier: string): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacWebAppUtils_launchAppWithIdentifier) --> */
  launchAppWithIdentifier(bundleIdentifier: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacWebAppUtils_trashApp) --> */
  trashApp(path: string, callback: nsITrashAppCallback): void;
}

// https://searchfox.org/firefox-main/source/widget/nsIStandaloneNativeMenu.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIStandaloneNativeMenu) --> */
interface nsIStandaloneNativeMenu extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIStandaloneNativeMenu_init) --> */
  init(aElement: Element): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIStandaloneNativeMenu_menuWillOpen) --> */
  menuWillOpen(): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIStandaloneNativeMenu_activateNativeMenuItemAt) --> */
  activateNativeMenuItemAt(anIndexString: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIStandaloneNativeMenu_forceUpdateNativeMenuAt) --> */
  forceUpdateNativeMenuAt(anIndexString: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIStandaloneNativeMenu_dump) --> */
  dump(): void;
}

// https://searchfox.org/firefox-main/source/widget/nsITaskbarProgress.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsITaskbarProgress) --> */
interface nsITaskbarProgress extends nsISupports {
  /** <!-- binding_to(idl, const, XPIDL_nsITaskbarProgress_STATE_NO_PROGRESS) --> */
  readonly STATE_NO_PROGRESS?: 0;
  /** <!-- binding_to(idl, const, XPIDL_nsITaskbarProgress_STATE_INDETERMINATE) --> */
  readonly STATE_INDETERMINATE?: 1;
  /** <!-- binding_to(idl, const, XPIDL_nsITaskbarProgress_STATE_NORMAL) --> */
  readonly STATE_NORMAL?: 2;
  /** <!-- binding_to(idl, const, XPIDL_nsITaskbarProgress_STATE_ERROR) --> */
  readonly STATE_ERROR?: 3;
  /** <!-- binding_to(idl, const, XPIDL_nsITaskbarProgress_STATE_PAUSED) --> */
  readonly STATE_PAUSED?: 4;

  /** <!-- binding_to(idl, method, XPIDL_nsITaskbarProgress_setProgressState) --> */
  setProgressState(state: nsTaskbarProgressState, currentValue?: u64, maxValue?: u64): void;
}

// https://searchfox.org/firefox-main/source/widget/nsITouchBarHelper.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsITouchBarHelper) --> */
interface nsITouchBarHelper extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarHelper_activeUrl) --> */
  readonly activeUrl: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarHelper_activeTitle) --> */
  readonly activeTitle: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarHelper_isUrlbarFocused) --> */
  readonly isUrlbarFocused: boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsITouchBarHelper_toggleFocusUrlbar) --> */
  toggleFocusUrlbar(): void;
  /** <!-- binding_to(idl, method, XPIDL_nsITouchBarHelper_unfocusUrlbar) --> */
  unfocusUrlbar(): void;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarHelper_allItems) --> */
  allItems: nsIArray;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarHelper_document) --> */
  readonly document: Document;
  /** <!-- binding_to(idl, method, XPIDL_nsITouchBarHelper_getTouchBarInput) --> */
  getTouchBarInput(aInputName: string): nsITouchBarInput;
  /** <!-- binding_to(idl, method, XPIDL_nsITouchBarHelper_insertRestrictionInUrlbar) --> */
  insertRestrictionInUrlbar(aToken: string): void;
}

// https://searchfox.org/firefox-main/source/widget/nsITouchBarInput.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsITouchBarInputCallback) --> */
type nsITouchBarInputCallback = Callable<{
  /** <!-- binding_to(idl, method, XPIDL_nsITouchBarInputCallback_onCommand) --> */
  onCommand(): void;
}>

/** <!-- binding_to(idl, interface_name, XPIDL_nsITouchBarInput) --> */
interface nsITouchBarInput extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarInput_key) --> */
  readonly key: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarInput_title) --> */
  title: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarInput_image) --> */
  image: nsIURI;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarInput_type) --> */
  type: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarInput_callback) --> */
  callback: nsITouchBarInputCallback;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarInput_color) --> */
  color: u32;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarInput_disabled) --> */
  disabled: boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITouchBarInput_children) --> */
  children: nsIArray;
}

// https://searchfox.org/firefox-main/source/widget/nsITouchBarUpdater.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsITouchBarUpdater) --> */
interface nsITouchBarUpdater extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsITouchBarUpdater_updateTouchBarInputs) --> */
  updateTouchBarInputs(aWindow: nsIBaseWindow, aInputs: nsITouchBarInput[]): void;
  /** <!-- binding_to(idl, method, XPIDL_nsITouchBarUpdater_enterCustomizeMode) --> */
  enterCustomizeMode(): void;
  /** <!-- binding_to(idl, method, XPIDL_nsITouchBarUpdater_isTouchBarInitialized) --> */
  isTouchBarInitialized(): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsITouchBarUpdater_setTouchBarInitialized) --> */
  setTouchBarInitialized(aIsInitialized: boolean): void;
  /** <!-- binding_to(idl, method, XPIDL_nsITouchBarUpdater_showPopover) --> */
  showPopover(aWindow: nsIBaseWindow, aPopover: nsITouchBarInput, aShowing: boolean): void;
}

// https://searchfox.org/firefox-main/source/xpcom/base/nsIMacPreferencesReader.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIMacPreferencesReader) --> */
interface nsIMacPreferencesReader extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIMacPreferencesReader_policiesEnabled) --> */
  policiesEnabled(): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIMacPreferencesReader_readPolicySources) --> */
  readPolicySources(): any;
}

// https://searchfox.org/firefox-main/source/xpcom/io/nsILocalFileMac.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsILocalFileMac) --> */
interface nsILocalFileMac extends nsIFile {
  /** <!-- binding_to(idl, method, XPIDL_nsILocalFileMac_launchWithDoc) --> */
  launchWithDoc(aDocToLoad: nsIFile, aLaunchInBackground: boolean): void;
  /** <!-- binding_to(idl, method, XPIDL_nsILocalFileMac_isPackage) --> */
  isPackage(): boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsILocalFileMac_bundleDisplayName) --> */
  readonly bundleDisplayName: string;
  /** <!-- binding_to(idl, method, XPIDL_nsILocalFileMac_hasXAttr) --> */
  hasXAttr(aAttrName: string): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsILocalFileMac_getXAttr) --> */
  getXAttr(aAttrName: string): u8[];
  /** <!-- binding_to(idl, method, XPIDL_nsILocalFileMac_setXAttr) --> */
  setXAttr(aAttrName: string, aAttrValue: u8[]): void;
  /** <!-- binding_to(idl, method, XPIDL_nsILocalFileMac_delXAttr) --> */
  delXAttr(aAttrName: string): void;
}

interface nsIXPCComponents_Interfaces {
  nsIAccessibleMacNSObjectWrapper: nsJSIID<nsIAccessibleMacNSObjectWrapper>;
  nsIAccessibleMacInterface: nsJSIID<nsIAccessibleMacInterface>;
  nsIAccessibleMacEvent: nsJSIID<nsIAccessibleMacEvent>;
  nsIKeychainMigrationUtils: nsJSIID<nsIKeychainMigrationUtils>;
  nsIMacShellService: nsJSIID<nsIMacShellService>;
  nsIASWebAuthSessionRequest: nsJSIID<nsIASWebAuthSessionRequest>;
  nsICocoaAppShellTestUtils: nsJSIID<nsICocoaAppShellTestUtils>;
  nsIAppBundleLaunchOptions: nsJSIID<nsIAppBundleLaunchOptions>;
  nsIMacDockSupport: nsJSIID<nsIMacDockSupport>;
  nsIMacFinderProgressCanceledCallback: nsJSIID<nsIMacFinderProgressCanceledCallback>;
  nsIMacFinderProgress: nsJSIID<nsIMacFinderProgress>;
  nsIMacShareCustomItemHandler: nsJSIID<nsIMacShareCustomItemHandler>;
  nsIMacShareCustomItem: nsJSIID<nsIMacShareCustomItem>;
  nsIMacSharingService: nsJSIID<nsIMacSharingService>;
  nsIMacUserActivityUpdater: nsJSIID<nsIMacUserActivityUpdater>;
  nsITrashAppCallback: nsJSIID<nsITrashAppCallback>;
  nsIMacWebAppUtils: nsJSIID<nsIMacWebAppUtils>;
  nsIStandaloneNativeMenu: nsJSIID<nsIStandaloneNativeMenu>;
  nsITaskbarProgress: nsJSIID<nsITaskbarProgress>;
  nsITouchBarHelper: nsJSIID<nsITouchBarHelper>;
  nsITouchBarInputCallback: nsJSIID<nsITouchBarInputCallback>;
  nsITouchBarInput: nsJSIID<nsITouchBarInput>;
  nsITouchBarUpdater: nsJSIID<nsITouchBarUpdater>;
  nsIMacPreferencesReader: nsJSIID<nsIMacPreferencesReader>;
  nsILocalFileMac: nsJSIID<nsILocalFileMac>;
}

}  // global

// Typedefs from xpidl.
type CSPDirective = nsIContentSecurityPolicy.CSPDirective;
type PRTime = i64;
type RequireTrustedTypesForDirectiveState = nsIContentSecurityPolicy.RequireTrustedTypesForDirectiveState;
type nsContentPolicyType = nsIContentPolicy.nsContentPolicyType;
type nsTaskbarProgressState = i32;

// XPCOM internal utility types.

/** XPCOM inout param is passed in as a js object with a value property. */
type InOutParam<T> = { value: T };

/** XPCOM out param is written to the passed in object's value property. */
type OutParam<T> = { value?: T };

/** Enable interfaces to inherit from enums: pick variants as optional. */
type Enums<enums> = Partial<Pick<enums, keyof enums>>;

/** Callable accepts either form of a [function] interface. */
type Callable<iface> = iface | Extract<iface[keyof iface], Function>

export {};
