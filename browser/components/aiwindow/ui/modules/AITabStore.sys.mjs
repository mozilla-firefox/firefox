/*
 This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import {
  CURRENT_SCHEMA_VERSION,
  DB_FILE_NAME,
  PREF_BRANCH,
} from "moz-src:///browser/components/aiwindow/ui/modules/AITabConstants.sys.mjs";

import { migrations } from "moz-src:///browser/components/aiwindow/ui/modules/AITabMigrations.sys.mjs";
import {
  DEFAULT_AITAB_PAGE_TYPE,
  getAITabPageType,
  isAITabPageEditable,
} from "moz-src:///browser/components/aiwindow/ui/modules/AITabPageTypes.sys.mjs";
import {
  AITAB_PAGES_TABLE,
  AITAB_PAGES_SLUG_VERSION_INDEX,
  AITAB_PAGE_INSERT,
  GET_NEXT_VERSION,
  GET_AITAB_BY_SLUG,
  GET_AITAB_BY_SLUG_AND_VERSION,
  GET_AITAB_VERSIONS_BY_SLUG,
  GET_AITAB_PAGES_BY_CONV_ID,
  DELETE_AITAB_PAGES_BY_SLUG,
  SLUG_EXISTS,
  DELETE_AITAB_VERSIONS_BEFORE,
} from "moz-src:///browser/components/aiwindow/ui/modules/AITabSql.sys.mjs";
import { SQLiteStoreBase } from "moz-src:///browser/components/aiwindow/ui/modules/SQLiteStoreBase.sys.mjs";
import {
  parseJSONOrNull,
  toJSONOrNull,
} from "moz-src:///browser/components/aiwindow/ui/modules/ChatUtils.sys.mjs";

/**
 * Represents a row from the aitab_pages table in the
 * ai-tab-pages-store.sqlite database.
 *
 * @typedef {object} AITabRow
 * @property {string} uuid - Identifier for this version.
 * @property {string} convId - Conversation that owns the page.
 * @property {string|null} toolConvId - Conversation used to generate the page.
 * @property {string} slug - URL identifier shared across versions.
 * @property {number} version - Sequential version number.
 * @property {number} pageType - One of AITAB_PAGE_TYPES.
 * @property {string} title - Page title.
 * @property {number} createdAt - Creation time in microseconds since the Unix epoch.
 * @property {number} updatedAt - Update time in microseconds since the Unix epoch.
 * @property {object|null} context - Inputs used to generate the page.
 * @property {*} components - Component data decoded from stored JSON.
 * @property {object|null} localState - Saved component state, such as checkboxes.
 */

// How far #mintSlug will count before giving up on a readable slug.
const MAX_SLUG_SUFFIX = 50;

/**
 * @param {Date} date
 * @returns {string} The local calendar date as YYYY-MM-DD.
 */
function formatLocalDate(date) {
  const month = String(date.getMonth() + 1).padStart(2, "0");
  const day = String(date.getDate()).padStart(2, "0");
  return `${date.getFullYear()}-${month}-${day}`;
}

/**
 * Simple interface to store and retrieve AITab UI specific data
 *
 * @augments {SQLiteStoreBase}
 */
class AITabStore extends SQLiteStoreBase {
  get logPrefix() {
    return "AITabStore";
  }

  get logLevelPref() {
    return "browser.smartwindow.aiTabStore.logLevel";
  }

  get shutdownBlockerName() {
    return "AITabStore: Shutdown";
  }

  get CURRENT_SCHEMA_VERSION() {
    return CURRENT_SCHEMA_VERSION;
  }

  get databaseFileName() {
    return DB_FILE_NAME;
  }

  get prefBranch() {
    return PREF_BRANCH;
  }

  get createEntityStatements() {
    return [AITAB_PAGES_TABLE, AITAB_PAGES_SLUG_VERSION_INDEX];
  }

  get migrations() {
    return migrations;
  }

  /**
   * Creates a new tab at version 1.
   *
   * `page.slug` is the slug the caller would like; the stored slug is derived
   * from it and is returned on the result, so callers must read it back
   * rather than assume they got what they asked for. Slugs come from page
   * titles, which repeat, so this is the layer that settles who holds one.
   *
   * @param {object} page - Page fields: convId, slug, title, and optional
   *   pageType (DEFAULT_AITAB_PAGE_TYPE when omitted), toolConvId, context,
   *   components, localState
   * @returns {Promise<object>} The persisted page (with the slug it was
   *   stored under, plus generated uuid, version, and timestamps)
   */
  async create(page) {
    return this.#insertNextVersion(page, { expectNew: true });
  }

