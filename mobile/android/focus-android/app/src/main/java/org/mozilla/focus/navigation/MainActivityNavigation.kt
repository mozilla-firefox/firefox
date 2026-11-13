/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.navigation

import android.os.Bundle
import androidx.fragment.app.Fragment
import androidx.fragment.app.FragmentManager
import androidx.fragment.app.FragmentTransaction
import androidx.fragment.app.commit
import androidx.fragment.app.replace
import org.mozilla.focus.R
import org.mozilla.focus.activity.MainActivity
import org.mozilla.focus.biometrics.BiometricAuthenticationFragment
import org.mozilla.focus.fragment.BrowserFragment
import org.mozilla.focus.fragment.CrashListFragment
import org.mozilla.focus.fragment.UrlInputFragment
import org.mozilla.focus.fragment.onboarding.OnboardingFirstFragment
import org.mozilla.focus.fragment.onboarding.OnboardingSecondFragment
import org.mozilla.focus.fragment.onboarding.OnboardingStep
import org.mozilla.focus.fragment.onboarding.OnboardingStorage
import org.mozilla.focus.nimbus.FocusNimbus
import org.mozilla.focus.settings.permissions.permissionoptions.SitePermission
import org.mozilla.focus.settings.permissions.permissionoptions.SitePermissionOptionsFragment
import org.mozilla.focus.state.Screen

/**
 * A helper class that manages fragment-based navigation within [MainActivity].
 *
 * This class centralizes the logic for creating and switching between different fragments (e.g., home screen, browser,
 * settings, onboarding) using the [FragmentManager]. By encapsulating navigation, it simplifies management and testing.
 *
 * @param supportFragmentManager The [FragmentManager] used to perform fragment transactions.
 * @param onboardingStorage Manages the state and progress of the user onboarding flow.
 * @param isInPictureInPictureMode A lambda that returns whether the activity is currently in PiP mode.
 * @param shouldAnimateHome A lambda that determines if the home screen transition should be animated.
 * @param showStartBrowsingCfr A function to trigger the "start browsing" CFR if applicable.
 * @param onEraseAction A function to potentially trigger the widget promotion.
 */
