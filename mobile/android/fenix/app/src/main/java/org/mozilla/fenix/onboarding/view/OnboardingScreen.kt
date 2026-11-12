/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.onboarding.view

import androidx.activity.compose.BackHandler
import androidx.annotation.VisibleForTesting
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.DragInteraction
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
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
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import mozilla.components.compose.base.PagerIndicator
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.button.TextButton
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.FeatureFlags
import org.mozilla.fenix.R
import org.mozilla.fenix.components.components
import org.mozilla.fenix.theme.FirefoxTheme

private val logger = Logger("OnboardingScreen")

/**
 * A screen for displaying onboarding.
 *
 * @param pagesToDisplay List of pages to be displayed in onboarding pager ui.
 * @param initialPageIndex The initial page index of the pager.
 * @param onMakeFirefoxDefaultClick Invoked when positive button on default browser page is clicked.
 * @param onSkipDefaultClick Invoked when negative button on default browser page is clicked.
 * @param onSignInButtonClick Invoked when the positive button on the sign in page is clicked.
 * @param onSkipSignInClick Invoked when the negative button on the sign in page is clicked.
 * @param onSyncPairingScanClick Invoked when the scan button on the pairing sync page is clicked.
 * @param onSyncPairingEmailClick Invoked when the email button on the pairing sync page is clicked.
 * @param onSyncPairingSkipClick Invoked when the skip button on the pairing sync page is clicked.
 * @param termsOfServiceEventHandler Invoked when the primary button on the terms of service page is clicked.
 * @param onMarketingDataLearnMoreClick callback for when the user clicks the learn more text link
 * @param onMarketingOptInToggle callback for when the user toggles the opt-in checkbox
 * @param onMarketingDataContinueClick callback for when the user clicks the continue button on the marketing data opt
 *   out screen.
 * @param onMarketingDataSkipClick callback for when the user clicks the skip button on the marketing data opt out
 *   screen.
 * @param onFinish Invoked when the onboarding is completed.
 * @param onImpression Invoked when a page in the pager is displayed.
 * @param currentIndex callback for when the current horizontal pager page changes
 * @param onNavigateToNextPage callback for when the user navigates to the next page in onboarding.
 */
