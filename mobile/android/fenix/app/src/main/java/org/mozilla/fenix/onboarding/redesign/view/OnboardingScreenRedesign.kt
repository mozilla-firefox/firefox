/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.redesign.view

import android.content.res.Configuration
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.BoxWithConstraintsScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBarsPadding
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PageSize
import androidx.compose.foundation.pager.PagerState
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.paint
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import mozilla.components.compose.base.PagerIndicator
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.setup.checklist.ChecklistItem
import org.mozilla.fenix.components.components
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.onboarding.WidgetPinnedReceiver.WidgetPinnedState
import org.mozilla.fenix.onboarding.store.OnboardingAction.OnboardingToolbarAction
import org.mozilla.fenix.onboarding.store.OnboardingStore
import org.mozilla.fenix.onboarding.view.OnboardingPageState
import org.mozilla.fenix.onboarding.view.OnboardingPageUiData
import org.mozilla.fenix.onboarding.view.OnboardingTermsOfService
import org.mozilla.fenix.onboarding.view.OnboardingTermsOfServiceEventHandler
import org.mozilla.fenix.onboarding.view.ToolbarOption
import org.mozilla.fenix.onboarding.view.ToolbarOptionType
import org.mozilla.fenix.onboarding.view.mapToOnboardingPageState
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.utils.isLargeScreenSize

/**
 * The small device max height. The value comes from [org.mozilla.fenix.ext.isTallWindow].
 */
private val SMALL_SCREEN_MAX_HEIGHT = 570.dp
private val logger: Logger = Logger("OnboardingScreenRedesign")

/**
 * A screen for displaying onboarding.
 *
 * @param pagesToDisplay List of pages to be displayed in onboarding pager ui.
 * @param onMakeFirefoxDefaultClick Invoked when positive button on default browser page is clicked.
 * @param onSkipDefaultClick Invoked when negative button on default browser page is clicked.
 * @param onSignInButtonClick Invoked when the positive button on the sign in page is clicked.
 * @param onSkipSignInClick Invoked when the negative button on the sign in page is clicked.
 * @param onNotificationPermissionButtonClick Invoked when positive button on notification page is
 * clicked.
 * @param onSkipNotificationClick Invoked when negative button on notification page is clicked.
 * @param onAddFirefoxWidgetClick Invoked when positive button on add search widget page is clicked.
 * @param onSkipFirefoxWidgetClick Invoked when negative button on add search widget page is clicked.
 * @param onboardingStore The store which contains all the state related to the add-ons onboarding screen.
 * @param termsOfServiceEventHandler Invoked when the primary button on the terms of service page is clicked.
 * @param onCustomizeToolbarClick Invoked when positive button customize toolbar page is clicked.
 * @param onMarketingDataLearnMoreClick callback for when the user clicks the learn more text link
 * @param onMarketingOptInToggle callback for when the user toggles the opt-in checkbox
 * @param onMarketingDataContinueClick callback for when the user clicks the continue button on the
 * marketing data opt out screen.
 * @param onMarketingDataSkipClick callback for when the user clicks the skip button on the
 * marketing data opt out screen.
 * @param onFinish Invoked when the onboarding is completed.
 * @param onImpression Invoked when a page in the pager is displayed.
 * @param currentIndex callback for when the current horizontal pager page changes
 * @param onNavigateToNextPage callback for when the user navigates to the next page in onboarding.
 */
