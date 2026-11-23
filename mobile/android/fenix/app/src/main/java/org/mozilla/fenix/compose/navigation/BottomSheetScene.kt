/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.compose.navigation

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.ModalBottomSheetProperties
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation3.runtime.NavEntry
import androidx.navigation3.scene.OverlayScene
import androidx.navigation3.scene.Scene
import androidx.navigation3.scene.SceneStrategy
import androidx.navigation3.scene.SceneStrategyScope
import kotlin.time.Duration.Companion.milliseconds
import kotlinx.coroutines.delay
import mozilla.components.compose.base.BottomSheetHandle
import org.mozilla.fenix.compose.BetaLabel
import org.mozilla.fenix.compose.navigation.BottomSheetSceneStrategy.Companion.bottomSheet
import org.mozilla.fenix.compose.rememberModalSheetState
import org.mozilla.fenix.theme.FirefoxTheme

private val firstOpenDelay = 25.milliseconds

/** An [OverlayScene] that renders an [entry] within a [ModalBottomSheet]. */
@OptIn(ExperimentalMaterial3Api::class)
@Suppress("LongParameterList")
internal class BottomSheetScene<T : Any>(
    override val key: Any,
    override val previousEntries: List<NavEntry<T>>,
    override val overlaidEntries: List<NavEntry<T>>,
    private val entry: NavEntry<T>,
    private val modalBottomSheetProperties: ModalBottomSheetProperties,
    private val skipPartiallyExpanded: Boolean,
    private val handleContentDescription: String,
    private val showBetaLabel: Boolean,
    private val fullyExpandOnFirstOpen: Boolean,
    private val onBack: () -> Unit,
) : OverlayScene<T> {

    override val entries: List<NavEntry<T>> = listOf(entry)

    override val content: @Composable (() -> Unit) = {
        val sheetState = rememberModalSheetState(skipPartiallyExpanded = skipPartiallyExpanded)

        LaunchedEffect(Unit) {
            if (fullyExpandOnFirstOpen) {
                // There is a race condition with the sheet's initial animation and invoking `sheetState.expand()`.
                // Wait a minor amount of time to invoke the full expansion.
                delay(duration = firstOpenDelay)
                sheetState.expand()
            }
        }

        ModalBottomSheet(
            onDismissRequest = onBack,
            properties = modalBottomSheetProperties,
            sheetState = sheetState,
            dragHandle = null,
            containerColor = MaterialTheme.colorScheme.surface,
            scrimColor = MaterialTheme.colorScheme.scrim,
        ) {
            Box(modifier = Modifier.fillMaxWidth()) {
                if (showBetaLabel) {
                    BetaLabel(
                        modifier =
                            Modifier.align(Alignment.TopStart)
                                .padding(
                                    start = FirefoxTheme.layout.space.static200,
                                    top = FirefoxTheme.layout.space.static200,
                                )
                    )
                }

                BottomSheetHandle(
                    onRequestDismiss = onBack,
                    contentDescription = handleContentDescription,
                    modifier = Modifier.align(Alignment.Center).padding(all = 16.dp),
                )
            }

            entry.Content()
        }
    }

    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other == null || this::class != other::class) return false

        other as BottomSheetScene<*>

        return key == other.key &&
            previousEntries == other.previousEntries &&
            overlaidEntries == other.overlaidEntries &&
            entry == other.entry &&
            modalBottomSheetProperties == other.modalBottomSheetProperties
    }

    override fun hashCode(): Int {
        return key.hashCode() * 31 +
            previousEntries.hashCode() * 31 +
            overlaidEntries.hashCode() * 31 +
            entry.hashCode() * 31 +
            modalBottomSheetProperties.hashCode() * 31
    }
}

/**
 * A [SceneStrategy] that displays entries that have added [bottomSheet] to their [NavEntry.metadata] within a
 * [ModalBottomSheet] instance.
 *
 * This strategy should always be added before any non-overlay scene strategies.
 */
@OptIn(ExperimentalMaterial3Api::class)
class BottomSheetSceneStrategy<T : Any> : SceneStrategy<T> {

