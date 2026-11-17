/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import { MigrationUtils } from "moz-src:///browser/components/migration/MigrationUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  PlacesUtils: "resource://gre/modules/PlacesUtils.sys.mjs",
});

const kImportableProtocols = new Set(["http:", "https:", "file:", "ftp:"]);

const kMaxTreeDepth = 32;

const kArcDefaultProfile = "Default";

/**
 * Arc stores Swift dictionaries as flat arrays alternating key and value, for
 * example `[id, item, id, item]` or `["pinned", containerId, "unpinned", id]`.
 * A trailing key with no value after it is silently dropped, and a non-array
 * yields no pairs.
 *
 * @param {Array} list
 * @returns {Array<[*, *]>}
 */
function pairs(list) {
  const result = [];
  if (!Array.isArray(list)) {
    return result;
  }
  for (let i = 0; i + 1 < list.length; i += 2) {
    result.push([list[i], list[i + 1]]);
  }
  return result;
}

function asString(value) {
  return typeof value === "string" ? value : "";
}

/**
 * The Chromium profile directory an Arc profile marker refers to. Arc writes
 * `{ default: {} }` for its main profile and
 * `{ custom: { _0: { directoryBasename } } }` for the others.
 *
 * @param {object} marker
 * @returns {string}
 */
function arcProfileId(marker) {
  const directory = marker?.custom?._0?.directoryBasename;
  return typeof directory === "string" && directory
    ? directory
    : kArcDefaultProfile;
}

/**
 * @typedef {object} ArcTab
 * @property {"tab"} type Identifies a tab.
 * @property {string} url The normalized URL, see normalizeArcUrl.
 * @property {string} title The custom tab title, else the page title, else "".
 */

/**
 * A pinned folder. Split views are flattened into their parent instead.
 *
 * @typedef {object} ArcFolder
 * @property {"folder"} type Identifies a folder.
 * @property {string} title The folder name, possibly "".
 * @property {Array<ArcTab|ArcFolder>} items The folder contents, never empty.
 */

/**
 * A Favorite (top app). Folders among the Favorites are flattened.
 *
 * @typedef {object} ArcFavorite
 * @property {"tab"} type Identifies a tab.
 * @property {string} url The normalized URL, see normalizeArcUrl.
 * @property {string} title The custom tab title, else the page title, else "".
 * @property {string} profile The Chromium profile directory name, e.g.
 *   "Default".
 */

/**
 * The plan entry for one space, as returned by parseArcSpace. Subclasses that
 * override parseArcSpace may add more properties.
 *
 * @typedef {object} ArcSpace
 * @property {string} id The space's id in StorableSidebar.json.
 * @property {string} profile The Chromium profile directory name.
 * @property {string} title The space name, see parseArcSpaceTitle.
 * @property {Array<ArcTab|ArcFolder>} items The pinned items, possibly empty.
 */

/**
 * @typedef {object} ArcPlan
 * @property {ArcFavorite[]} favorites The Favorites of every profile.
 * @property {ArcSpace[]} spaces The spaces of every profile, in sidebar order.
 */

/**
 * Parses Arc's spaces, pinned tabs and Favorites from StorableSidebar.json.
 * Other browsers can extend this class and override parseArcSpace to read
 * more from each space.
 */
export class ArcMigrationUtilsClass {
  /**
   * Converts Arc's StorableSidebar.json into an import plan. `containers` is
   * a key/value list: the `global` entry is the main sidebar, and each
   * `littleBrowser` entry is the sidebar of a Little Arc window, which only
   * holds an untitled space with unpinned tabs and is skipped.
   *
   * @param {object} sidebar The parsed JSON file.
   * @returns {Promise<ArcPlan>}
   */
  async parseArcSidebar(sidebar) {
    const plan = { favorites: [], spaces: [] };
    const containers = sidebar?.sidebar?.containers;
    if (!Array.isArray(containers)) {
      return plan;
    }
    for (const [kind, container] of pairs(containers)) {
      if (
        !kind?.global ||
        !container ||
        typeof container !== "object" ||
        !Array.isArray(container.items)
      ) {
        continue;
      }
      const items = new Map();
      for (const [, item] of pairs(container.items)) {
        if (item && typeof item === "object" && typeof item.id === "string") {
          items.set(item.id, item);
        }
      }
      const childrenOf = parentId => {
        const parent = items.get(parentId);
        let children = Array.isArray(parent?.childrenIds)
          ? parent.childrenIds.map(id => items.get(id)).filter(Boolean)
          : [];
        if (!children?.length) {
          children = [...items.values()].filter(
            item => item.parentID === parentId
          );
        }
        return children;
      };
      const convert = (parentId, depth, visited = new Set([parentId])) => {
        const result = [];
        if (depth > kMaxTreeDepth) {
          return result;
        }
        for (const item of childrenOf(parentId)) {
          if (visited.has(item.id)) {
            continue;
          }
          visited.add(item.id);
          const data = item.data ?? {};
          if (data.tab) {
            const url = this.normalizeArcUrl(data.tab.savedURL);
            if (url) {
              result.push({
                type: "tab",
                url,
                title: asString(item.title) || asString(data.tab.savedTitle),
              });
            }
          } else if (data.list) {
            const folderItems = convert(item.id, depth + 1, visited);
            if (folderItems.length) {
              result.push({
                type: "folder",
                title: asString(item.title),
                items: folderItems,
              });
            }
          } else if (data.splitView) {
            result.push(...convert(item.id, depth + 1, visited));
          }
        }
        return result;
      };
      for (const [marker, containerId] of pairs(
        container.topAppsContainerIDs
      )) {
        if (typeof containerId === "string") {
          const profile = arcProfileId(marker);
          for (const favorite of this.#flattenTabs(convert(containerId, 0))) {
            plan.favorites.push({ ...favorite, profile });
          }
        }
      }
      for (const [, space] of pairs(container.spaces)) {
        if (!space || typeof space !== "object") {
          continue;
        }
        const pinnedId = pairs(space.containerIDs).find(
          ([key]) => key === "pinned"
        )?.[1];
        const profile = arcProfileId(space.profile);
        const number =
          plan.spaces.filter(entry => entry.profile === profile).length + 1;
        plan.spaces.push(
          await this.parseArcSpace(
            space,
            typeof pinnedId === "string" ? convert(pinnedId, 0) : [],
            { number }
          )
        );
      }
    }
    return plan;
  }

