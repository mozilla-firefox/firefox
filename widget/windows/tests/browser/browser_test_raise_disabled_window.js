/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// While a native file dialog is up, our root window is disabled (see
// nsWindow::PickerOpen) and the dialog is its only enabled owned popup. This
// test recreates that state with a bare Win32 popup rather than a real file
// dialog -- a real one cannot be dismissed reliably from script -- and then
// checks that a system caller, i.e. another application handing us a URL, can
// still raise us out from behind an unrelated window.
// Proper testing requires a widget testing framework.  This test is instead a
// dense collection of jsctypes calls.
const { ctypes } = ChromeUtils.importESModule(
  "moz-src:///toolkit/components/ctypes/ctypes.sys.mjs"
);

const BOOL = ctypes.int32_t;
const DWORD = ctypes.uint32_t;
const HWND = ctypes.voidptr_t;
const INT = ctypes.int32_t;
const LPCWSTR = ctypes.char16_t.ptr;

const GW_HWNDNEXT = 2;
const GW_ENABLEDPOPUP = 6;
const WS_CAPTION = 0x00c00000;
const WS_POPUP = 0x80000000;
const WS_SYSMENU = 0x00080000;
const WS_VISIBLE = 0x10000000;

const user32 = ctypes.open("user32.dll");
registerCleanupFunction(() => user32.close());

const CreateWindowExW = user32.declare(
  "CreateWindowExW",
  ctypes.winapi_abi,
  HWND,
  DWORD,
  LPCWSTR,
  LPCWSTR,
  DWORD,
  INT,
  INT,
  INT,
  INT,
  HWND,
  ctypes.voidptr_t,
  ctypes.voidptr_t,
  ctypes.voidptr_t
);
const DestroyWindow = user32.declare(
  "DestroyWindow",
  ctypes.winapi_abi,
  BOOL,
  HWND
);
const GetForegroundWindow = user32.declare(
  "GetForegroundWindow",
  ctypes.winapi_abi,
  HWND
);
const GetTopWindow = user32.declare(
  "GetTopWindow",
  ctypes.winapi_abi,
  HWND,
  HWND
);
const GetWindow = user32.declare(
  "GetWindow",
  ctypes.winapi_abi,
  HWND,
  HWND,
  DWORD
);
const GetWindowTextW = user32.declare(
  "GetWindowTextW",
  ctypes.winapi_abi,
  INT,
  HWND,
  LPCWSTR,
  INT
);
const IsWindowEnabled = user32.declare(
  "IsWindowEnabled",
  ctypes.winapi_abi,
  BOOL,
  HWND
);
const IsWindowVisible = user32.declare(
  "IsWindowVisible",
  ctypes.winapi_abi,
  BOOL,
  HWND
);
const SetForegroundWindow = user32.declare(
  "SetForegroundWindow",
  ctypes.winapi_abi,
  BOOL,
  HWND
);

// Handles as comparable, readable strings, for identity checks and for
// assertion messages.
function id(aHwnd) {
  return `0x${ctypes.cast(aHwnd, ctypes.uintptr_t).value.toString(16)}`;
}

function createWindow(aName, aOwner) {
  const wnd = CreateWindowExW(
    0,
    "Static",
    aName,
    // WS_POPUP makes this negative as an int32, which ctypes won't accept.
    (WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE) >>> 0,
    100,
    100,
    300,
    200,
    aOwner,
    null,
    null,
    null
  );
  ok(!wnd.isNull(), `Created the "${aName}" window`);
  return wnd;
}

// Walks the desktop's z-order front to back and returns whichever of the two
// windows it reaches first.
function frontmostOf(aHwnd, aOther) {
  for (let w = GetTopWindow(null); !w.isNull(); w = GetWindow(w, GW_HWNDNEXT)) {
    if (id(w) == id(aHwnd) || id(w) == id(aOther)) {
      return id(w);
    }
  }
  return null;
}

// Positions in the desktop's z-order, front lowest, plus the visible windows
// in front of ours. One walk: the z-order is a live list, so positions taken
// from separate walks can disagree with each other.
function logZOrder({ root, dialog, interloper }, aLabel) {
  const wanted = new Map([
    [id(dialog), "dialog"],
    [id(root), "us"],
    [id(interloper), "interloper"],
  ]);
  const positions = new Map();
  const lines = [];
  let index = 0;
  for (
    let w = GetTopWindow(null);
    !w.isNull() && positions.size < wanted.size;
    w = GetWindow(w, GW_HWNDNEXT)
  ) {
    const name = wanted.get(id(w));
    if (name) {
      positions.set(name, index);
    }
    if (IsWindowVisible(w)) {
      lines.push(`  ${index}: ${describe(w)}`);
    }
    index++;
  }

  const summary = [...wanted.values()].map(
    name => `${name}=${positions.get(name) ?? "not found"}`
  );
  info(
    `${aLabel}: ${summary.join(" ")} (frontmost is lowest)\n` +
      `visible windows from the front:\n${lines.join("\n")}`
  );
}

