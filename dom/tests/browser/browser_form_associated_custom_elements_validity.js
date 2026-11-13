/**
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

"use strict";

add_task(async function report_validity() {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: `data:text/html,<my-control></my-control>`,
    },
    async function (aBrowser) {
      let promisePopupShown = BrowserTestUtils.waitForEvent(
        window,
        "popupshown"
      );

      let message = "valueMissing message";
      await SpecialPowers.spawn(aBrowser, [message], function (aMessage) {
        class MyControl extends content.HTMLElement {
          static get formAssociated() {
            return true;
          }
          constructor() {
            super();
            let shadow = this.attachShadow({ mode: "open" });
            let input = content.document.createElement("input");
            shadow.appendChild(input);

            let internals = this.attachInternals();
            internals.setValidity({ valueMissing: true }, aMessage, input);
            internals.reportValidity();
          }
        }
        content.customElements.define("my-control", MyControl);

        let myControl = content.document.querySelector("my-control");
        content.customElements.upgrade(myControl);
      });
      await promisePopupShown;

      let invalidFormPopup =
        window.document.getElementById("invalid-form-popup");
      is(invalidFormPopup.state, "open", "invalid-form-popup should be opened");
      is(invalidFormPopup.firstChild.textContent, message, "check message");

      let promisePopupHidden = BrowserTestUtils.waitForEvent(
        invalidFormPopup,
        "popuphidden"
      );
      invalidFormPopup.hidePopup();
      await promisePopupHidden;
    }
  );
});

add_task(async function form_report_validity() {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: `data:text/html,<form><my-control></my-control></form>`,
    },
    async function (aBrowser) {
      let promisePopupShown = BrowserTestUtils.waitForEvent(
        window,
        "popupshown"
      );

      let message = "valueMissing message";
      await SpecialPowers.spawn(aBrowser, [message], function (aMessage) {
        class MyControl extends content.HTMLElement {
          static get formAssociated() {
            return true;
          }
          constructor() {
            super();
            let shadow = this.attachShadow({ mode: "open" });
            let input = content.document.createElement("input");
            shadow.appendChild(input);

            let internals = this.attachInternals();
            internals.setValidity({ valueMissing: true }, aMessage, input);
          }
        }
        content.customElements.define("my-control", MyControl);

        let myControl = content.document.querySelector("my-control");
        content.customElements.upgrade(myControl);

        let form = content.document.querySelector("form");
        is(form.length, "1", "check form.length");
        form.reportValidity();
      });
      await promisePopupShown;

      let invalidFormPopup =
        window.document.getElementById("invalid-form-popup");
      is(invalidFormPopup.state, "open", "invalid-form-popup should be opened");
      is(invalidFormPopup.firstChild.textContent, message, "check message");

      let promisePopupHidden = BrowserTestUtils.waitForEvent(
        invalidFormPopup,
        "popuphidden"
      );
      invalidFormPopup.hidePopup();
      await promisePopupHidden;
    }
  );
});

add_task(async function no_validation_anchor() {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: `data:text/html,<my-control tabindex=0>custom elements</my-control>`,
    },
    async function (aBrowser) {
      let promisePopupShown = BrowserTestUtils.waitForEvent(
        window,
        "popupshown"
      );

      let message = "valueMissing message";
      await SpecialPowers.spawn(aBrowser, [message], function (aMessage) {
        class MyControl extends content.HTMLElement {
          static get formAssociated() {
            return true;
          }
          constructor() {
            super();
            let internals = this.attachInternals();
            internals.setValidity({ valueMissing: true }, aMessage);
            internals.reportValidity();
          }
        }
        content.customElements.define("my-control", MyControl);

        let myControl = content.document.querySelector("my-control");
        content.customElements.upgrade(myControl);
      });
      await promisePopupShown;

      let invalidFormPopup =
        window.document.getElementById("invalid-form-popup");
      is(invalidFormPopup.state, "open", "invalid-form-popup should be opened");
      is(invalidFormPopup.firstChild.textContent, message, "check message");

      let promisePopupHidden = BrowserTestUtils.waitForEvent(
        invalidFormPopup,
        "popuphidden"
      );
      invalidFormPopup.hidePopup();
      await promisePopupHidden;
    }
  );
});

add_task(async function display_contents_delegates_focus() {
  await BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: `data:text/html,<my-control style="display: contents"></my-control>`,
    },
    async function (aBrowser) {
      let promisePopupShown = BrowserTestUtils.waitForEvent(
        window,
        "popupshown"
      );

      let message = "valueMissing message";
      await SpecialPowers.spawn(aBrowser, [message], function (aMessage) {
        class MyControl extends content.HTMLElement {
          static get formAssociated() {
            return true;
          }
          constructor() {
            super();
            let shadow = this.attachShadow({
              mode: "open",
              delegatesFocus: true,
            });
            shadow.appendChild(content.document.createElement("input"));

            let internals = this.attachInternals();
            internals.setValidity({ valueMissing: true }, aMessage);
            internals.reportValidity();
          }
        }
        content.customElements.define("my-control", MyControl);

        let myControl = content.document.querySelector("my-control");
        content.customElements.upgrade(myControl);
      });
      await promisePopupShown;

      await SpecialPowers.spawn(aBrowser, [], function () {
        let myControl = content.document.querySelector("my-control");
        let input = myControl.shadowRoot.querySelector("input");
        is(content.document.activeElement, myControl, "host is focused");
        is(myControl.shadowRoot.activeElement, input, "input is focused");
      });

      let invalidFormPopup =
        window.document.getElementById("invalid-form-popup");
      is(invalidFormPopup.state, "open", "invalid-form-popup should be opened");
      is(invalidFormPopup.firstChild.textContent, message, "check message");

      let promisePopupHidden = BrowserTestUtils.waitForEvent(
        invalidFormPopup,
        "popuphidden"
      );
      invalidFormPopup.hidePopup();
      await promisePopupHidden;
    }
  );
});
