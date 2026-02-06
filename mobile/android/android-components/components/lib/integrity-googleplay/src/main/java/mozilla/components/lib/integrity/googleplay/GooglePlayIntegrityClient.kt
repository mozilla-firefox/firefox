/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.integrity.googleplay

import mozilla.components.concept.integrity.IntegrityClient
import mozilla.components.concept.integrity.IntegrityToken

class GooglePlayIntegrityClient: IntegrityClient {
    override suspend fun request(): Result<IntegrityToken> = Result.success(IntegrityToken("TOKEN"))
}
