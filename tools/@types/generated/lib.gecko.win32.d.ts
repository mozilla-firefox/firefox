/**
 * NOTE: Do not modify this file by hand.
 * Content was generated from source XPCOM .idl files.
 * If you're updating some of the sources, see README for instructions.
 */

declare global {

// https://searchfox.org/firefox-main/source/browser/app/installation_dir_layout/nsIInstallationDirLayout.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIInstallationDirLayout) --> */
interface nsIInstallationDirLayout extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIInstallationDirLayout_isInstallationLayoutVersioned) --> */
  readonly isInstallationLayoutVersioned: boolean;
}

// https://searchfox.org/firefox-main/source/browser/components/shell/nsILimitedAccessFeature.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsILimitedAccessFeature) --> */
interface nsILimitedAccessFeature extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsILimitedAccessFeature_featureId) --> */
  readonly featureId: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsILimitedAccessFeature_token) --> */
  readonly token: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsILimitedAccessFeature_attestation) --> */
  readonly attestation: string;
  /** <!-- binding_to(idl, method, XPIDL_nsILimitedAccessFeature_unlock) --> */
  unlock(): boolean;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsILimitedAccessFeatureService) --> */
interface nsILimitedAccessFeatureService extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsILimitedAccessFeatureService_taskbarPinFeatureId) --> */
  readonly taskbarPinFeatureId: string;
  /** <!-- binding_to(idl, method, XPIDL_nsILimitedAccessFeatureService_generateLimitedAccessFeature) --> */
  generateLimitedAccessFeature(featureId: string): nsILimitedAccessFeature;
}

// https://searchfox.org/firefox-main/source/browser/components/shell/nsISecondaryTile.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsISecondaryTileListener) --> */
interface nsISecondaryTileListener extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsISecondaryTileListener_succeeded) --> */
  succeeded(accepted: boolean): void;
  /** <!-- binding_to(idl, method, XPIDL_nsISecondaryTileListener_failed) --> */
  failed(aHresult: i32): void;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsISecondaryTileService) --> */
interface nsISecondaryTileService extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsISecondaryTileService_requestCreateAndPin) --> */
  requestCreateAndPin(aTileId: string, aName: string, aIconPath: string, aArguments: string[], aListener: nsISecondaryTileListener): void;
  /** <!-- binding_to(idl, method, XPIDL_nsISecondaryTileService_requestDelete) --> */
  requestDelete(aTileId: string, aListener: nsISecondaryTileListener): void;
}

// https://searchfox.org/firefox-main/source/browser/components/shell/nsIWindowsShellService.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIInstallShortcutInfo) --> */
interface nsIInstallShortcutInfo extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIInstallShortcutInfo_path) --> */
  readonly path: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIInstallShortcutInfo_location) --> */
  readonly location: string;
}

}  // global

/** <!-- binding_to(idl, class, XPIDL_nsIWindowsShellService_LaunchOnLoginEnabledEnumerator) --> */
declare enum nsIWindowsShellService_LaunchOnLoginEnabledEnumerator {
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_LaunchOnLoginEnabledEnumerator_LAUNCH_ON_LOGIN_DISABLED_BY_SETTINGS) --> */
  LAUNCH_ON_LOGIN_DISABLED_BY_SETTINGS = 0,
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_LaunchOnLoginEnabledEnumerator_LAUNCH_ON_LOGIN_DISABLED) --> */
  LAUNCH_ON_LOGIN_DISABLED = 1,
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_LaunchOnLoginEnabledEnumerator_LAUNCH_ON_LOGIN_ENABLED) --> */
  LAUNCH_ON_LOGIN_ENABLED = 2,
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_LaunchOnLoginEnabledEnumerator_LAUNCH_ON_LOGIN_ENABLED_BY_POLICY) --> */
  LAUNCH_ON_LOGIN_ENABLED_BY_POLICY = 3,
}

/** <!-- binding_to(idl, class, XPIDL_nsIWindowsShellService_PinResult) --> */
declare enum nsIWindowsShellService_PinResult {
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_PinResult_PINNED) --> */
  PINNED = 0,
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_PinResult_REJECTED) --> */
  REJECTED = 1,
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_PinResult_UNKNOWN) --> */
  UNKNOWN = 2,
}

