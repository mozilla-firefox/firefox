/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import android.app.Dialog
import android.content.res.Configuration
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.view.KeyEvent
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.view.animation.OvershootInterpolator
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CornerSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.hapticfeedback.HapticFeedback
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.AwaitPointerEventScope
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.PointerEventTimeoutCancellationException
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.PointerInputScope
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.graphics.drawable.toDrawable
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat.Type.systemBars
import androidx.fragment.compose.content
import androidx.lifecycle.coroutineScope
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavOptions
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import com.google.android.material.R as materialR
import com.google.android.material.bottomsheet.BottomSheetBehavior
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.bottomsheet.BottomSheetDialogFragment
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlin.getValue
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.Menu
import mozilla.components.compose.menu.data.MenuAttribution
import mozilla.components.compose.menu.store.MenuState
import mozilla.components.compose.menu.store.MenuStore
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.feature.automotive.isAndroidAutomotiveAvailable
import mozilla.components.lib.state.helpers.StoreProvider.Companion.composableStore
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.ktx.android.util.dpToPx
import mozilla.components.support.utils.ext.getWindowInsets
import mozilla.components.support.utils.ext.isLandscape
import mozilla.components.support.utils.ext.pixelSizeFor
import mozilla.components.support.utils.ext.top
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Events
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.compose.CustomizeMenuPopup
import org.mozilla.fenix.components.menu.compose.MenuDialogBottomSheet
import org.mozilla.fenix.components.menu.compose.MenuHandleState
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.configuration.CustomTabMenuConfiguration
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration
import org.mozilla.fenix.components.menu.middleware.MenuMiddleware
import org.mozilla.fenix.components.menu.middleware.MenuTelemetryMiddleware
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isToolbarAtBottom
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.openSetDefaultBrowserOption
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ipprotection.ui.IPProtectionSnackbarBinding
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.translations.TranslationsEnabledSettings

private const val EXPANDED_OFFSET = 56
private const val HIDING_FRICTION = 0.9f
private const val MENU_ANIMATION_DURATION = 300L
private const val MENU_ANIMATION_START_OFFSET_RATIO = 0.2f
private const val PRIVATE_HOME_MENU_BACKGROUND_ALPHA = 100

/** A bottom sheet fragment hosting the customizable menu. */
class MenuFragment : BottomSheetDialogFragment() {
    private val args by navArgs<MenuFragmentArgs>()
    private val snackbarHostState = SnackbarHostState()
    private var bottomSheetBehavior: BottomSheetBehavior<View>? = null

    private val ipProtectionSnackbarBinding = ViewBoundFeatureWrapper<IPProtectionSnackbarBinding>()

