/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.password.parser.csv

import kotlinx.parcelize.Parcelize
import mozilla.components.concept.fileparser.FileParserError

/** An error type representing possible errors while parsing a file for passwords */
@Parcelize
sealed interface PasswordsParseError : FileParserError {

    /** The file is missing a required column and can not be parsed. */
    data object FileFormat : PasswordsParseError

    /**
     * Two distinct columns in the header map to the same logical field. For example, a file containing both `url` and
     * `login_uri`.
     */
    data object ConflictingColumns : PasswordsParseError

    /** Some other uncaught exception happened during parsing. */
    data object Unexpected : PasswordsParseError
}