declare global {

namespace nsIWindowsShellService {
  type LaunchOnLoginEnabledEnumerator = nsIWindowsShellService_LaunchOnLoginEnabledEnumerator;
  type PinResult = nsIWindowsShellService_PinResult;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWindowsShellService) --> */
interface nsIWindowsShellService extends nsIShellService, Enums<typeof nsIWindowsShellService_LaunchOnLoginEnabledEnumerator & typeof nsIWindowsShellService_PinResult> {
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_OPEN_WITH_SUPPRESS_OPEN) --> */
  readonly OPEN_WITH_SUPPRESS_OPEN?: 4;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_OPEN_WITH_PROTOCOL_MESSAGING) --> */
  readonly OPEN_WITH_PROTOCOL_MESSAGING?: 8;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_OPEN_WITH_OPEN_ONCE) --> */
  readonly OPEN_WITH_OPEN_ONCE?: 64;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_OPEN_WITH_SET_HANDLER) --> */
  readonly OPEN_WITH_SET_HANDLER?: 128;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsShellService_OPEN_WITH_SET_HANDLER_WIN10) --> */
  readonly OPEN_WITH_SET_HANDLER_WIN10?: 8192;

  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_createShortcut) --> */
  createShortcut(aBinary: nsIFile, aArguments: string[], aDescription: string, aIconFile: nsIFile, aIconIndex: u16, aAppUserModelId: string, aShortcutFolder: string, aShortcutRelativePath: string): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_deleteShortcut) --> */
  deleteShortcut(aShortcutFolder: string, aShortcutRelativePath: string): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_getLaunchOnLoginShortcuts) --> */
  getLaunchOnLoginShortcuts(): string[];
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_pinCurrentAppToStartMenu) --> */
  pinCurrentAppToStartMenu(): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_isCurrentAppPinnedToStartMenu) --> */
  isCurrentAppPinnedToStartMenu(): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_enableLaunchOnLoginMSIX) --> */
  enableLaunchOnLoginMSIX(aTaskId: string): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_disableLaunchOnLoginMSIX) --> */
  disableLaunchOnLoginMSIX(aTaskId: string): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_getLaunchOnLoginEnabledMSIX) --> */
  getLaunchOnLoginEnabledMSIX(aTaskId: string): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_pinCurrentAppToTaskbar) --> */
  pinCurrentAppToTaskbar(aPrivateBrowsing: boolean, aFireAndForget?: boolean, aPromptOnly?: boolean): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_canPinToTaskbar) --> */
  canPinToTaskbar(): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_isCurrentAppPinnedToTaskbar) --> */
  isCurrentAppPinnedToTaskbar(aumid: string): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_pinShortcutToTaskbar) --> */
  pinShortcutToTaskbar(aAppUserModelId: string, aShortcutFolder: string, aShortcutRelativePath: string): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_unpinShortcutFromTaskbar) --> */
  unpinShortcutFromTaskbar(aShortcutFolder: string, aShortcutRelativePath: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_launchSetDefaultAppPicker) --> */
  launchSetDefaultAppPicker(aTarget: string, aFlags: i32): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_launchModernSettingsDialogDefaultApps) --> */
  launchModernSettingsDialogDefaultApps(): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_classifyShortcut) --> */
  classifyShortcut(aPath: string): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_hasPinnableShortcut) --> */
  hasPinnableShortcut(aAUMID: string, aPrivateBrowsing: boolean): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_canSetDefaultBrowserUserChoice) --> */
  canSetDefaultBrowserUserChoice(): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_checkAllProgIDsExist) --> */
  checkAllProgIDsExist(): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_checkBrowserUserChoiceHashes) --> */
  checkBrowserUserChoiceHashes(): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_isUserChoiceProtectionDriverRunning) --> */
  isUserChoiceProtectionDriverRunning(): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_canRenameUserChoiceAssociationKey) --> */
  canRenameUserChoiceAssociationKey(aAssociation: string): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_checkCurrentProcessAUMIDForTesting) --> */
  checkCurrentProcessAUMIDForTesting(): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_isDefaultHandlerFor) --> */
  isDefaultHandlerFor(aFileExtensionOrProtocol: string): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_queryCurrentDefaultHandlerFor) --> */
  queryCurrentDefaultHandlerFor(aFileExtensionOrProtocol: string): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_setShortcutsIcon) --> */
  setShortcutsIcon(aShortcutPaths: string[], aIconPath: string, aIconResourceId: u16): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsShellService_enumerateInstallShortcuts) --> */
  enumerateInstallShortcuts(aAppUserModelId: string): Promise<any>;
}

