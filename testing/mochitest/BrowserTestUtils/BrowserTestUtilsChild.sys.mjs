/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const lazy = {};
ChromeUtils.defineESModuleGetters(lazy, {
  E10SUtils: "resource://gre/modules/E10SUtils.sys.mjs",
});

class BrowserTestUtilsChildObserver {
  constructor() {
    this.currentObserverStatus = "";
    this.observerItems = [];
  }

  startObservingTopics(aTopics) {
    for (let topic of aTopics) {
      Services.obs.addObserver(this, topic);
      this.observerItems.push({ topic });
    }
  }

  stopObservingTopics(aTopics) {
    if (aTopics) {
      for (let topic of aTopics) {
        let index = this.observerItems.findIndex(item => item.topic == topic);
        if (index >= 0) {
          Services.obs.removeObserver(this, topic);
          this.observerItems.splice(index, 1);
        }
      }
    } else {
      for (let topic of this.observerItems) {
        Services.obs.removeObserver(this, topic);
      }
      this.observerItems = [];
    }

    if (this.currentObserverStatus) {
      let error = new Error(this.currentObserverStatus);
      this.currentObserverStatus = "";
      throw error;
    }
  }

  observeTopic(topic, count, filterFn, callbackResolver) {
    // If the topic is in the list already, assume that it came from a
    // startObservingTopics call. If it isn't in the list already, assume
    // that it isn't within a start/stop set and the observer has to be
    // removed afterwards.
    let removeObserver = false;
    let index = this.observerItems.findIndex(item => item.topic == topic);
    if (index == -1) {
      removeObserver = true;
      this.startObservingTopics([topic]);
    }

    for (let item of this.observerItems) {
      if (item.topic == topic) {
        item.count = count || 1;
        item.filterFn = filterFn;
        item.promiseResolver = () => {
          if (removeObserver) {
            this.stopObservingTopics([topic]);
          }
          callbackResolver();
        };
        break;
      }
    }
  }

  observe(aSubject, aTopic, aData) {
    for (let item of this.observerItems) {
      if (item.topic != aTopic) {
        continue;
      }
      if (item.filterFn && !item.filterFn(aSubject, aTopic, aData)) {
        break;
      }

      if (--item.count >= 0) {
        if (item.count == 0 && item.promiseResolver) {
          item.promiseResolver();
        }
        return;
      }
    }

    // Otherwise, if the observer doesn't match, fail.
    console.log(
      "Failed: Observer topic " + aTopic + " not expected in content process"
    );
    this.currentObserverStatus +=
      "Topic " + aTopic + " not expected in content process\n";
  }
}

BrowserTestUtilsChildObserver.prototype.QueryInterface = ChromeUtils.generateQI(
  ["nsIObserver", "nsISupportsWeakReference"]
);

export class BrowserTestUtilsChild extends JSWindowActorChild {
  actorCreated() {
    this._EventUtils = null;
  }

  get EventUtils() {
    if (!this._EventUtils) {
      // Set up a dummy environment so that EventUtils works. We need to be careful to
      // pass a window object into each EventUtils method we call rather than having
      // it rely on the |window| global.
      let win = this.contentWindow;
      let EventUtils = {
        get KeyboardEvent() {
          return win.KeyboardEvent;
        },
        // EventUtils' `sendChar` function relies on the navigator to synthetize events.
        get navigator() {
          return win.navigator;
        },
      };

      EventUtils.window = {};
      EventUtils.parent = EventUtils.window;
      EventUtils._EU_Ci = Ci;
      EventUtils._EU_Cc = Cc;

      Services.scriptloader.loadSubScript(
        "chrome://mochikit/content/tests/SimpleTest/EventUtils.js",
        EventUtils
      );

      this._EventUtils = EventUtils;
    }

    return this._EventUtils;
  }

  /**
   * Waits until painting is no longer suppressed in this window.
   */
  async waitForPaintingUnsuppressed() {
    let win = this.contentWindow;
    if (win.windowUtils.paintingSuppressed) {
      await new Promise(resolve => {
        let observe = subject => {
          if (subject == win.document) {
            Services.obs.removeObserver(observe, "before-first-paint");
            resolve();
          }
        };
        Services.obs.addObserver(observe, "before-first-paint");
      });
    }
  }

