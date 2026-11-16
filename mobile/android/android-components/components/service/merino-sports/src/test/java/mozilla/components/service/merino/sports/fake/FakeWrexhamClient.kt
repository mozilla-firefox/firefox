/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports.fake

import mozilla.appservices.merino.MerinoWrexhamApiException
import mozilla.appservices.merino.WrexhamClientInterface
import mozilla.appservices.merino.WrexhamMatchesOptions

/**
 * A fake [WrexhamClientInterface] that records the [options] it received.
 *
 * @property matchesResponse The raw JSON matches response to return.
 * @property exception The [MerinoWrexhamApiException] to throw instead of returning the [matchesResponse].
 */
internal class FakeWrexhamClient(
    private val matchesResponse: String = "",
    private val exception: MerinoWrexhamApiException? = null,
) : WrexhamClientInterface {

    var options: WrexhamMatchesOptions? = null
        private set

    override fun getMatches(options: WrexhamMatchesOptions): String {
        this.options = options
        exception?.let { throw it }
        return matchesResponse
    }

    override fun getStandings(): String = throw UnsupportedOperationException()
}
