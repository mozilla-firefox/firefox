/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// A local handler application receives the URI it is launched with, or the
// path of the file it opens, as its only argument, unchanged, and so does a
// default application from a mailcap file.

// Characters which shells, URIs and desktop entries treat specially.
const SPECIAL = ` %f %% 'single' "double" \\ $HOME #hash ?query ä`;

const tempDir = do_get_tempdir();
const outFile = tempDir.clone();
outFile.append("argument.txt");

let writeArgument;

add_setup(function () {
  writeArgument = do_get_cwd();
  writeArgument.append("WriteArgument");
  Assert.ok(writeArgument.exists(), "WriteArgument exists");

  // WriteArgument needs the libraries next to the application.
  Services.env.set(
    "LD_LIBRARY_PATH",
    Services.dirsvc.get("GreD", Ci.nsIFile).path
  );
  Services.env.set("WRITE_ARGUMENT_FILE", outFile.path);
});

function localHandler(executable) {
  let handler = Cc["@mozilla.org/uriloader/local-handler-app;1"].createInstance(
    Ci.nsILocalHandlerApp
  );
  handler.executable = executable;
  return handler;
}

function createFile(name) {
  let file = tempDir.clone();
  file.append(name);
  file.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0o644);
  return file;
}

async function launchedArgument(launch) {
  launch();
  await TestUtils.waitForCondition(
    async () =>
      (await IOUtils.exists(outFile.path)) &&
      (await IOUtils.stat(outFile.path)).size > 0,
    "WriteArgument wrote its argument"
  );
  let argument = await IOUtils.readUTF8(outFile.path);
  await IOUtils.remove(outFile.path);
  return argument;
}

function textMIMEInfo(handler) {
  let mimeInfo = Cc["@mozilla.org/mime;1"]
    .getService(Ci.nsIMIMEService)
    .getFromTypeAndExtension("text/plain", "txt");
  mimeInfo.preferredApplicationHandler = handler;
  mimeInfo.preferredAction = Ci.nsIHandlerInfo.useHelperApp;
  return mimeInfo;
}

add_task(async function test_file() {
  let file = createFile(`file${SPECIAL}.txt`);
  let mimeInfo = textMIMEInfo(localHandler(writeArgument));
  Assert.equal(
    await launchedArgument(() => mimeInfo.launchWithFile(file)),
    file.path
  );
});

async function checkExecutablePath(name) {
  let dir = tempDir.clone();
  dir.append(`handler ${name}`);
  dir.createUnique(Ci.nsIFile.DIRECTORY_TYPE, 0o755);
  writeArgument.copyTo(dir, `Write Argument ${name}`);
  let executable = dir.clone();
  executable.append(`Write Argument ${name}`);
  executable.permissions = 0o755;

  let file = createFile("file.txt");
  let mimeInfo = textMIMEInfo(localHandler(executable));
  Assert.equal(
    await launchedArgument(() => mimeInfo.launchWithFile(file)),
    file.path
  );
}

add_task(async function test_executable_path() {
  await checkExecutablePath(SPECIAL.replaceAll("%", ""));
});

add_task(async function test_executable_path_percent() {
  // GLib does not accept an executable path with %, which then runs with
  // nsIProcess.
  await checkExecutablePath("%f %%");
});

add_task(async function test_uri() {
  let uri = Services.io.newURI(
    "https://example.com/a%20b/%25f?c='d'&e=%22f%22#g"
  );
  let handler = localHandler(writeArgument);
  Assert.equal(
    await launchedArgument(() => handler.launchWithURI(uri)),
    uri.asciiSpec
  );
});

add_task(async function test_file_uri() {
  // A file URI is passed as the URI, not as the path of the file.
  let uri = Services.io.newFileURI(createFile(`file${SPECIAL}.txt`));
  let handler = localHandler(writeArgument);
  Assert.equal(
    await launchedArgument(() => handler.launchWithURI(uri)),
    uri.asciiSpec
  );
});

add_task(async function test_mailcap_default() {
  let mailcap = tempDir.clone();
  mailcap.append("mailcap");
  await IOUtils.writeUTF8(
    mailcap.path,
    `application/x-launch-local-handler-test; ${writeArgument.path}\n`
  );
  Services.prefs.setStringPref("helpers.private_mailcap_file", mailcap.path);
  registerCleanupFunction(() =>
    Services.prefs.clearUserPref("helpers.private_mailcap_file")
  );

  let mimeInfo = Cc["@mozilla.org/mime;1"]
    .getService(Ci.nsIMIMEService)
    .getFromTypeAndExtension("application/x-launch-local-handler-test", null);
  Assert.equal(mimeInfo.defaultDescription, writeArgument.path);
  mimeInfo.preferredAction = Ci.nsIHandlerInfo.useSystemDefault;

  let file = createFile(`file${SPECIAL}.bin`);
  Assert.equal(
    await launchedArgument(() => mimeInfo.launchWithFile(file)),
    file.path
  );
});
