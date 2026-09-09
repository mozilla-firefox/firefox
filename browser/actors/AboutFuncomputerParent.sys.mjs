/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  FunComputerAccounts: "resource:///modules/FunComputerAccounts.sys.mjs",
});

export class AboutFuncomputerParent extends JSWindowActorParent {
  async receiveMessage(message) {
    switch (message.name) {
      case "GetSession":
        return lazy.FunComputerAccounts.getSession();
      case "Signup":
        return lazy.FunComputerAccounts.signup(
          String(message.data?.email || ""),
          String(message.data?.password || "")
        );
      case "Login":
        return lazy.FunComputerAccounts.login(
          String(message.data?.email || ""),
          String(message.data?.password || "")
        );
      case "Logout":
        return lazy.FunComputerAccounts.logout();
      default:
        return undefined;
    }
  }
}
