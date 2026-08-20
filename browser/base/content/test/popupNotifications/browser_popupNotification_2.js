/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

function test() {
  waitForExplicitFinish();

  ok(PopupNotifications, "PopupNotifications object exists");
  ok(PopupNotifications.panel, "PopupNotifications panel exists");

  setup();
}

var tests = [
  // Test optional params
  {
    id: "Test#1",
    run() {
      this.notifyObj = new BasicNotification(this.id);
      this.notifyObj.secondaryActions = undefined;
      this.notification = showNotification(this.notifyObj);
    },
    onShown(popup) {
      checkPopup(popup, this.notifyObj);
      dismissNotification(popup);
    },
    onHidden() {
      ok(
        this.notifyObj.dismissalCallbackTriggered,
        "dismissal callback triggered"
      );
      this.notification.remove();
      ok(this.notifyObj.removedCallbackTriggered, "removed callback triggered");
    },
  },
  // Test that icons appear
  {
    id: "Test#2",
    run() {
      this.notifyObj = new BasicNotification(this.id);
      this.notifyObj.id = "geolocation";
      this.notifyObj.anchorID = "geo-notification-icon";
      this.notification = showNotification(this.notifyObj);
    },
    onShown(popup) {
      checkPopup(popup, this.notifyObj);
      isnot(
        document.getElementById("geo-notification-icon").getBoundingClientRect()
          .width,
        0,
        "geo anchor should be visible"
      );
      dismissNotification(popup);
    },
    onHidden() {
      let icon = document.getElementById("geo-notification-icon");
      isnot(
        icon.getBoundingClientRect().width,
        0,
        "geo anchor should be visible after dismissal"
      );
      this.notification.remove();
      is(
        icon.getBoundingClientRect().width,
        0,
        "geo anchor should not be visible after removal"
      );
    },
  },

  // Test that persistence allows the notification to persist across reloads
  {
    id: "Test#3",
    async run() {
      this.oldSelectedTab = gBrowser.selectedTab;
      await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        // eslint-disable-next-line sdl/no-insecure-url
        "http://example.com/"
      );
      this.notifyObj = new BasicNotification(this.id);
      this.notifyObj.addOptions({
        persistence: 2,
      });
      this.notification = showNotification(this.notifyObj);
    },
    async onShown() {
      this.complete = false;
      await BrowserTestUtils.loadURIString({
        browser: gBrowser.selectedTab.linkedBrowser,
        // eslint-disable-next-line sdl/no-insecure-url
        uriString: "http://example.org/",
      });
      await BrowserTestUtils.loadURIString({
        browser: gBrowser.selectedTab.linkedBrowser,
        // eslint-disable-next-line sdl/no-insecure-url
        uriString: "http://example.com/",
      });
      // Next load will remove the notification
      this.complete = true;
      await BrowserTestUtils.loadURIString({
        browser: gBrowser.selectedTab.linkedBrowser,
        // eslint-disable-next-line sdl/no-insecure-url
        uriString: "http://example.org/",
      });
    },
    onHidden() {
      ok(
        this.complete,
        "Should only have hidden the notification after 3 page loads"
      );
      ok(this.notifyObj.removedCallbackTriggered, "removal callback triggered");
      gBrowser.removeTab(gBrowser.selectedTab);
      gBrowser.selectedTab = this.oldSelectedTab;
    },
  },
  // Test that a timeout allows the notification to persist across reloads
  {
    id: "Test#4",
    async run() {
      this.oldSelectedTab = gBrowser.selectedTab;
      await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        // eslint-disable-next-line sdl/no-insecure-url
        "http://example.com/"
      );
      this.notifyObj = new BasicNotification(this.id);
      // Set a timeout of 10 minutes that should never be hit
      this.notifyObj.addOptions({
        timeout: Date.now() + 600000,
      });
      this.notification = showNotification(this.notifyObj);
    },
    async onShown() {
      this.complete = false;
      await BrowserTestUtils.loadURIString({
        browser: gBrowser.selectedTab.linkedBrowser,
        // eslint-disable-next-line sdl/no-insecure-url
        uriString: "http://example.org/",
      });
      await BrowserTestUtils.loadURIString({
        browser: gBrowser.selectedTab.linkedBrowser,
        // eslint-disable-next-line sdl/no-insecure-url
        uriString: "http://example.com/",
      });
      // Next load will hide the notification
      this.notification.options.timeout = Date.now() - 1;
      this.complete = true;
      await BrowserTestUtils.loadURIString({
        browser: gBrowser.selectedTab.linkedBrowser,
        // eslint-disable-next-line sdl/no-insecure-url
        uriString: "http://example.org/",
      });
    },
    onHidden() {
      ok(
        this.complete,
        "Should only have hidden the notification after the timeout was passed"
      );
      this.notification.remove();
      gBrowser.removeTab(gBrowser.selectedTab);
      gBrowser.selectedTab = this.oldSelectedTab;
    },
  },
  // Test that setting persistWhileVisible allows a visible notification to
  // persist across location changes
  {
    id: "Test#5",
    async run() {
      this.oldSelectedTab = gBrowser.selectedTab;
      await BrowserTestUtils.openNewForegroundTab(
        gBrowser,
        // eslint-disable-next-line sdl/no-insecure-url
        "http://example.com/"
      );
      this.notifyObj = new BasicNotification(this.id);
      this.notifyObj.addOptions({
        persistWhileVisible: true,
      });
      this.notification = showNotification(this.notifyObj);
    },
    async onShown(popup) {
      this.complete = false;

      await BrowserTestUtils.loadURIString({
        browser: gBrowser.selectedTab.linkedBrowser,
        // eslint-disable-next-line sdl/no-insecure-url
        uriString: "http://example.org/",
      });
      await BrowserTestUtils.loadURIString({
        browser: gBrowser.selectedTab.linkedBrowser,
        // eslint-disable-next-line sdl/no-insecure-url
        uriString: "http://example.com/",
      });
      // Notification should persist across location changes
      this.complete = true;
      dismissNotification(popup);
    },
    onHidden() {
      ok(
        this.complete,
        "Should only have hidden the notification after it was dismissed"
      );
      this.notification.remove();
      gBrowser.removeTab(gBrowser.selectedTab);
      gBrowser.selectedTab = this.oldSelectedTab;
    },
  },

  // Test that nested icon nodes correctly activate popups
  {
    id: "Test#6",
    run() {
      // Add a temporary box as the anchor with a button
      this.box = document.createXULElement("box");
      PopupNotifications.iconBox.appendChild(this.box);

      let button = document.createXULElement("button");
      button.setAttribute("label", "Please click me!");
      this.box.appendChild(button);

      // The notification should open up on the box
      this.notifyObj = new BasicNotification(this.id);
      this.notifyObj.anchorID = this.box.id = "nested-box";
      this.notifyObj.addOptions({ dismissed: true });
      this.notification = showNotification(this.notifyObj);

      // This test places a normal button in the notification area, which has
      // standard GTK styling and dimensions. Due to the clip-path, this button
      // gets clipped off, which makes it necessary to synthesize the mouse click
      // a little bit downward. To be safe, we click at center.
      EventUtils.synthesizeMouseAtCenter(button, {});
    },
    onShown(popup) {
      checkPopup(popup, this.notifyObj);
      dismissNotification(popup);
    },
    onHidden() {
      this.notification.remove();
      this.box.remove();
    },
  },
  // Test that popupnotifications without popups have anchor icons shown
  {
    id: "Test#7",
    async run() {
      let notifyObj = new BasicNotification(this.id);
      notifyObj.anchorID = "geo-notification-icon";
      notifyObj.addOptions({ neverShow: true });
      let promiseTopic = TestUtils.topicObserved(
        "PopupNotifications-updateNotShowing"
      );
      showNotification(notifyObj);
      await promiseTopic;
      isnot(
        document.getElementById("geo-notification-icon").getBoundingClientRect()
          .width,
        0,
        "geo anchor should be visible"
      );
      goNext();
    },
  },
  // Test that autoplay media icon is shown
  {
    id: "Test#8",
    async run() {
      let notifyObj = new BasicNotification(this.id);
      notifyObj.anchorID = "autoplay-media-notification-icon";
      notifyObj.addOptions({ neverShow: true });
      let promiseTopic = TestUtils.topicObserved(
        "PopupNotifications-updateNotShowing"
      );
      showNotification(notifyObj);
      await promiseTopic;
      isnot(
        document
          .getElementById("autoplay-media-notification-icon")
          .getBoundingClientRect().width,
        0,
        "autoplay media icon should be visible"
      );
      goNext();
    },
  },
  // Test notification close button
  {
    id: "Test#9",
    run() {
      this.notifyObj = new BasicNotification(this.id);
      this.notification = showNotification(this.notifyObj);
    },
    onShown(popup) {
      checkPopup(popup, this.notifyObj);
      let notification = popup.children[0];
      EventUtils.synthesizeMouseAtCenter(notification.closebutton, {});
    },
    onHidden() {
      ok(
        this.notifyObj.dismissalCallbackTriggered,
        "dismissal callback triggered"
      );
      this.notification.remove();
      ok(this.notifyObj.removedCallbackTriggered, "removed callback triggered");
      ok(
        !this.notifyObj.secondaryActionClicked,
        "secondary action not clicked"
      );
    },
  },
];
