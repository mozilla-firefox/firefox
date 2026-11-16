/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * Answers agent-monitor-item requests that need privileged data, in any page
 * that hosts the card. The answer is dispatched back on the card that asked,
 * so the hosting page doesn't take part.
 */
export class AgentMonitorItemChild extends JSWindowActorChild {
  /**
   * @param {CustomEvent} event
   */
  async handleEvent(event) {
    switch (event.type) {
      case "agent-monitor-item:resolve-titles":
        await this.#resolveTitles(event);
        break;
      case "agent-monitor-item:request-suggestion":
        await this.#requestSuggestion(event);
        break;
    }
  }

  /**
   * @param {CustomEvent} event
   */
  async #resolveTitles(event) {
    // event.target is retargeted to the page's outermost shadow host, so take the
    // card from the path, which is only available while the event is dispatching
    const card = event.composedPath()[0];
    const urls = Array.from(event.detail?.urls ?? []);
    if (!card || !urls.length) {
      return;
    }

    try {
      const titles = await this.sendQuery("AgentMonitorItem:ResolveTitles", {
        urls,
      });
      card.dispatchEvent(
        new this.contentWindow.CustomEvent(
          "agent-monitor-item:titles-resolved",
          { detail: Cu.cloneInto({ titles }, this.contentWindow) }
        )
      );
    } catch (error) {
      console.warn("AgentMonitorItemChild: could not resolve titles", error);
    }
  }

  /**
   * The card waits for the answer, so one is always sent, with empty fields
   * when there is nothing to suggest.
   *
   * @param {CustomEvent} event
   */
  async #requestSuggestion(event) {
    const card = event.composedPath()[0];
    const { requestId, watchUrls, previousDrafts } = event.detail ?? {};
    if (!card) {
      return;
    }

    let suggestion = null;
    try {
      suggestion = await this.sendQuery("AgentMonitorItem:Suggest", {
        watchUrls: Array.from(watchUrls ?? []),
        previousDrafts: Array.from(previousDrafts ?? [], draft => ({
          title: String(draft?.title ?? ""),
          condition: String(draft?.condition ?? ""),
        })),
      });
    } catch (error) {
      console.warn("AgentMonitorItemChild: could not get a suggestion", error);
    }
    try {
      card.dispatchEvent(
        new this.contentWindow.CustomEvent("agent-monitor-item:suggestion", {
          detail: Cu.cloneInto(
            {
              requestId,
              title: suggestion?.title ?? "",
              condition: suggestion?.condition ?? "",
            },
            this.contentWindow
          ),
        })
      );
    } catch (error) {
      console.warn("AgentMonitorItemChild: could not answer the card", error);
    }
  }
}
