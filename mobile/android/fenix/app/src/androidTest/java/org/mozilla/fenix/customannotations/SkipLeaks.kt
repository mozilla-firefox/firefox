/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.customannotations

/**
 * Custom annotation to skip memory leak checks in tests
 *
 * You can annotate a test function to disable memory leak checks for the test
 *
 * @param reasons Reasons for skipping leak detection, typically the bugzilla bug link for it
 */
@Target(AnnotationTarget.FUNCTION)
@Retention(AnnotationRetention.RUNTIME)
annotation class SkipLeaks(
    val reasons: Array<String> = [],
)