  receiveMessage(aMessage) {
    switch (aMessage.name) {
      case "Test:SynthesizeMouse": {
        return this.synthesizeMouse(aMessage.data, this.contentWindow);
      }

      case "Test:SynthesizeTouch": {
        return this.synthesizeTouch(aMessage.data, this.contentWindow);
      }

      case "Test:SendChar": {
        return this.EventUtils.sendChar(aMessage.data.char, this.contentWindow);
      }

      case "Test:SynthesizeKey":
        this.EventUtils.synthesizeKey(
          aMessage.data.key,
          aMessage.data.event || {},
          this.contentWindow
        );
        break;

      case "Test:SynthesizeComposition": {
        return this.EventUtils.synthesizeComposition(
          aMessage.data.event,
          this.contentWindow
        );
      }

      case "Test:SynthesizeCompositionChange":
        this.EventUtils.synthesizeCompositionChange(
          aMessage.data.event,
          this.contentWindow
        );
        break;

      case "BrowserTestUtils:WaitForPaintingUnsuppressed": {
        return this.waitForPaintingUnsuppressed();
      }

      case "BrowserTestUtils:StartObservingTopics": {
        this.observer = new BrowserTestUtilsChildObserver();
        this.observer.startObservingTopics(aMessage.data.topics);
        break;
      }

      case "BrowserTestUtils:StopObservingTopics": {
        if (this.observer) {
          this.observer.stopObservingTopics(aMessage.data.topics);
          this.observer = null;
        }
        break;
      }

      case "BrowserTestUtils:ObserveTopic": {
        return new Promise(resolve => {
          let filterFn;
          if (aMessage.data.filterFunctionSource) {
            // eslint-disable-next-line mozilla/reject-globalThis-modification
            let sb = Cu.Sandbox(globalThis, {
              sandboxPrototype: {
                __proto__: globalThis,
                content: this.contentWindow,
              },
            });
            filterFn = Cu.evalInSandbox(
              `(() => (${aMessage.data.filterFunctionSource}))()`,
              sb
            );
          }

          let observer = this.observer || new BrowserTestUtilsChildObserver();
          observer.observeTopic(
            aMessage.data.topic,
            aMessage.data.count,
            filterFn,
            resolve
          );
        });
      }

      case "BrowserTestUtils:CrashFrame": {
        // This is to intentionally crash the frame.
        // We crash by using js-ctypes. The crash
        // should happen immediately
        // upon loading this frame script.

        const { ctypes } = ChromeUtils.importESModule(
          "resource://gre/modules/ctypes.sys.mjs"
        );

        let dies = function () {
          dump("\nEt tu, Brute?\n");
          ChromeUtils.privateNoteIntentionalCrash();

          try {
            // Annotate test failure to allow callers to separate intentional
            // crashes from unintentional crashes.
            Services.appinfo.annotateCrashReport("TestKey", "CrashFrame");
          } catch (e) {
            dump(`Failed to annotate crash in CrashFrame: ${e}\n`);
          }

          switch (aMessage.data.crashType) {
            case "CRASH_OOM": {
              let debug = Cc["@mozilla.org/xpcom/debug;1"].getService(
                Ci.nsIDebug2
              );
              debug.crashWithOOM();
              break;
            }
            case "CRASH_SYSCALL": {
              if (Services.appinfo.OS == "Linux") {
                let libc = ctypes.open("libc.so.6");
                let chroot = libc.declare(
                  "chroot",
                  ctypes.default_abi,
                  ctypes.int,
                  ctypes.char.ptr
                );
                chroot("/");
              }
              break;
            }
            case "CRASH_INVALID_POINTER_DEREF": // Fallthrough
            default: {
              // Dereference a bad pointer.
              let zero = new ctypes.intptr_t(8);
              let badptr = ctypes.cast(
                zero,
                ctypes.PointerType(ctypes.int32_t)
              );
              badptr.contents;
            }
          }
        };

        if (aMessage.data.asyncCrash) {
          let { setTimeout } = ChromeUtils.importESModule(
            "resource://gre/modules/Timer.sys.mjs"
          );
          // Get out of the stack.
          setTimeout(dies, 0);
        } else {
          dies();
        }
      }
    }

    return undefined;
  }

  handleEvent(aEvent) {
    switch (aEvent.type) {
      case "DOMContentLoaded":
      case "load": {
        this.sendAsyncMessage(aEvent.type, {
          internalURL: aEvent.target.documentURI,
          visibleURL: aEvent.target.location
            ? aEvent.target.location.href
            : null,
        });
        break;
      }
    }
  }

  #getTarget(data, window) {
    let { target, targetFn } = data;
    if (typeof target == "string") {
      return this.document.querySelector(target);
    }
    if (typeof targetFn == "string") {
      let sb = Cu.Sandbox(window, { sandboxPrototype: window });
      return Cu.evalInSandbox(`(${targetFn})()`, sb);
    }
    return null;
  }

  synthesizeMouse(data, window) {
    let target = this.#getTarget(data, window);

    let left = data.x;
    let top = data.y;
    if (target) {
      if (target.ownerDocument !== this.document) {
        // Account for nodes found in iframes.
        let cur = target;
        do {
          // eslint-disable-next-line mozilla/use-documentGlobal
          let frame = cur.ownerDocument.defaultView.frameElement;
          let rect = frame.getBoundingClientRect();

          left += rect.left;
          top += rect.top;

          cur = frame;
        } while (cur && cur.ownerDocument !== this.document);

        // node must be in this document tree.
        if (!cur) {
          throw new Error("target must be in the main document tree");
        }
      }

      let rect = target.getBoundingClientRect();
      left += rect.left;
      top += rect.top;

      if (data.event.centered) {
        left += rect.width / 2;
        top += rect.height / 2;
      }
    } else {
      dump(
        `Target not found for selector ${data.target} ` +
          `and targetFn ${data.targetFn} ` +
          `in document ${this.document.documentURI}\n`
      );
    }

    let result;

    lazy.E10SUtils.wrapHandlingUserInput(window, data.handlingUserInput, () => {
      if (data.event && data.event.wheel) {
        this.EventUtils.synthesizeWheelAtPoint(left, top, data.event, window);
      } else {
        result = this.EventUtils.synthesizeMouseAtPoint(
          left,
          top,
          data.event,
          window
        );
      }
    });

    return result;
  }

  synthesizeTouch(data, window) {
    let target = this.#getTarget(data, window);

    if (target) {
      if (target.ownerDocument !== this.document) {
        // Account for nodes found in iframes.
        let cur = target;
        do {
          cur = cur.documentGlobal.frameElement;
        } while (cur && cur.ownerDocument !== this.document);

        // node must be in this document tree.
        if (!cur) {
          throw new Error("target must be in the main document tree");
        }
      }
    }

    return this.EventUtils.synthesizeTouch(
      target,
      data.x,
      data.y,
      data.event,
      window
    );
  }
}