  /**
   * Persists an edit as a new version of an existing tab. The version is the
   * current highest version for the slug plus one, and it keeps the page type
   * of the version before it. Rejects if the slug has no existing version
   * (use create instead), or if its page type cannot be edited.
   *
   * @param {object} page - Page fields: convId, slug, title, and optional
   *   toolConvId, context, components, localState. A pageType is ignored.
   * @returns {Promise<object>} The newly persisted version
   */
  async edit(page) {
    return this.#insertNextVersion(page, { expectNew: false });
  }

  /**
   * Gets the latest version of the tab with the given slug. This is the common
   * "load the page" query: a slug-only URL implies the newest version.
   *
   * @param {string} slug
   * @returns {Promise<AITabRow|null>} The latest version, or null if no tab matches
   */
  async getBySlug(slug) {
    await this.ensureDatabase();

    const rows = await this.connection.executeCached(GET_AITAB_BY_SLUG, {
      slug,
    });

    return rows.length ? this.#parseRow(rows[0]) : null;
  }

  /**
   * Gets a specific version of the tab with the given slug.
   *
   * @param {string} slug
   * @param {number} version
   * @returns {Promise<?object>} The matching version, or null if none exists
   */
  async getBySlugAndVersion(slug, version) {
    await this.ensureDatabase();

    const rows = await this.connection.executeCached(
      GET_AITAB_BY_SLUG_AND_VERSION,
      { slug, version }
    );

    return rows.length ? this.#parseRow(rows[0]) : null;
  }

  /**
   * Gets the available version numbers for the tab with the given slug, newest
   * first. Backs the history / undo affordance (which versions exist, and
   * whether an undo target is available).
   *
   * @param {string} slug
   * @returns {Promise<Array<number>>}
   */
  async getVersionsBySlug(slug) {
    await this.ensureDatabase();

    const rows = await this.connection.executeCached(
      GET_AITAB_VERSIONS_BY_SLUG,
      { slug }
    );

    return rows.map(row => row.getResultByName("version"));
  }

  /**
   * Gets every stored version (full page rows) for a conversation, oldest
   * first.
   *
   * @param {string} convId
   * @returns {Promise<Array<object>>}
   */
  async getAITabPagesByConvId(convId) {
    await this.ensureDatabase();

    const rows = await this.connection.executeCached(
      GET_AITAB_PAGES_BY_CONV_ID,
      { conv_id: convId }
    );

    return rows.map(row => this.#parseRow(row));
  }

  /**
   * Deletes every version of the tab with the given slug older than
   * `version`, leaving that one as the only version of the page.
   *
   * The version numbers of the rows that remain are untouched, so the next
   * write still lands above them: this prunes history without resetting it.
   *
   * @param {string} slug
   * @param {number} version - The version to keep.
   */
  async deleteVersionsBefore(slug, version) {
    await this.ensureDatabase();

    await this.connection.execute(DELETE_AITAB_VERSIONS_BEFORE, {
      slug,
      version,
    });
  }

  /**
   * Deletes every version of the tab with the given slug.
   *
   * Keyed on slug rather than conv_id to use the (slug, version) index. The
   * UNIQUE constraint on that index means a slug belongs to exactly one
   * conversation, so this cannot reach another tab's rows.
   *
   * The conversation each row names in `tool_conv_id` lives in a different
   * database file, so nothing cascades from here: callers must also delete it
   * through `ConversationStore.deleteConversationById`, using an id read off
   * the page before this call takes the row that carries it. Delete the pages
   * first — a conversation left without pages is unreachable, whereas pages
   * left without a conversation still load by slug.
   *
   * @param {string} slug
   */
  async deleteBySlug(slug) {
    await this.ensureDatabase();

    await this.connection.execute(DELETE_AITAB_PAGES_BY_SLUG, { slug });
  }

  /**
   * Converts an aitab_pages result row into a plain page object.
   *
   * @param {mozIStorageRow} row
   * @returns {object}
   */
  #parseRow(row) {
    return {
      uuid: row.getResultByName("uuid"),
      convId: row.getResultByName("conv_id"),
      toolConvId: row.getResultByName("tool_conv_id"),
      slug: row.getResultByName("slug"),
      version: row.getResultByName("version"),
      pageType: row.getResultByName("page_type"),
      title: row.getResultByName("title"),
      createdAt: row.getResultByName("created_at"),
      updatedAt: row.getResultByName("updated_at"),
      context: parseJSONOrNull(row.getResultByName("context")),
      components: parseJSONOrNull(row.getResultByName("components")),
      localState: parseJSONOrNull(row.getResultByName("localstate")),
    };
  }

