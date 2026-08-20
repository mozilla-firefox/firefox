/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { PrivateBrowsingUtils } from "resource://gre/modules/PrivateBrowsingUtils.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const NOTIFICATION_EVENT_DISMISSED = "dismissed";
const NOTIFICATION_EVENT_REMOVED = "removed";
const NOTIFICATION_EVENT_SHOWING = "showing";
const NOTIFICATION_EVENT_SHOWN = "shown";
const NOTIFICATION_EVENT_SWAPPING = "swapping";

const ICON_SELECTOR = ".notification-anchor-icon";
const ICON_ATTRIBUTE_SHOWING = "showing";
const ICON_ANCHOR_ATTRIBUTE = "popupnotificationanchor";

const PREF_SECURITY_DELAY = "security.notification_enable_delay";
const FULLSCREEN_TRANSITION_TIME_SHOWN_OFFSET_MS = 2000;
// Caps how far rejected button clicks during the security delay can push the
// effective deadline. Without a cap, continuous rapid clicking resets the
// countdown faster than it elapses and the user can never dismiss the popup
// (see bug 2035581). With this multiplier, the wait is bounded at
// SECURITY_DELAY_EXTENSION_CAP_MULTIPLIER * the configured delay.
const SECURITY_DELAY_EXTENSION_CAP_MULTIPLIER = 20;

// Enumerated values for the POPUP_NOTIFICATION_STATS telemetry histogram.
const TELEMETRY_STAT_OFFERED = 0;
const TELEMETRY_STAT_ACTION_1 = 1;
const TELEMETRY_STAT_ACTION_2 = 2;
// const TELEMETRY_STAT_ACTION_3 = 3;
const TELEMETRY_STAT_ACTION_LAST = 4;
// const TELEMETRY_STAT_DISMISSAL_CLICK_ELSEWHERE = 5;
const TELEMETRY_STAT_REMOVAL_LEAVE_PAGE = 6;
// const TELEMETRY_STAT_DISMISSAL_CLOSE_BUTTON = 7;
const TELEMETRY_STAT_OPEN_SUBMENU = 10;
const TELEMETRY_STAT_LEARN_MORE = 11;

const TELEMETRY_STAT_REOPENED_OFFSET = 20;
const lazy = {};

XPCOMUtils.defineLazyPreferenceGetter(lazy, "buttonDelay", PREF_SECURITY_DELAY);

var popupNotificationsMap = new WeakMap();
var gNotificationParents = new WeakMap();

function getAnchorFromBrowser(aBrowser, aAnchorID) {
  let attrPrefix = aAnchorID ? aAnchorID.replace("notification-icon", "") : "";
  let anchor =
    aBrowser.getAttribute(attrPrefix + ICON_ANCHOR_ATTRIBUTE) ||
    aBrowser[attrPrefix + ICON_ANCHOR_ATTRIBUTE] ||
    aBrowser.getAttribute(ICON_ANCHOR_ATTRIBUTE) ||
    aBrowser[ICON_ANCHOR_ATTRIBUTE];
  if (anchor) {
    if (ChromeUtils.getClassName(anchor) == "XULElement") {
      return anchor;
    }
    return aBrowser.ownerDocument.getElementById(anchor);
  }
  return null;
}

/**
 * Given a DOM node inside a <popupnotification>, return the parent <popupnotification>.
 */
function getNotificationFromElement(aElement) {
  return aElement.closest("popupnotification");
}

/**
 * Returns true if the given browser element belongs to a sidebar.
 */
function isSidebarBrowser(aBrowser) {
  let sidebarBrowser =
    aBrowser?.browsingContext?.topChromeWindow?.SidebarController?.browser;

  if (!sidebarBrowser) {
    return false;
  }

  let nestedSidebarBrowsers =
    sidebarBrowser.contentDocument?.querySelectorAll("browser");
  return Array.from(nestedSidebarBrowsers).some(b => b === aBrowser);
}

/**
 * Notification object describes a single popup notification.
 *
 * @see PopupNotifications.show()
 */
function Notification(
  id,
  message,
  anchorID,
  mainAction,
  secondaryActions,
  browser,
  owner,
  options
) {
  this.id = id;
  this.message = message;
  this.anchorID = anchorID;
  this.mainAction = mainAction;
  this.secondaryActions = secondaryActions || [];
  this.browser = browser;
  this.owner = owner;
  this.options = options || {};

  this._dismissed = false;
  // Will become a boolean when manually toggled by the user.
  this._checkboxChecked = null;
  this.wasDismissed = false;
  this.recordedTelemetryStats = new Set();
  this.isPrivate = PrivateBrowsingUtils.isWindowPrivate(
    this.browser.documentGlobal
  );
  this.timeCreated = ChromeUtils.now();
}

Notification.prototype = {
  id: null,
  message: null,
  anchorID: null,
  mainAction: null,
  secondaryActions: null,
  browser: null,
  owner: null,
  options: null,
  timeShown: null,
  // Snapshot of `timeShown` taken every time the notification is (re)shown,
  // before any rejected-click extensions have been applied. Reassigned on
  // every show, reshow, popup-positioned re-anchor, _extendSecurityDelay
  // call (fullscreen transition / pointer lock), and the fallback in
  // _onButtonEvent -- it is NOT a record of the first-ever show. Used by
  // _onButtonEvent as the lower bound for the cap on how far rejected clicks
  // can push `timeShown` forward (see SECURITY_DELAY_EXTENSION_CAP_MULTIPLIER).
  timeShownWithoutClickExtensions: null,

  /**
   * Indicates whether the notification is currently dismissed.
   */
  set dismissed(value) {
    this._dismissed = value;
    if (value) {
      // Keep the dismissal into account when recording telemetry.
      this.wasDismissed = true;
    }
  },
  get dismissed() {
    return this._dismissed;
  },

  /**
   * Removes the notification and updates the popup accordingly if needed.
   */
  remove: function Notification_remove() {
    this.owner.remove(this);
  },

  get anchorElement() {
    let iconBox = this.owner.iconBox;

    let anchorElement = getAnchorFromBrowser(this.browser, this.anchorID);
    if (!iconBox) {
      return anchorElement;
    }

    if (!anchorElement && this.anchorID) {
      anchorElement = iconBox.querySelector("#" + this.anchorID);
    }

    // Sidebar special case:
    // Sidebar notifications use anchors inside the sidebar document rather than
    // the main browser document. Prefer the sidebar’s anchor when available.
    if (!anchorElement && isSidebarBrowser(this.browser)) {
      const sidebarBrowser =
        this.browser.browsingContext?.topChromeWindow?.SidebarController
          ?.browser;
      iconBox = sidebarBrowser.contentDocument.getElementById(`${iconBox.id}`);
      anchorElement = iconBox.querySelector("#" + this.anchorID);
    }

    // Use a default anchor icon if it's available
    if (!anchorElement) {
      anchorElement =
        iconBox.querySelector("#default-notification-icon") || iconBox;
    }

    return anchorElement;
  },

  reshow() {
    this.owner._reshowNotifications(this.anchorElement, this.browser);
  },

  /**
   * Adds a value to the specified metric, that must be labeled by ID.
   */
  _recordTelemetry(metricName, value) {
    if (this.isPrivate && !this.options.recordTelemetryInPrivateBrowsing) {
      // The reason why we don't record telemetry in private windows is because
      // the available actions can be different from regular mode. The main
      // difference is that all of the persistent permission options like
      // "Always remember" aren't there, so they really need to be handled
      // separately to avoid skewing results. For notifications with the same
      // choices, there would be no reason not to record in private windows as
      // well, but it's just simpler to use the same check for everything.
      return;
    }
    let metric = Glean.popupNotification[metricName];
    metric["(all)"].accumulateSingleSample(value);
    metric[this.id].accumulateSingleSample(value);
  },

  /**
   * Adds an enumerated value to the POPUP_NOTIFICATION_STATS histogram,
   * ensuring that it is recorded at most once for each distinct Notification.
   *
   * Statistics for reopened notifications are recorded in separate buckets.
   *
   * @param value
   *        One of the TELEMETRY_STAT_ constants.
   */
  _recordTelemetryStat(value) {
    if (this.wasDismissed) {
      value += TELEMETRY_STAT_REOPENED_OFFSET;
    }
    if (!this.recordedTelemetryStats.has(value)) {
      this.recordedTelemetryStats.add(value);
      this._recordTelemetry("stats", value);
    }
  },
};

