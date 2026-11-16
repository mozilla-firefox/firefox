/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

import { MEMORY_FILTER_COMPARATOR } from "moz-src:///browser/components/aiwindow/services/MemoryStoreConstants.sys.mjs";

// Digest fields that EQUAL_TO and IN accept
const FILTER_EQUALITY_FIELDS = [
  "status",
  "unread",
  "memory_id",
  "ai_tab_slug",
  "schedule.type",
];

// Digest time fields that EQUAL_TO and the ordering comparators accept. The
// ordering comparators never match a null time; use EQUAL_TO null to find one.
const FILTER_TIME_FIELDS = [
  "last_run_time",
  "last_read_time",
  "next_run_time",
  "created_at",
  "updated_at",
];

// Digest attribute filters keyed by comparator name
// Each specifies valid fields it can compare, constraints on the value, and the filter's predicate
export const DIGEST_FILTERS = new Map([
  // EQUAL_TO
  // Value cannot be undefined or an Object
  [
    MEMORY_FILTER_COMPARATOR.EQUAL_TO,
    {
      fields: [...FILTER_EQUALITY_FIELDS, ...FILTER_TIME_FIELDS],
      isValidValue: value => value !== undefined && value !== Object(value),
      predicate: (fieldValue, value) => fieldValue === value,
    },
  ],
  // IN
  // Value must be an array
  [
    MEMORY_FILTER_COMPARATOR.IN,
    {
      fields: FILTER_EQUALITY_FIELDS,
      isValidValue: value => Array.isArray(value),
      predicate: (fieldValue, value) => value.includes(fieldValue),
    },
  ],
  // Ordering Comparators
  // Value must be a finite number and cannot be null
  ...[
    [MEMORY_FILTER_COMPARATOR.GREATER_THAN, (a, b) => a > b],
    [MEMORY_FILTER_COMPARATOR.LESS_THAN, (a, b) => a < b],
    [MEMORY_FILTER_COMPARATOR.GREATER_THAN_OR_EQUAL_TO, (a, b) => a >= b],
    [MEMORY_FILTER_COMPARATOR.LESS_THAN_OR_EQUAL_TO, (a, b) => a <= b],
  ].map(([comparator, compare]) => [
    comparator,
    {
      fields: FILTER_TIME_FIELDS,
      isValidValue: value => Number.isFinite(value),
      predicate: (fieldValue, value) =>
        fieldValue !== null && compare(fieldValue, value),
    },
  ]),
]);
