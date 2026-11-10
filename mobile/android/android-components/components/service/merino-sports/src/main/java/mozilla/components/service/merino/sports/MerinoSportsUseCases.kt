/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import androidx.annotation.VisibleForTesting

/** Contains use cases related to the Merino sports feature. */
internal class MerinoSportsUseCases {

    /** Refresh matches use case. */
    class RefreshMatchesUseCase internal constructor() {
        /**
         * Fetches the latest matches and stores them.
         *
         * @return true if the matches were fetched and stored, false if the fetch failed.
         * @throws IllegalArgumentException if [initialize] has not been called.
         */
        suspend operator fun invoke(): Boolean {
            val matches = requireProvider().getMatches() ?: return false
            requireStorage().setMatches(matches)
            return true
        }
    }

    internal companion object {
        @Volatile @VisibleForTesting internal var provider: MerinoSportsProvider? = null

        @Volatile @VisibleForTesting internal var storage: MerinoMatchesStorage? = null

        /**
         * Initializes the [MerinoSportsProvider] used to fetch the matches and the [MerinoMatchesStorage] used to store
         * them.
         */
        internal fun initialize(provider: MerinoSportsProvider, storage: MerinoMatchesStorage) {
            this.provider = provider
            this.storage = storage
        }

        /** Unbinds the [MerinoSportsProvider] and [MerinoMatchesStorage]. */
        internal fun destroy() {
            this.provider = null
            this.storage = null
        }

        /** Returns the [MerinoSportsProvider], otherwise throw an exception if it has not been initialized. */
        internal fun requireProvider(): MerinoSportsProvider {
            return requireNotNull(provider) {
                "initialize must be called before trying to access the MerinoSportsProvider"
            }
        }

        /** Returns the [MerinoMatchesStorage], otherwise throw an exception if it has not been initialized. */
        internal fun requireStorage(): MerinoMatchesStorage {
            return requireNotNull(storage) {
                "initialize must be called before trying to access the MerinoMatchesStorage"
            }
        }
    }

    val refreshMatches: RefreshMatchesUseCase by lazy { RefreshMatchesUseCase() }
}