@Composable
@Suppress("LongParameterList", "LongMethod")
fun OnboardingScreenRedesign(
    pagesToDisplay: MutableList<OnboardingPageUiData>,
    onMakeFirefoxDefaultClick: () -> Unit,
    onSkipDefaultClick: () -> Unit,
    onSignInButtonClick: () -> Unit,
    onSkipSignInClick: () -> Unit,
    onNotificationPermissionButtonClick: () -> Unit,
    onSkipNotificationClick: () -> Unit,
    onAddFirefoxWidgetClick: () -> Unit,
    onSkipFirefoxWidgetClick: () -> Unit,
    onboardingStore: OnboardingStore? = null,
    termsOfServiceEventHandler: OnboardingTermsOfServiceEventHandler,
    onCustomizeToolbarClick: () -> Unit,
    onMarketingDataLearnMoreClick: () -> Unit,
    onMarketingOptInToggle: (optIn: Boolean) -> Unit,
    onMarketingDataContinueClick: (allowMarketingDataCollection: Boolean) -> Unit,
    onMarketingDataSkipClick: () -> Unit,
    onFinish: (pageType: OnboardingPageUiData) -> Unit,
    onImpression: (pageType: OnboardingPageUiData) -> Unit,
    currentIndex: (index: Int) -> Unit,
    onNavigateToNextPage: () -> Unit,
) {
    val coroutineScope = rememberCoroutineScope()
    val pagerState = rememberPagerState(pageCount = { pagesToDisplay.size })
    val isSignedIn: State<Boolean?> = components.backgroundServices.syncStore
        .observeAsComposableState { it.account != null }
    val widgetPinnedFlow: StateFlow<Boolean> = WidgetPinnedState.isPinned
    val isWidgetPinnedState by widgetPinnedFlow.collectAsState()
    val isSetToDefault by components.appStore.observeAsComposableState { it.isDefaultBrowser }
    var lastSettledPage by remember { mutableIntStateOf(pagerState.settledPage) }

    LaunchedEffect(pagerState) {
        snapshotFlow { pagerState.currentPage }
            .distinctUntilChanged()
            .collect { page ->
                if (page > lastSettledPage) {
                    onNavigateToNextPage()
                }
                currentIndex(page)
                lastSettledPage = page
            }
    }

    BackHandler(enabled = pagerState.currentPage > 0) {
        coroutineScope.launch {
            pagerState.animateScrollToPage(pagerState.currentPage - 1)
        }
    }

    val scrollToNextPageOrDismiss: () -> Unit = {
        if (pagerState.currentPage >= pagesToDisplay.lastIndex) {
            onFinish(pagesToDisplay[pagesToDisplay.lastIndex])
        } else {
            coroutineScope.launch {
                pagerState.animateScrollToPage(pagerState.currentPage + 1)
            }
        }
    }

    val hasScrolledToNextPage = remember { mutableStateOf(false) }

    LaunchedEffect(isSignedIn.value, isWidgetPinnedState, isSetToDefault) {
        val scrollToNextCard = shouldLaunchEffectScrollToNextPage(
            isSignedIn = isSignedIn,
            isWidgetPinnedState = isWidgetPinnedState,
            isSetToDefault = isSetToDefault,
            pagesToDisplay = pagesToDisplay,
            pagerState = pagerState,
        )

        if (scrollToNextCard && !hasScrolledToNextPage.value) {
            scrollToNextPageOrDismiss()
            hasScrolledToNextPage.value = true
        }
    }

    LaunchedEffect(pagerState) {
        snapshotFlow { pagerState.currentPage }.collect { page ->
            onImpression(pagesToDisplay[page])
        }
    }

    OnboardingContent(
        pagesToDisplay = pagesToDisplay,
        pagerState = pagerState,
        onMakeFirefoxDefaultClick = {
            setToDefaultClick(
                isSetToDefault = isSetToDefault,
                scrollToNextPageOrDismiss = scrollToNextPageOrDismiss,
                onMakeFirefoxDefaultClick = onMakeFirefoxDefaultClick,
            )
        },
        onMakeFirefoxDefaultSkipClick = {
            onSkipDefaultClick()
            scrollToNextPageOrDismiss()
        },
        onSignInButtonClick = {
            onSignInButtonClick()
            scrollToNextPageOrDismiss()
        },
        onSignInSkipClick = {
            onSkipSignInClick()
            scrollToNextPageOrDismiss()
        },
        onNotificationPermissionButtonClick = {
            onNotificationPermissionButtonClick()
            scrollToNextPageOrDismiss()
        },
        onNotificationPermissionSkipClick = {
            onSkipNotificationClick()
            scrollToNextPageOrDismiss()
        },
        onAddFirefoxWidgetClick = {
            if (isWidgetPinnedState) {
                scrollToNextPageOrDismiss()
            } else {
                onAddFirefoxWidgetClick()
            }
        },
        onSkipFirefoxWidgetClick = {
            onSkipFirefoxWidgetClick()
            scrollToNextPageOrDismiss()
        },
        onCustomizeToolbarButtonClick = {
            onCustomizeToolbarClick()
            scrollToNextPageOrDismiss()
        },
        termsOfServiceEventHandler = termsOfServiceEventHandler,
        onAgreeAndConfirmTermsOfService = {
            termsOfServiceEventHandler.onAcceptTermsButtonClicked()
            scrollToNextPageOrDismiss()
        },
        onMarketingDataLearnMoreClick = onMarketingDataLearnMoreClick,
        onMarketingOptInToggle = onMarketingOptInToggle,
        onMarketingDataContinueClick = { allowMarketingDataCollection ->
            onMarketingDataContinueClick(allowMarketingDataCollection)
            scrollToNextPageOrDismiss()
        },
        onMarketingDataSkipClick = {
            onMarketingDataSkipClick()
            scrollToNextPageOrDismiss()
        },
        onboardingStore = onboardingStore,
    )
}

