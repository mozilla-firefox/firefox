/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

class SessionHistoryWatcher {
  #entriesByKey = {};
  #sessionHistory;
  #sessionHistoryListener;
  #currentEntry = null;

  /**
   * Start watching (from the parent process) for session history updates related to a
   * given Watcher Actor.
   *
   * @param {WatcherActor} watcherActor: The watcher actor from which we should observe
   *        document event
   * @param {object} options
   * @param {Function} options.onAvailable
   * @param {Function} options.onUpdated
   * @param {Function} options.onDestroyed
   */
  async watch(watcherActor, { onAvailable, onUpdated, onDestroyed }) {
    this.watcherActor = watcherActor;
    this.onAvailable = onAvailable;
    this.onUpdated = onUpdated;
    this.onDestroyed = onDestroyed;

    this.#sessionHistory = BrowsingContext.get(
      this.watcherActor.browserElement.browsingContext.id
    ).sessionHistory;

    this.#processSessionHistory();
    this.#currentEntry = this.#sessionHistory.getEntryAtIndex(
      this.#sessionHistory.index
    );

    this.#sessionHistoryListener = {
      QueryInterface: ChromeUtils.generateQI([
        "nsISHistoryListener",
        "nsISupportsWeakReference",
      ]),
      OnHistoryCommit: () => {
        const newEntry = this.#sessionHistory.getEntryAtIndex(
          this.#sessionHistory.index
        );
        const isUpdate = this.#currentEntry?.sharesDocumentWith(newEntry);
        this.#currentEntry = newEntry;
        this.#processSessionHistory(isUpdate);
      },
      OnEntryUpdated: entry => {
        const title = entry.title;
        const url = entry.URI.spec;
        const name = entry.name;
        this.onUpdated([
          {
            resourceId: this.resourceId,
            resourceUpdates: {
              sessionHistoryEntry: { url, title, name, key: key(entry.ID) },
            },
            browsingContextID:
              this.watcherActor.browserElement.browsingContext.id,
          },
        ]);
      },
    };

    this.#sessionHistory.addSHistoryListener(this.#sessionHistoryListener);
  }

  get resourceId() {
    return "session-history-" + this.watcherActor.browserElement.browserId;
  }

  /**
   * Will go over the session history and call onAvailable or onUpdated.
   *
   * @param {boolean} isUpdate When true, delivers changes via onUpdated instead of onAvailable.
   */
  #processSessionHistory(isUpdate = false) {
    const sessionHistorydiagrams = [];
    for (const { start, end } of sameDocuments(this.#sessionHistory)) {
      const rows = createRows(
        this.#sessionHistory,
        this.#entriesByKey,
        start,
        end
      );
      sessionHistorydiagrams.push({ rows, start, end });
    }

    const resource = {
      resourceId: this.resourceId,
      browsingContextID: this.watcherActor.browserElement.browsingContext.id,
      count: this.#sessionHistory.count,
      current: this.#sessionHistory.index,
      diagrams: sessionHistorydiagrams,
      entriesByKey: this.#entriesByKey,
    };

    if (isUpdate) {
      this.onUpdated([
        {
          resourceId: this.resourceId,
          resourceUpdates: { sessionHistory: resource },
          browsingContextID:
            this.watcherActor.browserElement.browsingContext.id,
        },
      ]);
    } else {
      this.onAvailable([resource]);
    }
  }

  destroy() {
    this.#sessionHistory.removeSHistoryListener(this.#sessionHistoryListener);
    this.#sessionHistory = null;
    this.#sessionHistoryListener = null;
    this.#currentEntry = null;
  }
}

/**
 * Given a tree-like data structure, performs a pre-order (also known as tree
 * order) traversal of that tree. Since the elements can be anything, the
 * caller needs to supply a way to find how many children a specific node has
 * as well as a way to iterate children.
 *
 * @param {*} root Starting node of the traversal
 * @param {function(*): int} getChildCount Helper to find child count
 * @param {function(*, int): *} getChildAtIndex Helper to find child at index
 */
function* preOrderTraversal(root, getChildCount, getChildAtIndex) {
  const queue = [root];
  while (queue.length) {
    const entry = queue.shift();
    if (!entry) {
      continue;
    }

    for (let index = getChildCount(entry) - 1; index >= 0; --index) {
      queue.unshift(getChildAtIndex(entry, index));
    }

    yield entry;
  }
}

function key(k) {
  return `${k}`;
}

class Diagram {
  #parent;
  #entry;
  #key;
  #rows = [];
  #lookup = new Map();

  static EMPTY = Symbol("Diagram.EMPTY");