    override fun SceneStrategyScope<T>.calculateScene(entries: List<NavEntry<T>>): Scene<T>? {
        val bottomSheetEntries = entries.trailingBottomSheetEntries()
        val lastEntry = bottomSheetEntries.lastOrNull()
        val bottomSheetProperties = lastEntry?.metadata?.get(BOTTOM_SHEET_KEY) as? ModalBottomSheetProperties
        val skipPartiallyExpanded = lastEntry?.metadata?.get(SKIP_PARTIALLY_EXPANDED_KEY) as? Boolean ?: false
        val handleContentDescription = lastEntry?.metadata?.get(HANDLE_CONTENT_DESCRIPTION_KEY) as? String ?: ""
        val showBetaLabel = lastEntry?.metadata?.get(SHOW_BETA_LABEL_KEY) as? Boolean ?: false
        val fullyExpandOnFirstOpen = lastEntry?.metadata?.get(EXPAND_ON_FIRST_OPEN_KEY) as? Boolean ?: false

        return bottomSheetProperties?.let { properties ->
            val underlyingEntries = entries.dropLast(bottomSheetEntries.size)
            BottomSheetScene(
                key = lastEntry.contentKey,
                previousEntries = underlyingEntries,
                overlaidEntries = underlyingEntries,
                entry = lastEntry,
                modalBottomSheetProperties = properties,
                skipPartiallyExpanded = skipPartiallyExpanded,
                showBetaLabel = showBetaLabel,
                fullyExpandOnFirstOpen = fullyExpandOnFirstOpen,
                onBack = onBack,
                handleContentDescription = handleContentDescription,
            )
        }
    }

    companion object {
        /**
         * Function to be called on the [NavEntry.metadata] to mark this entry as something that should be displayed
         * within a [ModalBottomSheet].
         *
         * @param skipPartiallyExpanded Whether to skip the partially expanded sheet state.
         * @param handleContentDescription Content description for the bottom sheet's drag handle.
         * @param modalBottomSheetProperties properties that should be passed to the containing [ModalBottomSheet].
         * @param showBetaLabel Whether to display the beta label next to the bottom sheet's drag handle
         * @param fullyExpandOnFirstOpen Whether to fully expand the bottom sheet on first open.
         */
        @OptIn(ExperimentalMaterial3Api::class)
        fun bottomSheet(
            skipPartiallyExpanded: Boolean = false,
            handleContentDescription: String,
            modalBottomSheetProperties: ModalBottomSheetProperties = ModalBottomSheetProperties(),
            showBetaLabel: Boolean = false,
            fullyExpandOnFirstOpen: Boolean = false,
        ): Map<String, Any> =
            mapOf(
                BOTTOM_SHEET_KEY to modalBottomSheetProperties,
                SKIP_PARTIALLY_EXPANDED_KEY to skipPartiallyExpanded,
                HANDLE_CONTENT_DESCRIPTION_KEY to handleContentDescription,
                SHOW_BETA_LABEL_KEY to showBetaLabel,
                EXPAND_ON_FIRST_OPEN_KEY to fullyExpandOnFirstOpen,
            )

        internal const val BOTTOM_SHEET_KEY = "bottom_sheet"
        private const val SKIP_PARTIALLY_EXPANDED_KEY = "skip_partially_expanded"
        private const val HANDLE_CONTENT_DESCRIPTION_KEY = "handle_content_description"
        private const val SHOW_BETA_LABEL_KEY = "show_beta_label"
        private const val EXPAND_ON_FIRST_OPEN_KEY = "expand_on_first_open"
    }
}

/**
 * Returns the sequence of trailing bottom sheet entries at the end of the back stack.
 *
 * For example, these back stacks would return the following bottom sheet entries:
 * - `Root, ExpandedTabGroup, EditTabGroup` returns `ExpandedTabGroup, EditTabGroup`
 * - `Root, TabSearch, AddToTabGroup` returns `AddToTabGroup`
 */
private fun <T : Any> List<NavEntry<T>>.trailingBottomSheetEntries(): List<NavEntry<T>> {
    val lastNonBottomSheetIndex = indexOfLast { entry ->
        entry.metadata[BottomSheetSceneStrategy.BOTTOM_SHEET_KEY] == null
    }
    val firstTrailingBottomSheetIndex = lastNonBottomSheetIndex + 1

    return subList(firstTrailingBottomSheetIndex, size)
}