private fun setToDefaultClick(
    isSetToDefault: Boolean,
    scrollToNextPageOrDismiss: () -> Unit,
    onMakeFirefoxDefaultClick: () -> Unit,
) {
    if (isSetToDefault) {
        scrollToNextPageOrDismiss()
    } else {
        onMakeFirefoxDefaultClick()
    }
}

private fun shouldLaunchEffectScrollToNextPage(
    isSignedIn: State<Boolean?>,
    isWidgetPinnedState: Boolean,
    isSetToDefault: Boolean,
    pagesToDisplay: List<OnboardingPageUiData>,
    pagerState: PagerState,
): Boolean {
    val scrollToNextCardFromSignIn = isSignedIn.value?.let {
        scrollToNextCardFromSignIn(
            pagesToDisplay,
            pagerState.currentPage,
            it,
        )
    } ?: false

    val scrollToNextCardFromAddWidget = scrollToNextCardFromAddWidget(
        pagesToDisplay,
        pagerState.currentPage,
        isWidgetPinnedState,
    )

    val scrollToNextCardFromSetToDefault = scrollToNextCardFromSetToDefault(
        pagesToDisplay,
        pagerState.currentPage,
        isSetToDefault,
    )

    val scrollToNextCard =
        scrollToNextCardFromSignIn || scrollToNextCardFromAddWidget || scrollToNextCardFromSetToDefault

    return scrollToNextCard
}

private fun scrollToNextCardFromAddWidget(
    pagesToDisplay: List<OnboardingPageUiData>,
    currentPageIndex: Int,
    isWidgetPinnedState: Boolean,
): Boolean {
    val indexOfWidgetPage =
        pagesToDisplay.indexOfFirst { it.type == OnboardingPageUiData.Type.ADD_SEARCH_WIDGET }
    val currentPageIsWidgetPage = currentPageIndex == indexOfWidgetPage
    return isWidgetPinnedState && currentPageIsWidgetPage
}

private fun scrollToNextCardFromSignIn(
    pagesToDisplay: List<OnboardingPageUiData>,
    currentPageIndex: Int,
    isSignedIn: Boolean,
): Boolean {
    val indexOfSignInPage =
        pagesToDisplay.indexOfFirst { it.type == OnboardingPageUiData.Type.SYNC_SIGN_IN }
    val currentPageIsSignInPage = currentPageIndex == indexOfSignInPage
    return isSignedIn && currentPageIsSignInPage
}

private fun scrollToNextCardFromSetToDefault(
    pagesToDisplay: List<OnboardingPageUiData>,
    currentPageIndex: Int,
    isSetToDefault: Boolean,
): Boolean {
    val indexOfSetToDefaultPage =
        pagesToDisplay.indexOfFirst { it.type == OnboardingPageUiData.Type.DEFAULT_BROWSER }
    val currentPageIsSetToDefaultPage = currentPageIndex == indexOfSetToDefaultPage
    return isSetToDefault && currentPageIsSetToDefaultPage
}

