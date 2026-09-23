/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.samples.browser.integration

import android.content.Context
import android.os.Environment
import android.view.View
import androidx.fragment.app.FragmentManager
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.app.links.AppLinksUseCases
import mozilla.components.feature.contextmenu.ContextMenuCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createAddContactCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createCopyEmailAddressCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createCopyImageLocationCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createCopyLinkCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createOpenImageInNewTabCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createSaveImageCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createShareEmailAddressCandidate
import mozilla.components.feature.contextmenu.ContextMenuCandidate.Companion.createShareLinkCandidate
import mozilla.components.feature.contextmenu.ContextMenuFeature
import mozilla.components.feature.contextmenu.ContextMenuUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.ui.widgets.DefaultSnackbarDelegate
import org.mozilla.samples.browser.databinding.FragmentBrowserBinding

@Suppress("UndocumentedPublicClass")
class ContextMenuIntegration(
    context: Context,
    fragmentManager: FragmentManager,
    browserStore: BrowserStore,
    tabsUseCases: TabsUseCases,
    contextMenuUseCases: ContextMenuUseCases,
    parentView: View,
    sessionId: String? = null,
) : LifecycleAwareFeature {

    private val candidates = run {
        if (sessionId != null) {
            val snackbarDelegate = DefaultSnackbarDelegate()
            listOf(
                createCopyLinkCandidate(context, parentView, snackbarDelegate),
                createShareLinkCandidate(context),
                createOpenImageInNewTabCandidate(
                    context,
                    tabsUseCases,
                    parentView,
                    snackbarDelegate,
                ),
                createSaveImageCandidate(
                    context = context,
                    contextMenuUseCases = contextMenuUseCases,
                    downloadsLocation = {
                        Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS).path
                    },
                ),
                createCopyImageLocationCandidate(context, parentView, snackbarDelegate),
                createAddContactCandidate(context),
                createShareEmailAddressCandidate(context),
                createCopyEmailAddressCandidate(context, parentView, snackbarDelegate),
            )
        } else {
            val appLinksCandidate =
                ContextMenuCandidate.createOpenInExternalAppCandidate(
                    context = context,
                    appLinksUseCases =
                        AppLinksUseCases(
                            context = context,
                            launchInApp = { true },
                        ),
                )
            ContextMenuCandidate.defaultCandidates(
                context = context,
                tabsUseCases = tabsUseCases,
                contextMenuUseCases = contextMenuUseCases,
                snackBarParentView = parentView,
                downloadsLocation = {
                    Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS).path
                },
            ) + appLinksCandidate
        }
    }

    private val feature =
        ContextMenuFeature(
            fragmentManager,
            browserStore,
            candidates,
            FragmentBrowserBinding.bind(parentView).engineView,
            contextMenuUseCases,
        )

    override fun start() {
        feature.start()
    }

    override fun stop() {
        feature.stop()
    }
}
