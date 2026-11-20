/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

let { nsUnknownContentTypeDialog } = ChromeUtils.importESModule(
  "resource://gre/modules/HelperAppDlg.sys.mjs"
);
let { sinon } = ChromeUtils.importESModule(
  "resource://testing-common/Sinon.sys.mjs"
);

const HELD_DOWNLOAD_URL =
  getRootDirectory(gTestPath).replace(
    "chrome://mochitests/content",
    "https://example.com"
  ) + "held_download.sjs";

let MockFilePicker = SpecialPowers.MockFilePicker;

add_setup(async function () {
  await SpecialPowers.pushPrefEnv({
    set: [
      ["browser.download.useDownloadDir", false],
      ["browser.download.lastDir", ""],
      ["browser.download.lastDir.savePerSite", true],
    ],
  });
  MockFilePicker.init();
  registerCleanupFunction(() => MockFilePicker.cleanup());
});

add_task(async function test_invalid_destination_cancels_download() {
  let source = Services.io.newURI("https://example.com/file.bin");
  let lastDir = new DownloadLastDir(window);
  let goodDir = await IOUtils.getDirectory(PathUtils.tempDir);
  lastDir.setFile(source, goodDir);
  registerCleanupFunction(() => lastDir.setFile(source, null));

  let missingFile = await IOUtils.getFile(
    PathUtils.tempDir,
    "missing-download-dir",
    "file.bin"
  );
  await IOUtils.remove(PathUtils.parent(missingFile.path), {
    recursive: true,
  });

  MockFilePicker.showCallback = picker => {
    Assert.equal(
      picker.displayDirectory.path,
      goodDir.path,
      "The picker starts in the remembered directory"
    );
    MockFilePicker.setFiles([missingFile]);
    return MockFilePicker.returnOK;
  };

  let alertStub = sinon.stub(
    nsUnknownContentTypeDialog.prototype,
    "displayBadPermissionAlert"
  );
  registerCleanupFunction(() => alertStub.restore());

  Assert.equal(await promptToSave(source), null, "The download is cancelled");
  Assert.ok(alertStub.calledOnce, "The user is told about the bad directory");
  Assert.equal(
    (await lastDir.getFileAsync(source)).path,
    goodDir.path,
    "The invalid directory is not remembered"
  );
});

add_task(async function test_unexpected_failure_cancels_transfer() {
  await SpecialPowers.pushPrefEnv({
    set: [["browser.download.always_ask_before_handling_new_types", false]],
  });
  let { promise: lookedUp, resolve } = Promise.withResolvers();
  let getFileStub = sinon
    .stub(DownloadLastDir.prototype, "getFileAsync")
    .callsFake(() => {
      resolve();
      return Promise.reject(new Error("Unexpected failure"));
    });
  registerCleanupFunction(() => getFileStub.restore());
  MockFilePicker.showCallback = () => {
    Assert.ok(false, "The picker should not be shown");
    return MockFilePicker.returnCancel;
  };

  let stopped = TestUtils.topicObserved(
    "http-on-stop-request",
    subject =>
      subject.QueryInterface(Ci.nsIChannel).URI.spec == HELD_DOWNLOAD_URL
  );
  let tab = BrowserTestUtils.addTab(gBrowser, HELD_DOWNLOAD_URL);
  await lookedUp;
  // Let the save flow handle the failure before more data arrives.
  await TestUtils.waitForTick();
  await fetch(HELD_DOWNLOAD_URL + "?continue");

  let [channel] = await stopped;
  Assert.equal(
    channel.QueryInterface(Ci.nsIChannel).status,
    Cr.NS_BINDING_ABORTED,
    "The transfer is cancelled instead of continuing in the background"
  );
  Assert.ok(getFileStub.calledOnce, "The last directory was looked up");

  BrowserTestUtils.removeTab(tab);
  getFileStub.restore();
});

function promptToSave(source) {
  let { promise, resolve } = Promise.withResolvers();
  let launcher = {
    source,
    MIMEInfo: { description: "Binary", primaryExtension: "bin" },
    saveDestinationAvailable: resolve,
  };
  new nsUnknownContentTypeDialog().promptForSaveToFileAsync(
    launcher,
    window.docShell.QueryInterface(Ci.nsIInterfaceRequestor),
    "file.bin",
    ".bin",
    true
  );
  return promise;
}
