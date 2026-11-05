/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.trustpanel

import androidx.core.net.toUri
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.mapNotNull
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.lib.state.helpers.AbstractBinding
import mozilla.components.support.ktx.kotlinx.coroutines.flow.ifAnyChanged
import org.mozilla.fenix.settings.trustpanel.store.TrustPanelAction

/**
 * Observes the session the trust panel was opened for and reports changes to its security info. Once the session
 * navigates to a different host, [onHostChange] is invoked instead, since the rest of the trust panel still describes
 * the previous host.
 *
 * @param browserStore The [BrowserStore] to observe.
 * @param sessionId The ID of the session to observe.
 * @param urlHost The host of the URL the trust panel was opened for, or null if the URL has no host.
 * @param onSecurityInfoUpdate Invoked when the security info of the session changes.
 * @param onHostChange Invoked when the session navigates to a different host.
 * @param mainDispatcher The [CoroutineDispatcher] for state observation.
 */
class SecurityInfoBinding(
    browserStore: BrowserStore,
    private val sessionId: String,
    private val urlHost: String?,
    private val onSecurityInfoUpdate: (TrustPanelAction.UpdateSecurityInfo) -> Unit,
    private val onHostChange: () -> Unit,
    mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : AbstractBinding<BrowserState>(browserStore, mainDispatcher) {

    override suspend fun onState(flow: Flow<BrowserState>) {
        flow
            .mapNotNull { state -> state.findTabOrCustomTab(sessionId)?.content }
            .ifAnyChanged { content -> arrayOf(content.url, content.securityInfo) }
            .collect { content ->
                if (content.url.toUri().host != urlHost) {
                    onHostChange()
                    return@collect
                }

                onSecurityInfoUpdate(
                    TrustPanelAction.UpdateSecurityInfo(
                        isSecured = content.securityInfo.isSecure,
                        certificate = content.securityInfo.certificate,
                    )
                )
            }
    }
}
