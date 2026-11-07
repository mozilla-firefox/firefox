/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.contextmenu

import android.content.ActivityNotFoundException
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.view.View
import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import com.google.android.material.snackbar.Snackbar
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.content.DownloadState
import mozilla.components.browser.state.state.content.ShareResourceState
import mozilla.components.concept.engine.HitResult
import mozilla.components.feature.app.links.AppLinksUseCases
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.MAX_TITLE_LENGTH
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.base.log.Log
import mozilla.components.support.ktx.android.content.addContact
import mozilla.components.support.ktx.android.content.createChooserExcludingCurrentApp
import mozilla.components.support.ktx.android.content.share
import mozilla.components.support.ktx.kotlin.stripMailToProtocol
import mozilla.components.support.ktx.kotlin.takeOrReplace
import mozilla.components.ui.widgets.DefaultSnackbarDelegate
import mozilla.components.ui.widgets.SnackbarDelegate

/**
 * A candidate for an item to be displayed in the context menu.
 *
 * @property id A unique ID that will be used to uniquely identify the candidate that the user selected.
 * @property label The label that will be displayed in the context menu
 * @property showFor If this lambda returns true for a given [SessionState] and [HitResult] then it will be displayed in
 *   the context menu.
 * @property action The action to be invoked once the user selects this item.
 */
