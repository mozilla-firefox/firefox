/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { GeckoViewModule } from "resource://gre/modules/GeckoViewModule.sys.mjs";

/**
 * GeckoViewModule to get the content of a page
 */
export class GeckoViewPageExtractor extends GeckoViewModule {
  onInit() {
    debug`onInit`;
    this.registerListener([
      "GeckoView:PageExtractor:GetTextContent",
      "GeckoView:PageExtractor:GetPageMetadata",
    ]);
  }

  async onEvent(aEvent, aData, aCallback) {
    debug`onEvent: event=${aEvent}, data=${aData}`;
    switch (aEvent) {
      case "GeckoView:PageExtractor:GetTextContent": {
        try {
          await this.getActor("PageExtractor")
            .getText(aData ?? {})
            .then(
              result => {
                aCallback.onSuccess({
                  text: result.text,
                });
              },
              error =>
                aCallback.onError(`Could not get page text content: ${error}`)
            );
        } catch (error) {
          aCallback.onError(`Unable to get text from PageExtractor: ${error}`);
        }
        break;
      }
      case "GeckoView:PageExtractor:GetPageMetadata": {
        try {
          await this.getActor("PageExtractor")
            .getPageMetadata()
            .then(
              result => {
                aCallback.onSuccess({
                  structuredDataTypes: result.structuredDataTypes,
                  wordCount: result.wordCount,
                  language: result.language,
                });
              },
              error =>
                aCallback.onError(`Could not get page metadata: ${error}`)
            );
        } catch (error) {
          aCallback.onError(
            `Unable to get page metadata from PageExtractor: ${error}`
          );
        }
        break;
      }
    }
  }
}

const { debug, warn } = GeckoViewPageExtractor.initLogging(
  "GeckoViewPageExtractor"
);
