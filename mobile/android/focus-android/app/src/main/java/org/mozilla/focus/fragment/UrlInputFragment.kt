/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.fragment

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.content.res.Configuration
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.appcompat.content.res.AppCompatResources
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.core.view.OneShotPreDrawListener
import androidx.core.view.isVisible
import androidx.fragment.app.activityViewModels
import mozilla.components.browser.domains.autocomplete.CustomDomainsProvider
import mozilla.components.browser.domains.autocomplete.ShippedDomainsProvider
import mozilla.components.browser.state.selector.findTab
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.compose.menu.store.MenuState
import mozilla.components.compose.menu.store.MenuStore
import mozilla.components.feature.top.sites.TopSitesConfig
import mozilla.components.feature.top.sites.TopSitesFeature
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.components.support.ktx.android.view.hideKeyboard
import mozilla.components.support.ktx.util.URLStringUtils
import mozilla.components.support.utils.StatusBarUtils
import mozilla.components.support.utils.ThreadUtils
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.focus.GleanMetrics.BrowserSearch
import org.mozilla.focus.GleanMetrics.SearchBar
import org.mozilla.focus.GleanMetrics.SearchWidget
import org.mozilla.focus.R
import org.mozilla.focus.databinding.FragmentUrlinputBinding
import org.mozilla.focus.ext.components
import org.mozilla.focus.ext.defaultSearchEngineName
import org.mozilla.focus.ext.hasSearchTerms
import org.mozilla.focus.ext.requireComponents
import org.mozilla.focus.ext.settings
import org.mozilla.focus.input.InputToolbarIntegration
import org.mozilla.focus.menu.MenuButton
import org.mozilla.focus.menu.home.HomeMenuItem
import org.mozilla.focus.menu.home.HomeMenuMiddleware
import org.mozilla.focus.menu.home.homeMenuGroups
import org.mozilla.focus.searchsuggestions.SearchSuggestionsViewModel
import org.mozilla.focus.searchsuggestions.ui.SearchSuggestionsFragment
import org.mozilla.focus.state.AppAction
import org.mozilla.focus.state.Screen
import org.mozilla.focus.topsites.DefaultTopSitesStorage.Companion.TOP_SITES_MAX_LIMIT
import org.mozilla.focus.topsites.DefaultTopSitesView
import org.mozilla.focus.topsites.TopSitesOverlay
import org.mozilla.focus.ui.theme.FocusTheme
import org.mozilla.focus.utils.SupportUtils
import org.mozilla.focus.utils.ViewUtils

/**
 * Custom exception used to trigger a crash in Focus. This can be triggered by typing "focus:crash" into the URL bar.
 */
class FocusCrashException : Exception()

/** Fragment for displaying the URL input controls. */
// Refactoring the size and function count of this fragment is non-trivial at this point.
// Therefore we ignore those violations for now.
@Suppress("LargeClass", "TooManyFunctions")
class UrlInputFragment : BaseFragment(), View.OnClickListener {
    companion object {
        const val FRAGMENT_TAG = "url_input"

        private const val ARGUMENT_ANIMATION = "animation"
        private const val ARGUMENT_SESSION_UUID = "sesssion_uuid"

        private const val ANIMATION_BROWSER_SCREEN = "browser_screen"

        private const val ANIMATION_DURATION = 200

        /**
         * Creates a [Bundle] containing the provided [tabId]. This is used to create a new [UrlInputFragment] for an
         * existing tab session.
         *
         * @param tabId The unique identifier of the tab.
         * @return A [Bundle] with the tab ID and animation arguments.
         */
        fun bundleForTab(tabId: String): Bundle {
            return Bundle().apply {
                putString(ARGUMENT_ANIMATION, ANIMATION_BROWSER_SCREEN)
                putString(ARGUMENT_SESSION_UUID, tabId)
            }
        }
    }

    private val shippedDomainsProvider = ShippedDomainsProvider()
    private val customDomainsProvider = CustomDomainsProvider()
    private var _binding: FragmentUrlinputBinding? = null
    private val binding
        get() = _binding!!

    private val searchSuggestionsViewModel: SearchSuggestionsViewModel by activityViewModels()