/**
 * The PopupNotifications object manages popup notifications for a given browser
 * window.
 *
 * @param tabbrowser
 *        window's TabBrowser. Used to observe tab switching events and
 *        for determining the active browser element.
 * @param panel
 *        The <xul:panel/> element to use for notifications. The panel is
 *        populated with <popupnotification> children and displayed it as
 *        needed.
 * @param iconBox
 *        Reference to a container element that should be hidden or
 *        unhidden when notifications are hidden or shown. It should be the
 *        parent of anchor elements whose IDs are passed to show().
 *        It is used as a fallback popup anchor if notifications specify
 *        invalid or non-existent anchor IDs.
 * @param options
 *        An optional object with the following optional properties:
 *        {
 *          shouldSuppress:
 *            If this function returns true, then all notifications are
 *            suppressed for this window. This state is checked on construction
 *            and when the "anchorVisibilityChange" method is called.
 *          getVisibleAnchorElement(anchorElement):
 *            A function which takes an anchor element as input and should return
 *            either the anchor if it's visible, a fallback anchor element, or if
 *            no fallback exists, a null element.
 *        }
 */
export function PopupNotifications(tabbrowser, panel, iconBox, options = {}) {
  if (!tabbrowser) {
    throw new Error("Invalid tabbrowser");
  }
  if (iconBox && ChromeUtils.getClassName(iconBox) != "XULElement") {
    throw new Error("Invalid iconBox");
  }
  if (ChromeUtils.getClassName(panel) != "XULPopupElement") {
    throw new Error("Invalid panel");
  }

  this._shouldSuppress = options.shouldSuppress || (() => false);
  this._suppress = this._shouldSuppress();

  this._getVisibleAnchorElement = options.getVisibleAnchorElement;

  this.window = tabbrowser.documentGlobal;
  this.panel = panel;
  this.tabbrowser = tabbrowser;
  this.iconBox = iconBox;

  // panel itself has a listener in the bubble phase and this listener
  // needs to be called after that, so use bubble phase here.
  this.panel.addEventListener("popuphidden", this);
  this.panel.addEventListener("popuppositioned", this);
  this.panel.classList.add("popup-notification-panel", "panel-no-padding");

  // This listener will be attached to the chrome window whenever a notification
  // is showing, to allow the user to dismiss notifications using the escape key.
  this._handleWindowKeyPress = aEvent => {
    if (aEvent.keyCode != aEvent.DOM_VK_ESCAPE) {
      return;
    }

    // Esc key cancels the topmost notification, if there is one.
    let notification = this.panel.firstElementChild;
    if (!notification) {
      return;
    }

    let doc = this.window.document;
    let focusedElement = Services.focus.focusedElement;

    // Go through shadow roots so that focus delegated into a shadow DOM
    // (e.g., moz-button with delegatesFocus:true) is still treated as inside
    // the notification.
    let focusedInsideNotification = false;
    for (let el = focusedElement; el; el = el.parentNode ?? el.host) {
      if (el === notification) {
        focusedInsideNotification = true;
        break;
      }
    }
    if (
      !focusedElement ||
      focusedElement == doc.body ||
      focusedElement == this.tabbrowser.selectedBrowser ||
      focusedInsideNotification
    ) {
      let escAction = notification.notification.options.escAction;
      this._onButtonEvent(aEvent, escAction, "esc-press", notification);
      // Without this preventDefault call, the event will be sent to the content page
      // and our event listener might be called again after receiving a reply from
      // the content process, which could accidentally dismiss another notification.
      aEvent.preventDefault();
    }
  };

  let isFullscreen = !!this.window.document.fullscreenElement;

  this.panel.setAttribute("followanchor", !isFullscreen);

  // There are no anchor icons in DOM fullscreen mode, but we would
  // still like to show the popup notification. To avoid an infinite
  // loop of showing and hiding, we have to disable followanchor
  // (which hides the element without an anchor) in fullscreen.
  this.window.addEventListener(
    "MozDOMFullscreen:Entered",
    () => {
      this.panel.setAttribute("followanchor", "false");
    },
    true
  );
  this.window.addEventListener(
    "MozDOMFullscreen:Exited",
    () => {
      this.panel.setAttribute("followanchor", "true");
    },
    true
  );

  Services.obs.addObserver(this, "fullscreen-transition-start");
  Services.obs.addObserver(this, "pointer-lock-entered");

  this.window.addEventListener("unload", () => {
    Services.obs.removeObserver(this, "fullscreen-transition-start");
    Services.obs.removeObserver(this, "pointer-lock-entered");
  });

  this.window.addEventListener("activate", this, true);
  if (this.tabbrowser.tabContainer) {
    this.tabbrowser.tabContainer.addEventListener("TabSelect", this, true);

    this.tabbrowser.tabContainer.addEventListener("TabClose", aEvent => {
      // If the tab was just closed and we have notifications associated with it,
      // then the notifications were closed because of the tab removal. We need to
      // record this event in telemetry and fire the removal callback.
      this.nextRemovalReason = TELEMETRY_STAT_REMOVAL_LEAVE_PAGE;
      let notifications = this.getNotificationsForBrowser(
        aEvent.target.linkedBrowser
      );
      for (let notification of notifications) {
        this._fireCallback(
          notification,
          NOTIFICATION_EVENT_REMOVED,
          this.nextRemovalReason,
          /* withoutUserResponse = */ true
        );
        notification._recordTelemetryStat(this.nextRemovalReason);
      }
    });
  }
}

