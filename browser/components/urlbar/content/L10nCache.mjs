/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { UrlbarShared } from "chrome://browser/content/urlbar/UrlbarShared.mjs";

/**
 * @typedef {object} L10nCachedMessage
 *   A cached L10n message object is similar to `L10nMessage` (defined in
 *   Localization.webidl) but its attributes are stored differently for
 *   convenience.
 *
 *   For example, if we cache these strings from an ftl file:
 *
 *     foo = Foo's value
 *     bar =
 *       .label = Bar's label value
 *
 *   Then:
 *
 *     cache.get("foo")
 *     // => { value: "Foo's value", attributes: null }
 *     cache.get("bar")
 *     // => { value: null, attributes: { label: "Bar's label value" }}
 * @property {string} [value]
 *   The bare value of the string. If the string does not have a bare value
 *   (i.e., it has only attributes), this will be null.
 * @property {{[key: string]: string}|null} [attributes]
 *   A mapping from attribute names to their values. If the string doesn't have
 *   any attributes, this will be null.
 */

/**
 * This class implements a cache for l10n strings. Cached strings can be
 * accessed synchronously, avoiding the asynchronicity of `data-l10n-id` and
 * `document.l10n.setAttributes`, which can lead to text pop-in and flickering
 * as strings are fetched from Fluent. (`document.l10n.formatValueSync` is also
 * sync but should not be used since it may perform sync I/O.)
 *
 * Values stored and returned by the cache are JS objects similar to
 * `L10nMessage` objects, not bare strings. This allows the cache to store not
 * only l10n strings with bare values but also strings that define attributes
 * (e.g., ".label = My label value"). See `get` for details.
 *
 * The cache stores up to `MAX_ENTRIES_PER_ID` entries per l10n ID, and entries
 * are sorted from least recently cached to most recently cached. This only
 * matters for strings that have arguments. For strings that don't have
 * arguments, there can be only one cached value, so there can be only one cache
 * entry. But for strings that do have arguments, their cached values depend on
 * the arguments they were cached with. The cache will store up to
 * `MAX_ENTRIES_PER_ID` of the most recently cached values for a given l10n ID.
 *
 * For example, given the following string from an ftl file:
 *
 *   foo = My arg value is { $bar }
 *
 * And the following cache calls:
 *
 *   cache.add({ id: "foo", args: { bar: "aaa" }});
 *   cache.add({ id: "foo", args: { bar: "bbb" }});
 *   cache.add({ id: "foo", args: { bar: "ccc" }});
 *
 * Then three different versions of the "foo" string will be cached, from least
 * recently cached to most recently cached:
 *
 *   "My arg value is aaa"
 *   "My arg value is bbb"
 *   "My arg value is ccc"
 *
 * If `MAX_ENTRIES_PER_ID` is 3 and we cache a fourth version like this:
 *
 *   cache.add({ id: "foo", args: { bar: "zzz" }});
 *
 * Then the least recently cached version -- the "aaa" one -- will be evicted
 * from the cache, and the remaining cached versions will be:
 *
 *   "My arg value is bbb"
 *   "My arg value is ccc"
 *   "My arg value is zzz"
 */
export class L10nCache {
  static MAX_ENTRIES_PER_ID = 5;

  /**
   * @param {Localization} [l10n]
   *   [test-only] A `Localization` to use instead of `document.l10n`.
   */
  constructor(l10n) {
    this.l10n = l10n ?? document.l10n;
  }

  /**
   * Gets a cached l10n message.
   *
   * @param {object} options
   *   Options
   * @param {string} options.id
   *   The string's Fluent ID.
   * @param {object} [options.args]
   *   The Fluent arguments as passed to `l10n.setAttributes`. Required if the
   *   l10n string has arguments.
   * @returns {L10nCachedMessage|null}
   *   The cached message or null if it's not cached.
   */
  get({ id, args = undefined }) {
    return this.#messagesByArgsById.get(id)?.get(this.#argsKey(args)) ?? null;
  }

  /**
   * Fetches a string from Fluent and caches it.
   *
   * @param {object} options
   *   Options
   * @param {string} options.id
   *   The string's Fluent ID.
   * @param {object} [options.args]
   *   The Fluent arguments as passed to `l10n.setAttributes`. Required if the
   *   l10n string has arguments.
   */
  async add({ id, args = undefined }) {
    let messages = await this.l10n.formatMessages([{ id, args }]);
    if (!messages?.[0]) {
      console.error(
        "l10n.formatMessages returned an unexpected value for ID: ",
        id
      );
      return;
    }

    /** @type {L10nCachedMessage} */
    let message = { value: messages[0].value, attributes: null };
    if (messages[0].attributes) {
      // Convert `attributes` from an array of `{ name, value }` objects to one
      // object mapping names to values.
      message.attributes = messages[0].attributes.reduce((valuesByName, a) => {
        valuesByName[a.name] = a.value;
        return valuesByName;
      }, {});
    }

    this.#update({ id, args, message });
  }

