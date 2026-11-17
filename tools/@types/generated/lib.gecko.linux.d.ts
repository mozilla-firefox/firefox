/**
 * NOTE: Do not modify this file by hand.
 * Content was generated from source XPCOM .idl files.
 * If you're updating some of the sources, see README for instructions.
 */

declare global {

// https://searchfox.org/firefox-main/source/browser/components/shell/nsIGNOMEShellService.idl

}  // global

/** <!-- binding_to(idl, class, XPIDL_nsIGNOMEShellService_DesktopEntryStatus) --> */
declare enum nsIGNOMEShellService_DesktopEntryStatus {
  /** <!-- binding_to(idl, const, XPIDL_nsIGNOMEShellService_DesktopEntryStatus_DESKTOP_ENTRY_ABSENT) --> */
  DESKTOP_ENTRY_ABSENT = 0,
  /** <!-- binding_to(idl, const, XPIDL_nsIGNOMEShellService_DesktopEntryStatus_DESKTOP_ENTRY_INVISIBLE) --> */
  DESKTOP_ENTRY_INVISIBLE = 1,
  /** <!-- binding_to(idl, const, XPIDL_nsIGNOMEShellService_DesktopEntryStatus_DESKTOP_ENTRY_VISIBLE) --> */
  DESKTOP_ENTRY_VISIBLE = 2,
}

declare global {

namespace nsIGNOMEShellService {
  type DesktopEntryStatus = nsIGNOMEShellService_DesktopEntryStatus;
}

/** <!-- binding_to(idl, interface_name, XPIDL_nsIGNOMEShellService) --> */
interface nsIGNOMEShellService extends nsIShellService, Enums<typeof nsIGNOMEShellService_DesktopEntryStatus> {
  /** <!-- binding_to(idl, attribute, XPIDL_nsIGNOMEShellService_canSetDesktopBackground) --> */
  readonly canSetDesktopBackground: boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIGNOMEShellService_isDefaultForScheme) --> */
  isDefaultForScheme(aScheme: string): boolean;
  /** <!-- binding_to(idl, method, XPIDL_nsIGNOMEShellService_getGSettingsString) --> */
  getGSettingsString(aScheme: string, aKey: string): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIGNOMEShellService_setGSettingsString) --> */
  setGSettingsString(aScheme: string, aKey: string, aValue: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIGNOMEShellService_getArgv0) --> */
  getArgv0(): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIGNOMEShellService_getGlibPrgname) --> */
  getGlibPrgname(): string;
  /** <!-- binding_to(idl, method, XPIDL_nsIGNOMEShellService_getDesktopEntryStatus) --> */
  getDesktopEntryStatus(aEntryId: string): nsIGNOMEShellService.DesktopEntryStatus;
  /** <!-- binding_to(idl, method, XPIDL_nsIGNOMEShellService_requestInstallDynamicLauncher) --> */
  requestInstallDynamicLauncher(aEntryId: string, aDesktopEntry: nsIINIParserWriter, aWindow: mozIDOMWindowProxy): Promise<any>;
  /** <!-- binding_to(idl, method, XPIDL_nsIGNOMEShellService_requestUninstallDynamicLauncher) --> */
  requestUninstallDynamicLauncher(aEntryId: string): Promise<any>;
}

// https://searchfox.org/firefox-main/source/browser/components/shell/nsIOpenTabsProvider.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIOpenTabsProvider) --> */
interface nsIOpenTabsProvider extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIOpenTabsProvider_getOpenTabs) --> */
  getOpenTabs(): string[];
  /** <!-- binding_to(idl, method, XPIDL_nsIOpenTabsProvider_switchToOpenTab) --> */
  switchToOpenTab(url: string): void;
}

// https://searchfox.org/firefox-main/source/security/sandbox/linux/interfaces/mozISandboxReporter.idl

/** <!-- binding_to(idl, interface_name, XPIDL_mozISandboxReport) --> */
interface mozISandboxReport extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_mozISandboxReport_msecAgo) --> */
  readonly msecAgo: u64;
  /** <!-- binding_to(idl, attribute, XPIDL_mozISandboxReport_pid) --> */
  readonly pid: i32;
  /** <!-- binding_to(idl, attribute, XPIDL_mozISandboxReport_tid) --> */
  readonly tid: i32;
  /** <!-- binding_to(idl, attribute, XPIDL_mozISandboxReport_procType) --> */
  readonly procType: string;
  /** <!-- binding_to(idl, attribute, XPIDL_mozISandboxReport_syscall) --> */
  readonly syscall: u32;
  /** <!-- binding_to(idl, attribute, XPIDL_mozISandboxReport_numArgs) --> */
  readonly numArgs: u32;
  /** <!-- binding_to(idl, method, XPIDL_mozISandboxReport_getArg) --> */
  getArg(aIndex: u32): string;
}

