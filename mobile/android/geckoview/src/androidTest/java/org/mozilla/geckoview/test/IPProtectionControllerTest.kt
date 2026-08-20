/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.geckoview.test

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.MediumTest
import org.hamcrest.MatcherAssert.assertThat
import org.hamcrest.Matchers.equalTo
import org.hamcrest.Matchers.nullValue
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertThrows
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.gecko.EventDispatcher
import org.mozilla.gecko.util.BundleEventListener
import org.mozilla.gecko.util.GeckoBundle
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.IPProtectionController

@RunWith(AndroidJUnit4::class)
@MediumTest
class IPProtectionControllerTest : BaseSessionTest() {

    private val ipProtectionController
        get() = sessionRule.runtime.ipProtectionController

    @Before
    fun setup() {
        sessionRule.setPrefsUntilTestEnd(
            mapOf(
                "browser.ipProtection.enabled" to true,
                "browser.ipProtection.cacheDisabled" to true,
                "browser.ipProtection.guardian.endpoint" to "https://vpn.mozilla.com",
                "browser.ipProtection.log" to true,
                // Use a pref-backed server list so activation does not depend on
                // Remote Settings. Selected once when the serverlist module first
                // loads, so it must be set before any test triggers init().
                "browser.ipProtection.override.serverlist" to SERVER_LIST_JSON,
            )
        )
    }

    @After
    fun teardown() {
        ipProtectionController.setDelegate(null)
        ipProtectionController.setAuthProvider(null)
        sessionRule.waitForResult(ipProtectionController.uninit())
    }

    @Test
    fun serviceStateInitializedAfterConstruction() {
        var serviceState = sessionRule.waitForResult(ipProtectionController.getServiceState())
        assertThat(
            "service state is uninitialized after construction",
            serviceState,
            equalTo(IPProtectionController.SERVICE_STATE_UNINITIALIZED),
        )
        sessionRule.waitForResult(ipProtectionController.init())
        serviceState = sessionRule.waitForResult(ipProtectionController.getServiceState())
        assertThat(
            "After Init we should be signed-out",
            serviceState,
            equalTo(IPProtectionController.SERVICE_STATE_UNAUTHENTICATED),
        )
    }

    private fun dispatchServiceState(bundle: GeckoBundle): Int {
        val result = GeckoResult<Int>()
        ipProtectionController.setDelegate(
            object : IPProtectionController.Delegate {
                override fun onServiceStateChanged(state: Int) {
                    result.complete(state)
                }
            }
        )
        EventDispatcher.getInstance().dispatch("GeckoView:IPProtection:IPProtectionService:StateChanged", bundle)
        return sessionRule.waitForResult(result)
    }

    @Test
    fun delegateCalledOnServiceStateChange() {
        val delegate = object : IPProtectionController.Delegate {}
        ipProtectionController.setDelegate(delegate)
        assertThat(
            "getDelegate returns the delegate that was set",
            ipProtectionController.delegate,
            equalTo(delegate),
        )
    }

    @Test
    fun serviceStateCodesAreParsedCorrectly() {
        val states =
            listOf(
                "unauthenticated" to IPProtectionController.SERVICE_STATE_UNAUTHENTICATED,
                "unavailable" to IPProtectionController.SERVICE_STATE_UNAVAILABLE,
                "optedout" to IPProtectionController.SERVICE_STATE_OPTED_OUT,
                "ready" to IPProtectionController.SERVICE_STATE_READY,
            )
        for ((stateString, expectedCode) in states) {
            val bundle = GeckoBundle()
            bundle.putString("state", stateString)
            assertThat(dispatchServiceState(bundle), equalTo(expectedCode))
        }
    }

    private fun dispatchProxyState(bundle: GeckoBundle): IPProtectionController.ProxyState {
        val result = GeckoResult<IPProtectionController.ProxyState>()
        ipProtectionController.setDelegate(
            object : IPProtectionController.Delegate {
                override fun onProxyStateChanged(state: IPProtectionController.ProxyState) {
                    result.complete(state)
                }
            }
        )
        EventDispatcher.getInstance().dispatch("GeckoView:IPProtection:IPPProxyManager:StateChanged", bundle)
        return sessionRule.waitForResult(result)
    }

