/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalFoundationApi::class)

package org.mozilla.fenix.onboarding.view

import android.content.SharedPreferences
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PagerState
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.LocalLifecycleOwner
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import mozilla.components.compose.base.annotation.LightDarkPreview
import mozilla.components.lib.state.ext.observeAsComposableState
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.setup.checklist.ChecklistItem
import org.mozilla.fenix.components.components
import org.mozilla.fenix.compose.LinkTextState
import org.mozilla.fenix.compose.PagerIndicator
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.onboarding.WidgetPinnedReceiver.WidgetPinnedState
import org.mozilla.fenix.onboarding.store.OnboardingAction
import org.mozilla.fenix.onboarding.store.OnboardingAction.OnboardingThemeAction
import org.mozilla.fenix.onboarding.store.OnboardingAction.OnboardingToolbarAction
import org.mozilla.fenix.onboarding.store.OnboardingStore
import org.mozilla.fenix.theme.FirefoxTheme

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
 * @param onAddOnsButtonClick Invoked when the primary button on add-ons page is clicked.
 * @param onInstallAddOnButtonClick Invoked when a button for installing an add-on is clicked.
 * @param termsOfServiceEventHandler Invoked when the primary button on the terms of service page is clicked.
 * @param onCustomizeToolbarClick Invoked when positive button customize toolbar page is clicked.
 * @param onCustomizeThemeClick Invoked when the primary button on the theme selection page is clicked.
 * @param onMarketingDataLearnMoreClick callback for when the user clicks the learn more text link
 * @param onMarketingOptInToggle callback for when the user toggles the opt-in checkbox
 * @param onMarketingDataContinueClick callback for when the user clicks the continue button on the
 * marketing data opt out screen.
 * on the marketing data opt out screen.
 * @param onFinish Invoked when the onboarding is completed.
 * @param onImpression Invoked when a page in the pager is displayed.
 */
