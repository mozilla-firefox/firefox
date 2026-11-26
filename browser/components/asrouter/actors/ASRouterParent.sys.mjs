/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// We use importESModule here instead of static import so that
// the Karma test environment won't choke on this module, since
// it doesn't seem to understand using static import for sys.mjs
// files.
// eslint-disable-next-line mozilla/use-static-import
const { ASRouterNewTabHook } = ChromeUtils.importESModule(
  "resource:///modules/asrouter/ASRouterNewTabHook.sys.mjs"
);

import { ASRouterDefaultConfig } from "resource:///modules/asrouter/ASRouterDefaultConfig.sys.mjs";

export class ASRouterTabs {
  constructor({ asRouterNewTabHook }) {
    this.actors = new Set();
    this.destroy = () => {};
    // This is one of several entrypoints to ASRouter Initialization. There is
    // another one in BrowserGlue, and another in BackgroundTaskUtils.
    asRouterNewTabHook.createInstance(ASRouterDefaultConfig());
    this.loadingMessageHandler = asRouterNewTabHook
      .getInstance()
      .then(initializer => {
        const parentProcessMessageHandler = initializer.connect({
          updateAdminState: state => this.messageAll("UpdateAdminState", state),
        });
        this.destroy = () => {
          initializer.disconnect();
        };
        return parentProcessMessageHandler;
      });
  }

  get size() {
    return this.actors.size;
  }

  messageAll(message, data) {
    return Promise.all(
      [...this.actors].map(a => a.sendAsyncMessage(message, data))
    );
  }

  registerActor(actor) {
    this.actors.add(actor);
  }

  unregisterActor(actor) {
    this.actors.delete(actor);
  }
}

const defaultTabsFactory = () =>
  new ASRouterTabs({ asRouterNewTabHook: ASRouterNewTabHook });

export class ASRouterParent extends JSWindowActorParent {
  static tabs = null;

  static nextTabId = 0;

  constructor({ tabsFactory } = { tabsFactory: defaultTabsFactory }) {
    super();
    this.tabsFactory = tabsFactory;
  }

  actorCreated() {
    ASRouterParent.tabs = ASRouterParent.tabs || this.tabsFactory();
    this.tabsFactory = null;
    this.tabId = ++ASRouterParent.nextTabId;
    ASRouterParent.tabs.registerActor(this);
  }

  didDestroy() {
    ASRouterParent.tabs.unregisterActor(this);
    if (ASRouterParent.tabs.size < 1) {
      ASRouterParent.tabs.destroy();
      ASRouterParent.tabs = null;
    }
  }

  getTab() {
    return {
      id: this.tabId,
      browser: this.browsingContext.embedderElement,
    };
  }

  receiveMessage({ name, data }) {
    return ASRouterParent.tabs.loadingMessageHandler.then(handler => {
      return handler.handleMessage(name, data, this.getTab());
    });
  }
}