    override fun onCreateDialog(savedInstanceState: Bundle?): Dialog {
        Events.toolbarMenuVisible.record(NoExtras())

        return object : BottomSheetDialog(requireContext(), theme) {
                override fun onKeyDown(
                    keyCode: Int,
                    event: KeyEvent,
                ): Boolean {
                    if (event.action == KeyEvent.ACTION_DOWN && event.keyCode == KeyEvent.KEYCODE_MENU) {
                        // Repeats from a held down hardware key would otherwise keep dismissing
                        // and reopening the menu.
                        if (event.repeatCount == 0) {
                            dismiss()
                        }
                        return true
                    }
                    return super.onKeyDown(keyCode, event)
                }
            }
            .apply {
                setOnShowListener {
                    val safeActivity = activity ?: return@setOnShowListener
                    val isPrivate = safeActivity.components.appStore.state.mode.isPrivate

                    if (isPrivate && args.accessPoint == MenuAccessPoint.Home) {
                        window?.setBackgroundDrawable(
                            Color.BLACK.toDrawable().mutate().apply {
                                alpha = PRIVATE_HOME_MENU_BACKGROUND_ALPHA
                            }
                        )
                    }

                    val bottomSheet = findViewById<View?>(materialR.id.design_bottom_sheet)

                    bottomSheet?.let {
                        ViewCompat.setOnApplyWindowInsetsListener(it) { view, insets ->
                            val systemBarInsets = insets.getInsets(systemBars())
                            view.setPadding(0, systemBarInsets.top, 0, systemBarInsets.bottom)
                            insets
                        }
                    }
                    bottomSheet?.setBackgroundResource(R.drawable.bottom_sheet_with_top_rounded_corners)

                    // https://bugzilla.mozilla.org/show_bug.cgi?id=1982004
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.VANILLA_ICE_CREAM) {
                        bottomSheet?.let { sheet ->
                            sheet.translationY = sheet.height * MENU_ANIMATION_START_OFFSET_RATIO
                            sheet
                                .animate()
                                .translationY(0f)
                                .setInterpolator(OvershootInterpolator())
                                .setDuration(MENU_ANIMATION_DURATION)
                                .start()
                        }
                    }

                    bottomSheetBehavior = bottomSheet?.let {
                        BottomSheetBehavior.from(it).apply {
                            maxWidth = calculateMenuSheetWidth()
                            peekHeight = resources.displayMetrics.heightPixels
                            maxHeight = calculateMenuSheetHeight()
                            skipCollapsed = true
                            state = BottomSheetBehavior.STATE_EXPANDED
                            hideFriction = HIDING_FRICTION
                        }
                    }
                }
            }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        bottomSheetBehavior?.apply {
            maxWidth = calculateMenuSheetWidth()
            maxHeight = calculateMenuSheetHeight()
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        val context = requireContext()

        val menuStore by
            composableStore(
                MenuState(
                    menuGroups = emptyList(),
                    attribution =
                        if (args.accessPoint == MenuAccessPoint.External) {
                            MenuAttribution(
                                title =
                                    Text.Resource(
                                        R.string.browser_menu_powered_by2,
                                        listOf(context.getString(R.string.app_name)),
                                    ),
                                icon = MenuItemIconRes(R.drawable.ic_firefox),
                                showAtTop = context.isToolbarAtBottom(),
                            )
                        } else {
                            null
                        },
                )
            ) {
                buildMenuStore(it)
            }

        MenuFragmentContent(menuStore = menuStore)
    }

