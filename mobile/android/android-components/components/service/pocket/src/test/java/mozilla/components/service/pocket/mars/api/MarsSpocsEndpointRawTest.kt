/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.pocket.mars.api

import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.IOException
import kotlinx.serialization.json.Json
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Headers.Names.CONTENT_TYPE
import mozilla.components.concept.fetch.Headers.Names.USER_AGENT
import mozilla.components.concept.fetch.Headers.Values.CONTENT_TYPE_APPLICATION_JSON
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.service.pocket.helpers.assertRequestParams
import mozilla.components.service.pocket.helpers.assertResponseIsClosed
import mozilla.components.service.pocket.helpers.assertSuccessfulRequestReturnsResponseBody
import mozilla.components.support.test.any
import mozilla.components.support.test.helpers.MockResponses
import mozilla.components.support.test.mock
import mozilla.components.support.test.whenever
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MarsSpocsEndpointRawTest {

    private lateinit var endpoint: MarsSpocsEndpointRaw
    private lateinit var client: Client

    private lateinit var errorResponse: Response
    private lateinit var successResponse: Response
    private lateinit var defaultResponse: Response

    @Before
    fun setUp() {
        errorResponse = MockResponses.getError()
        successResponse = MockResponses.getSuccess()
        defaultResponse = errorResponse

        client =
            mock<Client>().also {
                whenever(it.fetch(any())).thenReturn(defaultResponse)
            }

        endpoint =
            MarsSpocsEndpointRaw(
                client = client,
                config = getRequestConfig(),
            )
    }

    @Test
    fun `WHEN requesting sponsored stories with a custom request config THEN ensure the correct request parameters are used`() {
        MarsSpocsEndpointRaw.isDebugBuild = false

        val placement = Placement(placement = "newtab_mobile_spocs", count = 10)
        val config = getRequestConfig(placements = listOf(placement))
        val customEndpoint =
            MarsSpocsEndpointRaw(
                client = client,
                config = config,
            )

        assertRequestParams(
            client = client,
            makeRequest = {
                customEndpoint.getSponsoredStories()
            },
            assertParams = { request ->
                assertEquals(MARS_ENDPOINT_BASE_URL + MARS_ENDPOINT_ADS_PATH, request.url)
                assertEquals(Request.Method.POST, request.method)
                assertTrue(request.conservative)

                assertTrue(
                    request.headers!!
                        .first {
                            it.name.equals(CONTENT_TYPE, true)
                        }
                        .value
                        .contains(CONTENT_TYPE_APPLICATION_JSON, true)
                )

                assertTrue(
                    request.headers!!
                        .last {
                            it.name.equals(USER_AGENT, true)
                        }
                        .value
                        .contains(config.userAgent!!, true)
                )

                val requestBody =
                    JSONObject(
                        request.body!!.useStream {
                            it.bufferedReader().readText()
                        }
                    )

                assertEquals(config.contextId, requestBody.get(REQUEST_BODY_CONTEXT_ID_KEY))

                val placements =
                    Json.decodeFromString<List<Placement>>(requestBody.get(REQUEST_BODY_PLACEMENTS_KEY).toString())

                assertEquals(1, placements.size)
                assertEquals(placement.placement, placements.first().placement)
                assertEquals(placement.count, placements.first().count)
            },
        )
    }

    @Test
    fun `WHEN deleting a user with a custom request config THEN ensure the correct request parameters are used`() {
        val config = getRequestConfig()
        val customEndpoint =
            MarsSpocsEndpointRaw(
                client = client,
                config = config,
            )

        assertRequestParams(
            client = client,
            makeRequest = {
                customEndpoint.deleteUser()
            },
            assertParams = { request ->
                assertEquals(MARS_ENDPOINT_BASE_URL + MARS_ENDPOINT_DELETE_USER_PATH, request.url)
                assertEquals(Request.Method.DELETE, request.method)
                assertTrue(request.conservative)

                assertTrue(
                    request.headers!!
                        .first {
                            it.name.equals(CONTENT_TYPE, true)
                        }
                        .value
                        .contains(CONTENT_TYPE_APPLICATION_JSON, true)
                )

                val requestBody =
                    JSONObject(
                        request.body!!.useStream {
                            it.bufferedReader().readText()
                        }
                    )

                assertEquals(config.contextId, requestBody.get(REQUEST_BODY_CONTEXT_ID_KEY))
            },
        )
    }

    @Test
    fun `WHEN requesting sponsored stories and the client throws an IOException THEN null is returned`() {
        whenever(client.fetch(any())).thenThrow(IOException::class.java)
        assertNull(endpoint.getSponsoredStories())
    }

    @Test
    fun `WHEN requesting sponsored stories and the response is null THEN null is returned`() {
        whenever(client.fetch(any())).thenReturn(null)
        assertNull(endpoint.getSponsoredStories())
    }

    @Test
    fun `WHEN requesting sponsored stories and the response is a failure THEN null is returned`() {
        whenever(client.fetch(any())).thenReturn(errorResponse)
        assertNull(endpoint.getSponsoredStories())
    }

    @Test
    fun `WHEN requesting sponsored stories and the response is a success THEN the response body is returned`() {
        assertSuccessfulRequestReturnsResponseBody(client, endpoint::getSponsoredStories)
    }

    @Test
    fun `WHEN requesting sponsored stories and a success or error response is received THEN response is closed`() {
        assertResponseIsClosed(client, successResponse) {
            endpoint.getSponsoredStories()
        }

        assertResponseIsClosed(client, errorResponse) {
            endpoint.getSponsoredStories()
        }
    }

    @Test
    fun `WHEN deleting an user and the client throws an IOException THEN false is returned`() {
        whenever(client.fetch(any())).thenThrow(IOException::class.java)
        assertFalse(endpoint.deleteUser())
    }

    @Test
    fun `WHEN deleting an user and the response is null THEN false is returned`() {
        whenever(client.fetch(any())).thenReturn(null)
        assertFalse(endpoint.deleteUser())
    }

    @Test
    fun `WHEN deleting an user and the response is a failure THEN false is returned`() {
        whenever(client.fetch(any())).thenReturn(errorResponse)
        assertFalse(endpoint.deleteUser())
    }

    @Test
    fun `WHEN deleting an user and the response is a success THEN true is returned`() {
        whenever(client.fetch(any())).thenReturn(successResponse)
        assertTrue(endpoint.deleteUser())
    }

    @Test
    fun `WHEN deleting an user and a success or error response is received THEN response is closed`() {
        assertResponseIsClosed(client, successResponse) {
            endpoint.deleteUser()
        }

        assertResponseIsClosed(client, errorResponse) {
            endpoint.deleteUser()
        }
    }

    @Test
    fun `GIVEN a debug build WHEN querying the base url THEN use the staging endpoint`() {
        MarsSpocsEndpointRaw.isDebugBuild = true

        assertEquals(MARS_ENDPOINT_STAGING_BASE_URL, MarsSpocsEndpointRaw.baseUrl)
    }

    @Test
    fun `GIVEN a release build WHEN querying the base url THEN use the production endpoint`() {
        MarsSpocsEndpointRaw.isDebugBuild = false

        assertEquals(MARS_ENDPOINT_BASE_URL, MarsSpocsEndpointRaw.baseUrl)
    }

    private fun getRequestConfig(
        contextId: String = "contextId",
        userAgent: String = "userAgent",
        placements: List<Placement> = listOf(),
    ) =
        MarsSpocsRequestConfig(
            contextId = contextId,
            userAgent = userAgent,
            placements = placements,
        )
}
