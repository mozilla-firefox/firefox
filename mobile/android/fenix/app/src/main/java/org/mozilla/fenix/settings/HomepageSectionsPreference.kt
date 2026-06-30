/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.content.Context
import android.util.AttributeSet
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGesturesAfterLongPress
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.hapticfeedback.HapticFeedbackType
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalHapticFeedback
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import mozilla.components.compose.base.button.FilledButton
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.home.ui.HomepageSection
import org.mozilla.fenix.utils.Settings
import kotlin.math.abs
import mozilla.components.ui.icons.R as iconsR

/**
 * A [ComposePreference] that shows every homepage section in a single drag-to-reorder list.
 * Sections that have a corresponding on/off setting are shown with a toggle; the rest (the Firefox
 * logo, the header buttons and collections) are reorder-only. The setup checklist is omitted.
 *
 * Reordering is persisted to [Settings.homepageSectionOrder], the same preference used by the
 * standalone reorder screen.
 */
class HomepageSectionsPreference @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0,
) : ComposePreference(context, attrs, defStyleAttr) {

    @Composable
    override fun Content() {
        HomepageSectionsList()
    }
}

private const val DRAGGED_ROW_Z_INDEX = 1f
private const val ROW_MIN_HEIGHT_DP = 56

// Minimum recommended touch-target size (Material) for the drag handle.
private const val HANDLE_TOUCH_TARGET_DP = 48

// Elevation applied to a row while it is being dragged, to lift it above the others.
private const val DRAGGED_ROW_ELEVATION_DP = 6

/**
 * Sections that are not offered in the homepage settings list: the setup checklist is transient,
 * collections is not user-managed here, and the Pocket stories carousel is mutually exclusive with
 * the private-mode-and-stories entry point experiment (so it never renders alongside this list).
 */
private val EXCLUDED_SECTIONS = setOf(
    HomepageSection.SETUP_CHECKLIST,
    HomepageSection.COLLECTIONS,
    HomepageSection.POCKET_STORIES,
)

