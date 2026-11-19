/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.ipprotection

import androidx.annotation.OptIn
import androidx.test.ext.junit.runners.AndroidJUnit4
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionDelegate
import mozilla.components.concept.engine.ipprotection.ProxyState
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.support.test.mock
import mozilla.components.test.ReflectionUtils
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.geckoview.ExperimentalGeckoViewApi
import org.mozilla.geckoview.IPProtectionController as GeckoViewIPProtectionController

@OptIn(ExperimentalGeckoViewApi::class)
@kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
@RunWith(AndroidJUnit4::class)
class GeckoIPProtectionDelegateTest {

    @Test
    fun `GIVEN a proxy state code from GeckoView THEN it maps to the same proxy state AND an unrecognised code maps to unknown`() {
        val cases =
            listOf(
                GeckoViewIPProtectionController.ProxyState.NOT_READY to ProxyState.NOT_READY,
                GeckoViewIPProtectionController.ProxyState.READY to ProxyState.READY,
                GeckoViewIPProtectionController.ProxyState.ACTIVATING to ProxyState.ACTIVATING,
                GeckoViewIPProtectionController.ProxyState.ACTIVE to ProxyState.ACTIVE,
                GeckoViewIPProtectionController.ProxyState.ERROR to ProxyState.ERROR,
                GeckoViewIPProtectionController.ProxyState.PAUSED to ProxyState.PAUSED,
                99 to ProxyState.UNKNOWN,
            )

        cases.forEach { (code, expected) ->
            assertEquals("Code $code should map to $expected", expected, code.toProxyState())
        }
    }

    @Test
    fun `GIVEN usage was forwarded WHEN the service and the proxy report THEN each report is forwarded once AND the usage is not sent again`() {
        val delegate = RecordingIPProtectionDelegate()
        val subject = GeckoIPProtectionDelegate(delegate)

        subject.onUsageChanged(createUsageInfo(remaining = 1000L, max = 5000L, resetTime = "2026-06-01T00:00:00Z"))
        subject.onServiceStateChanged(GeckoViewIPProtectionController.SERVICE_STATE_READY)
        subject.onProxyStateChanged(
            createProxyState(state = GeckoViewIPProtectionController.ProxyState.ERROR, errorType = "boom")
        )

        assertEquals(
            listOf(
                Call.UsageChanged(remaining = 1000L, max = 5000L, resetTime = "2026-06-01T00:00:00Z"),
                Call.ServiceStateChanged(ServiceState.Ready),
                Call.ProxyStateChanged(ProxyState.ERROR, lastError = "boom"),
            ),
            delegate.calls,
        )
    }

    private fun createProxyState(state: Int, errorType: String?): GeckoViewIPProtectionController.ProxyState {
        val proxyState: GeckoViewIPProtectionController.ProxyState = mock()
        ReflectionUtils.setField(proxyState, "state", state)
        ReflectionUtils.setField(proxyState, "errorType", errorType)
        return proxyState
    }

    private fun createUsageInfo(
        remaining: Long,
        max: Long,
        resetTime: String?,
    ): GeckoViewIPProtectionController.UsageInfo {
        val usageInfo: GeckoViewIPProtectionController.UsageInfo = mock()
        ReflectionUtils.setField(usageInfo, "remaining", remaining)
        ReflectionUtils.setField(usageInfo, "max", max)
        ReflectionUtils.setField(usageInfo, "resetTime", resetTime)
        return usageInfo
    }

    private sealed class Call {
        data class ServiceStateChanged(val serviceState: ServiceState) : Call()

        data class ProxyStateChanged(val proxyState: ProxyState, val lastError: String?) : Call()

        data class UsageChanged(val remaining: Long, val max: Long, val resetTime: String?) : Call()
    }

    private class RecordingIPProtectionDelegate : IPProtectionDelegate {
        val calls = mutableListOf<Call>()

        override fun onServiceStateChanged(serviceState: ServiceState) {
            calls.add(Call.ServiceStateChanged(serviceState))
        }

        override fun onProxyStateChanged(proxyState: ProxyState, lastError: String?) {
            calls.add(Call.ProxyStateChanged(proxyState, lastError))
        }

        override fun onUsageChanged(remaining: Long, max: Long, resetTime: String?) {
            calls.add(Call.UsageChanged(remaining, max, resetTime))
        }
    }
}
