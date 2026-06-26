/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

let nextListenerId = 0;

this.downloads = class extends ExtensionAPI {
  getAPI(context) {
    const childManager = context.childManager;
    const path = "downloads.onDeterminingFilename";

    return {
      downloads: {
        onDeterminingFilename: {
          addListener(fn) {
            const map = childManager.listeners.get(path);
            if (map.listeners.has(fn)) {
              return;
            }

            const id = ++nextListenerId;

            const wrappedFn = item => {
              let suggestCalled = false;
              let suggestValue;
              let resolveAsync;

              const suggest = Cu.exportFunction(suggestion => {
                suggestCalled = true;
                suggestValue = suggestion;
                if (resolveAsync) {
                  resolveAsync(suggestion);
                }
              }, context.cloneScope);

              const result = context.applySafeWithoutClone(fn, [item, suggest]);

              if (result === true) {
                if (suggestCalled) {
                  return suggestValue;
                }
                return new Promise(resolve => {
                  resolveAsync = resolve;
                });
              }
              if (suggestCalled) {
                return suggestValue;
              }
              return result;
            };

            map.ids.set(id, wrappedFn);
            map.listeners.set(fn, id);

            childManager.conduit.sendAddListener({
              childId: childManager.id,
              listenerId: id,
              path,
              args: [],
            });
          },

          removeListener(fn) {
            const map = childManager.listeners.get(path);
            if (!map.listeners.has(fn)) {
              return;
            }

            const id = map.listeners.get(fn);
            map.listeners.delete(fn);
            map.ids.delete(id);
            map.removedIds.add(id);

            childManager.conduit.sendRemoveListener({
              childId: childManager.id,
              listenerId: id,
              path,
            });
          },

          hasListener(fn) {
            return childManager.listeners.get(path).listeners.has(fn);
          },
        },
      },
    };
  }
};