// https://searchfox.org/firefox-main/source/netwerk/socket/nsINamedPipeService.idl

// https://searchfox.org/firefox-main/source/toolkit/components/aboutthirdparty/nsIAboutThirdParty.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIInstalledApplication) --> */
interface nsIInstalledApplication extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIInstalledApplication_name) --> */
  readonly name: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIInstalledApplication_publisher) --> */
  readonly publisher: string;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsIAboutThirdParty) --> */
interface nsIAboutThirdParty extends nsISupports {
  /** <!-- binding_to(idl, const, XPIDL_nsIAboutThirdParty_ModuleType_Unknown) --> */
  readonly ModuleType_Unknown?: 1;
  /** <!-- binding_to(idl, const, XPIDL_nsIAboutThirdParty_ModuleType_IME) --> */
  readonly ModuleType_IME?: 2;
  /** <!-- binding_to(idl, const, XPIDL_nsIAboutThirdParty_ModuleType_ShellExtension) --> */
  readonly ModuleType_ShellExtension?: 4;
  /** <!-- binding_to(idl, const, XPIDL_nsIAboutThirdParty_ModuleType_BlockedByUser) --> */
  readonly ModuleType_BlockedByUser?: 8;
  /** <!-- binding_to(idl, const, XPIDL_nsIAboutThirdParty_ModuleType_BlockedByUserAtLaunch) --> */
  readonly ModuleType_BlockedByUserAtLaunch?: 16;

  /** <!-- binding_to(idl, method, XPIDL_nsIAboutThirdParty_lookupModuleType) --> */
  lookupModuleType(aLeafName: string): u32;
  /** <!-- binding_to(idl, method, XPIDL_nsIAboutThirdParty_lookupApplication) --> */
  lookupApplication(aModulePath: string): nsIInstalledApplication;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIAboutThirdParty_isDynamicBlocklistAvailable) --> */
  readonly isDynamicBlocklistAvailable: boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIAboutThirdParty_isDynamicBlocklistDisabled) --> */
  readonly isDynamicBlocklistDisabled: boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIAboutThirdParty_updateBlocklist) --> */
  updateBlocklist(aLeafName: string, aNewBlockStatus: boolean): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIAboutThirdParty_collectSystemInfo) --> */
  collectSystemInfo(): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIAboutThirdParty_loadModuleForTesting) --> */
  loadModuleForTesting(aModulePath: string): void;
}

// https://searchfox.org/firefox-main/source/toolkit/components/aboutwindowsmessages/nsIAboutWindowsMessages.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIAboutWindowsMessages) --> */
interface nsIAboutWindowsMessages extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIAboutWindowsMessages_getMessages) --> */
  getMessages(currentWindow: mozIDOMWindowProxy, messages: OutParam<string[][]>, windowTitles: OutParam<string[]>): void;
}

// https://searchfox.org/firefox-main/source/toolkit/components/alerts/nsIWindowsAlertsService.idl

}  // global

/** <!-- binding_to(idl, class, XPIDL_nsIWindowsAlertNotification_ImagePlacement) --> */
declare enum nsIWindowsAlertNotification_ImagePlacement {
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsAlertNotification_ImagePlacement_eInline) --> */
  eInline = 0,
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsAlertNotification_ImagePlacement_eHero) --> */
  eHero = 1,
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsAlertNotification_ImagePlacement_eIcon) --> */
  eIcon = 2,
}

declare global {

namespace nsIWindowsAlertNotification {
  type ImagePlacement = nsIWindowsAlertNotification_ImagePlacement;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWindowsAlertNotification) --> */
interface nsIWindowsAlertNotification extends nsIAlertNotification, Enums<typeof nsIWindowsAlertNotification_ImagePlacement> {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsAlertNotification_imagePlacement) --> */
  imagePlacement: nsIWindowsAlertNotification.ImagePlacement;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsAlertNotification_imagePathUnchecked) --> */
  imagePathUnchecked: string;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWindowsAlertsService) --> */
