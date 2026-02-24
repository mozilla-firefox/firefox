/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.ui.richtext

import android.widget.Toast
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Scaffold
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.UriHandler
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.ui.richtext.parsing.Parser
import mozilla.components.ui.richtext.rendering.Render

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
    uriHandler: UriHandler = NoOpUriHandler(),
) {
    val document = remember(text, typography) {
        Parser().parse(text)
    }
    CompositionLocalProvider(
        LocalRichTextColors provides colors,
        LocalRichTextTypography provides typography,
        LocalUriHandler provides uriHandler,
    ) {
        LazyColumn(
            modifier = modifier,
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            items(document.blocks) { block ->
                block.Render()
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
    Scaffold {
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(it)
                .padding(horizontal = 16.dp)
                .verticalScroll(rememberScrollState()),
            contentAlignment = Alignment.Center,
        ) {
            val view = LocalView.current
            RichText(
                modifier = Modifier,
                text = SampleText,
                uriHandler = object : UriHandler {
                    override fun openUri(uri: String) {
                        Toast.makeText(view.context, uri, Toast.LENGTH_SHORT).show()
                    }
                },
            )
        }
    }
}
