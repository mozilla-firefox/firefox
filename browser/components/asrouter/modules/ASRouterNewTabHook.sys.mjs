/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

class ASRouterNewTabHookInstance {
  constructor() {
    this._newTabMessageHandler = null;
    this._parentProcessMessageHandler = null;
    this._router = null;
    this._clearChildMessages = (...params) =>
      this._newTabMessageHandler === null
        ? Promise.resolve()
        : this._newTabMessageHandler.clearChildMessages(...params);
    this._clearChildProviders = (...params) =>
      this._newTabMessageHandler === null
        ? Promise.resolve()
        : this._newTabMessageHandler.clearChildProviders(...params);
    this._updateAdminState = (...params) =>
      this._newTabMessageHandler === null
        ? Promise.resolve()
        : this._newTabMessageHandler.updateAdminState(...params);
  }

  /**
   * Params:
   *    object - {
   *      messageHandler: message handler for parent process messages
   *      {
   *        handleCFRAction: Responds to CFR action and returns a Promise
   *        handleTelemetry: Logs telemetry events and returns nothing
   *      },
   *      router: ASRouter instance
   *      createStorage: function to create DB storage for ASRouter
   *   }
   */
  async initialize({ messageHandler, router, createStorage }) {
    this._parentProcessMessageHandler = messageHandler;
    this._router = router;
    if (!this._router.initialized) {
      const storage = await createStorage();
      await this._router.init({
        storage,
        sendTelemetry: this._parentProcessMessageHandler.handleTelemetry,
        dispatchCFRAction: this._parentProcessMessageHandler.handleCFRAction,
        clearChildMessages: this._clearChildMessages,
        clearChildProviders: this._clearChildProviders,
        updateAdminState: this._updateAdminState,
      });
    }
  }

  destroy() {
    if (this._router?.initialized) {
      this.disconnect();
      this._router.uninit();
    }
  }

  /**
   * Connects new tab message handler to hook.
   * Note: Should only ever be called on an initialized instance
   * Params:
   *    newTabMessageHandler - {
   *      clearChildMessages: clears child messages and returns Promise
   *      clearChildProviders: clears child providers and returns Promise.
   *      updateAdminState: updates admin state and returns Promise
   *   }
   * Returns: parentProcessMessageHandler
   */
  connect(newTabMessageHandler) {
    this._newTabMessageHandler = newTabMessageHandler;
    return this._parentProcessMessageHandler;
  }

  /**
   * Disconnects new tab message handler from hook.
   */
  disconnect() {
    this._newTabMessageHandler = null;
  }
}

class AwaitSingleton {
  constructor() {
    this.instance = null;
    const initialized = new Promise(resolve => {
      this.setInstance = instance => {
        this.setInstance = () => {};
        this.instance = instance;
        resolve(instance);
      };
    });
    this.getInstance = () => initialized;
  }
}

export const ASRouterNewTabHook = (() => {
  const singleton = new AwaitSingleton();
  const instance = new ASRouterNewTabHookInstance();
  return {
    getInstance: singleton.getInstance,

    /**
     * Param:
     *    params - see ASRouterNewTabHookInstance.init
     */
    createInstance: async params => {
      await instance.initialize(params);
      singleton.setInstance(instance);
    },

    destroy: () => {
      instance.destroy();
    },
  };
})();