    @Test
    fun proxyStateCodesAreParsedCorrectly() {
        val states =
            listOf(
                "not-ready" to IPProtectionController.ProxyState.NOT_READY,
                "ready" to IPProtectionController.ProxyState.READY,
                "activating" to IPProtectionController.ProxyState.ACTIVATING,
                "active" to IPProtectionController.ProxyState.ACTIVE,
                "error" to IPProtectionController.ProxyState.ERROR,
                "paused" to IPProtectionController.ProxyState.PAUSED,
            )
        for ((stateString, expectedCode) in states) {
            val bundle = GeckoBundle()
            bundle.putString("state", stateString)
            assertThat(dispatchProxyState(bundle).state, equalTo(expectedCode))
        }
    }

    @Test
    fun proxyStateErrorTypeIsNullWhenNotError() {
        val bundle = GeckoBundle()
        bundle.putString("state", "ready")
        assertThat(dispatchProxyState(bundle).errorType, nullValue())
    }

    @Test
    fun proxyStateErrorTypeIsPresentWhenError() {
        val bundle = GeckoBundle()
        bundle.putString("state", "error")
        bundle.putString("errorType", "dns_error")
        assertThat(dispatchProxyState(bundle).errorType, equalTo("dns_error"))
    }

    private fun dispatchUsageInfo(bundle: GeckoBundle): IPProtectionController.UsageInfo {
        val result = GeckoResult<IPProtectionController.UsageInfo>()
        ipProtectionController.setDelegate(
            object : IPProtectionController.Delegate {
                override fun onUsageChanged(info: IPProtectionController.UsageInfo) {
                    result.complete(info)
                }
            }
        )
        EventDispatcher.getInstance().dispatch("GeckoView:IPProtection:IPPProxyManager:UsageChanged", bundle)
        return sessionRule.waitForResult(result)
    }

    @Test
    fun usageInfoIsParsedCorrectly() {
        val bundle = GeckoBundle()
        bundle.putLong("remaining", 1000L)
        bundle.putLong("max", 10000L)
        bundle.putString("resetTime", "2024-01-01T00:00:00Z")
        val info = dispatchUsageInfo(bundle)
        assertThat(info.remaining, equalTo(1000L))
        assertThat(info.max, equalTo(10000L))
        assertThat(info.resetTime, equalTo("2024-01-01T00:00:00Z"))
    }

    @Test
    fun activateRejectsWithIPProxyExceptionWhenNotReady() {
        val proxyState = sessionRule.waitForResult(ipProtectionController.getProxyState())
        assertThat(
            "proxy is not ready thus activate will throw when called",
            proxyState.state,
            equalTo(IPProtectionController.ProxyState.NOT_READY),
        )
        val thrown =
            assertThrows(IPProtectionController.IPProxyException::class.java) {
                sessionRule.waitForResult(ipProtectionController.activate())
            }
        assertThat(thrown.code, equalTo(IPProtectionController.IPProxyException.ERROR_NOT_READY))
    }

    @Test
    fun ipProxyExceptionKnownErrorStringsMapToSpecificCodes() {
        val cases =
            listOf(
                "network-error" to IPProtectionController.IPProxyException.ERROR_NETWORK,
                "timeout-error" to IPProtectionController.IPProxyException.ERROR_TIMEOUT,
                "pass-unavailable" to IPProtectionController.IPProxyException.ERROR_PASS_UNAVAILABLE,
                "server-not-found" to IPProtectionController.IPProxyException.ERROR_SERVER_NOT_FOUND,
                "activation-canceled" to IPProtectionController.IPProxyException.ERROR_ACTIVATION_CANCELED,
                "catastrophic-error" to IPProtectionController.IPProxyException.ERROR_CATASTROPHIC,
                "vpn-unavailable" to IPProtectionController.IPProxyException.ERROR_VPN_UNAVAILABLE,
            )
        for ((errorString, expectedCode) in cases) {
            assertThat(
                IPProtectionController.IPProxyException.fromErrorString(errorString).code,
                equalTo(expectedCode),
            )
        }
    }

