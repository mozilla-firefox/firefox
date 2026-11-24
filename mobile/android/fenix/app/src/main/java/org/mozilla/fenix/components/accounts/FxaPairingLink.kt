/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.accounts

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.view.ContextThemeWrapper
import mozilla.components.support.ktx.kotlin.isSameOriginAs
import org.mozilla.fenix.R
import org.mozilla.fenix.settings.SupportUtils

private const val FXA_PAIRING_PATH = "/pair"
private const val FXA_EMBED_PARAM = "embed"
private const val FXA_EMBED_CUSTOM_TAB = "customtab"

/**
 * Returns whether [uri] has the same origin as [fxaServerUrl] and the Firefox Accounts pairing path. [fxaServerUrl] is
 * the origin of the configured account server, as returned by `FxaServer.contentUrl()`.
 */
internal fun isFxaPairingLink(uri: Uri, fxaServerUrl: String): Boolean =
    uri.toString().lowercase().isSameOriginAs(fxaServerUrl) && uri.path == FXA_PAIRING_PATH

/**
 * Creates an intent opening the Firefox Accounts [pairingUrl] in the auth custom tab with `embed=customtab` appended so
 * the page can adapt its wording.
 */
internal fun createPairingAuthCustomTabIntent(context: Context, pairingUrl: Uri): Intent {
    // Neither the receiver's transparent theme nor the application context defines colorSurface, which the custom
    // tab intent reads, so resolve it against the app theme explicitly.
    val themedContext = ContextThemeWrapper(context, R.style.NormalTheme)
    val pairingCustomTabUrl = pairingUrl.buildUpon().appendQueryParameter(FXA_EMBED_PARAM, FXA_EMBED_CUSTOM_TAB).build()
    return SupportUtils.createAuthCustomTabIntent(themedContext, pairingCustomTabUrl.toString())
}
