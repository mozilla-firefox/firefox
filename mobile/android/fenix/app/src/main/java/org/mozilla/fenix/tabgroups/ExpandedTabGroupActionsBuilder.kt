/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.net.Uri
import androidx.annotation.ColorInt
import androidx.annotation.VisibleForTesting
import androidx.core.graphics.createBitmap
import androidx.fragment.app.Fragment
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavController
import androidx.navigation.fragment.findNavController
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.engine.prompt.ShareData
import mozilla.components.support.utils.BuildManufacturerChecker
import org.mozilla.fenix.GleanMetrics.TabsTray
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.components.share.CacheHelper
import org.mozilla.fenix.components.share.ShareSheetChooserAction
import org.mozilla.fenix.components.share.ShareSource
import org.mozilla.fenix.components.usecases.ShareUseCases
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.ext.runIfFragmentIsAttached
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowEntryPoint
import org.mozilla.fenix.tabstray.data.TabsTrayItem
import org.mozilla.fenix.tabstray.redux.action.TabGroupAction

private const val TAB_GROUP_SHARE_DOT_SIZE_PX = 128

/**
 * Builds the [ExpandedTabGroupActions] shared by the Tab Manager and the Tab Group flow.
 *
 * @param group The group the expanded view is showing.
 * @param homepageAsNewTabEnabled Whether the homepage as a new tab (HNT) feature is enabled.
 * @param dispatch Dispatches a [TabGroupAction] to the host's store.
 * @param addNewHomepageTab Opens a new homepage tab and returns its ID.
 * @param onTabClick Invoked when a tab in the group is clicked.
 * @param onShareClick Invoked when the group is shared.
 * @param onAddNewTabCompleted Invoked after a new tab has been added to the group.
 */
fun expandedTabGroupActions(
    group: TabsTrayItem.TabGroup,
    homepageAsNewTabEnabled: Boolean,
    dispatch: (TabGroupAction) -> Unit,
    addNewHomepageTab: () -> String,
    onTabClick: (TabsTrayItem.Tab) -> Unit,
    onShareClick: () -> Unit,
    onAddNewTabCompleted: () -> Unit = {},
): ExpandedTabGroupActions =
    ExpandedTabGroupActions(
        onItemClick = {
            when (it) {
                is TabsTrayItem.Tab -> onTabClick(it)
                else -> {}
            }
        },
        onTabClose = { dispatch(TabGroupAction.TabClosed(tab = it, group = group)) },
        onDeleteTabGroupClick = { dispatch(TabGroupAction.DeleteClicked(group)) },
        onEditTabGroupClick = { dispatch(TabGroupAction.EditTabGroupClicked(group = group)) },
        onCloseTabGroupClick = { dispatch(TabGroupAction.CloseTabGroupClicked(group = group)) },
        onUngroupTabGroupClick = { dispatch(TabGroupAction.UngroupRequested(group = group)) },
        onAddNewTabClick =
            if (homepageAsNewTabEnabled) {
                {
                    val newTabId = addNewHomepageTab()
                    dispatch(TabGroupAction.TabAddedToExistingTabGroup(tabId = newTabId, groupId = group.id))
                    onAddNewTabCompleted()
                }
            } else {
                null
            },
        onShareTabGroupClick = onShareClick,
    )

/**
 * Opens the expanded view of the tab group with [groupId] in the Tab Group flow.
 *
 * @param groupId The ID of the group to expand.
 */
internal fun Fragment.navigateToExpandedTabGroup(groupId: String) = runIfFragmentIsAttached {
    findNavController()
        .navigate(
            NavGraphDirections.actionGlobalTabGroupFlowFragment(
                entryPoint = TabGroupFlowEntryPoint.ViewGroup(groupId = groupId)
            )
        )
}

/**
 * Shares [group]'s tabs through the system share sheet.
 *
 * @param group The group being shared.
 * @param dotColor The group's color, used for the share thumbnail.
 */
fun Fragment.shareTabGroup(group: TabsTrayItem.TabGroup, @ColorInt dotColor: Int) = runIfFragmentIsAttached {
    shareTabGroup(
        context = requireContext(),
        scope = viewLifecycleOwner.lifecycleScope,
        navController = findNavController(),
        shareUseCases = requireComponents.useCases.shareUseCases,
        group = group,
        dotColor = dotColor,
    )
}

/**
 * Shares [group]'s tabs through the system share sheet.
 *
 * @param context Used to build and cache the share thumbnail.
 * @param scope Used to build the thumbnail off the main thread.
 * @param navController Used to open the share fragment.
 * @param shareUseCases Performs the share.
 * @param group The group being shared.
 * @param dotColor The group's color, used for the share thumbnail.
 */
@VisibleForTesting
internal fun shareTabGroup(
    context: Context,
    scope: CoroutineScope,
    navController: NavController,
    shareUseCases: ShareUseCases,
    group: TabsTrayItem.TabGroup,
    @ColorInt dotColor: Int,
) {
    TabsTray.shareTabGroup.record(TabsTray.ShareTabGroupExtra(tabCount = group.tabs.size))

    val data = group.tabs.map { ShareData(url = it.url, title = it.title, private = it.private) }
    val share = { thumbnailUri: Uri? ->
        shareUseCases.shareItems(
            items = data,
            source = ShareSource.TABS_TRAY,
            isPrivate = group.tabs.any { it.private },
            subject = group.title,
            chooserActions = ShareSheetChooserAction.tabChooserActions,
            thumbnailUri = thumbnailUri,
            navigateToShareFragment = {
                navController.navigate(
                    NavGraphDirections.actionGlobalShareFragment(
                        data = data.toTypedArray(),
                        shareGroupTitle = group.title,
                        shareGroupColor = dotColor,
                    )
                )
            },
        )
    }

    // ColorOS-based devices render the system share sheet's content-preview image as a
    // large graphic rather than a small icon, so we omit the thumbnail on these devices.
    if (isColorOsBasedDevice()) {
        share(null)
        return
    }

    scope.launch {
        val thumbnailUri =
            withContext(Dispatchers.IO) {
                runCatching {
                    CacheHelper()
                        .saveBitmapToCache(
                            context = context,
                            bitmap = createTabGroupDotBitmap(dotColor),
                            name = "tab_group_share_thumbnail_$dotColor",
                        )
                }
                    .getOrNull()
            }
        if (thumbnailUri == null) {
            context.components.analytics.crashReporter.recordCrashBreadcrumb(
                Breadcrumb(message = "Failed to build tab group share thumbnail")
            )
        }
        share(thumbnailUri)
    }
}

/** Whether this device runs a ColorOS-based OS. */
private fun isColorOsBasedDevice(): Boolean =
    BuildManufacturerChecker().let { it.isOppo() || it.isOnePlus() || it.isRealme() }

private fun createTabGroupDotBitmap(@ColorInt color: Int): Bitmap {
    val bitmap = createBitmap(TAB_GROUP_SHARE_DOT_SIZE_PX, TAB_GROUP_SHARE_DOT_SIZE_PX)
    val radius = TAB_GROUP_SHARE_DOT_SIZE_PX / 2f
    Canvas(bitmap)
        .drawCircle(
            radius,
            radius,
            radius,
            Paint(Paint.ANTI_ALIAS_FLAG).apply { this.color = color },
        )
    return bitmap
}
