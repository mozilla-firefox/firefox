/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.tooling.detekt

import io.gitlab.arturbosch.detekt.test.lint
import junit.framework.TestCase.assertEquals
import mozilla.components.tooling.detekt.acorn.AcornDisplayedTextCasingRule
import org.junit.Test

class AcornDisplayedTextCasingRuleTest {
    @Test
    fun `WHEN uppercase is passed as the named text argument to a Text Composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusBadge(status: String) {
                StatusBadge {
                    Text(
                        text = status.uppercase(),
                        overflow = TextOverflow.Ellipsis,
                        maxLines = 1,
                    )
                }
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
        assertEquals(
            "Avoid 'uppercase()': casing transformations break localization.",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN lowercase is passed as the positional argument to a Text composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String) {
                Text(status.lowercase())
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
        assertEquals(
            "Avoid 'lowercase()': casing transformations break localization.",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN a stringResource is uppercased inside a Text composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun Badge() {
                Text(text = stringResource(R.string.new_badge).uppercase())
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
    }

    @Test
    fun `WHEN uppercase is called with a safe call inside a Text composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String?) {
                Text(text = status?.uppercase().orEmpty())
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
    }

    @Test
    fun `WHEN uppercase is called with an explicit Locale inside a Text composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String) {
                Text(text = status.uppercase(Locale.ROOT))
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
    }

    @Test
    fun `WHEN an uppercased value is concatenated inside a Text composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String, suffix: String) {
                Text(text = status.uppercase() + suffix)
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
    }

    @Test
    fun `WHEN the first character is titlecased with replaceFirstChar inside a Text composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String) {
                Text(text = status.replaceFirstChar { it.titlecase() })
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
        assertEquals(
            "Avoid 'titlecase()': casing transformations break localization.",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN capitalize is called inside a Text composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String) {
                Text(text = status.capitalize())
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
        assertEquals(
            "Avoid 'capitalize()': casing transformations break localization.",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN toUpperCase is called inside a Text composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String) {
                Text(text = status.toUpperCase())
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
    }

    @Test
    fun `WHEN casing transformations are chained inside a Text composable THEN each call is flagged once`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String) {
                Text(text = status.lowercase().uppercase())
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(2, findings.size)
    }

    @Test
    fun `WHEN several calls transform their text THEN each usage is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun Buttons() {
                Text(text = stringResource(R.string.positive_button).uppercase())
                Text(text = stringResource(R.string.negative_button).uppercase())
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(2, findings.size)
    }

    @Test
    fun `WHEN an uppercase function reference is used inside a Text composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabels(statuses: List<String>) {
                Text(text = statuses.joinToString(transform = String::uppercase))
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
        assertEquals(
            "Avoid 'uppercase()': casing transformations break localization.",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN a Text composable is instantiated without a casing transformation THEN it is not flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String) {
                Text(text = status.trim())
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN a value is uppercased outside Text THEN it is not flagged`() {
        val code =
            """
            package com.example
            fun label(status: String): String {
                val label = status.uppercase()
                return label
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN an enum name is lowercased for telemetry THEN it is not flagged`() {
        val code =
            """
            package com.example
            fun toggle(status: Status) {
                dispatch(ToggleCompleted(status = status.name.lowercase()))
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN a URL host is lowercased THEN it is not flagged`() {
        val code =
            """
            package com.example
            fun host(uri: Uri) = uri.host?.lowercase()
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN a casing transformation is in a Composable outside Text THEN it is not flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String) {
                val tag = status.lowercase()
                Text(text = status)
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN a casing transformation is in the modifier argument of a Text composable THEN it is not flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(label: String, id: String) {
                Text(text = label, modifier = Modifier.testTag(id.lowercase()))
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN a casing transformation is in a lambda argument of a Text composable THEN it is not flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(label: String) {
                Text(
                    text = label,
                    onTextLayout = { result -> log(result.toString().lowercase()) },
                )
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN a casing transformation is in a positional argument after text THEN it is not flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(label: String, id: String) {
                Text(label, Modifier.testTag(id.lowercase()))
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN the named text argument is not first in a Text composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String) {
                Text(modifier = Modifier, text = status.uppercase())
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
    }

    @Test
    fun `WHEN uppercase is called on an implicit receiver inside Text THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun StatusLabel(status: String) {
                Text(text = status.run { uppercase() })
                Text(text = with(status) { lowercase() })
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(2, findings.size)
        assertEquals(
            "Avoid 'uppercase()': casing transformations break localization.",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN a function named uppercase is called without a receiver inside Text THEN it is flagged`() {
        val code =
            """
            package com.example
            fun uppercase(): String = "LABEL"
            @Composable
            fun StatusLabel() {
                Text(text = uppercase())
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
    }

    @Test
    fun `WHEN a function reference named uppercase has no receiver inside Text THEN it is flagged`() {
        val code =
            """
            package com.example
            fun uppercase(value: String): String = value.uppercase()
            @Composable
            fun StatusLabels(statuses: List<String>) {
                Text(text = statuses.joinToString(transform = ::uppercase))
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
    }

    @Test
    fun `WHEN a function named uppercase is called without a receiver as a text argument THEN it is flagged`() {
        val code =
            """
            package com.example
            fun uppercase(): String = "LABEL"
            @Composable
            fun NewBadgeButton() {
                PrimaryButton(text = uppercase(), onClick = {})
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
    }

    @Test
    fun `WHEN uppercase is the value of a text argument to another composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun NewBadgeButton(label: String) {
                PrimaryButton(
                    text = label.uppercase(),
                    onClick = {},
                )
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
        assertEquals(
            "Avoid 'uppercase()': casing transformations break localization.",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN a stringResource is uppercased as a text argument to another composable THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun NewBadgeButton() {
                PrimaryButton(text = stringResource(R.string.new_badge).uppercase(), onClick = {})
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
    }

    @Test
    fun `WHEN a safe call uppercase is the value of a text argument THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun NewBadgeButton(label: String?) {
                PrimaryButton(text = label?.uppercase(), onClick = {})
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
    }

    @Test
    fun `WHEN capitalize is the value of a text argument THEN it is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun NewBadgeButton(label: String) {
                PrimaryButton(text = label.capitalize(), onClick = {})
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
        assertEquals(
            "Avoid 'capitalize()': casing transformations break localization.",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN casing transformations are chained as a text argument THEN only the outermost call is flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun NewBadgeButton(label: String) {
                PrimaryButton(text = label.lowercase().uppercase(), onClick = {})
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(1, findings.size)
        assertEquals(
            "Avoid 'uppercase()': casing transformations break localization.",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN a lowercased value is interpolated into a text argument THEN it is not flagged`() {
        val code =
            """
            package com.example
            fun startChunkGap(cause: Cause) {
                start(markerName = CHUNK_GAP, text = "cause=${'$'}{cause.name.lowercase()}")
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN an uppercased value is concatenated into a text argument outside Text THEN it is not flagged`() {
        val code =
            """
            package com.example
            @Composable
            fun NewBadgeButton(label: String, suffix: String) {
                PrimaryButton(text = label.uppercase() + suffix, onClick = {})
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN uppercase is the value of an argument not named text THEN it is not flagged`() {
        val code =
            """
            package com.example
            fun toggle(status: Status) {
                dispatch(ToggleCompleted(status = status.name.lowercase()))
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN uppercase is passed positionally to a call other than Text THEN it is not flagged`() {
        val code =
            """
            package com.example
            fun record(label: String) {
                advertiser.set(label.uppercase())
            }
            """
                .trimIndent()

        val findings = AcornDisplayedTextCasingRule().lint(code)

        assertEquals(0, findings.size)
    }
}