@Composable
private fun HomepageSectionsList(modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val settings = context.components.settings

    val sections = remember {
        mutableStateListOf<HomepageSection>().apply {
            addAll(
                HomepageSection.fromOrderString(settings.homepageSectionOrder)
                    .filter { it !in EXCLUDED_SECTIONS },
            )
        }
    }

    val haptics = LocalHapticFeedback.current
    var draggingIndex by remember { mutableStateOf<Int?>(null) }
    var dragOffsetY by remember { mutableFloatStateOf(0f) }
    // Measured height of each section's row, so reordering thresholds use the actual neighbour
    // height and stay correct when rows differ (e.g. wrapped titles at large font sizes).
    val rowHeights = remember { mutableStateMapOf<HomepageSection, Int>() }

    Column(modifier = modifier.fillMaxWidth()) {
        Text(
            text = stringResource(R.string.customize_section_order_list_description),
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 16.dp),
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        sections.forEachIndexed { index, section ->
            key(section) {
                val isDragging = draggingIndex == index
                var isEnabled by remember { mutableStateOf(section.isEnabled(settings)) }

                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .onSizeChanged { rowHeights[section] = it.height }
                        .zIndex(if (isDragging) DRAGGED_ROW_Z_INDEX else 0f)
                        .graphicsLayer { translationY = if (isDragging) dragOffsetY else 0f }
                        .shadow(elevation = if (isDragging) DRAGGED_ROW_ELEVATION_DP.dp else 0.dp)
                        .background(MaterialTheme.colorScheme.surface)
                        .heightIn(min = ROW_MIN_HEIGHT_DP.dp)
                        .pointerInput(section) {
                            detectDragGesturesAfterLongPress(
                                onDragStart = {
                                    draggingIndex = sections.indexOf(section)
                                    dragOffsetY = 0f
                                    haptics.performHapticFeedback(HapticFeedbackType.LongPress)
                                },
                                onDrag = { change, dragAmount ->
                                    change.consume()
                                    dragOffsetY += dragAmount.y
                                    // Swap once the row has crossed a neighbour's midpoint, looping
                                    // so a fast drag can move past several rows in one event.
                                    var from = draggingIndex
                                    while (from != null) {
                                        val direction = if (dragOffsetY > 0) 1 else -1
                                        val target = from + direction
                                        val neighborHeight = sections.getOrNull(target)
                                            ?.let { rowHeights[it] } ?: 0
                                        if (target !in sections.indices || neighborHeight <= 0 ||
                                            abs(dragOffsetY) < neighborHeight / 2f
                                        ) {
                                            break
                                        }
                                        sections.add(target, sections.removeAt(from))
                                        draggingIndex = target
                                        dragOffsetY -= direction * neighborHeight
                                        from = target
                                        haptics.performHapticFeedback(HapticFeedbackType.TextHandleMove)
                                    }
                                    // Clamp so the row can't be dragged above the first row's top
                                    // or below the last row's bottom.
                                    draggingIndex?.let { current ->
                                        val above = (0 until current)
                                            .sumOf { rowHeights[sections[it]] ?: 0 }
                                        val below = (current + 1..sections.lastIndex)
                                            .sumOf { rowHeights[sections[it]] ?: 0 }
                                        dragOffsetY = dragOffsetY
                                            .coerceIn(-above.toFloat(), below.toFloat())
                                    }
                                },
                                onDragEnd = {
                                    draggingIndex = null
                                    dragOffsetY = 0f
                                    settings.homepageSectionOrder =
                                        HomepageSection.toOrderString(sections.toList())
                                },
                                onDragCancel = {
                                    draggingIndex = null
                                    dragOffsetY = 0f
                                },
                            )
                        }
                        .padding(horizontal = 16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Icon(
                        painter = painterResource(section.iconRes),
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.onSurfaceVariant,
                    )

                    Spacer(modifier = Modifier.width(16.dp))

                    Text(
                        text = stringResource(section.titleRes),
                        modifier = Modifier.weight(1f),
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.onSurface,
                    )

                    if (section.isToggleable(settings)) {
                        Switch(
                            checked = isEnabled,
                            onCheckedChange = { checked ->
                                isEnabled = checked
                                section.setEnabled(settings, checked)
                                if (section == HomepageSection.SPORTS_WIDGET) {
                                    context.components.appStore.dispatch(
                                        AppAction.SportsWidgetAction.VisibilityChanged(isVisible = checked),
                                    )
                                }
                            },
                        )
                    }

                    Spacer(modifier = Modifier.width(8.dp))

                    Box(
                        modifier = Modifier.size(HANDLE_TOUCH_TARGET_DP.dp),
                        contentAlignment = Alignment.Center,
                    ) {
                        Icon(
                            painter = painterResource(iconsR.drawable.mozac_ic_app_menu_24),
                            contentDescription = stringResource(
                                R.string.customize_section_order_item_content_description,
                                stringResource(section.titleRes),
                            ),
                            tint = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }
        }

        FilledButton(
            text = stringResource(R.string.customize_section_order_reset),
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 16.dp),
            onClick = {
                settings.homepageSectionOrder = ""
                sections.clear()
                sections.addAll(
                    HomepageSection.fromOrderString("").filter { it !in EXCLUDED_SECTIONS },
                )
            },
        )
    }
}

/**
 * Whether [this] section can be toggled on or off from the homepage settings. Sections without a
 * corresponding setting (the Firefox logo, header buttons and collections) are reorder-only.
 */
private fun HomepageSection.isToggleable(settings: Settings): Boolean = when (this) {
    HomepageSection.TOP_SITES,
    HomepageSection.PRIVACY_REPORT,
    HomepageSection.RECENT_TABS,
    HomepageSection.BOOKMARKS,
    HomepageSection.RECENTLY_VISITED,
    HomepageSection.POCKET_STORIES,
    -> true

    HomepageSection.SPORTS_WIDGET -> settings.enableHomepageSportsWidget

    HomepageSection.WORDMARK,
    HomepageSection.HEADER_BUTTONS,
    HomepageSection.COLLECTIONS,
    HomepageSection.SETUP_CHECKLIST,
    -> false
}

private fun HomepageSection.isEnabled(settings: Settings): Boolean = when (this) {
    HomepageSection.TOP_SITES -> settings.showTopSitesFeature
    HomepageSection.PRIVACY_REPORT -> settings.showPrivacyReportFeature
    HomepageSection.SPORTS_WIDGET -> settings.showHomepageSportsWidget
    HomepageSection.RECENT_TABS -> settings.showRecentTabsFeature
    HomepageSection.BOOKMARKS -> settings.showBookmarksHomeFeature
    HomepageSection.RECENTLY_VISITED -> settings.historyMetadataUIFeature
    HomepageSection.POCKET_STORIES -> settings.showPocketRecommendationsFeature
    else -> false
}

private fun HomepageSection.setEnabled(settings: Settings, enabled: Boolean) {
    when (this) {
        HomepageSection.TOP_SITES -> settings.showTopSitesFeature = enabled
        HomepageSection.PRIVACY_REPORT -> settings.showPrivacyReportFeature = enabled
        HomepageSection.SPORTS_WIDGET -> settings.showHomepageSportsWidget = enabled
        HomepageSection.RECENT_TABS -> settings.showRecentTabsFeature = enabled
        HomepageSection.BOOKMARKS -> settings.showBookmarksHomeFeature = enabled
        HomepageSection.RECENTLY_VISITED -> settings.historyMetadataUIFeature = enabled
        HomepageSection.POCKET_STORIES -> settings.showPocketRecommendationsFeature = enabled
        else -> Unit
    }
}
