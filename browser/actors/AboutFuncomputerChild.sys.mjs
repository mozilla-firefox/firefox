/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

export class AboutFuncomputerChild extends JSWindowActorChild {
  actorCreated() {
    let window = this.contentWindow;
    if (!window) {
      return;
    }
    let self = this;
    function exportQuery(name, defineAs) {
      Cu.exportFunction(
        function (...args) {
          return self.sendQuery(name, args[0] || {}).then(result =>
            Cu.cloneInto(result, window)
          );
        },
        window,
        { defineAs }
      );
    }
    exportQuery("GetSession", "funcomputerSession");
    Cu.exportFunction(
      function (email, password) {
        return self
          .sendQuery("Signup", { email, password })
          .then(result => Cu.cloneInto(result, window));
      },
      window,
      { defineAs: "funcomputerSignup" }
    );
    Cu.exportFunction(
      function (email, password) {
        return self
          .sendQuery("Login", { email, password })
          .then(result => Cu.cloneInto(result, window));
      },
      window,
      { defineAs: "funcomputerLogin" }
    );
    Cu.exportFunction(
      function () {
        return self
          .sendQuery("Logout")
          .then(result => Cu.cloneInto(result, window));
      },
      window,
      { defineAs: "funcomputerLogout" }
    );
  }
}