@Composable
@Suppress("LongParameterList")
private fun OnboardingContent(
    pagesToDisplay: List<OnboardingPageUiData>,
    pagerState: PagerState,
    onMakeFirefoxDefaultClick: () -> Unit,
    onMakeFirefoxDefaultSkipClick: () -> Unit,
    onSignInButtonClick: () -> Unit,
    onSignInSkipClick: () -> Unit,
    onNotificationPermissionButtonClick: () -> Unit,
    onNotificationPermissionSkipClick: () -> Unit,
    onAddFirefoxWidgetClick: () -> Unit,
    onSkipFirefoxWidgetClick: () -> Unit,
    onboardingStore: OnboardingStore? = null,
    onCustomizeToolbarButtonClick: () -> Unit,
    termsOfServiceEventHandler: OnboardingTermsOfServiceEventHandler,
    onAgreeAndConfirmTermsOfService: () -> Unit,
    onMarketingOptInToggle: (optIn: Boolean) -> Unit,
    onMarketingDataLearnMoreClick: () -> Unit,
    onMarketingDataContinueClick: (allowMarketingDataCollection: Boolean) -> Unit,
    onMarketingDataSkipClick: () -> Unit,
) {
    BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
        val layout = getOnboardingLayout(this)
        OnboardingBackground(
            isVisible = !isNonLargeScreenLandscape(
                isLargeScreen = layout.isLarge,
                isLandscape = layout.isLandscape,
            ),
            isSolidBackground = layout.isSmall,
        )

        Column(
            verticalArrangement = Arrangement.Center,
            modifier = Modifier.systemBarsPadding(),
        ) {
            Spacer(Modifier.weight(1f)).takeIf { !layout.isSmall }

            HorizontalPager(
                state = pagerState,
                modifier = Modifier
                    .fillMaxWidth()
                    .run {
                        if (layout.isSmall) fillMaxSize() else height(layout.pagerHeight)
                    },
                userScrollEnabled = pagerState.currentPage != 0, // Disable scroll for the Terms of Use card.
                contentPadding = layout.contentPadding,
                pageSize = PageSize.Fill,
                beyondViewportPageCount = 2,
                pageSpacing = pageSpacing(layout.isLarge, layout.isSmall, layout.pagePeekWidth),
                key = { pagesToDisplay[it].type },
                overscrollEffect = null,
            ) { pageIndex ->
                // protect against a rare case where the user goes to the marketing screen at the same
                // moment it gets removed by [MarketingPageRemovalSupport]
                val pageUiState = pagesToDisplay.getOrElse(pageIndex) { pagesToDisplay[it.dec()] }
                val onboardingPageState = mapToOnboardingPageState(
                    onboardingPageUiData = pageUiState,
                    onMakeFirefoxDefaultClick = onMakeFirefoxDefaultClick,
                    onMakeFirefoxDefaultSkipClick = onMakeFirefoxDefaultSkipClick,
                    onSignInButtonClick = onSignInButtonClick,
                    onSignInSkipClick = onSignInSkipClick,
                    onNotificationPermissionButtonClick = onNotificationPermissionButtonClick,
                    onNotificationPermissionSkipClick = onNotificationPermissionSkipClick,
                    onAddFirefoxWidgetClick = onAddFirefoxWidgetClick,
                    onAddFirefoxWidgetSkipClick = onSkipFirefoxWidgetClick,
                    onCustomizeToolbarButtonClick = onCustomizeToolbarButtonClick,
                    onTermsOfServiceButtonClick = onAgreeAndConfirmTermsOfService,
                    shouldShowElevation = !layout.isSmall,
                    isSmallDevice = layout.isSmall,
                )

                OnboardingPageForType(
                    type = pageUiState.type,
                    state = onboardingPageState,
                    onboardingStore = onboardingStore,
                    termsOfServiceEventHandler = termsOfServiceEventHandler,
                    onMarketingDataLearnMoreClick = onMarketingDataLearnMoreClick,
                    onMarketingOptInToggle = onMarketingOptInToggle,
                    onMarketingDataContinueClick = onMarketingDataContinueClick,
                    onMarketingDataSkipClick = onMarketingDataSkipClick,
                )
            }

            Spacer(Modifier.weight(1f)).takeIf { !layout.isSmall }

            if (!layout.isSmall) {
                PagerIndicator(
                    pagerState = pagerState,
                    modifier = Modifier
                        .align(Alignment.CenterHorizontally)
                        .padding(bottom = 16.dp),
                    activeColor = MaterialTheme.colorScheme.onPrimary,
                    inactiveColor = MaterialTheme.colorScheme.onSurfaceVariant,
                    leaveTrail = false,
                )
            }
        }
    }
}