@Composable
@Suppress("LongParameterList", "LongMethod")
fun OnboardingScreen(
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
    onAddOnsButtonClick: () -> Unit,
    onInstallAddOnButtonClick: (AddOn) -> Unit,
    termsOfServiceEventHandler: OnboardingTermsOfServiceEventHandler,
    onCustomizeToolbarClick: () -> Unit,
    onCustomizeThemeClick: () -> Unit,
    onMarketingDataLearnMoreClick: () -> Unit,
    onMarketingOptInToggle: (optIn: Boolean) -> Unit,
    onMarketingDataContinueClick: (allowMarketingDataCollection: Boolean) -> Unit,
    onFinish: (pageType: OnboardingPageUiData) -> Unit,
    onImpression: (pageType: OnboardingPageUiData) -> Unit,
) {
    val coroutineScope = rememberCoroutineScope()
    val pagerState = rememberPagerState(pageCount = { pagesToDisplay.size })
    val isSignedIn: State<Boolean?> = components.backgroundServices.syncStore
        .observeAsComposableState { it.account != null }
    val widgetPinnedFlow: StateFlow<Boolean> = WidgetPinnedState.isPinned
    val isWidgetPinnedState by widgetPinnedFlow.collectAsState()
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current

    DisposableEffect(lifecycleOwner) {
        val settings = context.settings()
        val isNotPartnershipDistribution = !context.components.distributionIdManager.isPartnershipDistribution()

        // Observe the shouldShowMarketingOnboarding preference and disable the marketing page
        // if the preference switches to false
        val listener = SharedPreferences.OnSharedPreferenceChangeListener { _, key ->
            val marketingPageIndex = pagesToDisplay.indexOfFirst { it.type == OnboardingPageUiData.Type.MARKETING_DATA }
            val shouldShowMarketingPreferenceKey = context.getString(R.string.pref_key_should_show_marketing_onboarding)
            val removeMarketingPage = key == shouldShowMarketingPreferenceKey &&
                !settings.shouldShowMarketingOnboarding &&
                pagerState.currentPage < marketingPageIndex &&
                isNotPartnershipDistribution

            if (removeMarketingPage) {
                pagesToDisplay.removeAt(marketingPageIndex)
            }
        }

        settings.preferences.registerOnSharedPreferenceChangeListener(listener)

        // If the preference is already false, disable the marketing page
        if (!settings.shouldShowMarketingOnboarding && isNotPartnershipDistribution) {
            val marketingPage = pagesToDisplay.find { it.type == OnboardingPageUiData.Type.MARKETING_DATA }
            marketingPage?.let { pagesToDisplay.remove(it) }
        }

        onDispose {
            settings.preferences.unregisterOnSharedPreferenceChangeListener(listener)
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

    LaunchedEffect(isSignedIn.value, isWidgetPinnedState) {
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

        val scrollToNextCard = scrollToNextCardFromSignIn || scrollToNextCardFromAddWidget

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
            scrollToNextPageOrDismiss()
            onMakeFirefoxDefaultClick()
        },
        onMakeFirefoxDefaultSkipClick = {
            scrollToNextPageOrDismiss()
            onSkipDefaultClick()
        },
        onSignInButtonClick = {
            onSignInButtonClick()
            scrollToNextPageOrDismiss()
        },
        onSignInSkipClick = {
            scrollToNextPageOrDismiss()
            onSkipSignInClick()
        },
        onNotificationPermissionButtonClick = {
            scrollToNextPageOrDismiss()
            onNotificationPermissionButtonClick()
        },
        onNotificationPermissionSkipClick = {
            scrollToNextPageOrDismiss()
            onSkipNotificationClick()
        },
        onAddFirefoxWidgetClick = {
            if (isWidgetPinnedState) {
                scrollToNextPageOrDismiss()
            } else {
                onAddFirefoxWidgetClick()
            }
        },
        onSkipFirefoxWidgetClick = {
            scrollToNextPageOrDismiss()
            onSkipFirefoxWidgetClick()
        },
        onAddOnsButtonClick = {
            scrollToNextPageOrDismiss()
            onAddOnsButtonClick()
        },
        onInstallAddOnButtonClick = onInstallAddOnButtonClick,
        onCustomizeToolbarButtonClick = {
            scrollToNextPageOrDismiss()
            onCustomizeToolbarClick()
        },
        onCustomizeThemeButtonClick = {
            scrollToNextPageOrDismiss()
            onCustomizeThemeClick()
        },
        termsOfServiceEventHandler = termsOfServiceEventHandler,
        onAgreeAndConfirmTermsOfService = {
            scrollToNextPageOrDismiss()
            termsOfServiceEventHandler.onAcceptTermsButtonClicked()
        },
        onMarketingDataLearnMoreClick = onMarketingDataLearnMoreClick,
        onMarketingOptInToggle = onMarketingOptInToggle,
        onMarketingDataContinueClick = { allowMarketingDataCollection ->
            onMarketingDataContinueClick(allowMarketingDataCollection)
            scrollToNextPageOrDismiss()
        },
        onboardingStore = onboardingStore,
    )
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
    onAddOnsButtonClick: () -> Unit,
    onInstallAddOnButtonClick: (AddOn) -> Unit,
    onCustomizeToolbarButtonClick: () -> Unit,
    onCustomizeThemeButtonClick: () -> Unit,
    termsOfServiceEventHandler: OnboardingTermsOfServiceEventHandler,
    onAgreeAndConfirmTermsOfService: () -> Unit,
    onMarketingOptInToggle: (optIn: Boolean) -> Unit,
    onMarketingDataLearnMoreClick: () -> Unit,
    onMarketingDataContinueClick: (allowMarketingDataCollection: Boolean) -> Unit,
) {
    val nestedScrollConnection = remember { DisableForwardSwipeNestedScrollConnection(pagerState) }

    Column(
        modifier = Modifier
            .background(FirefoxTheme.colors.layer1)
            .statusBarsPadding()
            .navigationBarsPadding(),
    ) {
        HorizontalPager(
            state = pagerState,
            key = { pagesToDisplay[it].type },
            modifier = Modifier
                .weight(1f)
                .nestedScroll(nestedScrollConnection),
        ) { pageIndex ->
            val pageUiState = pagesToDisplay[pageIndex]
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
                onAddOnsButtonClick = onAddOnsButtonClick,
                onCustomizeToolbarButtonClick = onCustomizeToolbarButtonClick,
                onCustomizeThemeClick = onCustomizeThemeButtonClick,
                onTermsOfServiceButtonClick = onAgreeAndConfirmTermsOfService,
            )
            OnboardingPageForType(
                type = pageUiState.type,
                state = onboardingPageState,
                onboardingStore = onboardingStore,
                termsOfServiceEventHandler = termsOfServiceEventHandler,
                onInstallAddOnButtonClick = onInstallAddOnButtonClick,
                onMarketingDataLearnMoreClick = onMarketingDataLearnMoreClick,
                onMarketingOptInToggle = onMarketingOptInToggle,
                onMarketingDataContinueClick = onMarketingDataContinueClick,
            )
        }

        PagerIndicator(
            pagerState = pagerState,
            activeColor = FirefoxTheme.colors.actionPrimary,
            inactiveColor = FirefoxTheme.colors.actionSecondary,
            leaveTrail = true,
            modifier = Modifier
                .align(Alignment.CenterHorizontally)
                .padding(bottom = 16.dp),
        )
    }
}