data class ContextMenuCandidate(
    val id: String,
    val label: String,
    val showFor: (SessionState, HitResult) -> Boolean,
    val action: (SessionState, HitResult) -> Unit,
) {
    companion object {
        // This is used for limiting image title, in order to prevent crashes caused by base64 encoded image
        // https://github.com/mozilla-mobile/android-components/issues/8298
        const val MAX_TITLE_LENGTH = 2500

        /**
         * Returns the default list of context menu candidates.
         *
         * Use this list if you do not intend to customize the context menu items to be displayed.
         */
        fun defaultCandidates(
            context: Context,
            tabsUseCases: TabsUseCases,
            contextMenuUseCases: ContextMenuUseCases,
            snackBarParentView: View,
            snackbarDelegate: SnackbarDelegate = DefaultSnackbarDelegate(),
            downloadsLocation: () -> String,
        ): List<ContextMenuCandidate> =
            listOf(
                createOpenInNewTabCandidate(
                    context,
                    tabsUseCases,
                    snackBarParentView,
                    snackbarDelegate,
                ),
                createOpenInPrivateTabCandidate(
                    context,
                    tabsUseCases,
                    snackBarParentView,
                    snackbarDelegate,
                ),
                createCopyLinkCandidate(context, snackBarParentView, snackbarDelegate),
                createCopyLinkTextCandidate(context, snackBarParentView, snackbarDelegate),
                createDownloadLinkCandidate(context, contextMenuUseCases, downloadsLocation),
                createShareLinkCandidate(context),
                createShareImageCandidate(context, contextMenuUseCases),
                createOpenImageInNewTabCandidate(
                    context,
                    tabsUseCases,
                    snackBarParentView,
                    snackbarDelegate,
                ),
                createCopyImageCandidate(
                    context,
                    contextMenuUseCases,
                ),
                createSaveImageCandidate(context, contextMenuUseCases, downloadsLocation),
                createSaveVideoAudioCandidate(context, contextMenuUseCases, downloadsLocation),
                createCopyImageLocationCandidate(context, snackBarParentView, snackbarDelegate),
                createAddContactCandidate(context),
                createShareEmailAddressCandidate(context),
                createCopyEmailAddressCandidate(context, snackBarParentView, snackbarDelegate),
            )

        /**
         * Context Menu item: "Open Link in New Tab".
         *
         * @param context [Context] used for various system interactions.
         * @param tabsUseCases [TabsUseCases] used for adding new tabs.
         * @param snackBarParentView The view in which to find a suitable parent for displaying the `Snackbar`.
         * @param snackbarDelegate [SnackbarDelegate] used to actually show a `Snackbar`.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createOpenInNewTabCandidate(
            context: Context,
            tabsUseCases: TabsUseCases,
            snackBarParentView: View,
            snackbarDelegate: SnackbarDelegate = DefaultSnackbarDelegate(),
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.open_in_new_tab",
                label = context.getString(R.string.mozac_feature_contextmenu_open_link_in_new_tab),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isHttpLink() &&
                        !tab.content.private &&
                        additionalValidation(tab, hitResult)
                },
                action = { parent, hitResult ->
                    val tab =
                        tabsUseCases.addTab(
                            hitResult.getLink(),
                            selectTab = false,
                            startLoading = true,
                            textDirectiveUserActivation = true,
                            parentId = parent.id,
                            contextId = parent.contextId,
                        )

                    snackbarDelegate.show(
                        snackBarParentView = snackBarParentView,
                        text = R.string.mozac_feature_contextmenu_snackbar_new_tab_opened,
                        duration = Snackbar.LENGTH_LONG,
                        action = R.string.mozac_feature_contextmenu_snackbar_action_switch,
                    ) {
                        tabsUseCases.selectTab(tab)
                    }
                },
            )

        /**
         * Context Menu item: "Open Link in Private Tab".
         *
         * @param context [Context] used for various system interactions.
         * @param tabsUseCases [TabsUseCases] used for adding new tabs.
         * @param snackBarParentView The view in which to find a suitable parent for displaying the `Snackbar`.
         * @param snackbarDelegate [SnackbarDelegate] used to actually show a `Snackbar`.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createOpenInPrivateTabCandidate(
            context: Context,
            tabsUseCases: TabsUseCases,
            snackBarParentView: View,
            snackbarDelegate: SnackbarDelegate = DefaultSnackbarDelegate(),
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.open_in_private_tab",
                label = context.getString(R.string.mozac_feature_contextmenu_open_link_in_private_tab),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isHttpLink() &&
                        additionalValidation(tab, hitResult)
                },
                action = { parent, hitResult ->
                    val tab =
                        tabsUseCases.addTab(
                            hitResult.getLink(),
                            selectTab = false,
                            startLoading = true,
                            textDirectiveUserActivation = true,
                            parentId = parent.id,
                            private = true,
                        )

                    snackbarDelegate.show(
                        snackBarParentView = snackBarParentView,
                        text = R.string.mozac_feature_contextmenu_snackbar_new_private_tab_opened,
                        duration = Snackbar.LENGTH_LONG,
                        action = R.string.mozac_feature_contextmenu_snackbar_action_switch,
                    ) {
                        tabsUseCases.selectTab(tab)
                    }
                },
            )

        /**
         * Context Menu item: "Open Link in external App".
         *
         * @param context [Context] used for various system interactions.
         * @param appLinksUseCases [AppLinksUseCases] used to interact with urls that can be opened in 3rd party apps.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createOpenInExternalAppCandidate(
            context: Context,
            appLinksUseCases: AppLinksUseCases,
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.open_in_external_app",
                label = context.getString(R.string.mozac_feature_contextmenu_open_link_in_external_app),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.canOpenInExternalApp(appLinksUseCases) &&
                        additionalValidation(tab, hitResult)
                },
                action = { _, hitResult ->
                    val link = hitResult.getLink()
                    val redirect = appLinksUseCases.appLinkRedirectIncludeInstall(link)
                    val appIntent = redirect.appIntent
                    val marketPlaceIntent = redirect.marketplaceIntent
                    if (appIntent != null) {
                        appLinksUseCases.openAppLink(appIntent)
                    } else if (marketPlaceIntent != null) {
                        appLinksUseCases.openAppLink(marketPlaceIntent)
                    }
                },
            )

        /**
         * Context Menu item: "Add to contact".
         *
         * @param context [Context] used for various system interactions.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createAddContactCandidate(
            context: Context,
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.add_to_contact",
                label = context.getString(R.string.mozac_feature_contextmenu_add_to_contact),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isMailto() &&
                        additionalValidation(tab, hitResult)
                },
                action = { _, hitResult -> context.addContact(hitResult.getLink().stripMailToProtocol()) },
            )

        /**
         * Context Menu item: "Share email address".
         *
         * @param context [Context] used for various system interactions.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createShareEmailAddressCandidate(
            context: Context,
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.share_email",
                label = context.getString(R.string.mozac_feature_contextmenu_share_email_address),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isMailto() &&
                        additionalValidation(tab, hitResult)
                },
                action = { _, hitResult -> context.share(hitResult.getLink().stripMailToProtocol()) },
            )

        /**
         * Context Menu item: "Copy email address".
         *
         * @param context [Context] used for various system interactions.
         * @param snackBarParentView The view in which to find a suitable parent for displaying the `Snackbar`.
         * @param snackbarDelegate [SnackbarDelegate] used to actually show a `Snackbar`.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createCopyEmailAddressCandidate(
            context: Context,
            snackBarParentView: View,
            snackbarDelegate: SnackbarDelegate = DefaultSnackbarDelegate(),
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.copy_email_address",
                label = context.getString(R.string.mozac_feature_contextmenu_copy_email_address),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isMailto() &&
                        additionalValidation(tab, hitResult)
                },
                action = { _, hitResult ->
                    val email = hitResult.getLink().stripMailToProtocol()
                    clipPlainText(
                        context,
                        email,
                        email,
                        R.string.mozac_feature_contextmenu_snackbar_email_address_copied,
                        snackBarParentView,
                        snackbarDelegate,
                    )
                },
            )

        /**
         * Context Menu item: "Open Image in New Tab".
         *
         * @param context [Context] used for various system interactions.
         * @param tabsUseCases [TabsUseCases] used for adding new tabs.
         * @param snackBarParentView The view in which to find a suitable parent for displaying the `Snackbar`.
         * @param snackbarDelegate [SnackbarDelegate] used to actually show a `Snackbar`.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createOpenImageInNewTabCandidate(
            context: Context,
            tabsUseCases: TabsUseCases,
            snackBarParentView: View,
            snackbarDelegate: SnackbarDelegate = DefaultSnackbarDelegate(),
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.open_image_in_new_tab",
                label = context.getString(R.string.mozac_feature_contextmenu_open_image_in_new_tab),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isImage() &&
                        additionalValidation(tab, hitResult)
                },
                action = { parent, hitResult ->
                    val tab =
                        tabsUseCases.addTab(
                            hitResult.src,
                            selectTab = false,
                            startLoading = true,
                            parentId = parent.id,
                            contextId = parent.contextId,
                            private = parent.content.private,
                        )

                    snackbarDelegate.show(
                        snackBarParentView = snackBarParentView,
                        text = R.string.mozac_feature_contextmenu_snackbar_new_tab_opened,
                        duration = Snackbar.LENGTH_LONG,
                        action = R.string.mozac_feature_contextmenu_snackbar_action_switch,
                    ) {
                        tabsUseCases.selectTab(tab)
                    }
                },
            )

        /**
         * Context Menu item: "Save image".
         *
         * @param context [Context] used for various system interactions.
         * @param contextMenuUseCases [ContextMenuUseCases] used to integrate other features.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createSaveImageCandidate(
            context: Context,
            contextMenuUseCases: ContextMenuUseCases,
            downloadsLocation: () -> String,
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.save_image",
                label = context.getString(R.string.mozac_feature_contextmenu_save_image),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isImage() &&
                        additionalValidation(tab, hitResult)
                },
                action = { tab, hitResult ->
                    contextMenuUseCases.injectDownload(
                        tab.id,
                        DownloadState(
                            hitResult.src,
                            skipConfirmation = true,
                            private = tab.content.private,
                            directoryPath = downloadsLocation(),
                            referrerUrl = tab.content.url,
                        ),
                    )
                },
            )

        /**
         * Context Menu item: "Copy image".
         *
         * @param context [Context] used for various system interactions.
         * @param contextMenuUseCases [ContextMenuUseCases] used to integrate other features.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createCopyImageCandidate(
            context: Context,
            contextMenuUseCases: ContextMenuUseCases,
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.copy_image",
                label = context.getString(R.string.mozac_feature_contextmenu_copy_image),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isImage() &&
                        additionalValidation(tab, hitResult)
                },
                action = { tab, hitResult ->
                    contextMenuUseCases.injectCopyFromInternet(
                        tab.id,
                        ShareResourceState.InternetResource(
                            url = hitResult.src,
                            private = tab.content.private,
                            referrerUrl = tab.content.url,
                        ),
                    )
                },
            )

        /**
         * Context Menu item: "Save video".
         *
         * @param context [Context] used for various system interactions.
         * @param contextMenuUseCases [ContextMenuUseCases] used to integrate other features.
         * @param downloadsLocation Callback providing the directory path where the file should be saved.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createSaveVideoAudioCandidate(
            context: Context,
            contextMenuUseCases: ContextMenuUseCases,
            downloadsLocation: () -> String,
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.save_video",
                label = context.getString(R.string.mozac_feature_contextmenu_save_file_to_device),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isVideoAudio() &&
                        additionalValidation(tab, hitResult)
                },
                action = { tab, hitResult ->
                    contextMenuUseCases.injectDownload(
                        tab.id,
                        DownloadState(
                            hitResult.src,
                            skipConfirmation = true,
                            private = tab.content.private,
                            directoryPath = downloadsLocation(),
                            referrerUrl = tab.content.url,
                        ),
                    )
                },
            )

        /**
         * Context Menu item: "Save link".
         *
         * @param context [Context] used for various system interactions.
         * @param contextMenuUseCases [ContextMenuUseCases] used to integrate other features.
         * @param downloadsLocation Callback providing the directory path where the file should be saved.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createDownloadLinkCandidate(
            context: Context,
            contextMenuUseCases: ContextMenuUseCases,
            downloadsLocation: () -> String,
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.download_link",
                label = context.getString(R.string.mozac_feature_contextmenu_download_link),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isLinkForOtherThanWebpage() &&
                        additionalValidation(tab, hitResult)
                },
                action = { tab, hitResult ->
                    contextMenuUseCases.injectDownload(
                        tab.id,
                        DownloadState(
                            hitResult.getLink(),
                            skipConfirmation = true,
                            private = tab.content.private,
                            directoryPath = downloadsLocation(),
                            referrerUrl = tab.content.url,
                        ),
                    )
                },
            )

        /**
         * Context Menu item: "Share Link".
         *
         * @param context [Context] used for various system interactions.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createShareLinkCandidate(
            context: Context,
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.share_link",
                label = context.getString(R.string.mozac_feature_contextmenu_share_link),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        (hitResult.isUri() || hitResult.isImage() || hitResult.isVideoAudio()) &&
                        additionalValidation(tab, hitResult)
                },
                action = { _, hitResult ->
                    val intent =
                        Intent(Intent.ACTION_SEND).apply {
                            type = "text/plain"
                            flags = Intent.FLAG_ACTIVITY_NEW_TASK
                            putExtra(Intent.EXTRA_TEXT, hitResult.getUrl())
                        }

                    try {
                        context.startActivity(
                            intent.createChooserExcludingCurrentApp(
                                context,
                                context.getString(R.string.mozac_feature_contextmenu_share_link),
                            )
                        )
                    } catch (e: ActivityNotFoundException) {
                        Log.log(
                            Log.Priority.WARN,
                            message = "No activity to share to found",
                            throwable = e,
                            tag = "createShareLinkCandidate",
                        )
                    }
                },
            )

        /**
         * Context Menu item: "Share Link".
         *
         * @param context [Context] used for various system interactions.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         * @param action The action to be invoked once the user selects the item.
         */
        fun createShareLinkCandidate(
            context: Context,
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
            action: (SessionState, HitResult) -> Unit,
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.share_link",
                label = context.getString(R.string.mozac_feature_contextmenu_share_link),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        (hitResult.isUri() || hitResult.isImage() || hitResult.isVideoAudio()) &&
                        additionalValidation(tab, hitResult)
                },
                action = action,
            )

        /**
         * Context Menu item: "Share image"
         *
         * @param context [Context] used for various system interactions.
         * @param contextMenuUseCases [ContextMenuUseCases] used to integrate other features.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createShareImageCandidate(
            context: Context,
            contextMenuUseCases: ContextMenuUseCases,
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.share_image",
                label = context.getString(R.string.mozac_feature_contextmenu_share_image),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isImage() &&
                        additionalValidation(tab, hitResult)
                },
                action = { tab, hitResult ->
                    contextMenuUseCases.injectShareFromInternet(
                        tab.id,
                        ShareResourceState.InternetResource(
                            url = hitResult.src,
                            private = tab.content.private,
                            referrerUrl = tab.content.url,
                        ),
                    )
                },
            )

        /**
         * Context Menu item: "Copy Link".
         *
         * @param context [Context] used for various system interactions.
         * @param snackBarParentView The view in which to find a suitable parent for displaying the `Snackbar`.
         * @param snackbarDelegate [SnackbarDelegate] used to actually show a `Snackbar`.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createCopyLinkCandidate(
            context: Context,
            snackBarParentView: View,
            snackbarDelegate: SnackbarDelegate = DefaultSnackbarDelegate(),
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.copy_link",
                label = context.getString(R.string.mozac_feature_contextmenu_copy_link),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        (hitResult.isUri() || hitResult.isImage() || hitResult.isVideoAudio()) &&
                        additionalValidation(tab, hitResult)
                },
                action = { _, hitResult ->
                    clipPlainText(
                        context,
                        hitResult.getLink(),
                        hitResult.getUrl(),
                        R.string.mozac_feature_contextmenu_snackbar_link_copied,
                        snackBarParentView,
                        snackbarDelegate,
                    )
                },
            )

        /**
         * Context Menu item: "Copy link text".
         *
         * @param context [Context] used for various system interactions.
         * @param snackBarParentView The view in which to find a suitable parent for displaying the `Snackbar`.
         * @param snackbarDelegate [SnackbarDelegate] used to actually show a `Snackbar`.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createCopyLinkTextCandidate(
            context: Context,
            snackBarParentView: View,
            snackbarDelegate: SnackbarDelegate = DefaultSnackbarDelegate(),
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.copy_link_text",
                label = context.getString(R.string.mozac_feature_contextmenu_copy_link_text),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isUri() &&
                        hitResult.hasLinkText() &&
                        additionalValidation(tab, hitResult)
                },
                action = { _, hitResult ->
                    val innerText = (hitResult as? HitResult.UNKNOWN)?.linkText ?: return@ContextMenuCandidate
                    clipPlainText(
                        context,
                        label = hitResult.getLink(),
                        plainText = innerText,
                        displayTextId = R.string.mozac_feature_contextmenu_snackbar_link_text_copied,
                        snackBarParentView,
                        snackbarDelegate,
                    )
                },
            )

        /**
         * Context Menu item: "Copy Image Location".
         *
         * @param context [Context] used for various system interactions.
         * @param snackBarParentView The view in which to find a suitable parent for displaying the `Snackbar`.
         * @param snackbarDelegate [SnackbarDelegate] used to actually show a `Snackbar`.
         * @param additionalValidation Callback for the final validation in deciding whether this menu option will be
         *   shown. Will only be called if all the intrinsic validations passed.
         */
        fun createCopyImageLocationCandidate(
            context: Context,
            snackBarParentView: View,
            snackbarDelegate: SnackbarDelegate = DefaultSnackbarDelegate(),
            additionalValidation: (SessionState, HitResult) -> Boolean = { _, _ -> true },
        ) =
            ContextMenuCandidate(
                id = "mozac.feature.contextmenu.copy_image_location",
                label = context.getString(R.string.mozac_feature_contextmenu_copy_image_location),
                showFor = { tab, hitResult ->
                    tab.isUrlSchemeAllowed(hitResult.getLink()) &&
                        hitResult.isImage() &&
                        additionalValidation(tab, hitResult)
                },
                action = { _, hitResult ->
                    clipPlainText(
                        context,
                        hitResult.getLink(),
                        hitResult.src,
                        R.string.mozac_feature_contextmenu_snackbar_link_copied,
                        snackBarParentView,
                        snackbarDelegate,
                    )
                },
            )

        private fun clipPlainText(
            context: Context,
            label: String,
            plainText: String,
            displayTextId: Int,
            snackBarParentView: View,
            snackbarDelegate: SnackbarDelegate = DefaultSnackbarDelegate(),
        ) {
            val clipboardManager = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            val clip = ClipData.newPlainText(label, plainText)
            clipboardManager.setPrimaryClip(clip)

            snackbarDelegate.show(
                snackBarParentView = snackBarParentView,
                text = displayTextId,
                duration = Snackbar.LENGTH_SHORT,
            )
        }
    }
}

