/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko

import android.util.JsonReader
import android.util.JsonWriter
import java.io.IOException
import mozilla.components.browser.engine.gecko.facts.emitStateRestoredFact
import mozilla.components.concept.engine.EngineSessionState
import org.json.JSONException
import org.json.JSONObject
import org.mozilla.geckoview.GeckoSession

private const val GECKO_STATE_KEY = "GECKO_STATE"

class GeckoEngineSessionState internal constructor(internal val actualState: GeckoSession.SessionState?) :
    EngineSessionState {
    override fun writeTo(writer: JsonWriter) {
        with(writer) {
            beginObject()

            name(GECKO_STATE_KEY)
            value(actualState.toString())

            endObject()
            flush()
        }
    }

    companion object {
        fun fromJSON(json: JSONObject): GeckoEngineSessionState =
            try {
                val state = json.getString(GECKO_STATE_KEY)

                GeckoEngineSessionState(GeckoSession.SessionState.fromString(state))
            } catch (e: JSONException) {
                GeckoEngineSessionState(null)
            }

        /** Creates a [GeckoEngineSessionState] from the given [JsonReader]. */
        fun from(reader: JsonReader): GeckoEngineSessionState =
            try {
                reader.beginObject()

                val rawState =
                    if (reader.hasNext()) {
                        val key = reader.nextName()
                        if (key != GECKO_STATE_KEY) {
                            throw AssertionError("Unknown state key: $key")
                        }

                        reader.nextString()
                    } else {
                        null
                    }

                reader.endObject()

                val sessionState = rawState?.let { GeckoSession.SessionState.fromString(it) }
                if (rawState != null && sessionState != null) {
                    emitStateRestoredFact(
                        engineStateLength = rawState.length,
                        historyEntries = sessionState.size,
                    )
                }

                GeckoEngineSessionState(sessionState)
            } catch (e: IOException) {
                GeckoEngineSessionState(null)
            } catch (e: JSONException) {
                // Internally GeckoView uses org.json and currently may throw JSONException in certain cases
                // https://github.com/mozilla-mobile/android-components/issues/9332
                GeckoEngineSessionState(null)
            }
    }
}