    @Composable
    private fun MenuFragmentContent(menuStore: MenuStore) {
        val context = requireContext()
        val haptics = LocalHapticFeedback.current
        val isMenuCustomizationAllowed = remember {
            args.accessPoint == MenuAccessPoint.Browser &&
                context.components.settings.isMenuCustomizationEnabled &&
                context.components.settings.isMenuCustomizationScreenEnabled
        }

        FirefoxTheme {
            BackHandler { dismissAllowingStateLoss() }

            var isHeaderDark by remember { mutableStateOf(false) }
            var showCustomizeMenuPopup by remember { mutableStateOf(false) }
            var popupOffset by remember { mutableStateOf<Offset?>(null) }

            Box(
                modifier =
                    Modifier.then(
                        if (isMenuCustomizationAllowed) {
                            Modifier.pointerInput(Unit) {
                                detectLongPress(haptics) { offset ->
                                    popupOffset = offset
                                    showCustomizeMenuPopup = true
                                }
                            }
                        } else {
                            Modifier
                        }
                    )
            ) {
                MenuDialogBottomSheet(
                    modifier =
                        Modifier.padding(vertical = FirefoxTheme.layout.space.static200)
                            .width(FirefoxTheme.layout.space.static400),
                    onRequestDismiss = ::dismiss,
                    menuHandleState =
                        MenuHandleState(
                            contentDescription =
                                stringResource(
                                    when (args.accessPoint) {
                                        MenuAccessPoint.External ->
                                            R.string.browser_custom_tab_menu_handlebar_content_description
                                        else -> R.string.browser_close_main_menu_handlebar_content_description
                                    }
                                ),
                            useDarkBackground = isHeaderDark,
                        ),
                    snackbarHostState = snackbarHostState,
                    cornerShape =
                        MaterialTheme.shapes.extraLarge.copy(
                            bottomStart = CornerSize(0.dp),
                            bottomEnd = CornerSize(0.dp),
                        ),
                ) {
                    Menu(
                        store = menuStore,
                        modifier = Modifier.testTag(MenuTestTag.MAIN_MENU),
                        onHeaderDarkBackgroundChanged = { isHeaderDark = it },
                    )
                }

                CustomizeMenuPopupContainer(
                    popupOffset = popupOffset,
                    showPopup = showCustomizeMenuPopup,
                    onDismiss = { showCustomizeMenuPopup = false },
                    onCustomizeMenuClick = {
                        showCustomizeMenuPopup = false
                        nav(
                            id = R.id.menuFragment,
                            directions = MenuFragmentDirections.actionMenuFragmentToCustomizeMenuFragment(),
                            options = NavOptions.Builder().setPopUpTo(R.id.menuFragment, true).build(),
                        )
                    },
                )
            }
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        ipProtectionSnackbarBinding.set(
            feature =
                IPProtectionSnackbarBinding(
                    appStore = requireComponents.appStore,
                    context = requireContext(),
                    navController = findNavController(),
                    snackbarDelegate =
                        FenixSnackbarDelegate(
                            snackbarHostState = snackbarHostState,
                            scope = viewLifecycleOwner.lifecycleScope,
                            context = requireContext(),
                        ),
                ),
            owner = this,
            view = view,
        )
    }

    private fun calculateMenuSheetWidth(): Int {
        val isLandscape = requireContext().isLandscape()
        val screenWidthPx = requireContext().resources.configuration.screenWidthDp.dpToPx(resources.displayMetrics)
        val totalHorizontalPadding = 2 * pixelSizeFor(R.dimen.browser_menu_padding)
        val minScreenWidth = pixelSizeFor(R.dimen.browser_menu_max_width) + totalHorizontalPadding

        // We only want to restrict the width of the menu if the device is in landscape mode AND the
        // device's screen width is smaller than the menu's max width and total horizontal padding combined.
        // Otherwise, the menu being at max width would still leave sufficient padding on each side in landscape mode.
        return if (isLandscape && screenWidthPx < minScreenWidth) {
            screenWidthPx - totalHorizontalPadding
        } else {
            pixelSizeFor(R.dimen.browser_menu_max_width)
        }
    }

    private fun calculateMenuSheetHeight(): Int {
        val bottomSheet = dialog?.findViewById<View?>(materialR.id.design_bottom_sheet)
        val topBarHeight = bottomSheet?.getWindowInsets()?.top() ?: 0

        val orientationMaxHeight =
            if (requireContext().isLandscape()) {
                resources.displayMetrics.heightPixels
            } else {
                resources.displayMetrics.heightPixels - EXPANDED_OFFSET.dpToPx(resources.displayMetrics)
            }

        return orientationMaxHeight - topBarHeight
    }

    private fun buildMenuItemProvidersFactory(): MenuItemProvidersFactory {
        val components = requireComponents
        val context = requireContext()
        val activity = requireActivity()

        return MenuItemProvidersFactory(
            context = context,
            accessPoint = args.accessPoint,
            target =
                when (args.accessPoint) {
                    MenuAccessPoint.Home -> MenuTarget.Home
                    MenuAccessPoint.Browser -> MenuTarget.BrowserTab
                    MenuAccessPoint.External ->
                        args.customTabSessionId?.let { MenuTarget.CustomTab(it, args.isSandboxCustomTab) }
                            ?: MenuTarget.BrowserTab
                },
            browserStore = components.core.store,
            appStore = components.appStore,
            ipProtectionStore = components.ipProtection.store,
            syncStore = components.backgroundServices.syncStore,
            httpClient = components.core.client,
            historyStorage = components.core.historyStorage,
            bookmarksStorage = components.core.bookmarksStorage,
            pinnedSiteStorage = components.core.pinnedSiteStorage,
            tabCollectionStorage = components.core.tabCollectionStorage,
            permissionStorage = components.core.permissionStorage,
            useCases = components.useCases,
            addonManager = components.addonManager,
            engine = components.core.engine,
            settings = components.settings,
            summarizeFeatureSettings = components.core.summarizeFeatureSettings,
            summarizeEligibilityChecker = components.core.summarizationEligibilityChecker,
            translationsSettings = TranslationsEnabledSettings.dataStore(context),
            isAndroidAutomotiveAvailable = context.isAndroidAutomotiveAvailable(),
            materialAlertDialogBuilder = MaterialAlertDialogBuilder(context),
            quitApplicationDelegate = activity::finishAndRemoveTask,
            setAsDefaultBrowserDelegate = activity::openSetDefaultBrowserOption,
            menuViewScope = viewLifecycleOwner.lifecycle.coroutineScope,
            applicationScope = components.applicationScope,
        )
    }

    private fun buildMenuStore(initialState: MenuState): MenuStore {
        val menuItemsRegistry = buildMenuItemsRegistry()

        return MenuStore(
            initialState = initialState,
            middleware =
                listOf(
                    MenuMiddleware(
                        menuBuilder = MenuBuilder(menuItemsRegistry),
                        eventRouter =
                            MenuItemEventRouter(
                                registry = menuItemsRegistry,
                                host = DefaultMenuHost(findNavController()),
                            ),
                        scope = viewLifecycleOwner.lifecycle.coroutineScope,
                    ),
                    MenuTelemetryMiddleware(accessPoint = args.accessPoint),
                ),
        )
    }

    private fun buildMenuItemsRegistry() =
        MenuItemsRegistry(
            configuration = buildMenuConfigurationFor(args.accessPoint),
            resolver = buildMenuItemProvidersFactory()::buildProviderFor,
        )

    private fun buildMenuConfigurationFor(menuAccessPoint: MenuAccessPoint) =
        when (menuAccessPoint) {
            MenuAccessPoint.Browser ->
                BrowserMenuConfiguration.sections(
                    isToolbarAtBottom = requireContext().isToolbarAtBottom(),
                    isExpandedToolbarEnabled = requireComponents.settings.shouldUseExpandedToolbar,
                    customizationInfo = requireComponents.menuCustomizationRepository.customizedMenuInfo.value,
                )
            MenuAccessPoint.Home -> HomeMenuConfiguration.sections()
            MenuAccessPoint.External ->
                CustomTabMenuConfiguration.sections(
                    isToolbarAtBottom = requireContext().isToolbarAtBottom(),
                    shouldShowExtensions = requireComponents.settings.shouldShowCustomTabExtensions,
                )
        }
}

@Composable
private fun CustomizeMenuPopupContainer(
    popupOffset: Offset?,
    showPopup: Boolean,
    onDismiss: () -> Unit,
    onCustomizeMenuClick: () -> Unit,
) {
    if (showPopup) {
        CustomizeMenuPopup(
            popupOffset = popupOffset,
            onDismissRequest = onDismiss,
            onCustomizeMenuClick = onCustomizeMenuClick,
        )
    }
}

private suspend fun PointerInputScope.detectLongPress(
    haptics: HapticFeedback,
    onLongPress: (Offset) -> Unit,
) {
    awaitEachGesture {
        val down = awaitFirstDown(requireUnconsumed = false, pass = PointerEventPass.Initial)
        val startPosition = down.position

        if (awaitLongPressOrCancellation(down.id, startPosition)) {
            haptics.performHapticFeedback(HapticFeedbackType.LongPress)
            onLongPress(startPosition)
            consumeRemainingPointerEvents()
        }
    }
}

private suspend fun AwaitPointerEventScope.awaitLongPressOrCancellation(
    pointerId: PointerId,
    startPosition: Offset,
): Boolean {
    return try {
        withTimeout(viewConfiguration.longPressTimeoutMillis) {
            while (true) {
                val event = awaitPointerEvent(PointerEventPass.Initial)
                val pointerChange = event.changes.firstOrNull { it.id == pointerId } ?: event.changes.firstOrNull()

                if (pointerChange == null || !pointerChange.pressed) {
                    return@withTimeout false
                }

                val dx = pointerChange.position.x - startPosition.x
                val dy = pointerChange.position.y - startPosition.y
                val touchSlop = viewConfiguration.touchSlop
                if (dx * dx + dy * dy > touchSlop * touchSlop) {
                    return@withTimeout false
                }
            }
        }
        false
    } catch (_: PointerEventTimeoutCancellationException) {
        true
    }
}

private suspend fun AwaitPointerEventScope.consumeRemainingPointerEvents() {
    do {
        val event = awaitPointerEvent(PointerEventPass.Initial)
        event.changes.forEach { it.consume() }
    } while (event.changes.any { it.pressed })
}
