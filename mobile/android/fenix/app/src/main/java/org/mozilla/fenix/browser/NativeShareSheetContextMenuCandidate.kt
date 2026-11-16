/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import android.content.Context
import android.view.View
import mozilla.components.browser.state.state.SessionState
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.feature.contextmenu.ContextMenuCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createAddContactCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createCopyEmailAddressCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createCopyImageCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createCopyImageLocationCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createCopyLinkCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createCopyLinkTextCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createDownloadLinkCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createOpenImageInNewTabCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createOpenInNewTabCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createOpenInPrivateTabCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createSaveImageCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createSaveVideoAudioCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createShareEmailAddressCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createShareImageCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createShareLinkCandidate
import mozilla.components.feature.contextmenu.ContextMenuUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.ui.widgets.DefaultSnackbarDelegate
import mozilla.components.ui.widgets.SnackbarDelegate
import org.mozilla.fenix.components.share.ShareSheetChooserAction
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.ShareUseCases

/** Provides the context menu candidates for [BrowserFragment] when native share sheet is enabled. */
object NativeShareSheetContextMenuCandidate {

    /**
     * Returns the default list of context menu candidates for [BrowserFragment] when the native share sheet is enabled.
     *
     * @param context [Context] used for various system interactions.
     * @param tabsUseCases [TabsUseCases] used for adding new tabs.
     * @param contextMenuUseCases [ContextMenuUseCases] used to integrate other features.
     * @param shareUseCases [ShareUseCases] used to share the long-pressed link.
     * @param getShareItems Callback providing the [ShareData] items to share for a given URL.
     * @param snackBarParentView The view in which to find a suitable parent for displaying the `Snackbar`.
     * @param snackbarDelegate [SnackbarDelegate] used to actually show a `Snackbar`.
     * @param downloadsLocation Callback providing the directory path where the file should be saved.
     * @param navigateToShareFragment Lambda provided by the caller that provides navigation to the `ShareFragment`.
     * @param additionalTabCandidates App-defined candidates rendered immediately after the built-in "open in tab"
     *   items, so that all tab-opening options stay grouped together in the menu.
     */
    @Suppress("LongParameterList")
    fun defaultCandidates(
        context: Context,
        tabsUseCases: TabsUseCases,
        contextMenuUseCases: ContextMenuUseCases,
        shareUseCases: ShareUseCases,
        getShareItems: (String) -> List<ShareData>,
        snackBarParentView: View,
        snackbarDelegate: SnackbarDelegate = DefaultSnackbarDelegate(),
        downloadsLocation: () -> String,
        navigateToShareFragment: (currentTab: SessionState, hitTabUrl: String) -> Unit,
        additionalTabCandidates: List<ContextMenuCandidate> = emptyList(),
    ): List<ContextMenuCandidate> = buildList {
        add(
            createOpenInNewTabCandidate(
                context,
                tabsUseCases,
                snackBarParentView,
                snackbarDelegate,
            )
        )
        add(
            createOpenInPrivateTabCandidate(
                context,
                tabsUseCases,
                snackBarParentView,
                snackbarDelegate,
            )
        )
        addAll(additionalTabCandidates)
        add(createCopyLinkCandidate(context, snackBarParentView, snackbarDelegate))
        add(createCopyLinkTextCandidate(context, snackBarParentView, snackbarDelegate))
        add(createDownloadLinkCandidate(context, contextMenuUseCases, downloadsLocation))
        add(
            createShareLinkCandidate(
                context = context,
                shareUseCases = shareUseCases,
                shareItems = getShareItems,
                navigateToShareFragment = navigateToShareFragment,
            )
        )
        add(createShareImageCandidate(context, contextMenuUseCases))
        add(
            createOpenImageInNewTabCandidate(
                context,
                tabsUseCases,
                snackBarParentView,
                snackbarDelegate,
            )
        )
        add(
            createCopyImageCandidate(
                context,
                contextMenuUseCases,
            )
        )
        add(createSaveImageCandidate(context, contextMenuUseCases, downloadsLocation))
        add(createSaveVideoAudioCandidate(context, contextMenuUseCases, downloadsLocation))
        add(createCopyImageLocationCandidate(context, snackBarParentView, snackbarDelegate))
        add(createAddContactCandidate(context))
        add(createShareEmailAddressCandidate(context))
        add(createCopyEmailAddressCandidate(context, snackBarParentView, snackbarDelegate))
    }

    private fun createShareLinkCandidate(
        context: Context,
        shareUseCases: ShareUseCases,
        shareItems: (hitTab: String) -> List<ShareData>,
        navigateToShareFragment: (currentTab: SessionState, hitTabUrl: String) -> Unit,
    ) =
        createShareLinkCandidate(
            context = context,
            action = { currentTab, hitTab ->
                val url = hitTab.getUrl()
                shareUseCases.shareItems(
                    items = shareItems(url),
                    source = ShareSource.CONTEXT_MENU_LINK,
                    subject = null,
                    chooserActions =
                        listOf(
                            ShareSheetChooserAction.SEND_TO_DEVICES,
                            ShareSheetChooserAction.QR_CODE,
                        ),
                    navigateToShareFragment = { navigateToShareFragment(currentTab, url) },
                )
            },
        )
}