interface nsIWindowsAlertsService extends nsIAlertsService {
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsAlertsService_handleWindowsTag) --> */
  handleWindowsTag(aWindowsTag: string): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsAlertsService_getXmlStringForWindowsAlert) --> */
  getXmlStringForWindowsAlert(aAlert: nsIAlertNotification, aWindowsTag?: string): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsAlertsService_removeAllNotificationsForInstall) --> */
  removeAllNotificationsForInstall(): void;
}

// https://searchfox.org/firefox-main/source/toolkit/components/taskscheduler/nsIWinBackgroundTaskRegistrar.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWinBackgroundTaskRegistrar) --> */
interface nsIWinBackgroundTaskRegistrar extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIWinBackgroundTaskRegistrar_registerTask) --> */
  registerTask(aId: string, aIntervalMinutes: u32): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinBackgroundTaskRegistrar_deleteTask) --> */
  deleteTask(aId: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinBackgroundTaskRegistrar_deleteAllTasks) --> */
  deleteAllTasks(): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinBackgroundTaskRegistrar_taskExists) --> */
  taskExists(aId: string): boolean;
}

// https://searchfox.org/firefox-main/source/toolkit/components/taskscheduler/nsIWinTaskSchedulerService.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWinTaskSchedulerService) --> */
interface nsIWinTaskSchedulerService extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskSchedulerService_registerTask) --> */
  registerTask(aFolderName: string, aTaskName: string, aDefinitionXML: string, aUpdateExisting?: boolean): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskSchedulerService_validateTaskDefinition) --> */
  validateTaskDefinition(aDefinitionXML: string): i32;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskSchedulerService_getTaskXML) --> */
  getTaskXML(aFolderName: string, aTaskName: string): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskSchedulerService_getCurrentUserSid) --> */
  getCurrentUserSid(): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskSchedulerService_deleteTask) --> */
  deleteTask(aFolderName: string, aTaskName: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskSchedulerService_getFolderTasks) --> */
  getFolderTasks(aFolderName: string): string[];
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskSchedulerService_createFolder) --> */
  createFolder(aParentFolderName: string, aSubFolderName: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskSchedulerService_deleteFolder) --> */
  deleteFolder(aParentFolderName: string, aSubFolderName: string): void;
}

// https://searchfox.org/firefox-main/source/toolkit/mozapps/defaultagent/nsIDefaultAgent.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIDefaultAgent) --> */
interface nsIDefaultAgent extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_registerTask) --> */
  registerTask(aUniqueToken: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_updateTask) --> */
  updateTask(aUniqueToken: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_unregisterTask) --> */
  unregisterTask(aUniqueToken: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_uninstall) --> */
  uninstall(aUniqueToken: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_secondsSinceLastAppRun) --> */
  secondsSinceLastAppRun(): i64;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_getDefaultBrowser) --> */
  getDefaultBrowser(): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_getReplacePreviousDefaultBrowser) --> */
  getReplacePreviousDefaultBrowser(aCurrentBrowser: string): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_getDefaultPdfHandler) --> */
  getDefaultPdfHandler(): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_sendPing) --> */
  sendPing(aCurrentBrowser: string, aPreviousBrowser: string, aPdfHandler: string, aNotificationShown: string, aNotificationAction: string, daysSinceLastAppLaunch: u32, aIsTaskbarPinned: string, aHasWinPackageId: boolean, aWinPackageFamilyName: string, aInstallerType: string, aInstallerFromMsi: boolean, aInstallerSilent: boolean): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_setDefaultBrowserUserChoice) --> */
  setDefaultBrowserUserChoice(aAumid: string, aExtraFileExtensions: string[]): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_setDefaultBrowserUserChoiceAsync) --> */
  setDefaultBrowserUserChoiceAsync(aAumid: string, aExtraFileExtensions: string[]): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_setDefaultExtensionHandlersUserChoice) --> */
  setDefaultExtensionHandlersUserChoice(aAumid: string, aFileExtensions: string[]): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIDefaultAgent_agentDisabled) --> */
  agentDisabled(): boolean;
}

// https://searchfox.org/firefox-main/source/toolkit/mozapps/defaultagent/nsIWindowsMutex.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWindowsMutex) --> */
interface nsIWindowsMutex extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsMutex_tryLock) --> */
  tryLock(): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsMutex_isLocked) --> */
  isLocked(): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsMutex_unlock) --> */
  unlock(): void;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWindowsMutexFactory) --> */