@Composable
private fun OnboardingPageForType(
    type: OnboardingPageUiData.Type,
    state: OnboardingPageState,
    onboardingStore: OnboardingStore? = null,
    termsOfServiceEventHandler: OnboardingTermsOfServiceEventHandler,
    onInstallAddOnButtonClick: (AddOn) -> Unit,
    onMarketingDataLearnMoreClick: () -> Unit,
    onMarketingOptInToggle: (optIn: Boolean) -> Unit,
    onMarketingDataContinueClick: (allowMarketingDataCollection: Boolean) -> Unit,
) {
    when (type) {
        OnboardingPageUiData.Type.DEFAULT_BROWSER,
        OnboardingPageUiData.Type.SYNC_SIGN_IN,
        OnboardingPageUiData.Type.ADD_SEARCH_WIDGET,
        OnboardingPageUiData.Type.NOTIFICATION_PERMISSION,
        -> OnboardingPage(state)

        OnboardingPageUiData.Type.TOOLBAR_PLACEMENT -> {
            val context = LocalContext.current
            onboardingStore?.let { store ->
                ToolbarOnboardingPage(
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

        OnboardingPageUiData.Type.THEME_SELECTION -> {
            val context = LocalContext.current
            onboardingStore?.let { store ->
                ThemeOnboardingPage(
                    onboardingStore = store,
                    pageState = state,
                    onThemeSelectionClicked = {
                        store.dispatch(OnboardingThemeAction.UpdateSelected(it))
                        context.components.appStore.dispatch(
                            AppAction.SetupChecklistAction.TaskPreferenceUpdated(
                                ChecklistItem.Task.Type.SELECT_THEME,
                                true,
                            ),
                        )
                    },
                )
            }
        }

        OnboardingPageUiData.Type.MARKETING_DATA -> MarketingDataOnboardingPage(
            state = state,
            onMarketingDataLearnMoreClick = onMarketingDataLearnMoreClick,
            onMarketingOptInToggle = onMarketingOptInToggle,
            onMarketingDataContinueClick = onMarketingDataContinueClick,
        )

        OnboardingPageUiData.Type.ADD_ONS,
        -> onboardingStore?.let { store ->
            state.addOns?.let { addOns ->
                store.dispatch(OnboardingAction.OnboardingAddOnsAction.UpdateAddons(addOns))
            }
            AddOnsOnboardingPage(store, state, onInstallAddOnButtonClick)
        }

        OnboardingPageUiData.Type.TERMS_OF_SERVICE -> TermsOfServiceOnboardingPage(
            state,
            termsOfServiceEventHandler,
        )
    }
}

private class DisableForwardSwipeNestedScrollConnection(
    private val pagerState: PagerState,
) : NestedScrollConnection {

    override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset =
        if (available.x > 0) {
            // Allow going back on swipe
            Offset.Zero
        } else {
            // For forward swipe, only allow if the visible item offset is less than 0,
            // this would be a result of a slow back fling, and we should allow snapper to
            // snap to the appropriate item.
            // Else consume the whole offset and disable going forward.
            if (pagerState.currentPageOffsetFraction < 0) {
                Offset.Zero
            } else {
                Offset(available.x, 0f)
            }
        }
}

@LightDarkPreview
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
            onNotificationPermissionButtonClick = {},
            onNotificationPermissionSkipClick = {},
            onAddFirefoxWidgetClick = {},
            onSkipFirefoxWidgetClick = {},
            onAddOnsButtonClick = {},
            onInstallAddOnButtonClick = {},
            onCustomizeToolbarButtonClick = {},
            onCustomizeThemeButtonClick = {},
            onAgreeAndConfirmTermsOfService = {},
            termsOfServiceEventHandler = object : OnboardingTermsOfServiceEventHandler {},
            onMarketingDataLearnMoreClick = {},
            onMarketingOptInToggle = {},
            onMarketingDataContinueClick = {},
        )
    }
}