    @Volatile private var isAnimating: Boolean = false

    var tab: TabSessionState? = null
        private set

    private val isOverlay: Boolean
        get() = tab != null

    private var isInitialized = false

    private val toolbarIntegration = ViewBoundFeatureWrapper<InputToolbarIntegration>()
    private val topSitesFeature = ViewBoundFeatureWrapper<TopSitesFeature>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Get session from session manager if there's a session UUID in the fragment's arguments
        arguments?.getString(ARGUMENT_SESSION_UUID)?.let { id ->
            tab = requireComponents.store.state.findTab(id)
        }
    }

    override fun onResume() {
        super.onResume()

        activity?.let {
            shippedDomainsProvider.initialize(it.applicationContext)
            customDomainsProvider.initialize(it.applicationContext)
        }

        StatusBarUtils.getStatusBarHeight(binding.landingLayout) {
            adjustViewToStatusBarHeight(it)
        }

        if (!isInitialized) {
            // Explicitly switching to "edit mode" here in order to focus the toolbar and select
            // all text in it. We only want to do this once per fragment.
            binding.browserToolbar.editMode()
            isInitialized = true
        }

        if (
            requireComponents.settings.searchWidgetInstalled &&
                requireComponents.appStore.state.showSearchWidgetSnackbar
        ) {
            ViewUtils.showBrandedSnackbar(view, R.string.promote_search_widget_snackbar_message, 0)
            SearchWidget.widgetWasAdded.record(NoExtras())
            requireComponents.appStore.dispatch(AppAction.ShowSearchWidgetSnackBar(false))
        }
    }

    override fun onPause() {
        super.onPause()
        view?.hideKeyboard()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }

    private fun adjustViewToStatusBarHeight(statusBarHeight: Int) {
        val inputHeight = resources.getDimension(R.dimen.urlinput_height)

        binding.urlInputLayout.layoutParams.height = (inputHeight + statusBarHeight).toInt()

        val topMargin = (inputHeight + statusBarHeight).toInt()

        if (binding.searchViewContainer.layoutParams is ViewGroup.MarginLayoutParams) {
            val marginParams = binding.searchViewContainer.layoutParams as ViewGroup.MarginLayoutParams
            marginParams.topMargin = topMargin
        }

        if (binding.landingLayout.layoutParams is ViewGroup.MarginLayoutParams) {
            val marginParams = binding.landingLayout.layoutParams as ViewGroup.MarginLayoutParams
            marginParams.topMargin = topMargin
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        _binding = FragmentUrlinputBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        setupTopSitesView()
        setupSearchSuggestionsFragment()
        observeSearchSuggestions()
        observeAutocompleteSuggestions()
        setupToolbarAndFeatures()
        setupUiInteractions()
        initializeViewStates()
    }

    private fun setupTopSitesView() {
        binding.topSites.apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                FocusTheme {
                    TopSitesOverlay()
                }
            }
        }
    }

    private fun setupSearchSuggestionsFragment() {
        childFragmentManager
            .beginTransaction()
            .replace(binding.searchViewContainer.id, SearchSuggestionsFragment.create())
            .commit()
    }

    private fun observeSearchSuggestions() {
        searchSuggestionsViewModel.selectedSearchSuggestion.observe(viewLifecycleOwner) { suggestion ->
            suggestion?.let {
                val isActualSuggestion = searchSuggestionsViewModel.searchQuery.value != it
                val alwaysSearchEnabled = searchSuggestionsViewModel.alwaysSearch
                val finalIsSuggestion = if (alwaysSearchEnabled) false else isActualSuggestion
                onSearch(it, isSuggestion = finalIsSuggestion, alwaysSearch = alwaysSearchEnabled)
                searchSuggestionsViewModel.clearSearchSuggestion()
            }
        }
    }

    private fun observeAutocompleteSuggestions() {
        searchSuggestionsViewModel.autocompleteSuggestion.observe(viewLifecycleOwner) { text ->
            if (text != null) {
                searchSuggestionsViewModel.clearAutocompleteSuggestion()
                binding.browserToolbar.setSearchTerms(text)
            }
        }
    }

    private fun setupToolbarAndFeatures() {
        binding.browserToolbar.private = true

        toolbarIntegration.set(
            InputToolbarIntegration(
                binding.browserToolbar,
                fragment = this,
                shippedDomainsProvider = shippedDomainsProvider,
                customDomainsProvider = customDomainsProvider,
            ),
            owner = this,
            view = binding.browserToolbar,
        )

        topSitesFeature.set(
            feature =
                TopSitesFeature(
                    view = DefaultTopSitesView(requireComponents.appStore),
                    storage = requireComponents.topSitesStorage,
                    config = {
                        TopSitesConfig(
                            targetSitesCount = TOP_SITES_MAX_LIMIT,
                            frecencyConfig = null,
                            providerConfig = null,
                        )
                    },
                ),
            owner = this,
            view = requireView(),
        )
    }

    private fun setupUiInteractions() {
        binding.dismissView.setOnClickListener(this)

        OneShotPreDrawListener.add(binding.urlInputContainerView) {
            animateFirstDraw()
        }
    }

    private fun initializeViewStates() {
        if (isOverlay) {
            binding.landingLayout.isVisible = false
        } else {
            binding.backgroundView.background =
                AppCompatResources.getDrawable(
                    requireContext(),
                    R.drawable.home_background,
                )
            binding.dismissView.isVisible = false
            binding.menuView.isVisible = true
        }

        tab?.let { currentTab ->
            binding.browserToolbar.url =
                if (currentTab.content.hasSearchTerms) {
                    currentTab.content.searchTerms
                } else {
                    currentTab.content.url
                }
            binding.searchViewContainer.isVisible = false
            binding.menuView.isVisible = false
        }

        binding.browserToolbar.editMode()
        setHomeMenu()
    }

    private fun setHomeMenu() {
        binding.menuView.apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                FocusTheme {
                    MenuButton { _, onDismiss ->
                        MenuStore(
                            initialState = MenuState(menuGroups = homeMenuGroups()),
                            middleware =
                                listOf(
                                    HomeMenuMiddleware(
                                        onItemTapped = ::handleHomeMenuItem,
                                        onDismiss = onDismiss,
                                    )
                                ),
                        )
                    }
                }
            }
        }
    }

    private fun handleHomeMenuItem(menuItem: HomeMenuItem) {
        when (menuItem) {
            is HomeMenuItem.Help -> openHelpPage()
            is HomeMenuItem.Settings -> openSettingsPage()
        }
    }

    private fun openHelpPage() {
        requireComponents.tabsUseCases.addTab(
            SupportUtils.HELP_URL,
            source = SessionState.Source.Internal.Menu,
            private = true,
        )
    }

    private fun openSettingsPage() {
        requireComponents.appStore.dispatch(AppAction.OpenSettings(page = Screen.Settings.Page.Start))
    }

    /**
     * Handles the back button press.
     *
     * If the fragment is an overlay, it will animate and dismiss the fragment. Otherwise, it will allow the default
     * back button behavior.
     *
     * @return True if the back button press was handled by this fragment, false otherwise.
     */
    fun onBackPressed(): Boolean {
        if (isOverlay) {
            animateAndDismiss()
            return true
        }

        return false
    }

    override fun onStart() {
        super.onStart()

        activity?.let {
            if (requireContext().settings.isFirstRun) return@onStart
        }
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)

        if (newConfig.orientation != Configuration.ORIENTATION_UNDEFINED) {
            binding.backgroundView.background =
                AppCompatResources.getDrawable(
                    requireContext(),
                    R.drawable.home_background,
                )
        }
    }

    override fun onClick(view: View) {
        if (view.id == R.id.dismissView) {
            handleDismiss()
        } else {
            throw IllegalStateException("Unhandled view in onClick()")
        }
    }

    private fun handleDismiss() {
        if (isOverlay) {
            animateAndDismiss()
        } else {
            // This is a bit hacky, but emulates what the legacy toolbar did before we replaced
            // it with `browser-toolbar`. First we clear the text and then we invoke the "text
            // changed" callback manually for this change.
            binding.browserToolbar.edit.updateUrl("", false)
            onTextChange("")
        }
    }

    private fun animateFirstDraw() {
        if (ANIMATION_BROWSER_SCREEN == arguments?.getString(ARGUMENT_ANIMATION)) {
            animateEnter()
        }
    }

    private fun animateAndDismiss() {
        ThreadUtils.assertOnUiThread()

        if (isAnimating) {
            // We are already animating some state change. Ignore all other requests.
            return
        }

        // Don't allow any more clicks: dismissView is still visible until the animation ends,
        // but we don't want to restart animations and/or trigger hiding again (which could potentially
        // cause crashes since we don't know what state we're in). Ignoring further clicks is the simplest
        // solution, since dismissView is about to disappear anyway.
        binding.dismissView.isClickable = false

        if (ANIMATION_BROWSER_SCREEN == arguments?.getString(ARGUMENT_ANIMATION)) {
            animateExit(onEnd = { if (isOverlay) dismiss() })
        } else {
            dismiss()
        }
    }

    private data class AnimationParams(val xyOffset: Float, val widthScale: Float, val heightScale: Float)

    private fun resolveAnimationParams(): AnimationParams {
        val xyOffset =
            if (isOverlay) {
                (binding.urlInputContainerView.layoutParams as FrameLayout.LayoutParams).bottomMargin.toFloat()
            } else {
                0f
            }
        val width = binding.urlInputBackgroundView.width.toFloat()
        val height = binding.urlInputBackgroundView.height.toFloat()
        return AnimationParams(
            xyOffset = xyOffset,
            widthScale = if (isOverlay) (width + 2 * xyOffset) / width else 1f,
            heightScale = if (isOverlay) (height + 2 * xyOffset) / height else 1f,
        )
    }

    private fun animateBackground(
        scaleX: Float,
        scaleY: Float,
        alpha: Float,
        translationX: Float,
        translationY: Float,
        onEnd: (() -> Unit)? = null,
    ) {
        binding.urlInputBackgroundView
            .animate()
            .setDuration(ANIMATION_DURATION.toLong())
            .scaleX(scaleX)
            .scaleY(scaleY)
            .alpha(alpha)
            .translationX(translationX)
            .translationY(translationY)
            .setListener(
                object : AnimatorListenerAdapter() {
                    // onAnimationEnd is called even after a cancel on ViewPropertyAnimator,
                    // so we track cancellation to avoid invoking onEnd in that case.
                    private var cancelled = false

                    override fun onAnimationCancel(animation: Animator) {
                        cancelled = true
                        isAnimating = false
                    }

                    override fun onAnimationEnd(animation: Animator) {
                        isAnimating = false
                        if (!cancelled) onEnd?.invoke()
                    }
                }
            )
    }

    private fun animateEnter() {
        if (isAnimating) return
        isAnimating = true

        val (xyOffset, widthScale, heightScale) = resolveAnimationParams()

        binding.urlInputBackgroundView.apply {
            pivotX = 0f
            pivotY = 0f
            scaleX = widthScale
            scaleY = heightScale
            translationX = -xyOffset
            translationY = -xyOffset
        }

        animateBackground(scaleX = 1f, scaleY = 1f, alpha = 1f, translationX = 0f, translationY = 0f)
        binding.toolbarBottomBorder.isVisible = false
    }

    private fun animateExit(onEnd: (() -> Unit)? = null) {
        if (isAnimating) return
        isAnimating = true

        val (xyOffset, widthScale, heightScale) = resolveAnimationParams()

        animateBackground(
            scaleX = widthScale,
            scaleY = heightScale,
            alpha = if (isOverlay) 0f else 1f,
            translationX = -xyOffset,
            translationY = -xyOffset,
            onEnd = onEnd,
        )

        binding.toolbarBottomBorder.isVisible = true
        if (!isOverlay) {
            binding.dismissView.isVisible = false
            binding.menuView.isVisible = true
        }
    }

    private fun dismiss() {
        // This method is called from animation callbacks. In the short time frame between the animation
        // starting and ending the activity can be paused. In this case this code can throw an
        // IllegalStateException because we already saved the state (of the activity / fragment) before
        // this transaction is committed. To avoid this we commit while allowing a state loss here.
        // We do not save any state in this fragment (It's getting destroyed) so this should not be a problem.

        val components = context?.components ?: return
        val tabId = tab?.id ?: return
        val currentTabState = components.store.state.findTab(tabId)
        if (currentTabState?.content?.url?.isEmpty() == true) {
            components.tabsUseCases.removeTab(tabId, selectParentIfExists = false)
            components.store.state.selectedTabId?.let { nextId ->
                components.appStore.dispatch(AppAction.FinishEdit(nextId))
            }
        } else {
            components.appStore.dispatch(AppAction.FinishEdit(tabId))
        }
    }

    internal fun onCommit(input: String) {
        if (input.trim().isNotEmpty()) {
            handleCrashTrigger(input)

            binding.browserToolbar.hideKeyboard()

            val isUrl = URLStringUtils.isURLLike(input)
            if (isUrl) {
                openUrl(input)
            } else {
                search(input)
            }

            if (isUrl) {
                SearchBar.enteredUrl.record(NoExtras())
            } else {
                val defaultSearchEngineName = requireComponents.store.defaultSearchEngineName().lowercase()
                SearchBar.performedSearch.record(SearchBar.PerformedSearchExtra(defaultSearchEngineName))
                BrowserSearch.searchCount["$defaultSearchEngineName.action"].add()
            }
        }
    }

    private fun handleCrashTrigger(input: String) {
        if (input == "focus:crash") {
            throw FocusCrashException()
        }
    }

    private fun onSearch(
        query: String,
        @Suppress("UNUSED_PARAMETER") isSuggestion: Boolean = false,
        alwaysSearch: Boolean = false,
    ) {
        if (alwaysSearch) {
            search(query)
        } else {
            if (URLStringUtils.isURLLike(query)) {
                openUrl(query)
            } else {
                search(query)
            }
        }

        val defaultSearchEngineName = requireComponents.store.defaultSearchEngineName().lowercase()
        BrowserSearch.searchCount["$defaultSearchEngineName.suggestion"].add()
    }

    private fun search(searchTerms: String) {
        val tab = tab
        if (tab != null) {
            requireComponents.searchUseCases.defaultSearch.invoke(searchTerms, tab.id)
            requireComponents.appStore.dispatch(AppAction.FinishEdit(tab.id))
        } else {
            requireComponents.searchUseCases.newPrivateTabSearch.invoke(
                searchTerms = searchTerms,
                source = SessionState.Source.Internal.UserEntered,
            )
        }

        searchSuggestionsViewModel.setSearchQuery("")
    }

    private fun openUrl(url: String) {
        val normalizedUrl = URLStringUtils.toNormalizedURL(url)
        when (normalizedUrl) {
            "focus:about" -> {
                requireComponents.appStore.dispatch(AppAction.OpenSettings(Screen.Settings.Page.About))
                return
            }
        }

        val tab = tab
        if (tab != null) {
            requireComponents.sessionUseCases.loadUrl(
                normalizedUrl,
                tab.id,
                originalInput = url,
            )

            requireComponents.appStore.dispatch(AppAction.FinishEdit(tab.id))
        } else {
            requireComponents.tabsUseCases.addTab(
                normalizedUrl,
                source = SessionState.Source.Internal.UserEntered,
                selectTab = true,
                private = true,
                originalInput = url,
            )
        }

        searchSuggestionsViewModel.setSearchQuery("")
    }

    internal fun onStartEditing() {
        if (tab != null) {
            binding.searchViewContainer.isVisible = true
        }
    }

    internal fun onCancelEditing() {
        handleDismiss()
    }

    internal fun onTextChange(text: String) {
        searchSuggestionsViewModel.setSearchQuery(text)

        if (text.trim().isEmpty()) {
            binding.searchViewContainer.isVisible = false

            if (!isOverlay) {
                animateExit()
            }
        } else {
            binding.menuView.isVisible = false

            if (!isOverlay && !binding.dismissView.isVisible) {
                animateEnter()
                binding.dismissView.isVisible = true
            }

            binding.searchViewContainer.isVisible = true
        }
    }
}
