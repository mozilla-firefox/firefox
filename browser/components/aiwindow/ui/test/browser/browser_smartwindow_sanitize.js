/* Any copyright is dedicated to the Public Domain.
 * https://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

const { ChatStore, ChatConversation } = ChromeUtils.importESModule(
  "moz-src:///browser/components/aiwindow/ui/modules/ChatStore.sys.mjs"
);

const PREF_SMARTWINDOW_CONSENT_TIME = "browser.smartwindow.tos.consentTime";

var now_mSec = Date.now();
var now_uSec = now_mSec * 1000;

function enableConsent() {
  Services.prefs.setIntPref(PREF_SMARTWINDOW_CONSENT_TIME, 12345);
}

function disableConsent() {
  Services.prefs.clearUserPref(PREF_SMARTWINDOW_CONSENT_TIME);
}

async function addConversation(createdDateMs, title) {
  let conversation = new ChatConversation({
    createdDate: createdDateMs,
    updatedDate: createdDateMs,
    pageUrl: "https://www.example.com",
  });
  conversation.title = title;
  conversation.addUserMessage("test content", "https://www.example.com", 0);
  await ChatStore.updateConversation(conversation);
  return conversation;
}

async function getConversationCount() {
  let results = await ChatStore.findRecentConversations(1000);
  return results.length;
}

registerCleanupFunction(async () => {
  disableConsent();
  await ChatStore.destroyDatabase();
});

describe("Sanitize chat conversations", () => {
  beforeEach(async () => {
    await ChatStore.destroyDatabase();
    enableConsent();

    // Clear storage activity recorded by earlier tests in this manifest. The
    // cookiesAndStorage range cleaner reads it via getActiveOrigins(), and a
    // Gecko bug there throws NS_ERROR_ILLEGAL_VALUE on certain stale origins
    // Fixes an intermittent test failure in:
    // "clears chats by range via browsingHistoryAndDownloads"
    Cc["@mozilla.org/storage/activity-service;1"]
      .getService(Ci.nsIStorageActivityService)
      .testOnlyReset();
  });

  afterEach(async () => {
    disableConsent();
    await ChatStore.destroyDatabase();
  });

  it("clears all chats via browsingHistoryAndDownloads", async () => {
    await addConversation(now_mSec - 60 * 60 * 1000, "one hour ago");
    await addConversation(now_mSec - 10 * 60 * 1000, "ten minutes ago");
    is(await getConversationCount(), 2, "should have 2 conversations");

    await Sanitizer.sanitize(["browsingHistoryAndDownloads"]);

    is(
      await getConversationCount(),
      0,
      "all conversations should be cleared after sanitize history"
    );
  });

  it("clears all chats via cookiesAndStorage", async () => {
    await addConversation(now_mSec - 30 * 60 * 1000, "thirty minutes ago");
    await addConversation(now_mSec - 5 * 60 * 1000, "five minutes ago");
    is(await getConversationCount(), 2, "should have 2 conversations");

    await Sanitizer.sanitize(["cookiesAndStorage"]);

    is(
      await getConversationCount(),
      0,
      "all conversations should be cleared after sanitize cookies"
    );
  });

  it("clears chats by range via browsingHistoryAndDownloads", async () => {
    await addConversation(now_mSec - 2 * 60 * 60 * 1000, "two hours ago");
    await addConversation(now_mSec - 5 * 60 * 1000, "five minutes ago");
    await addConversation(now_mSec - 1 * 60 * 1000, "one minute ago");
    is(await getConversationCount(), 3, "should have 3 conversations");

    // Clear last 10 minutes (range in microseconds).
    let range = [now_uSec - 10 * 60 * 1000000, now_uSec];
    await Sanitizer.sanitize(["browsingHistoryAndDownloads"], {
      range,
      ignoreTimespan: false,
    });

    is(
      await getConversationCount(),
      1,
      "only the conversation outside the range should remain"
    );

    let remaining = await ChatStore.findRecentConversations(10);
    is(
      remaining[0].title,
      "two hours ago",
      "the older conversation should remain"
    );
  });

  it("clears chats by range via cookiesAndStorage", async () => {
    await addConversation(now_mSec - 3 * 60 * 60 * 1000, "three hours ago");
    await addConversation(now_mSec - 2 * 60 * 1000, "two minutes ago");
    is(await getConversationCount(), 2, "should have 2 conversations");

    let range = [now_uSec - 10 * 60 * 1000000, now_uSec];
    await Sanitizer.sanitize(["cookiesAndStorage"], {
      range,
      ignoreTimespan: false,
    });

    is(
      await getConversationCount(),
      1,
      "only the conversation outside the range should remain"
    );
  });

  it("does not clear chats when AIWindow is disabled", async () => {
    disableConsent();

    await addConversation(now_mSec - 5 * 60 * 1000, "recent chat");
    is(await getConversationCount(), 1, "should have 1 conversation");

    ok(!AIWindow.isEnabled, "AIWindow should be disabled");

    await Sanitizer.sanitize(["browsingHistoryAndDownloads"]);

    is(
      await getConversationCount(),
      1,
      "conversation should remain when AIWindow is disabled"
    );
  });
});