    @Test
    fun ipProxyExceptionUnknownErrorStringsMapsToErrorUnknown() {
        val unknownStrings = listOf("some-unknown-error", null)
        for (errorString in unknownStrings) {
            assertThat(
                IPProtectionController.IPProxyException.fromErrorString(errorString).code,
                equalTo(IPProtectionController.IPProxyException.ERROR_UNKNOWN),
            )
        }
    }

    @Test
    fun usageInfoResetTimeIsNullWhenAbsent() {
        val bundle = GeckoBundle()
        bundle.putLong("remaining", 0L)
        bundle.putLong("max", 5000L)
        val info = dispatchUsageInfo(bundle)
        assertThat(info.remaining, equalTo(0L))
        assertThat(info.max, equalTo(5000L))
        assertThat(info.resetTime, nullValue())
    }

    private class StubAuthProvider(private val token: GeckoResult<String> = GeckoResult.fromValue("stub-token")) :
        IPProtectionController.AuthProvider {
        override fun onTokenRequest(): GeckoResult<String> = token
    }

    @Test
    fun setAuthProviderRoundTrips() {
        val provider = StubAuthProvider()
        ipProtectionController.setAuthProvider(provider)
        assertThat(ipProtectionController.authProvider, equalTo<IPProtectionController.AuthProvider>(provider))
        ipProtectionController.setAuthProvider(null)
        assertThat(ipProtectionController.authProvider, nullValue())
    }

    @Test
    fun getTokenEventIsRoutedToAuthProvider() {
        ipProtectionController.setAuthProvider(StubAuthProvider(token = GeckoResult.fromValue("secret-token")))
        val response =
            sessionRule.waitForResult(EventDispatcher.getInstance().queryBundle("GeckoView:IPProtection:GetToken"))
        assertThat(response.getString("token"), equalTo("secret-token"))
    }

    @Test
    fun getTokenEventWithoutProviderReturnsError() {
        ipProtectionController.setAuthProvider(null)
        assertGetTokenError("no_auth_provider")
    }

    @Test
    fun getTokenEventWithRejectedTokenReturnsError() {
        ipProtectionController.setAuthProvider(
            StubAuthProvider(token = GeckoResult.fromException(RuntimeException("no-token")))
        )
        assertGetTokenError("no-token")
    }

    @Test
    fun getTokenEventWithNullTokenStringReturnsError() {
        ipProtectionController.setAuthProvider(StubAuthProvider(token = GeckoResult.fromValue(null)))
        assertGetTokenError("login_needed")
    }

    @Test
    fun getTokenEventWithEmptyTokenStringReturnsError() {
        ipProtectionController.setAuthProvider(StubAuthProvider(token = GeckoResult.fromValue("")))
        assertGetTokenError("login_needed")
    }

    private fun assertGetTokenError(expected: String) {
        val thrown =
            assertThrows(RuntimeException::class.java) {
                sessionRule.waitForResult(EventDispatcher.getInstance().queryBundle("GeckoView:IPProtection:GetToken"))
            }
        val cause = thrown.cause as EventDispatcher.QueryException
        assertThat(cause.data.toString(), equalTo(expected))
    }

    @Test
    fun notifySignInStateChangedDispatchesAuthStateChanged() {
        ipProtectionController.setAuthProvider(StubAuthProvider())
        val result = GeckoResult<Boolean>()
        val listener = BundleEventListener { _, message, callback ->
            result.complete(message.getBoolean("isSignedIn", false))
            callback?.sendSuccess(null)
        }
        EventDispatcher.getInstance().registerUiThreadListener(listener, "GeckoView:IPProtection:AuthStateChanged")
        try {
            sessionRule.waitForResult(ipProtectionController.notifySignInStateChanged(true))
            assertThat(sessionRule.waitForResult(result), equalTo(true))
        } finally {
            EventDispatcher.getInstance()
                .unregisterUiThreadListener(listener, "GeckoView:IPProtection:AuthStateChanged")
        }
    }

