/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import androidx.annotation.VisibleForTesting

/** Provides the dependencies used to refresh and store the Merino sports matches. */
object GlobalMerinoSportsDependencyProvider {

    @Volatile @VisibleForTesting internal var provider: Lazy<MerinoSportsProvider>? = null

    @Volatile @VisibleForTesting internal var storage: Lazy<MerinoMatchesStorage>? = null

    /**
     * Initializes the [MerinoSportsProvider] used to fetch the matches and the [MerinoMatchesStorage] used to store
     * them.
     *
     * Your application's [onCreate][android.app.Application.onCreate] method should call this method once, so that the
     * work scheduled by [MerinoSportsUpdater.startPeriodicWork] can also run when the app is started in the background.
     */
    fun initialize(provider: Lazy<MerinoSportsProvider>, storage: Lazy<MerinoMatchesStorage>) {
        this.provider = provider
        this.storage = storage
    }

    /** Returns the [MerinoSportsProvider], otherwise throw an exception if it has not been initialized. */
    internal fun requireProvider(): MerinoSportsProvider {
        return requireNotNull(provider?.value) {
            "initialize must be called before trying to access the MerinoSportsProvider"
        }
    }

    /** Returns the [MerinoMatchesStorage], otherwise throw an exception if it has not been initialized. */
    internal fun requireStorage(): MerinoMatchesStorage {
        return requireNotNull(storage?.value) {
            "initialize must be called before trying to access the MerinoMatchesStorage"
        }
    }
}
