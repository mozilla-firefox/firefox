/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.widget.glance

import android.appwidget.AppWidgetManager
import android.content.Context
import android.content.Intent
import android.content.res.Configuration
import androidx.annotation.ColorRes
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.glance.ColorFilter
import androidx.glance.GlanceId
import androidx.glance.GlanceModifier
import androidx.glance.Image
import androidx.glance.ImageProvider
import androidx.glance.LocalContext
import androidx.glance.action.clickable
import androidx.glance.appwidget.GlanceAppWidget
import androidx.glance.appwidget.GlanceAppWidgetReceiver
import androidx.glance.appwidget.action.actionStartActivity
import androidx.glance.appwidget.provideContent
import androidx.glance.background
import androidx.glance.color.ColorProvider
import androidx.glance.layout.Alignment
import androidx.glance.layout.Box
import androidx.glance.layout.Row
import androidx.glance.layout.RowScope
import androidx.glance.layout.Spacer
import androidx.glance.layout.fillMaxWidth
import androidx.glance.layout.height
import androidx.glance.layout.padding
import androidx.glance.layout.size
import androidx.glance.layout.width
import androidx.glance.preview.ExperimentalGlancePreviewApi
import androidx.glance.preview.Preview
import androidx.glance.text.Text
import androidx.glance.text.TextStyle
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.update
import mozilla.components.compose.base.theme.layout.AcornSpace
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.iconpicker.DefaultAppIconRepository
import org.mozilla.fenix.iconpicker.DefaultPackageManagerWrapper
import org.mozilla.fenix.widget.SearchWidgetIntents
import org.mozilla.fenix.widget.SearchWidgets

private val space = AcornSpace.Small

private const val SEARCH_BAR_HEIGHT_DP = 56
private val searchBarHeight = SEARCH_BAR_HEIGHT_DP.dp
private val compactSearchPillHeight = 40.dp
private val buttonSize = 40.dp
private val buttonIconSize = 24.dp
private val appIconSize = 32.dp
private val compactAppIconSize = 24.dp
private val dividerWidth = 1.dp
private val dividerHeight = 24.dp

/**
 * Different styles of the Glance search bar widget offered.
 *
 * @property telemetryId The widget type recorded in the search widget telemetry.
 */
enum class SearchBarWidgetStyle(val telemetryId: String) {
    /** Full width bar with private and voice search buttons separated by a divider. */
    FULL_WIDTH("search_bar"),

    /** Full width bar with circular image, private and voice search buttons. */
    FULL_WIDTH_WITH_LENS("search_bar_actions"),

    /** Compact bar with a search pill followed by circular private, voice and image search buttons. */
    COMPACT("bar_compact"),
}

/** Glance home screen search bar widget rendered according to [style]. */
class SearchBarWidget(private val style: SearchBarWidgetStyle) : GlanceAppWidget() {
    override suspend fun provideGlance(context: Context, id: GlanceId) {
        provideContent {
            val refreshCount by refreshSignal.collectAsState()
            val state = remember(refreshCount) { SearchBarState.live(context, style.telemetryId) }
            SearchBar(style, state)
        }
    }

    override suspend fun providePreview(context: Context, widgetCategory: Int) {
        provideContent {
            SearchBar(style, SearchBarState.preview)
        }
    }

    companion object {
        private val refreshSignal = MutableStateFlow(0)

        /** Refresh the running widget to reflect their current state and buttons. */
        fun refresh() = refreshSignal.update { it + 1 }
    }
}

/** Base receiver for a search bar widget of the given [style], keeping the installed widget state up to date. */
abstract class BaseSearchBarWidgetReceiver(style: SearchBarWidgetStyle) : GlanceAppWidgetReceiver() {
    override val glanceAppWidget: GlanceAppWidget = SearchBarWidget(style)

    override fun onEnabled(context: Context) {
        super.onEnabled(context)
        SearchWidgets.syncInstalledState(context)
    }

    override fun onDisabled(context: Context) {
        super.onDisabled(context)
        SearchWidgets.syncInstalledState(context)
    }

    override fun onUpdate(context: Context, appWidgetManager: AppWidgetManager, appWidgetIds: IntArray) {
        super.onUpdate(context, appWidgetManager, appWidgetIds)
        SearchWidgets.syncInstalledState(context)
    }
}

/** Receiver for the [SearchBarWidgetStyle.FULL_WIDTH] search bar widget. */
class SearchBarWidgetReceiver : BaseSearchBarWidgetReceiver(SearchBarWidgetStyle.FULL_WIDTH)

/** Receiver for the [SearchBarWidgetStyle.FULL_WIDTH_WITH_LENS] search bar widget. */
class SearchBarWithLensWidgetReceiver : BaseSearchBarWidgetReceiver(SearchBarWidgetStyle.FULL_WIDTH_WITH_LENS)

