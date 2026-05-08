/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import android.app.Activity
import android.content.Intent
import android.content.res.ColorStateList
import android.content.res.Resources
import android.view.Menu
import android.view.MenuInflater
import android.view.MenuItem
import android.view.ViewGroup
import android.view.WindowManager
import androidx.activity.result.ActivityResult
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.DimenRes
import androidx.annotation.IdRes
import androidx.annotation.StringRes
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.MenuHost
import androidx.core.view.MenuProvider
import androidx.fragment.app.Fragment
import androidx.lifecycle.Lifecycle
import androidx.navigation.NavController
import androidx.navigation.NavDirections
import androidx.navigation.NavOptions
import androidx.navigation.fragment.findNavController
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.support.utils.ext.isLandscape
import org.mozilla.fenix.NavHostActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.components.Components
import org.mozilla.fenix.components.toolbar.ToolbarContainerView
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.navigation.DefaultNavControllerProvider
import org.mozilla.fenix.navigation.NavControllerProvider
import org.mozilla.fenix.theme.ThemeManager
import org.mozilla.fenix.utils.isLargeScreenSize
import com.google.android.material.R as materialR
import mozilla.components.ui.icons.R as iconsR

/**
 * Get the requireComponents of this application.
 */
val Fragment.requireComponents: Components
    get() = requireContext().components

/**
 * Navigates from the given [Fragment] to a specified destination using the provided [NavDirections].
 *
 * @param id The ID of the current destination.
 * @param directions The [NavDirections] that define the navigation action and arguments.
 * @param options Optional [NavOptions] to customize the navigation behavior (e.g., animations, pop behavior).
 * @param navControllerProvider The [NavControllerProvider] used to retrieve the [NavController].
 * Defaults to [.DefaultNavControllerProvider].
 *
 * @throws IllegalStateException if the [NavController] cannot be found for the current [Fragment].
 */
fun Fragment.nav(
    @IdRes id: Int?,
    directions: NavDirections,
    options: NavOptions? = null,
    navControllerProvider: NavControllerProvider = DefaultNavControllerProvider(),
) {
    val navController = navControllerProvider.getNavController(this)
    navController.nav(id, directions, options)
}

fun Fragment.getPreferenceKey(
    @StringRes resourceId: Int,
): String = getString(resourceId)

/**
 * Displays the activity toolbar with the given [title].
 * Throws if the fragment is not attached to an [AppCompatActivity].
 */
fun Fragment.showToolbar(title: String) {
    (requireActivity() as AppCompatActivity).title = title
    activity?.setNavigationIcon(iconsR.drawable.mozac_ic_back_24)
    (activity as? NavHostActivity)?.getSupportActionBarAndInflateIfNecessary()?.show()
}

/**
 * Displays the activity toolbar with a given [title] and an icon button
 * with the given [iconResId] and [onClick]
 * Throws if the fragment is not attached to an [AppCompatActivity].
 *
 * @param title The title of the toolbar.
 * @param contentDescription The content description of the icon button for accessibility.
 * @param iconResId The resource ID of the icon to be displayed.
 * @param onClick The click event for the icon button.
 */
fun Fragment.showToolbarWithIconButton(
    title: String,
    contentDescription: String,
    iconResId: Int,
    onClick: () -> Unit,
) {
    val activity = requireActivity() as AppCompatActivity
    activity.title = title
    activity.setNavigationIcon(iconsR.drawable.mozac_ic_back_24)
    (activity as? NavHostActivity)?.getSupportActionBarAndInflateIfNecessary()?.show()

    val menuHost = activity as MenuHost
    val provider = object : MenuProvider {
        override fun onCreateMenu(menu: Menu, menuInflater: MenuInflater) {
            menu.clear()

            val item = menu.add(Menu.NONE, Menu.NONE, Menu.NONE, "")
            item.setIcon(iconResId)
            val colorResId = ThemeManager.resolveAttribute(materialR.attr.colorOnSurface, activity)
            item.iconTintList = ColorStateList.valueOf(activity.getColor(colorResId))
            item.contentDescription = contentDescription
            item.setShowAsAction(MenuItem.SHOW_AS_ACTION_IF_ROOM)
            item.setOnMenuItemClickListener {
                onClick()
                true
            }
        }

        override fun onMenuItemSelected(menuItem: MenuItem): Boolean = false
    }

    menuHost.addMenuProvider(provider, viewLifecycleOwner, Lifecycle.State.RESUMED)
}

