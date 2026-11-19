/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.ipprotection

import androidx.annotation.OptIn
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionDelegate
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.concept.engine.ipprotection.ProxyState
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.geckoview.ExperimentalGeckoViewApi
import org.mozilla.geckoview.IPProtectionController as GeckoViewIPProtectionController

private val logger = Logger("IPP:GeckoDelegate")

@OptIn(ExperimentalGeckoViewApi::class)
@kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
internal class GeckoIPProtectionDelegate(private val delegate: IPProtectionDelegate) :
    GeckoViewIPProtectionController.Delegate {

    override fun onServiceStateChanged(state: Int) {
        delegate.onServiceStateChanged(state.toServiceState())
    }

    override fun onProxyStateChanged(state: GeckoViewIPProtectionController.ProxyState) {
        delegate.onProxyStateChanged(
            proxyState = state.state.toProxyState(),
            lastError = state.errorType,
        )
    }

    override fun onUsageChanged(info: GeckoViewIPProtectionController.UsageInfo) {
        delegate.onUsageChanged(
            remaining = info.remaining,
            max = info.max,
            resetTime = info.resetTime,
        )
    }

    override fun onCountryListChanged(countries: List<GeckoViewIPProtectionController.Country>) {
        delegate.onCountryListChanged(
            countries.map {
                IPProtectionHandler.Country(code = it.code, available = it.available)
            }
        )
    }
}

@OptIn(ExperimentalGeckoViewApi::class)
@kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
internal fun Int.toServiceState(): ServiceState =
    when (this) {
        GeckoViewIPProtectionController.SERVICE_STATE_UNINITIALIZED -> ServiceState.Uninitialized
        GeckoViewIPProtectionController.SERVICE_STATE_UNAVAILABLE -> ServiceState.Unavailable
        GeckoViewIPProtectionController.SERVICE_STATE_UNAUTHENTICATED -> ServiceState.Unauthenticated
        GeckoViewIPProtectionController.SERVICE_STATE_READY -> ServiceState.Ready
        else -> {
            ServiceState.Unavailable
        }
    }

@OptIn(ExperimentalGeckoViewApi::class)
@kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
internal fun Int.toProxyState(): ProxyState =
    when (this) {
        GeckoViewIPProtectionController.ProxyState.NOT_READY -> ProxyState.NOT_READY
        GeckoViewIPProtectionController.ProxyState.READY -> ProxyState.READY
        GeckoViewIPProtectionController.ProxyState.ACTIVATING -> ProxyState.ACTIVATING
        GeckoViewIPProtectionController.ProxyState.ACTIVE -> ProxyState.ACTIVE
        GeckoViewIPProtectionController.ProxyState.ERROR -> ProxyState.ERROR
        GeckoViewIPProtectionController.ProxyState.PAUSED -> ProxyState.PAUSED
        else -> {
            logger.warn("Unsupported proxy state from GeckoView: $this")
            ProxyState.UNKNOWN
        }
    }
