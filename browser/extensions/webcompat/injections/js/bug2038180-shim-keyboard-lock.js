/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

(() => {
  if (navigator.keyboard) {
    return;
  }

  let locked = false;
  let pendingFullscreen = null;
  let fakeFullscreenElement = null;

  const origRequestFullscreen = Element.prototype.requestFullscreen;

  const fullscreenDesc = Object.getOwnPropertyDescriptor(
    Document.prototype,
    "fullscreen"
  );
  const fullscreenElementDesc = Object.getOwnPropertyDescriptor(
    Document.prototype,
    "fullscreenElement"
  );

  Object.defineProperty(Document.prototype, "fullscreen", {
    configurable: true,
    enumerable: fullscreenDesc.enumerable,
    get() {
      return !!fakeFullscreenElement || fullscreenDesc.get.call(this);
    },
  });
  Object.defineProperty(Document.prototype, "fullscreenElement", {
    configurable: true,
    enumerable: fullscreenElementDesc.enumerable,
    get() {
      return fakeFullscreenElement ?? fullscreenElementDesc.get.call(this);
    },
  });

  function animationFrame() {
    return new Promise(resolve => requestAnimationFrame(resolve));
  }

  function dispatchFakeFullscreenChange(element) {
    element.dispatchEvent(
      new Event("fullscreenchange", {
        bubbles: true,
        composed: true,
      })
    );
  }

  function clearPendingFullscreen() {
    if (pendingFullscreen?.abortController) {
      pendingFullscreen.abortController.abort();
    }
    pendingFullscreen = null;
    fakeFullscreenElement = null;
  }

  Element.prototype.requestFullscreen = function (options = {}) {
    if ("keyboardLock" in options) {
      return origRequestFullscreen.call(this, options);
    }

    if (locked) {
      return origRequestFullscreen.call(this, {
        ...options,
        keyboardLock: "browser",
      });
    }

    const element = this;
    const abortController = new AbortController();

    clearPendingFullscreen();

    pendingFullscreen = {
      element,
      options,
      abortController,
    };

    fakeFullscreenElement = element;

    (async () => {
      await animationFrame();

      if (
        abortController.signal.aborted ||
        pendingFullscreen?.element !== element
      ) {
        return;
      }

      dispatchFakeFullscreenChange(element);

      await animationFrame();

      if (
        abortController.signal.aborted ||
        pendingFullscreen?.element !== element
      ) {
        return;
      }

      clearPendingFullscreen();

      origRequestFullscreen.call(element, {
        ...options,
        keyboardLock: "none",
      });
    })();

    return Promise.resolve();
  };

  navigator.keyboard = {
    async lock() {
      locked = true;

      if (pendingFullscreen) {
        const { element, options } = pendingFullscreen;

        clearPendingFullscreen();

        await origRequestFullscreen.call(element, {
          ...options,
          keyboardLock: "browser",
        });
        return undefined;
      }

      if (document.fullscreenElement && navigator.userActivation.isActive) {
        await origRequestFullscreen.call(document.fullscreenElement, {
          keyboardLock: "browser",
        });
        return undefined;
      }

      return Promise.resolve();
    },
    unlock() {
      const lockStateChanged = locked === true;
      locked = false;

      if (
        lockStateChanged &&
        document.fullscreenElement &&
        navigator.userActivation.isActive
      ) {
        origRequestFullscreen.call(document.fullscreenElement, {
          keyboardLock: "none",
        });
      }
    },
  };

  const origPermissionsQuery = Permissions.prototype.query;

  Permissions.prototype.query = function (permissionDesc) {
    if (permissionDesc.name === "keyboard-lock") {
      return Promise.resolve({ state: "granted" });
    }

    return origPermissionsQuery.call(this, permissionDesc);
  };
})();