interface nsIWindowsMutexFactory extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsMutexFactory_createMutex) --> */
  createMutex(aName: string): nsIWindowsMutex;
}

// https://searchfox.org/firefox-main/source/toolkit/system/windowsPackageManager/nsIWindowsPackageManager.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWindowsPackageManager) --> */
interface nsIWindowsPackageManager extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsPackageManager_findUserInstalledPackages) --> */
  findUserInstalledPackages(prefix: string[]): string[];
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsPackageManager_getInstalledDate) --> */
  getInstalledDate(): u64;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsPackageManager_campaignId) --> */
  campaignId(): Promise<any>;
}

// https://searchfox.org/firefox-main/source/toolkit/xre/nsIWinAppHelper.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWinAppHelper) --> */
interface nsIWinAppHelper extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWinAppHelper_userCanElevate) --> */
  readonly userCanElevate: boolean;
}

// https://searchfox.org/firefox-main/source/widget/nsIJumpListBuilder.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIJumpListBuilder) --> */
interface nsIJumpListBuilder extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIJumpListBuilder_obtainAndCacheFavicon) --> */
  obtainAndCacheFavicon(faviconURL: nsIURI): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIJumpListBuilder_obtainAndCacheFaviconAsync) --> */
  obtainAndCacheFaviconAsync(faviconURL: nsIURI): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIJumpListBuilder_isAvailable) --> */
  isAvailable(): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIJumpListBuilder_checkForRemovals) --> */
  checkForRemovals(): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIJumpListBuilder_populateJumpList) --> */
  populateJumpList(aTaskDescriptions: any, aCustomTitle: string, aCustomDescriptions: any): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIJumpListBuilder_clearRecentsList) --> */
  clearRecentsList(): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIJumpListBuilder_clearJumpList) --> */
  clearJumpList(): Promise<any>;
}

// https://searchfox.org/firefox-main/source/widget/nsIPrintSettingsWin.idl

// https://searchfox.org/firefox-main/source/widget/nsITaskbarOverlayIconController.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsITaskbarOverlayIconController) --> */
interface nsITaskbarOverlayIconController extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsITaskbarOverlayIconController_setOverlayIcon) --> */
  setOverlayIcon(statusIcon: imgIContainer, statusDescription: string, paintContext?: nsISVGPaintContext): void;
}

// https://searchfox.org/firefox-main/source/widget/nsITaskbarPreview.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsITaskbarPreview) --> */
interface nsITaskbarPreview extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreview_controller) --> */
  controller: nsITaskbarPreviewController;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreview_tooltip) --> */
  tooltip: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreview_visible) --> */
  visible: boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreview_active) --> */
  active: boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsITaskbarPreview_invalidate) --> */
  invalidate(): void;
}

// https://searchfox.org/firefox-main/source/widget/nsITaskbarPreviewButton.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsITaskbarPreviewButton) --> */
interface nsITaskbarPreviewButton extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreviewButton_tooltip) --> */
  tooltip: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreviewButton_dismissOnClick) --> */
  dismissOnClick: boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreviewButton_hasBorder) --> */
  hasBorder: boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreviewButton_disabled) --> */
  disabled: boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreviewButton_image) --> */
  image: imgIContainer;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreviewButton_visible) --> */
  visible: boolean;
}

// https://searchfox.org/firefox-main/source/widget/nsITaskbarPreviewController.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsITaskbarPreviewCallback) --> */
type nsITaskbarPreviewCallback = Callable<{
  /** <!-- binding_to(idl, method, XPIDL_nsITaskbarPreviewCallback_done) --> */
  done(aCanvas: nsISupports, aDrawBorder: boolean): void;
}>

