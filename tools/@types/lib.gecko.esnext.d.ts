/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * This file is for manual type definitions for things that aren't yet available
 * in TypeScript, but should be in the next target version (esnext). This
 * typically occurs when a feature has been implemented in Gecko, but not yet
 * reached ECMAScript stage 4, or multi-browser support.
 */

/**
 * Copied from TypeScript 7.1's lib.es2026.iterator.d.ts, Copyright (c)
 * Microsoft Corporation, licensed under the Apache License, Version 2.0.
 * Remove once we upgrade to TypeScript 7.1.
 */
interface IteratorConstructor {
  /**
   * Creates an iterator that yields the values of each iterable in sequence.
   * @param iterables Iterable objects to concatenate.
   */
  concat<T extends readonly unknown[] | []>(
    ...iterables: { -readonly [K in keyof T]: Iterable<T[K]> & object }
  ): IteratorObject<T[number], undefined, unknown>;
}
