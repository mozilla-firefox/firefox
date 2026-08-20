/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import android.view.View
import androidx.navigation.findNavController
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.ui.AddonsManagerAdapterDelegate
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.core.Action
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.openToBrowser
import org.mozilla.fenix.settings.SupportUtils

/**
 * Shows the Fenix Snackbar in the given view along with the provided text.
 *
 * @param view A [View] used to determine a parent for the [Snackbar].
 * @param text The text to display in the [Snackbar].
 * @param duration The duration to show the [Snackbar] for.
 * @param action Optional action button to display alongside the text.
 */
internal fun showSnackBar(
    view: View,
    text: String,
    duration: SnackbarState.Duration = SnackbarState.Duration.Preset.Short,
    action: Action? = null,
) {
    Snackbar.make(
            snackBarParentView = view,
            snackbarState =
                SnackbarState(
                    message = text,
                    duration = duration,
                    action = action,
                ),
        )
        .show()
}

/**
 * Shows the Snackbar for a failed add-on operation, with a retry action.
 *
 * @param view A [View] used to determine a parent for the [Snackbar].
 * @param text The text to display in the [Snackbar].
 * @param onRetry Called when the user taps the retry action.
 */
internal fun showRetryableSnackBar(
    view: View,
    text: String,
    onRetry: () -> Unit,
) {
    showSnackBar(
        view = view,
        text = text,
        duration = SnackbarState.Duration.Preset.Long,
        action =
            Action(
                label = view.context.getString(R.string.addon_failure_retry_action),
                onClick = onRetry,
            ),
    )
}

internal fun View.openLearnMoreLink(
    link: AddonsManagerAdapterDelegate.LearnMoreLinks,
    addon: Addon,
) {
    val url = resolveLearnMoreUrl(link, addon) ?: return
    findNavController().openToBrowser()
    context.components.useCases.fenixBrowserUseCases.loadUrlOrSearch(
        searchTermOrURL = url,
        newTab = true,
    )
}

private fun resolveLearnMoreUrl(
    link: AddonsManagerAdapterDelegate.LearnMoreLinks,
    addon: Addon,
): String? {
    return when (link) {
        AddonsManagerAdapterDelegate.LearnMoreLinks.BLOCKLISTED_ADDON ->
            "${BuildConfig.AMO_BASE_URL}/android/blocked-addon/${addon.id}/${addon.version}/"
        AddonsManagerAdapterDelegate.LearnMoreLinks.ADDON_NOT_CORRECTLY_SIGNED ->
            SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.UNSIGNED_ADDONS)
    }
}