async function promiseFrontmostOf(aHwnd, aOther) {
  let frontmost = null;
  // Let the caller's assertion report a mismatch instead of throwing here.
  await TestUtils.waitForCondition(
    () => (frontmost = frontmostOf(aHwnd, aOther)) == id(aHwnd),
    "Waiting for the z-order to settle",
    100 /* interval */,
    20 /* max number of attempts */
  ).catch(() => {});
  return frontmost;
}

function describe(aHwnd) {
  const title = ctypes.char16_t.array(256)();
  GetWindowTextW(aHwnd, title, 256);
  return `${id(aHwnd)} "${title.readString()}"`;
}

async function promiseForegroundWindow(aHwnd) {
  // Let the caller's assertion report a mismatch instead of throwing here.
  await TestUtils.waitForCondition(
    () => id(GetForegroundWindow()) == id(aHwnd),
    "Waiting for the foreground window to change",
    100 /* interval */,
    20 /* max number of attempts */
  ).catch(() => {});
  return id(GetForegroundWindow());
}

// Keeps asking until it sticks: something transient -- a window dying from a
// previous test, a system notification, etc -- can hold the foreground
// briefly.
async function promiseTakeForeground(aHwnd) {
  await TestUtils.waitForCondition(
    () => {
      SetForegroundWindow(aHwnd);
      return id(GetForegroundWindow()) == id(aHwnd);
    },
    "Trying to take the foreground",
    100,
    20
  ).catch(() => {});
  return id(GetForegroundWindow());
}

// Set our widget to the state a file dialog leaves us in -- disabled root window, one
// enabled owned popup -- with an unrelated "interloper" window in front of us, and hand it
// to aTask.
async function withDisabledWindowBehindInterloper(aTask) {
  const baseWindow = window.docShell.treeOwner.nsIBaseWindow;
  const root = HWND(Number(baseWindow.nativeHandle));

  // Await Windows reporting that we have taken focus.
  if ((await promiseTakeForeground(root)) != id(root)) {
    // Win32 only grants SetForegroundWindow to a process that already owns the
    // foreground. When something else owns it -- an editor or terminal on a
    // dev machine -- every call below is silently denied and the z-order we
    // would measure says nothing about the code under test, so we report that
    // error and end the test.
    ok(
      false,
      `${describe(
        GetForegroundWindow()
      )} owns the foreground, so we cannot activate our own windows. This ` +
        "test needs a session where the browser can come forward; it cannot " +
        "measure anything here."
    );
    return;
  }

  const dialog = createWindow("Modal dialog stand-in", root);
  baseWindow.enabled = false;
  const interloper = createWindow("Interloper", null);

  try {
    ok(!IsWindowEnabled(root), "Our root window should be disabled");
    is(
      id(GetWindow(root, GW_ENABLEDPOPUP)),
      id(dialog),
      "The stand-in should be our only enabled popup"
    );
    is(
      await promiseTakeForeground(interloper),
      id(interloper),
      "The interloper should hold the foreground"
    );
    is(
      await promiseFrontmostOf(interloper, root),
      id(interloper),
      "The interloper should be in front of us"
    );

    logZOrder({ root, dialog, interloper }, "Before the trigger");
    await aTask({ root, dialog, interloper });
  } finally {
    baseWindow.enabled = true;
    DestroyWindow(interloper);
    DestroyWindow(dialog);
    await promiseTakeForeground(root);
    await SimpleTest.promiseFocus(window);
  }
}

async function assertRaised({ root, dialog, interloper }) {
  is(
    await promiseForegroundWindow(dialog),
    id(dialog),
    "The dialog should have been activated, since we cannot be"
  );
  logZOrder({ root, dialog, interloper }, "After the trigger");
  is(
    await promiseFrontmostOf(root, interloper),
    id(root),
    "We should have been raised in front of the interloper"
  );
}

add_task(async function test_focus_from_system_caller() {
  await withDisabledWindowBehindInterloper(async windows => {
    window.focus();
    await assertRaised(windows);
  });
});

add_task(async function test_url_from_another_instance() {
  const remoteService = Cc["@mozilla.org/remote;1"].getService(
    Ci.nsIRemoteService
  );

  await withDisabledWindowBehindInterloper(async windows => {
    const tabOpened = BrowserTestUtils.waitForNewTab(
      gBrowser,
      "https://example.org/",
      true
    );
    remoteService.sendCommandLine(
      PathUtils.profileDir,
      ["https://example.org/"],
      true
    );
    const tab = await tabOpened;

    await assertRaised(windows);

    BrowserTestUtils.removeTab(tab);
  });
});