@Composable
@Suppress("LongParameterList", "LongMethod")
fun OnboardingScreen(
    pagesToDisplay: MutableList<OnboardingPageUiData>,
    initialPageIndex: Int,
    onMakeFirefoxDefaultClick: () -> Unit,
    onSkipDefaultClick: () -> Unit,
    onSignInButtonClick: () -> Unit,
    onSkipSignInClick: () -> Unit,
    onSyncPairingScanClick: () -> Unit,
    onSyncPairingEmailClick: () -> Unit,
    onSyncPairingSkipClick: () -> Unit,
    termsOfServiceEventHandler: OnboardingTermsOfServiceEventHandler,
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
    // Pages can be appended from a background thread. Create a copy per composition so the pager's page count always
    // matches the page states built in `OnboardingContent`.
    val pages = pagesToDisplay.toList()
    val pagerState =
        rememberPagerState(
            initialPage = resumedPageIndex(initialPageIndex, pages.size),
            pageCount = { pages.size },
        )
    val isSignedIn: State<Boolean?> =
        components.backgroundServices.syncStore.observeAsComposableState { it.account != null }
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

    if (FeatureFlags.onboardingHapticsEnabled) {
        OnboardingPagerHaptics(pagerState)
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

    LaunchedEffect(isSignedIn.value, isSetToDefault) {
        val scrollToNextCard =
            shouldLaunchEffectScrollToNextPage(
                isSignedIn = isSignedIn,
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
        snapshotFlow { pagerState.currentPage }
            .collect { page ->
                val pageUiData = pagesToDisplay.getOrNull(page)
                if (pageUiData != null) {
                    onImpression(pageUiData)
                } else {
                    logger.warn("Skipping impression for page $page, only ${pagesToDisplay.size} pages to display")
                }
            }
    }

    OnboardingContent(
        pagesToDisplay = pages,
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
        onSyncPairingScanClick = {
            onSyncPairingScanClick()
            scrollToNextPageOrDismiss()
        },
        onSyncPairingEmailClick = {
            onSyncPairingEmailClick()
            scrollToNextPageOrDismiss()
        },
        onSyncPairingSkipClick = {
            onSyncPairingSkipClick()
            scrollToNextPageOrDismiss()
        },
    )
}

/**
 * Returns the page index to resume onboarding on, keeping the persisted [pageIndex] when it still points at an existing
 * page, otherwise falling back to the last available page.
 *
 * The persisted index could become stale as the set of onboarding pages is built from runtime conditions (e.g. whether
 * Firefox is already the default browser) and can also change with experiment rollouts, so between launches it can
 * grow, shrink, or be reordered, potentially leaving the saved index pointing past the end of the current page list.
 * Bounding it to the last available page both prevents an out-of-bounds access and resumes as close as possible to
 * where the user left off.
 *
 * @param pageIndex The page index to resume from.
 * @param pageCount The number of pages for the current onboarding flow.
 */
@VisibleForTesting
internal fun resumedPageIndex(pageIndex: Int, pageCount: Int): Int {
    // Upper bound for the resumed index. The maxOf guard is defensive:
    // onboarding flows are expected to contain at least one page.
    val lastPageIndex = maxOf(0, pageCount - 1)
    // Bound the saved index to the available pages so a stale value can't point past the last page.
    return pageIndex.coerceIn(0, lastPageIndex)
}

@Composable
private fun OnboardingPagerHaptics(pagerState: PagerState) {
    val haptic = LocalHapticFeedback.current

    LaunchedEffect(pagerState, haptic) {
        var userDragged = false
        var previousPage = pagerState.settledPage

        launch {
            pagerState.interactionSource.interactions.collect { interaction ->
                when (interaction) {
                    is DragInteraction.Start -> userDragged = true
                    is DragInteraction.Cancel -> userDragged = false
                }
            }
        }

        snapshotFlow {
            pagerState.isScrollInProgress to pagerState.settledPage
        }
            .collect { (isScrolling, settledPage) ->
                if (!isScrolling) {
                    if (userDragged && settledPage != previousPage) {
                        haptic.performHapticFeedback(HapticFeedbackType.GestureEnd)
                    }

                    previousPage = settledPage
                    userDragged = false
                }
            }
    }
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
    isSetToDefault: Boolean,
    pagesToDisplay: List<OnboardingPageUiData>,
    pagerState: PagerState,
): Boolean {
    val scrollToNextCardFromSignIn =
        isSignedIn.value?.let {
            scrollToNextCardFromSignIn(
                pagesToDisplay,
                pagerState.currentPage,
                it,
            )
        } ?: false

    val scrollToNextCardFromSetToDefault =
        scrollToNextCardFromSetToDefault(
            pagesToDisplay,
            pagerState.currentPage,
            isSetToDefault,
        )

    val scrollToNextCard = scrollToNextCardFromSignIn || scrollToNextCardFromSetToDefault

    return scrollToNextCard
}

@VisibleForTesting
internal fun scrollToNextCardFromSignIn(
    pagesToDisplay: List<OnboardingPageUiData>,
    currentPageIndex: Int,
    isSignedIn: Boolean,
): Boolean {
    val currentPageType = pagesToDisplay.getOrNull(currentPageIndex)?.type
    val isSyncPage =
        currentPageType == OnboardingPageUiData.Type.SYNC_SIGN_IN ||
            currentPageType == OnboardingPageUiData.Type.SYNC_PAIRING
    return isSignedIn && isSyncPage
}

private fun scrollToNextCardFromSetToDefault(
    pagesToDisplay: List<OnboardingPageUiData>,
    currentPageIndex: Int,
    isSetToDefault: Boolean,
): Boolean {
    val indexOfSetToDefaultPage = pagesToDisplay.indexOfFirst { it.type == OnboardingPageUiData.Type.DEFAULT_BROWSER }
    val currentPageIsSetToDefaultPage = currentPageIndex == indexOfSetToDefaultPage
    return isSetToDefault && currentPageIsSetToDefaultPage
}

@Composable
@Suppress("LongParameterList", "LongMethod")
private fun OnboardingContent(
    pagesToDisplay: List<OnboardingPageUiData>,
    pagerState: PagerState,
    onMakeFirefoxDefaultClick: () -> Unit,
    onMakeFirefoxDefaultSkipClick: () -> Unit,
    onSignInButtonClick: () -> Unit,
    onSignInSkipClick: () -> Unit,
    onSyncPairingScanClick: () -> Unit,
    onSyncPairingEmailClick: () -> Unit,
    onSyncPairingSkipClick: () -> Unit,
    termsOfServiceEventHandler: OnboardingTermsOfServiceEventHandler,
    onAgreeAndConfirmTermsOfService: () -> Unit,
    onMarketingOptInToggle: (optIn: Boolean) -> Unit,
    onMarketingDataLearnMoreClick: () -> Unit,
    onMarketingDataContinueClick: (allowMarketingDataCollection: Boolean) -> Unit,
    onMarketingDataSkipClick: () -> Unit,
) {
    BoxWithConstraints(modifier = Modifier.fillMaxSize()) {
        val layout = getOnboardingLayout(this)

        val pageStates = pagesToDisplay.map { pageUiData ->
            mapToOnboardingPageState(
                onboardingPageUiData = pageUiData,
                onMakeFirefoxDefaultClick = onMakeFirefoxDefaultClick,
                onMakeFirefoxDefaultSkipClick = onMakeFirefoxDefaultSkipClick,
                onSignInButtonClick = onSignInButtonClick,
                onSignInSkipClick = onSignInSkipClick,
                onSyncPairingScanClick = onSyncPairingScanClick,
                onSyncPairingEmailClick = onSyncPairingEmailClick,
                onSyncPairingSkipClick = onSyncPairingSkipClick,
                onTermsOfServiceButtonClick = onAgreeAndConfirmTermsOfService,
                isSmallDevice = layout.isSmall,
            )
        }

        OnboardingBackground(
            isVisible =
                !isNonLargeScreenLandscape(
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

            if (pageStates.any { it.onSkipClick != null }) {
                OnboardingSkipButton(
                    onClick = pageStates.getOrNull(pagerState.currentPage)?.onSkipClick,
                    isSmallDevice = layout.isSmall,
                    modifier = Modifier.align(Alignment.End).padding(layout.contentPadding),
                )
            }

            HorizontalPager(
                state = pagerState,
                modifier =
                    Modifier.fillMaxWidth().run {
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
                OnboardingPageForType(
                    type = pagesToDisplay[pageIndex].type,
                    state = pageStates[pageIndex],
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
                    modifier = Modifier.align(Alignment.CenterHorizontally).padding(bottom = 16.dp),
                    activeColor = MaterialTheme.colorScheme.primary,
                    inactiveColor = MaterialTheme.colorScheme.outlineVariant,
                    leaveTrail = false,
                )
            }
        }
    }
}

@Composable
private fun OnboardingBackground(isVisible: Boolean, isSolidBackground: Boolean) {
    if (!isVisible) return

    val backgroundModifier =
        if (isSolidBackground) {
            Modifier.background(color = MaterialTheme.colorScheme.surfaceBright)
        } else {
            Modifier.paint(
                painter = painterResource(R.drawable.nova_onboarding_background),
                contentScale = ContentScale.Crop,
            )
        }

    Box(modifier = Modifier.fillMaxSize().then(backgroundModifier))
}

/**
 * Skip button shown above the onboarding card for pages that define a skip action.
 *
 * @param onClick Invoked when the button is clicked.
 * @param isSmallDevice Whether the card fills the screen, in which case the button has no bottom padding.
 * @param modifier The [Modifier] to be applied to the button.
 */
@Composable
private fun OnboardingSkipButton(
    onClick: (() -> Unit)?,
    isSmallDevice: Boolean,
    modifier: Modifier = Modifier,
) {
    val isVisible = onClick != null
    val alpha by
        animateFloatAsState(
            targetValue = if (isVisible) 1f else 0f,
            label = "SkipButtonAlpha",
        )
    val bottomPadding = if (isSmallDevice) 0.dp else FirefoxTheme.layout.space.static200

    TextButton(
        text = stringResource(R.string.nova_onboarding_skip_button),
        onClick = { onClick?.invoke() },
        modifier =
            modifier
                .padding(bottom = bottomPadding)
                .graphicsLayer { this.alpha = alpha }
                .then(if (isVisible) Modifier else Modifier.clearAndSetSemantics {}),
        enabled = isVisible,
    )
}

@Composable
private fun OnboardingPageForType(
    type: OnboardingPageUiData.Type,
    state: OnboardingPageState,
    termsOfServiceEventHandler: OnboardingTermsOfServiceEventHandler,
    onMarketingDataLearnMoreClick: () -> Unit,
    onMarketingOptInToggle: (optIn: Boolean) -> Unit,
    onMarketingDataContinueClick: (allowMarketingDataCollection: Boolean) -> Unit,
    onMarketingDataSkipClick: () -> Unit,
) {
    when (type) {
        OnboardingPageUiData.Type.DEFAULT_BROWSER,
        OnboardingPageUiData.Type.SYNC_PAIRING,
        OnboardingPageUiData.Type.SYNC_SIGN_IN -> OnboardingPage(state)

        OnboardingPageUiData.Type.MARKETING_DATA ->
            MarketingDataOnboardingPage(
                state = state,
                onMarketingDataLearnMoreClick = onMarketingDataLearnMoreClick,
                onMarketingOptInToggle = onMarketingOptInToggle,
                onMarketingDataContinueClick = onMarketingDataContinueClick,
                onMarketingDataSkipClick = onMarketingDataSkipClick,
            )

        OnboardingPageUiData.Type.TERMS_OF_SERVICE ->
            TermsOfServiceOnboardingPage(
                state,
                termsOfServiceEventHandler,
            )
    }
}

// *** Code below used for previews only *** //

@FlexibleWindowLightDarkPreview
@Composable
private fun OnboardingScreenPreview() {
    val pageCount = defaultPreviewPages().size
    FirefoxTheme {
        OnboardingContent(
            pagesToDisplay = defaultPreviewPages(),
            pagerState =
                rememberPagerState(initialPage = 0) {
                    pageCount
                },
            onMakeFirefoxDefaultClick = {},
            onMakeFirefoxDefaultSkipClick = {},
            onSignInButtonClick = {},
            onSignInSkipClick = {},
            onAgreeAndConfirmTermsOfService = {},
            termsOfServiceEventHandler = object : OnboardingTermsOfServiceEventHandler {},
            onMarketingDataLearnMoreClick = {},
            onMarketingOptInToggle = {},
            onMarketingDataContinueClick = {},
            onMarketingDataSkipClick = {},
            onSyncPairingScanClick = {},
            onSyncPairingEmailClick = {},
            onSyncPairingSkipClick = {},
        )
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun OnboardingScreenPairingPreview() {
    val syncPairingPageUiData =
        OnboardingPageUiData(
            type = OnboardingPageUiData.Type.SYNC_PAIRING,
            imageRes = R.drawable.nova_onboarding_sync_qr,
            title = stringResource(R.string.nova_onboarding_sync_pairing_title),
            description = stringResource(R.string.nova_onboarding_sync_pairing_body),
            primaryButtonLabel = stringResource(R.string.nova_onboarding_sync_pairing_scan_button),
            secondaryButtonLabel = stringResource(R.string.nova_onboarding_sync_pairing_email_button),
        )

    FirefoxTheme {
        OnboardingContent(
            pagesToDisplay = listOf(syncPairingPageUiData),
            pagerState = rememberPagerState(initialPage = 0) { 1 },
            onMakeFirefoxDefaultClick = {},
            onMakeFirefoxDefaultSkipClick = {},
            onSignInButtonClick = {},
            onSignInSkipClick = {},
            onAgreeAndConfirmTermsOfService = {},
            termsOfServiceEventHandler = object : OnboardingTermsOfServiceEventHandler {},
            onMarketingDataLearnMoreClick = {},
            onMarketingOptInToggle = {},
            onMarketingDataContinueClick = {},
            onMarketingDataSkipClick = {},
            onSyncPairingScanClick = {},
            onSyncPairingEmailClick = {},
            onSyncPairingSkipClick = {},
        )
    }
}

@Composable
private fun defaultPreviewPages() =
    listOf(
        defaultBrowserPageUiData(),
        touPageUIData(),
        syncPageUiData(),
    )

@Composable
private fun touPageUIData() =
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.TERMS_OF_SERVICE,
        title = stringResource(id = R.string.onboarding_welcome_to_firefox),
        description = "",
        termsOfService =
            OnboardingTermsOfService(
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
        primaryButtonLabel = stringResource(id = R.string.nova_onboarding_continue_button),
    )

@Composable
private fun defaultBrowserPageUiData() =
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.DEFAULT_BROWSER,
        imageRes = R.drawable.ic_onboarding_welcome,
        title = stringResource(R.string.nova_onboarding_set_to_default_title_2),
        description = stringResource(R.string.nova_onboarding_set_to_default_subtitle),
        primaryButtonLabel = stringResource(R.string.nova_onboarding_set_to_default_button),
        secondaryButtonLabel = stringResource(R.string.nova_onboarding_negative_button),
    )

@Composable
private fun syncPageUiData() =
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.SYNC_SIGN_IN,
        imageRes = R.drawable.ic_onboarding_sync,
        title = stringResource(R.string.nova_onboarding_sync_title),
        description = stringResource(R.string.nova_onboarding_sync_subtitle),
        primaryButtonLabel = stringResource(R.string.nova_onboarding_sync_button),
        secondaryButtonLabel = stringResource(R.string.nova_onboarding_negative_button),
    )