/** <!-- binding_to(idl, interface_name, XPIDL_nsITaskbarPreviewController) --> */
interface nsITaskbarPreviewController extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreviewController_width) --> */
  readonly width: u32;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreviewController_height) --> */
  readonly height: u32;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarPreviewController_thumbnailAspectRatio) --> */
  readonly thumbnailAspectRatio: float;
  /** <!-- binding_to(idl, method, XPIDL_nsITaskbarPreviewController_requestPreview) --> */
  requestPreview(aCallback: nsITaskbarPreviewCallback): void;
  /** <!-- binding_to(idl, method, XPIDL_nsITaskbarPreviewController_requestThumbnail) --> */
  requestThumbnail(aCallback: nsITaskbarPreviewCallback, width: u32, height: u32): void;
  /** <!-- binding_to(idl, method, XPIDL_nsITaskbarPreviewController_onClose) --> */
  onClose(): void;
  /** <!-- binding_to(idl, method, XPIDL_nsITaskbarPreviewController_onActivate) --> */
  onActivate(): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsITaskbarPreviewController_onClick) --> */
  onClick(button: nsITaskbarPreviewButton): void;
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

// https://searchfox.org/firefox-main/source/widget/nsITaskbarTabPreview.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsITaskbarTabPreview) --> */
interface nsITaskbarTabPreview extends nsITaskbarPreview {
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarTabPreview_title) --> */
  title: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarTabPreview_icon) --> */
  icon: imgIContainer;
  /** <!-- binding_to(idl, method, XPIDL_nsITaskbarTabPreview_move) --> */
  move(aNext: nsITaskbarTabPreview): void;
}

// https://searchfox.org/firefox-main/source/widget/nsITaskbarWindowPreview.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsITaskbarWindowPreview) --> */
interface nsITaskbarWindowPreview extends nsITaskbarPreview {
  /** <!-- binding_to(idl, const, XPIDL_nsITaskbarWindowPreview_NUM_TOOLBAR_BUTTONS) --> */
  readonly NUM_TOOLBAR_BUTTONS?: 7;

  /** <!-- binding_to(idl, method, XPIDL_nsITaskbarWindowPreview_getButton) --> */
  getButton(index: u32): nsITaskbarPreviewButton;
  /** <!-- binding_to(idl, attribute, XPIDL_nsITaskbarWindowPreview_enableCustomDrawing) --> */
  enableCustomDrawing: boolean;
}

// https://searchfox.org/firefox-main/source/widget/nsIWinTaskbar.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWinTaskbar) --> */
interface nsIWinTaskbar extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWinTaskbar_available) --> */
  readonly available: boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWinTaskbar_defaultGroupId) --> */
  readonly defaultGroupId: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWinTaskbar_defaultPrivateGroupId) --> */
  readonly defaultPrivateGroupId: string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskbar_createTaskbarTabPreview) --> */
  createTaskbarTabPreview(shell: nsIDocShell, controller: nsITaskbarPreviewController): nsITaskbarTabPreview;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskbar_getTaskbarWindowPreview) --> */
  getTaskbarWindowPreview(shell: nsIDocShell): nsITaskbarWindowPreview;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskbar_getTaskbarProgress) --> */
  getTaskbarProgress(shell: nsIDocShell): nsITaskbarProgress;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskbar_getOverlayIconController) --> */
  getOverlayIconController(shell: nsIDocShell): nsITaskbarOverlayIconController;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskbar_createJumpListBuilder) --> */
  createJumpListBuilder(aPrivateBrowsing: boolean): nsIJumpListBuilder;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskbar_getGroupIdForWindow) --> */
  getGroupIdForWindow(aParent: mozIDOMWindow): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskbar_setGroupIdForWindow) --> */
  setGroupIdForWindow(aParent: mozIDOMWindow, aIdentifier: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskbar_setAllWindowIcons) --> */
  setAllWindowIcons(aIconResourceId: u16): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWinTaskbar_refreshTaskbarButtons) --> */
  refreshTaskbarButtons(): void;
}

// https://searchfox.org/firefox-main/source/widget/nsIWindowsUIUtils.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWindowsUIUtils) --> */
interface nsIWindowsUIUtils extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsUIUtils_systemSmallIconSize) --> */
  readonly systemSmallIconSize: i32;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsUIUtils_systemLargeIconSize) --> */
  readonly systemLargeIconSize: i32;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsUIUtils_setWindowIcon) --> */
  setWindowIcon(aWindow: mozIDOMWindowProxy, aSmallIcon: imgIContainer, aLargeIcon: imgIContainer): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsUIUtils_setWindowIconFromExe) --> */
  setWindowIconFromExe(aWindow: mozIDOMWindowProxy, aExe: string, aIndex: u16): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsUIUtils_setWindowIconNoData) --> */
  setWindowIconNoData(aWindow: mozIDOMWindowProxy): void;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsUIUtils_inWin10TabletMode) --> */
  readonly inWin10TabletMode: boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsUIUtils_inWin11TabletMode) --> */
  readonly inWin11TabletMode: boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsUIUtils_isTabletCapable) --> */
  readonly isTabletCapable: boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsUIUtils_shareUrl) --> */
  shareUrl(urlToShare: string, shareTitle: string): void;
}

