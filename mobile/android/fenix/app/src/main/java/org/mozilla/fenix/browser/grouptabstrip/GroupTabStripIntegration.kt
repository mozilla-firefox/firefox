/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.grouptabstrip

import android.content.Context
import android.view.ViewGroup
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.coordinatorlayout.widget.CoordinatorLayout
import androidx.coordinatorlayout.widget.CoordinatorLayout.LayoutParams
import androidx.core.view.isVisible
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.support.base.feature.LifecycleAwareFeature
import org.mozilla.fenix.R
import org.mozilla.fenix.components.toolbar.ToolbarPosition
import org.mozilla.fenix.tabgroups.storage.repository.TabGroupRepository

/**
 * Wires up the bottom group tab strip on the browser screen.
 *
 * The integration owns a single [ComposeView] that is added to the [browserLayout]
 * [CoordinatorLayout] and anchored above the bottom toolbar via [GroupTabStripBehavior].
 *
 * The strip becomes visible only when the active tab is assigned to an open tab group;
 * it is hidden in private mode and on tabs that do not belong to any group.
 *
 * @param context Used to instantiate the view.
 * @param browserLayout The browser-screen [CoordinatorLayout] the strip is attached to.
 * @param browserStore Source of truth for the active tab and the live tab list.
 * @param tabGroupRepository Source of truth for tab groups and their tab assignments.
 * @param tabsUseCases Used to switch and close tabs from the strip.
 * @param toolbarPosition The user's toolbar position; passed to the strip's behavior so it
 *  anchors above the bottom toolbar when applicable.
 * @param isFeatureEnabled Predicate evaluated on every state change; when false the strip stays
 *  hidden and the behavior is detached.
 * @param onAddTabInGroup Adds a new tab to the given group id. Defaults to creating a new homepage
 *  tab and assigning it to the group via the repository.
 * @param onShowGroupInTabsTray Invoked when the user taps the group chip; the host is expected to
 *  navigate to the tabs tray scoped to the group.
 */
class GroupTabStripIntegration(
    private val context: Context,
    private val browserLayout: CoordinatorLayout,
    private val browserStore: BrowserStore,
    private val tabGroupRepository: TabGroupRepository,
    private val tabsUseCases: TabsUseCases,
    private val toolbarPosition: ToolbarPosition,
    private val isFeatureEnabled: () -> Boolean,
    private val onAddTabInGroup: (groupId: String) -> Unit,
    private val onShowGroupInTabsTray: (groupId: String) -> Unit,
) : LifecycleAwareFeature {

    private var observerJob: Job? = null
    private var stripView: ComposeView? = null
    private var currentState by mutableStateOf<GroupTabStripState?>(null)

    override fun start() {
        if (stripView == null) {
            stripView = createStripView().also { browserLayout.addView(it) }
        }

        val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
        observerJob = scope.launch {
            combine(
                browserStore.stateFlow,
                tabGroupRepository.observeTabGroups(),
                tabGroupRepository.observeTabGroupAssignments(),
            ) { browserState, groups, assignments ->
                if (!isFeatureEnabled()) {
                    null
                } else {
                    browserState.toGroupTabStripState(
                        tabGroups = groups,
                        tabGroupAssignments = assignments,
                    )
                }
            }
                .distinctUntilChanged()
                .collect { state ->
                    currentState = state
                    stripView?.isVisible = state != null
                }
        }
    }

    override fun stop() {
        observerJob?.cancel()
        observerJob = null
    }

    private fun createStripView(): ComposeView {
        val view = ComposeView(context).apply {
            id = R.id.group_tab_strip
            isVisible = false
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                GroupTabStrip(
                    state = currentState,
                    onSelectTab = { tabId -> tabsUseCases.selectTab(tabId) },
                    onCloseTab = { tabId -> tabsUseCases.removeTab(tabId) },
                    onAddTabInGroup = { groupId -> onAddTabInGroup(groupId) },
                    onShowGroup = { groupId -> onShowGroupInTabsTray(groupId) },
                )
            }
        }

        view.layoutParams = LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT,
        ).apply {
            behavior = GroupTabStripBehavior<ComposeView>(context, toolbarPosition)
        }

        return view
    }

}
