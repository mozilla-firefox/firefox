/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import android.view.LayoutInflater
import android.view.View
import android.widget.TextView
import androidx.core.view.isVisible
import androidx.navigation.NavController
import androidx.navigation.Navigation
import com.google.android.material.materialswitch.MaterialSwitch
import io.mockk.CapturingSlot
import io.mockk.Runs
import io.mockk.coEvery
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.slot
import io.mockk.spyk
import io.mockk.verify
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.concept.base.profiler.Profiler
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.webextension.EnableSource
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.addons.R as addonsR
import mozilla.components.feature.search.SearchUseCases
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.databinding.FragmentInstalledAddOnDetailsBinding
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.settings.SupportUtils
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class InstalledAddonDetailsFragmentTest {
    private val testDispatcher = StandardTestDispatcher()
    private lateinit var fragment: InstalledAddonDetailsFragment
    private lateinit var tabsUseCases: TabsUseCases
    private lateinit var loadUrlUseCase: SessionUseCases.DefaultLoadUrlUseCase
    private lateinit var searchUseCases: SearchUseCases
    private lateinit var homepageTitle: String
    private lateinit var profiler: Profiler
    private val addonManager = mockk<AddonManager>()

    @Before
    fun setup() {
        fragment = spyk(InstalledAddonDetailsFragment())
        tabsUseCases = mockk(relaxed = true)
        loadUrlUseCase = mockk(relaxed = true)
        searchUseCases = mockk(relaxed = true)
        homepageTitle = testContext.getString(R.string.tab_tray_homepage_tab)
        profiler =
            mockk(relaxed = true) {
                every { getProfilerTime() } returns PROFILER_START_TIME
                every { isProfilerActive() } returns true
            }
    }

    @Test
    fun `GIVEN add-on is supported and not disabled WHEN enabling it THEN the add-on is requested by the user`() {
        val addon = mockk<Addon>()
        every { addon.isDisabledAsUnsupported() } returns false
        every { addon.isSupported() } returns true
        every { fragment.addon } returns addon
        every { addonManager.enableAddon(any(), any(), any(), any()) } just Runs

        fragment.enableAddon(addonManager, {}, {})

        verify { addonManager.enableAddon(addon, EnableSource.USER, any(), any()) }
    }

    @Test
    fun `GIVEN add-on is supported and disabled as previously unsupported WHEN enabling it THEN the add-on is requested by both the app and the user`() {
        val addon = mockk<Addon>()
        every { addon.isDisabledAsUnsupported() } returns true
        every { addon.isSupported() } returns true
        every { fragment.addon } returns addon
        val capturedAddon = slot<Addon>()
        val capturedOnSuccess = slot<(Addon) -> Unit>()
        every {
            addonManager.enableAddon(
                capture(capturedAddon),
                any(),
                capture(capturedOnSuccess),
                any(),
            )
        } answers { capturedOnSuccess.captured.invoke(capturedAddon.captured) }

        fragment.enableAddon(addonManager, {}, {})

        verify { addonManager.enableAddon(addon, EnableSource.APP_SUPPORT, any(), any()) }
        verify { addonManager.enableAddon(capturedAddon.captured, EnableSource.USER, any(), any()) }
    }

    @Test
    fun `GIVEN blocklisted addon WHEN binding the enable switch THEN disable the switch`() {
        val addon = mockk<Addon>()
        val enableSwitch = mockk<MaterialSwitch>(relaxed = true)
        val privateBrowsingSwitch = mockk<MaterialSwitch>(relaxed = true)

        every { fragment.provideEnableSwitch() } returns enableSwitch
        every { fragment.providePrivateBrowsingSwitch() } returns privateBrowsingSwitch
        every { addon.isEnabled() } returns true
        every { addon.isDisabledAsBlocklisted() } returns true
        every { fragment.addon } returns addon

        fragment.bindEnableSwitch()

        verify { enableSwitch.isEnabled = false }
    }

    @Test
    fun `GIVEN enabled addon WHEN binding the enable switch THEN do not disable the switch`() {
        val addon = mockk<Addon>()
        val enableSwitch = mockk<MaterialSwitch>(relaxed = true)
        val privateBrowsingSwitch = mockk<MaterialSwitch>(relaxed = true)

        every { fragment.provideEnableSwitch() } returns enableSwitch
        every { fragment.providePrivateBrowsingSwitch() } returns privateBrowsingSwitch
        every { addon.isDisabledAsBlocklisted() } returns false
        every { addon.isDisabledAsNotCorrectlySigned() } returns false
        every { addon.isDisabledAsIncompatible() } returns false
        every { addon.isEnabled() } returns true
        every { fragment.addon } returns addon

        fragment.bindEnableSwitch()

        verify(exactly = 0) { enableSwitch.isEnabled = false }
    }

    @Test
    fun `GIVEN addon not correctly signed WHEN binding the enable switch THEN disable the switch`() {
        val addon = mockk<Addon>()
        val enableSwitch = mockk<MaterialSwitch>(relaxed = true)
        val privateBrowsingSwitch = mockk<MaterialSwitch>(relaxed = true)

        every { fragment.provideEnableSwitch() } returns enableSwitch
        every { fragment.providePrivateBrowsingSwitch() } returns privateBrowsingSwitch
        every { addon.isEnabled() } returns true
        every { addon.isDisabledAsBlocklisted() } returns false
        every { addon.isDisabledAsNotCorrectlySigned() } returns true
        every { fragment.addon } returns addon

        fragment.bindEnableSwitch()

        verify { enableSwitch.isEnabled = false }
    }

    @Test
    fun `GIVEN incompatible addon WHEN binding the enable switch THEN disable the switch`() {
        val addon = mockk<Addon>()
        val enableSwitch = mockk<MaterialSwitch>(relaxed = true)
        val privateBrowsingSwitch = mockk<MaterialSwitch>(relaxed = true)

        every { fragment.provideEnableSwitch() } returns enableSwitch
        every { fragment.providePrivateBrowsingSwitch() } returns privateBrowsingSwitch
        every { addon.isEnabled() } returns true
        every { addon.isDisabledAsBlocklisted() } returns false
        every { addon.isDisabledAsNotCorrectlySigned() } returns false
        every { addon.isDisabledAsIncompatible() } returns true
        every { fragment.addon } returns addon

        fragment.bindEnableSwitch()

        verify { enableSwitch.isEnabled = false }
    }

    @Test
    fun `GIVEN enabling fails WHEN tapping retry on the snackbar THEN the enable is attempted again`() {
        val addon = mockAddon()
        every { addon.isEnabled() } returns false
        every { addon.isSupported() } returns true
        every { addon.isDisabledAsUnsupported() } returns false
        setUpFragmentForAddon(addon)
        every {
            addonManager.enableAddon(addon, EnableSource.USER, any(), any())
        } answers { lastArg<(Throwable) -> Unit>().invoke(RuntimeException("boom")) }
        val onRetry = captureSnackbarRetry()

        fragment.toggleAddon(
            switch = mockk(relaxed = true),
            privateBrowsingSwitch = mockk(relaxed = true),
            isChecked = true,
        )

        verify(exactly = 1) { addonManager.enableAddon(addon, EnableSource.USER, any(), any()) }
        verify { fragment.showAddonFailureSnackbar(any(), any()) }

        onRetry.captured.invoke()

        verify(exactly = 2) { addonManager.enableAddon(addon, EnableSource.USER, any(), any()) }
    }

    @Test
    fun `GIVEN enabling fails WHEN the addon is enabled again before tapping retry THEN the enable is not attempted again`() {
        val addon = mockAddon()
        every { addon.isEnabled() } returns false
        every { addon.isSupported() } returns true
        every { addon.isDisabledAsUnsupported() } returns false
        setUpFragmentForAddon(addon)
        every {
            addonManager.enableAddon(addon, EnableSource.USER, any(), any())
        } answers { lastArg<(Throwable) -> Unit>().invoke(RuntimeException("boom")) }
        val onRetry = captureSnackbarRetry()

        fragment.toggleAddon(
            switch = mockk(relaxed = true),
            privateBrowsingSwitch = mockk(relaxed = true),
            isChecked = true,
        )

        verify(exactly = 1) { addonManager.enableAddon(addon, EnableSource.USER, any(), any()) }

        every { addon.isEnabled() } returns true

        onRetry.captured.invoke()

        verify(exactly = 1) { addonManager.enableAddon(addon, EnableSource.USER, any(), any()) }
    }

    @Test
    fun `GIVEN disabling fails WHEN tapping retry on the snackbar THEN the disable is attempted again`() {
        val addon = mockAddon()
        every { addon.isEnabled() } returns true
        setUpFragmentForAddon(addon)
        every {
            addonManager.disableAddon(addon, any(), any(), any())
        } answers { lastArg<(Throwable) -> Unit>().invoke(RuntimeException("boom")) }
        val onRetry = captureSnackbarRetry()

        fragment.toggleAddon(
            switch = mockk(relaxed = true),
            privateBrowsingSwitch = mockk(relaxed = true),
            isChecked = false,
        )

        verify(exactly = 1) { addonManager.disableAddon(addon, any(), any(), any()) }
        verify { fragment.showAddonFailureSnackbar(any(), any()) }

        onRetry.captured.invoke()

        verify(exactly = 2) { addonManager.disableAddon(addon, any(), any(), any()) }
    }

    @Test
    fun `GIVEN an add-on with a settings page WHEN disabling it fails THEN the settings stay visible`() {
        val addon = mockAddon()
        every { addon.isEnabled() } returns true
        setUpFragmentForAddon(addon)
        every {
            addonManager.disableAddon(addon, any(), any(), any())
        } answers { lastArg<(Throwable) -> Unit>().invoke(RuntimeException("boom")) }
        captureSnackbarRetry()

        fragment.toggleAddon(
            switch = mockk(relaxed = true),
            privateBrowsingSwitch = mockk(relaxed = true),
            isChecked = false,
        )

        assertTrue(fragment.binding.settings.isVisible)
    }

    @Test
    fun `GIVEN uninstalling fails WHEN tapping retry on the snackbar THEN the uninstall is attempted again`() {
        val addon = mockAddon()
        setUpFragmentForAddon(addon)
        every {
            addonManager.uninstallAddon(addon, any(), any())
        } answers { lastArg<(String, Throwable) -> Unit>().invoke("some-addon-id", RuntimeException("boom")) }
        val onRetry = captureSnackbarRetry()
        fragment.bindRemoveButton()

        fragment.binding.removeAddOn.performClick()

        verify(exactly = 1) { addonManager.uninstallAddon(addon, any(), any()) }
        verify { fragment.showAddonFailureSnackbar(any(), any()) }

        onRetry.captured.invoke()

        verify(exactly = 2) { addonManager.uninstallAddon(addon, any(), any()) }
    }

    @Test
    fun `GIVEN the fragment is detached WHEN tapping retry on the snackbar THEN the retry action is not run`() {
        var retried = false
        val guardedRetry = fragment.retryIfFragmentIsAttached { retried = true }

        every { fragment.context } returns null
        guardedRetry.invoke()
        assertFalse(retried)

        every { fragment.context } returns testContext
        guardedRetry.invoke()
        assertTrue(retried)
    }

    @Test
    fun `GIVEN an add-on WHEN clicking the report button THEN a new tab is open`() {
        val addon = mockAddon()
        every { fragment.addon } returns addon
        val useCases = mockk<TabsUseCases>()
        val selectOrAddTab = mockk<TabsUseCases.SelectOrAddUseCase>()
        every { selectOrAddTab.invoke(any(), any(), any(), any(), any()) } returns "some-tab-id"
        every { useCases.selectOrAddTab } returns selectOrAddTab
        every { testContext.components.useCases.tabsUseCases } returns useCases
        // We create the `binding` instance and bind the UI here because `onCreateView()` checks a late init variable
        // and we cannot easily mock it to skip the check.
        fragment.setBindingAndBindUI(
            FragmentInstalledAddOnDetailsBinding.inflate(
                LayoutInflater.from(testContext),
                mockk(relaxed = true),
                false,
            )
        )
        val navController = mockk<NavController>(relaxed = true)
        Navigation.setViewNavController(fragment.binding.root, navController)

        val appStore = mockk<AppStore>()
        val appState = mockk<AppState>()
        every { appState.mode } returns
            mockk(relaxed = true) {
                every { isPrivate } returns false // or true
            }

        every { appStore.state } returns appState
        every { testContext.components.appStore } returns appStore

        // Click the report button.
        fragment.binding.reportAddOn.performClick()

        verify {
            selectOrAddTab.invoke(
                url = "https://addons.mozilla.org/android/feedback/addon/some-addon-id/",
                private = false,
                ignoreFragment = true,
            )
        }
        verify {
            navController.navigate(InstalledAddonDetailsFragmentDirections.actionGlobalBrowser(null))
        }
    }

    @Test
    fun `GIVEN an add-on and private browsing mode is used WHEN clicking the report button THEN a new private tab is open`() {
        val addon = mockAddon()
        every { fragment.addon } returns addon
        val useCases = mockk<TabsUseCases>()
        val selectOrAddTab = mockk<TabsUseCases.SelectOrAddUseCase>()
        every { selectOrAddTab.invoke(any(), any(), any(), any(), any()) } returns "some-tab-id"
        every { useCases.selectOrAddTab } returns selectOrAddTab
        every { testContext.components.useCases.tabsUseCases } returns useCases
        every { testContext.components.appStore.state.mode.isPrivate } returns true
        // We create the `binding` instance and bind the UI here because `onCreateView()` checks a late init variable
        // and we cannot easily mock it to skip the check.
        fragment.setBindingAndBindUI(
            FragmentInstalledAddOnDetailsBinding.inflate(
                LayoutInflater.from(testContext),
                mockk(relaxed = true),
                false,
            )
        )
        val navController = mockk<NavController>(relaxed = true)
        Navigation.setViewNavController(fragment.binding.root, navController)

        // Click the report button.
        fragment.binding.reportAddOn.performClick()

        verify {
            selectOrAddTab.invoke(
                url = "https://addons.mozilla.org/android/feedback/addon/some-addon-id/",
                private = true,
                ignoreFragment = true,
            )
        }
        verify {
            navController.navigate(InstalledAddonDetailsFragmentDirections.actionGlobalBrowser(null))
        }
    }

    @Test
    fun `GIVEN an add-on with 'openOptionsPageInTab' false WHEN clicking the settings button THEN navigation to the embedded options page occurs`() {
        val addon = mockAddon(openOptionsPageInTab = false)
        every { fragment.addon } returns addon
        every { fragment.activity } returns mockk<HomeActivity>(relaxed = true)

        every { fragment.requireContext() } returns testContext

        // We create the `binding` instance and bind the UI here because `onCreateView()` checks a late init variable
        // and we cannot easily mock it to skip the check.
        fragment.setBindingAndBindUI(
            FragmentInstalledAddOnDetailsBinding.inflate(
                LayoutInflater.from(testContext),
                mockk(relaxed = true),
                false,
            )
        )
        val navController = mockk<NavController>(relaxed = true)
        Navigation.setViewNavController(fragment.binding.root, navController)

        // Click the settings button.
        fragment.binding.settings.performClick()

        val expectedDirections =
            InstalledAddonDetailsFragmentDirections.actionInstalledAddonFragmentToAddonInternalSettingsFragment(
                webExtensionName = "some-name",
                optionsPageUrl = "some-addon-options-page-url",
                webExtensionId = "some-addon-id",
            )
        verify {
            navController.navigate(eq(expectedDirections))
        }
    }

    @Test
    fun `GIVEN addon does not allow private browsing WHEN binding THEN update switch`() {
        val addon = mockAddon()
        val privateBrowsingSwitch = mockk<MaterialSwitch>(relaxed = true)

        every { fragment.providePrivateBrowsingSwitch() } returns privateBrowsingSwitch
        every { addon.incognito } returns Addon.Incognito.NOT_ALLOWED
        every { fragment.addon } returns addon
        every { fragment.context } returns testContext

        fragment.bindAllowInPrivateBrowsingSwitch()

        verify { privateBrowsingSwitch.isEnabled = false }
        verify { privateBrowsingSwitch.isChecked = false }
        verify { privateBrowsingSwitch.text = "Not allowed in private windows" }
    }

    @Test
    fun `GIVEN a not found addon WHEN binding THEN show an error message`() =
        runTest(testDispatcher) {
            val addon = mockAddon()

            coEvery { addonManager.getAddonByID(any()) } returns null
            every { fragment.activity } returns mockk<HomeActivity>(relaxed = true)
            every { addon.isInstalled() } returns true
            every { fragment.provideAddonManager() } returns addonManager
            every { fragment.addon } returns addon
            every { fragment.context } returns testContext
            every { fragment.bindUI() } just Runs
            every { fragment.showUnableToQueryAddonsMessage() } just Runs
            every { testContext.components.analytics.crashReporter } returns mockk(relaxed = true)

            fragment._binding =
                FragmentInstalledAddOnDetailsBinding.inflate(
                    LayoutInflater.from(testContext),
                    mockk(relaxed = true),
                    false,
                )

            fragment.bindAddon(testDispatcher, testDispatcher)
            testDispatcher.scheduler.advanceUntilIdle()

            verify { fragment.showUnableToQueryAddonsMessage() }
            verify(exactly = 0) { fragment.bindUI() }
        }

    @Test
    fun `GIVEN an addon WHEN binding THEN bind the UI`() =
        runTest(testDispatcher) {
            val addon = mockAddon()

            coEvery { addonManager.getAddonByID(any()) } returns addon
            every { fragment.activity } returns mockk<HomeActivity>(relaxed = true)
            every { addon.isInstalled() } returns true
            every { fragment.provideAddonManager() } returns addonManager
            every { fragment.addon } returns addon
            every { fragment.context } returns testContext
            every { fragment.bindUI() } just Runs
            every { fragment.showUnableToQueryAddonsMessage() } just Runs

            fragment._binding =
                FragmentInstalledAddOnDetailsBinding.inflate(
                    LayoutInflater.from(testContext),
                    mockk(relaxed = true),
                    false,
                )

            fragment.bindAddon(testDispatcher, testDispatcher)
            testDispatcher.scheduler.advanceUntilIdle()

            verify { fragment.bindUI() }
            verify(exactly = 0) { fragment.showUnableToQueryAddonsMessage() }
        }

    @Test
    fun `GIVEN a hard blocked add-on WHEN binding THEN an error message is shown`() {
        val addon = mockAddon()
        every { addon.isDisabledAsBlocklisted() } returns true
        every { fragment.addon } returns addon
        every { fragment.context } returns testContext

        // We create the `binding` instance and bind the UI here because `onCreateView()` checks a late init variable
        // and we cannot easily mock it to skip the check.
        val binding =
            FragmentInstalledAddOnDetailsBinding.inflate(
                LayoutInflater.from(testContext),
                mockk(relaxed = true),
                false,
            )
        fragment.setBindingAndBindUI(binding)
        val navController = mockk<NavController>(relaxed = true)
        Navigation.setViewNavController(binding.root, navController)

        val appStore = AppStore(initialState = AppState(mode = BrowsingMode.Normal))
        val useCases =
            FenixBrowserUseCases(
                appStore = appStore,
                tabsUseCases = tabsUseCases,
                loadUrlUseCase = loadUrlUseCase,
                searchUseCases = searchUseCases,
                homepageTitle = homepageTitle,
                profiler = profiler,
            )
        every { testContext.components.useCases.fenixBrowserUseCases } returns useCases

        val warningView = binding.root.findViewById<View>(addonsR.id.add_on_messagebar_warning)
        assertFalse(warningView.isVisible)
        val errorView = binding.root.findViewById<View>(addonsR.id.add_on_messagebar_error)
        assertTrue(errorView.isVisible)

        errorView.findViewById<TextView>(addonsR.id.add_on_messagebar_error_learn_more_link).performClick()

        verify { navController.navigate(R.id.browserFragment) }
        val url = "${BuildConfig.AMO_BASE_URL}/android/blocked-addon/some-addon-id/1.2.3/"

        verify {
            tabsUseCases.addTab.invoke(
                url = url,
                flags = EngineSession.LoadUrlFlags.none(),
                private = false,
                historyMetadata = null,
                originalInput = url,
            )
        }
    }

    @Test
    fun `GIVEN an add-on not correctly signed WHEN binding THEN an error message is shown`() {
        val addon = mockAddon()
        every { addon.isDisabledAsNotCorrectlySigned() } returns true
        every { fragment.addon } returns addon
        every { fragment.context } returns testContext

        // We create the `binding` instance and bind the UI here because `onCreateView()` checks a late init variable
        // and we cannot easily mock it to skip the check.
        val binding =
            FragmentInstalledAddOnDetailsBinding.inflate(
                LayoutInflater.from(testContext),
                mockk(relaxed = true),
                false,
            )
        fragment.setBindingAndBindUI(binding)
        val navController = mockk<NavController>(relaxed = true)
        Navigation.setViewNavController(binding.root, navController)

        val appStore = AppStore(initialState = AppState(mode = BrowsingMode.Normal))
        val useCases =
            FenixBrowserUseCases(
                appStore = appStore,
                tabsUseCases = tabsUseCases,
                loadUrlUseCase = loadUrlUseCase,
                searchUseCases = searchUseCases,
                homepageTitle = homepageTitle,
                profiler = profiler,
            )
        every { testContext.components.useCases.fenixBrowserUseCases } returns useCases

        val warningView = binding.root.findViewById<View>(addonsR.id.add_on_messagebar_warning)
        assertFalse(warningView.isVisible)
        val errorView = binding.root.findViewById<View>(addonsR.id.add_on_messagebar_error)
        assertTrue(errorView.isVisible)

        errorView.findViewById<TextView>(addonsR.id.add_on_messagebar_error_learn_more_link).performClick()

        verify { navController.navigate(R.id.browserFragment) }

        val url = SupportUtils.getGenericSumoURLForTopic(SupportUtils.SumoTopic.UNSIGNED_ADDONS)

        verify {
            tabsUseCases.addTab.invoke(
                url = url,
                flags = EngineSession.LoadUrlFlags.none(),
                private = false,
                historyMetadata = null,
                originalInput = url,
            )
        }
    }

    @Test
    fun `GIVEN a soft-blocked add-on WHEN binding THEN a warning message is shown`() {
        val addon = mockAddon()
        every { addon.isSoftBlocked() } returns true
        every { fragment.addon } returns addon
        every { fragment.context } returns testContext

        // We create the `binding` instance and bind the UI here because `onCreateView()` checks a late init variable
        // and we cannot easily mock it to skip the check.
        val binding =
            FragmentInstalledAddOnDetailsBinding.inflate(
                LayoutInflater.from(testContext),
                mockk(relaxed = true),
                false,
            )
        fragment.setBindingAndBindUI(binding)
        val navController = mockk<NavController>(relaxed = true)
        Navigation.setViewNavController(binding.root, navController)

        val appStore = AppStore(initialState = AppState(mode = BrowsingMode.Normal))
        val useCases =
            FenixBrowserUseCases(
                appStore = appStore,
                tabsUseCases = tabsUseCases,
                loadUrlUseCase = loadUrlUseCase,
                searchUseCases = searchUseCases,
                homepageTitle = homepageTitle,
                profiler = profiler,
            )
        every { testContext.components.useCases.fenixBrowserUseCases } returns useCases

        val warningView = binding.root.findViewById<View>(addonsR.id.add_on_messagebar_warning)
        assertTrue(warningView.isVisible)
        val errorView = binding.root.findViewById<View>(addonsR.id.add_on_messagebar_error)
        assertFalse(errorView.isVisible)

        warningView.findViewById<TextView>(addonsR.id.add_on_messagebar_warning_learn_more_link).performClick()

        verify { navController.navigate(R.id.browserFragment) }
        val url = "${BuildConfig.AMO_BASE_URL}/android/blocked-addon/some-addon-id/1.2.3/"

        verify {
            tabsUseCases.addTab.invoke(
                url = url,
                flags = EngineSession.LoadUrlFlags.none(),
                private = false,
                historyMetadata = null,
                originalInput = url,
            )
        }
    }

    private fun mockAddon(openOptionsPageInTab: Boolean = true): Addon {
        val addon: Addon = mockk()
        every { addon.id } returns "some-addon-id"
        every { addon.version } returns "1.2.3"
        every { addon.incognito } returns Addon.Incognito.SPANNING
        every { addon.isEnabled() } returns true
        every { addon.isDisabledAsBlocklisted() } returns false
        every { addon.isDisabledAsNotCorrectlySigned() } returns false
        every { addon.isDisabledAsIncompatible() } returns false
        every { addon.isAllowedInPrivateBrowsing() } returns false
        every { addon.translatableName } returns mapOf("en-US" to "some-name")
        every { addon.defaultLocale } returns "en-US"

        val installedState: Addon.InstalledState = mockk()
        every { installedState.openOptionsPageInTab } returns openOptionsPageInTab
        every { installedState.optionsPageUrl } returns "some-addon-options-page-url"
        every { addon.installedState } returns installedState

        return addon
    }

    private fun setUpFragmentForAddon(addon: Addon) {
        every { fragment.addon } returns addon
        every { fragment.context } returns testContext
        every { fragment.provideAddonManager() } returns addonManager
        fragment._binding =
            FragmentInstalledAddOnDetailsBinding.inflate(
                LayoutInflater.from(testContext),
                mockk(relaxed = true),
                false,
            )
    }

    private fun captureSnackbarRetry(): CapturingSlot<() -> Unit> {
        val onRetry = slot<() -> Unit>()
        every { fragment.showAddonFailureSnackbar(any(), capture(onRetry)) } just Runs
        return onRetry
    }

    companion object {
        private const val PROFILER_START_TIME = Double.MAX_VALUE
    }
}
