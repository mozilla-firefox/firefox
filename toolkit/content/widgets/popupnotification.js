/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

// This is loaded into all XUL windows. Wrap in a block to prevent
// leaking to window scope.
{
  class MozPopupNotification extends MozXULElement {
    static get inheritedAttributes() {
      return {
        ".popup-notification-icon": "popupid,src=icon,class=iconclass,hasicon",
        ".popup-notification-body": "popupid",
        ".popup-notification-origin": "value=origin,tooltiptext=origin",
        ".popup-notification-description": "popupid,id=descriptionid",
        ".popup-notification-description > span:first-of-type":
          "text=label,popupid",
        ".popup-notification-description > .popup-notification-description-name":
          "text=name,popupid",
        ".popup-notification-description > span:nth-of-type(2)":
          "text=endlabel,popupid",
        ".popup-notification-description > b:last-of-type":
          "text=secondname,popupid",
        ".popup-notification-description > span:last-of-type":
          "text=secondendlabel,popupid",
        ".popup-notification-hint-text": "text=hinttext",
        ".popup-notification-closebutton": "hidden=closebuttonhidden",
        ".popup-notification-learnmore-link": "href=learnmoreurl",
        ".popup-notification-warning": "hidden=warninghidden,text=warninglabel",
        ".popup-notification-secondary-button":
          "label=secondarybuttonlabel,accesskey=secondarybuttonaccesskey,hidden=secondarybuttonhidden",
        ".popup-notification-more-actions-popup":
          "hidden=secondarybuttonhidden",
        ".popup-notification-primary-button":
          "label=buttonlabel,accesskey=buttonaccesskey,disabled=mainactiondisabled",
      };
    }

    static get observedAttributes() {
      return [...super.observedAttributes, "dropmarkerhidden"];
    }

    attributeChangedCallback(name, oldValue, newValue) {
      if (!this._hasSlotted) {
        return;
      }

      // If the label and/or accesskey for the primary button is set by
      // inherited attributes, its data-l10n-id needs to be unset or
      // DOM Localization will overwrite the values.
      if (name === "buttonlabel" || name === "buttonaccesskey") {
        this.button?.removeAttribute("data-l10n-id");
      }

      if (name === "dropmarkerhidden" && this.secondaryButton) {
        this.secondaryButton.type = newValue !== null ? "default" : "split";
      }

      super.attributeChangedCallback(name, oldValue, newValue);
    }

    show() {
      this.slotContents();

      if (this.checkboxState) {
        this.checkbox.checked = this.checkboxState.checked;
        this.checkbox.setAttribute("label", this.checkboxState.label);
        this.checkbox.hidden = false;
      } else {
        this.checkbox.hidden = true;
        // Reset checked state to avoid wrong using of previous value.
        this.checkbox.checked = false;
      }

      this.hidden = false;
    }

    static get markup() {
      return `
      <hbox class="popup-notification-header-container"></hbox>
      <hbox align="start" class="popup-notification-body-container">
        <image class="popup-notification-icon"/>
        <vbox pack="start" class="popup-notification-body">
          <a class="popup-notification-origin header" crop="center"></a>
          <!-- These need to be on the same line to avoid creating
              whitespace between them (whitespace is added in the
              localization file, if necessary). -->
          <description class="popup-notification-description"><html:span></html:span><html:b class="popup-notification-description-name"></html:b><html:span></html:span><html:b></html:b><html:span></html:span></description>
          <description class="popup-notification-hint-text"></description>
          <vbox class="popup-notification-bottom-content" align="start">
            <label class="popup-notification-learnmore-link" is="text-link" data-l10n-id="popup-notification-learn-more"></label>
            <checkbox class="popup-notification-checkbox"/>
            <description class="popup-notification-warning"/>
          </vbox>
        </vbox>
        <toolbarbutton class="messageCloseButton close-icon popup-notification-closebutton tabbable" data-l10n-id="close-notification-message"></toolbarbutton>
      </hbox>
      <hbox class="popup-notification-footer-container"></hbox>
      <html:moz-button-group class="panel-footer">
        <html:moz-button type="split" class="popup-notification-secondary-button">
        </html:moz-button>
        <html:moz-button type="primary" class="popup-notification-primary-button" data-l10n-id="popup-notification-default-button2">
        </html:moz-button>
      </html:moz-button-group>
      <menupopup class="popup-notification-more-actions-popup" position="after_end" data-l10n-id="popup-notification-more-actions-button">
      </menupopup>
      `;
    }

    slotContents() {
      if (this._hasSlotted) {
        return;
      }

      if (
        this.hasAttribute("buttoncommand") ||
        this.hasAttribute("secondarybuttoncommand") ||
        this.hasAttribute("learnmoreclick")
      ) {
        throw new Error(
          "The attributes 'buttoncommand', 'secondarybuttoncommand' and 'learnmoreclick' are not supported anymore use `addEventListener` instead"
        );
      }

      this._hasSlotted = true;
      MozXULElement.insertFTLIfNeeded("toolkit/global/notification.ftl");
      MozXULElement.insertFTLIfNeeded("toolkit/global/popupnotification.ftl");
      this.appendChild(this.constructor.fragment);

      this.button = this.querySelector(".popup-notification-primary-button");
      if (
        this.hasAttribute("buttonlabel") ||
        this.hasAttribute("buttonaccesskey")
      ) {
        this.button.removeAttribute("data-l10n-id");
      }
      this.secondaryButton = this.querySelector(
        ".popup-notification-secondary-button"
      );
      this.secondaryButton.type = this.hasAttribute("dropmarkerhidden")
        ? "default"
        : "split";
      this.checkbox = this.querySelector(".popup-notification-checkbox");
      this.closebutton = this.querySelector(".popup-notification-closebutton");
      this.menubutton = this.secondaryButton;
      this.menupopup = this.querySelector(
        ".popup-notification-more-actions-popup"
      );
      this.buttonGroup = this.querySelector(".panel-footer");

      let popupnotificationfooter = this.querySelector(
        "popupnotificationfooter"
      );
      if (popupnotificationfooter) {
        this.querySelector(".popup-notification-footer-container").append(
          popupnotificationfooter
        );
      }

      let popupnotificationheader = this.querySelector(
        "popupnotificationheader"
      );
      if (popupnotificationheader) {
        this.querySelector(".popup-notification-header-container").append(
          popupnotificationheader
        );
      }

      for (let popupnotificationcontent of this.querySelectorAll(
        "popupnotificationcontent"
      )) {
        this.appendNotificationContent(popupnotificationcontent);
      }

      this.initializeAttributeInheritance();

      let customEventDelegator = (type, event) => {
        let customEvent = new CustomEvent(type, {
          cancelable: true,
          bubbles: true,
        });
        // Give listeners the chance to prevent the default behavior.
        if (this.dispatchEvent(customEvent)) {
          PopupNotifications._onButtonEvent(event, type, "button", this);
        }
      };

      this.button.addEventListener(
        "click",
        event => customEventDelegator("buttoncommand", event),
        { capture: true }
      );
      this.secondaryButton.addEventListener(
        "click",
        event => {
          if (
            this.secondaryButton.chevronButtonEl?.contains(event.originalTarget)
          ) {
            this.menupopup.openPopup(
              this.secondaryButton.chevronButtonEl,
              "after_end"
            );
          } else {
            customEventDelegator("secondarybuttoncommand", event);
          }
        },
        { capture: true }
      );
      this.secondaryButton.addEventListener("keydown", event => {
        if (event.key === "ArrowDown") {
          event.preventDefault();

          this.menupopup.openPopup(
            this.secondaryButton.chevronButtonEl,
            "after_end"
          );
        }
      });
      this.buttonGroup.addEventListener("keydown", event =>
        this.onButtonGroupKeydown(event)
      );
      this.checkbox.addEventListener("command", event => {
        PopupNotifications._onCheckboxCommand(event);
      });
      this.closebutton.addEventListener("command", event => {
        PopupNotifications._dismiss(event, true);
      });
      this.menupopup.addEventListener("popupshown", event => {
        PopupNotifications._onButtonEvent(event, "dropmarkerpopupshown");
      });
      this.menupopup.addEventListener("command", event => {
        PopupNotifications._onMenuCommand(event);
      });
      this.querySelector(".popup-notification-learnmore-link").addEventListener(
        "click",
        event => customEventDelegator("learnmoreclick", event)
      );
    }

    appendNotificationContent(el) {
      this.querySelector(".popup-notification-bottom-content").before(el);
    }

    get footerFocusableButtons() {
      let buttons = [];
      for (let child of this.buttonGroup.children) {
        if (
          child.localName !== "moz-button" ||
          child.hidden ||
          child.disabled ||
          !child.buttonEl
        ) {
          continue;
        }
        buttons.push(child.buttonEl);
        if (child.isSplitButton) {
          buttons.push(child.chevronButtonEl);
        }
      }
      return buttons;
    }

    onButtonGroupKeydown(event) {
      if (event.altKey || event.ctrlKey || event.metaKey || event.shiftKey) {
        return;
      }

      let isRTL = this.matches(":dir(rtl)");
      let forwardKey = isRTL ? "ArrowLeft" : "ArrowRight";
      let backwardKey = isRTL ? "ArrowRight" : "ArrowLeft";
      if (event.key !== forwardKey && event.key !== backwardKey) {
        return;
      }

      let buttons = this.footerFocusableButtons;
      let currentIndex = buttons.findIndex(
        button => button.getRootNode().activeElement === button
      );
      if (currentIndex === -1) {
        return;
      }

      let lastIndex = buttons.length - 1;
      let nextIndex;
      if (event.key === forwardKey) {
        nextIndex = currentIndex === lastIndex ? 0 : currentIndex + 1;
      } else {
        nextIndex = currentIndex === 0 ? lastIndex : currentIndex - 1;
      }

      event.preventDefault();
      buttons[nextIndex].focus();
    }
  }

  customElements.define("popupnotification", MozPopupNotification);
}
