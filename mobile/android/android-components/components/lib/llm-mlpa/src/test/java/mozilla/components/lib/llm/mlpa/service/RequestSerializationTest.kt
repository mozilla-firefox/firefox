/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.lib.llm.mlpa.service

import kotlinx.serialization.json.Json
import mozilla.components.concept.integrity.IntegrityToken
import org.junit.Assert.assertEquals
import org.junit.Test

class RequestSerializationTest {

    val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }

    @Test
    fun `authentication service request gets serialized to json correctly`() {
        val request = AuthenticationService.Request(
            userId = UserId("my-user-id"),
            integrityToken = IntegrityToken(value = "my-integrity-token"),
            packageName = PackageName("my.package.name"),
        )

        assertEquals(
            "{" +
                "\"user_id\":\"my-user-id\"," +
                "\"integrity_token\":\"my-integrity-token\"," +
                "\"package_name\":\"my.package.name\"" +
                "}",
            json.encodeToString(request),
        )
    }

    @Test
    fun `chat service completion request gets serialized to json correctly`() {
        val request = ChatService.Request(
            model = ChatService.Request.ModelID.mozSummarization,
            messages = listOf(
                ChatService.Request.Message.system("system prompt"),
                ChatService.Request.Message.user("hello"),
            ),
        )

        assertEquals(
            "{\"model\":\"moz-summarization\",\"messages\":[{\"role\":\"system\",\"content\":\"system prompt\"},{\"role\":\"user\",\"content\":\"hello\"}],\"stream\":true,\"temperature\":0.1,\"top_p\":0.01}",
            json.encodeToString(request),
        )
    }
}
