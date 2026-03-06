/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.customtabs

import android.app.PendingIntent
import android.graphics.Bitmap
import android.graphics.drawable.Drawable
import android.util.Size
import android.view.Window
import androidx.annotation.ColorInt
import androidx.annotation.VisibleForTesting
import androidx.appcompat.app.AppCompatDelegate.MODE_NIGHT_FOLLOW_SYSTEM
import androidx.appcompat.app.AppCompatDelegate.NightMode
import androidx.appcompat.content.res.AppCompatResources.getDrawable
import androidx.core.graphics.drawable.toDrawable
import androidx.core.graphics.scale
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.mapNotNull
import mozilla.components.browser.menu.BrowserMenuBuilder
import mozilla.components.browser.state.selector.findCustomTab
import mozilla.components.browser.state.state.CustomTabActionButtonConfig
import mozilla.components.browser.state.state.CustomTabConfig
import mozilla.components.browser.state.state.CustomTabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.browser.toolbar.BrowserToolbar
import mozilla.components.concept.toolbar.Toolbar
import mozilla.components.feature.customtabs.feature.CustomTabSessionTitleObserver
import mozilla.components.feature.customtabs.menu.sendWithUrl
import mozilla.components.feature.tabs.CustomTabsUseCases
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.base.feature.UserInteractionHandler
import mozilla.components.support.ktx.android.content.share
import mozilla.components.support.ktx.android.util.dpToPx
import mozilla.components.support.ktx.android.view.setSystemBarsBackground
import mozilla.components.support.ktx.kotlinx.coroutines.flow.ifAnyChanged
import mozilla.components.support.utils.ext.resizeMaintainingAspectRatio
import mozilla.components.ui.icons.R as iconsR

// The menu button must have the largest weight out of all toolbar items to ensure that it is right-aligned.
private const val MENU_WEIGHT = Int.MAX_VALUE

/**
 * Initializes and resets the [BrowserToolbar] for a Custom Tab based on the [CustomTabConfig].
 *
 * @property store The given [BrowserStore] to use.
 * @property toolbar Reference to the [BrowserToolbar], so that the color and menu items can be set.
 * @property sessionId ID of the custom tab session. No-op if null or invalid.
 * @property useCases The given [CustomTabsUseCases] to use.
 * @property menuBuilder [BrowserMenuBuilder] reference to pull menu options from.
 * @property menuItemIndex Location to insert any custom menu options into the predefined menu list.
 * @property window Reference to the [Window] so the navigation bar color can be set.
 * @property appNightMode The [NightMode] used in the app. Defaults to [MODE_NIGHT_FOLLOW_SYSTEM].
 * @property forceActionButtonTinting When set to true the [toolbar] action button will always be tinted
 * based on the [toolbar] background, ignoring the value of [CustomTabActionButtonConfig.tint].
 * @property customTabsToolbarButtonConfig Holds button configurations for the toolbar.
 * @property customTabsColorsConfig Contains the color configurations for styling the application and system UI.
 * @property customTabsToolbarListeners Holds click listeners for buttons on the toolbar.
 * @property closeListener Invoked when the close button is pressed.
 */
