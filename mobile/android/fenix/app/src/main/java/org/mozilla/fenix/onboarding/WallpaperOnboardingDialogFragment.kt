/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding

import android.app.Dialog
import android.content.DialogInterface
import android.content.pm.ActivityInfo
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.runtime.rememberCoroutineScope
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import kotlinx.coroutines.launch
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Wallpapers
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.compose.core.Action
import org.mozilla.fenix.compose.snackbar.Snackbar
import org.mozilla.fenix.compose.snackbar.SnackbarState
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.settings.wallpaper.getWallpapersForOnboarding
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.wallpapers.Wallpaper
import org.mozilla.fenix.wallpapers.WallpaperOnboarding

/** Dialog displaying the wallpapers onboarding. */
class WallpaperOnboardingDialogFragment : BottomSheetDialogFragment() {
    private val appStore by lazy {
        requireComponents.appStore
    }

    private val wallpaperUseCases by lazy {
        requireComponents.useCases.wallpaperUseCases
    }

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog =
        (super.onCreateDialog(savedInstanceState) as BottomSheetDialog).apply {
            setOnShowListener {
                behavior.state = BottomSheetBehavior.STATE_EXPANDED
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setStyle(STYLE_NO_TITLE, R.style.WallpaperOnboardingDialogStyle)
        activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
    }

    override fun onDestroy() {
        super.onDestroy()
        activity?.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
    }

    override fun onDismiss(dialog: DialogInterface) {
        super.onDismiss(dialog)

        val currentWallpaper = requireContext().components.appStore.state.wallpaperState.currentWallpaper
        Wallpapers.onboardingClosed.record(
            Wallpapers.OnboardingClosedExtra(isSelected = currentWallpaper.name != Wallpaper.SOLID)
        )
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        requireComponents.settings.showWallpaperOnboarding = false
        Wallpapers.onboardingOpened.record(NoExtras())
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        dialog?.setCanceledOnTouchOutside(true)
        return content {
            FirefoxTheme {
                val wallpapers =
                    appStore
                        .observeAsComposableState { state ->
                            state.wallpaperState.availableWallpapers.getWallpapersForOnboarding()
                        }
                        .value
                val currentWallpaper =
                    appStore
                        .observeAsComposableState { state ->
                            state.wallpaperState.currentWallpaper
                        }
                        .value

                val coroutineScope = rememberCoroutineScope()

                WallpaperOnboarding(
                    wallpapers = wallpapers,
                    currentWallpaper = currentWallpaper,
                    onCloseClicked = { dismiss() },
                    onExploreMoreButtonClicked = {
                        val directions = NavGraphDirections.actionGlobalWallpaperSettingsFragment()
                        findNavController().navigate(directions)
                        Wallpapers.onboardingExploreMoreClick.record(NoExtras())
                    },
                    loadWallpaperResource = { wallpaper, size -> wallpaperUseCases.loadThumbnail(wallpaper, size) },
                    onSelectWallpaper = {
                        coroutineScope.launch {
                            val result = wallpaperUseCases.selectWallpaper(it)
                            onWallpaperSelected(it, result, this@WallpaperOnboardingDialogFragment.requireView())
                        }
                    },
                )
            }
        }
    }

    private fun onWallpaperSelected(
        wallpaper: Wallpaper,
        result: Wallpaper.ImageFileState,
        view: View,
    ) {
        when (result) {
            Wallpaper.ImageFileState.Downloaded -> {
                Wallpapers.wallpaperSelected.record(
                    Wallpapers.WallpaperSelectedExtra(
                        name = wallpaper.name,
                        source = "onboarding",
                        themeCollection = wallpaper.collection.name,
                    )
                )
            }
            Wallpaper.ImageFileState.Error -> {
                Snackbar.make(
                        snackBarParentView = view,
                        snackbarState =
                            SnackbarState(
                                message = getString(R.string.wallpaper_download_error_snackbar_message),
                                action =
                                    Action(
                                        label = getString(R.string.wallpaper_download_error_snackbar_action),
                                        onClick = {
                                            viewLifecycleOwner.lifecycleScope.launch {
                                                val retryResult = wallpaperUseCases.selectWallpaper(wallpaper)
                                                onWallpaperSelected(wallpaper, retryResult, view)
                                            }
                                        },
                                    ),
                            ),
                    )
                    .show()
            }
            else -> {
                /* noop */
            }
        }
    }

    companion object {
        // The number of wallpaper thumbnails to display.
        const val THUMBNAILS_SELECTION_COUNT = 6

        // The desired amount of seasonal wallpapers inside of the selector.
        const val SEASONAL_WALLPAPERS_COUNT = 3
    }
}
