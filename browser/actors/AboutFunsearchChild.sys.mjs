/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export class AboutFunsearchChild extends JSWindowActorChild {
  actorCreated() {
    let window = this.contentWindow;
    if (!window) {
      return;
    }
    let self = this;
    Cu.exportFunction(
      function (query, page) {
        return self.sendQuery("FetchResults", { query, page });
      },
      window,
      { defineAs: "funsearchFetchResults" }
    );
  }
}