@Composable
private fun OnboardingBackground(isVisible: Boolean, isSolidBackground: Boolean) {
    if (!isVisible) return

    val backgroundModifier = if (isSolidBackground) {
        Modifier.background(color = MaterialTheme.colorScheme.surface)
    } else {
        Modifier.paint(
            painter = painterResource(R.drawable.nova_onboarding_background),
            contentScale = ContentScale.Crop,
        )
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .then(backgroundModifier),
    )
}

@Composable
private fun OnboardingPageForType(
    type: OnboardingPageUiData.Type,
    state: OnboardingPageState,
    onboardingStore: OnboardingStore? = null,
    termsOfServiceEventHandler: OnboardingTermsOfServiceEventHandler,
    onMarketingDataLearnMoreClick: () -> Unit,
    onMarketingOptInToggle: (optIn: Boolean) -> Unit,
    onMarketingDataContinueClick: (allowMarketingDataCollection: Boolean) -> Unit,
    onMarketingDataSkipClick: () -> Unit,
) {
    when (type) {
        OnboardingPageUiData.Type.DEFAULT_BROWSER,
        OnboardingPageUiData.Type.SYNC_SIGN_IN,
        OnboardingPageUiData.Type.ADD_SEARCH_WIDGET,
        OnboardingPageUiData.Type.NOTIFICATION_PERMISSION,
            -> OnboardingPageRedesign(state)

        OnboardingPageUiData.Type.TOOLBAR_PLACEMENT -> {
            val context = LocalContext.current
            onboardingStore?.let { store ->
                ToolbarOnboardingPageRedesign(
                    onboardingStore = store,
                    pageState = state,
                    onToolbarSelectionClicked = {
                        store.dispatch(OnboardingToolbarAction.UpdateSelected(it))
                        context.components.appStore.dispatch(
                            AppAction.SetupChecklistAction.TaskPreferenceUpdated(
                                ChecklistItem.Task.Type.CHANGE_TOOLBAR_PLACEMENT,
                                true,
                            ),
                        )
                    },
                )
            }
        }

        OnboardingPageUiData.Type.MARKETING_DATA -> MarketingDataOnboardingPageRedesign(
            state = state,
            onMarketingDataLearnMoreClick = onMarketingDataLearnMoreClick,
            onMarketingOptInToggle = onMarketingOptInToggle,
            onMarketingDataContinueClick = onMarketingDataContinueClick,
            onMarketingDataSkipClick = onMarketingDataSkipClick,
        )

        OnboardingPageUiData.Type.TERMS_OF_SERVICE -> TermsOfServiceOnboardingPageRedesign(
            state,
            termsOfServiceEventHandler,
        )

        // no-ops
        OnboardingPageUiData.Type.THEME_SELECTION,
            -> {
            logger.error("Unsupported page type: $type used for onboarding redesign.")
        }
    }
}

@Composable
private fun getOnboardingLayout(scope: BoxWithConstraintsScope): OnboardingLayout {
    val context = LocalContext.current
    val config = LocalConfiguration.current
    val isSmall = scope.maxHeight <= SMALL_SCREEN_MAX_HEIGHT
    val isLarge = context.isLargeScreenSize()
    val isLandscape = config.orientation == Configuration.ORIENTATION_LANDSCAPE

    val pagerWidth = pageContentWidth(
        scope = scope,
        isLandscape = isLandscape,
        isSmallScreen = isSmall,
        isLargeScreen = isLarge,
    )
    val pagerHeight = pageContentHeight(
        scope = scope,
        isLargeScreen = isLarge,
        isSmallScreen = isSmall,
        isLandscape = isLandscape,
    )

    val peek = ((scope.maxWidth - pagerWidth) / 2).coerceAtLeast(8.dp)

    val padding = when {
        isSmall && !isLandscape -> PaddingValues(0.dp)
        !isLarge && isLandscape -> PaddingValues(0.dp)
        else -> PaddingValues(horizontal = peek)
    }

    return OnboardingLayout(
        pagerHeight = pagerHeight,
        contentPadding = padding,
        pagePeekWidth = peek,
        isSmall = isSmall,
        isLarge = isLarge,
        isLandscape = isLandscape,
    )
}

