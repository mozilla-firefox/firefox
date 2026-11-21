/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.redux.middleware

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.launch
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction
import org.mozilla.fenix.tabstray.redux.action.TabManagerUiStateStorageAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction
import org.mozilla.fenix.tabstray.redux.action.TabsTrayAction.InitAction
import org.mozilla.fenix.tabstray.redux.state.Page
import org.mozilla.fenix.tabstray.redux.state.TabsTrayState
import org.mozilla.fenix.tabstray.repository.uistate.TabManagerUiStateRepository

/**
 * [Middleware] that reacts to [TabsTrayAction] and performs UI state storage side effects.
 *
 * @param uiStateRepository [TabManagerUiStateRepository] used for performing UI state storage side effects.
 * @param scope The [CoroutineScope] used for reading from/writing to [uiStateRepository].
 */
class TabManagerUiStateStorageMiddleware(
    private val uiStateRepository: TabManagerUiStateRepository,
    private val scope: CoroutineScope,
) : Middleware<TabsTrayState, TabsTrayAction> {

    private val logger = Logger("TabManagerUiStateStorageMiddleware")

    override fun invoke(
        store: Store<TabsTrayState, TabsTrayAction>,
        next: (TabsTrayAction) -> Unit,
        action: TabsTrayAction,
    ) {
        when (action) {
            is TabManagerUiStateStorageAction ->
                processAction(
                    action = action,
                    store = store,
                )

            else -> {}
        }

        next(action)
    }

    private fun processAction(
        action: TabManagerUiStateStorageAction,
        store: Store<TabsTrayState, TabsTrayAction>,
    ) {
        when (action) {
            InitAction -> {
                // Set up the data observer and set the Flow collection to the lifetime of main scope.
                scope.launch {
                    uiStateRepository.uiState.filterNotNull().distinctUntilChanged().collect { stateUpdate ->
                        store.dispatch(TabsTrayAction.PersistedUiStateUpdateReceived(update = stateUpdate))
                    }
                }
            }

            TabGroupAction.OnboardingDismissed ->
                scope.launch {
                    val success = uiStateRepository.dismissTabGroupOnboarding()
                    if (!success) {
                        logger.debug("Failed to update tab group onboarding was dismissed")
                    }
                }

            is TabsTrayAction.TabDataUpdateReceived -> {
                val userHasTabGroups = action.tabStorageUpdate.tabGroups.isNotEmpty()
                recordUserHadTabGroup(userHasTabGroups = userHasTabGroups)
                dismissTabGroupOnboardingForExistingGroups(userHasTabGroups = userHasTabGroups)
            }

            is TabsTrayAction.PageSelected -> {
                recordTabGroupsPageViewed(
                    page = action.page,
                    userHasTabGroups = store.state.tabGroupState.groups.isNotEmpty(),
                )
            }

            TabGroupAction.OnboardingShown -> {
                if (!store.state.tabGroupState.hasRecordedOnboardingImpression) {
                    recordOnboardingImpression()
                }
            }

            is TabGroupAction.UngroupRequested -> {
                if (store.state.tabGroupState.skipUngroupConfirmation) {
                    store.dispatch(TabGroupAction.UngroupConfirmed(group = action.group, dontAskAgain = true))
                } else {
                    store.dispatch(TabGroupAction.UngroupConfirmationRequested(group = action.group))
                }
            }

            is TabGroupAction.UngroupConfirmed -> setSkipUngroupConfirmation(action.dontAskAgain)
        }
    }

    private fun setSkipUngroupConfirmation(dontAskAgain: Boolean) {
        if (dontAskAgain && uiStateRepository.uiState.value?.skipUngroupConfirmation != true) {
            scope.launch {
                val success = uiStateRepository.setSkipUngroupConfirmation()
                if (!success) {
                    logger.debug("Failed to update whether the ungroup confirmation was dismissed")
                }
            }
        }
    }

    private fun recordUserHadTabGroup(userHasTabGroups: Boolean) {
        // Check if we can avoid any unnecessary writes - skip if this is already true.
        if (userHasTabGroups && uiStateRepository.uiState.value?.hasUserEverHadOneTabGroup != true) {
            scope.launch {
                val success = uiStateRepository.recordUserHadTabGroup()
                if (!success) {
                    logger.debug("Failed to update whether the user had a tab group")
                }
            }
        }
    }

    private fun recordTabGroupsPageViewed(page: Page, userHasTabGroups: Boolean) {
        if (
            page == Page.TabGroups &&
                userHasTabGroups &&
                uiStateRepository.uiState.value?.hasViewedTabGroupsPage != true
        ) {
            scope.launch {
                val success = uiStateRepository.recordTabGroupsPageViewed()
                if (!success) {
                    logger.debug("Failed to update whether the tab groups page was viewed")
                }
            }
        }
    }

    private fun dismissTabGroupOnboardingForExistingGroups(userHasTabGroups: Boolean) {
        if (userHasTabGroups && uiStateRepository.uiState.value?.hasUserDismissedTabGroupOnboarding != true) {
            scope.launch {
                val success = uiStateRepository.dismissTabGroupOnboarding()
                if (!success) {
                    logger.debug("Failed to dismiss tab group onboarding for a user with a tab group")
                }
            }
        }
    }

    private fun recordOnboardingImpression() {
        scope.launch {
            val success = uiStateRepository.incrementTabGroupOnboardingImpressionCount()
            if (!success) {
                logger.debug("Failed to increment the tab group onboarding impression count")
            }
        }
    }
}
