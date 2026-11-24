/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// @ts-check

/**
 * @import { GetTextOptions, ExtractionResult, PageMetadata } from "../../pageextractor/PageExtractor"
 */

/**
 * App-independent half of the AI driver's binding layer.
 *
 * This is the Gecko side. The commands here reach Firefox's AI features through
 * actors and toolkit modules only, so they hold for any application built on
 * Gecko: Firefox Desktop, Firefox for Android, Thunderbird.
 */

import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = XPCOMUtils.declareLazy({
  PageExtractorParent: "resource://gre/actors/PageExtractorParent.sys.mjs",
});

/**
 * Bumped whenever a command is removed or its arguments change shape, so an
 * external client pinned to an older release can fail with an actionable
 * message instead of an "Unknown message" from the dispatcher. Adding a command
 * does not require a bump; clients discover those through `describe`.
 */
export const AI_DRIVER_API_VERSION = 1;

/**
 * Live objects handed back to the client as opaque ids. The ES module registry
 * keeps this module alive for the lifetime of the parent process, so entries
 * survive across the separate script evaluations that each WebDriver command
 * runs in.
 *
 * @type {Map<string, any>}
 */
export const sessions = new Map();

/**
 * The subset of toolkit-specific actors used here.
 *
 * @typedef {object} GetActor
 * @property {import("resource://gre/actors/PageExtractorParent.sys.mjs").PageExtractorParent} PageExtractor
 */

/**
 * @typedef {object} DriverContext
 * @property {<K extends keyof GetActor>(name: K) => GetActor[K]} getActor
 * @property {Map<string, any>} sessions
 */

/**
 * Commands that need nothing but actors and toolkit modules.
 *
 * @type {Record<string, (ctx: DriverContext, ...args: any[]) => any>}
 */
export const geckoCommands = {
  /**
   * @param {DriverContext} ctx
   */
  wait_for_page_ready(ctx) {
    return ctx.getActor("PageExtractor").waitForPageReady();
  },

  /**
   * @param {DriverContext} ctx
   */
  get_page_text(ctx, options) {
    return ctx.getActor("PageExtractor").getText(options ?? {});
  },

  /**
   * @param {DriverContext} ctx
   */
  get_page_metadata(ctx) {
    return ctx.getActor("PageExtractor").getPageMetadata();
  },

  /**
   * Extraction from a hidden browser needs no visible tab, so this command
   * never touches `ctx.getActor`.
   *
   * @param {DriverContext} _ctx
   * @param {string} urlString
   * @param {Partial<GetTextOptions>} [options]
   * @param {boolean} [anonymousFetch]
   */
  get_headless_page_text(_ctx, urlString, options, anonymousFetch) {
    if (typeof urlString !== "string") {
      throw new Error("Expected the URL to be a string.");
    }
    if (options && typeof options !== "object") {
      throw new Error("Extraction options must be an object");
    }
    return lazy.PageExtractorParent.getHeadlessExtractor({
      urlString,
      callback: actor => actor.getText(options ?? {}),
      anonymousFetch: Boolean(anonymousFetch),
    });
  },
};
