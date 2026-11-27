/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test color scheme simulation.
const TEST_DOCUMENT = "target_configuration_test_doc.sjs";
const TEST_URI = URL_ROOT_COM_SSL + TEST_DOCUMENT;

add_task(async function () {
  info("Setup the test page with workers of all types");
  const tab = await addTab(TEST_URI);

  is(
    await topLevelDocumentMatchPrefersDarkColorSchemeMediaAtStartup(),
    false,
    "The dark mode simulation wasn't enabled in the content page when it loaded"
  );
  is(
    await topLevelDocumentMatchPrefersDarkColorSchemeMedia(),
    false,
    "The dark mode simulation isn't enabled in the content page by default"
  );
  is(
    await iframeDocumentMatchPrefersDarkColorSchemeMedia(),
    false,
    "The dark mode simulation isn't enabled in the remote iframe by default"
  );

  info("Create a target list for a tab target");
  const commands = await CommandsFactory.forTab(tab);

  const targetConfigurationCommand = commands.targetConfigurationCommand;
  const targetCommand = commands.targetCommand;
  await targetCommand.startListening();

  info("Update configuration to enable dark mode simulation");
  await targetConfigurationCommand.updateConfiguration({
    colorSchemeSimulation: "dark",
  });
  is(
    await topLevelDocumentMatchPrefersDarkColorSchemeMedia(),
    true,
    "The dark mode simulation is enabled after updating the configuration"
  );
  is(
    await iframeDocumentMatchPrefersDarkColorSchemeMedia(),
    true,
    "The dark mode simulation is enabled in the remote iframe after updating the configuration"
  );

  info("Reload the page");
  await BrowserTestUtils.reloadTab(tab, {
    includeSubFrames: true,
  });

  is(
    await topLevelDocumentMatchPrefersDarkColorSchemeMediaAtStartup(),
    true,
    "The dark mode simulation was enabled in the content page when it loaded after reloading"
  );
  is(
    await topLevelDocumentMatchPrefersDarkColorSchemeMedia(),
    true,
    "The dark mode simulation is enabled in the content page after reloading"
  );
  await waitFor(
    () => iframeDocumentMatchPrefersDarkColorSchemeMedia(),
    "Wait for the dark mode simulation to be enabled in the remote iframe after reloading"
  );

  const previousBrowsingContextId = gBrowser.selectedBrowser.browsingContext.id;
  info(
    "Check that navigating to a page that forces the creation of a new browsing context keep the simulation enabled"
  );

  const onPageLoaded = BrowserTestUtils.browserLoaded(
    gBrowser.selectedBrowser,
    /* includeSubFrames */ true
  );
  BrowserTestUtils.startLoadingURIString(
    gBrowser.selectedBrowser,
    URL_ROOT_ORG_SSL + TEST_DOCUMENT + "?crossOriginIsolated=true"
  );
  await onPageLoaded;

  isnot(
    gBrowser.selectedBrowser.browsingContext.id,
    previousBrowsingContextId,
    "A new browsing context was created"
  );

  is(
    await topLevelDocumentMatchPrefersDarkColorSchemeMediaAtStartup(),
    true,
    "The dark mode simulation was enabled in the content page when it loaded after navigating to a new browsing context"
  );
  is(
    await topLevelDocumentMatchPrefersDarkColorSchemeMedia(),
    true,
    "The dark mode simulation is enabled in the content page after navigating to a new browsing context"
  );
  await waitFor(
    () => iframeDocumentMatchPrefersDarkColorSchemeMedia(),
    "Wait for the dark mode simulation to be enabled in the remote iframe after navigating to a new browsing context"
  );

  targetCommand.destroy();
  await commands.destroy();

  is(
    await topLevelDocumentMatchPrefersDarkColorSchemeMedia(),
    false,
    "The dark mode simulation is disabled in the content page after destroying the commands"
  );
  is(
    await iframeDocumentMatchPrefersDarkColorSchemeMedia(),
    false,
    "The dark mode simulation is disabled in the remote iframe after destroying the commands"
  );
});

function matchPrefersDarkColorSchemeMedia(browserOrBrowsingContext) {
  return SpecialPowers.spawn(
    browserOrBrowsingContext,
    [],
    () => content.matchMedia("(prefers-color-scheme: dark)").matches
  );
}

function matchPrefersDarkColorSchemeMediaAtStartup(browserOrBrowsingContext) {
  return SpecialPowers.spawn(
    browserOrBrowsingContext,
    [],
    () => content.wrappedJSObject.initialMatchesPrefersDarkColorScheme
  );
}

function topLevelDocumentMatchPrefersDarkColorSchemeMedia() {
  return matchPrefersDarkColorSchemeMedia(gBrowser.selectedBrowser);
}

function topLevelDocumentMatchPrefersDarkColorSchemeMediaAtStartup() {
  return matchPrefersDarkColorSchemeMediaAtStartup(gBrowser.selectedBrowser);
}

function getIframeBrowsingContext() {
  return SpecialPowers.spawn(gBrowser.selectedBrowser, [], async function () {
    // Ensure we've rendered the iframe so that the prefers-color-scheme
    // value propagated from the embedder is up-to-date.
    await new Promise(resolve => {
      content.requestAnimationFrame(() =>
        content.requestAnimationFrame(resolve)
      );
    });
    return content.document.querySelector("iframe").browsingContext;
  });
}

async function iframeDocumentMatchPrefersDarkColorSchemeMedia() {
  const iframeBC = await getIframeBrowsingContext();
  return matchPrefersDarkColorSchemeMedia(iframeBC);
}
