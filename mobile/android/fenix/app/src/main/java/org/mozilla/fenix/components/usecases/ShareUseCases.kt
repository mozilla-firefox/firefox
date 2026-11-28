/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.usecases

import android.net.Uri
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.prompt.ShareData
import org.mozilla.fenix.GleanMetrics.NativeShareSheet
import org.mozilla.fenix.components.share.ShareSheetChooserAction
import org.mozilla.fenix.components.share.ShareSheetLauncher
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.share.isSystemShareSheetSupported
import org.mozilla.fenix.components.share.tryDispatchPdfShareAction
import org.mozilla.fenix.share.ShareFragment
import org.mozilla.fenix.utils.Settings

/**
 * Use cases for sharing content via the system share sheet or the in-app [ShareFragment].
 *
 * @param browserStore [BrowserStore] used to dispatch PDF share action.
 * @param shareSheetLauncher [ShareSheetLauncher] used to show the system share sheet.
 * @param settings [Settings] used for accessing user preferences.
 */
class ShareUseCases(
    private val browserStore: BrowserStore,
    private val shareSheetLauncher: ShareSheetLauncher,
    private val settings: Settings,
) {
    /**
     * Shares a single URL.
     *
     * @param id The session id of the tab to share from.
     * @param url The url to share.
     * @param title The title of the page to share.
     * @param source The surface from which the share was initiated, used for telemetry.
     * @param text Optional text to share alongside the url, e.g. supplied by the share deep link.
     * @param subject Optional subject for the share. When `null` or empty, defaults to the title.
     * @param isPrivate Whether the tab is in private browsing mode.
     * @param isCustomTab Whether the share is being initiated from a custom tab.
     * @param navigateToShareFragment Lambda provided by the caller that provides navigation to the [ShareFragment].
     *   Invoked as a fallback when the system share sheet nor the PDF share action applies.
     */
    @Suppress("LongParameterList")
    fun shareUrl(
        id: String?,
        url: String?,
        title: String?,
        source: ShareSource,
        text: String = "",
        subject: String? = null,
        isPrivate: Boolean = false,
        isCustomTab: Boolean = false,
        navigateToShareFragment: () -> Unit,
    ) {
        // Determines if this is a PDF share case and will dispatch appropriately, if it is.
        if (browserStore.tryDispatchPdfShareAction(id, url)) {
            return
        }

        when {
            settings.nativeShareSheetEnabled && isSystemShareSheetSupported && url != null -> {
                NativeShareSheet.shown.record(NativeShareSheet.ShownExtra(source = source.value))
                shareSheetLauncher.showSystemShareSheet(
                    id = id,
                    url = url,
                    title = title,
                    text = text,
                    subject = subject,
                    isPrivate = isPrivate,
                    isCustomTab = isCustomTab,
                )
            }

            else -> {
                navigateToShareFragment()
            }
        }
    }

    /**
     * Shares multiple [ShareData] items.
     *
     * @param items The list of [ShareData] items to share.
     * @param source The surface from which the share was initiated, used for telemetry.
     * @param isPrivate Whether the items belong to private browsing mode.
     * @param subject Optional subject for the share. When `null`, the underlying launcher defaults to the first item's
     *   title.
     * @param text Optional text to share alongside the urls in the system share sheet.
     * @param chooserActions An array of chooser actions that will be added to the share intent chooser in the native
     *   share sheet.
     * @param thumbnailUri Optional thumbnail shown in the system share sheet preview.
     * @param navigateToShareFragment Lambda provided by the caller that provides navigation to the [ShareFragment].
     *   Invoked as a fallback when the system share sheet nor the PDF share action applies.
     */
    fun shareItems(
        items: List<ShareData>,
        source: ShareSource,
        isPrivate: Boolean = false,
        subject: String? = null,
        text: String? = null,
        chooserActions: List<ShareSheetChooserAction> = listOf(),
        thumbnailUri: Uri? = null,
        navigateToShareFragment: () -> Unit,
    ) {
        if (settings.nativeShareSheetEnabled && isSystemShareSheetSupported) {
            NativeShareSheet.shown.record(NativeShareSheet.ShownExtra(source = source.value))
            shareSheetLauncher.showSystemShareSheet(
                items = items,
                isPrivate = isPrivate,
                subject = subject,
                text = text,
                chooserActions = chooserActions,
                thumbnailUri = thumbnailUri,
            )
        } else {
            navigateToShareFragment()
        }
    }
}
