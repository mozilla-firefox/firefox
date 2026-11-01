/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.extension

import android.widget.TextView
import androidx.core.view.isVisible
import androidx.fragment.app.FragmentManager
import androidx.navigation.NavController
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.runs
import io.mockk.slot
import io.mockk.spyk
import io.mockk.verify
import kotlin.test.assertNotNull
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.WebExtensionAction.UpdatePendingExtensionUpdateAction
import mozilla.components.browser.state.action.WebExtensionAction.UpdatePromptRequestWebExtensionAction
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.browser.state.state.extension.WebExtensionPromptRequest
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.concept.engine.webextension.WebExtensionInstallException
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.addons.R as addonsR
import mozilla.components.feature.addons.ui.PermissionsDialogFragment
import mozilla.components.feature.addons.ui.PermissionsDialogMode
import mozilla.components.support.ktx.android.content.appVersionName
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.R
import org.mozilla.fenix.addons.AddonsManagementFragmentDirections
import org.mozilla.fenix.addons.DownloadAddonDialogFragment
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.utils.LinkTextView
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class WebExtensionPromptFeatureTest {

    private lateinit var webExtensionPromptFeature: WebExtensionPromptFeature
    private lateinit var store: BrowserStore

    private val onLinkClickedCalls = mutableListOf<Pair<String, Boolean>>()
    private val onLinkClicked: (String, Boolean) -> Unit = { url, isFirstParty ->
        onLinkClickedCalls.add(url to isFirstParty)
    }
    private val navController: NavController = mockk(relaxed = true)
    private val updateAnswers = mutableListOf<Boolean>()
    private val pendingUpdate = newPendingUpdate()
    private val extension = Addon(id = "extension", translatableName = mapOf(Addon.DEFAULT_LOCALE to "Extension"))
    private val fragmentManager: FragmentManager = mockk(relaxed = true)
    private val addonManager: AddonManager = mockk(relaxed = true)

    private val testDispatcher = StandardTestDispatcher()

    @Before
    fun setup() {
        store = BrowserStore()
        webExtensionPromptFeature =
            spyk(
                WebExtensionPromptFeature(
                    store = store,
                    context = testContext,
                    fragmentManager = fragmentManager,
                    onLinkClicked = onLinkClicked,
                    navController = navController,
                    addonManager = addonManager,
                    mainDispatcher = testDispatcher,
                )
            )
    }

    @Test
    fun `WHEN InstallationFailed is dispatched THEN handleInstallationFailedRequest is called`() =
        runTest(testDispatcher) {
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            every { webExtensionPromptFeature.handleInstallationFailedRequest(any()) } returns null

            store.dispatch(
                UpdatePromptRequestWebExtensionAction(
                    WebExtensionPromptRequest.BeforeInstallation.InstallationFailed(
                        mockk(),
                        mockk(),
                    )
                )
            )
            testDispatcher.scheduler.advanceUntilIdle()

            verify { webExtensionPromptFeature.handleInstallationFailedRequest(any()) }
        }

    @Test
    fun `WHEN calling handleInstallationFailedRequest with network error THEN showDialog with the correct message`() =
        runTest(testDispatcher) {
            val expectedTitle = testContext.getString(addonsR.string.mozac_feature_addons_cant_install_extension)
            val exception =
                WebExtensionInstallException.NetworkFailure(
                    extensionName = "name",
                    throwable = Exception(),
                )
            val expectedMessage =
                testContext.getString(
                    addonsR.string.mozac_feature_addons_extension_failed_to_install_network_error,
                    "name",
                )

            val dialog = webExtensionPromptFeature.handleInstallationFailedRequest(exception = exception)

            verify { webExtensionPromptFeature.showDialog(expectedTitle, expectedMessage) }
            val linkView = dialog?.findViewById<LinkTextView>(R.id.link)
            assertFalse(linkView!!.isVisible)
        }

    @Test
    fun `WHEN calling handleInstallationFailedRequest with Blocklisted error THEN showDialog with the correct message`() =
        runTest(testDispatcher) {
            val expectedTitle = testContext.getString(addonsR.string.mozac_feature_addons_cant_install_extension)
            val extensionId = "extensionId"
            val extensionName = "extensionName"
            val extensionVersion = "extensionVersion"
            val exception =
                WebExtensionInstallException.Blocklisted(
                    extensionId = extensionId,
                    extensionName = extensionName,
                    extensionVersion = extensionVersion,
                    throwable = Exception(),
                )
            val appName = testContext.getString(R.string.app_name)
            val expectedMessage =
                testContext.getString(addonsR.string.mozac_feature_addons_blocklisted_2, extensionName, appName)
            val expectedUrl = "${BuildConfig.AMO_BASE_URL}/android/blocked-addon/$extensionId/$extensionVersion/"

            val dialog = webExtensionPromptFeature.handleInstallationFailedRequest(exception = exception)

            verify { webExtensionPromptFeature.showDialog(expectedTitle, expectedMessage, expectedUrl) }
            val linkView = dialog?.findViewById<LinkTextView>(R.id.link)
            assertTrue(linkView!!.isVisible)

            // Click the link, then verify.
            linkView.performClick()
            assertEquals(listOf(expectedUrl to true), onLinkClickedCalls)
        }

    @Test
    fun `WHEN calling handleInstallationFailedRequest with UserCancelled error THEN do not showDialog`() =
        runTest(testDispatcher) {
            val expectedTitle = ""
            val extensionName = "extensionName"
            val exception =
                WebExtensionInstallException.UserCancelled(
                    extensionName = extensionName,
                    throwable = Exception(),
                )
            val expectedMessage =
                testContext.getString(addonsR.string.mozac_feature_addons_failed_to_install, extensionName)

            webExtensionPromptFeature.handleInstallationFailedRequest(exception = exception)

            verify(exactly = 0) { webExtensionPromptFeature.showDialog(expectedTitle, expectedMessage) }
        }

    @Test
    fun `WHEN calling handleInstallationFailedRequest with Unknown error THEN showDialog with the correct message`() =
        runTest(testDispatcher) {
            val expectedTitle = ""
            val extensionName = "extensionName"
            val exception =
                WebExtensionInstallException.Unknown(
                    extensionName = extensionName,
                    throwable = Exception(),
                )
            val expectedMessage =
                testContext.getString(addonsR.string.mozac_feature_addons_failed_to_install, extensionName)

            val dialog = webExtensionPromptFeature.handleInstallationFailedRequest(exception = exception)

            verify { webExtensionPromptFeature.showDialog(expectedTitle, expectedMessage) }
            val linkView = dialog?.findViewById<LinkTextView>(R.id.link)
            assertFalse(linkView!!.isVisible)
        }

    @Test
    fun `WHEN calling handleInstallationFailedRequest with Unknown error and no extension name THEN showDialog with the correct message`() =
        runTest(testDispatcher) {
            val expectedTitle = ""
            val exception =
                WebExtensionInstallException.Unknown(
                    extensionName = null,
                    throwable = Exception(),
                )
            val expectedMessage = testContext.getString(addonsR.string.mozac_feature_addons_extension_failed_to_install)

            val dialog = webExtensionPromptFeature.handleInstallationFailedRequest(exception = exception)

            verify { webExtensionPromptFeature.showDialog(expectedTitle, expectedMessage) }
            val linkView = dialog?.findViewById<LinkTextView>(R.id.link)
            assertFalse(linkView!!.isVisible)
        }

    @Test
    fun `WHEN calling handleInstallationFailedRequest with CorruptFile error THEN showDialog with the correct message`() =
        runTest(testDispatcher) {
            val expectedTitle = testContext.getString(addonsR.string.mozac_feature_addons_cant_install_extension)
            val exception = WebExtensionInstallException.CorruptFile(throwable = Exception())
            val expectedMessage =
                testContext.getString(addonsR.string.mozac_feature_addons_extension_failed_to_install_corrupt_error)

            val dialog = webExtensionPromptFeature.handleInstallationFailedRequest(exception = exception)

            verify { webExtensionPromptFeature.showDialog(expectedTitle, expectedMessage) }
            val linkView = dialog?.findViewById<LinkTextView>(R.id.link)
            assertFalse(linkView!!.isVisible)
        }

    @Test
    fun `WHEN calling handleInstallationFailedRequest with NotSigned error THEN showDialog with the correct message`() =
        runTest(testDispatcher) {
            val expectedTitle = testContext.getString(addonsR.string.mozac_feature_addons_cant_install_extension)
            val exception = WebExtensionInstallException.NotSigned(throwable = Exception())
            val expectedMessage =
                testContext.getString(addonsR.string.mozac_feature_addons_extension_failed_to_install_not_signed_error)

            val dialog = webExtensionPromptFeature.handleInstallationFailedRequest(exception = exception)

            verify { webExtensionPromptFeature.showDialog(expectedTitle, expectedMessage) }
            val linkView = dialog?.findViewById<LinkTextView>(R.id.link)
            assertFalse(linkView!!.isVisible)
        }

    @Test
    fun `WHEN calling handleInstallationFailedRequest with Incompatible error THEN showDialog with the correct message`() =
        runTest(testDispatcher) {
            val expectedTitle = testContext.getString(addonsR.string.mozac_feature_addons_cant_install_extension)
            val extensionName = "extensionName"
            val exception =
                WebExtensionInstallException.Incompatible(
                    extensionName = extensionName,
                    throwable = Exception(),
                )
            val appName = testContext.getString(R.string.app_name)
            val version = testContext.appVersionName
            val expectedMessage =
                testContext.getString(
                    addonsR.string.mozac_feature_addons_failed_to_install_incompatible_error,
                    extensionName,
                    appName,
                    version,
                )

            val dialog = webExtensionPromptFeature.handleInstallationFailedRequest(exception = exception)

            verify { webExtensionPromptFeature.showDialog(expectedTitle, expectedMessage) }
            val linkView = dialog?.findViewById<LinkTextView>(R.id.link)
            assertFalse(linkView!!.isVisible)
        }

    @Test
    fun `WHEN handling InstallationRequested THEN start installing the addon and show a dialog informing about this`() =
        runTest(testDispatcher) {
            val downloadUrl = "https://example.com/addon.xpi"
            val addonName = "uBlock Origin"
            val addonIconUrl = "https://example.com/icon.png"
            val method = InstallationMethod.RTAMO
            val request =
                WebExtensionPromptRequest.InstallationRequested(
                    url = downloadUrl,
                    name = addonName,
                    iconUrl = addonIconUrl,
                    installationMethod = method,
                )
            every { webExtensionPromptFeature.startInstallingAddon(any(), any()) } just runs
            every { webExtensionPromptFeature.showDownloadAddonDialog(any(), any(), any(), any()) } returns null
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            store.dispatch(UpdatePromptRequestWebExtensionAction(request))
            testDispatcher.scheduler.advanceUntilIdle()

            verify {
                webExtensionPromptFeature.startInstallingAddon(
                    addonDownloadUrl = downloadUrl,
                    addonInstallationSource = method,
                )
            }
            verify {
                webExtensionPromptFeature.showDownloadAddonDialog(
                    addonDownloadUrl = downloadUrl,
                    addonName = addonName,
                    addonImageUrl = addonIconUrl,
                    addonInstallationSource = method,
                )
            }
            assertNull(store.state.webExtensionPromptRequest)
        }

    @Test
    fun `WHEN installing an addon THEN use the provided arguments`() =
        runTest(testDispatcher) {
            val downloadUrl = "https://example.com/addon.xpi"
            val method = InstallationMethod.RTAMO

            webExtensionPromptFeature.startInstallingAddon(
                addonDownloadUrl = downloadUrl,
                addonInstallationSource = method,
            )

            verify {
                addonManager.installAddon(
                    url = downloadUrl,
                    installationMethod = method,
                    onSuccess = any(),
                    onError = any(),
                )
            }
        }

    @Test
    fun `GIVEN installing an addon WHEN this succeeds THEN dismiss the existing dialog informing about the progress`() =
        runTest(testDispatcher) {
            val existingDialog: DownloadAddonDialogFragment = mockk(relaxed = true)
            every { fragmentManager.findFragmentByTag("DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG") } returns existingDialog
            val onSuccessSlot = slot<(Addon) -> Unit>()
            every {
                addonManager.installAddon(
                    url = any(),
                    installationMethod = any(),
                    onSuccess = capture(onSuccessSlot),
                    onError = any(),
                )
            } returns mockk()
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            webExtensionPromptFeature.startInstallingAddon(
                addonDownloadUrl = "https://example.com/addon.xpi",
                addonInstallationSource = InstallationMethod.RTAMO,
            )

            onSuccessSlot.captured(mockk())
            testDispatcher.scheduler.advanceUntilIdle()

            verify { existingDialog.dismissAllowingStateLoss() }
        }

    @Test
    fun `GIVEN installing an addon WHEN this fails THEN dismiss the existing dialog informing about the progress`() =
        runTest(testDispatcher) {
            val existingDialog: DownloadAddonDialogFragment = mockk(relaxed = true)
            every { fragmentManager.findFragmentByTag("DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG") } returns existingDialog
            val onErrorSlot = slot<(Throwable) -> Unit>()
            every {
                addonManager.installAddon(
                    url = any(),
                    installationMethod = any(),
                    onSuccess = any(),
                    onError = capture(onErrorSlot),
                )
            } returns mockk()
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            webExtensionPromptFeature.startInstallingAddon(
                addonDownloadUrl = "https://example.com/addon.xpi",
                addonInstallationSource = InstallationMethod.RTAMO,
            )

            onErrorSlot.captured(RuntimeException("Install failed"))
            testDispatcher.scheduler.advanceUntilIdle()

            verify { existingDialog.dismissAllowingStateLoss() }
        }

    @OptIn(ExperimentalCoroutinesApi::class) // for advanceTimeBy
    @Test
    fun `GIVEN download dialog is shown WHEN install succeeds before min display elapses THEN delay the dismissal until after the min display`() =
        runTest(testDispatcher) {
            every { fragmentManager.findFragmentByTag("DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG") } returns null
            val onSuccessSlot = slot<(Addon) -> Unit>()
            every {
                addonManager.installAddon(
                    url = any(),
                    installationMethod = any(),
                    onSuccess = capture(onSuccessSlot),
                    onError = any(),
                )
            } returns mockk()
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            webExtensionPromptFeature.showDownloadAddonDialog(
                addonDownloadUrl = "https://example.com/addon.xpi",
                addonName = "uBlock Origin",
                addonImageUrl = "https://example.com/icon.png",
                addonInstallationSource = InstallationMethod.RTAMO,
            )
            webExtensionPromptFeature.startInstallingAddon(
                addonDownloadUrl = "https://example.com/addon.xpi",
                addonInstallationSource = InstallationMethod.RTAMO,
            )
            val existingDialog: DownloadAddonDialogFragment = mockk(relaxed = true)
            every { fragmentManager.findFragmentByTag("DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG") } returns existingDialog
            onSuccessSlot.captured(mockk())

            testDispatcher.scheduler.advanceTimeBy(WebExtensionPromptFeature.MIN_DOWNLOAD_DIALOG_DISPLAY_MS - 100L)
            testDispatcher.scheduler.runCurrent()
            verify(exactly = 0) { existingDialog.dismissAllowingStateLoss() }

            testDispatcher.scheduler.advanceTimeBy(200L)
            testDispatcher.scheduler.runCurrent()
            verify(exactly = 1) { existingDialog.dismissAllowingStateLoss() }
        }

    @OptIn(ExperimentalCoroutinesApi::class) // for advanceTimeBy
    @Test
    fun `GIVEN download dialog is shown WHEN install fails before min display elapses THEN delay the dismissal until after the min display`() =
        runTest(testDispatcher) {
            every { fragmentManager.findFragmentByTag("DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG") } returns null
            val onErrorSlot = slot<(Throwable) -> Unit>()
            every {
                addonManager.installAddon(
                    url = any(),
                    installationMethod = any(),
                    onSuccess = any(),
                    onError = capture(onErrorSlot),
                )
            } returns mockk()
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            webExtensionPromptFeature.showDownloadAddonDialog(
                addonDownloadUrl = "https://example.com/addon.xpi",
                addonName = "uBlock Origin",
                addonImageUrl = "https://example.com/icon.png",
                addonInstallationSource = InstallationMethod.RTAMO,
            )
            webExtensionPromptFeature.startInstallingAddon(
                addonDownloadUrl = "https://example.com/addon.xpi",
                addonInstallationSource = InstallationMethod.RTAMO,
            )
            val existingDialog: DownloadAddonDialogFragment = mockk(relaxed = true)
            every { fragmentManager.findFragmentByTag("DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG") } returns existingDialog
            onErrorSlot.captured(RuntimeException("Install failed"))

            testDispatcher.scheduler.advanceTimeBy(WebExtensionPromptFeature.MIN_DOWNLOAD_DIALOG_DISPLAY_MS - 100L)
            testDispatcher.scheduler.runCurrent()
            verify(exactly = 0) { existingDialog.dismissAllowingStateLoss() }

            testDispatcher.scheduler.advanceTimeBy(200L)
            testDispatcher.scheduler.runCurrent()
            verify(exactly = 1) { existingDialog.dismissAllowingStateLoss() }
        }

    @OptIn(ExperimentalCoroutinesApi::class) // for advanceTimeBy
    @Test
    fun `GIVEN download dialog is shown WHEN AfterInstallation is dispatched before min display elapses THEN delay handling until after the min display`() =
        runTest(testDispatcher) {
            every { fragmentManager.findFragmentByTag(any()) } returns null
            every { webExtensionPromptFeature.handleAfterInstallationRequest(any()) } returns mockk()
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            webExtensionPromptFeature.showDownloadAddonDialog(
                addonDownloadUrl = "https://example.com/addon.xpi",
                addonName = "uBlock Origin",
                addonImageUrl = "https://example.com/icon.png",
                addonInstallationSource = InstallationMethod.RTAMO,
            )

            store.dispatch(
                UpdatePromptRequestWebExtensionAction(
                    WebExtensionPromptRequest.AfterInstallation.Permissions.Optional(
                        mockk(relaxed = true),
                        emptyList(),
                        emptyList(),
                        emptyList(),
                    ) {}
                )
            )

            testDispatcher.scheduler.advanceTimeBy(WebExtensionPromptFeature.MIN_DOWNLOAD_DIALOG_DISPLAY_MS - 100L)
            testDispatcher.scheduler.runCurrent()
            verify(exactly = 0) { webExtensionPromptFeature.handleAfterInstallationRequest(any()) }

            testDispatcher.scheduler.advanceTimeBy(200L)
            testDispatcher.scheduler.runCurrent()
            verify(exactly = 1) { webExtensionPromptFeature.handleAfterInstallationRequest(any()) }
        }

    @OptIn(ExperimentalCoroutinesApi::class) // for advanceTimeBy
    @Test
    fun `GIVEN download dialog is shown WHEN InstallationFailed is dispatched before min display elapses THEN delay handling until after the min display`() =
        runTest(testDispatcher) {
            every { fragmentManager.findFragmentByTag(any()) } returns null
            every { webExtensionPromptFeature.handleInstallationFailedRequest(any()) } returns null
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            webExtensionPromptFeature.showDownloadAddonDialog(
                addonDownloadUrl = "https://example.com/addon.xpi",
                addonName = "uBlock Origin",
                addonImageUrl = "https://example.com/icon.png",
                addonInstallationSource = InstallationMethod.RTAMO,
            )

            store.dispatch(
                UpdatePromptRequestWebExtensionAction(
                    WebExtensionPromptRequest.BeforeInstallation.InstallationFailed(
                        mockk(),
                        mockk(),
                    )
                )
            )

            testDispatcher.scheduler.advanceTimeBy(WebExtensionPromptFeature.MIN_DOWNLOAD_DIALOG_DISPLAY_MS - 100L)
            testDispatcher.scheduler.runCurrent()
            verify(exactly = 0) { webExtensionPromptFeature.handleInstallationFailedRequest(any()) }

            testDispatcher.scheduler.advanceTimeBy(200L)
            testDispatcher.scheduler.runCurrent()
            verify(exactly = 1) { webExtensionPromptFeature.handleInstallationFailedRequest(any()) }
        }

    @Test
    fun `GIVEN download dialog is shown AND AfterInstallation is received WHEN download is cancelled before min display elapses THEN consume the new prompt`() =
        runTest(testDispatcher) {
            every { fragmentManager.findFragmentByTag(any()) } returns null
            every { webExtensionPromptFeature.handleAfterInstallationRequest(any()) } returns mockk()
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            val downloadDialog =
                webExtensionPromptFeature.showDownloadAddonDialog(
                    addonDownloadUrl = "https://example.com/addon.xpi",
                    addonName = "uBlock Origin",
                    addonImageUrl = "https://example.com/icon.png",
                    addonInstallationSource = InstallationMethod.RTAMO,
                )
            assertNotNull(downloadDialog)
            assertNotNull(downloadDialog.onCancelled)

            store.dispatch(
                UpdatePromptRequestWebExtensionAction(
                    WebExtensionPromptRequest.AfterInstallation.Permissions.Optional(
                        mockk(relaxed = true),
                        emptyList(),
                        emptyList(),
                        emptyList(),
                    ) {}
                )
            )
            testDispatcher.scheduler.runCurrent()

            downloadDialog.onCancelled?.invoke()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(exactly = 0) { webExtensionPromptFeature.handleAfterInstallationRequest(any()) }
            verify(exactly = 1) { webExtensionPromptFeature.consumePromptRequest() }
        }

    @Test
    fun `GIVEN download dialog is shown AND InstallationFailed is received WHEN install is cancelled before min display elapses THEN consume the new prompt`() =
        runTest(testDispatcher) {
            every { fragmentManager.findFragmentByTag(any()) } returns null
            every { webExtensionPromptFeature.handleInstallationFailedRequest(any()) } returns null
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            val downloadDialog =
                webExtensionPromptFeature.showDownloadAddonDialog(
                    addonDownloadUrl = "https://example.com/addon.xpi",
                    addonName = "uBlock Origin",
                    addonImageUrl = "https://example.com/icon.png",
                    addonInstallationSource = InstallationMethod.RTAMO,
                )
            assertNotNull(downloadDialog)
            assertNotNull(downloadDialog.onCancelled)

            store.dispatch(
                UpdatePromptRequestWebExtensionAction(
                    WebExtensionPromptRequest.BeforeInstallation.InstallationFailed(mockk(), mockk())
                )
            )
            testDispatcher.scheduler.runCurrent()

            downloadDialog.onCancelled?.invoke()
            testDispatcher.scheduler.advanceUntilIdle()

            verify(exactly = 0) { webExtensionPromptFeature.handleInstallationFailedRequest(any()) }
            verify(exactly = 1) { webExtensionPromptFeature.consumePromptRequest() }
        }

    @Test
    fun `GIVEN download dialog was previously cancelled WHEN AfterInstallation is dispatched THEN handle it without delay`() =
        runTest(testDispatcher) {
            every { fragmentManager.findFragmentByTag(any()) } returns null
            every { webExtensionPromptFeature.handleAfterInstallationRequest(any()) } returns mockk()
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            val downloadDialog =
                webExtensionPromptFeature.showDownloadAddonDialog(
                    addonDownloadUrl = "https://example.com/addon.xpi",
                    addonName = "uBlock Origin",
                    addonImageUrl = "https://example.com/icon.png",
                    addonInstallationSource = InstallationMethod.RTAMO,
                )
            assertNotNull(downloadDialog)
            assertNotNull(downloadDialog.onCancelled)
            downloadDialog.onCancelled?.invoke()
            testDispatcher.scheduler.advanceUntilIdle()

            store.dispatch(
                UpdatePromptRequestWebExtensionAction(
                    WebExtensionPromptRequest.AfterInstallation.Permissions.Optional(
                        mockk(relaxed = true),
                        emptyList(),
                        emptyList(),
                        emptyList(),
                    ) {}
                )
            )
            testDispatcher.scheduler.advanceUntilIdle()

            verify(exactly = 1) { webExtensionPromptFeature.handleAfterInstallationRequest(any()) }
        }

    @Test
    fun `GIVEN a request to show a dialog informing about the addon install progress WHEN such a dialog does not yet exist THEN a new instance is built`() =
        runTest(testDispatcher) {
            every { fragmentManager.findFragmentByTag(any()) } returns null

            val dialog =
                webExtensionPromptFeature.showDownloadAddonDialog(
                    addonDownloadUrl = "https://example.com/addon.xpi",
                    addonName = "uBlock Origin",
                    addonImageUrl = "https://example.com/icon.png",
                    addonInstallationSource = InstallationMethod.RTAMO,
                )

            assertNotNull(dialog)
        }

    @Test
    fun `GIVEN a request to show a dialog informing about the addon install progress WHEN such a dialog already exists THEN don't build a new one`() =
        runTest(testDispatcher) {
            val existingDialog: DownloadAddonDialogFragment = mockk(relaxed = true)
            every { fragmentManager.findFragmentByTag("DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG") } returns existingDialog

            val dialog =
                webExtensionPromptFeature.showDownloadAddonDialog(
                    addonDownloadUrl = "https://example.com/addon.xpi",
                    addonName = "uBlock Origin",
                    addonImageUrl = "https://example.com/icon.png",
                    addonInstallationSource = InstallationMethod.RTAMO,
                )

            assertNull(dialog)
        }

    @Test
    fun `GIVEN the feature is restarted WHEN a previous DownloadAddonDialogFragment exists THEN reattach the onCancelled handler`() =
        runTest(testDispatcher) {
            val previousDialog: DownloadAddonDialogFragment = mockk(relaxed = true)
            every { fragmentManager.findFragmentByTag("DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG") } returns previousDialog

            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify { previousDialog.onCancelled = any() }
        }

    @Test
    fun `WHEN AfterInstallation is dispatched THEN handleAfterInstallationRequest is called`() =
        runTest(testDispatcher) {
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            every { webExtensionPromptFeature.handleAfterInstallationRequest(any()) } returns mockk()

            store.dispatch(
                UpdatePromptRequestWebExtensionAction(
                    WebExtensionPromptRequest.AfterInstallation.Permissions.Optional(
                        mockk(relaxed = true),
                        emptyList(),
                        emptyList(),
                        emptyList(),
                    ) {}
                )
            )
            testDispatcher.scheduler.advanceUntilIdle()

            verify { webExtensionPromptFeature.handleAfterInstallationRequest(any()) }
        }

    @Test
    fun `GIVEN Optional Permissions WHEN handleAfterInstallationRequest is called THEN handleOptionalPermissionsRequest is called`() =
        runTest(testDispatcher) {
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            val request =
                mockk<WebExtensionPromptRequest.AfterInstallation.Permissions.Optional>(relaxed = true) {
                    every { extension } returns
                        mockk(relaxed = true) {
                            every { getMetadata() } returns
                                mockk(relaxed = true) {
                                    every { updateDate } returns "2023-10-27T10:15:30.500Z"
                                }
                        }
                }

            webExtensionPromptFeature.handleAfterInstallationRequest(request)

            verify { webExtensionPromptFeature.handleOptionalPermissionsRequest(any(), any()) }
        }

    @Test
    fun `WHEN calling handleOptionalPermissionsRequest with permissions THEN call showPermissionDialog`() =
        runTest(testDispatcher) {
            val addon: Addon = mockk(relaxed = true)
            val promptRequest =
                WebExtensionPromptRequest.AfterInstallation.Permissions.Optional(
                    extension = mockk(),
                    permissions = listOf("tabs"),
                    origins = emptyList(),
                    dataCollectionPermissions = emptyList(),
                    onConfirm = { error("onConfirm should not be invoked when a permission dialog is shown") },
                )

            webExtensionPromptFeature.handleOptionalPermissionsRequest(addon = addon, promptRequest = promptRequest)

            verify {
                webExtensionPromptFeature.showPermissionDialog(
                    eq(addon),
                    eq(promptRequest),
                    eq(PermissionsDialogMode.OPTIONAL),
                    eq(promptRequest.permissions),
                    eq(promptRequest.origins),
                    eq(promptRequest.dataCollectionPermissions),
                )
            }
        }

    @Test
    fun `WHEN calling handleOptionalPermissionsRequest with a permission that doesn't have a description THEN do not call showPermissionDialog`() =
        runTest(testDispatcher) {
            val addon: Addon = mockk(relaxed = true)
            val onConfirmCalls = mutableListOf<Boolean>()
            val onConfirm: (Boolean) -> Unit = { onConfirmCalls.add(it) }
            val promptRequest =
                WebExtensionPromptRequest.AfterInstallation.Permissions.Optional(
                    extension = mockk(),
                    // The "scripting" API permission doesn't have a description so we should not show a dialog for it.
                    permissions = listOf("scripting"),
                    origins = emptyList(),
                    dataCollectionPermissions = emptyList(),
                    onConfirm = onConfirm,
                )

            webExtensionPromptFeature.handleOptionalPermissionsRequest(addon = addon, promptRequest = promptRequest)

            verify(exactly = 0) {
                webExtensionPromptFeature.showPermissionDialog(any(), any(), any(), any(), any(), any())
            }
            assertEquals(listOf(true), onConfirmCalls)
        }

    @Test
    fun `WHEN calling handleOptionalPermissionsRequest with host permissions along with permissions that don't have a description THEN call showPermissionDialog`() =
        runTest(testDispatcher) {
            val addon: Addon = mockk(relaxed = true)
            val onConfirmCalls = mutableListOf<Boolean>()
            val onConfirm: (Boolean) -> Unit = { onConfirmCalls.add(it) }
            val promptRequest =
                WebExtensionPromptRequest.AfterInstallation.Permissions.Optional(
                    extension = mockk(),
                    // The "scripting" API permission doesn't have a description so we should not show a dialog for it.
                    permissions = listOf("scripting"),
                    origins = listOf("*://developer.mozilla.org/*"),
                    dataCollectionPermissions = emptyList(),
                    onConfirm = onConfirm,
                )

            webExtensionPromptFeature.handleOptionalPermissionsRequest(addon = addon, promptRequest = promptRequest)

            verify {
                webExtensionPromptFeature.showPermissionDialog(
                    eq(addon),
                    eq(promptRequest),
                    eq(PermissionsDialogMode.OPTIONAL),
                    eq(promptRequest.permissions),
                    eq(promptRequest.origins),
                    eq(promptRequest.dataCollectionPermissions),
                )
            }

            assertTrue(onConfirmCalls.isEmpty())
        }

    @Test
    fun `WHEN calling handleOptionalPermissionsRequest with no permissions THEN do not call showPermissionDialog`() =
        runTest(testDispatcher) {
            val addon: Addon = mockk(relaxed = true)
            val onConfirmCalls = mutableListOf<Boolean>()
            val onConfirm: (Boolean) -> Unit = { onConfirmCalls.add(it) }
            val promptRequest =
                WebExtensionPromptRequest.AfterInstallation.Permissions.Optional(
                    extension = mockk(),
                    permissions = emptyList(),
                    origins = emptyList(),
                    dataCollectionPermissions = emptyList(),
                    onConfirm = onConfirm,
                )

            webExtensionPromptFeature.handleOptionalPermissionsRequest(addon = addon, promptRequest = promptRequest)

            verify(exactly = 0) {
                webExtensionPromptFeature.showPermissionDialog(any(), any(), any(), any(), any(), any())
            }
            assertEquals(listOf(true), onConfirmCalls)
        }

    @Test
    fun `WHEN calling handleInstallationFailedRequest with UnsupportedAddonType error THEN showDialog with the correct message`() =
        runTest(testDispatcher) {
            val expectedTitle = ""
            val extensionName = "extensionName"
            val exception =
                WebExtensionInstallException.UnsupportedAddonType(
                    extensionName = extensionName,
                    throwable = Exception(),
                )
            val expectedMessage =
                testContext.getString(addonsR.string.mozac_feature_addons_failed_to_install, extensionName)

            val dialog = webExtensionPromptFeature.handleInstallationFailedRequest(exception = exception)

            verify { webExtensionPromptFeature.showDialog(expectedTitle, expectedMessage) }
            val linkView = dialog?.findViewById<LinkTextView>(R.id.link)
            assertFalse(linkView!!.isVisible)
        }

    @Test
    fun `WHEN calling handleInstallationFailedRequest with AdminInstallOnly error THEN showDialog with the correct message`() =
        runTest(testDispatcher) {
            val expectedTitle = testContext.getString(addonsR.string.mozac_feature_addons_cant_install_extension)
            val extensionName = "extensionName"
            val exception =
                WebExtensionInstallException.AdminInstallOnly(
                    extensionName = extensionName,
                    throwable = Exception(),
                )
            val expectedMessage =
                testContext.getString(addonsR.string.mozac_feature_addons_admin_install_only, extensionName)

            val dialog = webExtensionPromptFeature.handleInstallationFailedRequest(exception = exception)

            verify { webExtensionPromptFeature.showDialog(expectedTitle, expectedMessage) }
            val linkView = dialog?.findViewById<LinkTextView>(R.id.link)
            assertFalse(linkView!!.isVisible)
        }

    @Test
    fun `WHEN calling handleInstallationFailedRequest with SoftBlocked error THEN showDialog with the correct message`() =
        runTest(testDispatcher) {
            val expectedTitle = testContext.getString(addonsR.string.mozac_feature_addons_cant_install_extension)
            val extensionId = "extensionId"
            val extensionName = "extensionName"
            val extensionVersion = "extensionVersion"
            val exception =
                WebExtensionInstallException.SoftBlocked(
                    extensionId = extensionId,
                    extensionName = extensionName,
                    extensionVersion = extensionVersion,
                    throwable = Exception(),
                )
            val appName = testContext.getString(R.string.app_name)
            val expectedMessage =
                testContext.getString(addonsR.string.mozac_feature_addons_soft_blocked_2, extensionName, appName)
            val expectedUrl = "${BuildConfig.AMO_BASE_URL}/android/blocked-addon/$extensionId/$extensionVersion/"

            val dialog = webExtensionPromptFeature.handleInstallationFailedRequest(exception = exception)

            verify { webExtensionPromptFeature.showDialog(expectedTitle, expectedMessage, expectedUrl) }
            val linkView = dialog?.findViewById<LinkTextView>(R.id.link)
            assertTrue(linkView!!.isVisible)

            // Click the link, then verify.
            linkView.performClick()
            assertEquals(listOf(expectedUrl to true), onLinkClickedCalls)
        }

    @Test
    fun `WHEN clicking Learn More on the Permissions Dialog THEN open the correct SUMO page in a custom tab`() =
        runTest(testDispatcher) {
            val addon: Addon = mockk(relaxed = true)
            val fragment =
                webExtensionPromptFeature.showPermissionDialog(
                    addon = addon,
                    promptRequest = mockk(relaxed = true),
                    mode = PermissionsDialogMode.INSTALL,
                    permissions = emptyList(),
                    origins = emptyList(),
                    dataCollectionPermissions = emptyList(),
                )
            val spyFragment = fragment?.let { spyk(it) }

            every { spyFragment?.requireContext() } returns testContext

            val dialog = spyFragment?.onCreateDialog(null)
            dialog?.findViewById<TextView>(addonsR.id.learn_more_link)?.performClick()

            val expectedUrl =
                SupportUtils.getSumoURLForTopic(
                    testContext,
                    SupportUtils.SumoTopic.EXTENSION_PERMISSIONS,
                )
            assertEquals(listOf(expectedUrl to false), onLinkClickedCalls)
        }

    @Test
    fun `WHEN clicking the link in the description THEN navigates to the add-on detail view`() =
        runTest(testDispatcher) {
            val addon: Addon = mockk(relaxed = true)
            val fragment = webExtensionPromptFeature.showPostInstallationDialog(addon = addon)

            // Simulate a click to the link in the description.
            fragment?.onExtensionSettingsLinkClicked?.invoke(addon)

            verify {
                navController.navigate(
                    AddonsManagementFragmentDirections.actionGlobalToInstalledAddonDetailsFragment(addon)
                )
            }
        }

    @Test
    fun `WHEN showing a permissions prompt THEN the dialog has the request's instance id`() {
        val promptRequest = newOptionalRequest()

        val dialog = webExtensionPromptFeature.showPermissionDialog(extension, promptRequest)

        assertEquals(promptRequest.instanceId, dialog!!.promptInstanceId)
    }

    @Test
    fun `WHEN a recreated permissions prompt's request is in the store THEN it gets its handlers back`() =
        runTest(testDispatcher) {
            val answers = mutableListOf<Boolean>()
            val promptRequest = newOptionalRequest { answers.add(it) }
            store.dispatch(UpdatePromptRequestWebExtensionAction(promptRequest))
            val dialog = recreatedDialog(promptRequest.instanceId)

            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            val onPositiveButtonClicked = slot<(Addon, Boolean, Boolean) -> Unit>()
            verify { dialog.onPositiveButtonClicked = capture(onPositiveButtonClicked) }
            verify { dialog.onNegativeButtonClicked = any() }
            verify(exactly = 0) { dialog.dismissNow() }
            onPositiveButtonClicked.captured(extension, false, false)
            assertEquals(listOf(true), answers)
        }

    @Test
    fun `WHEN a recreated permissions prompt's request was replaced THEN dismiss it`() =
        runTest(testDispatcher) {
            store.dispatch(UpdatePromptRequestWebExtensionAction(newOptionalRequest()))
            val dialog = recreatedDialog(newOptionalRequest().instanceId)

            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify { dialog.dismissNow() }
            verify(exactly = 0) { dialog.onPositiveButtonClicked = any() }
        }

    @Test
    fun `WHEN showing an extension update prompt THEN show the update dialog for that update`() {
        val dialog = webExtensionPromptFeature.showUpdatePermissionDialog(extension, pendingUpdate)
        val spyDialog = spyk(dialog!!)
        every { spyDialog.requireContext() } returns testContext

        val title = spyDialog.onCreateDialog(null).findViewById<TextView>(addonsR.id.title)

        assertEquals(pendingUpdate.instanceId, dialog.promptInstanceId)
        assertEquals(
            testContext.getString(addonsR.string.mozac_feature_addons_updater_permissions_dialog_title, "Extension"),
            title.text.toString(),
        )
    }

    @Test
    fun `WHEN a permissions prompt is already shown THEN neither an update nor an install prompt is shown`() {
        every { fragmentManager.findFragmentByTag(PERMISSIONS_DIALOG_TAG) } returns mockk<PermissionsDialogFragment>()

        assertNull(webExtensionPromptFeature.showUpdatePermissionDialog(extension, pendingUpdate))
        assertNull(webExtensionPromptFeature.showPermissionDialog(extension, mockk(relaxed = true)))
    }

    @Test
    fun `WHEN the extension update prompt is accepted THEN install the update and remove it`() {
        store.dispatch(UpdatePendingExtensionUpdateAction(pendingUpdate))
        val dialog = webExtensionPromptFeature.showUpdatePermissionDialog(extension, pendingUpdate)

        dialog!!.onPositiveButtonClicked?.invoke(extension, false, false)

        assertEquals(listOf(true), updateAnswers)
        assertTrue(store.state.pendingExtensionUpdates.isEmpty())
    }

    @Test
    fun `WHEN the extension update prompt is accepted after the update was replaced THEN do nothing`() {
        val newerUpdate = newPendingUpdate()
        store.dispatch(UpdatePendingExtensionUpdateAction(newerUpdate))
        val dialog = webExtensionPromptFeature.showUpdatePermissionDialog(extension, pendingUpdate)

        dialog!!.onPositiveButtonClicked?.invoke(extension, false, false)

        assertTrue(updateAnswers.isEmpty())
        assertEquals(newerUpdate, store.state.pendingExtensionUpdates["extension"])
    }

    @Test
    fun `WHEN the extension update prompt is cancelled THEN the update stays pending`() {
        store.dispatch(UpdatePendingExtensionUpdateAction(pendingUpdate))
        val dialog = webExtensionPromptFeature.showUpdatePermissionDialog(extension, pendingUpdate)

        dialog!!.onNegativeButtonClicked?.invoke()

        assertTrue(updateAnswers.isEmpty())
        assertEquals(pendingUpdate, store.state.pendingExtensionUpdates["extension"])
    }

    @Test
    fun `WHEN a recreated extension update prompt's update is pending THEN it gets its handlers back`() =
        runTest(testDispatcher) {
            store.dispatch(UpdatePendingExtensionUpdateAction(pendingUpdate))
            val dialog = recreatedDialog(pendingUpdate.instanceId)

            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            val onPositiveButtonClicked = slot<(Addon, Boolean, Boolean) -> Unit>()
            verify { dialog.onPositiveButtonClicked = capture(onPositiveButtonClicked) }
            verify(exactly = 0) { dialog.dismissNow() }
            onPositiveButtonClicked.captured(extension, false, false)
            assertEquals(listOf(true), updateAnswers)
        }

    @Test
    fun `WHEN a recreated extension update prompt's update is gone THEN dismiss it`() =
        runTest(testDispatcher) {
            val dialog = recreatedDialog(pendingUpdate.instanceId)

            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()

            verify { dialog.dismissNow() }
            verify(exactly = 0) { dialog.onPositiveButtonClicked = any() }
        }

    @Test
    fun `WHEN clicking Learn More on the extension update prompt THEN open the SUMO page in a custom tab`() =
        runTest(testDispatcher) {
            store.dispatch(UpdatePendingExtensionUpdateAction(pendingUpdate))
            val dialog = recreatedDialog(pendingUpdate.instanceId)
            webExtensionPromptFeature.start()
            testDispatcher.scheduler.advanceUntilIdle()
            val onLearnMoreClicked = slot<() -> Unit>()
            verify { dialog.onLearnMoreClicked = capture(onLearnMoreClicked) }

            onLearnMoreClicked.captured()

            val expectedUrl = SupportUtils.getSumoURLForTopic(testContext, SupportUtils.SumoTopic.EXTENSION_PERMISSIONS)
            assertEquals(listOf(expectedUrl to false), onLinkClickedCalls)
            assertTrue(updateAnswers.isEmpty())
        }

    private fun newPendingUpdate() =
        PendingExtensionUpdate(
            extensionId = "extension",
            permissions = listOf("privacy"),
            origins = emptyList(),
            dataCollectionPermissions = emptyList(),
            onPermissionsGranted = { updateAnswers.add(it) },
        )

    private fun newOptionalRequest(onConfirm: (Boolean) -> Unit = {}) =
        WebExtensionPromptRequest.AfterInstallation.Permissions.Optional(
            extension =
                mockk(relaxed = true) {
                    every { getMetadata() } returns
                        mockk(relaxed = true) { every { updateDate } returns "2023-10-27T10:15:30.500Z" }
                },
            permissions = listOf("tabs"),
            origins = emptyList(),
            dataCollectionPermissions = emptyList(),
            onConfirm = onConfirm,
        )

    private fun recreatedDialog(instanceId: String?): PermissionsDialogFragment {
        val dialog: PermissionsDialogFragment = mockk(relaxed = true) { every { promptInstanceId } returns instanceId }
        every { fragmentManager.findFragmentByTag(PERMISSIONS_DIALOG_TAG) } returns dialog
        return dialog
    }

    private companion object {
        const val PERMISSIONS_DIALOG_TAG = "ADDONS_PERMISSIONS_DIALOG_FRAGMENT"
    }
}