    @Test
    fun notifySignInStateChangedRejectsWithoutAuthProvider() {
        ipProtectionController.setAuthProvider(null)
        assertThrows(IllegalStateException::class.java) {
            sessionRule.waitForResult(ipProtectionController.notifySignInStateChanged(true))
        }
    }

    @Test
    fun getCountryListDeliversConfiguredCountriesToDelegate() {
        // Countries come from the pref-backed server list (SERVER_LIST_JSON):
        // US has a usable server, DE only a quarantined one, and REC is the
        // reserved recommended entry. getCountryList pushes the current list to
        // the delegate via onCountryListChanged rather than returning it.
        sessionRule.waitForResult(ipProtectionController.init())

        val received = GeckoResult<List<IPProtectionController.Country>>()
        ipProtectionController.setDelegate(
            object : IPProtectionController.Delegate {
                override fun onCountryListChanged(countries: List<IPProtectionController.Country>) {
                    received.complete(countries)
                }
            }
        )

        sessionRule.waitForResult(ipProtectionController.getCountryList())

        val countries = sessionRule.waitForResult(received)
        assertThat(
            "the recommended entry is not reported as a country",
            countries.none { it.code == "REC" },
            equalTo(true),
        )
        assertThat(countries.size, equalTo(2))
        assertThat(countries[0].code, equalTo("US"))
        assertThat(countries[0].available, equalTo(true))
        assertThat(countries[1].code, equalTo("DE"))
        assertThat(countries[1].available, equalTo(false))
    }

    @Test
    fun delegateIsNotifiedWhenServerlistChanges() {
        sessionRule.waitForResult(ipProtectionController.init())

        // First push from getCountryList reflects the initial SERVER_LIST_JSON.
        val initial = GeckoResult<List<IPProtectionController.Country>>()
        val updated = GeckoResult<List<IPProtectionController.Country>>()
        var sawInitial = false
        ipProtectionController.setDelegate(
            object : IPProtectionController.Delegate {
                override fun onCountryListChanged(countries: List<IPProtectionController.Country>) {
                    if (!sawInitial) {
                        sawInitial = true
                        initial.complete(countries)
                    } else {
                        updated.complete(countries)
                    }
                }
            }
        )

        sessionRule.waitForResult(ipProtectionController.getCountryList())
        val initialCountries = sessionRule.waitForResult(initial)
        assertThat(initialCountries.map { it.code }, equalTo(listOf("US", "DE")))

        // Changing the serverlist pref makes the pref-backed list re-fetch and
        // dispatch a change event, which is forwarded to the delegate.
        sessionRule.setPrefsUntilTestEnd(mapOf("browser.ipProtection.override.serverlist" to UPDATED_SERVER_LIST_JSON))

        val updatedCountries = sessionRule.waitForResult(updated)
        assertThat(updatedCountries.map { it.code }, equalTo(listOf("FR")))
        assertThat(updatedCountries[0].available, equalTo(true))
    }

    @Test
    fun activateReachesActiveWithTestAuthProvider() {
        sessionRule.setPrefsUntilTestEnd(mapOf("toolkit.ipProtection.android.authProvider" to "test"))
        // Seed the faked Guardian backend, starting signed out.
        sessionRule.setupIPPAuthProvider(JSONObject().put("signedIn", false))

        sessionRule.waitForResult(ipProtectionController.init())
        assertThat(
            "signed out after init",
            sessionRule.waitForResult(ipProtectionController.getServiceState()),
            equalTo(IPProtectionController.SERVICE_STATE_UNAUTHENTICATED),
        )

        sessionRule.simulateIPPSignIn(true)
        assertThat(
            "ready once signed in",
            sessionRule.waitForResult(ipProtectionController.getServiceState()),
            equalTo(IPProtectionController.SERVICE_STATE_READY),
        )

        sessionRule.waitForResult(ipProtectionController.activate())
        assertThat(
            "proxy is active after activate",
            sessionRule.waitForResult(ipProtectionController.getProxyState()).state,
            equalTo(IPProtectionController.ProxyState.ACTIVE),
        )
    }

