/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.library.history

import android.os.Build
import android.view.HapticFeedbackConstants
import android.view.LayoutInflater
import android.view.ViewGroup
import androidx.core.view.isInvisible
import androidx.core.view.isVisible
import androidx.paging.LoadState
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.SimpleItemAnimator
import com.google.android.material.R as materialR
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.concept.sync.SyncEngine
import mozilla.components.service.fxa.manager.FxaAccountManager
import mozilla.components.service.fxa.sync.SyncReason
import org.mozilla.fenix.R
import org.mozilla.fenix.databinding.ComponentHistoryBinding
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.library.LibraryPageView
import org.mozilla.fenix.theme.ThemeManager

/** View that contains and configures the History List */
class HistoryView(
    container: ViewGroup,
    val store: HistoryFragmentStore,
    val onZeroItemsLoaded: () -> Unit,
    val onRecentlyClosedClicked: () -> Unit,
    val onHistoryItemClicked: (History) -> Unit,
    val onDeleteInitiated: (Set<History>) -> Unit,
    val onEmptyStateChanged: (Boolean) -> Unit,
    private val accountManager: FxaAccountManager,
    private val scope: CoroutineScope,
) : LibraryPageView(container) {

    val binding =
        ComponentHistoryBinding.inflate(
            LayoutInflater.from(container.context),
            container,
            true,
        )

    var mode: HistoryFragmentState.Mode = HistoryFragmentState.Mode.Normal
        private set

    val historyAdapter =
        HistoryAdapter(
                store = store,
                onHistoryItemClicked = onHistoryItemClicked,
                onRecentlyClosedClicked = onRecentlyClosedClicked,
                onDeleteInitiated = onDeleteInitiated,
            ) { isEmpty ->
                onEmptyStateChanged(isEmpty)
            }
            .apply {
                addLoadStateListener {
                    // First call will always have itemCount == 0, but we want to keep adapterItemCount
                    // as null until we can distinguish an empty list from populated, so updateEmptyState()
                    // could work correctly.
                    if (itemCount > 0) {
                        adapterItemCount = itemCount
                    } else if (
                        it.source.refresh is LoadState.NotLoading && it.append.endOfPaginationReached && itemCount < 1
                    ) {
                        adapterItemCount = 0
                        onZeroItemsLoaded.invoke()
                    }
                }
            }
    private val layoutManager = LinearLayoutManager(container.context)
    private var adapterItemCount: Int? = null

    init {
        binding.historyList.apply {
            layoutManager = this@HistoryView.layoutManager
            adapter = historyAdapter
            (itemAnimator as SimpleItemAnimator).supportsChangeAnimations = false
        }

        val primaryTextColor = ThemeManager.resolveAttribute(materialR.attr.colorOnSurface, context)
        val primaryBackgroundColor = ThemeManager.resolveAttribute(materialR.attr.colorSurfaceContainerLowest, context)
        binding.swipeRefresh.apply {
            setColorSchemeResources(primaryTextColor)
            setProgressBackgroundColorSchemeResource(primaryBackgroundColor)
        }

        binding.swipeRefresh.setOnRefreshListener {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                binding.swipeRefresh.performHapticFeedback(HapticFeedbackConstants.CONFIRM)
            }
            store.dispatch(HistoryFragmentAction.StartSync)
            scope.launch {
                accountManager.syncNow(
                    reason = SyncReason.User,
                    debounce = true,
                    customEngineSubset = listOf(SyncEngine.History),
                )
            }
            historyAdapter.refresh()
            store.dispatch(HistoryFragmentAction.FinishSync)
        }
    }

    fun update(state: HistoryFragmentState) {
        binding.progressBar.isVisible = state.isDeletingItems
        binding.swipeRefresh.isRefreshing = state.mode === HistoryFragmentState.Mode.Syncing
        binding.swipeRefresh.isEnabled =
            state.mode === HistoryFragmentState.Mode.Normal || state.mode === HistoryFragmentState.Mode.Syncing
        mode = state.mode

        historyAdapter.updatePendingDeletionItems(state.pendingDeletionItems)

        updateEmptyState(userHasHistory = !state.isEmpty)

        historyAdapter.updateMode(state.mode)
        // We want to update the one item above the upper border of the screen, because
        // RecyclerView won't redraw it on scroll and onBindViewHolder() method won't be called.
        val first = layoutManager.findFirstVisibleItemPosition() - 1
        val last = layoutManager.findLastVisibleItemPosition() + 1
        historyAdapter.notifyItemRangeChanged(first, last - first)

        when (val mode = state.mode) {
            is HistoryFragmentState.Mode.Normal -> {
                setUiForNormalMode(context.getString(R.string.library_history))
            }
            is HistoryFragmentState.Mode.Editing -> {
                setUiForSelectingMode(context.getString(R.string.history_multi_select_title, mode.selectedItems.size))
            }
            else -> {
                // no-op
            }
        }
    }

    private fun updateEmptyState(userHasHistory: Boolean) {
        binding.historyList.isInvisible = !userHasHistory
        binding.historyEmptyView.isVisible = !userHasHistory
        binding.topSpacer.isVisible = !userHasHistory

        with(binding.recentlyClosedNavEmpty) {
            recentlyClosedNav.setOnClickListener {
                onRecentlyClosedClicked()
            }
            val numRecentTabs = recentlyClosedNav.context.components.core.store.state.closedTabs.size
            recentlyClosedTabsDescription.text =
                String.format(
                    context.getString(
                        if (numRecentTabs == 1) {
                            R.string.recently_closed_tab
                        } else {
                            R.string.recently_closed_tabs
                        }
                    ),
                    numRecentTabs,
                )
            recentlyClosedNav.isVisible = !userHasHistory
        }
    }
}