private object PageContentLayout {
    val MIN_HEIGHT_DP = 650.dp
    val MIN_WIDTH_DP = 360.dp
    val MIN_HEIGHT_SMALL_SCREEN_DP = 430.dp
    val MIN_WIDTH_SMALL_SCREEN_DP = 300.dp
    val MIN_HEIGHT_TABLET_DP = 620.dp
    val MIN_WIDTH_TABLET_DP = 440.dp
    const val HEIGHT_RATIO = 0.6f
    const val WIDTH_RATIO = 0.85f
    const val TABLET_WIDTH_RATIO = 0.35f
    const val TABLET_HEIGHT_RATIO = 0.50f
    const val HEIGHT_RATIO_LANDSCAPE_NON_LARGE_SCREEN = 1f
    const val WIDTH_RATIO_LANDSCAPE_NON_LARGE_SCREEN = 1f
    const val HEIGHT_RATIO_SMALL_SCREEN = 0.9f
    const val WIDTH_RATIO_SMALL_SCREEN = 0.9f
}

private fun pageContentHeight(
    scope: BoxWithConstraintsScope,
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
    isLandscape: Boolean,
): Dp {
    val minHeight = minHeight(isLargeScreen, isSmallScreen)
    val heightRatio = heightRatio(isLargeScreen, isSmallScreen, isLandscape)

    return scope.maxHeight.times(heightRatio).coerceAtLeast(minHeight)
}

private fun minHeight(
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
): Dp = when {
    isLargeScreen -> PageContentLayout.MIN_HEIGHT_TABLET_DP
    isSmallScreen -> PageContentLayout.MIN_HEIGHT_SMALL_SCREEN_DP
    else -> PageContentLayout.MIN_HEIGHT_DP
}

private fun heightRatio(
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
    isLandscape: Boolean,
): Float = when {
    isLargeScreen -> PageContentLayout.TABLET_HEIGHT_RATIO
    isSmallScreen -> PageContentLayout.HEIGHT_RATIO_SMALL_SCREEN
    !isLargeScreen && isLandscape -> PageContentLayout.HEIGHT_RATIO_LANDSCAPE_NON_LARGE_SCREEN
    else -> PageContentLayout.HEIGHT_RATIO
}

private fun pageContentWidth(
    scope: BoxWithConstraintsScope,
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
    isLandscape: Boolean,
): Dp {
    val minWidth = minWidth(isLargeScreen, isSmallScreen)
    val widthRatio = widthRatio(isLargeScreen, isSmallScreen, isLandscape)

    return scope.maxWidth.times(widthRatio).coerceAtLeast(minWidth)
}

private fun widthRatio(
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
    isLandscape: Boolean,
): Float = when {
    isLargeScreen -> PageContentLayout.TABLET_WIDTH_RATIO
    isSmallScreen -> PageContentLayout.WIDTH_RATIO_SMALL_SCREEN
    !isLargeScreen && isLandscape -> PageContentLayout.WIDTH_RATIO_LANDSCAPE_NON_LARGE_SCREEN
    else -> PageContentLayout.WIDTH_RATIO
}

private fun minWidth(
    isLargeScreen: Boolean,
    isSmallScreen: Boolean,
): Dp = when {
    isLargeScreen -> PageContentLayout.MIN_WIDTH_TABLET_DP
    isSmallScreen -> PageContentLayout.MIN_WIDTH_SMALL_SCREEN_DP
    else -> PageContentLayout.MIN_WIDTH_DP
}

private fun isNonLargeScreenLandscape(isLargeScreen: Boolean, isLandscape: Boolean) =
    (isLandscape && !isLargeScreen)

private fun pageSpacing(isLargeScreen: Boolean, isSmallScreen: Boolean, pagePeekWidth: Dp) = when {
    isLargeScreen -> pagePeekWidth
    isSmallScreen -> 0.dp
    else -> 8.dp
}

private data class OnboardingLayout(
    val pagerHeight: Dp,
    val contentPadding: PaddingValues,
    val pagePeekWidth: Dp,
    val isSmall: Boolean,
    val isLarge: Boolean,
    val isLandscape: Boolean,
)

// *** Code below used for previews only *** //

