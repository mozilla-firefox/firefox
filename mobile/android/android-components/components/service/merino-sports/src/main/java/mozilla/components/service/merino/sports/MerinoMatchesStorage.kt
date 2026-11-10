/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import android.content.Context
import androidx.datastore.core.CorruptionException
import androidx.datastore.core.DataStore
import androidx.datastore.core.Serializer
import androidx.datastore.core.handlers.ReplaceFileCorruptionHandler
import androidx.datastore.dataStore
import java.io.InputStream
import java.io.OutputStream
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import mozilla.components.support.base.log.logger.Logger

internal const val ASSET_FILE_NAME = "merino_sports_matches.json"

private val logger = Logger("MerinoMatchesStorage")

/** A storage API for the latest [MerinoMatches] fetched from the Merino sports API. */
interface MerinoMatchesStorage {
    /** The latest stored [MerinoMatches] or null if none are stored. */
    val matches: Flow<MerinoMatches?>

    /** Replace the stored matches with [matches] or remove them if null. */
    suspend fun setMatches(matches: MerinoMatches?)

    companion object {
        /** Creates a simple in-memory implementation of [MerinoMatchesStorage] for use in tests or previews. */
        fun inMemory(initialMatches: MerinoMatches? = null): MerinoMatchesStorage =
            InMemoryMerinoMatchesStorage(initialMatches)
    }
}

private class InMemoryMerinoMatchesStorage(initialMatches: MerinoMatches?) : MerinoMatchesStorage {
    private val _matches = MutableStateFlow(initialMatches)
    override val matches: Flow<MerinoMatches?> = _matches

    override suspend fun setMatches(matches: MerinoMatches?) {
        _matches.value = matches
    }
}

/** The data source for [MerinoMatchesStorage]. */
fun MerinoMatchesStorage.Companion.dataStore(context: Context): MerinoMatchesStorage =
    DataStoreMerinoMatchesStorage(context.merinoMatchesDataStore)

/**
 * [MerinoMatchesStorage] that persists the matches to disk with a [DataStore].
 *
 * @param dataStore The [DataStore] that reads and writes the stored [MerinoMatches].
 */
internal class DataStoreMerinoMatchesStorage(private val dataStore: DataStore<MerinoMatches?>) : MerinoMatchesStorage {
    override val matches: Flow<MerinoMatches?>
        get() = dataStore.data

    override suspend fun setMatches(matches: MerinoMatches?) {
        dataStore.updateData { matches }
    }
}

/**
 * Replaces a stored file that [MerinoMatchesSerializer] cannot read with no matches, so that a corrupt file does not
 * prevent new matches from being stored.
 */
internal val merinoMatchesCorruptionHandler =
    ReplaceFileCorruptionHandler<MerinoMatches?> { exception ->
        logger.warn("Replacing the corrupt stored matches", exception)
        null
    }

/** The [DataStore] instance for the stored [MerinoMatches]. */
private val Context.merinoMatchesDataStore by
    dataStore(
        fileName = ASSET_FILE_NAME,
        serializer = MerinoMatchesSerializer,
        corruptionHandler = merinoMatchesCorruptionHandler,
    )

/** [Serializer] that reads and writes [MerinoMatches] as JSON. */
internal object MerinoMatchesSerializer : Serializer<MerinoMatches?> {
    override val defaultValue: MerinoMatches? = null

    override suspend fun readFrom(input: InputStream): MerinoMatches? {
        val text = input.readBytes().decodeToString()
        if (text.isEmpty()) {
            return null
        }

        return try {
            merinoSportsJson.decodeFromString(MerinoMatches.serializer(), text)
        } catch (e: IllegalArgumentException) {
            throw CorruptionException("Failed to deserialize the stored matches", e)
        }
    }

    override suspend fun writeTo(t: MerinoMatches?, output: OutputStream) {
        if (t != null) {
            output.write(merinoSportsJson.encodeToString(MerinoMatches.serializer(), t).encodeToByteArray())
        }
    }
}