// https://searchfox.org/firefox-main/source/widget/windows/nsIAlertsServiceRust.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIAlertsServiceRust) --> */
interface nsIAlertsServiceRust extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIAlertsServiceRust_getHistory) --> */
  getHistory(aAumid: string): string[];
}

// https://searchfox.org/firefox-main/source/widget/windows/nsIWindowsTestDebug.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWindowsDebugProcessData) --> */
interface nsIWindowsDebugProcessData extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsDebugProcessData_name) --> */
  readonly name: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsDebugProcessData_executablePath) --> */
  readonly executablePath: string;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsDebugProcessData_pid) --> */
  readonly pid: u32;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWindowsTestDebug) --> */
interface nsIWindowsTestDebug extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsTestDebug_processesThatOpenedFile) --> */
  processesThatOpenedFile(aFilename: string): nsIWindowsDebugProcessData[];
}

// https://searchfox.org/firefox-main/source/xpcom/ds/nsIWindowsRegKey.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIWindowsRegKey) --> */
interface nsIWindowsRegKey extends nsISupports {
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ROOT_KEY_CLASSES_ROOT) --> */
  readonly ROOT_KEY_CLASSES_ROOT?: 2147483648;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ROOT_KEY_CURRENT_USER) --> */
  readonly ROOT_KEY_CURRENT_USER?: 2147483649;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ROOT_KEY_LOCAL_MACHINE) --> */
  readonly ROOT_KEY_LOCAL_MACHINE?: 2147483650;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ACCESS_BASIC) --> */
  readonly ACCESS_BASIC?: 131072;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ACCESS_QUERY_VALUE) --> */
  readonly ACCESS_QUERY_VALUE?: 1;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ACCESS_SET_VALUE) --> */
  readonly ACCESS_SET_VALUE?: 2;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ACCESS_CREATE_SUB_KEY) --> */
  readonly ACCESS_CREATE_SUB_KEY?: 4;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ACCESS_ENUMERATE_SUB_KEYS) --> */
  readonly ACCESS_ENUMERATE_SUB_KEYS?: 8;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ACCESS_NOTIFY) --> */
  readonly ACCESS_NOTIFY?: 16;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ACCESS_READ) --> */
  readonly ACCESS_READ?: 131097;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ACCESS_WRITE) --> */
  readonly ACCESS_WRITE?: 131078;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_ACCESS_ALL) --> */
  readonly ACCESS_ALL?: 131103;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_WOW64_32) --> */
  readonly WOW64_32?: 512;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_WOW64_64) --> */
  readonly WOW64_64?: 256;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_TYPE_NONE) --> */
  readonly TYPE_NONE?: 0;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_TYPE_STRING) --> */
  readonly TYPE_STRING?: 1;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_TYPE_BINARY) --> */
  readonly TYPE_BINARY?: 3;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_TYPE_INT) --> */
  readonly TYPE_INT?: 4;
  /** <!-- binding_to(idl, const, XPIDL_nsIWindowsRegKey_TYPE_INT64) --> */
  readonly TYPE_INT64?: 11;

  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_close) --> */
  close(): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_open) --> */
  open(rootKey: u32, relPath: string, mode: u32): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_create) --> */
  create(rootKey: u32, relPath: string, mode: u32): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_openChild) --> */
  openChild(relPath: string, mode: u32): nsIWindowsRegKey;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_createChild) --> */
  createChild(relPath: string, mode: u32): nsIWindowsRegKey;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsRegKey_childCount) --> */
  readonly childCount: u32;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_getChildName) --> */
  getChildName(index: u32): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_hasChild) --> */
  hasChild(name: string): boolean;
  /** <!-- binding_to(idl, attribute, XPIDL_nsIWindowsRegKey_valueCount) --> */
  readonly valueCount: u32;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_getValueName) --> */
  getValueName(index: u32): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_hasValue) --> */
  hasValue(name: string): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_removeChild) --> */
  removeChild(relPath: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_removeValue) --> */
  removeValue(name: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_getValueType) --> */
  getValueType(name: string): u32;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_readStringValue) --> */
  readStringValue(name: string): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_readIntValue) --> */
  readIntValue(name: string): u32;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_readInt64Value) --> */
  readInt64Value(name: string): u64;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_readBinaryValue) --> */
  readBinaryValue(name: string): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_writeStringValue) --> */
  writeStringValue(name: string, data: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_writeIntValue) --> */
  writeIntValue(name: string, data: u32): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_writeInt64Value) --> */
  writeInt64Value(name: string, data: u64): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIWindowsRegKey_writeBinaryValue) --> */
  writeBinaryValue(name: string, data: string): void;
}