@FlexibleWindowLightDarkPreview
@Composable
private fun OnboardingScreenPreview() {
    val pageCount = defaultPreviewPages().size
    FirefoxTheme {
        OnboardingContent(
            pagesToDisplay = defaultPreviewPages(),
            pagerState = rememberPagerState(initialPage = 0) {
                pageCount
            },
            onMakeFirefoxDefaultClick = {},
            onMakeFirefoxDefaultSkipClick = {},
            onSignInButtonClick = {},
            onSignInSkipClick = {},
            onAddFirefoxWidgetClick = {},
            onSkipFirefoxWidgetClick = {},
            onCustomizeToolbarButtonClick = {},
            onAgreeAndConfirmTermsOfService = {},
            termsOfServiceEventHandler = object : OnboardingTermsOfServiceEventHandler {},
            onMarketingDataLearnMoreClick = {},
            onMarketingOptInToggle = {},
            onMarketingDataContinueClick = {},
            onMarketingDataSkipClick = {},
            onNotificationPermissionButtonClick = {},
            onNotificationPermissionSkipClick = {},
        )
    }
}

@Composable
private fun defaultPreviewPages() = listOf(
    defaultBrowserPageUiData(),
    touPageUIData(),
    syncPageUiData(),
    toolbarPlacementPageUiData(),
)

@Composable
private fun touPageUIData() = OnboardingPageUiData(
    type = OnboardingPageUiData.Type.TERMS_OF_SERVICE,
    title = stringResource(id = R.string.onboarding_welcome_to_firefox),
    description = "",
    termsOfService = OnboardingTermsOfService(
        subheaderOneText = stringResource(id = R.string.nova_onboarding_tou_subtitle),
        lineOneText = stringResource(id = R.string.nova_onboarding_tou_body_line_1),
        lineOneLinkText = stringResource(id = R.string.nova_onboarding_tou_body_line_1_link_text),
        lineOneLinkUrl = "URL",
        lineTwoText = stringResource(id = R.string.nova_onboarding_tou_body_line_2),
        lineTwoLinkText = stringResource(id = R.string.nova_onboarding_tou_body_line_2_link_text),
        lineTwoLinkUrl = "URL",
        lineThreeText = stringResource(id = R.string.nova_onboarding_tou_body_line_3),
        lineThreeLinkText = stringResource(id = R.string.nova_onboarding_tou_body_line_3_link_text),
    ),
    imageRes = R.drawable.nova_onboarding_tou,
    primaryButtonLabel = stringResource(
        id = R.string.nova_onboarding_continue_button,
    ),
)

@Composable
private fun defaultBrowserPageUiData() = OnboardingPageUiData(
    type = OnboardingPageUiData.Type.DEFAULT_BROWSER,
    imageRes = R.drawable.ic_onboarding_welcome,
    title = stringResource(R.string.nova_onboarding_set_to_default_title_2),
    description = stringResource(R.string.nova_onboarding_set_to_default_subtitle),
    primaryButtonLabel = stringResource(R.string.nova_onboarding_set_to_default_button),
    secondaryButtonLabel = stringResource(R.string.nova_onboarding_negative_button),
)

@Composable
private fun syncPageUiData() = OnboardingPageUiData(
    type = OnboardingPageUiData.Type.SYNC_SIGN_IN,
    imageRes = R.drawable.ic_onboarding_sync,
    title = stringResource(R.string.nova_onboarding_sync_title),
    description = stringResource(R.string.nova_onboarding_sync_subtitle),
    primaryButtonLabel = stringResource(R.string.nova_onboarding_sync_button),
    secondaryButtonLabel = stringResource(R.string.nova_onboarding_negative_button),
)

@Composable
private fun toolbarPlacementPageUiData() = OnboardingPageUiData(
    type = OnboardingPageUiData.Type.TOOLBAR_PLACEMENT,
    imageRes = R.drawable.ic_onboarding_customize_toolbar,
    title = stringResource(R.string.nova_onboarding_toolbar_selection_title),
    description = "", // Unused in redesign
    primaryButtonLabel = stringResource(R.string.nova_onboarding_continue_button),
    toolbarOptions = listOf(
        ToolbarOption(
            toolbarType = ToolbarOptionType.TOOLBAR_TOP,
            imageRes = R.drawable.ic_onboarding_top_toolbar,
            label = stringResource(R.string.nova_onboarding_toolbar_selection_top_label),
        ),
        ToolbarOption(
            toolbarType = ToolbarOptionType.TOOLBAR_BOTTOM,
            imageRes = R.drawable.ic_onboarding_bottom_toolbar,
            label = stringResource(R.string.nova_onboarding_toolbar_selection_bottom_label),
        ),
    ),
)