/**
 * Run the [block] only if the [Fragment] is attached.
 *
 * @param block A callback to be executed if the container [Fragment] is attached.
 */
internal inline fun Fragment.runIfFragmentIsAttached(block: () -> Unit) {
    context?.let {
        block()
    }
}

/**
 * Hides the activity toolbar.
 * Throws if the fragment is not attached to an [AppCompatActivity].
 */
fun Fragment.hideToolbar() {
    (requireActivity() as AppCompatActivity).supportActionBar?.hide()
}

/**
 * Pops the backstack to force users to re-auth if they put the app in the background and return to
 * it while being inside a secured flow (e.g. logins or credit cards).
 *
 * Does nothing if the user is currently navigating to any of the [destinations] given as a
 * parameter.
 */
fun Fragment.redirectToReAuth(
    destinations: List<Int>,
    currentDestination: Int?,
    currentLocation: Int,
) {
    if (currentDestination !in destinations) {
        // Workaround for memory leak caused by Android SDK bug
        // https://issuetracker.google.com/issues/37125819
        activity?.invalidateOptionsMenu()
        when (currentLocation) {
            R.id.creditCardEditorFragment,
            R.id.creditCardsManagementFragment,
            -> {
                findNavController().popBackStack(R.id.autofillSettingFragment, false)
            }
        }
    }
}

fun Fragment.breadcrumb(
    message: String,
    data: Map<String, String> = emptyMap(),
) {
    val activityName = activity?.let { it::class.java.simpleName } ?: "null"

    requireComponents.analytics.crashReporter.recordCrashBreadcrumb(
        Breadcrumb(
            category = this::class.java.simpleName,
            message = message,
            data = data + mapOf(
                "instance" to hashCode().toString(),
                "activityInstance" to activity?.hashCode().toString(),
                "activityName" to activityName,
            ),
            level = Breadcrumb.Level.INFO,
        ),
    )
}

/**
 * Sets the [WindowManager.LayoutParams.FLAG_SECURE] flag for the current activity window.
 *
 * When user preference allowScreenCaptureInSecureScreens is true, this function is a no-op
 */
fun Fragment.secure() {
    if (context?.settings()?.allowScreenCaptureInSecureScreens != true) {
        this.activity?.window?.addFlags(
            WindowManager.LayoutParams.FLAG_SECURE,
        )
    }
}

/**
 * Clears the [WindowManager.LayoutParams.FLAG_SECURE] flag for the current activity window.
 */
fun Fragment.removeSecure() {
    this.activity?.window?.clearFlags(
        WindowManager.LayoutParams.FLAG_SECURE,
    )
}

/**
 * Register a request to start an activity for result.
 */
fun Fragment.registerForActivityResult(
    onFailure: (result: ActivityResult) -> Unit = {},
    onSuccess: (result: ActivityResult) -> Unit,
): ActivityResultLauncher<Intent> {
    return registerForActivityResult(ActivityResultContracts.StartActivityForResult()) { result ->
        if (result.resultCode == Activity.RESULT_OK) {
            onSuccess(result)
        } else {
            onFailure(result)
        }
    }
}

/**
 *  Checks whether the current fragment is running on a large window.
 */
fun Fragment.isLargeWindow(): Boolean {
    return requireContext().isLargeWindow()
}

/**
 * Checks whether the current fragment is running on a tablet.
 */
fun Fragment.isLargeScreenSize(): Boolean {
    return requireContext().isLargeScreenSize()
}

/**
 * Checks whether the app's current window height is at least more than [TALL_SCREEN_HEIGHT_DP].
 */
fun Fragment.isTallWindow(): Boolean {
    return requireContext().isTallWindow()
}

/**
 * Checks whether the app's current window width is at least more than [WIDE_SCREEN_WIDTH_DP].
 */
