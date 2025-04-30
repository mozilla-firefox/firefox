/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test setting custom user agent.
const TEST_DOCUMENT = "target_configuration_test_doc.sjs";
const TEST_URI = URL_ROOT_COM_SSL + TEST_DOCUMENT;

add_task(async function () {
  const tab = await addTab(TEST_URI);

  info("Create commands for the tab");
  const commands = await CommandsFactory.forTab(tab);

  const targetConfigurationCommand = commands.targetConfigurationCommand;
  const targetCommand = commands.targetCommand;
  await targetCommand.startListening();

  const initialUserAgent = await getTopLevelUserAgent();

  info("Update configuration to change user agent");
  const CUSTOM_USER_AGENT = "<MY_BORING_CUSTOM_USER_AGENT>";

  await targetConfigurationCommand.updateConfiguration({
    customUserAgent: CUSTOM_USER_AGENT,
  });

  is(
    await getTopLevelUserAgent(),
    CUSTOM_USER_AGENT,
    "The user agent is properly set on the top level document after updating the configuration"
  );
  is(
    await getUserAgentForTopLevelRequest(commands),
    CUSTOM_USER_AGENT,
    "The custom user agent is used when retrieving resources on the top level document"
  );

  is(
    await getIframeUserAgent(),
    CUSTOM_USER_AGENT,
    "The user agent is properly set on the iframe after updating the configuration"
  );
  is(
    await getUserAgentForIframeRequest(commands),
    CUSTOM_USER_AGENT,
    "The custom user agent is used when retrieving resources on the iframe"
  );

  info("Reload the page");
  await BrowserTestUtils.reloadTab(tab, {
    includeSubFrames: true,
  });

  is(
    await getTopLevelDocumentUserAgentAtStartup(),
    CUSTOM_USER_AGENT,
    "The custom user agent was set in the content page when it loaded after reloading"
  );
  is(
    await getTopLevelUserAgent(),
    CUSTOM_USER_AGENT,
    "The custom user agent is set in the content page after reloading"
  );
  is(
    await getUserAgentForTopLevelRequest(commands),
    CUSTOM_USER_AGENT,
    "The custom user agent is used when retrieving resources after reloading"
  );
  is(
    await getIframeUserAgentAtStartup(),
    CUSTOM_USER_AGENT,
    "The custom user agent was set in the remote iframe when it loaded after reloading"
  );
  is(
    await getIframeUserAgent(),
    CUSTOM_USER_AGENT,
    "The custom user agent is set in the remote iframe after reloading"
  );
  is(
    await getUserAgentForIframeRequest(commands),
    CUSTOM_USER_AGENT,
    "The custom user agent is used when retrieving resources in the remote iframe after reloading"
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
    await getTopLevelDocumentUserAgentAtStartup(),
    CUSTOM_USER_AGENT,
    "The custom user agent was set in the content page when it loaded after navigating to a new browsing context"
  );
  is(
    await getTopLevelUserAgent(),
    CUSTOM_USER_AGENT,
    "The custom user agent is set in the content page after navigating to a new browsing context"
  );
  is(
    await getUserAgentForTopLevelRequest(commands),
    CUSTOM_USER_AGENT,
    "The custom user agent is used when retrieving resources after navigating to a new browsing context"
  );
  is(
    await getIframeUserAgentAtStartup(),
    CUSTOM_USER_AGENT,
    "The custom user agent was set in the remote iframe when it loaded after navigating to a new browsing context"
  );
  is(
    await getIframeUserAgent(),
    CUSTOM_USER_AGENT,
    "The custom user agent is set in the remote iframe after navigating to a new browsing context"
  );
  is(
    await getUserAgentForTopLevelRequest(commands),
    CUSTOM_USER_AGENT,
    "The custom user agent is used when retrieving resources in the remote iframes after navigating to a new browsing context"
  );

  info(
    "Create another commands instance and check that destroying it won't reset the user agent"
  );
  const otherCommands = await CommandsFactory.forTab(tab);
  const otherTargetConfigurationCommand =
    otherCommands.targetConfigurationCommand;
  const otherTargetCommand = otherCommands.targetCommand;
  await otherTargetCommand.startListening();
  // wait for the target to be fully attached to avoid pending connection to the server
  await otherTargetCommand.watchTargets({
    types: [otherTargetCommand.TYPES.FRAME],
    onAvailable: () => {},
  });

  // Let's update the configuration with this commands instance to make sure we hit the TargetConfigurationActor
  await otherTargetConfigurationCommand.updateConfiguration({
    colorSchemeSimulation: "dark",
  });

  otherTargetCommand.destroy();
  await otherCommands.destroy();

  is(
    await getTopLevelUserAgent(),
    CUSTOM_USER_AGENT,
    "The custom user agent is still set on the page after destroying another commands instance"
  );

  info(
    "Check that destroying the commands we set the user agent in will reset the user agent"
  );
  targetCommand.destroy();
  await commands.destroy();

  // XXX: This is needed at the moment since Navigator.cpp retrieve the UserAgent from the
  // headers (when there's no custom user agent). And here, since we reloaded the page once
  // we set the custom user agent, the header was set accordingly and still holds the custom
  // user agent value. This should be fixed by Bug 1705326.
  is(
    await getTopLevelUserAgent(),
    CUSTOM_USER_AGENT,
    "The custom user agent is still set on the page after destroying the first commands instance. Bug 1705326 will fix that and make it equal to `initialUserAgent`"
  );

  await BrowserTestUtils.reloadTab(tab, {
    includeSubFrames: true,
  });
  is(
    await getTopLevelUserAgent(),
    initialUserAgent,
    "The user agent was reset in the content page after destroying the commands"
  );
  is(
    await getIframeUserAgent(),
    initialUserAgent,
    "The user agent was reset in the remote iframe after destroying the commands"
  );

  // We need commands to retrieve the headers of the network request, and
  // all those we created so far were destroyed; let's create new ones.
  const newCommands = await CommandsFactory.forTab(tab);
  await newCommands.targetCommand.startListening();
  is(
    await getUserAgentForTopLevelRequest(newCommands),
    initialUserAgent,
    "The initial user agent is used when retrieving resources after destroying the commands"
  );
  is(
    await getUserAgentForIframeRequest(newCommands),
    initialUserAgent,
    "The initial user agent is used when retrieving resources on the remote iframe after destroying the commands"
  );
});