PopupNotifications.prototype = {
  window: null,
  panel: null,
  tabbrowser: null,

  _iconBox: null,
  set iconBox(iconBox) {
    // Remove the listeners on the old iconBox, if needed
    if (this._iconBox) {
      this._iconBox.removeEventListener("click", this);
      this._iconBox.removeEventListener("keypress", this);
    }
    this._iconBox = iconBox;
    if (iconBox) {
      iconBox.addEventListener("click", this);
      iconBox.addEventListener("keypress", this);
    }
  },
  get iconBox() {
    return this._iconBox;
  },

  observe(subject, topic) {
    // These observers apply to all windows.
    if (
      topic == "fullscreen-transition-start" ||
      topic == "pointer-lock-entered"
    ) {
      // Extend security delay if the panel is open.
      if (this.isPanelOpen) {
        let notification = this.panel.firstChild?.notification;
        if (notification) {
          this._extendSecurityDelay([notification]);
        }
      }
    }
  },

  /**
   * Retrieve one or many Notification object/s associated with the browser/ID pair.
   *
   * @param {string|string[]} id
   *        The Notification ID or an array of IDs to search for.
   * @param [browser]
   *        The browser whose notifications should be searched. If null, the
   *        currently selected browser's notifications will be searched.
   *
   * @returns {Notification|Notification[]|null} If passed a single id, returns the corresponding Notification object, or null if no such
   *          notification exists.
   *          If passed an id array, returns an array of Notification objects which match the ids.
   */
  getNotification: function PopupNotifications_getNotification(id, browser) {
    let notifications = this.getNotificationsForBrowser(
      browser || this.tabbrowser.selectedBrowser
    );
    if (Array.isArray(id)) {
      return notifications.filter(x => id.includes(x.id));
    }
    return notifications.find(x => x.id == id) || null;
  },

  /**
   * Adds a new popup notification.
   *
   * @param browser
   *        The <xul:browser> element associated with the notification. Must not
   *        be null.
   * @param id
   *        A unique ID that identifies the type of notification (e.g.
   *        "geolocation"). Only one notification with a given ID can be visible
   *        at a time. If a notification already exists with the given ID, it
   *        will be replaced.
   * @param message
   *        A string containing the text to be displayed as the notification
   *        header.  The string may optionally contain one or two "<>" as a
   *        placeholder which is later replaced by a host name or an addon name
   *        that is formatted to look bold, in which case the options.name
   *        property (as well as options.secondName if passing a "<>" and a "{}"
   *        placeholder) needs to be specified. "<>" will be considered as the
   *        first and "{}" as the second placeholder.
   * @param anchorID
   *        The ID of the element that should be used as this notification
   *        popup's anchor. May be null, in which case the notification will be
   *        anchored to the iconBox.
   * @param mainAction
   *        A JavaScript object literal describing the notification button's
   *        action. If present, it must have the following properties:
   *          - label (string): the button's label.
   *          - accessKey (string): the button's accessKey.
   *          - callback (function): a callback to be invoked when the button is
   *            pressed, is passed an object that contains the following fields:
   *              - checkboxChecked: (boolean) If the optional checkbox is checked.
   *              - source: (string): the source of the action that initiated the
   *                callback, either:
   *                - "button" if popup buttons were directly activated, or
   *                - "esc-press" if the user pressed the escape key, or
   *                - "menucommand" if a menu was activated.
   *          - [optional] dismiss (boolean): If this is true, the notification
   *            will be dismissed instead of removed after running the callback.
   *          - [optional] disabled (boolean): If this is true, the button
   *            will be disabled.
   *          - [optional] disableSecurityDelay (boolean): If this is true,
   *            this action skips the clickjacking security delay. Use only
   *            for actions whose semantics cannot harm the user (e.g.
   *            "Cancel", "Don't Allow", "Block"). Never set this for actions
   *            that grant a permission, install software, or otherwise
   *            expose attack surface.
   *        If null, the notification will have a default "OK" action button
   *        that can be used to dismiss the popup and secondaryActions will be ignored.
   * @param secondaryActions
   *        An optional JavaScript array describing the notification's alternate
   *        actions. The array should contain objects with the same properties
   *        as mainAction. These are used to populate the notification button's
   *        dropdown menu.
   * @param options
   *        An options JavaScript object holding additional properties for the
   *        notification. The following properties are currently supported:
   *        persistence: An integer. The notification will not automatically
   *                     dismiss for this many page loads.
   *        timeout:     A time in milliseconds. The notification will not
   *                     automatically dismiss before this time.
   *        persistWhileVisible:
   *                     A boolean. If true, a visible notification will always
   *                     persist across location changes.
   *        lowerPanelLevel:
   *                     A boolean. If true, the panel's popup level is lowered
   *                     to "parent" so a popup hosted inside the notification
   *                     (such as an autocomplete dropdown) can stack above the
   *                     panel instead of behind it.
   *        persistent:  A boolean. If true, the notification will always
   *                     persist even across tab and app changes (but not across
   *                     location changes), until the user accepts or rejects
   *                     the request. The notification will never be implicitly
   *                     dismissed.
   *        dismissed:   Whether the notification should be added as a dismissed
   *                     notification. Dismissed notifications can be activated
   *                     by clicking on their anchorElement.
   *        autofocus:   Whether the notification should be autofocused on
   *                     showing, stealing focus from any other focused element.
   *        eventCallback:
   *                     Callback to be invoked when the notification changes
   *                     state. The callback's first argument is a string
   *                     identifying the state change:
   *                     "dismissed": notification has been dismissed by the
   *                                  user (e.g. by clicking away or switching
   *                                  tabs)
   *                     "removed": notification has been removed (due to
   *                                location change or user action)
   *                     "showing": notification is about to be shown
   *                                (this can be fired multiple times as
   *                                 notifications are dismissed and re-shown)
   *                                If the callback returns true, the notification
   *                                will be dismissed.
   *                     "shown": notification has been shown (this can be fired
   *                              multiple times as notifications are dismissed
   *                              and re-shown)
   *                     "swapping": the docshell of the browser that created
   *                                 the notification is about to be swapped to
   *                                 another browser. A second parameter contains
   *                                 the browser that is receiving the docshell,
   *                                 so that the event callback can transfer stuff
   *                                 specific to this notification.
   *                                 If the callback returns true, the notification
   *                                 will be moved to the new browser.
   *                                 If the callback isn't implemented, returns false,
   *                                 or doesn't return any value, the notification
   *                                 will be removed.
   *        neverShow:   Indicate that no popup should be shown for this
   *                     notification. Useful for just showing the anchor icon.
   *        removeOnDismissal:
   *                     Notifications with this parameter set to true will be
   *                     removed when they would have otherwise been dismissed
   *                     (i.e. any time the popup is closed due to user
   *                     interaction).
   *        hideClose:   Indicate that the little close button in the corner of
   *                     the panel should be hidden.
   *        checkbox:    An object that allows you to add a checkbox and
   *                     control its behavior with these fields:
   *                       label:
   *                         (required) Label to be shown next to the checkbox.
   *                       checked:
   *                         (optional) Whether the checkbox should be checked
   *                         by default. Defaults to false.
   *                       checkedState:
   *                         (optional) An object that allows you to customize
   *                         the notification state when the checkbox is checked.
   *                           disableMainAction:
   *                             (optional) Whether the mainAction is disabled.
   *                             Defaults to false.
   *                           warningLabel:
   *                             (optional) A (warning) text that is shown below the
   *                             checkbox. Pass null to hide.
   *                       uncheckedState:
   *                         (optional) An object that allows you to customize
   *                         the notification state when the checkbox is not checked.
   *                         Has the same attributes as checkedState.
   *        popupIconClass:
   *                     A string. A class (or space separated list of classes)
   *                     that will be applied to the icon in the popup so that
   *                     several notifications using the same panel can use
   *                     different icons.
   *        popupIconURL:
   *                     A string. URL of the image to be displayed in the popup.
   *        learnMoreURL:
   *                     A string URL. Setting this property will make the
   *                     prompt display a "Learn More" link that, when clicked,
   *                     opens the URL in a new tab.
   *        displayURI:
   *                     The nsIURI of the page the notification came
   *                     from. If present, this will be displayed above the message.
   *                     If the nsIURI represents a file, the path will be displayed,
   *                     otherwise the hostPort will be displayed.
   *        name:
   *                     An optional string formatted to look bold and used in the
   *                     notifiation description header text. Usually a host name or
   *                     addon name.
   *        secondName:
   *                     An optional string formatted to look bold and used in the
   *                     notification description header text. Usually a host name or
   *                     addon name. This is similar to name, and only used in case
   *                     where message contains a "<>" and a "{}" placeholder. "<>"
   *                     is considered the first and "{}" is considered the second
   *                     placeholder.
   *        escAction:
   *                     An optional string indicating the action to take when the
   *                     Esc key is pressed. This should be set to the name of the
   *                     command to run. If not provided, "secondarybuttoncommand"
   *                     will be used.
   *        extraAttr:
   *                     An optional string value which will be given to the
   *                     extraAttr attribute on the notification's anchorElement
   *        popupOptions:
   *                     An optional object containing popup options passed to
   *                     `openPopup()` when defined.
   *        recordTelemetryInPrivateBrowsing:
   *                     An optional boolean indicating whether popup telemetry
   *                     should be recorded in private browsing windows. By default,
   *                     telemetry is NOT recorded in PBM, because the available
   *                     options for persistent permission notifications are
   *                     different between normal and PBM windows, potentially
   *                     skewing the data. But for notifications that do not differ
   *                     in PBM, this option can be used to ensure that popups in
   *                     both PBM and normal windows record the same interactions.
   * @returns the Notification object corresponding to the added notification.
   */
  show: function PopupNotifications_show(
    browser,
    id,
    message,
    anchorID,
    mainAction,
    secondaryActions,
    options
  ) {
    function isInvalidAction(a) {
      return (
        !a || !(typeof a.callback == "function") || !a.label || !a.accessKey
      );
    }

    if (!browser) {
      throw new Error("PopupNotifications_show: invalid browser");
    }
    if (!id) {
      throw new Error("PopupNotifications_show: invalid ID");
    }
    if (mainAction && isInvalidAction(mainAction)) {
      throw new Error("PopupNotifications_show: invalid mainAction");
    }
    if (secondaryActions && secondaryActions.some(isInvalidAction)) {
      throw new Error("PopupNotifications_show: invalid secondaryActions");
    }

    let notification = new Notification(
      id,
      message,
      anchorID,
      mainAction,
      secondaryActions,
      browser,
      this,
      options
    );

    if (options) {
      let escAction = options.escAction;
      if (
        escAction != "buttoncommand" &&
        escAction != "secondarybuttoncommand"
      ) {
        escAction = "secondarybuttoncommand";
      }
      notification.options.escAction = escAction;
    }

    if (options && options.dismissed) {
      notification.dismissed = true;
    }

    let existingNotification = this.getNotification(id, browser);
    if (existingNotification) {
      this._remove(existingNotification, /* withoutUserResponse = */ true);
    }

    let notifications = this.getNotificationsForBrowser(browser);
    notifications.push(notification);

    let isActiveBrowser = this._isActiveBrowser(browser);
    let isActiveWindow = Services.focus.activeWindow == this.window;

    if (isActiveBrowser) {
      if (isActiveWindow) {
        // show panel now
        this._update(
          notifications,
          new Set([notification.anchorElement]),
          true
        );
      } else {
        // indicate attention and update the icon if necessary
        if (!notification.dismissed) {
          this.window.getAttention();
        }
        this._updateAnchorIcons(
          notifications,
          this._getAnchorsForNotifications(
            notifications,
            notification.anchorElement
          )
        );
        this._notify("backgroundShow");
      }
    } else {
      // Notify observers that we're not showing the popup (useful for testing)
      this._notify("backgroundShow");
    }

    return notification;
  },

  /**
   * Returns true if the notification popup is currently being displayed.
   */
  get isPanelOpen() {
    let panelState = this.panel.state;

    return panelState == "showing" || panelState == "open";
  },

  /**
   * Called by the consumer to indicate that the open panel should
   * temporarily be hidden while the given panel is showing.
   */
  suppressWhileOpen(panel) {
    this._hidePanel().catch(console.error);
    panel.addEventListener("popuphidden", () => {
      this._update();
    });
  },

  /**
   * Called by the consumer to indicate that a browser's location has changed,
   * so that we can update the active notifications accordingly.
   */
  locationChange: function PopupNotifications_locationChange(aBrowser) {
    if (!aBrowser) {
      throw new Error("PopupNotifications_locationChange: invalid browser");
    }

    let notifications = this.getNotificationsForBrowser(aBrowser);

    this.nextRemovalReason = TELEMETRY_STAT_REMOVAL_LEAVE_PAGE;

    notifications = notifications.filter(function (notification) {
      // The persistWhileVisible option allows an open notification to persist
      // across location changes
      if (notification.options.persistWhileVisible && this.isPanelOpen) {
        if (
          "persistence" in notification.options &&
          notification.options.persistence
        ) {
          notification.options.persistence--;
        }
        return true;
      }

      // The persistence option allows a notification to persist across multiple
      // page loads
      if (
        "persistence" in notification.options &&
        notification.options.persistence
      ) {
        notification.options.persistence--;
        return true;
      }

      // The timeout option allows a notification to persist until a certain time
      if (
        "timeout" in notification.options &&
        Date.now() <= notification.options.timeout
      ) {
        return true;
      }

      notification._recordTelemetryStat(this.nextRemovalReason);
      this._fireCallback(
        notification,
        NOTIFICATION_EVENT_REMOVED,
        this.nextRemovalReason,
        /* withoutUserResponse = */ true
      );
      return false;
    }, this);

    this._setNotificationsForBrowser(aBrowser, notifications);

    if (this._isActiveBrowser(aBrowser)) {
      this.anchorVisibilityChange();
    }
  },

  /**
   * Called by the consumer to indicate that the visibility of the notification
   * anchors may have changed, but the location has not changed. This also
   * checks whether all notifications are suppressed for this window.
   *
   * Calling this method may result in the "showing" and "shown" events for
   * visible notifications to be invoked even if the anchor has not changed.
   */
  anchorVisibilityChange() {
    let suppress = this._shouldSuppress();
    if (!suppress) {
      // If notifications are not suppressed, always update the visibility.
      this._suppress = false;
      let notifications = this.getNotificationsForBrowser(
        this.tabbrowser.selectedBrowser
      );
      this._update(
        notifications,
        this._getAnchorsForNotifications(
          notifications,
          getAnchorFromBrowser(this.tabbrowser.selectedBrowser)
        )
      );
      return;
    }

    // Notifications are suppressed, ensure that the panel is hidden.
    if (!this._suppress) {
      this._suppress = true;
      this._hidePanel().catch(console.error);
    }
  },

  /**
   * Removes one or many Notifications.
   *
   * @param {Notification|Notification[]} notification - The Notification object/s to remove.
   * @param {boolean} [withoutUserResponse] - Whether the removal happens without the user
   *  responding to the notification via an action button or menu item. Consumers that
   *  represent pending user requests (e.g. permission prompts) should treat a true value
   *  as a signal to abandon the request.
   */
  remove: function PopupNotifications_remove(
    notification,
    withoutUserResponse = false
  ) {
    let notificationArray = Array.isArray(notification)
      ? notification
      : [notification];
    let activeBrowser;

    notificationArray.forEach(n => {
      this._remove(n, withoutUserResponse);
      if (!activeBrowser && this._isActiveBrowser(n.browser)) {
        activeBrowser = n.browser;
      }
    });

    if (activeBrowser) {
      let browserNotifications = this.getNotificationsForBrowser(activeBrowser);
      this._update(browserNotifications);
    }
  },

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "popuphidden":
        this._onPopupHidden(aEvent);
        break;
      case "activate":
      case "popuppositioned":
        if (this.isPanelOpen) {
          for (let elt of this.panel.children) {
            let now = ChromeUtils.now();
            elt.notification.timeShown = Math.max(
              now,
              elt.notification.timeShown ?? 0
            );
            elt.notification.timeShownWithoutClickExtensions =
              elt.notification.timeShown;
          }
          break;
        }
      // fall through
      case "TabSelect":
        // setTimeout(..., 0) needed, otherwise openPopup from "activate" event
        // handler results in the popup being hidden again for some reason...
        this.window.setTimeout(() => {
          this._suppress = this._shouldSuppress();
          this._update();
        }, 0);
        break;
      case "click":
      case "keypress":
        this._onIconBoxCommand(aEvent);
        break;
    }
  },

  // Utility methods

  _ignoreDismissal: null,
  _currentAnchorElement: null,
  _popupshownListener: null,
  _popupshownListenerTarget: null,

  _clearPopupshownListener() {
    if (this._popupshownListener) {
      this._popupshownListenerTarget.removeEventListener(
        "popupshown",
        this._popupshownListener,
        true
      );
      this._popupshownListener = null;
      this._popupshownListenerTarget = null;
    }
  },

  /**
   * Gets notifications for the currently selected browser.
   */
  get _currentNotifications() {
    return this.tabbrowser.selectedBrowser
      ? this.getNotificationsForBrowser(this.tabbrowser.selectedBrowser)
      : [];
  },

  _remove: function PopupNotifications_removeHelper(
    notification,
    withoutUserResponse = false
  ) {
    // This notification may already be removed, in which case let's just fail
    // silently.
    let notifications = this.getNotificationsForBrowser(notification.browser);
    if (!notifications) {
      return;
    }

    var index = notifications.indexOf(notification);
    if (index == -1) {
      return;
    }

    if (this._isActiveBrowser(notification.browser)) {
      notification.anchorElement.removeAttribute(ICON_ATTRIBUTE_SHOWING);
    }

    // remove the notification
    notifications.splice(index, 1);
    this._fireCallback(
      notification,
      NOTIFICATION_EVENT_REMOVED,
      this.nextRemovalReason,
      withoutUserResponse
    );
  },

  /**
   * Dismisses the notification without removing it.
   *
   * @param {Event} the event associated with the user interaction that
   *                caused the dismissal
   * @param {boolean} whether to disable persistent status. Normally,
   *                  persistent prompts can not be dismissed. You can
   *                  use this argument to force dismissal.
   */
  _dismiss: function PopupNotifications_dismiss(
    event,
    disablePersistent = false
  ) {
    if (disablePersistent) {
      let notificationEl = getNotificationFromElement(event.target);
      if (notificationEl) {
        notificationEl.notification.options.persistent = false;
      }
    }

    let browser =
      this.panel.firstElementChild &&
      this.panel.firstElementChild.notification.browser;
    this.panel.hidePopup();
    // Focus the browser if it's still selected by the time we dismiss.
    if (browser && this.tabbrowser.selectedBrowser === browser) {
      browser.focus();
    }
  },

  /**
   * Hides the notification popup.
   */
  _hidePanel: function PopupNotifications_hide() {
    if (this.panel.state == "closed") {
      return Promise.resolve();
    }
    if (this._ignoreDismissal) {
      return this._ignoreDismissal.promise;
    }
    let deferred = Promise.withResolvers();
    this._ignoreDismissal = deferred;
    this.panel.hidePopup();
    return deferred.promise;
  },

  /**
   * Removes all notifications from the notification popup.
   */
  _clearPanel() {
    let popupnotification;
    while ((popupnotification = this.panel.lastElementChild)) {
      this.panel.removeChild(popupnotification);

      // If this notification was provided by the chrome document rather than
      // created ad hoc, move it back to where we got it from.
      let originalParent = gNotificationParents.get(popupnotification);
      if (originalParent) {
        popupnotification.notification = null;

        // Re-hide the notification such that it isn't rendered in the chrome
        // document. _refreshPanel will unhide it again when needed.
        popupnotification.hidden = true;

        originalParent.appendChild(popupnotification);
      }
    }
  },

  /**
   * Formats the notification description message before we display it
   * and splits it into three parts if the message contains "<>" as
   * placeholder.
   *
   * param notification
   *       The Notification object which contains the message to format.
   *
   * @returns a Javascript object that has the following properties:
   * start: A start label string containing the first part of the message.
   *        It may contain the whole string if the description message
   *        does not have "<>" as a placeholder. For example, local
   *        file URIs with description messages that don't display hostnames.
   * name:  A string that is formatted to look bold. It replaces the
   *        placeholder with the options.name property from the notification
   *        object which is usually an addon name or a host name.
   * end:   The last part of the description message.
   */
  _formatDescriptionMessage(n) {
    let text = {};
    let array = n.message.split(/<>|{}/);
    text.start = array[0] || "";
    text.name = n.options.name || "";
    text.end = array[1] || "";
    if (array.length == 3) {
      text.secondName = n.options.secondName || "";
      text.secondEnd = array[2] || "";

      // name and secondName should be in logical positions.  Swap them in case
      // the second placeholder came before the first one in the original string.
      if (n.message.indexOf("{}") < n.message.indexOf("<>")) {
        let tmp = text.name;
        text.name = text.secondName;
        text.secondName = tmp;
      }
    } else if (array.length > 3) {
      console.error(
        "Unexpected array length encountered in " +
          "_formatDescriptionMessage: ",
        array.length
      );
    }
    return text;
  },

  _refreshPanel: function PopupNotifications_refreshPanel(notificationsToShow) {
    this._clearPanel();

    notificationsToShow.forEach(function (n) {
      let doc = this.window.document;

      // Append "-notification" to the ID to try to avoid ID conflicts with other stuff
      // in the document.
      let popupnotificationID = n.id + "-notification";

      // If the chrome document provides a popupnotification with this id, use
      // that. Otherwise create it ad-hoc.
      let popupnotification = doc.getElementById(popupnotificationID);
      if (popupnotification) {
        gNotificationParents.set(
          popupnotification,
          popupnotification.parentNode
        );
      } else {
        popupnotification = doc.createXULElement("popupnotification");
      }

      // Create the notification description element.
      let desc = this._formatDescriptionMessage(n);
      popupnotification.setAttribute("label", desc.start);
      popupnotification.setAttribute("name", desc.name);
      popupnotification.setAttribute("endlabel", desc.end);
      if ("secondName" in desc && "secondEnd" in desc) {
        popupnotification.setAttribute("secondname", desc.secondName);
        popupnotification.setAttribute("secondendlabel", desc.secondEnd);
      } else {
        popupnotification.removeAttribute("secondname");
        popupnotification.removeAttribute("secondendlabel");
      }

      if (n.options.hintText) {
        popupnotification.setAttribute("hinttext", n.options.hintText);
      } else {
        popupnotification.removeAttribute("hinttext");
      }

      popupnotification.setAttribute("id", popupnotificationID);
      popupnotification.setAttribute("popupid", n.id);

      popupnotification.addEventListener("command", event =>
        this._onCommand(event)
      );

      popupnotification.toggleAttribute(
        "hasicon",
        !!(n.options.popupIconURL || n.options.popupIconClass)
      );

      if (n.mainAction) {
        popupnotification.setAttribute("buttonlabel", n.mainAction.label);
        popupnotification.setAttribute(
          "buttonaccesskey",
          n.mainAction.accessKey
        );
      } else {
        // Enable the default button to let the user close the popup if the close button is hidden
        popupnotification.toggleAttribute("buttonhighlight", true);
        popupnotification.removeAttribute("buttonlabel");
        popupnotification.removeAttribute("buttonaccesskey");
      }

      let classes = "popup-notification-icon";
      if (n.options.popupIconClass) {
        classes += " " + n.options.popupIconClass;
      }
      popupnotification.setAttribute("iconclass", classes);

      if (n.options.popupIconURL) {
        popupnotification.setAttribute("icon", n.options.popupIconURL);
      } else {
        popupnotification.removeAttribute("icon");
      }

      if (n.options.learnMoreURL) {
        popupnotification.setAttribute("learnmoreurl", n.options.learnMoreURL);
      } else {
        popupnotification.removeAttribute("learnmoreurl");
      }

      if (n.options.displayURI) {
        let uri;
        try {
          if (n.options.displayURI instanceof Ci.nsIFileURL) {
            uri = n.options.displayURI.pathQueryRef;
          } else {
            try {
              uri = n.options.displayURI.hostPort;
            } catch (e) {
              uri = n.options.displayURI.spec;
            }
          }
          popupnotification.setAttribute("origin", uri);
        } catch (e) {
          console.error(e);
          popupnotification.removeAttribute("origin");
        }
      } else {
        popupnotification.removeAttribute("origin");
      }

      popupnotification.toggleAttribute(
        "closebuttonhidden",
        !!n.options.hideClose
      );

      popupnotification.notification = n;
      let menuitems = [];

      const hasSecondaryActions = n.mainAction && n.secondaryActions.length;
      if (hasSecondaryActions) {
        let telemetryStatId = TELEMETRY_STAT_ACTION_2;

        let secondaryAction = n.secondaryActions[0];
        popupnotification.setAttribute(
          "secondarybuttonlabel",
          secondaryAction.label
        );
        popupnotification.setAttribute(
          "secondarybuttonaccesskey",
          secondaryAction.accessKey
        );

        for (let i = 1; i < n.secondaryActions.length; i++) {
          let action = n.secondaryActions[i];
          let item = doc.createXULElement("menuitem");
          item.setAttribute("label", action.label);
          item.setAttribute("accesskey", action.accessKey);
          item.notification = n;
          item.action = action;

          menuitems.push(item);

          // We can only record a limited number of actions in telemetry. If
          // there are more, the latest are all recorded in the last bucket.
          item.action.telemetryStatId = telemetryStatId;
          if (telemetryStatId < TELEMETRY_STAT_ACTION_LAST) {
            telemetryStatId++;
          }
        }
      }
      popupnotification.toggleAttribute(
        "secondarybuttonhidden",
        !hasSecondaryActions
      );
      popupnotification.toggleAttribute(
        "dropmarkerhidden",
        n.secondaryActions.length < 2
      );

      let checkbox = n.options.checkbox;
      if (checkbox && checkbox.label) {
        let checked =
          n._checkboxChecked != null ? n._checkboxChecked : !!checkbox.checked;
        popupnotification.checkboxState = {
          checked,
          label: checkbox.label,
        };

        if (checked) {
          this._setNotificationUIState(
            popupnotification,
            checkbox.checkedState
          );
        } else {
          this._setNotificationUIState(
            popupnotification,
            checkbox.uncheckedState
          );
        }
      } else {
        popupnotification.checkboxState = null;
        // Reset the UI state to avoid previous state bleeding into this prompt.
        this._setNotificationUIState(popupnotification);
      }

      this.panel.appendChild(popupnotification);

      // The popupnotification may be hidden if we got it from the chrome
      // document rather than creating it ad hoc.
      popupnotification.show();

      popupnotification.menupopup.textContent = "";
      popupnotification.menupopup.append(...menuitems);
    }, this);
  },

  _setNotificationUIState(notification, state = {}) {
    let mainAction = notification.notification.mainAction;
    notification.toggleAttribute(
      "mainactiondisabled",
      (mainAction && mainAction.disabled) ||
        state.disableMainAction ||
        notification.hasAttribute("invalidselection")
    );
    if (state.warningLabel) {
      notification.setAttribute("warninglabel", state.warningLabel);
      notification.removeAttribute("warninghidden");
    } else {
      notification.setAttribute("warninghidden", "true");
    }
  },

  _extendSecurityDelay(notifications) {
    let now = ChromeUtils.now();
    notifications.forEach(n => {
      n.timeShown = now + FULLSCREEN_TRANSITION_TIME_SHOWN_OFFSET_MS;
      n.timeShownWithoutClickExtensions = n.timeShown;
    });
  },

  /**
   * Lowers the panel's popup level to "parent" when a notification hosts its
   * own popup (e.g. the autocomplete dropdown in the password doorhanger), so
   * that popup can stack above the panel. The level is only read when the
   * popup widget is created, so changing the attribute recreates the widget.
   *
   * @param {Array} notificationsToShow
   *        The notifications about to be shown in the panel.
   */
  _updatePanelLevel(notificationsToShow) {
    if (notificationsToShow.some(n => n.options.lowerPanelLevel)) {
      this.panel.setAttribute("level", "parent");
    } else {
      this.panel.removeAttribute("level");
    }
  },

  _showPanel: function PopupNotifications_showPanel(
    notificationsToShow,
    anchorElement
  ) {
    this.panel.hidden = false;

    notificationsToShow = notificationsToShow.filter(n => {
      if (anchorElement != n.anchorElement) {
        return false;
      }

      let dismiss = this._fireCallback(n, NOTIFICATION_EVENT_SHOWING);
      if (dismiss) {
        n.dismissed = true;
      }
      return !dismiss;
    });
    if (!notificationsToShow.length) {
      return;
    }

    let notificationIds = notificationsToShow.map(n => n.id);

    this._refreshPanel(notificationsToShow);

    // The element the PopupNotification should anchor to might not be visible.
    // Check its visibility using a callback that returns the same anchor
    // element if its visible, or a fallback option that is visible.
    // If no fallbacks are visible, it should return null.
    if (this._getVisibleAnchorElement) {
      anchorElement = this._getVisibleAnchorElement(anchorElement);
    }
    // In case _getVisibleAnchorElement provided a non-visible element.
    if (!anchorElement?.checkVisibility()) {
      // We only ever show notifications for the current browser,
      // so we can just use the current tab.
      anchorElement = this.tabbrowser.selectedTab;
      if (!anchorElement?.checkVisibility()) {
        // If we're in an entirely chromeless environment, set the anchorElement
        // to null and let openPopup show the notification at (0,0) later.
        anchorElement = null;
      }
    }

    if (this.isPanelOpen && this._currentAnchorElement == anchorElement) {
      // isPanelOpen is also true while the panel is animating open, in which
      // case a popupshown listener is still pending. It holds the notifications
      // that were current when the panel started opening, some of which we may
      // just have replaced, and firing "shown" for those would let their
      // callbacks overwrite the panel contents. We fire "shown" for the current
      // notifications right here, so the pending listener has nothing left to do.
      this._clearPopupshownListener();

      notificationsToShow.forEach(function (n) {
        // If the panel is already open remember the time the notification was
        // shown for the security delay.
        n.timeShown = Math.max(ChromeUtils.now(), n.timeShown ?? 0);
        n.timeShownWithoutClickExtensions = n.timeShown;
        this._fireCallback(n, NOTIFICATION_EVENT_SHOWN);
      }, this);

      // Make sure we update the noautohide attribute on the panel, in case it changed.
      if (notificationsToShow.some(n => n.options.persistent)) {
        this.panel.setAttribute("noautohide", "true");
      } else {
        this.panel.removeAttribute("noautohide");
      }

      this._updatePanelLevel(notificationsToShow);

      // Let tests know that the panel was updated and what notifications it was
      // updated with so that tests can wait for the correct notifications to be
      // added.
      let event = new this.window.CustomEvent("PanelUpdated", {
        detail: notificationIds,
      });
      this.panel.dispatchEvent(event);
      return;
    }

    // If the panel is already open but we're changing anchors, we need to hide
    // it first.  Otherwise it can appear in the wrong spot.  (_hidePanel is
    // safe to call even if the panel is already hidden.)
    this._hidePanel().then(() => {
      this._currentAnchorElement = anchorElement;

      if (notificationsToShow.some(n => n.options.persistent)) {
        this.panel.setAttribute("noautohide", "true");
      } else {
        this.panel.removeAttribute("noautohide");
      }

      // Autofocus the panel if any notification being shown requests focus.
      // Done here when the panel is actually opened, rather than
      // in show(), since a notification may be shown asynchronously (e.g. after the
      // window/browser becomes active again following a navigation). This path
      // reliably runs before openPopup() in every case.
      if (notificationsToShow.some(n => !n.dismissed && n.options.autofocus)) {
        this.panel.removeAttribute("noautofocus");
      } else {
        this.panel.setAttribute("noautofocus", "true");
      }

      this._updatePanelLevel(notificationsToShow);

      notificationsToShow.forEach(function (n) {
        // Record that the notification was actually displayed on screen.
        // Notifications that were opened a second time or that were originally
        // shown with "options.dismissed" will be recorded in a separate bucket.
        n._recordTelemetryStat(TELEMETRY_STAT_OFFERED);
      }, this);

      // We're about to open the panel while in a full screen transition or
      // during pointer lock. Extend the security delay to avoid clickjacking.
      if (
        this.window.isInFullScreenTransition ||
        this.window.PointerLock?.isActive
      ) {
        this._extendSecurityDelay(notificationsToShow);
      }

      this._clearPopupshownListener();
      // NOTIFICATION_EVENT_SHOWN should be fired for the panel before
      // anyone listening for popupshown on the panel gets run. Otherwise,
      // the panel will not be initialized when the popupshown event
      // listeners run.
      // By targeting the panel's parent and using a capturing listener, we
      // can have our listener called before others waiting for the panel to
      // be shown (which probably expect the panel to be fully initialized)
      let target = this.panel.parentNode || this.panel;
      this._popupshownListener = function () {
        this._clearPopupshownListener();

        notificationsToShow.forEach(function (n) {
          // The panel has been opened, remember the time the notification was
          // shown for the security delay.
          n.timeShown = Math.max(ChromeUtils.now(), n.timeShown ?? 0);
          n.timeShownWithoutClickExtensions = n.timeShown;
          this._fireCallback(n, NOTIFICATION_EVENT_SHOWN);
        }, this);
        // These notifications are used by tests to know when all the processing
        // required to display the panel has happened.
        this.panel.dispatchEvent(new this.window.CustomEvent("Shown"));
        let event = new this.window.CustomEvent("PanelUpdated", {
          detail: notificationIds,
        });
        this.panel.dispatchEvent(event);
      };
      this._popupshownListener = this._popupshownListener.bind(this);
      this._popupshownListenerTarget = target;
      target.addEventListener("popupshown", this._popupshownListener, true);

      let popupOptions = notificationsToShow.findLast(
        n => n.options?.popupOptions
      )?.options?.popupOptions;
      if (popupOptions) {
        this.panel.openPopup(anchorElement, popupOptions);
      } else {
        this.panel.openPopup(anchorElement, "bottomleft topleft", 0, 0);
      }
    });
  },

  /**
   * Updates the notification state in response to window activation or tab
   * selection changes.
   *
   * @param notifications an array of Notification instances. if null,
   *                      notifications will be retrieved off the current
   *                      browser tab
   * @param anchors       is a XUL element or a Set of XUL elements that the
   *                      notifications panel(s) will be anchored to.
   * @param dismissShowing if true, dismiss any currently visible notifications
   *                       if there are no notifications to show. Otherwise,
   *                       currently displayed notifications will be left alone.
   */
  _update: function PopupNotifications_update(
    notifications,
    anchors = new Set(),
    dismissShowing = false
  ) {
    if (ChromeUtils.getClassName(anchors) == "XULElement") {
      anchors = new Set([anchors]);
    }

    if (!notifications) {
      notifications = this._currentNotifications;
    }

    let haveNotifications = !!notifications.length;
    if (!anchors.size && haveNotifications) {
      anchors = this._getAnchorsForNotifications(notifications);
    }

    let useIconBox = !!this.iconBox;
    if (useIconBox && anchors.size) {
      for (let anchor of anchors) {
        if (anchor.parentNode == this.iconBox) {
          continue;
        }
        useIconBox = false;
        break;
      }
    }

    // Filter out notifications that have been dismissed, unless they are
    // persistent. Also check if we should not show any notification.
    let notificationsToShow = [];
    if (!this._suppress) {
      notificationsToShow = notifications.filter(
        n => (!n.dismissed || n.options.persistent) && !n.options.neverShow
      );
    }

    if (useIconBox) {
      // Hide icons of the previous tab.
      this._hideIcons();
    }

    if (haveNotifications) {
      // Also filter out notifications that are for a different anchor.
      notificationsToShow = notificationsToShow.filter(function (n) {
        return anchors.has(n.anchorElement);
      });

      if (useIconBox) {
        this._showIcons(notifications);
        this.iconBox.hidden = false;
        // Make sure that panels can only be attached to anchors of shown
        // notifications inside an iconBox.
        anchors = this._getAnchorsForNotifications(notificationsToShow);
      } else if (anchors.size) {
        this._updateAnchorIcons(notifications, anchors);
      }
    }

    if (notificationsToShow.length) {
      let anchorElement = anchors.values().next().value;
      if (anchorElement) {
        this._showPanel(notificationsToShow, anchorElement);
      }

      // Setup a capturing event listener on the whole window to catch the
      // escape key while persistent notifications are visible.
      this.window.addEventListener(
        "keypress",
        this._handleWindowKeyPress,
        true
      );
    } else {
      // Notify observers that we're not showing the popup (useful for testing)
      this._notify("updateNotShowing");

      // Close the panel if there are no notifications to show.
      // When called from PopupNotifications.show() we should never close the
      // panel, however. It may just be adding a dismissed notification, in
      // which case we want to continue showing any existing notifications.
      if (!dismissShowing) {
        this._dismiss();
      }

      // Only hide the iconBox if we actually have no notifications (as opposed
      // to not having any showable notifications)
      if (!haveNotifications) {
        if (useIconBox) {
          this.iconBox.hidden = true;
        } else if (anchors.size) {
          for (let anchorElement of anchors) {
            anchorElement.removeAttribute(ICON_ATTRIBUTE_SHOWING);
          }
        }
      }

      // Stop listening to keyboard events for notifications.
      this.window.removeEventListener(
        "keypress",
        this._handleWindowKeyPress,
        true
      );
    }
  },

  _updateAnchorIcons: function PopupNotifications_updateAnchorIcons(
    notifications,
    anchorElements
  ) {
    for (let anchorElement of anchorElements) {
      anchorElement.setAttribute(ICON_ATTRIBUTE_SHOWING, "true");
    }
  },

  _showIcons: function PopupNotifications_showIcons(aCurrentNotifications) {
    for (let notification of aCurrentNotifications) {
      let anchorElm = notification.anchorElement;
      if (anchorElm) {
        anchorElm.setAttribute(ICON_ATTRIBUTE_SHOWING, "true");

        if (notification.options.extraAttr) {
          anchorElm.setAttribute("extraAttr", notification.options.extraAttr);
        } else {
          anchorElm.removeAttribute("extraAttr");
        }
      }
    }
  },

  _hideIcons: function PopupNotifications_hideIcons() {
    let icons = this.iconBox.querySelectorAll(ICON_SELECTOR);
    for (let icon of icons) {
      icon.removeAttribute(ICON_ATTRIBUTE_SHOWING);
    }
  },

  /**
   * Gets and sets notifications for the browser.
   */
  getNotificationsForBrowser: function PopupNotifications_getNotifications(
    browser
  ) {
    let notifications = popupNotificationsMap.get(browser);
    if (!notifications) {
      // Initialize the WeakMap for the browser so callers can reference/manipulate the array.
      notifications = [];
      popupNotificationsMap.set(browser, notifications);
    }
    return notifications;
  },
  _setNotificationsForBrowser: function PopupNotifications_setNotifications(
    browser,
    notifications
  ) {
    popupNotificationsMap.set(browser, notifications);
    return notifications;
  },

  _getAnchorsForNotifications:
    function PopupNotifications_getAnchorsForNotifications(
      notifications,
      defaultAnchor
    ) {
      let anchors = new Set();
      for (let notification of notifications) {
        if (notification.anchorElement) {
          anchors.add(notification.anchorElement);
        }
      }
      if (defaultAnchor && !anchors.size) {
        anchors.add(defaultAnchor);
      }
      return anchors;
    },

  _isActiveBrowser(browser) {
    if (isSidebarBrowser(browser)) {
      // Sidebar browser is always active for its notifications
      return true;
    }
    // We compare on frameLoader instead of just comparing the
    // selectedBrowser and browser directly because browser tabs in
    // Responsive Design Mode put the actual web content into a
    // mozbrowser iframe and proxy property read/write and method
    // calls from the tab to that iframe. This is so that attempts
    // to reload the tab end up reloading the content in
    // Responsive Design Mode, and not the Responsive Design Mode
    // viewer itself.
    //
    // This means that PopupNotifications can come up from a browser
    // in Responsive Design Mode, but the selectedBrowser will not match
    // the browser being passed into this function, despite the browser
    // actually being within the selected tab. We workaround this by
    // comparing frameLoader instead, which is proxied from the outer
    // <xul:browser> to the inner mozbrowser <iframe>.
    return this.tabbrowser.selectedBrowser.frameLoader == browser.frameLoader;
  },

  _onIconBoxCommand: function PopupNotifications_onIconBoxCommand(event) {
    // Left click, space or enter only
    let type = event.type;
    if (type == "click" && event.button != 0) {
      return;
    }

    if (
      type == "keypress" &&
      !(
        event.charCode == event.DOM_VK_SPACE ||
        event.keyCode == event.DOM_VK_RETURN
      )
    ) {
      return;
    }

    if (!this._currentNotifications.length) {
      return;
    }

    event.stopPropagation();

    // Get the anchor that is the immediate child of the icon box
    let anchor = event.target;
    while (anchor && anchor.parentNode != this.iconBox) {
      anchor = anchor.parentNode;
    }

    if (!anchor) {
      return;
    }

    // If the panel is not closed, and the anchor is different, immediately mark all
    // active notifications for the previous anchor as dismissed
    if (this.panel.state != "closed" && anchor != this._currentAnchorElement) {
      this._dismissOrRemoveCurrentNotifications();
    }

    // Avoid reshowing notifications that are already shown and have not been dismissed.
    if (this.panel.state == "closed" || anchor != this._currentAnchorElement) {
      // As soon as the panel is shown, focus the first element in the selected notification.
      this.panel.addEventListener(
        "popupshown",
        () =>
          this.window.document.commandDispatcher.advanceFocusIntoSubtree(
            this.panel
          ),
        { once: true }
      );

      this._reshowNotifications(anchor);
    } else {
      // Focus the first element in the selected notification.
      this.window.document.commandDispatcher.advanceFocusIntoSubtree(
        this.panel
      );
    }
  },

  _reshowNotifications: function PopupNotifications_reshowNotifications(
    anchor,
    browser
  ) {
    // Mark notifications anchored to this anchor as un-dismissed
    browser = browser || this.tabbrowser.selectedBrowser;
    let notifications = this.getNotificationsForBrowser(browser);
    notifications.forEach(function (n) {
      if (n.anchorElement == anchor) {
        n.dismissed = false;
      }
    });

    if (this._isActiveBrowser(browser)) {
      // ...and then show them.
      this._update(notifications, anchor);
    }
  },

  _swapBrowserNotifications:
    function PopupNotifications_swapBrowserNoficications(
      ourBrowser,
      otherBrowser
    ) {
      // When swaping browser docshells (e.g. dragging tab to new window) we need
      // to update our notification map.

      let ourNotifications = this.getNotificationsForBrowser(ourBrowser);
      let other = otherBrowser.documentGlobal.PopupNotifications;
      if (!other) {
        if (ourNotifications.length) {
          console.error(
            "unable to swap notifications: otherBrowser doesn't support notifications"
          );
        }
        return;
      }
      let otherNotifications = other.getNotificationsForBrowser(otherBrowser);
      if (ourNotifications.length < 1 && otherNotifications.length < 1) {
        // No notification to swap.
        return;
      }

      otherNotifications = otherNotifications.filter(n => {
        if (this._fireCallback(n, NOTIFICATION_EVENT_SWAPPING, ourBrowser)) {
          n.browser = ourBrowser;
          n.owner = this;
          return true;
        }
        other._fireCallback(
          n,
          NOTIFICATION_EVENT_REMOVED,
          this.nextRemovalReason,
          /* withoutUserResponse = */ true
        );
        return false;
      });

      ourNotifications = ourNotifications.filter(n => {
        if (this._fireCallback(n, NOTIFICATION_EVENT_SWAPPING, otherBrowser)) {
          n.browser = otherBrowser;
          n.owner = other;
          return true;
        }
        this._fireCallback(
          n,
          NOTIFICATION_EVENT_REMOVED,
          this.nextRemovalReason,
          /* withoutUserResponse = */ true
        );
        return false;
      });

      this._setNotificationsForBrowser(otherBrowser, ourNotifications);
      other._setNotificationsForBrowser(ourBrowser, otherNotifications);

      if (otherNotifications.length) {
        this._update(otherNotifications);
      }
      if (ourNotifications.length) {
        other._update(ourNotifications);
      }
    },

  _fireCallback: function PopupNotifications_fireCallback(n, event, ...args) {
    try {
      if (n.options.eventCallback) {
        return n.options.eventCallback.call(n, event, ...args);
      }
    } catch (error) {
      console.error(error);
    }
    return undefined;
  },

  _onPopupHidden: function PopupNotifications_onPopupHidden(event) {
    if (event.target != this.panel) {
      return;
    }

    // If the panel was hidden before popupshown fired, clean up the listener
    // so it doesn't leak references to notifications and their associated
    // windows.
    this._clearPopupshownListener();

    // It's possible that a popupnotification set `aria-describedby` on the
    // panel element in its eventCallback function. If so, we'll clear that out
    // before showing the next notification.
    this.panel.removeAttribute("aria-describedby");

    // We may have removed the "noautofocus" attribute before showing the panel
    // if the notification specified it wants to autofocus on first show.
    // When the panel is closed, we have to restore the attribute to its default
    // value, so we don't autofocus it if it's subsequently opened from a different code path.
    this.panel.setAttribute("noautofocus", "true");

    // Handle the case where the panel was closed programmatically.
    if (this._ignoreDismissal) {
      this._ignoreDismissal.resolve();
      this._ignoreDismissal = null;
      return;
    }

    this._dismissOrRemoveCurrentNotifications();

    this._clearPanel();

    this._update();
  },

  _dismissOrRemoveCurrentNotifications() {
    let browser =
      this.panel.firstElementChild &&
      this.panel.firstElementChild.notification.browser;
    if (!browser) {
      return;
    }

    let notifications = this.getNotificationsForBrowser(browser);
    // Mark notifications as dismissed and call dismissal callbacks
    for (let nEl of this.panel.children) {
      let notificationObj = nEl.notification;
      // Never call a dismissal handler on a notification that's been removed.
      if (!notifications.includes(notificationObj)) {
        return;
      }

      // Record the time of the first notification dismissal if the main action
      // was not triggered in the meantime.
      let timeSinceShown = ChromeUtils.now() - notificationObj.timeShown;
      if (
        !notificationObj.wasDismissed &&
        !notificationObj.recordedTelemetryMainAction
      ) {
        notificationObj._recordTelemetry("dismissal", timeSinceShown);
      }

      // Do not mark the notification as dismissed or fire NOTIFICATION_EVENT_DISMISSED
      // if the notification is removed.
      if (notificationObj.options.removeOnDismissal) {
        notificationObj._recordTelemetryStat(this.nextRemovalReason);
        this._remove(notificationObj, /* withoutUserResponse = */ true);
      } else {
        notificationObj.dismissed = true;
        this._fireCallback(notificationObj, NOTIFICATION_EVENT_DISMISSED);
      }
    }
  },

  _onCheckboxCommand(event) {
    let notificationEl = getNotificationFromElement(event.originalTarget);
    let checked = notificationEl.checkbox.checked;
    let notification = notificationEl.notification;

    // Save checkbox state to be able to persist it when re-opening the doorhanger.
    notification._checkboxChecked = checked;

    if (checked) {
      this._setNotificationUIState(
        notificationEl,
        notification.options.checkbox.checkedState
      );
    } else {
      this._setNotificationUIState(
        notificationEl,
        notification.options.checkbox.uncheckedState
      );
    }
    event.stopPropagation();
  },

  _onCommand(event) {
    // Ignore events from buttons as they are submitting and so don't need checks
    if (event.originalTarget.localName == "button") {
      return;
    }
    let notificationEl = getNotificationFromElement(event.target);

    let notification = notificationEl.notification;
    if (!notification.options.checkbox) {
      this._setNotificationUIState(notificationEl);
      return;
    }

    if (notificationEl.checkbox.checked) {
      this._setNotificationUIState(
        notificationEl,
        notification.options.checkbox.checkedState
      );
    } else {
      this._setNotificationUIState(
        notificationEl,
        notification.options.checkbox.uncheckedState
      );
    }
  },

  _onButtonEvent(event, type, source = "button", notificationEl = null) {
    if (!notificationEl) {
      notificationEl = getNotificationFromElement(event.originalTarget);
    }

    if (!notificationEl) {
      throw new Error(
        "PopupNotifications._onButtonEvent: couldn't find notification element"
      );
    }

    if (!notificationEl.notification) {
      throw new Error(
        "PopupNotifications._onButtonEvent: couldn't find notification"
      );
    }

    let notification = notificationEl.notification;

    // Resolve the action being invoked up front so we can honor per-action
    // flags such as disableSecurityDelay before the security-delay check.
    let action = notification.mainAction;
    let telemetryStatId = TELEMETRY_STAT_ACTION_1;
    if (type == "secondarybuttoncommand") {
      action = notification.secondaryActions?.[0];
      telemetryStatId = TELEMETRY_STAT_ACTION_2;
    }

    // Receiving a button event means the notification should have been shown.
    // Make sure that timeShown is always set to ensure we don't break the
    // security delay calculation below.
    if (!notification.timeShown) {
      console.warn(
        "_onButtonEvent: notification.timeShown is unset. Setting to now.",
        notification
      );
      notification.timeShown = ChromeUtils.now();
      notification.timeShownWithoutClickExtensions = notification.timeShown;
    }

    if (type == "dropmarkerpopupshown") {
      notification._recordTelemetryStat(TELEMETRY_STAT_OPEN_SUBMENU);
      return;
    }

    if (type == "learnmoreclick") {
      notification._recordTelemetryStat(TELEMETRY_STAT_LEARN_MORE);
      return;
    }

    if (type == "buttoncommand") {
      // Record the total timing of the main action since the notification was
      // created, even if the notification was dismissed in the meantime.
      let timeSinceCreated = ChromeUtils.now() - notification.timeCreated;
      if (!notification.recordedTelemetryMainAction) {
        notification.recordedTelemetryMainAction = true;
        notification._recordTelemetry("mainAction", timeSinceCreated);
      }
    }

    if (type == "buttoncommand" || type == "secondarybuttoncommand") {
      // TODO: Bug 1892756.
      if (
        Services.focus.activeWindow != this.window ||
        notificationEl.matches(":-moz-window-inactive")
      ) {
        Services.console.logStringMessage(
          "PopupNotifications._onButtonEvent: " +
            "Button click happened before the window was focused / active"
        );
        this.window.focus();
        return;
      }

      // Actions that opt out of the security delay (e.g. Cancel/Block) are
      // not a clickjacking target and should fire immediately.
      if (!action?.disableSecurityDelay) {
        let now = ChromeUtils.now();
        let timeSinceShown = now - notification.timeShown;
        if (timeSinceShown < lazy.buttonDelay) {
          Services.console.logStringMessage(
            "PopupNotifications._onButtonEvent: " +
              "Button click happened before the security delay: " +
              timeSinceShown +
              "ms"
          );
          // Cap how far rejected clicks can push the deadline so continuous
          // rapid clicking can never extend the wait beyond
          // SECURITY_DELAY_EXTENSION_CAP_MULTIPLIER * the configured delay.
          let cap =
            (notification.timeShownWithoutClickExtensions ??
              notification.timeShown) +
            SECURITY_DELAY_EXTENSION_CAP_MULTIPLIER * lazy.buttonDelay;
          notification.timeShown = Math.min(
            Math.max(now, notification.timeShown),
            cap
          );
          return;
        }
      }
    }

    notification._recordTelemetryStat(telemetryStatId);

    if (action) {
      try {
        action.callback.call(undefined, {
          checkboxChecked: notificationEl.checkbox.checked,
          source,
          event,
        });
      } catch (error) {
        console.error(error);
      }

      if (action.dismiss) {
        this._dismiss();
        return;
      }
    }

    this._remove(notification);
    this._update();
  },

  _onMenuCommand: function PopupNotifications_onMenuCommand(event) {
    let target = event.originalTarget;
    if (!target.action || !target.notification) {
      throw new Error(
        "menucommand target has no associated action/notification"
      );
    }

    let notificationEl = getNotificationFromElement(target);
    event.stopPropagation();

    target.notification._recordTelemetryStat(target.action.telemetryStatId);

    try {
      target.action.callback.call(undefined, {
        checkboxChecked: notificationEl.checkbox.checked,
        source: "menucommand",
      });
    } catch (error) {
      console.error(error);
    }

    if (target.action.dismiss) {
      this._dismiss();
      return;
    }

    this._remove(target.notification);
    this._update();
  },

  _notify: function PopupNotifications_notify(topic) {
    Services.obs.notifyObservers(null, "PopupNotifications-" + topic);
  },
};
