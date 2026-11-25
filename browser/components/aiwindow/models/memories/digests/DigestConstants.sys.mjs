/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { SCHEDULE_TYPES } from "moz-src:///browser/components/aiwindow/models/agents/Schedule.sys.mjs";

/**
 * Digest statuses
 */
export const DIGEST_STATUS = Object.freeze({
  ENABLED: "enabled",
  DISABLED: "disabled",
  SUGGESTED: "suggested",
});

/**
 * Digest schedule types
 */
export const DIGEST_SCHEDULE_TYPE = Object.freeze({
  DAILY: SCHEDULE_TYPES.DAILY,
  WEEKLY: SCHEDULE_TYPES.WEEKLY,
  ON_DEMAND: "on-demand",
});

/**
 * Digest lengths. Not stored yet: DigestStore gets a length field in
 * bug 2085182.
 */
export const DIGEST_LENGTH = Object.freeze({
  STANDARD: "standard",
  SHORT: "short",
});