    @Test
    fun activateRejectsWithPassUnavailableWhenProxyPassFails() {
        sessionRule.setPrefsUntilTestEnd(mapOf("toolkit.ipProtection.android.authProvider" to "test"))
        sessionRule.setupIPPAuthProvider(JSONObject().put("signedIn", false))

        sessionRule.waitForResult(ipProtectionController.init())
        sessionRule.simulateIPPSignIn(true)
        assertThat(
            "ready once signed in",
            sessionRule.waitForResult(ipProtectionController.getServiceState()),
            equalTo(IPProtectionController.SERVICE_STATE_READY),
        )

        sessionRule.setIPPProxyPassError("pass-unavailable")

        val thrown =
            assertThrows(IPProtectionController.IPProxyException::class.java) {
                sessionRule.waitForResult(ipProtectionController.activate())
            }
        assertThat(
            thrown.code,
            equalTo(IPProtectionController.IPProxyException.ERROR_PASS_UNAVAILABLE),
        )
    }

    @Test
    fun refreshUsageInvokesDelegate() {
        sessionRule.setPrefsUntilTestEnd(mapOf("toolkit.ipProtection.android.authProvider" to "test"))
        sessionRule.setupIPPAuthProvider(JSONObject().put("signedIn", false))
        sessionRule.waitForResult(ipProtectionController.init())
        sessionRule.simulateIPPSignIn(true)

        val received = GeckoResult<IPProtectionController.UsageInfo>()
        ipProtectionController.setDelegate(
            object : IPProtectionController.Delegate {
                override fun onUsageChanged(info: IPProtectionController.UsageInfo) {
                    received.complete(info)
                }
            }
        )

        sessionRule.waitForResult(ipProtectionController.refreshUsage())

        val info = sessionRule.waitForResult(received)
        // Seeded by setupIPPAuthProvider: ProxyUsage(max, remaining) = (5368709120, 4294967296).
        assertThat(info.max, equalTo(5368709120L))
        assertThat(info.remaining, equalTo(4294967296L))
    }

    @Test
    fun refreshUsageReportsZeroForUnlimitedBandwidth() {
        sessionRule.setPrefsUntilTestEnd(mapOf("toolkit.ipProtection.android.authProvider" to "test"))
        sessionRule.setupIPPAuthProvider(JSONObject().put("signedIn", false))
        sessionRule.waitForResult(ipProtectionController.init())
        sessionRule.simulateIPPSignIn(true)

        // It shouldn't matter what guardian sends down for the usage values,
        // if the unlimited bit is sent, it should be ignored and set to 0.
        sessionRule.setIPPProxyUsage(
            JSONObject().put("max", "999999999999").put("remaining", "888888888888").put("unlimited", true)
        )

        val received = GeckoResult<IPProtectionController.UsageInfo>()
        ipProtectionController.setDelegate(
            object : IPProtectionController.Delegate {
                override fun onUsageChanged(info: IPProtectionController.UsageInfo) {
                    received.complete(info)
                }
            }
        )

        sessionRule.waitForResult(ipProtectionController.refreshUsage())
        val info = sessionRule.waitForResult(received)
        assertThat("max bytes is 0 for unlimited", info.max, equalTo(0L))
        assertThat("used bytes is 0 for unlimited", info.remaining, equalTo(0L))
    }

    @Test
    fun activateRoutesThroughSelectedServer() {
        sessionRule.setPrefsUntilTestEnd(mapOf("toolkit.ipProtection.android.authProvider" to "test"))
        sessionRule.setupIPPAuthProvider(JSONObject().put("signedIn", false))
        sessionRule.waitForResult(ipProtectionController.init())
        sessionRule.simulateIPPSignIn(true)

        // No country: the recommended (REC) anycast location is selected.
        sessionRule.waitForResult(ipProtectionController.activate())
        assertThat(
            "recommended location routes through the REC server",
            sessionRule.getIPPProxyInfo()?.getString("host"),
            equalTo("rec.example.com"),
        )

        sessionRule.waitForResult(ipProtectionController.deactivate())

        // Explicit country: the matching country's server is selected.
        sessionRule.waitForResult(ipProtectionController.activate(true, false, "US"))
        assertThat(
            "the selected country routes through its server",
            sessionRule.getIPPProxyInfo()?.getString("host"),
            equalTo("us.example.com"),
        )
    }