/** Receiver for the [SearchBarWidgetStyle.COMPACT] search bar widget. */
class CompactSearchBarWidgetReceiver : BaseSearchBarWidgetReceiver(SearchBarWidgetStyle.COMPACT)

/**
 * What a search bar widget shows and what its buttons launch.
 *
 * @property voiceSearch Intent launched by the voice search button, or null if it is not clickable.
 * @property lensSearch Intent launched by the image search button, or null if it is not clickable.
 * @property showVoice Whether the voice search button is shown, by default when [voiceSearch] is not null.
 * @property showLens Whether the image search button is shown, by default when [lensSearch] is not null.
 */
private class SearchBarState(
    val voiceSearch: Intent?,
    val lensSearch: Intent?,
    val showVoice: Boolean = voiceSearch != null,
    val showLens: Boolean = lensSearch != null,
) {
    companion object {
        fun live(context: Context, widgetType: String): SearchBarState {
            return SearchBarState(
                voiceSearch = SearchWidgetIntents.maybeVoiceSearch(context, widgetType),
                lensSearch = SearchWidgetIntents.maybeLensSearch(context, widgetType),
            )
        }

        /** State for the widget picker preview, which shows every button. */
        val preview =
            SearchBarState(
                voiceSearch = null,
                lensSearch = null,
                showVoice = true,
                showLens = true,
            )
    }
}

private val contentColor
    @Composable get() = LocalContext.current.dayNightColorProvider(R.color.fx_mobile_on_surface)

private fun Context.dayNightColorProvider(@ColorRes resId: Int) =
    ColorProvider(
        day = Color(withNightMode(false).getColor(resId)),
        night = Color(withNightMode(true).getColor(resId)),
    )

private fun Context.withNightMode(night: Boolean): Context {
    val configuration =
        Configuration(resources.configuration).apply {
            val nightMode = if (night) Configuration.UI_MODE_NIGHT_YES else Configuration.UI_MODE_NIGHT_NO
            uiMode = (uiMode and Configuration.UI_MODE_NIGHT_MASK.inv()) or nightMode
        }
    return createConfigurationContext(configuration)
}

@Composable
private fun SearchBar(style: SearchBarWidgetStyle, state: SearchBarState) {
    when (style) {
        SearchBarWidgetStyle.FULL_WIDTH -> FullWidthSearchBar(state, style.telemetryId)
        SearchBarWidgetStyle.FULL_WIDTH_WITH_LENS -> FullWidthSearchBarWithLens(state, style.telemetryId)
        SearchBarWidgetStyle.COMPACT -> CompactSearchBar(state, style.telemetryId)
    }
}

@Composable
private fun FullWidthSearchBar(state: SearchBarState, widgetType: String) {
    SearchBarContainer(modifier = GlanceModifier.fillMaxWidth().padding(horizontal = space.static150)) {
        SearchLabel(widgetType, modifier = GlanceModifier.defaultWeight(), iconSize = appIconSize)
        IconButton(privateButton(LocalContext.current, widgetType), filled = false)
        if (state.showVoice) {
            Box(
                modifier =
                    GlanceModifier.width(dividerWidth)
                        .height(dividerHeight)
                        .background(R.color.search_bar_widget_divider),
                content = {},
            )
            IconButton(voiceButton(state), filled = false)
        }
    }
}

@Composable
private fun FullWidthSearchBarWithLens(state: SearchBarState, widgetType: String) {
    val buttons =
        listOfNotNull(
            lensButton(state).takeIf { state.showLens },
            privateButton(LocalContext.current, widgetType),
            voiceButton(state).takeIf { state.showVoice },
        )

    SearchBarContainer(modifier = GlanceModifier.fillMaxWidth().padding(horizontal = space.static150)) {
        SearchLabel(widgetType, modifier = GlanceModifier.defaultWeight(), iconSize = appIconSize)
        ActionButtons(buttons, spacing = space.static100)
    }
}

@Composable
private fun CompactSearchBar(state: SearchBarState, widgetType: String) {
    val buttons =
        listOfNotNull(
            privateButton(LocalContext.current, widgetType),
            voiceButton(state).takeIf { state.showVoice },
            lensButton(state).takeIf { state.showLens },
        )

    SearchBarContainer(modifier = GlanceModifier.padding(horizontal = space.static100)) {
        SearchLabel(
            widgetType,
            modifier =
                GlanceModifier.height(compactSearchPillHeight)
                    .background(ImageProvider(R.drawable.search_bar_widget_pill_background))
                    .padding(start = space.static150, end = space.static200),
            iconSize = compactAppIconSize,
        )
        Spacer(GlanceModifier.width(space.static150))
        ActionButtons(buttons, spacing = 6.dp)
    }
}