  /**
   * Inserts the next version row for a slug. A create mints a free slug and
   * stores version 1; an edit addresses the slug it was given and stores
   * MAX(existing version) + 1. Resolving the slug or version and inserting
   * the row run in one transaction, so two writers cannot settle on the same
   * pair.
   *
   * Keyed on slug rather than conv_id because the slug is the page's identity
   * and what UNIQUE (slug, version) is enforced on; a conversation can hold
   * more than one page.
   *
   * @param {object} page
   * @param {string} page.convId - Conversation id the page belongs to
   * @param {?string} [page.toolConvId] - Id of the conversation the page was
   *   composed in, in the ConversationStore. Null when it is not known, which
   *   is the case for every page stored before the column existed.
   * @param {string} page.slug - Opaque URL token supplied by the caller
   * @param {number} [page.pageType] - One of AITAB_PAGE_TYPES, for a create.
   *   An edit keeps the type already stored for the slug.
   * @param {string} page.title - Human-readable title of the page
   * @param {*} [page.context] - Context describing how the page was created
   * @param {*} [page.components] - Component list describing how the page renders
   * @param {*} [page.localState] - Component state (checkboxes, etc.)
   * @param {object} opts
   * @param {boolean} opts.expectNew - True for create (mint a free slug and
   *   store version 1); false for edit (a prior version must exist).
   * @returns {Promise<object>}
   */
  async #insertNextVersion(
    {
      convId,
      toolConvId = null,
      slug,
      pageType = DEFAULT_AITAB_PAGE_TYPE,
      title,
      context = null,
      components = null,
      localState = null,
    },
    { expectNew }
  ) {
    if (expectNew && !getAITabPageType(pageType)) {
      throw new Error(`create() called with unknown page type ${pageType}`);
    }

    await this.ensureDatabase();

    const uuid = crypto.randomUUID();
    const now = Date.now() * 1000;
    let version;
    // A create can be moved off the slug it asked for; an edit names a page
    // that already exists and has to keep it.
    let storedSlug = slug;
    let storedPageType = pageType;

    await this.connection
      .executeTransaction(async () => {
        if (expectNew) {
          storedSlug = await this.#mintSlug(slug);
          version = 1;
        } else {
          const rows = await this.connection.execute(GET_NEXT_VERSION, {
            slug,
          });
          version = rows[0].getResultByName("next_version");

          // version === 1 means this slug has no prior rows.
          if (version === 1) {
            throw new Error(
              `edit() called for unknown tab "${slug}"; use create()`
            );
          }

          storedPageType = rows[0].getResultByName("page_type");
          if (!isAITabPageEditable(storedPageType)) {
            throw new Error(
              `edit() called for tab "${slug}", whose page type ${storedPageType} cannot be edited`
            );
          }
        }

        await this.connection.executeCached(AITAB_PAGE_INSERT, {
          uuid,
          conv_id: convId,
          tool_conv_id: toolConvId,
          slug: storedSlug,
          version,
          page_type: storedPageType,
          title,
          created_at: now,
          updated_at: now,
          context: toJSONOrNull(context),
          components: toJSONOrNull(components),
          localstate: toJSONOrNull(localState),
        });
      })
      .catch(e => {
        this.log.error(
          "Could not insert AITab page version",
          e.message,
          e.stack
        );
        throw e;
      });

    return {
      uuid,
      convId,
      toolConvId,
      slug: storedSlug,
      version,
      pageType: storedPageType,
      title,
      createdAt: now,
      updatedAt: now,
      context,
      components,
      localState,
    };
  }

  /**
   * A slug no tab holds yet, derived from `base`.
   *
   * Titles repeat. A taken slug gets today's date, then a counter:
   * `base`, `base_YYYY-MM-DD`, `base_YYYY-MM-DD_1`, ...
   * The check and the save happen together, so two pages saved at once can't get the same slug.
   *
   * Only called for a create, so `base` itself is available far more often
   * than not and the loop usually ends on its first pass.
   *
   * @param {string} base - The slug the caller asked for.
   * @returns {Promise<string>}
   */
  async #mintSlug(base) {
    const dated = `${base}_${formatLocalDate(new Date())}`;
    const candidates = [base, dated];
    for (let suffix = 1; suffix <= MAX_SLUG_SUFFIX; suffix++) {
      candidates.push(`${dated}_${suffix}`);
    }

    for (const candidate of candidates) {
      const rows = await this.connection.execute(SLUG_EXISTS, {
        slug: candidate,
      });
      if (!rows[0].getResultByName("taken")) {
        return candidate;
      }
    }

    // Enough same-titled pages to exhaust the numbered suffixes. Readability
    // is already lost by this point, so take a slug that needs no search.
    return `${dated}_${crypto.randomUUID().slice(0, 8)}`;
  }
}

const aiTabStore = new AITabStore();
export { aiTabStore as AITabStore };
