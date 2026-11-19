/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.settings.privacy

import java.security.cert.X509Certificate
import mozilla.components.concept.engine.EngineSession
import mozilla.components.support.test.any
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.mockito.Mockito.doAnswer
import org.mockito.Mockito.mock
import org.mockito.Mockito.times
import org.mockito.Mockito.verify

class QwacStatusRequestTest {

    private val engineSession = mock(EngineSession::class.java)
    private val qwac = mock(X509Certificate::class.java)
    private var qwacStatusCallback: ((X509Certificate?) -> Unit)? = null

    @Before
    fun setUp() {
        doAnswer {
                @Suppress("UNCHECKED_CAST")
                qwacStatusCallback = it.arguments[0] as (X509Certificate?) -> Unit
            }
            .`when`(engineSession)
            .qwacStatus(any())
    }

    @Test
    fun `WHEN created THEN the QWAC status is requested once`() {
        val request = QwacStatusRequest(engineSession)

        request.observe {}
        request.observe {}

        verify(engineSession, times(1)).qwacStatus(any())
    }

    @Test
    fun `GIVEN observers added before the result WHEN it arrives THEN they all receive it`() {
        val request = QwacStatusRequest(engineSession)
        val results = mutableListOf<X509Certificate?>()

        request.observe { results += it }
        request.observe { results += it }
        deliverResult(qwac)

        assertEquals(listOf(qwac, qwac), results)
    }

    @Test
    fun `GIVEN the result has arrived WHEN an observer is added THEN it receives the result right away`() {
        val request = QwacStatusRequest(engineSession)
        deliverResult(null)
        var received = false

        request.observe {
            assertEquals(null, it)
            received = true
        }

        assertTrue(received)
    }

    @Test
    fun `GIVEN no engine session THEN no result arrives`() {
        val request = QwacStatusRequest(engineSession = null)
        var received = false

        request.observe { received = true }

        assertFalse(received)
    }

    private fun deliverResult(result: X509Certificate?) {
        qwacStatusCallback!!.invoke(result)
    }
}
