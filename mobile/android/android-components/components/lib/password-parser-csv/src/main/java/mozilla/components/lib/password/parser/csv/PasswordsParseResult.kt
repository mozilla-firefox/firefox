/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.password.parser.csv

import mozilla.components.concept.storage.LoginEntry

/**
 * Result of parsing a passwords file.
 *
 * @property logins The successfully parsed [LoginEntry] records.
 * @property skippedRowCount The number of rows that were skipped because a required value (origin, username, or
 *   password) was empty.
 */
data class PasswordsParseResult(
    val logins: List<LoginEntry>,
    val skippedRowCount: Int,
)
