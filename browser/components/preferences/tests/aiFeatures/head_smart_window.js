/* Any copyright is dedicated to the Public Domain.
   https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* global
  openPreferencesViaOpenPreferencesAPI,
  waitForPaneChange,
  openAiFeaturePanel,
*/

/* exported
  modelFor,
  openSmartWindowPreferencesPage,
  openSmartWindowPanel,
  openManageMemoriesPanel,
  populateMemories,
  addMemory,
  addChat,
  triggerBlockAndWaitForDialog,
  selectCustomModel,
  waitForModelRadio,
*/

ChromeUtils.defineESModuleGetters(this, {
  getModelForChoice:
    "moz-src:///browser/components/aiwindow/models/Utils.sys.mjs",
});

async function modelFor(choiceId) {
  return (await getModelForChoice(choiceId)).model;
}

async function openSmartWindowPreferencesPage() {
  await openPreferencesViaOpenPreferencesAPI("general", { leaveOpen: true });
  const doc = gBrowser.selectedBrowser.contentDocument;
  const win = doc.documentGlobal;
  return { doc, win };
}

async function openSmartWindowPanel(doc, win) {
  if (!doc) {
    ({ doc, win } = await openSmartWindowPreferencesPage());
  }

  await openAiFeaturePanel(doc, win);

  const personalizeButton = doc.getElementById("personalizeSmartWindowButton");
  personalizeButton.scrollIntoView();
  const panelLoaded = waitForPaneChange("personalizeSmartWindow");
  EventUtils.synthesizeMouseAtCenter(personalizeButton, {}, win);
  await panelLoaded;

  return { doc, win };
}

async function openManageMemoriesPanel(doc, win) {
  if (!doc) {
    ({ doc, win } = await openSmartWindowPanel());
  }

  const manageButton = doc.getElementById("manageMemoriesButton");
  manageButton.scrollIntoView();
  const paneLoaded = waitForPaneChange("manageMemories");
  EventUtils.synthesizeMouseAtCenter(manageButton, {}, win);
  await paneLoaded;

  return { doc, win };
}

async function addMemory(data = {}) {
  const { MemoryStore } = ChromeUtils.importESModule(
    "moz-src:///browser/components/aiwindow/services/MemoryStore.sys.mjs"
  );
  const memory = await MemoryStore.addMemory({
    memory_summary: "Test memory",
    category: "interests",
    intent: "general",
    score: 5,
    ...data,
  });
  return { MemoryStore, memory };
}

async function populateMemories() {
  const { MemoryStore, memory: memoryOne } = await addMemory({
    memory_summary: "Lorem ipsum dolor sit amet 1",
    category: "interests",
    score: 5,
  });
  const { memory: memoryTwo } = await addMemory({
    memory_summary: "Lorem ipsum dolor sit amet 2",
    category: "habits",
    score: 4,
  });

  registerCleanupFunction(async () => {
    for (const { id } of [memoryOne, memoryTwo]) {
      try {
        await MemoryStore.hardDeleteMemory(id);
      } catch (err) {
        console.error("Failed to delete memory:", id, err);
      }
    }
  });

  return { MemoryStore, memories: [memoryOne, memoryTwo] };
}

async function addChat() {
  const { ChatStore, ChatConversation, ChatMessage, MESSAGE_ROLE } =
    ChromeUtils.importESModule(
      "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
    );
  const convo = new ChatConversation({ title: "Test", description: "" });
  convo.messages = [
    new ChatMessage({
      ordinal: 0,
      role: MESSAGE_ROLE.USER,
      content: { body: "test" },
      turnIndex: 0,
    }),
  ];
  await ChatStore.updateConversation(convo);
  return { ChatStore, convo };
}

async function triggerBlockAndWaitForDialog(doc, win) {
  const setting = win.Preferences.getSetting("aiControlSmartWindowSelect");
  const dialogEl = doc.querySelector("block-ai-confirmation-dialog");
  await dialogEl.updateComplete;

  const dialogShown = BrowserTestUtils.waitForEvent(
    dialogEl.shadowRoot.querySelector("dialog"),
    "toggle"
  );
  setting.userChange("blocked");
  await dialogShown;
  await dialogEl.updateComplete;
  return { setting, dialogEl };
}

async function selectCustomModel(doc) {
  const modelSelection = doc.getElementById("modelSelection");
  await modelSelection.updateComplete;

  const customRadio = doc.querySelector(
    'moz-radio[data-l10n-id="smart-window-model-custom"]'
  );
  await BrowserTestUtils.waitForMutationCondition(
    doc.body,
    { attributes: true, childList: true, subtree: true },
    () => BrowserTestUtils.isVisible(customRadio)
  );
  customRadio.click();

  await BrowserTestUtils.waitForMutationCondition(
    doc.body,
    { attributes: true, childList: true, subtree: true },
    () => BrowserTestUtils.isVisible(doc.getElementById("customModelName"))
  );
  return doc.getElementById("customModelName");
}

/**
 * Waits for the model radio with the given l10n ID to render and returns it.
 *
 * @param {Document} doc
 * @param {string} l10nId
 * @returns {Promise<Element>}
 */
async function waitForModelRadio(doc, l10nId) {
  const modelSelection = doc.getElementById("modelSelection");
  await modelSelection.updateComplete;
  const findRadio = () =>
    modelSelection.querySelector(`moz-radio[data-l10n-id="${l10nId}"]`);
  return BrowserTestUtils.waitForMutationCondition(
    modelSelection,
    { attributes: true, childList: true, subtree: true },
    findRadio
  );
}