class MainActivityNavigation(
    private val supportFragmentManager: FragmentManager,
    private val onboardingStorage: OnboardingStorage,
    private val isInPictureInPictureMode: () -> Boolean,
    private val shouldAnimateHome: () -> Boolean,
    private val showStartBrowsingCfr: () -> Unit,
    private val onEraseAction: () -> Unit,
) : AppNavigation {

    /** Home screen. */
    override fun navigateToHome() {
        // The erase action should only be triggered when we are navigating away from an existing browser session.
        if (supportFragmentManager.findFragmentByTag(BrowserFragment.FRAGMENT_TAG) != null) {
            onEraseAction()
        }

        // We only want to play the animation if a browser fragment is added and resumed.
        // If it is not resumed then the application is currently in the process of resuming
        // and the session was removed while the app was in the background (e.g. via the
        // notification). In this case we do not want to show the content and remove the
        // browser fragment immediately.
        commitFragmentTransaction(allowStateLoss = true) {
            if (shouldAnimateHome()) {
                setCustomAnimations(0, R.anim.erase_animation)
            }
            replace<UrlInputFragment>(R.id.container, UrlInputFragment.FRAGMENT_TAG)
        }
        showStartBrowsingCfr()
    }

    /** Show browser for tab with the given [tabId]. */
    override fun navigateToBrowser(tabId: String) {
        val currentFragment = supportFragmentManager.findFragmentById(R.id.container)

        // Check if the correct BrowserFragment is already visible.
        if (currentFragment is BrowserFragment && currentFragment.tab.id == tabId) {
            return
        }

        commitFragmentTransaction(allowStateLoss = true) {
            val args = BrowserFragment.bundleForTab(tabId)
            replace<BrowserFragment>(R.id.container, BrowserFragment.FRAGMENT_TAG, args)
        }
    }

    /**
     * Opens the URL input fragment to allow editing the URL of the tab associated with the given [tabId].
     *
     * If a URL input fragment is already open for this tab, this function does nothing.
     *
     * @param tabId The ID of the tab whose URL is to be edited.
     */
    override fun navigateToEditUrl(tabId: String) {
        val existingFragment =
            supportFragmentManager.findFragmentByTag(UrlInputFragment.FRAGMENT_TAG) as? UrlInputFragment

        if (existingFragment?.tab?.id == tabId) {
            // There's already an UrlInputFragment for this tab.
            return
        }

        val args = UrlInputFragment.bundleForTab(tabId)
        commitFragmentTransaction {
            add(R.id.container, UrlInputFragment::class.java, args, UrlInputFragment.FRAGMENT_TAG)
        }
    }

    /** Show onBoarding. */
    override fun navigateToFirstRun() {
        FocusNimbus.features.onboarding.recordExposure()

        when (onboardingStorage.getCurrentOnboardingStep()) {
            OnboardingStep.ON_BOARDING_FIRST_SCREEN ->
                navigateTo<OnboardingFirstFragment>(tag = OnboardingFirstFragment.FRAGMENT_TAG)

            OnboardingStep.ON_BOARDING_SECOND_SCREEN ->
                navigateTo<OnboardingSecondFragment>(tag = OnboardingSecondFragment.FRAGMENT_TAG)
        }
    }

    /**
     * Shows the second screen of the onboarding process. This function replaces the current fragment with
     * [OnboardingSecondFragment].
     */
    override fun navigateToOnboardingSecondScreen() {
        navigateTo<OnboardingSecondFragment>(tag = OnboardingSecondFragment.FRAGMENT_TAG)
    }

    /** Show content of about:crashes */
    override fun showCrashList() {
        navigateTo<CrashListFragment>(
            tag = CrashListFragment.FRAGMENT_TAG,
            args = Bundle().apply { putBoolean("show_all", true) },
        )
    }

    /**
     * Lock app.
     *
     * @param bundle it is used for app navigation. If the user can unlock with success he should be redirected to a
     *   certain screen. It comes from the external intent.
     */
    override fun navigateToLockScreen(bundle: Bundle?) {
        if (isInPictureInPictureMode()) {
            return
        }

        val biometricAuthenticationFragment =
            supportFragmentManager.findFragmentByTag(BiometricAuthenticationFragment.FRAGMENT_TAG)
        if (biometricAuthenticationFragment != null) {
            bundle?.let { biometricAuthenticationFragment.arguments = it }
            return
        }

        commitFragmentTransaction {
            supportFragmentManager.fragments.forEach { remove(it) }
            replace<BiometricAuthenticationFragment>(
                R.id.container,
                BiometricAuthenticationFragment.FRAGMENT_TAG,
                bundle,
            )
        }
    }

    /**
     * Navigates to the specified settings page.
     *
     * This function determines which settings fragment to display based on the [page] parameter. It then replaces the
     * current fragment with the new settings fragment, ensuring that the same fragment is not added multiple times.
     *
     * @param page The specific settings page to navigate to, defined in [Screen.Settings.Page].
     */
    override fun navigateToSettings(page: Screen.Settings.Page) {
        val tag = "settings_${page.fragmentClass.simpleName}"

        navigateTo(page.fragmentClass, tag = tag)
    }

    /**
     * Navigates to the site permission options screen. This function displays a fragment that allows users to configure
     * specific permissions for a website.
     *
     * @param sitePermission The [SitePermission] object containing information about the site and its current
     *   permissions.
     */
    override fun navigateToSitePermissionOptions(sitePermission: SitePermission) {
        navigateTo<SitePermissionOptionsFragment>(
            tag = SitePermissionOptionsFragment.FRAGMENT_TAG,
            args = SitePermissionOptionsFragment.bundleForSitePermission(sitePermission),
        )
    }

    /**
     * A generic navigator to replace the fragment in the main container.
     *
     * This function handles the transaction to replace the currently displayed fragment with a new one. It prevents
     * redundant transactions by checking if a fragment with the same tag is already the active one in the container.
     *
     * @param fragmentClass The class of the fragment to instantiate and display.
     * @param tag A unique tag used to identify the fragment in the FragmentManager.
     * @param args An optional [Bundle] of arguments to pass to the new fragment.
     * @param allowStateLoss If true, the transaction can be committed even after the activity's state has been saved.
     * @param transactionSetup An optional lambda to apply custom configurations to the [FragmentTransaction], such as
     *   setting custom animations.
     */
    private fun <F : Fragment> navigateTo(
        fragmentClass: Class<F>,
        tag: String,
        args: Bundle? = null,
        allowStateLoss: Boolean = false,
        transactionSetup: (FragmentTransaction.() -> Unit)? = null,
    ) {
        // Don't navigate if the fragment is already the current one in the container.
        if (supportFragmentManager.findFragmentById(R.id.container)?.tag == tag) {
            return
        }

        commitFragmentTransaction(allowStateLoss) {
            transactionSetup?.invoke(this)
            replace(R.id.container, fragmentClass, args, tag)
        }
    }

    /** A reified version of the generic navigator for cleaner call sites. */
    private inline fun <reified F : Fragment> navigateTo(
        tag: String,
        args: Bundle? = null,
        allowStateLoss: Boolean = false,
        noinline transactionSetup: (FragmentTransaction.() -> Unit)? = null,
    ) = navigateTo(F::class.java, tag, args, allowStateLoss, transactionSetup)

    /**
     * A wrapper for fragment transactions that sets common options.
     *
     * This function simplifies committing fragment transactions by providing a centralized place to configure default
     * behaviors, such as enabling reordering. It uses the `commit` extension function from `androidx.fragment.app` for
     * safer and more concise transaction management.
     *
     * @param allowStateLoss Whether the transaction can be committed after the activity's state has been saved. Setting
     *   this to `true` can prevent crashes but may result in the loss of the fragment state if the activity is
     *   restored. Defaults to `false`.
     * @param block A lambda with a [FragmentTransaction] receiver, containing the specific operations for this
     *   transaction (e.g., `replace`, `add`, `remove`).
     */
    private fun commitFragmentTransaction(allowStateLoss: Boolean = false, block: FragmentTransaction.() -> Unit) {
        supportFragmentManager.commit(allowStateLoss) {
            setReorderingAllowed(true)
            block()
        }
    }
}