  /**
   * Ensures that a string is the most recently cached for its ID. If the string
   * is not already cached, then it's fetched from Fluent. This is just a slight
   * optimization over `add` that avoids calling into Fluent unnecessarily.
   *
   * @param {object} options
   *   Options
   * @param {string} options.id
   *   The string's Fluent ID.
   * @param {object} [options.args]
   *   The Fluent arguments as passed to `l10n.setAttributes`. Required if the
   *   l10n string has arguments.
   */
  async ensure({ id, args = undefined }) {
    let message = this.get({ id, args });
    if (message) {
      this.#update({ id, args, message });
    } else {
      await this.add({ id, args });
    }
  }

  /**
   * A version of `ensure` that ensures multiple strings are cached at once.
   *
   * @param {object[]} objects
   *   An array of objects as passed to `ensure()`.
   */
  async ensureAll(objects) {
    let promises = [];
    for (let obj of objects) {
      promises.push(this.ensure(obj));
    }
    await Promise.all(promises);
  }

  /**
   * Removes a cached string.
   *
   * @param {object} options
   *   Options
   * @param {string} options.id
   *   The string's Fluent ID.
   * @param {object} [options.args]
   *   The Fluent arguments as passed to `l10n.setAttributes`. Required if the
   *   l10n string has arguments.
   */
  delete({ id, args = undefined }) {
    let messagesByArgs = this.#messagesByArgsById.get(id);
    if (messagesByArgs) {
      messagesByArgs.delete(this.#argsKey(args));
      if (!messagesByArgs.size) {
        this.#messagesByArgsById.delete(id);
      }
    }
  }

  /**
   * Removes all cached strings.
   */
  clear() {
    this.#messagesByArgsById.clear();
  }

  /**
   * Returns the number of cached messages.
   */
  size() {
    return this.#messagesByArgsById
      .values()
      .reduce((total, messagesByArg) => total + messagesByArg.size, 0);
  }

  /**
   * Sets an element's content or attribute to a cached l10n string. If the
   * string isn't cached, then this falls back to the usual
   * `document.l10n.setAttributes()` using the given l10n ID and args, which
   * means the string will pop in on a later animation frame.
   *
   * This also caches the string so that it will be ready the next time. It
   * returns a promise that will be resolved when the string has been cached.
   * Typically there's no need to await it unless you want to be sure the string
   * is cached before continuing.
   *
   * @param {Element} element
   *   The l10n string will be applied to this element.
   * @param {object} options
   *   Options object.
   * @param {string} options.id
   *   The l10n string ID.
   * @param {object} [options.args]
   *   The l10n string arguments.
   * @param {object} [options.argsHighlights]
   *   If this is set, apply substring highlighting to the corresponding l10n
   *   arguments in `args`. Each value in this object should be an array of
   *   highlights as returned by `UrlbarShared.getTokenMatches()` or
   *   `UrlbarResult.getDisplayableValueAndHighlights()`.
   * @param {string} [options.attribute]
   *   If the string applies to an attribute on the element, pass the name of
   *   the attribute. The string in the Fluent file should define a value for
   *   the attribute, like ".foo = My value". If the string applies to the
   *   element's content, leave this undefined.
   * @param {boolean} [options.parseMarkup]
   *   This controls whether the cached string is applied to the element's
   *   `textContent` or its `innerHTML`. It's not relevant if the string is
   *   applied to an attribute. Typically it should be set to true when the
   *   string is expected to contain markup. When true, the cached string is
   *   essentially assigned to the element's `innerHTML` via `setHTML`. Only
   *   a few elements and attributes are allowed.  When false, it's assigned
   *   to the element's `textContent`. It's ignored when `argsHighlights` is
   *   set, since markup is then always parsed, like `DOMLocalization` does.
   * @returns {Promise}
   *   A promise that's resolved when the string has been cached. You can ignore
   *   it and do not need to await it unless you want to make sure the string is
   *   cached before continuing.
   */
  setElementL10n(
    element,
    {
      id,
      args = undefined,
      argsHighlights = undefined,
      attribute = undefined,
      parseMarkup = false,
    }
  ) {
    // If args highlights were specified, replace the highlighted args with
    // their HTML version. They're used both to look up the cached string and
    // for `DOMLocalization`, which renders markup in translations.
    let highlighted = !attribute && argsHighlights;
    if (highlighted) {
      // Copy args to avoid modifying the caller's object, e.g. a result payload.
      args = { ...args };

      let span = element.ownerDocument.createElement("span");
      for (let key in argsHighlights) {
        UrlbarShared.addTextContentWithHighlights(
          span,
          args[key],
          argsHighlights[key]
        );
        args[key] = span.innerHTML;
      }
    }

    // If an attribute was passed in, make sure it's allowed to be localized by
    // setting `data-l10n-attrs`. This isn't strictly required for attributes
    // already in the Fluent allowlist.
    if (attribute) {
      element.setAttribute("data-l10n-attrs", attribute);
    } else {
      element.removeAttribute("data-l10n-attrs");
    }

    // If the message is cached, apply it to the element.
    let message = this.get({ id, args });
    if (highlighted) {
      // Render the markup and attributes exactly like `DOMLocalization` does.
      // `L10nOverlays` is chrome-only, so in unprivileged documents like
      // about:newtab, leave the translation to `DOMLocalization`.
      let overlays = element.documentGlobal?.L10nOverlays;
      if (message && overlays) {
        overlays.translateElement(element, {
          value: message.value,
          attributes:
            message.attributes &&
            Object.entries(message.attributes).map(([key, value]) => ({
              name: key,
              value,
            })),
        });
      }
    } else if (message) {
      if (message.attributes) {
        for (let [key, value] of Object.entries(message.attributes)) {
          element.setAttribute(key, value);
        }
      }
      if (typeof message.value == "string") {
        if (!parseMarkup) {
          element.textContent = message.value;
        } else {
          element.setHTML(message.value, {
            sanitizer: {
              elements: ["a", "br", "em", "span", "strong"],
              attributes: ["data-l10n-name", "href"],
            },
          });
        }
      }
    }

    // Set the l10n attributes. If the message wasn't cached, `DOMLocalization`
    // will do its asynchronous translation and the text content will pop in. If
    // the message was cached, then we already set the cached attributes and
    // text content above, but we set the l10n attributes anyway because some
    // tests rely on them being set. It shouldn't hurt anyway.
    element.ownerDocument.l10n.setAttributes(element, id, args);

    // Cache the string. We specifically do not do this first and await it
    // because the whole point of the l10n cache is to synchronously update the
    // element's content when possible. Here, we return a promise rather than
    // making this function async and awaiting so it's clearer to callers that
    // they probably don't need to wait for caching to finish.
    return this.ensure({ id, args });
  }

  /**
   * Removes content and attributes set by `setElementL10n()`.
   *
   * @param {Element} element
   *   The content and attributes will be removed from this element.
   * @param {object} [options]
   *   Options object.
   * @param {string} [options.attribute]
   *   If you passed an attribute to `setElementL10n()`, pass it here too.
   */
  removeElementL10n(element, { attribute = undefined } = {}) {
    if (attribute) {
      element.removeAttribute(attribute);
      element.removeAttribute("data-l10n-attrs");
    } else {
      element.textContent = "";
    }
    element.removeAttribute("data-l10n-id");
    element.removeAttribute("data-l10n-args");
  }

  /**
   * L10n ID => l10n args cache key => cached message object
   *
   * We rely on the fact that `Map` remembers insertion order to keep track of
   * which cache entries are least recent, per l10n ID. The inner `Map`s will
   * iterate their entries in order from least recently inserted to most
   * recently inserted, i.e., least recently cached to most recently cached.
   *
   * @type {Map<string, Map<string, L10nCachedMessage>>}
   */
  #messagesByArgsById = new Map();

  /**
   * Max entries per l10n ID for this cache.
   *
   * @type {number}
   */
  #maxEntriesPerId = L10nCache.MAX_ENTRIES_PER_ID;

  /**
   * Inserts a message into the cache and makes it most recently cached.
   *
   * @param {object} options
   *   Options
   * @param {string} options.id
   *   The string's Fluent ID.
   * @param {object} options.args
   *   The Fluent arguments as passed to `l10n.setAttributes`.
   * @param {L10nCachedMessage} options.message
   *   The message to cache.
   */
  #update({ id, args, message }) {
    let messagesByArgs = this.#messagesByArgsById.get(id);
    if (!messagesByArgs) {
      messagesByArgs = new Map();
      this.#messagesByArgsById.set(id, messagesByArgs);
    }

    // We rely on the fact that `Map` remembers insertion order to keep track of
    // which cache entries are least recent. To make `message` the most recent
    // for its ID, delete it from `messagesByArgs` (step 1) and then reinsert it
    // (step 2). That way it will move to the end of iteration.
    let argsKey = this.#argsKey(args);

    // step 1
    messagesByArgs.delete(argsKey);

    if (messagesByArgs.size == this.#maxEntriesPerId) {
      // The cache entries are full for this ID. Remove the least recently
      // cached entry, which will be the first entry returned by the map's
      // iterator.
      messagesByArgs.delete(messagesByArgs.keys().next().value);
    }

    // step 2
    messagesByArgs.set(argsKey, message);
  }

  /**
   * Returns a cache key for the inner `Maps` inside `#messagesByArgsById`.
   * These `Map`s are keyed on l10n args.
   *
   * @param {object} args
   *   The Fluent arguments as passed to `l10n.setAttributes`.
   * @returns {string}
   *   The args cache key.
   */
  #argsKey(args) {
    // `JSON.stringify` doesn't guarantee a particular ordering of object
    // properties, so instead of stringifying `args` as is, sort its entries by
    // key and then pull out the values. The final key is a JSON'ed array of
    // sorted-by-key `args` values.
    let argValues = Object.entries(args ?? [])
      .sort(([key1], [key2]) => key1.localeCompare(key2))
      .map(([_, value]) => value);
    return JSON.stringify(argValues);
  }
}