@Suppress("LargeClass")
class CustomTabsToolbarFeature(
    private val store: BrowserStore,
    private val toolbar: BrowserToolbar,
    private val sessionId: String? = null,
    private val useCases: CustomTabsUseCases,
    private val menuBuilder: BrowserMenuBuilder? = null,
    private val menuItemIndex: Int = menuBuilder?.items?.size ?: 0,
    private val window: Window? = null,
    @param:NightMode private val appNightMode: Int = MODE_NIGHT_FOLLOW_SYSTEM,
    private val forceActionButtonTinting: Boolean = false,
    private val customTabsToolbarButtonConfig: CustomTabsToolbarButtonConfig =
        CustomTabsToolbarButtonConfig(),
    private val customTabsColorsConfig: CustomTabsColorsConfig = CustomTabsColorsConfig(),
    private val customTabsToolbarListeners: CustomTabsToolbarListeners = CustomTabsToolbarListeners(),
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    private val closeListener: () -> Unit,
) : LifecycleAwareFeature, UserInteractionHandler {
    private var initialized: Boolean = false
    private val titleObserver = CustomTabSessionTitleObserver(toolbar)
    private val context get() = toolbar.context
    private var scope: CoroutineScope? = null
    private var menuButton: Toolbar.ActionButton? = null
    private var menuDrawableIcon: Drawable? = null

    /**
     * Gets the current custom tab session.
     */
    private val session: CustomTabSessionState?
        get() = sessionId?.let { store.state.findCustomTab(it) }

    @ColorInt
    private val fallbackIconColor: Int = toolbar.display.colors.menu

    @ColorInt
    var iconColor: Int = fallbackIconColor

    /**
     * Initializes the feature and registers the [CustomTabSessionTitleObserver].
     */
    override fun start() {
        val tabId = sessionId ?: return
        val tab = store.state.findCustomTab(tabId) ?: return

        scope = store.flowScoped(dispatcher = mainDispatcher) { flow ->
            flow
                .mapNotNull { state -> state.findCustomTab(tabId) }
                .ifAnyChanged { tab -> arrayOf(tab.content.title, tab.content.url) }
                .collect { tab -> titleObserver.onTab(tab) }
        }

        if (!initialized) {
            initialized = true
            init(tab.config)
        }
    }

    /**
     * Unregisters the [CustomTabSessionTitleObserver].
     */
    override fun stop() {
        scope?.cancel()
    }

    @VisibleForTesting
    internal fun init(config: CustomTabConfig) {
        // Don't allow clickable toolbar so a custom tab can't switch to edit mode.
        toolbar.display.onUrlClicked = { false }

        val colorSchemeParams = config.getConfiguredColorSchemeParams(
            currentNightMode = context.resources.configuration.uiMode,
            preferredNightMode = appNightMode,
        )

        val readableColor = colorSchemeParams.getToolbarContrastColor(
            context = context,
            shouldUpdateTheme = customTabsColorsConfig.updateToolbarsColor,
            fallbackColor = fallbackIconColor,
        ).also {
            iconColor = it
        }

        if (customTabsColorsConfig.isAnyColorUpdateAllowed()) {
            colorSchemeParams.let {
                updateTheme(
                    toolbarColor = it?.toolbarColor,
                    navigationBarColor = it?.navigationBarColor ?: it?.toolbarColor,
                    navigationBarDividerColor = it?.navigationBarDividerColor,
                    readableColor = readableColor,
                )
            }
        }

        // Add navigation close action
        if (config.showCloseButton) {
            val closeIcon = when {
                customTabsToolbarButtonConfig.allowCustomizingCloseButton -> config.closeButtonIcon
                else -> null
            }
            addCloseButton(readableColor, closeIcon)
        }

        // Add action button
        addActionButton(readableColor, config.actionButtonConfig)

        // Show share button
        if (config.showShareMenuItem) {
            //addShareButton(readableColor)
        }

        if (customTabsToolbarButtonConfig.showRefreshButton &&
            customTabsToolbarListeners.refreshListener != null
        ) {
            addRefreshButton(readableColor)
        }

        // Add menu items
        if (config.menuItems.isNotEmpty() || menuBuilder?.items?.isNotEmpty() == true) {
            addMenuItems()
        }

        menuDrawableIcon = getDrawable(context, iconsR.drawable.mozac_ic_ellipsis_vertical_24)
        //menuDrawableIcon?.setTint(readableColor)

        if (customTabsToolbarButtonConfig.showMenu && isMenuAvailable()) {
            addMenuButton()
        } else if (!customTabsToolbarButtonConfig.showMenu) {
            toolbar.display.hideMenuButton()
        }
    }

    @VisibleForTesting
    internal fun updateTheme(
        @ColorInt toolbarColor: Int? = null,
        @ColorInt navigationBarColor: Int? = null,
        @ColorInt navigationBarDividerColor: Int? = null,
        @ColorInt readableColor: Int,
    ) {
        if (customTabsColorsConfig.updateToolbarsColor && toolbarColor != null) {
            toolbar.setBackgroundColor(toolbarColor)

            toolbar.display.colors = toolbar.display.colors.copy(
                text = readableColor,
                title = readableColor,
                siteInfoIconSecure = readableColor,
                siteInfoIconInsecure = readableColor,
                trackingProtection = readableColor,
                menu = readableColor,
            )
        }

        window?.setSystemBarsBackground(
            statusBarColor = when (customTabsColorsConfig.updateStatusBarColor) {
                true -> toolbarColor
                false -> null
            },
            navigationBarColor = when (customTabsColorsConfig.updateSystemNavigationBarColor) {
                true -> navigationBarColor
                false -> null
            },
            navigationBarDividerColor = when (customTabsColorsConfig.updateSystemNavigationBarColor) {
                true -> navigationBarDividerColor
                false -> null
            },
        )
    }

    /**
     * Display a close button at the start of the toolbar.
     * When clicked, it calls [closeListener].
     */
    @VisibleForTesting
    internal fun addCloseButton(
        @ColorInt readableColor: Int,
        bitmap: Bitmap?,
    ) {
        val drawableIcon = bitmap?.toDrawable(context.resources)
            ?: getDrawable(context, iconsR.drawable.mozac_ic_cross_24)!!.mutate()

        //drawableIcon.setTint(readableColor)

        val button = Toolbar.ActionButton(
            drawableIcon,
            context.getString(R.string.mozac_feature_customtabs_exit_button),
        ) {
            emitCloseFact()
            session?.let {
                useCases.remove(it.id)
            }
            closeListener.invoke()
        }
        toolbar.addNavigationAction(button)
    }

    /**
     * Display an action button from the custom tab config on the toolbar.
     * When clicked, it activates the corresponding [PendingIntent].
     */
    @VisibleForTesting
    internal fun addActionButton(
        @ColorInt readableColor: Int,
        buttonConfig: CustomTabActionButtonConfig?,
    ) {
        buttonConfig?.let { config ->
            val icon = config.icon
            val scaledIconSize = icon.resizeMaintainingAspectRatio(ACTION_BUTTON_MAX_DRAWABLE_DP_SIZE)
            val drawableIcon = icon
                .scale(
                    scaledIconSize.width.dpToPx(context.resources.displayMetrics),
                    scaledIconSize.height.dpToPx(context.resources.displayMetrics),
                    filter = true,
                )
                .toDrawable(context.resources)

            if (config.tint || forceActionButtonTinting) {
                //drawableIcon.setTint(readableColor)
            }

            val button = Toolbar.ActionButton(
                drawableIcon,
                config.description,
            ) {
                emitActionButtonFact()
                session?.let {
                    config.pendingIntent.sendWithUrl(context, it.content.url)
                }
            }

            toolbar.addBrowserAction(button)
        }
    }

    /**
     * Display a refresh button as a button on the toolbar.
     * When clicked, it activates [CustomTabsToolbarListeners.refreshListener].
     */
    @VisibleForTesting
    internal fun addRefreshButton(
        @ColorInt readableColor: Int,
    ) {
        val drawableIcon = getDrawable(context, iconsR.drawable.mozac_ic_arrow_clockwise_24)
        //drawableIcon?.setTint(readableColor)

        val button = Toolbar.ActionButton(
            drawableIcon,
            context.getString(R.string.mozac_feature_customtabs_refresh_button),
        ) {
            emitActionButtonFact()
            customTabsToolbarListeners.refreshListener?.invoke()
        }

        toolbar.addBrowserAction(button)
    }

    /**
     * Display a share button as a button on the toolbar.
     * When clicked, it activates [CustomTabsToolbarListeners.shareListener]
     * and defaults to the [share] KTX helper.
     */
    @VisibleForTesting
    internal fun addShareButton(
        @ColorInt readableColor: Int,
    ) {
        val drawableIcon = getDrawable(context, iconsR.drawable.mozac_ic_share_android_24)!!
        //drawableIcon.setTint(readableColor)

        val button = Toolbar.ActionButton(
            drawableIcon,
            context.getString(R.string.mozac_feature_customtabs_share_link),
        ) {
            val listener = customTabsToolbarListeners.shareListener ?: {
                session?.let {
                    context.share(it.content.url)
                }
            }
            emitActionButtonFact()
            listener.invoke()
        }

        toolbar.addBrowserAction(button)
    }

    /**
     * Display a menu button on the toolbar. When clicked, it activates
     * [CustomTabsToolbarListeners.menuListener].
     */
    @VisibleForTesting
    internal fun addMenuButton() {
        menuButton = Toolbar.ActionButton(
            imageDrawable = menuDrawableIcon,
            contentDescription = context.getString(R.string.mozac_feature_customtabs_menu_button),
            weight = { MENU_WEIGHT },
        ) {
            customTabsToolbarListeners.menuListener?.invoke()
        }.also {
            toolbar.addBrowserAction(it)
        }
    }

    /**
     * Helper to check if menu button should be displayed.
     */
    private fun isMenuAvailable(): Boolean {
        return menuBuilder == null && customTabsToolbarListeners.menuListener != null && menuButton == null
    }

    /**
     * Updates the visibility of the menu in the toolbar.
     */
    fun updateMenuVisibility(isVisible: Boolean) {
        if (isVisible && isMenuAvailable()) {
            addMenuButton()
        } else if (!isVisible) {
            menuButton?.let {
                toolbar.removeBrowserAction(it)
            }
            menuButton = null
        }
    }

    /**
     * Build the menu items displayed when the 3-dot overflow menu is opened.
     */
    @VisibleForTesting
    internal fun addMenuItems() {
        toolbar.display.menuBuilder = menuBuilder?.addCustomMenuItems(context, store, sessionId, menuItemIndex)
    }

    /**
     * When the back button is pressed if not initialized returns false,
     * when initialized removes the current Custom Tabs session and returns true.
     * Should be called when the back button is pressed.
     */
    override fun onBackPressed(): Boolean {
        return if (!initialized) {
            false
        } else {
            if (sessionId != null && useCases.remove(sessionId)) {
                closeListener.invoke()
                true
            } else {
                false
            }
        }
    }

    companion object {
        private val ACTION_BUTTON_MAX_DRAWABLE_DP_SIZE = Size(48, 24)
    }
}