// Some helper methods to work with HitResult. We may want to improve the API of HitResult and remove some of the
// helpers eventually: https://github.com/mozilla-mobile/android-components/issues/1443

private fun HitResult.isImage(): Boolean = (this is HitResult.IMAGE || this is HitResult.IMAGE_SRC) && src.isNotEmpty()

private fun HitResult.isVideoAudio(): Boolean = (this is HitResult.VIDEO || this is HitResult.AUDIO) && src.isNotEmpty()

private fun HitResult.isUri(): Boolean =
    ((this is HitResult.UNKNOWN && src.isNotEmpty()) || this is HitResult.IMAGE_SRC)

private fun HitResult.hasLinkText(): Boolean =
    (!(this as? HitResult.UNKNOWN)?.linkText.isNullOrEmpty() && src.isNotEmpty())

private fun HitResult.isHttpLink(): Boolean = isUri() && getLink().startsWith("http")

private fun HitResult.isLinkForOtherThanWebpage(): Boolean {
    val link = getLink()
    val isHtml = link.endsWith("html") || link.endsWith("htm")
    return isHttpLink() && !isHtml
}

private fun HitResult.isIntent(): Boolean =
    (this is HitResult.UNKNOWN && src.isNotEmpty() && getLink().startsWith("intent:"))

