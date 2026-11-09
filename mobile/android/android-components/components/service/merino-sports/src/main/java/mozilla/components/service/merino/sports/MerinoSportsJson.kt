/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import kotlinx.serialization.json.Json

/** [Json] used to read and write [MerinoMatches] in [MerinoMatchesSerializer] and [MerinoSportsProvider]. */
internal val merinoSportsJson = Json {
    ignoreUnknownKeys = true
    useAlternativeNames = false
}