    @Test
    fun activateRejectsWithServerNotFoundForUnknownCountry() {
        sessionRule.setPrefsUntilTestEnd(mapOf("toolkit.ipProtection.android.authProvider" to "test"))
        sessionRule.setupIPPAuthProvider(JSONObject().put("signedIn", false))
        sessionRule.waitForResult(ipProtectionController.init())
        sessionRule.simulateIPPSignIn(true)

        // "ZZ" is not present in the serverlist, so no server can be selected.
        val thrown =
            assertThrows(IPProtectionController.IPProxyException::class.java) {
                sessionRule.waitForResult(ipProtectionController.activate(true, false, "ZZ"))
            }
        assertThat(
            thrown.code,
            equalTo(IPProtectionController.IPProxyException.ERROR_SERVER_NOT_FOUND),
        )
    }

    @Test
    fun activateWhileActiveSwitchesSelectedServer() {
        sessionRule.setPrefsUntilTestEnd(mapOf("toolkit.ipProtection.android.authProvider" to "test"))
        sessionRule.setupIPPAuthProvider(JSONObject().put("signedIn", false))
        sessionRule.waitForResult(ipProtectionController.init())
        sessionRule.simulateIPPSignIn(true)

        // Activate with no country: the recommended (REC) anycast server.
        sessionRule.waitForResult(ipProtectionController.activate())
        assertThat(
            "starts on the recommended server",
            sessionRule.getIPPProxyInfo()?.getString("host"),
            equalTo("rec.example.com"),
        )

        // Activate again with an explicit country to switch the connection.
        sessionRule.waitForResult(ipProtectionController.activate(true, false, "US"))
        assertThat(
            "switched to the US server",
            sessionRule.getIPPProxyInfo()?.getString("host"),
            equalTo("us.example.com"),
        )

        // Activate again with no country to switch back to the recommended server.
        sessionRule.waitForResult(ipProtectionController.activate(true, false, null))
        assertThat(
            "back on the recommended server",
            sessionRule.getIPPProxyInfo()?.getString("host"),
            equalTo("rec.example.com"),
        )
    }

    companion object {
        private const val SERVER_LIST_JSON =
            """[{"name":"United States","code":"US","cities":[{"name":"Test City",""" +
                """"code":"TC","servers":[{"hostname":"us.example.com","port":443,""" +
                """"quarantined":false}]}]},""" +
                """{"name":"Germany","code":"DE","cities":[{"name":"Berlin",""" +
                """"code":"BE","servers":[{"hostname":"de.example.com","port":443,""" +
                """"quarantined":true}]}]},""" +
                """{"name":"Recommended","code":"REC","cities":[{"name":"Anycast",""" +
                """"code":"REC","servers":[{"hostname":"rec.example.com","port":443,""" +
                """"quarantined":false}]}]}]"""

        private const val UPDATED_SERVER_LIST_JSON =
            """[{"name":"France","code":"FR","cities":[{"name":"Paris",""" +
                """"code":"PA","servers":[{"hostname":"fr1.example.com","port":443,""" +
                """"quarantined":false}]}]}]"""
    }

    private class StubGpiProvider(
        private val warmUpResult: GeckoResult<Void> = GeckoResult.fromValue(null),
        private val tokenResult: GeckoResult<String> = GeckoResult.fromValue("stub-gpi-token"),
    ) : IPProtectionController.GpiProvider {
        override fun warmUp(): GeckoResult<Void> = warmUpResult

        override fun onTokenRequest(): GeckoResult<String> = tokenResult
    }

    @Test
    fun setGpiProviderRoundTrips() {
        val provider = StubGpiProvider()
        ipProtectionController.setGpiProvider(provider)
        assertThat(ipProtectionController.gpiProvider, equalTo<IPProtectionController.GpiProvider>(provider))
        ipProtectionController.setGpiProvider(null)
        assertThat(ipProtectionController.gpiProvider, nullValue())
    }