function getUserAgent(browserOrBrowsingContext) {
  return SpecialPowers.spawn(browserOrBrowsingContext, [], () => {
    return content.navigator.userAgent;
  });
}

function getUserAgentAtStartup(browserOrBrowsingContext) {
  return SpecialPowers.spawn(
    browserOrBrowsingContext,
    [],
    () => content.wrappedJSObject.initialUserAgent
  );
}

function getTopLevelUserAgent() {
  return getUserAgent(gBrowser.selectedBrowser);
}

function getTopLevelDocumentUserAgentAtStartup() {
  return getUserAgentAtStartup(gBrowser.selectedBrowser);
}

function getIframeBrowsingContext() {
  return SpecialPowers.spawn(
    gBrowser.selectedBrowser,
    [],
    () => content.document.querySelector("iframe").browsingContext
  );
}

async function getIframeUserAgent() {
  const iframeBC = await getIframeBrowsingContext();
  return getUserAgent(iframeBC);
}

async function getIframeUserAgentAtStartup() {
  const iframeBC = await getIframeBrowsingContext();
  return getUserAgentAtStartup(iframeBC);
}

async function getRequestUserAgent(commands, browserOrBrowsingContext) {
  const url = `unknown?${Date.now()}`;

  // Wait for the resource and its headers to be available
  const onAvailable = () => {};
  let onUpdated;

  const onResource = new Promise(resolve => {
    onUpdated = updates => {
      for (const { resource } of updates) {
        if (resource.url.includes(url) && resource.requestHeadersAvailable) {
          resolve(resource);
        }
      }
    };
  });

  await commands.resourceCommand.watchResources(
    [commands.resourceCommand.TYPES.NETWORK_EVENT],
    {
      onAvailable,
      onUpdated,
      ignoreExistingResources: true,
    }
  );

  info(`Fetch ${url}`);
  SpecialPowers.spawn(browserOrBrowsingContext, [url], innerUrl => {
    content.fetch(`./${innerUrl}`);
  });
  info("waiting for matching resource…");
  const networkResource = await onResource;

  info("…got resource, retrieve headers");
  const packet = {
    to: networkResource.actor,
    type: "getRequestHeaders",
  };

  const { headers } = await commands.client.request(packet);

  commands.resourceCommand.unwatchResources(
    [commands.resourceCommand.TYPES.NETWORK_EVENT],
    {
      onAvailable,
      onUpdated,
      ignoreExistingResources: true,
    }
  );

  return headers.find(header => header.name == "User-Agent")?.value;
}

async function getUserAgentForTopLevelRequest(commands) {
  return getRequestUserAgent(commands, gBrowser.selectedBrowser);
}

async function getUserAgentForIframeRequest(commands) {
  const iframeBC = await getIframeBrowsingContext();
  return getRequestUserAgent(commands, iframeBC);
}
