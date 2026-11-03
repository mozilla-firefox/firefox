/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { getPEMString } = ChromeUtils.importESModule(
  "moz-src:///security/manager/pki/pippki.sys.mjs"
);

var MockFilePicker = SpecialPowers.MockFilePicker;

function createTemporarySaveDirectory() {
  var saveDir = Services.dirsvc.get("TmpD", Ci.nsIFile);
  saveDir.append("testsavedir");
  if (!saveDir.exists()) {
    info("create testsavedir!");
    saveDir.create(Ci.nsIFile.DIRECTORY_TYPE, 0o755);
  }
  info("return from createTempSaveDir: " + saveDir.path);
  return saveDir;
}

// Create the folder the certificates will be saved into.
var destDir = createTemporarySaveDirectory();
registerCleanupFunction(function () {
  destDir.remove(true);
  BrowserTestUtils.removeTab(gBrowser.selectedTab);
});

function stringOrArrayEquals(actual, expected, message) {
  is(
    typeof actual,
    typeof expected,
    "actual, expected should have the same type"
  );
  if (typeof expected == "string") {
    is(actual, expected, message);
  } else {
    is(actual.toString(), expected.toString(), message);
  }
}

var dialogWin;
var exportButton;
var expectedCert;

async function setupTest() {
  await openPreferencesViaOpenPreferencesAPI("paneConnectionSecurity", {
    leaveOpen: true,
  });
  let certButton = gBrowser.selectedBrowser.contentDocument.getElementById(
    "viewCertificatesButton"
  );
  certButton.scrollIntoView();
  let certDialogLoaded = promiseLoadSubDialog(
    "chrome://pippki/content/certManager.xhtml"
  );
  certButton.click();
  dialogWin = await certDialogLoaded;
  let doc = dialogWin.document;
  doc.getElementById("certmanagertabs").selectedTab =
    doc.getElementById("ca_tab");
  await TestUtils.waitForCondition(
    () => !!doc.getElementById("ca-tree").view.rowCount,
    "ca list populated"
  );
  let treeView = doc.getElementById("ca-tree").view;
  // Select any which cert. Ignore parent rows (ie rows without certs):
  for (let i = 0; i < treeView.rowCount; i++) {
    treeView.selection.select(i);
    dialogWin.getSelectedCerts();
    let certs = dialogWin.selected_certs; // yuck... but this is how the dialog works.
    if (certs && certs.length == 1 && certs[0]) {
      expectedCert = certs[0];
      // OK, we managed to select a cert!
      break;
    }
  }

  exportButton = doc.getElementById("ca_exportButton");
  is(exportButton.disabled, false, "Should enable export button");
}

async function checkCertExportWorks(
  exportType,
  encoding,
  expectedFileContents
) {
  MockFilePicker.displayDirectory = destDir;
  var destFile = destDir.clone();
  MockFilePicker.init();
  MockFilePicker.filterIndex = exportType;
  MockFilePicker.showCallback = function (fp) {
    info("showCallback");
    let fileName = fp.defaultString;
    info("fileName: " + fileName);
    destFile.append(fileName);
    MockFilePicker.setFiles([destFile]);
    info("done showCallback");
  };
  let finishedExporting = TestUtils.topicObserved("cert-export-finished");
  exportButton.click();
  await finishedExporting;
  MockFilePicker.cleanup();
  if (destFile && destFile.exists()) {
    let contents;
    if (encoding === "utf-8") {
      contents = await IOUtils.readUTF8(destFile.path);
    } else {
      is(encoding, "", "expected either utf-8 or empty string for encoding");
      contents = await IOUtils.read(destFile.path);
    }
    stringOrArrayEquals(
      contents,
      expectedFileContents,
      "Should have written correct contents"
    );
    destFile.remove(false);
  } else {
    ok(false, "No cert saved!");
  }
}

add_task(setupTest);

add_task(async function checkCertPEMExportWorks() {
  let expectedContents = getPEMString(expectedCert);
  await checkCertExportWorks(0, /* 0 = PEM */ "utf-8", expectedContents);
});

add_task(async function checkCertPEMChainExportWorks() {
  let expectedContents = getPEMString(expectedCert);
  await checkCertExportWorks(
    1, // 1 = PEM chain, but the chain is of length 1
    "utf-8",
    expectedContents
  );
});

add_task(async function checkCertDERExportWorks() {
  let expectedContents = Uint8Array.from(expectedCert.getRawDER());
  await checkCertExportWorks(2, /* 2 = DER */ "", expectedContents);
});

function stringToTypedArray(str) {
  let arr = new Uint8Array(str.length);
  for (let i = 0; i < arr.length; i++) {
    arr[i] = str.charCodeAt(i);
  }
  return arr;
}

add_task(async function checkCertPKCS7ExportWorks() {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  let expectedContents = stringToTypedArray(certdb.asPKCS7Blob([expectedCert]));
  await checkCertExportWorks(3, /* 3 = PKCS7 */ "", expectedContents);
});

add_task(async function checkCertPKCS7ChainExportWorks() {
  let certdb = Cc["@mozilla.org/security/x509certdb;1"].getService(
    Ci.nsIX509CertDB
  );
  let expectedContents = stringToTypedArray(certdb.asPKCS7Blob([expectedCert]));
  await checkCertExportWorks(
    4, // 4 = PKCS7 chain, but the chain is of length 1
    "",
    expectedContents
  );
});