@Composable
private fun defaultPreviewPages() = listOf(
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.DEFAULT_BROWSER,
        imageRes = R.drawable.ic_onboarding_welcome,
        title = stringResource(R.string.juno_onboarding_default_browser_title_nimbus_2),
        description = stringResource(R.string.juno_onboarding_default_browser_description_nimbus_3),
        primaryButtonLabel = stringResource(R.string.juno_onboarding_default_browser_positive_button),
        secondaryButtonLabel = stringResource(R.string.juno_onboarding_default_browser_negative_button),
        privacyCaption = Caption(
            text = stringResource(R.string.juno_onboarding_privacy_notice_text),
            linkTextState = LinkTextState(
                text = stringResource(R.string.juno_onboarding_privacy_notice_text),
                url = "",
                onClick = {},
            ),
        ),
    ),
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.SYNC_SIGN_IN,
        imageRes = R.drawable.ic_onboarding_sync,
        title = stringResource(R.string.juno_onboarding_sign_in_title_2),
        description = stringResource(R.string.juno_onboarding_sign_in_description_3),
        primaryButtonLabel = stringResource(R.string.juno_onboarding_sign_in_positive_button),
        secondaryButtonLabel = stringResource(R.string.juno_onboarding_sign_in_negative_button),
        privacyCaption = Caption(
            text = stringResource(R.string.juno_onboarding_privacy_notice_text),
            linkTextState = LinkTextState(
                text = stringResource(R.string.juno_onboarding_privacy_notice_text),
                url = "",
                onClick = {},
            ),
        ),
    ),
    OnboardingPageUiData(
        type = OnboardingPageUiData.Type.NOTIFICATION_PERMISSION,
        imageRes = R.drawable.ic_notification_permission,
        title = stringResource(R.string.juno_onboarding_enable_notifications_title_nimbus_2),
        description = stringResource(R.string.juno_onboarding_enable_notifications_description_nimbus_2),
        primaryButtonLabel = stringResource(R.string.juno_onboarding_enable_notifications_positive_button),
        secondaryButtonLabel = stringResource(R.string.juno_onboarding_enable_notifications_negative_button),
        privacyCaption = Caption(
            text = stringResource(R.string.juno_onboarding_privacy_notice_text),
            linkTextState = LinkTextState(
                text = stringResource(R.string.juno_onboarding_privacy_notice_text),
                url = "",
                onClick = {},
            ),
        ),
    ),
)