private data class ActionButtonState(
    @param:DrawableRes val icon: Int,
    @param:StringRes val contentDescription: Int,
    val intent: Intent?,
)

private fun privateButton(context: Context, widgetType: String) =
    ActionButtonState(
        iconsR.drawable.mozac_ic_private_mode_24,
        R.string.search_widget_private_search,
        SearchWidgetIntents.textSearch(context, widgetType, isPrivateBrowsingMode = true),
    )

private fun voiceButton(state: SearchBarState) =
    ActionButtonState(iconsR.drawable.mozac_ic_microphone_24, R.string.search_widget_voice, state.voiceSearch)

private fun lensButton(state: SearchBarState) =
    ActionButtonState(
        iconsR.drawable.mozac_ic_logo_google_lens_24,
        R.string.lens_search_content_description,
        state.lensSearch,
    )

private fun GlanceModifier.clickableIfNotNull(intent: Intent?): GlanceModifier =
    intent?.let { clickable(actionStartActivity(it)) } ?: this

@Composable
private fun SearchBarContainer(
    modifier: GlanceModifier,
    content: @Composable RowScope.() -> Unit,
) {
    Row(
        modifier =
            GlanceModifier.height(searchBarHeight)
                .background(ImageProvider(R.drawable.search_bar_widget_background))
                .then(modifier),
        verticalAlignment = Alignment.CenterVertically,
        content = content,
    )
}

/**
 * The app icon followed by the "Search" label, opening a new text search when clicked.
 *
 * @param widgetType The type of the search widget, recorded in telemetry when the label is clicked.
 * @param modifier Modifier applied to the clickable row.
 * @param iconSize Size of the app icon.
 */
@Composable
private fun SearchLabel(widgetType: String, modifier: GlanceModifier, iconSize: Dp) {
    val context = LocalContext.current
    val appIcon =
        DefaultAppIconRepository(
                packageManager = DefaultPackageManagerWrapper(context.packageManager),
                packageName = context.packageName,
            )
            .selectedAppIcon
            .iconForegroundId

    Row(
        modifier = modifier.clickable(actionStartActivity(SearchWidgetIntents.textSearch(context, widgetType))),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Image(
            provider = ImageProvider(appIcon),
            contentDescription =
                context.getString(
                    R.string.search_widget_content_description_2,
                    context.getString(R.string.app_name),
                ),
            modifier = GlanceModifier.size(iconSize),
        )
        Spacer(GlanceModifier.width(10.dp))
        Text(
            text = context.getString(R.string.search_widget_text_short),
            style = TextStyle(color = contentColor, fontSize = 16.sp),
            maxLines = 1,
        )
    }
}

@Composable
private fun ActionButtons(actionButtonStates: List<ActionButtonState>, spacing: Dp) {
    actionButtonStates.forEachIndexed { index, button ->
        if (index > 0) {
            Spacer(GlanceModifier.width(spacing))
        }
        IconButton(button, filled = true)
    }
}

/** A square button showing the icon of [actionButtonState], drawn on a circular background when [filled] is true. */
@Composable
private fun IconButton(actionButtonState: ActionButtonState, filled: Boolean) {
    val background =
        if (filled) {
            GlanceModifier.background(ImageProvider(R.drawable.search_bar_widget_button_background))
        } else {
            GlanceModifier
        }

    Box(
        modifier = GlanceModifier.size(buttonSize).then(background).clickableIfNotNull(actionButtonState.intent),
        contentAlignment = Alignment.Center,
    ) {
        Image(
            provider = ImageProvider(actionButtonState.icon),
            contentDescription = LocalContext.current.getString(actionButtonState.contentDescription),
            colorFilter = ColorFilter.tint(contentColor),
            modifier = GlanceModifier.size(buttonIconSize),
        )
    }
}

@OptIn(ExperimentalGlancePreviewApi::class)
@Preview(widthDp = 280, heightDp = SEARCH_BAR_HEIGHT_DP)
@Composable
private fun FullWidthSearchBarPreview() {
    SearchBar(SearchBarWidgetStyle.FULL_WIDTH, SearchBarState.preview)
}

@OptIn(ExperimentalGlancePreviewApi::class)
@Preview(widthDp = 280, heightDp = SEARCH_BAR_HEIGHT_DP)
@Composable
private fun FullWidthWithLensSearchBarPreview() {
    SearchBar(SearchBarWidgetStyle.FULL_WIDTH_WITH_LENS, SearchBarState.preview)
}

@OptIn(ExperimentalGlancePreviewApi::class)
@Preview(widthDp = 220, heightDp = SEARCH_BAR_HEIGHT_DP)
@Composable
private fun CompactSearchBarPreview() {
    SearchBar(SearchBarWidgetStyle.COMPACT, SearchBarState.preview)
}
