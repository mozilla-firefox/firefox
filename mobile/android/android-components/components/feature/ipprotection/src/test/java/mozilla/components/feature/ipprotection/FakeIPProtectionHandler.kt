/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.concept.engine.ipprotection.ServiceState

/**
 * In-memory [IPProtectionHandler] recording the requests it receives and replaying canned outcomes back through the
 * result callbacks.
 *
 * @property activateResult Passed to the `onResult` of [activate].
 * @property deactivateResult Passed to the `onResult` of [deactivate].
 * @property countryListResult Passed to the `onResult` of [updateCountryList].
 * @property enrollResult Passed to the `onResult` of [enroll].
 * @property serviceState Passed to the `onResult` of [getState].
 * @property holdResults Whether the results of [activate] and [deactivate] wait for [releaseHeldResults].
 */
internal class FakeIPProtectionHandler(
    var activateResult: Throwable? = null,
    var deactivateResult: Throwable? = null,
    var countryListResult: Throwable? = null,
    var enrollResult: IPProtectionHandler.EnrollResult = IPProtectionHandler.EnrollResult(true),
    var serviceState: ServiceState = ServiceState.Uninitialized,
    var holdResults: Boolean = false,
) : IPProtectionHandler {

    private val heldResults = mutableListOf<() -> Unit>()

    /** Every request made on the handler, in the order it was made. */
    val calls = mutableListOf<Call>()

    var authProvider: IPProtectionHandler.AuthProvider? = null
        private set

    var gpiProvider: IPProtectionHandler.GpiProvider? = null
        private set

    /** The thread [activate] or [deactivate] was last called on. */
    var lastProxyCallThread: Thread? = null
        private set

    override fun activate(countryCode: String?, onResult: (Throwable?) -> Unit) {
        calls += Call.Activate(countryCode)
        lastProxyCallThread = Thread.currentThread()
        val result = activateResult
        reply { onResult(result) }
    }

    override fun deactivate(onResult: (Throwable?) -> Unit) {
        calls += Call.Deactivate
        lastProxyCallThread = Thread.currentThread()
        val result = deactivateResult
        reply { onResult(result) }
    }

    /** Delivers the held results of [activate] and [deactivate], in the order the requests were made. */
    fun releaseHeldResults() {
        val results = heldResults.toList()
        heldResults.clear()
        results.forEach { it() }
    }

    private fun reply(result: () -> Unit) {
        if (holdResults) {
            heldResults += result
        } else {
            result()
        }
    }

    override fun enroll(onResult: (IPProtectionHandler.EnrollResult) -> Unit) {
        calls += Call.Enroll
        onResult(enrollResult)
    }

    override fun getState(onResult: (ServiceState) -> Unit) {
        calls += Call.GetState
        onResult(serviceState)
    }

    override fun updateCountryList(onResult: (Throwable?) -> Unit) {
        calls += Call.UpdateCountryList
        onResult(countryListResult)
    }

    override fun init() {
        calls += Call.Init
    }

    override fun uninit() {
        calls += Call.Uninit
    }

    override fun setAuthProvider(provider: IPProtectionHandler.AuthProvider?) {
        authProvider = provider
    }

    override fun setGpiProvider(provider: IPProtectionHandler.GpiProvider?) {
        gpiProvider = provider
    }

    override fun notifyAccountStatus(signedIn: Boolean) {
        calls += Call.NotifyAccountStatus(signedIn)
    }

    /** A request made on the handler. */
    sealed interface Call {
        data class Activate(val countryCode: String?) : Call

        data class NotifyAccountStatus(val signedIn: Boolean) : Call

        data object Deactivate : Call

        data object Enroll : Call

        data object GetState : Call

        data object UpdateCountryList : Call

        data object Init : Call

        data object Uninit : Call
    }
}