  /**
   * Builds the import plan entry for one Arc space. Override this to read
   * more from the space, such as its icon or theme.
   *
   * @param {object} space The space from StorableSidebar.json.
   * @param {Array<ArcTab|ArcFolder>} items The space's pinned items.
   * @param {object} [options]
   * @param {number} [options.number=1] The 1-based position of the space in
   *   its profile.
   * @returns {Promise<ArcSpace>} Overrides should spread the result of
   *   super.parseArcSpace so the base properties are kept.
   */
  async parseArcSpace(space, items, { number = 1 } = {}) {
    return {
      id: space.id,
      profile: arcProfileId(space.profile),
      title: await this.parseArcSpaceTitle(space, number),
      items,
    };
  }

  /**
   * Reads the title of an Arc space. Override this to name untitled spaces
   * differently.
   *
   * @param {object} space The space from StorableSidebar.json.
   * @param {number} number The 1-based position of the space in its profile.
   * @returns {Promise<string>} The trimmed title, or a localized numbered name
   *   for untitled spaces.
   */
  async parseArcSpaceTitle(space, number) {
    return (
      (typeof space.title === "string" && space.title.trim()) ||
      (await MigrationUtils.getLocalizedString(
        "migration-imported-arc-untitled-space",
        { number }
      ))
    );
  }

  /**
   * Checks a URL from Arc before it is imported.
   *
   * @param {string|URL} url
   * @returns {string|null} The normalized href, or null when the URL cannot be
   *   loaded as a pinned tab.
   */
  normalizeArcUrl(url) {
    if (URL.isInstance(url)) {
      url = url.href;
    }
    if (typeof url !== "string") {
      return null;
    }
    try {
      const parsed = new URL(url.trim());
      return kImportableProtocols.has(parsed.protocol) ? parsed.href : null;
    } catch {
      return null;
    }
  }

  /**
   * The part of an import plan that belongs to one Arc profile.
   *
   * @param {ArcPlan} plan
   * @param {string} [profileId] A Chromium profile directory name. Without
   *   one the whole plan is returned.
   * @returns {ArcPlan}
   */
  filterArcPlan(plan, profileId) {
    if (!profileId) {
      return plan;
    }
    const ofProfile = entry =>
      (entry.profile ?? kArcDefaultProfile) === profileId;
    return {
      favorites: plan.favorites.filter(ofProfile),
      spaces: plan.spaces.filter(ofProfile),
    };
  }

  /**
   * Converts the contents of Arc's StorableSidebar.json into bookmark trees
   * that can be passed to PlacesUtils.bookmarks.insertTree: the profile's
   * Favorites as bookmarks, followed by one folder per space. Only the pinned
   * section of each space is converted; unpinned tabs are transient and Arc
   * archives them automatically. Spaces and folders without any bookmarks are
   * skipped.
   *
   * @param {object} sidebarJSON
   *   The parsed contents of StorableSidebar.json.
   * @param {string} profileId
   *   The Chromium profile directory name (e.g. "Default", "Profile 1") whose
   *   Favorites and spaces should be returned.
   * @returns {Promise<object[]>}
   */
  async convertSidebarToBookmarks(sidebarJSON, profileId) {
    let plan = this.filterArcPlan(
      await this.parseArcSidebar(sidebarJSON),
      profileId
    );
    let toBookmarks = items =>
      items.flatMap(item => {
        if (item.type === "tab") {
          return [{ url: item.url, title: item.title }];
        }
        let children = toBookmarks(item.items);
        return children.length
          ? [
              {
                type: lazy.PlacesUtils.bookmarks.TYPE_FOLDER,
                title: item.title,
                children,
              },
            ]
          : [];
      });

    let result = toBookmarks(plan.favorites);
    for (let space of plan.spaces) {
      let children = toBookmarks(space.items);
      if (children.length) {
        result.push({
          type: lazy.PlacesUtils.bookmarks.TYPE_FOLDER,
          title: space.title,
          children,
        });
      }
    }
    return result;
  }

  #flattenTabs(items) {
    return items.flatMap(item =>
      item.type === "folder" ? this.#flattenTabs(item.items) : [item]
    );
  }
}

export const ArcMigrationUtils = new ArcMigrationUtilsClass();
