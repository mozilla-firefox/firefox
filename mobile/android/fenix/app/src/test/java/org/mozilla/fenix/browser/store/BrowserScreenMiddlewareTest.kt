/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.store

import android.view.Gravity
import androidx.fragment.app.FragmentManager
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.Runs
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.mockkObject
import io.mockk.slot
import io.mockk.verify
import mozilla.components.feature.downloads.ui.DownloadCancelDialogFragment
import mozilla.components.lib.crash.CrashReporter
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.rule.MainCoroutineRule
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.store.BrowserScreenAction.CancelPrivateDownloadsOnPrivateTabsClosedAccepted
import org.mozilla.fenix.browser.store.BrowserScreenMiddleware.Companion.CANCEL_PRIVATE_DOWNLOADS_DIALOG_FRAGMENT_TAG
import org.mozilla.fenix.browser.store.BrowserScreenMiddleware.LifecycleDependencies
import org.mozilla.fenix.theme.ThemeManager

@RunWith(AndroidJUnit4::class)
class BrowserScreenMiddlewareTest {
    @get:Rule
    val coroutinesTestRule = MainCoroutineRule()

    @Test
    fun `WHEN the last private tab is closing THEN record a breadcrumb and show a warning dialog`() {
        val crashReporter: CrashReporter = mockk(relaxed = true)
        val fragmentManager: FragmentManager = mockk(relaxed = true)
        val middleware = BrowserScreenMiddleware(crashReporter).apply {
            updateLifecycleDependencies(LifecycleDependencies(testContext, fragmentManager))
        }
        val store = BrowserScreenStore(
            middleware = listOf(middleware),
        )
        val warningDialog: DownloadCancelDialogFragment = mockk {
            every { show(any<FragmentManager>(), any<String>()) } just Runs
        }

        mockkObject(DownloadCancelDialogFragment.Companion) {
            every { DownloadCancelDialogFragment.Companion.newInstance(any(), any(), any(), any(), any(), any(), any()) } returns warningDialog

            store.dispatch(BrowserScreenAction.ClosingLastPrivateTab("tabId", 3))

            verify { crashReporter.recordCrashBreadcrumb(any()) }
            verify {
                DownloadCancelDialogFragment.Companion.newInstance(
                    downloadCount = 3,
                    tabId = "tabId",
                    source = null,
                    promptStyling = DownloadCancelDialogFragment.PromptStyling(
                        gravity = Gravity.BOTTOM,
                        shouldWidthMatchParent = true,
                        positiveButtonBackgroundColor = ThemeManager.resolveAttribute(
                            R.attr.accent,
                            testContext,
                        ),
                        positiveButtonTextColor = ThemeManager.resolveAttribute(
                            R.attr.textOnColorPrimary,
                            testContext,
                        ),
                        positiveButtonRadius = testContext.resources.getDimensionPixelSize(
                            R.dimen.tab_corner_radius,
                        ).toFloat(),
                    ),
                    onPositiveButtonClicked = any(),
                    onNegativeButtonClicked = null,
                )
            }
            verify { warningDialog.show(fragmentManager, CANCEL_PRIVATE_DOWNLOADS_DIALOG_FRAGMENT_TAG) }
        }
    }

    @Test
    fun `GIVEN a warning dialog for closing private tabs is shown WHEN the warning is accepted THEN inform about this`() {
        val middleware = BrowserScreenMiddleware(mockk(relaxed = true)).apply {
            updateLifecycleDependencies(LifecycleDependencies(testContext, mockk(relaxed = true)))
        }
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserScreenState, BrowserScreenAction>()
        val store = BrowserScreenStore(
            middleware = listOf(middleware, captureActionsMiddleware),
        )
        val warningDialog: DownloadCancelDialogFragment = mockk {
            every { show(any<FragmentManager>(), any<String>()) } just Runs
        }
        val positiveActionCaptor = slot<((tabId: String?, source: String?) -> Unit)>()

        mockkObject(DownloadCancelDialogFragment.Companion) {
            every { DownloadCancelDialogFragment.Companion.newInstance(any()) } returns warningDialog

            store.dispatch(BrowserScreenAction.ClosingLastPrivateTab("tabId", 3))
            verify {
                DownloadCancelDialogFragment.Companion.newInstance(
                    downloadCount = any(),
                    tabId = any(),
                    source = any(),
                    promptStyling = any(),
                    onPositiveButtonClicked = capture(positiveActionCaptor),
                    onNegativeButtonClicked = any(),
                )
            }
        }

        positiveActionCaptor.captured.invoke("test", "source")

        captureActionsMiddleware.assertLastAction(CancelPrivateDownloadsOnPrivateTabsClosedAccepted::class) {}
    }
}