/**
 * Holds button configurations for the custom tabs toolbar.
 *
 * @property showMenu Whether or not to show the menu button.
 * @property showRefreshButton Whether or not to show the refresh button.
 * @property allowCustomizingCloseButton Whether or not to allow a custom icon for the close button.
 */
data class CustomTabsToolbarButtonConfig(
    val showMenu: Boolean = true,
    val showRefreshButton: Boolean = false,
    val allowCustomizingCloseButton: Boolean = true,
)

/**
 * Contains the color configurations for styling the application and system UI.
 *
 * @property updateStatusBarColor Whether or not to update the status bar color.
 * @property updateSystemNavigationBarColor Whether or not to update the system navigation bar color.
 * @property updateToolbarsColor Whether or not to update the application's toolbars color.
 */
data class CustomTabsColorsConfig(
    val updateStatusBarColor: Boolean = true,
    val updateSystemNavigationBarColor: Boolean = true,
    val updateToolbarsColor: Boolean = true,
) {
    /**
     * Get if any color customisation is allowed for application's UI elements.
     */
    fun isAnyColorUpdateAllowed() =
        updateStatusBarColor || updateSystemNavigationBarColor || updateToolbarsColor
}

/**
 * Holds click listeners for buttons on the custom tabs toolbar.
 *
 * @property menuListener Invoked when the menu button is pressed.
 * @property refreshListener Invoked when the refresh button is pressed.
 * @property shareListener Invoked when the share button is pressed.
 */
data class CustomTabsToolbarListeners(
    val menuListener: (() -> Unit)? = null,
    val refreshListener: (() -> Unit)? = null,
    val shareListener: (() -> Unit)? = null,
)
