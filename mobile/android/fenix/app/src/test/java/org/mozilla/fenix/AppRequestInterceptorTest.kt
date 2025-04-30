/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix
import androidx.navigation.NavController
import io.mockk.every
import io.mockk.mockk
import io.mockk.spyk
import mozilla.components.browser.errorpages.ErrorPages
import mozilla.components.browser.errorpages.ErrorType
import mozilla.components.concept.engine.request.RequestInterceptor
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.AppRequestInterceptor.Companion.HIGH_RISK_ERROR_PAGES
import org.mozilla.fenix.AppRequestInterceptor.Companion.LOW_AND_MEDIUM_RISK_ERROR_PAGES
import org.mozilla.fenix.GleanMetrics.ErrorPage
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.mozilla.fenix.helpers.FenixRobolectricTestRunner

@RunWith(FenixRobolectricTestRunner::class)
class AppRequestInterceptorTest {

    @get:Rule
    val gleanTestRule = FenixGleanTestRule(testContext)

    private lateinit var interceptor: RequestInterceptor
    private lateinit var navigationController: NavController

    @Before
    fun setUp() {
        every { testContext.settings() } returns mockk(relaxed = true)

        navigationController = mockk(relaxed = true)
        interceptor = spyk(
            AppRequestInterceptor(testContext).also {
                it.setNavigationController(navigationController)
            },
        )

        every { (interceptor as AppRequestInterceptor).isConnected() } returns true
    }

    @Test
    fun `onErrorRequest results in correct error page for low risk level error`() {
        setOf(
            ErrorType.UNKNOWN,
            ErrorType.ERROR_NET_INTERRUPT,
            ErrorType.ERROR_NET_TIMEOUT,
            ErrorType.ERROR_CONNECTION_REFUSED,
            ErrorType.ERROR_UNKNOWN_SOCKET_TYPE,
            ErrorType.ERROR_REDIRECT_LOOP,
            ErrorType.ERROR_OFFLINE,
            ErrorType.ERROR_NET_RESET,
            ErrorType.ERROR_UNSAFE_CONTENT_TYPE,
            ErrorType.ERROR_CORRUPTED_CONTENT,
            ErrorType.ERROR_CONTENT_CRASHED,
            ErrorType.ERROR_INVALID_CONTENT_ENCODING,
            ErrorType.ERROR_UNKNOWN_HOST,
            ErrorType.ERROR_MALFORMED_URI,
            ErrorType.ERROR_FILE_NOT_FOUND,
            ErrorType.ERROR_FILE_ACCESS_DENIED,
            ErrorType.ERROR_PROXY_CONNECTION_REFUSED,
            ErrorType.ERROR_UNKNOWN_PROXY_HOST,
            ErrorType.ERROR_UNKNOWN_PROTOCOL,
        ).forEach { error ->
            val actualPage = createActualErrorPage(error)
            val expectedPage = createExpectedErrorPage(
                error = error,
                html = LOW_AND_MEDIUM_RISK_ERROR_PAGES,
            )

            assertEquals(expectedPage, actualPage)
            // Check if the error metric was recorded
            assertEquals(
                error.name,
                ErrorPage.visitedError.testGetValue()!!.last().extra?.get("error_type"),
            )
        }
    }

    @Test
    fun `onErrorRequest results in correct error page for medium risk level error`() {
        setOf(
            ErrorType.ERROR_SECURITY_BAD_CERT,
            ErrorType.ERROR_SECURITY_SSL,
            ErrorType.ERROR_PORT_BLOCKED,
        ).forEach { error ->
            val actualPage = createActualErrorPage(error)
            val expectedPage = createExpectedErrorPage(
                error = error,
                html = LOW_AND_MEDIUM_RISK_ERROR_PAGES,
            )

            assertEquals(expectedPage, actualPage)
            // Check if the error metric was recorded
            assertEquals(
                error.name,
                ErrorPage.visitedError.testGetValue()!!.last().extra?.get("error_type"),
            )
        }
    }

    @Test
    fun `onErrorRequest results in correct error page for high risk level error`() {
        setOf(
            ErrorType.ERROR_SAFEBROWSING_HARMFUL_URI,
            ErrorType.ERROR_SAFEBROWSING_MALWARE_URI,
            ErrorType.ERROR_SAFEBROWSING_PHISHING_URI,
            ErrorType.ERROR_SAFEBROWSING_UNWANTED_URI,
        ).forEach { error ->
            val actualPage = createActualErrorPage(error)
            val expectedPage = createExpectedErrorPage(
                error = error,
                html = HIGH_RISK_ERROR_PAGES,
            )

            assertEquals(expectedPage, actualPage)
            // Check if the error metric was recorded
            assertEquals(
                error.name,
                ErrorPage.visitedError.testGetValue()!!.last().extra?.get("error_type"),
            )
        }
    }

    private fun createActualErrorPage(error: ErrorType): String {
        val errorPage = interceptor.onErrorRequest(session = mockk(), errorType = error, uri = null)
            as RequestInterceptor.ErrorResponse
        return errorPage.uri
    }

    private fun createExpectedErrorPage(error: ErrorType, html: String): String {
        return ErrorPages.createUrlEncodedErrorPage(
            context = testContext,
            errorType = error,
            htmlResource = html,
        )
    }
}
