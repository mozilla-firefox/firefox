/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
"use strict";

Services.scriptloader.loadSubScript(
  "chrome://mochitests/content/browser/devtools/client/debugger/test/mochitest/shared-head.js",
  this
);

/* import-globals-from helper-addons.js */
Services.scriptloader.loadSubScript(CHROME_URL_ROOT + "helper-addons.js", this);

const L10N = new LocalizationHelper(
  "devtools/client/locales/toolbox.properties"
);

const EXTENSION_NAME = "temporary-web-extension";
const EXTENSION_ID = "test-devtools@mozilla.org";

add_task(async function testOpenDebuggerReload() {
  await enableExtensionDebugging();

  info(
    "The debugger should show the source codes of extension even if " +
      "devtools.chrome.enabled and devtools.debugger.remote-enabled are off"
  );
  await pushPref("devtools.chrome.enabled", false);
  await pushPref("devtools.debugger.remote-enabled", false);

  const { document, tab, window } = await openAboutDebugging();
  await selectThisFirefoxPage(document, window.AboutDebugging.store);
  const isStylesheetsInDebuggerEnabled = Services.prefs.getBoolPref(
    "devtools.debugger.features.stylesheets-in-debugger"
  );
  await installTemporaryExtensionFromXPI(
    {
      background() {
        window.someRandomMethodName = () => {
          // This will not be referred from anywhere.
          // However this is necessary to show as the source code in the debugger.
        };
      },
      id: EXTENSION_ID,
      name: EXTENSION_NAME,
    },
    document
  );

  // Select the debugger right away to avoid any noise coming from the inspector.
  await pushPref("devtools.toolbox.selectedTool", "jsdebugger");
  const { devtoolsWindow } = await openAboutDevtoolsToolbox(
    document,
    tab,
    window,
    EXTENSION_NAME
  );
  const toolbox = getToolbox(devtoolsWindow);
  const { panelWin } = toolbox.getCurrentPanel();
  const dbg = createDebuggerContext(toolbox);

  info("Check the state of redux");
  ok(
    panelWin.dbg.store.getState().sourcesTree.isWebExtension,
    "isWebExtension flag in sourcesTree is true"
  );

  const descriptorTitle = devtoolsWindow.document.querySelector(
    ".qa-descriptor-title"
  );
  is(
    descriptorTitle.textContent,
    EXTENSION_NAME,
    "The add-on name is displayed in the toolbox header"
  );

  info("Check whether the element displays correctly");
  let sourceList = panelWin.document.querySelector(".sources-list");
  ok(sourceList, "Source list element displays correctly");
  ok(
    sourceList.textContent.includes("temporary-web-extension"),
    "Extension name displays correctly"
  );

  const { onResource: onDomCompleteResource } =
    await toolbox.commands.resourceCommand.waitForNextResource(
      toolbox.commands.resourceCommand.TYPES.DOCUMENT_EVENT,
      {
        ignoreExistingResources: true,
        predicate: resource => {
          return (
            resource.name === "dom-complete" &&
            resource.targetFront.url.endsWith("background_page.html")
          );
        },
      }
    );

  info("Reload the addon using a toolbox reload shortcut");
  toolbox.win.focus();
  synthesizeKeyShortcut(L10N.getStr("toolbox.reload.key"), toolbox.win);

  await onDomCompleteResource;
  if (isStylesheetsInDebuggerEnabled) {
    // The background page is now the 5 entry in the source tree because
    // of the added styles to the tree, and is no longer automatically expanded.
    await clickElement(dbg, "sourceDirectoryLabel", 5);
  }
  info("Wait until a new background log message is logged");
  await waitFor(() => {
    // As React may re-create a new sources-list element,
    // fetch the latest instance
    sourceList = panelWin.document.querySelector(".sources-list");
    return sourceList?.textContent.includes("temporary-web-extension");
  }, "Wait for the source to re-appear");

  await closeWebExtAboutDevtoolsToolbox(devtoolsWindow, window);
  await removeTemporaryExtension(EXTENSION_NAME, document);
  await removeTab(tab);
});

add_task(async function testAddAndRemoveBreakpoint() {
  await enableExtensionDebugging();

  const { document, tab, window } = await openAboutDebugging();
  await selectThisFirefoxPage(document, window.AboutDebugging.store);

  await installTemporaryExtensionFromXPI(
    {
      background() {
        window.invokeLogFromWebextension = () => {
          console.log("From webextension");
        };
      },
      id: EXTENSION_ID,
      name: EXTENSION_NAME,
    },
    document
  );

  // Select the debugger right away to avoid any noise coming from the inspector.
  await pushPref("devtools.toolbox.selectedTool", "jsdebugger");
  const { devtoolsWindow } = await openAboutDevtoolsToolbox(
    document,
    tab,
    window,
    EXTENSION_NAME
  );
  const isStylesheetsInDebuggerEnabled = Services.prefs.getBoolPref(
    "devtools.debugger.features.stylesheets-in-debugger"
  );
  const toolbox = getToolbox(devtoolsWindow);
  const dbg = createDebuggerContext(toolbox);

  info("Assert the threads displayed in Source Tree as well as Threads pane");
  const sourceTreeThreads = findAllElements(dbg, "sourceTreeThreads");
  is(
    sourceTreeThreads.length,
    isStylesheetsInDebuggerEnabled ? 2 : 1,
    "There are two threads with source in the Source Tree"
  );
  is(
    sourceTreeThreads[sourceTreeThreads.length - 1].textContent,
    "/_generated_background_page.html",
    "That thread is the background page"
  );

  const threadLabels = findAllElements(dbg, "threadsPaneItems");
  is(threadLabels.length, 2, "But there are two threads in the thread panel");
  is(threadLabels[0].textContent, "Web Extension Fallback Document");
  is(threadLabels[1].textContent, "/_generated_background_page.html");

  info("Select the source and add a breakpoint");
  // Note: Style sheet sources are included and the background script filename is dynamically generated id, so we
  // use a regex pattern to match it.
  const displayedSources = dbg.selectors.getDisplayedSourcesList();
  const backgroundScriptRegex = /source-url-moz-extension:\/\/.+\.js/g;
  const backgroundScript = displayedSources.find(source =>
    backgroundScriptRegex.test(source.id)
  );
  await selectSource(dbg, backgroundScript);
  await addBreakpoint(dbg, backgroundScript, 3);

  info("Trigger the breakpoint and wait for the debugger to pause");
  const webconsole = await toolbox.selectTool("webconsole");
  const { hud } = webconsole;
  hud.ui.wrapper.dispatchEvaluateExpression("invokeLogFromWebextension()");
  await waitForPaused(dbg);

  info("Resume and remove the breakpoint");
  await resume(dbg);
  await removeBreakpoint(dbg, backgroundScript.id, 3);

  info("Trigger the function again and check the debugger does not pause");
  hud.ui.wrapper.dispatchEvaluateExpression("invokeLogFromWebextension()");
  await wait(500);
  assertNotPaused(dbg);

  await closeWebExtAboutDevtoolsToolbox(devtoolsWindow, window);
  await removeTemporaryExtension(EXTENSION_NAME, document);
  await removeTab(tab);
});