    @Test
    fun gpiWarmUpEventIsRoutedToGpiProvider() {
        val warmUpCompleted = GeckoResult<Boolean>()
        val listener = BundleEventListener { _, _, callback ->
            warmUpCompleted.complete(true)
            callback?.sendSuccess(null)
        }
        EventDispatcher.getInstance().registerUiThreadListener(listener, "GeckoView:IPProtection:GPI:WarmUpCompleted")
        try {
            ipProtectionController.setGpiProvider(StubGpiProvider())
            EventDispatcher.getInstance().dispatch("GeckoView:IPProtection:GPI:WarmUp", null)
            assertThat(sessionRule.waitForResult(warmUpCompleted), equalTo(true))
        } finally {
            EventDispatcher.getInstance()
                .unregisterUiThreadListener(listener, "GeckoView:IPProtection:GPI:WarmUpCompleted")
        }
    }

    @Test
    fun gpiWarmUpFailureDispatchesWarmUpFailed() {
        val warmUpFailed = GeckoResult<Boolean>()
        val listener = BundleEventListener { _, _, callback ->
            warmUpFailed.complete(true)
            callback?.sendSuccess(null)
        }
        EventDispatcher.getInstance().registerUiThreadListener(listener, "GeckoView:IPProtection:GPI:WarmUpFailed")
        try {
            ipProtectionController.setGpiProvider(
                StubGpiProvider(warmUpResult = GeckoResult.fromException(RuntimeException("warm-up failed")))
            )
            EventDispatcher.getInstance().dispatch("GeckoView:IPProtection:GPI:WarmUp", null)
            assertThat(sessionRule.waitForResult(warmUpFailed), equalTo(true))
        } finally {
            EventDispatcher.getInstance()
                .unregisterUiThreadListener(listener, "GeckoView:IPProtection:GPI:WarmUpFailed")
        }
    }

    @Test
    fun gpiWarmUpWithNoProviderIsSilentlyDropped() {
        ipProtectionController.setGpiProvider(null)
        // Should not crash when no provider is set.
        EventDispatcher.getInstance().dispatch("GeckoView:IPProtection:GPI:WarmUp", null)
    }

    @Test
    fun requestTokenEventIsRoutedToGpiProvider() {
        ipProtectionController.setGpiProvider(StubGpiProvider(tokenResult = GeckoResult.fromValue("secret-gpi-token")))
        val response =
            sessionRule.waitForResult(
                EventDispatcher.getInstance().queryBundle("GeckoView:IPProtection:GPI:RequestToken")
            )
        assertThat(response.getString("token"), equalTo("secret-gpi-token"))
    }

    @Test
    fun requestTokenEventWithoutProviderReturnsError() {
        ipProtectionController.setGpiProvider(null)
        assertRequestTokenError("no_gpi_provider")
    }

    @Test
    fun requestTokenEventWithRejectedTokenReturnsError() {
        ipProtectionController.setGpiProvider(
            StubGpiProvider(tokenResult = GeckoResult.fromException(RuntimeException("no-gpi-token")))
        )
        assertRequestTokenError("no-gpi-token")
    }

    @Test
    fun requestTokenEventWithNullTokenStringReturnsError() {
        ipProtectionController.setGpiProvider(StubGpiProvider(tokenResult = GeckoResult.fromValue(null)))
        assertRequestTokenError("no_gpi_token")
    }

    @Test
    fun requestTokenEventWithEmptyTokenStringReturnsError() {
        ipProtectionController.setGpiProvider(StubGpiProvider(tokenResult = GeckoResult.fromValue("")))
        assertRequestTokenError("no_gpi_token")
    }

    private fun assertRequestTokenError(expected: String) {
        val thrown =
            assertThrows(RuntimeException::class.java) {
                sessionRule.waitForResult(
                    EventDispatcher.getInstance().queryBundle("GeckoView:IPProtection:GPI:RequestToken")
                )
            }
        val cause = thrown.cause as EventDispatcher.QueryException
        assertThat(cause.data.toString(), equalTo(expected))
    }
}
