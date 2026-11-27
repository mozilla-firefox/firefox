/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko

import android.util.JsonReader
import android.util.JsonWriter
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.ByteArrayOutputStream
import java.io.StringReader
import mozilla.components.browser.engine.gecko.facts.GeckoEngineSessionStateFacts
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.processor.CollectionProcessor
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.GeckoSession

@RunWith(AndroidJUnit4::class)
class GeckoEngineSessionStateTest {

    @Test
    fun writeTo() {
        val geckoState: GeckoSession.SessionState = mock()
        whenever(geckoState.toString()).thenReturn("<state>")

        val state = GeckoEngineSessionState(geckoState)

        val stream = ByteArrayOutputStream()
        val writer = JsonWriter(stream.writer())
        state.writeTo(writer)
        val json = JSONObject(stream.toString())

        assertEquals(1, json.length())
        assertTrue(json.has("GECKO_STATE"))
        assertEquals("<state>", json.getString("GECKO_STATE"))
    }

    @Test
    fun fromJSON() {
        val json =
            JSONObject().apply {
                put("GECKO_STATE", "{ 'foo': 'bar' }")
            }

        val state = GeckoEngineSessionState.fromJSON(json)

        assertEquals("""{"foo":"bar"}""", state.actualState.toString())
    }

    @Test
    fun `fromJSON with invalid JSON returns empty State`() {
        val json =
            JSONObject().apply {
                put("nothing", "helpful")
            }

        val state = GeckoEngineSessionState.fromJSON(json)

        assertNull(state.actualState)
    }

    @Test
    fun `from reader emits a fact with the engine state length and history entries`() {
        val engineState = """{"history":{"entries":[{"url":"https://a.test"},{"url":"https://b.test"}]}}"""
        val reader = JsonReader(StringReader(JSONObject().put("GECKO_STATE", engineState).toString()))

        CollectionProcessor.withFactCollection { facts ->
            GeckoEngineSessionState.from(reader)

            with(facts.single()) {
                assertEquals(Component.BROWSER_ENGINE_GECKO, component)
                assertEquals(GeckoEngineSessionStateFacts.Items.STATE_RESTORED, item)
                assertEquals(
                    engineState.length,
                    metadata?.get(GeckoEngineSessionStateFacts.Metadata.ENGINE_STATE_LENGTH),
                )
                assertEquals(2, metadata?.get(GeckoEngineSessionStateFacts.Metadata.HISTORY_ENTRIES))
            }
        }
    }

    @Test
    fun `from reader with an invalid engine state emits no fact`() {
        val reader = JsonReader(StringReader(JSONObject().put("GECKO_STATE", "null").toString()))

        CollectionProcessor.withFactCollection { facts ->
            val state = GeckoEngineSessionState.from(reader)

            assertNull(state.actualState)
            assertTrue(facts.isEmpty())
        }
    }

    @Test
    fun `from reader with an empty object emits no fact`() {
        CollectionProcessor.withFactCollection { facts ->
            GeckoEngineSessionState.from(JsonReader(StringReader("{}")))

            assertTrue(facts.isEmpty())
        }
    }
}
