/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.animation.AnimationUtils
import androidx.annotation.DrawableRes
import androidx.annotation.VisibleForTesting
import androidx.appcompat.content.res.AppCompatResources
import androidx.compose.ui.platform.ComposeView
import androidx.constraintlayout.widget.ConstraintLayout
import androidx.constraintlayout.widget.ConstraintSet
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.core.view.isVisible
import androidx.core.view.updateLayoutParams
import androidx.navigation.fragment.findNavController
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.support.ktx.android.content.res.resolveAttribute
import mozilla.components.support.utils.ext.isLandscape
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.browser.tabstrip.isTabStripEnabled
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.components.toolbar.navbar.shouldAddNavigationBar
import org.mozilla.fenix.databinding.FragmentHomeBinding
import org.mozilla.fenix.databinding.FragmentHomeToolbarViewLayoutBinding
import org.mozilla.fenix.ext.increaseTapAreaVertically
import org.mozilla.fenix.ext.isLargeWindow
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.home.HomeFragment
import org.mozilla.fenix.home.HomeMenuView
import org.mozilla.fenix.search.toolbar.SearchSelector
import org.mozilla.fenix.utils.ToolbarPopupWindow
import java.lang.ref.WeakReference

/**
 * View class for setting up the home screen toolbar.
 */
