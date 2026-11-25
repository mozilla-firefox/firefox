/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

ChromeUtils.defineESModuleGetters(this, {
  sinon: "resource://testing-common/Sinon.sys.mjs",
  ShellService: "moz-src:///browser/components/shell/ShellService.sys.mjs",
  TaskbarTabsPin:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsPin.sys.mjs",
  TaskbarTabsRegistry:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsRegistry.sys.mjs",
  TaskbarTabsUtils:
    "moz-src:///browser/components/taskbartabs/TaskbarTabsUtils.sys.mjs",
  XPCOMUtils: "resource://gre/modules/XPCOMUtils.sys.mjs",
});

XPCOMUtils.defineLazyServiceGetters(this, {
  imgTools: ["@mozilla.org/image/tools;1", Ci.imgITools],
});

// We want to mock the native XPCOM interfaces of the initialized
// `ShellService.shellService`, but those interfaces are frozen. Instead we
// proxy `ShellService.shellService` and mock it.
let gWriteShortcutIcon = ShellService.writeShortcutIcon;
let gOverrideIconFileOnce;
const kMockNativeShellService = {
  ...ShellService.shellService,
  createShortcut: sinon.stub().resolves("dummy_path"),
  deleteShortcut: sinon.stub().resolves("dummy_path"),
  pinShortcutToTaskbar: sinon.stub().resolves(),
  unpinShortcutFromTaskbar: sinon.stub(),
};

sinon.stub(ShellService, "shellService").value(kMockNativeShellService);
sinon.stub(ShellService, "createLinuxDesktopEntry").resolves();
sinon.stub(ShellService, "deleteLinuxDesktopEntry").resolves();

sinon
  .stub(ShellService, "writeShortcutIcon")
  .callsFake(async (aIconFile, aImgContainer) => {
    if (gOverrideIconFileOnce) {
      await gWriteShortcutIcon(gOverrideIconFileOnce, aImgContainer);
      gOverrideIconFileOnce = null;
    }
  });

registerCleanupFunction(() => {
  sinon.restore();
});

// Favicons are written to the profile directory, ensure it exists.
do_get_profile();

let gPngFavicon;
let gSvgFavicon;
add_setup(async () => {
  const pngFile = do_get_file("favicon-normal16.png");
  const pngData = await IOUtils.read(pngFile.path);
  gPngFavicon = Services.io.newURI(
    `data:image/png;base64,${pngData.toBase64()}`
  );

  const svgFile = do_get_file("icon.svg");
  const svgData = await IOUtils.read(svgFile.path);
  gSvgFavicon = Services.io.newURI(
    `data:image/svg+xml;base64,${svgData.toBase64()}`
  );
});

async function testWrittenIconFile(aIconFile) {
  const data = await IOUtils.read(aIconFile.path);
  const imgContainer = imgTools.decodeImageFromArrayBuffer(
    data.buffer,
    ShellService.shortcutIconType.mimeType
  );
  equal(
    imgContainer.width,
    256,
    "Image written to disk should be 256px width."
  );
  equal(
    imgContainer.height,
    256,
    "Image written to disk should be 256px height."
  );
}

const url = Services.io.newURI("https://www.test.com");
const userContextId = 0;

const registry = createInMemoryRegistry();
const taskbarTab = createTaskbarTab(registry, url, userContextId);

function getTempFile() {
  let path = do_get_tempdir();
  let filename = Services.uuid.generateUUID().toString().slice(1, -1);
  path.append(filename + "." + ShellService.shortcutIconType.extension);
  return path;
}

add_task(async function test_pin_saves_raster_icon() {
  sinon.resetHistory();

  let iconFile = getTempFile();
  gOverrideIconFileOnce = iconFile;

  let img = await TaskbarTabsUtils._imageFromLocalURI(gPngFavicon);
  await TaskbarTabsPin.pinTaskbarTab(taskbarTab, img);

  equal(
    ShellService.writeShortcutIcon.firstCall.args[1],
    img,
    "The image that is saved should be the correct image"
  );

  await testWrittenIconFile(iconFile);
});

add_task(async function test_pin_saves_vector_icon() {
  sinon.resetHistory();

  let iconFile = getTempFile();
  gOverrideIconFileOnce = iconFile;

  let img = await TaskbarTabsUtils._imageFromLocalURI(gSvgFavicon);
  await TaskbarTabsPin.pinTaskbarTab(taskbarTab, img);

  equal(
    ShellService.writeShortcutIcon.firstCall.args[1],
    img,
    "The image that is saved should be the correct image"
  );

  await testWrittenIconFile(iconFile);
});
