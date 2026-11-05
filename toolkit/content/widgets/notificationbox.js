/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into chrome windows with the subscript loader. If you need to
// define globals, wrap in a block to prevent leaking onto `window`.
{
  MozElements.NotificationBox = class NotificationBox {
    /**
     * Creates a new class to handle a notification box, but does not add any
     * elements to the DOM until a notification has to be displayed.
     *
     * @param insertElementFn Called with the "notification-stack" element as an
     *        argument when the first notification has to be displayed.
     * @param {number} securityDelayMS - Delay in milliseconds until buttons are enabled to
     * protect against click- and tapjacking.
     */
    constructor(insertElementFn, securityDelayMS = 0) {
      this._insertElementFn = insertElementFn;
      this._securityDelayMS = securityDelayMS;
      this._animating = false;
      this.currentNotification = null;
    }

    get stack() {
      if (!this._stack) {
        let stack = document.createXULElement("vbox");
        stack._notificationBox = this;
        stack.className = "notificationbox-stack";
        stack.addEventListener("transitionend", event => {
          if (
            event.target.localName == "notification-message" &&
            event.propertyName == "margin-top"
          ) {
            this._finishAnimation();
          }
        });
        this._stack = stack;
        this._insertElementFn(stack);
      }
      return this._stack;
    }

    get _allowAnimation() {
      return window.matchMedia("(prefers-reduced-motion: no-preference)")
        .matches;
    }

    get allNotifications() {
      // Don't create any DOM if no new notification has been added yet.
      if (!this._stack) {
        return [];
      }

      var closedNotification = this._closedNotification;
      var notifications = [
        ...this.stack.getElementsByTagName("notification-message"),
      ];
      return notifications.filter(n => n != closedNotification);
    }

    getNotificationWithValue(aValue) {
      var notifications = this.allNotifications;
      for (var n = notifications.length - 1; n >= 0; n--) {
        if (aValue == notifications[n].getAttribute("value")) {
          return notifications[n];
        }
      }
      return null;
    }

    /**
     * Creates a <notification-message> element and shows it. The calling code can modify
     * the element asynchronously to add features to the notification.
     *
     * aType
     *        String identifier that can uniquely identify the type of the notification.
     * aNotification
     *        Object that contains any of the following properties, where only the
     *        priority must be specified:
     *    priority
     *        One of the PRIORITY_ constants. These determine the appearance of
     *        the notification based on severity (using the "type" attribute), and
     *        only the notification with the highest priority is displayed.
     *    label
     *        The main message text (as string), or object (with l10n-id, l10n-args),
     *        or a DocumentFragment containing elements to
     *        add as children of the notification's main <description> element.
     *    style
     *        Optional object of CSS overrides, currently for use in
     *        experiments. Currently supported keys:
     *          - "background-color": Any valid CSS color or `light-dark(light,
     *        dark)` expression to accommodate light/dark modes
     *          - "font-size": Any valid CSS font-size value, rem units
     *        recommended
     *    eventCallback
     *        This may be called with the "removed", "dismissed" or "disconnected"
     *        parameter:
     *          removed - notification has been removed
     *          dismissed - user dismissed notification
     *          disconnected - notification removed in any way
     * aButtons
     *        Array of objects defining action buttons:
     *        {
     *          label:
     *            Label of the <button> element.
     *          accessKey:
     *            Access key character for the <button> element.
     *          "l10n-id"
     *            Localization id for the <button>, to be used instead of
     *            specifying a separate label and access key.
     *          callback:
     *            When the button is used, this is called with the arguments:
     *             1. The <notification-message> element.
     *             2. This button object definition.
     *             3. The <button> element.
     *             4. The "command" event.
     *            If the callback returns false, the notification is closed.
     *          link:
     *             A url to open when the button is clicked. The button is
     *             rendered like a link. The callback is called as well.
     *          supportPage:
     *            Used for a support page link. If no other properties are specified,
     *            defaults to a link with a 'Learn more' label.
     *          popup:
     *            If specified, the button will open the popup element with this
     *            ID, anchored to the button. This is alternative to "callback".
     *          telemetry:
     *            Specifies the key to add for the telemetry to trigger when the
     *            button is pressed. If not specified, then 'action' is used for
     *            a press on any button. Specify this only if you want to distinguish
     *            which button has been pressed in telemetry data.
     *          is:
     *            Defines a Custom Element name to use as the "is" value on
     *            button creation.
     *        }
     *    aDisableClickJackingDelay
     *        Optional boolean arg to disable clickjacking protections. By
     *        default the security delay is enabled.
     *
     * @returns {Promise<object>} The <notification-message> element that is shown.
     */
    async appendNotification(
      aType,
      aNotification,
      aButtons,
      aDisableClickJackingDelay = false,
      dismissable = true
    ) {
      if (
        aNotification.priority < this.PRIORITY_SYSTEM ||
        aNotification.priority > this.PRIORITY_CRITICAL_HIGH
      ) {
        throw new Error(
          "Invalid notification priority " + aNotification.priority
        );
      }

      MozXULElement.insertFTLIfNeeded("toolkit/global/notification.ftl");

      // Create the Custom Element and connect it to the document immediately.
      let newitem;
      if (!customElements.get("notification-message")) {
        // There's some weird timing stuff when this element is created at
        // script load time, we don't need it until now anyway so be lazy.
        // Wrapped in a try/catch to handle rare cases where we start creating
        // a notification but then the window gets closed/goes away.
        try {
          await createNotificationMessageElement(dismissable);
        } catch (err) {
          console.warn(err);
          throw err;
        }
      }
      newitem = document.createElement("notification-message");
      newitem.dismissable = dismissable;
      newitem.setAttribute("message-bar-type", "infobar");

      // Append or prepend notification, based on stack preference.
      if (this.stack.hasAttribute("prepend-notifications")) {
        this.stack.prepend(newitem);
      } else {
        this.stack.append(newitem);
      }

      if (aNotification.label) {
        newitem.label = aNotification.label;
      } else if (newitem.messageText) {
        // Can't use instanceof in case this was created from a different document:
        if (
          aNotification.label &&
          typeof aNotification.label == "object" &&
          aNotification.label.nodeType &&
          aNotification.label.nodeType ==
            aNotification.label.DOCUMENT_FRAGMENT_NODE
        ) {
          newitem.messageText.appendChild(aNotification.label);
        } else if (
          aNotification.label &&
          typeof aNotification.label == "object" &&
          "l10n-id" in aNotification.label
        ) {
          let message = document.createElement("span");
          document.l10n.setAttributes(
            message,
            aNotification.label["l10n-id"],
            aNotification.label["l10n-args"]
          );
          newitem.messageText.appendChild(message);
        } else {
          newitem.messageText.textContent = aNotification.label;
        }
      }
      newitem.setAttribute("value", aType);

      newitem.eventCallback = aNotification.eventCallback;

      if (aButtons) {
        newitem.setButtons(aButtons);
      }

      newitem.priority = aNotification.priority;
      if (aNotification.priority == this.PRIORITY_SYSTEM) {
        newitem.setAttribute("type", "system");
      } else if (aNotification.priority >= this.PRIORITY_CRITICAL_LOW) {
        newitem.setAttribute("type", "critical");
      } else if (aNotification.priority <= this.PRIORITY_INFO_HIGH) {
        newitem.setAttribute("type", "info");
      } else {
        newitem.setAttribute("type", "warning");
      }

      // Apply any allowed configurable styles to the notification
      const CONFIGURABLE_NOTIFICATION_STYLES = [
        "background-color",
        "font-size",
      ];

      const CONFIGURED_STYLES = aNotification.style || {};
      for (let prop of Object.keys(CONFIGURED_STYLES)) {
        if (!CONFIGURABLE_NOTIFICATION_STYLES.includes(prop)) {
          continue;
        }

        if (prop === "background-color") {
          // Set as a CSS var to be referenced in infobar.css so that we can
          // skip for high contrast mode.
          newitem.style.setProperty(
            "--info-bar-background-color-configurable",
            aNotification.style["background-color"]
          );
        } else {
          newitem.style[prop] = aNotification.style[prop];
        }
      }

      // If clickjacking protection is not explicitly disabled, enable it.
      // aDisableClickJackingDelay is per notification, this._securityDelayMS is
      // global for the entire notification box.
      if (!aDisableClickJackingDelay && this._securityDelayMS > 0) {
        newitem._initClickJackingProtection(this._securityDelayMS);
      }

      // Animate the notification.
      newitem.style.display = "block";
      newitem.style.position = "fixed";
      newitem.style.top = "100%";
      newitem.style.marginTop = "-15px";
      newitem.style.opacity = "0";

      // Ensure the DOM has been created for the Lit-based notification-message
      // element so that we add the .animated class + it animates as expected.
      await newitem.updateComplete;

      if (aNotification.label?.["l10n-id"] && newitem.shadowRoot) {
        await document.l10n.translateFragment(newitem.shadowRoot);
        newitem.setAlertRole();
      }

      this._showNotification(newitem, true);

      // Fire event for accessibility APIs
      var event = document.createEvent("Events");
      event.initEvent("AlertActive", true, true);
      newitem.dispatchEvent(event);

      return newitem;
    }

    removeNotification(aItem, aSkipAnimation) {
      if (!aItem.parentNode) {
        return;
      }
      this.currentNotification = aItem;
      this.removeCurrentNotification(aSkipAnimation);
    }

    _removeNotificationElement(aChild) {
      let hadFocus = aChild.matches(":focus-within");

      if (aChild.eventCallback) {
        aChild.eventCallback("removed");
      }
      aChild.remove();

      // Make sure focus doesn't get lost (workaround for bug 570835).
      if (hadFocus) {
        Services.focus.moveFocus(
          window,
          this.stack,
          Services.focus.MOVEFOCUS_FORWARD,
          0
        );
      }
    }

    removeCurrentNotification(aSkipAnimation) {
      this._showNotification(this.currentNotification, false, aSkipAnimation);
    }

    removeAllNotifications(aImmediate) {
      var notifications = this.allNotifications;
      for (var n = notifications.length - 1; n >= 0; n--) {
        if (aImmediate) {
          this._removeNotificationElement(notifications[n]);
        } else {
          this.removeNotification(notifications[n]);
        }
      }
      this.currentNotification = null;

      // Clean up any currently-animating notification; this is necessary
      // if a notification was just opened and is still animating, but we
      // want to close it *without* animating.  This can even happen if
      // animations get disabled (via prefers-reduced-motion) and this method
      // is called immediately after an animated notification was displayed
      // (although this case isn't very likely).
      if (aImmediate || !this._allowAnimation) {
        this._finishAnimation();
      }
    }

    removeTransientNotifications() {
      var notifications = this.allNotifications;
      for (var n = notifications.length - 1; n >= 0; n--) {
        var notification = notifications[n];
        if (notification.persistence) {
          notification.persistence--;
        } else if (Date.now() > notification.timeout) {
          this.removeNotification(notification, true);
        }
      }
    }

    _showNotification(aNotification, aSlideIn, aSkipAnimation) {
      this._finishAnimation();

      let { marginTop, marginBottom } = getComputedStyle(aNotification);
      let baseHeight = aNotification.getBoundingClientRect().height;
      var height =
        baseHeight + parseInt(marginTop, 10) + parseInt(marginBottom, 10);
      var skipAnimation =
        aSkipAnimation || baseHeight == 0 || !this._allowAnimation;
      aNotification.classList.toggle("animated", !skipAnimation);

      if (aSlideIn) {
        this.currentNotification = aNotification;
        aNotification.style.removeProperty("display");
        aNotification.style.removeProperty("position");
        aNotification.style.removeProperty("top");
        aNotification.style.removeProperty("margin-top");
        aNotification.style.removeProperty("opacity");

        if (skipAnimation) {
          return;
        }
      } else {
        this._closedNotification = aNotification;
        var notifications = this.allNotifications;
        var idx = notifications.length - 1;
        this.currentNotification = idx >= 0 ? notifications[idx] : null;

        if (skipAnimation) {
          this._removeNotificationElement(this._closedNotification);
          delete this._closedNotification;
          return;
        }

        aNotification.style.marginTop = -height + "px";
        aNotification.style.opacity = 0;
      }

      this._animating = true;
    }

    _finishAnimation() {
      if (this._animating) {
        this._animating = false;
        if (this._closedNotification) {
          this._removeNotificationElement(this._closedNotification);
          delete this._closedNotification;
        }
      }
    }
  };

  // These are defined on the instance prototype for backwards compatibility.
  Object.assign(MozElements.NotificationBox.prototype, {
    PRIORITY_SYSTEM: 0,
    PRIORITY_INFO_LOW: 1,
    PRIORITY_INFO_MEDIUM: 2,
    PRIORITY_INFO_HIGH: 3,
    PRIORITY_WARNING_LOW: 4,
    PRIORITY_WARNING_MEDIUM: 5,
    PRIORITY_WARNING_HIGH: 6,
    PRIORITY_CRITICAL_LOW: 7,
    PRIORITY_CRITICAL_MEDIUM: 8,
    PRIORITY_CRITICAL_HIGH: 9,
  });

  async function createNotificationMessageElement(dismissable) {
    document.createElement("moz-message-bar");
    let MozMessageBar = await customElements.whenDefined("moz-message-bar");
    class NotificationMessage extends MozMessageBar {
      static queries = {
        ...MozMessageBar.queries,
        messageText: ".message",
        messageImage: ".icon",
      };

      constructor() {
        super();
        this.persistence = 0;
        this.priority = 0;
        this.timeout = 0;
        this.dismissable = dismissable;

        // Variables used for security delay / clickjacking protection.
        this._clickjackingDelayActive = false;
        this._securityDelayMS = 0;
        this._delayTimer = null;
        this._focusHandler = null;
        this._buttons = [];

        this.addEventListener("click", this);
        this.addEventListener("command", this);
      }

      connectedCallback() {
        super.connectedCallback();
        this.#setStyles();

        this.classList.add("infobar");
        this.setAlertRole();

        this.buttonContainer = document.createElement("span");
        this.buttonContainer.classList.add("notification-button-container");
        this.buttonContainer.setAttribute("slot", "actions");
        this.appendChild(this.buttonContainer);
      }

      disconnectedCallback() {
        super.disconnectedCallback();
        if (this.eventCallback) {
          this.eventCallback("disconnected");
        }
        // Clean up clickjacking listeners if active.
        this._uninitClickJackingProtection();
      }

      closeButtonTemplate() {
        return super.closeButtonTemplate({ size: "small" });
      }

      #setStyles() {
        let style = document.createElement("link");
        style.rel = "stylesheet";
        style.href = "chrome://global/content/elements/infobar.css";
        this.renderRoot.append(style);
      }

      get control() {
        return this.closest(".notificationbox-stack")._notificationBox;
      }

      close() {
        if (!this.parentNode) {
          return;
        }
        this.control.removeNotification(this);
      }

      setAlertRole() {
        // Wait a little for this to render before setting the role for more
        // consistent alerts to screen readers.
        this.removeAttribute("role");
        window.requestAnimationFrame(() => {
          window.requestAnimationFrame(() => {
            this.setAttribute("role", "alert");
          });
        });
      }

      handleEvent(e) {
        // If clickjacking delay is active, prevent any "click"/"command" from
        // going through. Also restart the delay if the user tries to click too early.
        if (this._clickjackingDelayActive) {
          // Only relevant if user clicked on the notification’s actual button/link area.
          if (
            e.type === "click" &&
            (e.target.localName === "button" ||
              e.target.classList.contains("text-link") ||
              e.target.classList.contains("notification-link"))
          ) {
            // Stop immediate action, restart the delay
            e.stopPropagation();
            e.preventDefault();
            this._startClickJackingDelay();
            return;
          }
        }

        if (e.type == "click" && e.target.localName != "label") {
          return;
        }

        if ("buttonInfo" in e.target) {
          let { buttonInfo } = e.target;
          let { callback, popup } = buttonInfo;

          if (popup) {
            document
              .getElementById(popup)
              .openPopup(
                e.originalTarget,
                "after_start",
                0,
                0,
                false,
                false,
                e
              );
            e.stopPropagation();
          } else if (callback) {
            if (!callback(this, buttonInfo, e.target, e)) {
              this.close();
            }
            e.stopPropagation();
          }
        }
      }

      /**
       * Changes the text of an existing notification. If the notification was
       * created with a custom fragment, it will be overwritten with plain text
       * or a localized message.
       *
       * @param {string | { "l10n-id": string, "l10n-args"?: string }} value
       */
      set label(value) {
        if (value && typeof value == "object" && "l10n-id" in value) {
          this.messageL10nId = value["l10n-id"];
          this.messageL10nArgs = value["l10n-args"];
        } else {
          this.message = value;
          this.setAlertRole();
        }
      }

      setButtons(buttons) {
        this._buttons = [];
        for (let button of buttons) {
          let link = button.link || button.supportPage;
          let localeId = button["l10n-id"];

          let buttonElem;
          if (button.hasOwnProperty("supportPage")) {
            buttonElem = document.createElement("a", {
              is: "moz-support-link",
            });
            buttonElem.classList.add("notification-link");
            buttonElem.setAttribute("support-page", button.supportPage);
          } else if (link) {
            buttonElem = document.createXULElement("label", {
              is: "text-link",
            });
            buttonElem.setAttribute("href", link);
            buttonElem.classList.add("notification-link", "text-link");
          } else {
            buttonElem = document.createXULElement(
              "button",
              button.is ? { is: button.is } : {}
            );
            buttonElem.classList.add(
              "notification-button",
              "small-button",
              "footer-button"
            );

            if (button.primary) {
              buttonElem.classList.add("primary");
            }
          }

          if (localeId) {
            document.l10n.setAttributes(buttonElem, localeId);
          } else {
            buttonElem.setAttribute(link ? "value" : "label", button.label);
            if (typeof button.accessKey == "string") {
              buttonElem.setAttribute("accesskey", button.accessKey);
            }
          }

          if (link) {
            buttonElem.setAttribute("slot", "support-link");
            this.appendChild(buttonElem);
          } else {
            this.buttonContainer.appendChild(buttonElem);
          }

          buttonElem.buttonInfo = button;
          this._buttons.push(buttonElem);
        }
      }

      dismiss() {
        if (this.eventCallback) {
          this.eventCallback("dismissed");
        }
        super.dismiss();
      }

      /**
       * Initialize clickjacking protection for this notification, disabling
       * buttons initially and re-enabling them after a short delay. The delay
       * restarts on window focus or if the user attempts to click during the
       * disabled period.
       *
       * @param {number} securityDelayMS - ClickJacking delay to apply
       * (milliseconds).
       */
      _initClickJackingProtection(securityDelayMS) {
        if (this._clickjackingDelayActive) {
          return; // Already enabled.
        }

        this._securityDelayMS = securityDelayMS;
        // Attach a global focus handler so we can restart the delay when the window
        // refocuses (e.g., user navigated away or used a popup).
        this._focusHandler = event => {
          // Only restart delay if the notification is still connected and this
          // is actually a window focus.
          if (this.isConnected && event.target === window) {
            this._startClickJackingDelay();
          }
        };

        window.addEventListener("focus", this._focusHandler, true);
        this._startClickJackingDelay();
      }

      /**
       * Remove any event listeners or timers related to clickjacking protection.
       */
      _uninitClickJackingProtection() {
        window.removeEventListener("focus", this._focusHandler, true);
        this._focusHandler = null;
        if (this._delayTimer) {
          clearTimeout(this._delayTimer);
          this._delayTimer = null;
        }
        this._enableAllButtons();
        this._clickjackingDelayActive = false;
      }

      _startClickJackingDelay() {
        this._clickjackingDelayActive = true;
        this._disableAllButtons();
        if (this._delayTimer) {
          clearTimeout(this._delayTimer);
        }
        this._delayTimer = setTimeout(() => {
          this._clickjackingDelayActive = false;
          this._enableAllButtons();
          this._delayTimer = null;
        }, this._securityDelayMS);
      }

      _disableAllButtons() {
        for (let button of this._buttons) {
          button.disabled = true;
        }
      }

      _enableAllButtons() {
        for (let button of this._buttons) {
          button.disabled = false;
        }
      }
    }

    if (!customElements.get("notification-message")) {
      customElements.define("notification-message", NotificationMessage);
    }
  }
}