internal class HomeToolbarView(
    private val homeBinding: FragmentHomeBinding,
    private val interactor: ToolbarInteractor,
    private val homeFragment: HomeFragment,
    private val homeActivity: HomeActivity,
) : FenixHomeToolbar {
    private var context = homeFragment.requireContext()

    override val layout: View = homeBinding.toolbarLayoutStub.inflate()
    private val toolbarBinding = FragmentHomeToolbarViewLayoutBinding.bind(layout)

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal val menuButton = toolbarBinding.menuButton

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal val tabButton = toolbarBinding.tabButton

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal var tabCounterView: TabCounterView? = null

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal var homeMenuView: HomeMenuView? = null

    init {
        initLayoutParameters()
        updateMargins()
    }

    override fun build(browserState: BrowserState) {
        initLayoutParameters()
        updateMargins()

        toolbarBinding.toolbarText.compoundDrawablePadding =
            context.resources.getDimensionPixelSize(R.dimen.search_bar_search_engine_icon_padding)

        toolbarBinding.toolbarWrapper.setOnClickListener {
            interactor.onNavigateSearch()
        }

        toolbarBinding.toolbarWrapper.setOnLongClickListener {
            ToolbarPopupWindow.show(
                WeakReference(it),
                WeakReference(homeBinding.dynamicSnackbarContainer),
                handlePasteAndGo = interactor::onPasteAndGo,
                handlePaste = interactor::onPaste,
                copyVisible = false,
            )
            true
        }

        toolbarBinding.toolbarWrapper.increaseTapAreaVertically(
            TOOLBAR_WRAPPER_INCREASE_HEIGHT_DPS,
        )

        updateButtonVisibility(browserState, context.shouldAddNavigationBar())
    }

    override fun updateButtonVisibility(browserState: BrowserState, shouldAddNavigationBar: Boolean) {
        val showMenu = !shouldAddNavigationBar
        val showTabCounter = !(shouldAddNavigationBar || context.isTabStripEnabled())
        toolbarBinding.menuButton.isVisible = showMenu
        toolbarBinding.tabButton.isVisible = showTabCounter

        tabCounterView = if (showTabCounter) {
            buildTabCounter().also {
                it.update(browserState)
            }
        } else {
            null
        }

        homeMenuView = if (showMenu) {
            buildHomeMenu()
        } else {
            null
        }
    }

    /**
     * Dismisses the home menu.
     */
    fun dismissMenu() {
        homeMenuView?.dismissMenu()
    }

    /**
     * Configure the tab strip [ComposeView].
     *
     * @param block Configuration block for the tab strip [ComposeView].
     */
    fun configureTabStripView(block: ComposeView.() -> Unit) = block(toolbarBinding.tabStripView)

    /**
     * Configure the search selector.
     *
     * @param block Configuration block for the search selector.
     */
    fun configureSearchSelector(block: SearchSelector.() -> Unit) = block(toolbarBinding.searchSelectorButton)

    /**
     * Updates the background of the toolbar.
     *
     * @param id The resource ID of the drawable to use as the background.
     */
    fun updateBackground(@DrawableRes id: Int) {
        toolbarBinding.toolbar.setBackgroundResource(id)
    }

    override fun updateTabCounter(browserState: BrowserState) {
        tabCounterView?.update(browserState)
    }

    override fun updateDividerVisibility(isVisible: Boolean) {
        toolbarBinding.toolbarDivider.isVisible = isVisible
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun buildHomeMenu() = HomeMenuView(
        context = context,
        lifecycleOwner = homeFragment.viewLifecycleOwner,
        homeActivity = homeActivity,
        navController = homeFragment.findNavController(),
        homeFragment = homeFragment,
        menuButton = WeakReference(toolbarBinding.menuButton),
    ).also { it.build() }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun buildTabCounter() = TabCounterView(
        context = context,
        browsingModeManager = homeActivity.browsingModeManager,
        navController = homeFragment.findNavController(),
        tabCounter = toolbarBinding.tabButton,
        showLongPressMenu = !(context.settings().navigationToolbarEnabled && context.isLargeWindow()),
    )

    private fun initLayoutParameters() {
        when (context.settings().toolbarPosition) {
            ToolbarPosition.TOP -> {
                toolbarBinding.toolbarLayout.layoutParams = CoordinatorLayout.LayoutParams(
                    ConstraintLayout.LayoutParams.MATCH_PARENT,
                    ConstraintLayout.LayoutParams.WRAP_CONTENT,
                ).apply {
                    gravity = Gravity.TOP
                }

                val isTabletAndTabStripEnabled = context.isTabStripEnabled()
                ConstraintSet().apply {
                    clone(toolbarBinding.toolbarLayout)
                    clear(toolbarBinding.toolbar.id, ConstraintSet.BOTTOM)
                    clear(toolbarBinding.toolbarDivider.id, ConstraintSet.BOTTOM)

                    if (isTabletAndTabStripEnabled) {
                        connect(
                            toolbarBinding.toolbar.id,
                            ConstraintSet.TOP,
                            toolbarBinding.tabStripView.id,
                            ConstraintSet.BOTTOM,
                        )
                    } else {
                        connect(
                            toolbarBinding.toolbar.id,
                            ConstraintSet.TOP,
                            ConstraintSet.PARENT_ID,
                            ConstraintSet.TOP,
                        )
                    }
                    connect(
                        toolbarBinding.toolbarDivider.id,
                        ConstraintSet.TOP,
                        toolbarBinding.toolbar.id,
                        ConstraintSet.BOTTOM,
                    )
                    connect(
                        toolbarBinding.toolbarDivider.id,
                        ConstraintSet.BOTTOM,
                        ConstraintSet.PARENT_ID,
                        ConstraintSet.BOTTOM,
                    )
                    applyTo(toolbarBinding.toolbarLayout)
                }

                toolbarBinding.toolbar.background = AppCompatResources.getDrawable(
                    context,
                    context.theme.resolveAttribute(R.attr.bottomBarBackgroundTop),
                )

                homeBinding.homeAppBar.updateLayoutParams<ViewGroup.MarginLayoutParams> {
                    topMargin =
                        context.resources.getDimensionPixelSize(R.dimen.home_fragment_top_toolbar_header_margin) +
                        if (isTabletAndTabStripEnabled) {
                            context.resources.getDimensionPixelSize(R.dimen.tab_strip_height)
                        } else {
                            0
                        }
                }
            }

            ToolbarPosition.BOTTOM -> {}
        }
    }

    private fun updateMargins() {
        if (context.settings().navigationToolbarEnabled) {
            val marginStart = context.resources.getDimensionPixelSize(R.dimen.toolbar_horizontal_margin)
            val marginEnd = if (context.isLandscape() || context.isLargeWindow()) {
                context.resources.getDimensionPixelSize(R.dimen.home_item_horizontal_short_margin)
            } else {
                context.resources.getDimensionPixelSize(R.dimen.home_item_horizontal_margin)
            }

            (toolbarBinding.toolbarWrapper.layoutParams as ConstraintLayout.LayoutParams).apply {
                this.marginStart = marginStart
                this.marginEnd = marginEnd
            }
        }
    }

    override fun updateAddressBarVisibility(isVisible: Boolean) {
        if (isVisible) {
            toolbarBinding.toolbarWrapper.startAnimation(AnimationUtils.loadAnimation(context, R.anim.fade_in))
            toolbarBinding.toolbarWrapper.visibility = View.VISIBLE
        } else {
            toolbarBinding.toolbarWrapper.startAnimation(AnimationUtils.loadAnimation(context, R.anim.fade_out))
            toolbarBinding.toolbarWrapper.visibility = View.INVISIBLE
        }
    }

    companion object {
        const val TOOLBAR_WRAPPER_INCREASE_HEIGHT_DPS = 5
    }
}
