/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.store

import android.content.Context
import android.view.Gravity
import androidx.annotation.VisibleForTesting
import androidx.fragment.app.FragmentManager
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.feature.downloads.ui.DownloadCancelDialogFragment
import mozilla.components.lib.crash.CrashReporter
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.MiddlewareContext
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.store.BrowserScreenAction.CancelPrivateDownloadsOnPrivateTabsClosedAccepted
import org.mozilla.fenix.browser.store.BrowserScreenAction.ClosingLastPrivateTab
import org.mozilla.fenix.components.toolbar.BrowserToolbarMiddleware
import org.mozilla.fenix.theme.ThemeManager

/**
 * [Middleware] responsible for handling actions related to the browser screen.
 *
 * @param crashReporter [CrashReporter] for recording crashes.
 */
class BrowserScreenMiddleware(
    private val crashReporter: CrashReporter,
) : Middleware<BrowserScreenState, BrowserScreenAction>, ViewModel() {
    private lateinit var dependencies: LifecycleDependencies
    private lateinit var store: BrowserScreenStore

    /**
     * Updates the [LifecycleDependencies] of this middleware.
     *
     * @param dependencies The new [LifecycleDependencies].
     */
    fun updateLifecycleDependencies(dependencies: LifecycleDependencies) {
        this.dependencies = dependencies
    }

    override fun invoke(
        context: MiddlewareContext<BrowserScreenState, BrowserScreenAction>,
        next: (BrowserScreenAction) -> Unit,
        action: BrowserScreenAction,
    ) {
        when (action) {
            is ClosingLastPrivateTab -> {
                next(action)

                store = context.store as BrowserScreenStore

                showCancelledDownloadWarning(
                    downloadCount = action.inProgressPrivateDownloads,
                    tabId = action.tabId,
                )
            }

            else -> next(action)
        }
    }

    private fun showCancelledDownloadWarning(downloadCount: Int, tabId: String?) {
        crashReporter.recordCrashBreadcrumb(
            Breadcrumb("DownloadCancelDialogFragment shown in browser screen"),
        )
        val dialog = DownloadCancelDialogFragment.newInstance(
            downloadCount = downloadCount,
            tabId = tabId,
            source = null,
            promptStyling = DownloadCancelDialogFragment.PromptStyling(
                gravity = Gravity.BOTTOM,
                shouldWidthMatchParent = true,
                positiveButtonBackgroundColor = ThemeManager.resolveAttribute(
                    R.attr.accent,
                    dependencies.context,
                ),
                positiveButtonTextColor = ThemeManager.resolveAttribute(
                    R.attr.textOnColorPrimary,
                    dependencies.context,
                ),
                positiveButtonRadius = dependencies.context.resources.getDimensionPixelSize(
                    R.dimen.tab_corner_radius,
                ).toFloat(),
            ),

            onPositiveButtonClicked = { _, _ ->
                store.dispatch(CancelPrivateDownloadsOnPrivateTabsClosedAccepted)
            },
        )
        dialog.show(dependencies.fragmentManager, CANCEL_PRIVATE_DOWNLOADS_DIALOG_FRAGMENT_TAG)
    }

    /**
     * Lifecycle dependencies for the [BrowserToolbarMiddleware].
     *
     * @property context [Context] used for various system interactions.
     * @property fragmentManager [FragmentManager] to use for showing other fragments.
     */
    data class LifecycleDependencies(
        val context: Context,
        val fragmentManager: FragmentManager,
    )

    /**
     * Static functionalities of the [BrowserToolbarMiddleware].
     */
    companion object {
        @VisibleForTesting
        internal const val CANCEL_PRIVATE_DOWNLOADS_DIALOG_FRAGMENT_TAG = "CANCEL_PRIVATE_DOWNLOADS_DIALOG_FRAGMENT_TAG"

        /**
         * [ViewModelProvider.Factory] for creating a [BrowserScreenMiddleware].
         *
         * @param crashReporter [CrashReporter] for recording crashes.
         */
        fun viewModelFactory(
            crashReporter: CrashReporter,
        ): ViewModelProvider.Factory = object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T {
                if (modelClass.isAssignableFrom(BrowserScreenMiddleware::class.java)) {
                    return BrowserScreenMiddleware(
                        crashReporter = crashReporter,
                    ) as T
                }
                throw IllegalArgumentException("Unknown ViewModel class")
            }
        }
    }
}
