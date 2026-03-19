/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.ui.richtext

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.platform.UriHandler
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.ui.richtext.ir.RichDocument
import mozilla.components.ui.richtext.parsing.Parser
import mozilla.components.ui.richtext.rendering.Render
import kotlin.time.Duration.Companion.seconds

/**
 * Rich text rendering composable.
 *
 * @param text The text to render.
 * @param modifier The modifier to apply to this layout.
 * @param typography The typography to use.
 * @param colors The colors to use.
 * @param uriHandler The [UriHandler] to use for link clicks
 */
@Composable
fun RichText(
    text: String,
    modifier: Modifier = Modifier,
    typography: RichTextTypography = RichTextDefaults.typography(),
    colors: RichTextColors = RichTextDefaults.colors(),
    uriHandler: UriHandler = remember { NoOpUriHandler() },
) {
    val document = remember(text, typography) {
        Parser().parse(text)
    }
    RichText(
        document = document,
        modifier = modifier,
        typography = typography,
        colors = colors,
        uriHandler = uriHandler,
    )
}

/**
 * Rich text rendering composable.
 *
 * @param document The [RichDocument] to render.
 * @param modifier The modifier to apply to this layout.
 * @param typography The typography to use.
 * @param colors The colors to use.
 * @param uriHandler The [UriHandler] to use for link clicks
 */
@Composable
fun RichText(
    document: RichDocument,
    modifier: Modifier = Modifier,
    typography: RichTextTypography = RichTextDefaults.typography(),
    colors: RichTextColors = RichTextDefaults.colors(),
    uriHandler: UriHandler = remember(Unit) { NoOpUriHandler() },
) {
    CompositionLocalProvider(
        LocalRichTextColors provides colors,
        LocalRichTextTypography provides typography,
        LocalUriHandler provides uriHandler,
    ) {
        Column(
            modifier = modifier,
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            document.blocks.forEach { block ->
                key(block) {
                    block.Render()
                }
            }
        }
    }
}

private val SampleText = """
    # H1: Markdown Renderer `Test`
    This paragraph contains **bold text**, *italic text*, and ***bold italic***.
    It also includes `inline code`, a [simple link](https://example.com), a [complex **link**](https://mozilla.org),
    and a soft line break right here →
    still the same paragraph.
    ## H2: Lists
    ### H3: Unordered list
    - Plain item
    - Item with *emphasis* and **strong**
    - Item with a nested list:
      - Nested item one
      - Nested item with `code`
      - Nested item with `code` and a link: [Mozilla](https://mozilla.org)
      - Nested item with `code` and a link: [Mozilla](https://mozilla.org) and a very long line.
    ### H3: Ordered list
    1. First item
    2. Second item
       - Mixed nesting
       - With **bold text**
    3. Third item
    > Block quote
    >
    > First paragraph
    >
    > Second paragraph
""".trimIndent()

@PreviewLightDark
@Composable
private fun PreviewRichText() = AcornTheme {
    var visibleCharCount by remember { mutableStateOf(0) }

    val currentText = remember(visibleCharCount) {
        SampleText.substring(0, visibleCharCount)
    }

    LaunchedEffect(Unit) {
        // TODO remove: delay to wait for the layout inspector to get attached
        delay(10.seconds.inWholeMilliseconds)

        while (visibleCharCount < SampleText.length) {
            // randomly pick 20-35 characters
            val chunkSize = (20..35).random()

            // randomly wait 500ms - 1s
            val delayMs = (500L..1000L).random()

            visibleCharCount = (visibleCharCount + chunkSize).coerceAtMost(SampleText.length)
            delay(delayMs)
        }
    }

    val document = remember(currentText) {
        Parser().parse(currentText)
    }

    Scaffold {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(it)
                .padding(horizontal = 16.dp)
                .verticalScroll(rememberScrollState()),
            contentAlignment = Alignment.Center,
        ) {
            RichText(
                modifier = Modifier,
                document = document,
            )
        }
    }
}
