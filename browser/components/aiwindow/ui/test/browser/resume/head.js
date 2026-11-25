/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from ../head.js */
Services.scriptloader.loadSubScript(
  new URL("../head.js", getRootDirectory(gTestPath)).href,
  this
);

const RESUME_ACTIVITY_PREFS = [
  ["browser.smartwindow.memories.generateFromConversation", true],
  ["browser.smartwindow.memories.generateFromHistory", true],
];
const DAY_MS = 24 * 60 * 60 * 1000;

// The confirmation card attached when resuming the first journey from
// stubResumeActivityGeneration.
const EXPECTED_RESUME_TOOL_UI_DATA = {
  uiType: "tab-group-confirmation",
  toolCallId: "resume-activity-memory-1",
  isResumeActivity: true,
  properties: {
    actionType: "open_tabs",
    tabGroupLabel: "Your research",
    tabs: [1, 2, 3, 4].map((id, index) => ({
      token: String(index),
      url: `https://example.com/${id}`,
      title: `Example ${id}`,
      iconSrc: `page-icon:https://example.com/${id}`,
      checked: false,
    })),
  },
};

/**
 * @param {Element} aiWindow
 * @returns {Promise<{section: Element, card: Element}>} The resume section and
 *   its first rendered card.
 */
async function waitForResumeCard(aiWindow) {
  const section = await TestUtils.waitForCondition(
    () => aiWindow.shadowRoot.querySelector("smartwindow-resume-section"),
    "Wait for smartwindow-resume-section element"
  );
  const card = await TestUtils.waitForCondition(
    () => section.shadowRoot.querySelector("smartwindow-resume-card"),
    "Wait for a resume card to render"
  );
  return { section, card };
}

/**
 * A tab that a test replaces can still record events for its own page, so only
 * count the ones from the page under test.
 *
 * @param {string} metric - The smart_window event metric's Glean name
 * @param {Element} aiWindow
 * @returns {object[]} The metric's events recorded for the page.
 */
function pageEvents(metric, aiWindow) {
  const chatId = String(aiWindow.conversationId);
  return (Glean.smartWindow[metric].testGetValue() ?? []).filter(
    event => event.extra.chat_id === chatId
  );
}

/**
 * Clicks an element the way a user would, with mouse events at its center.
 *
 * @param {Element} element
 */
function synthesizeClick(element) {
  element.scrollIntoView({ block: "center" });
  EventUtils.synthesizeMouseAtCenter(element, {}, element.documentGlobal);
}

/**
 * Checks the most recent event recorded for a resume card or section metric.
 *
 * @param {"resumeCardAction"|"resumeSectionAction"} metric
 * @param {Element} aiWindow
 * @param {object} expected - Extras beyond the shared telemetry context and
 *   the ages
 * @param {string} message
 * @param {object} [ages] - Expected ages for a resume card event, as taken by
 *   checkAges
 */
function assertResumeAction(metric, aiWindow, expected, message, ages) {
  const event = pageEvents(metric, aiWindow).at(-1);
  Assert.ok(event, `${message}: an action event was recorded`);
  Assert.deepEqual(
    ages ? checkAges(event.extra, ages) : event.extra,
    {
      chat_id: String(aiWindow.conversationId),
      location: "fullpage",
      message_seq: "0",
      ...expected,
    },
    message
  );
}

/**
 * Checks a resume card event's memory_age and last_visit_age, which grow with
 * the time between the stub running and the event being recorded, and
 * returns the other extras for an exact comparison.
 *
 * @param {object} extra - The event's extras
 * @param {object} ages
 * @param {number} [ages.memoryAge] - The memory's age, in ms, when the stub
 *   ran, or undefined if the memory has no created_at
 * @param {number} ages.lastVisitAge - The newest preview tab visit's age, in
 *   ms, when the stub ran
 * @param {number} ages.since - A Date.now() value taken before the stub ran
 * @returns {object}
 */
function checkAges(extra, { memoryAge, lastVisitAge, since }) {
  const { memory_age, last_visit_age, ...rest } = extra;
  const elapsed = Date.now() - since;
  if (memoryAge === undefined) {
    Assert.equal(memory_age, undefined, "memory_age is not recorded");
  } else {
    Assert.greaterOrEqual(Number(memory_age), memoryAge, "memory_age minimum");
    Assert.lessOrEqual(
      Number(memory_age),
      memoryAge + elapsed,
      "memory_age maximum"
    );
  }
  Assert.greaterOrEqual(
    Number(last_visit_age),
    lastVisitAge,
    "last_visit_age minimum"
  );
  Assert.lessOrEqual(
    Number(last_visit_age),
    lastVisitAge + elapsed,
    "last_visit_age maximum"
  );
  return rest;
}