  /**
   * Create a session history diagram from a particular entry.
   *
   * @param {object|null} parent Parent node
   * @param {object} entry Entry to create the diagram from
   */
  constructor(parent, entry) {
    this.#parent = parent;
    this.#entry = entry;
    this.#key = key(entry.docshellID);
  }

  get entry() {
    return this.#entry;
  }

  get key() {
    return this.#key;
  }

  get parent() {
    return this.#parent;
  }

  get rows() {
    return this.#rows;
  }

  lookup(id) {
    return this.#lookup.get(id);
  }

  /**
   * Append an entry to a row of the diagram.
   *
   * @param {object} entry New entry.
   * @param {int} index Index to append at.
   */
  addChild(entry, index) {
    const id = key(entry.docshellID);
    const row = this.#lookup.getOrInsertComputed(id, () => {
      const newRow = new Array(index).fill(Diagram.EMPTY);
      this.#rows.push({ id, newRow });
      return newRow;
    });

    row.push(entry);
  }
}

function* sameDocuments(sessionHistory) {
  let previous = {
    sharesDocumentWith() {
      return true;
    },
  };
  let start = 0;
  const size = sessionHistory.count;
  for (let index = 0; index < size; index++) {
    const entry = sessionHistory.getEntryAtIndex(index);
    const sameDocument = previous.sharesDocumentWith(entry);
    previous = entry;
    if (sameDocument) {
      continue;
    }

    yield { start, end: index };
    start = index;
  }

  yield { start, end: size };
}

function createRows(sessionHistory, entriesByKey, start, end) {
  const lookup = new Map();

  const size = end - start;
  // We add a rootDiagram for convenience to be able to add top-level entries
  // to it. This diagram will never be rendered to the full diagram, and the
  // id will never be used in lookup.
  const rootDiagram = new Diagram(null, { docshellID: "fakeDocShellID" });
  const getChildCount = entry => entry.childCount;
  const getChildAt = (entry, index) => entry.GetChildAt(index);
  for (let index = start; index < end; index++) {
    const root = sessionHistory.getEntryAtIndex(index);
    rootDiagram.addChild(root, index - start);
    lookup.getOrInsertComputed(key(root.docshellID), () => {
      return new Diagram(rootDiagram, root);
    });

    for (const entry of preOrderTraversal(root, getChildCount, getChildAt)) {
      const parent = entry.parent;
      if (parent) {
        const diagram = lookup.get(key(parent.docshellID));
        diagram.addChild(entry, index - start);
        lookup.getOrInsertComputed(key(entry.docshellID), () => {
          return new Diagram(diagram, entry);
        });
      }
    }
  }

  return Array.from(
    preOrderTraversal(
      rootDiagram,
      entry => entry.rows.length,
      ({ rows }, index) => lookup.get(rows[index].id)
    ),
    diagram => {
      if (!diagram.parent) {
        return [];
      }

      const row = [];
      const entries = diagram.parent.lookup(diagram.key);
      for (let count = entries.length; count > size; count--) {
        entries.push(Diagram.EMPTY);
      }
      while (entries.length < size) {
        entries.push(Diagram.EMPTY);
      }
      let previous = Diagram.EMPTY;
      let newEntry = null;
      while (entries.length) {
        const entry = entries.shift();

        if (entry !== Diagram.EMPTY && previous.ID != entry.ID) {
          const url = entry.URI.spec;
          newEntry = {
            age: 1,
            key: key(entry.ID),
            sameDocNav:
              previous !== Diagram.EMPTY && entry.sharesDocumentWith(previous),
          };
          const parent = entry.parent?.navigationId;
          const extra = {};
          if (parent) {
            extra.parent = key(parent);
          }
          const numChildren = getChildCount(entry);
          if (numChildren) {
            const children = new Array(numChildren);
            for (let index = 0; index < numChildren; ++index) {
              children[index] = getChildAt(entry, index)?.navigationId;
            }
            extra.children = children.map(key);
          }

          entriesByKey[key(entry.ID)] = {
            url,
            title: entry.title,
            name: entry.name,
            id: key(entry.navigationId),
            key: key(entry.navigationKey),
            bfcache: entry.isInBFCache,
            ...extra,
          };
          row.push(newEntry);
        } else if (!newEntry) {
          newEntry = { age: 1 };
          row.push(newEntry);
        } else if (entry !== Diagram.EMPTY || previous === Diagram.EMPTY) {
          newEntry.age++;
        } else {
          newEntry = { age: 1 };
          row.push(newEntry);
        }
        previous = entry;
      }

      return row;
    }
  ).filter(row => row.length);
}

module.exports = SessionHistoryWatcher;
