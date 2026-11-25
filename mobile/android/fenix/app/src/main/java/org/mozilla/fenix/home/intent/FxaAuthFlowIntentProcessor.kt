/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.intent

import android.content.Context
import android.content.Intent
import android.net.Uri
import androidx.navigation.NavController
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.concept.sync.SyncConfig
import mozilla.components.service.fxa.FxaService
import mozilla.components.service.fxa.manager.FxaAccountManager
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.components.accounts.FenixFxAEntryPoint
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.utils.Settings

private const val FXA_AUTH_DEEPLINK_HOST = "fxa_begin_auth"
private const val FXA_AUTH_DEEPLINK_PARAM_ENTRY_POINT = "entry_point"
private const val FXA_AUTH_DEEPLINK_PARAM_SERVICE = "service"

/**
 * Starts Firefox Accounts authentication for `fxa_begin_auth` deep links.
 *
 * Required query parameters:
 * 1. `entry_point` - must match an existing [FenixFxAEntryPoint].
 * 2. `service` - must match an existing [FxaService.identifier].
 *
 * An example of a deep link is
 * - `fenix://fxa_begin_auth?entry_point=vpn_integration_android_settings&service=vpn`.
 *
 * @param accountManager The account manager for processing the auth flow.
 * @param syncConfig The current sync config - used to determine whether the [SyncConfig.syncDecouplingEnabled] feature
 *   is on (in which case we don't want to send the sync scope with the auth)
 * @param coroutineScope The coroutine scope for async operations.
 * @param ioDispatcher The dispatcher for I/O operations.
 * @param mainDispatcher The dispatcher for UI-based operations like launching the activity.
 */
class FxaAuthFlowIntentProcessor(
    private val accountManager: FxaAccountManager,
    private val syncConfig: SyncConfig,
    private val coroutineScope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher,
    private val mainDispatcher: CoroutineDispatcher,
) : HomeIntentProcessor {
    override fun process(
        intent: Intent,
        navController: NavController,
        out: Intent,
        settings: Settings,
    ): Boolean {
        val isDeeplink = intent.scheme?.equals(BuildConfig.DEEP_LINK_SCHEME, ignoreCase = true) ?: return false
        return isDeeplink && intent.processMessageDeeplink(navController.context)
    }

    private fun Intent.processMessageDeeplink(context: Context?): Boolean {
        val uri = data ?: return false
        return when (uri.host) {
            FXA_AUTH_DEEPLINK_HOST -> {
                val params = uri.parseActionParams() ?: return false
                beginAuthentication(params, context)
                true
            }
            else -> false
        }
    }

    private fun beginAuthentication(params: AuthActionParams, context: Context?) {
        coroutineScope.launch {
            val url =
                withContext(ioDispatcher) {
                    accountManager.beginAuthentication(
                        entrypoint = params.entryPoint,
                        service = params.service.identifier,
                        authScopes =
                            buildSet {
                                addAll(params.service.requiredScopes.map { it.value })

                                if (!syncConfig.syncDecouplingEnabled) {
                                    addAll(FxaService.Sync.requiredScopes.map { it.value })
                                }
                            },
                    )
                } ?: return@launch

            withContext(mainDispatcher) {
                context?.let {
                    val intent = SupportUtils.createAuthCustomTabIntent(it, url)
                    it.startActivity(intent)
                }
            }
        }
    }

    private fun Uri.parseActionParams(): AuthActionParams? =
        try {
            var entryPoint: FenixFxAEntryPoint? = null
            var service: FxaService? = null
            for (parameter in queryParameterNames) {
                val value = getQueryParameter(parameter) ?: continue
                when (parameter) {
                    FXA_AUTH_DEEPLINK_PARAM_ENTRY_POINT -> entryPoint = value.toFenixFxaEntryPoint()
                    FXA_AUTH_DEEPLINK_PARAM_SERVICE -> service = value.toFxaService()
                    else -> continue
                }
            }

            if (entryPoint != null && service != null) {
                AuthActionParams(entryPoint = entryPoint, service = service)
            } else {
                null
            }
        } catch (_: UnsupportedOperationException) {
            null
        }

    private fun String.toFenixFxaEntryPoint(): FenixFxAEntryPoint? {
        return FenixFxAEntryPoint.entries.firstOrNull { it.entryName == this }
    }

    private fun String.toFxaService(): FxaService? {
        return FxaService.entries.firstOrNull { it.identifier == this }
    }

    private data class AuthActionParams(
        val entryPoint: FenixFxAEntryPoint,
        val service: FxaService,
    )
}