interface nsIXPCComponents_Interfaces {
  nsIInstallationDirLayout: nsJSIID<nsIInstallationDirLayout>;
  nsILimitedAccessFeature: nsJSIID<nsILimitedAccessFeature>;
  nsILimitedAccessFeatureService: nsJSIID<nsILimitedAccessFeatureService>;
  nsISecondaryTileListener: nsJSIID<nsISecondaryTileListener>;
  nsISecondaryTileService: nsJSIID<nsISecondaryTileService>;
  nsIInstallShortcutInfo: nsJSIID<nsIInstallShortcutInfo>;
  nsIWindowsShellService: nsJSIID<nsIWindowsShellService, typeof nsIWindowsShellService_LaunchOnLoginEnabledEnumerator & typeof nsIWindowsShellService_PinResult>;
  nsIInstalledApplication: nsJSIID<nsIInstalledApplication>;
  nsIAboutThirdParty: nsJSIID<nsIAboutThirdParty>;
  nsIAboutWindowsMessages: nsJSIID<nsIAboutWindowsMessages>;
  nsIWindowsAlertNotification: nsJSIID<nsIWindowsAlertNotification, typeof nsIWindowsAlertNotification_ImagePlacement>;
  nsIWindowsAlertsService: nsJSIID<nsIWindowsAlertsService>;
  nsIWinBackgroundTaskRegistrar: nsJSIID<nsIWinBackgroundTaskRegistrar>;
  nsIWinTaskSchedulerService: nsJSIID<nsIWinTaskSchedulerService>;
  nsIDefaultAgent: nsJSIID<nsIDefaultAgent>;
  nsIWindowsMutex: nsJSIID<nsIWindowsMutex>;
  nsIWindowsMutexFactory: nsJSIID<nsIWindowsMutexFactory>;
  nsIWindowsPackageManager: nsJSIID<nsIWindowsPackageManager>;
  nsIWinAppHelper: nsJSIID<nsIWinAppHelper>;
  nsIJumpListBuilder: nsJSIID<nsIJumpListBuilder>;
  nsITaskbarOverlayIconController: nsJSIID<nsITaskbarOverlayIconController>;
  nsITaskbarPreview: nsJSIID<nsITaskbarPreview>;
  nsITaskbarPreviewButton: nsJSIID<nsITaskbarPreviewButton>;
  nsITaskbarPreviewCallback: nsJSIID<nsITaskbarPreviewCallback>;
  nsITaskbarPreviewController: nsJSIID<nsITaskbarPreviewController>;
  nsITaskbarProgress: nsJSIID<nsITaskbarProgress>;
  nsITaskbarTabPreview: nsJSIID<nsITaskbarTabPreview>;
  nsITaskbarWindowPreview: nsJSIID<nsITaskbarWindowPreview>;
  nsIWinTaskbar: nsJSIID<nsIWinTaskbar>;
  nsIWindowsUIUtils: nsJSIID<nsIWindowsUIUtils>;
  nsIAlertsServiceRust: nsJSIID<nsIAlertsServiceRust>;
  nsIWindowsDebugProcessData: nsJSIID<nsIWindowsDebugProcessData>;
  nsIWindowsTestDebug: nsJSIID<nsIWindowsTestDebug>;
  nsIWindowsRegKey: nsJSIID<nsIWindowsRegKey>;
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