/** <!-- binding_to(idl, interface_name, XPIDL_mozISandboxReportArray) --> */
interface mozISandboxReportArray extends nsISupports {
  /** <!-- binding_to(idl, attribute, XPIDL_mozISandboxReportArray_begin) --> */
  readonly begin: u64;
  /** <!-- binding_to(idl, attribute, XPIDL_mozISandboxReportArray_end) --> */
  readonly end: u64;
  /** <!-- binding_to(idl, method, XPIDL_mozISandboxReportArray_getElement) --> */
  getElement(aIndex: u64): mozISandboxReport;
}

/** <!-- binding_to(idl, interface_name, XPIDL_mozISandboxReporter) --> */
interface mozISandboxReporter extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_mozISandboxReporter_snapshot) --> */
  snapshot(): mozISandboxReportArray;
}

// https://searchfox.org/firefox-main/source/widget/nsIApplicationChooser.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIApplicationChooserFinishedCallback) --> */
type nsIApplicationChooserFinishedCallback = Callable<{
  /** <!-- binding_to(idl, method, XPIDL_nsIApplicationChooserFinishedCallback_done) --> */
  done(handlerApp: nsIHandlerApp): void;
}>

/** <!-- binding_to(idl, interface_name, XPIDL_nsIApplicationChooser) --> */
interface nsIApplicationChooser extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIApplicationChooser_init) --> */
  init(parent: mozIDOMWindowProxy, title: string): void;
  /** <!-- binding_to(idl, method, XPIDL_nsIApplicationChooser_open) --> */
  open(contentType: string, applicationChooserFinishedCallback: nsIApplicationChooserFinishedCallback): void;
}

// https://searchfox.org/firefox-main/source/widget/nsIGtkTaskbarProgress.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIGtkTaskbarProgress) --> */
interface nsIGtkTaskbarProgress extends nsITaskbarProgress {
  /** <!-- binding_to(idl, method, XPIDL_nsIGtkTaskbarProgress_setPrimaryWindow) --> */
  setPrimaryWindow(aWindow: mozIDOMWindowProxy): void;
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

// https://searchfox.org/firefox-main/source/xpcom/base/nsIAvailableMemoryWatcherTestingLinux.idl

/** <!-- binding_to(idl, interface_name, XPIDL_nsIAvailableMemoryWatcherTestingLinux) --> */
interface nsIAvailableMemoryWatcherTestingLinux extends nsISupports {
  /** <!-- binding_to(idl, method, XPIDL_nsIAvailableMemoryWatcherTestingLinux_setPSIPathForTesting) --> */
  setPSIPathForTesting(aPSIPath: string): void;
}

interface nsIXPCComponents_Interfaces {
  nsIGNOMEShellService: nsJSIID<nsIGNOMEShellService, typeof nsIGNOMEShellService_DesktopEntryStatus>;
  nsIOpenTabsProvider: nsJSIID<nsIOpenTabsProvider>;
  mozISandboxReport: nsJSIID<mozISandboxReport>;
  mozISandboxReportArray: nsJSIID<mozISandboxReportArray>;
  mozISandboxReporter: nsJSIID<mozISandboxReporter>;
  nsIApplicationChooserFinishedCallback: nsJSIID<nsIApplicationChooserFinishedCallback>;
  nsIApplicationChooser: nsJSIID<nsIApplicationChooser>;
  nsIGtkTaskbarProgress: nsJSIID<nsIGtkTaskbarProgress>;
  nsITaskbarProgress: nsJSIID<nsITaskbarProgress>;
  nsIAvailableMemoryWatcherTestingLinux: nsJSIID<nsIAvailableMemoryWatcherTestingLinux>;
}

}  // global

// Typedefs from xpidl.
type CSPDirective = nsIContentSecurityPolicy.CSPDirective;
type PRTime = i64;
type RequireTrustedTypesForDirectiveState = nsIContentSecurityPolicy.RequireTrustedTypesForDirectiveState;
type nsContentPolicyType = nsIContentPolicy.nsContentPolicyType;
type nsHandlerInfoAction = i32;
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