private fun HitResult.isMailto(): Boolean =
    (this is HitResult.UNKNOWN && src.isNotEmpty()) && getLink().startsWith("mailto:")

private fun HitResult.canOpenInExternalApp(appLinksUseCases: AppLinksUseCases): Boolean {
    if (isHttpLink() || isIntent() || isVideoAudio()) {
        val redirect = appLinksUseCases.appLinkRedirectIncludeInstall(getLink())
        return redirect.hasExternalApp() || redirect.hasMarketplaceIntent()
    }
    return false
}

internal fun HitResult.getLink(): String =
    when (this) {
        is HitResult.UNKNOWN -> src
        is HitResult.IMAGE_SRC -> uri
        is HitResult.IMAGE ->
            if (title.isNullOrBlank()) {
                src.takeOrReplace(MAX_TITLE_LENGTH, "image")
            } else {
                title.toString()
            }
        is HitResult.VIDEO -> if (title.isNullOrBlank()) src else title.toString()
        is HitResult.AUDIO -> if (title.isNullOrBlank()) src else title.toString()
        else -> "about:blank"
    }

/** Returns a formatted url that includes the title, if available. */
internal fun HitResult.getDisplayTitle(): String =
    when {
        this is HitResult.IMAGE_SRC && !title.isNullOrBlank() -> "$uri\n\n$title"
        else -> getLink()
    }

@VisibleForTesting
internal fun SessionState.isUrlSchemeAllowed(url: String): Boolean {
    return when (val engineSession = engineState.engineSession) {
        null -> true
        else -> {
            val urlScheme = url.toUri().normalizeScheme().scheme
            !engineSession.getBlockedSchemes().contains(urlScheme)
        }
    }
}
