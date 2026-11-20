/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Child actor for about:smartpage. Forwards requests from the content document
 * to the parent process under the same name content dispatched them with, so a
 * message can be traced across the boundary without a translation table.
 */
export class AITabChild extends JSWindowActorChild {
  handleEvent(event) {
    switch (event.type) {
      case "AITab:GetPage":
      case "AITab:GetHeadingFont":
      case "AITab:DeletePage":
        this.#query(event);
        break;
      // Events whose dispatchers do not expect a response take this
      // fire-and-forget path, rather than waiting for a response like
      // events which go through #query().
      case "AITab:OpenLink":
        this.sendAsyncMessage(event.type, {
          ...(event.detail ?? {}),
          component_type: this.#componentType(event),
        });
        break;
      case "AITab:RecordView":
        this.sendAsyncMessage(event.type, event.detail);
        break;
      default:
        console.warn(`AITabChild received unknown event: ${event.type}`);
    }
  }

  receiveMessage({ name, data }) {
    if (!this.contentWindow) {
      return;
    }

    const win = this.contentWindow;
    switch (name) {
      case "AITab:RefreshPageData":
        win.dispatchEvent(
          new win.CustomEvent(name, {
            detail: Cu.cloneInto(data, win),
          })
        );
        break;

      default:
        break;
    }
  }

  /**
   * The component that contained the click, as it is rendered, in the name
   * format that creation telemetry uses. A block that A2UI converted reports
   * the type it renders as, not the type the model sent. A click outside
   * those blocks is "unknown".
   *
   * @param {Event} event
   * @returns {string}
   */
  #componentType(event) {
    for (const node of event.composedPath()) {
      const type = node.dataset?.aitabComponent;
      if (type) {
        return type;
      }
    }
    return "unknown";
  }

  /**
   * Forwards a message that expects an answer, and dispatches the parent's
   * reply back on the element that fired the event.
   *
   * @param {Event} event
   */
  #query(event) {
    this.sendQuery(event.type, event.detail)
      .then(
        response => this.#respond(event, "Response", response),
        error => this.#respond(event, "Error", { error: error.message })
      )
      .catch(error => {
        console.error("Could not answer an AI Tab page request", error);
      });
  }

  /**
   * Dispatches `<event name>:Response` or `<event name>:Error` on the
   * requesting element, which is what aitab-page's #request() waits for.
   *
   * @param {Event} event
   * @param {string} suffix - "Response" or "Error".
   * @param {object} detail
   */
  #respond(event, suffix, detail) {
    // The page can go away while the query is in flight.
    if (!this.contentWindow) {
      return;
    }
    event.target.dispatchEvent(
      new this.contentWindow.CustomEvent(`${event.type}:${suffix}`, {
        detail: Cu.cloneInto(detail, this.contentWindow),
        bubbles: false,
      })
    );
  }
}