fun Fragment.isWideWindow(): Boolean {
    return requireContext().isWideWindow()
}

/**
 * Returns the height of the bottom toolbar.
 *
 * The bottom toolbar can consist of:
 *  - a combination of address bar, navigation bar & a microsurvey.
 *  - be absent.
 *
 * @param includeNavBarIfEnabled If true and the navigation bar feature is enabled it's height
 * will be included in the calculation.
 */
fun Fragment.getBottomToolbarHeight(includeNavBarIfEnabled: Boolean = true): Int {
    val settings = requireComponents.settings

    val isMicrosurveyEnabled = settings.shouldShowMicrosurveyPrompt
    val isToolbarAtBottom = settings.toolbarPosition == ToolbarPosition.BOTTOM
    val isNavBarEnabled = settings.shouldUseExpandedToolbar && isTallWindow() && !isWideWindow()

    val microsurveyHeight = if (isMicrosurveyEnabled) {
        pixelSizeFor(R.dimen.browser_microsurvey_height)
    } else {
        0
    }

    val toolbarHeight = if (isToolbarAtBottom) {
        settings.browserToolbarHeight
    } else {
        0
    }

    val navBarHeight =
        if (includeNavBarIfEnabled && isNavBarEnabled) {
        pixelSizeFor(
            if (isToolbarAtBottom) {
                R.dimen.browser_navbar_height_small
            } else {
                R.dimen.browser_navbar_height
            },
        )
    } else {
        0
    }

    return microsurveyHeight + toolbarHeight + navBarHeight
}

/**
 * Returns the height of the top toolbar.
 *
 * @param includeTabStripIfAvailable If true and the tab strip feature is enabled it's height
 * will be included in the calculation.
 */
fun Fragment.getTopToolbarHeight(includeTabStripIfAvailable: Boolean = true): Int {
    val settings = requireComponents.settings
    val isToolbarAtTop = settings.toolbarPosition == ToolbarPosition.TOP
    val toolbarHeight = settings.browserToolbarHeight

    return if (includeTabStripIfAvailable && settings.isTabStripEnabled) {
        toolbarHeight + pixelSizeFor(R.dimen.tab_strip_height)
    } else if (isToolbarAtTop) {
        toolbarHeight
    } else {
        0
    }
}

/**
 *
 * Manages the state of the microsurvey prompt on orientation change.
 *
 * @param parent The top level [ViewGroup] of the fragment, which will be hosting the [bottomToolbarContainerView].
 * @param bottomToolbarContainerView The [ToolbarContainerView] hosting the microsurvey prompt.
 * @param reinitializeMicrosurveyPrompt lambda for re-initializing the microsurvey prompt inside the host [Fragment].
 */
fun Fragment.updateMicrosurveyPromptForConfigurationChange(
    parent: ViewGroup,
    bottomToolbarContainerView: ToolbarContainerView?,
    reinitializeMicrosurveyPrompt: () -> Unit,
) {
    if (!requireContext().isLandscape()) {
        // Already having a bottomContainer after switching back to portrait mode will happen when address bar is
        // positioned at bottom and also as an edge case if configurationChange is called after onCreateView with the
        // same orientation. Observed on a foldable emulator while going from single screen portrait mode to landscape
        // tablet, back and forth.
        bottomToolbarContainerView?.let {
            parent.removeView(it)
        }

        reinitializeMicrosurveyPrompt()
    }
}

/**
 * Returns the pixel size for the given dimension resource ID.
 *
 * This is a wrapper around [Resources.getDimensionPixelSize], reducing verbosity when accessing
 * dimension values from a [Fragment].
 *
 * @param resId Resource ID of the dimension.
 * @return The pixel size corresponding to the given dimension resource.
 */
fun Fragment.pixelSizeFor(
    @DimenRes resId: Int,
) = resources.getDimensionPixelSize(resId)

/**
 * Opens a [url] in a new tab and navigates to the browser fragment.
 */
fun Fragment.openInNewTab(url: String) {
    requireComponents.useCases.tabsUseCases.addTab(url)
    findNavController().navigate(R.id.browserFragment)
}
