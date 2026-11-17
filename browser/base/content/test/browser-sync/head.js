ChromeUtils.defineESModuleGetters(this, {
  SearchService: "moz-src:///toolkit/components/search/SearchService.sys.mjs",
  sinon: "resource://testing-common/Sinon.sys.mjs",
  UIState: "resource://services-sync/UIState.sys.mjs",
  WeaveService: "moz-src:///services/sync/Weave.sys.mjs",
});

function promiseSyncReady() {
  return WeaveService.whenLoaded();
}

function setupSendTabMocks({
  fxaDevices = null,
  state = UIState.STATUS_SIGNED_IN,
  isSendableURI = true,
}) {
  const sandbox = sinon.createSandbox();
  sandbox.stub(fxAccounts.device, "recentDeviceList").get(() => fxaDevices);
  sandbox.stub(UIState, "get").returns({
    status: state,
    syncEnabled: true,
  });
  if (isSendableURI) {
    sandbox.stub(BrowserUtils, "getShareableURL").returnsArg(0);
  } else {
    sandbox.stub(BrowserUtils, "getShareableURL").returns(null);
  }
  sandbox.stub(fxAccounts.device, "refreshDeviceList").resolves(true);
  sandbox.stub(fxAccounts.commands.sendTab, "send").resolves({ failed: [] });
  return sandbox;
}
