/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko.ipprotection

import androidx.annotation.OptIn
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.concept.engine.ipprotection.IPProtectionHandler
import mozilla.components.concept.engine.ipprotection.ServiceState
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.geckoview.ExperimentalGeckoViewApi
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.IPProtectionController

@OptIn(ExperimentalGeckoViewApi::class)
@kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
internal class GeckoIPProtectionHandler(private val runtime: GeckoRuntime) : IPProtectionHandler {

    private val logger = Logger("IPP:GeckoHandler")

    override fun activate(
        countryCode: String?,
        onResult: (Throwable?) -> Unit,
    ) {
        // `userAction` differentiates between user and system initiated actions, `false` is used by
        // the toolkit code internally; we are reporting `true`.
        // Both fields are used for toolkit telemetry, and since projects track telemetry independently,
        // defaulting `inPrivateBrowsing` to `false` for android is fine.
        val userAction = true
        val inPrivateBrowsing = false
        runtime.ipProtectionController
            .activate(userAction, inPrivateBrowsing, countryCode)
            .then(
                {
                    onResult(null)
                    GeckoResult.fromValue(null)
                },
                { ex ->
                    logger.error("activate() failed", ex)
                    onResult(ex)
                    GeckoResult.fromValue(null)
                },
            )
    }

    override fun deactivate(onResult: (Throwable?) -> Unit) {
        runtime.ipProtectionController
            .deactivate()
            .then(
                {
                    onResult(null)
                    GeckoResult.fromValue(null)
                },
                { ex ->
                    logger.error("deactivate() failed", ex)
                    onResult(ex)
                    GeckoResult.fromValue(null)
                },
            )
    }

    override fun enroll(onResult: (IPProtectionHandler.EnrollResult) -> Unit) {
        runtime.ipProtectionController
            .enroll()
            .then(
                { result ->
                    val logMessage =
                        "Enrollment request success. Status: ${result?.isEnrolledAndEntitled}, error: ${result?.error}"
                    logger.info(logMessage)
                    onResult(
                        IPProtectionHandler.EnrollResult(
                            isEnrolledAndEntitled = result?.isEnrolledAndEntitled == true,
                            error = result?.error,
                        )
                    )
                    GeckoResult.fromValue(null)
                },
                { ex ->
                    logger.info("Enrollment failed.", ex)
                    onResult(
                        IPProtectionHandler.EnrollResult(
                            isEnrolledAndEntitled = false,
                            error = ex.message,
                        )
                    )
                    GeckoResult.fromValue(null)
                },
            )
    }

    override fun init() {
        runtime.ipProtectionController.init()
    }

    override fun uninit() {
        runtime.ipProtectionController.uninit()
    }

    override fun getState(onResult: (ServiceState) -> Unit) {
        runtime.ipProtectionController.serviceState.then<Int>(
            { serviceState ->
                val service = serviceState ?: return@then GeckoResult()
                onResult(service.toServiceState())
                GeckoResult()
            },
            { throwable ->
                logger.error("GeckoIPProtectionHandler#getState failed.", throwable)
                GeckoResult()
            },
        )
    }

    override fun updateCountryList(onResult: (Throwable?) -> Unit) {
        runtime.ipProtectionController
            .getCountryList()
            .then(
                {
                    onResult(null)
                    GeckoResult.fromValue(null)
                },
                { ex ->
                    logger.error("updateCountryList() failed", ex)
                    onResult(ex)
                    GeckoResult.fromValue(null)
                },
            )
    }

    override fun setAuthProvider(provider: IPProtectionHandler.AuthProvider?) {
        logger.debug("setAuthProvider")
        runtime.ipProtectionController.setAuthProvider(
            object : IPProtectionController.AuthProvider {
                override fun onTokenRequest(): GeckoResult<String?> {
                    val result = GeckoResult<String?>()
                    provider?.getToken { token ->
                        logger.info("Retrieved access token.")
                        result.complete(token)
                    }
                    return result
                }
            }
        )
    }

    override fun notifyAccountStatus(signedIn: Boolean) {
        runtime.ipProtectionController.notifySignInStateChanged(signedIn)
    }

    override fun setGpiProvider(provider: IPProtectionHandler.GpiProvider?) {
        if (provider == null) {
            runtime.ipProtectionController.setGpiProvider(null)
            return
        }
        runtime.ipProtectionController.setGpiProvider(
            object : IPProtectionController.GpiProvider {
                override fun warmUp(): GeckoResult<Void> {
                    val result = GeckoResult<Void>()
                    provider.warmUp { success ->
                        if (success) {
                            result.complete(null)
                        } else {
                            result.completeExceptionally(RuntimeException("gpi-warm-up-failed"))
                        }
                    }
                    return result
                }

                override fun onTokenRequest(): GeckoResult<String> {
                    val result = GeckoResult<String>()
                    provider.getToken { token ->
                        if (token != null) {
                            result.complete(token)
                        } else {
                            result.completeExceptionally(RuntimeException("no_gpi_token"))
                        }
                    }
                    return result
                }
            }
        )
    }
}
