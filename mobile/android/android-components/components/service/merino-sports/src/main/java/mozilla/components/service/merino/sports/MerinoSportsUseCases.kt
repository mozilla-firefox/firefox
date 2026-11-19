/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

/** Contains use cases related to the Merino sports feature. */
internal class MerinoSportsUseCases {

    /** Refresh matches use case. */
    class RefreshMatchesUseCase internal constructor() {
        /**
         * Fetches the latest matches and stores them.
         *
         * @return true if the matches were fetched and stored, false if the fetch failed.
         * @throws IllegalArgumentException if [GlobalMerinoSportsDependencyProvider.initialize] has not been called.
         */
        suspend operator fun invoke(): Boolean {
            val matches = GlobalMerinoSportsDependencyProvider.requireProvider().getMatches() ?: return false
            GlobalMerinoSportsDependencyProvider.requireStorage().setMatches(matches)
            return true
        }
    }

    val refreshMatches: RefreshMatchesUseCase by lazy { RefreshMatchesUseCase() }
}
