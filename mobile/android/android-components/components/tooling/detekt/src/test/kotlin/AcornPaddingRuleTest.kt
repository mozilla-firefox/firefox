/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.tooling.detekt

import io.gitlab.arturbosch.detekt.test.lint
import junit.framework.TestCase.assertEquals
import mozilla.components.tooling.detekt.acorn.AcornPaddingRule
import org.junit.Assert
import org.junit.Test

class AcornPaddingRuleTest {

    @Test
    fun `WHEN padding is applied with Acorn spacing tokens THEN code is not flagged`() {
        val spacer = "Spacer(modifier = Modifier.padding(FirefoxTheme.layout.space.static100))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN padding is applied with raw Dp token THEN usage is flagged`() {
        val spacer = "Spacer(modifier = Modifier.padding(32.dp))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(1, findings.size)
        Assert.assertEquals(
            "Hardcoded padding '32.dp' detected",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN raw Dp padding is applied with vertical and horizontal parameters THEN code is flagged for each usage`() {
        val spacer = "Spacer(modifier = Modifier.padding(vertical = 32.dp, horizontal = 12.dp))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(2, findings.size)
        Assert.assertEquals(
            "Hardcoded padding '32.dp' detected",
            findings.first().message,
        )
        Assert.assertEquals(
            "Hardcoded padding '12.dp' detected",
            findings[1].message,
        )
    }

    @Test
    fun `WHEN raw Dp padding is applied with all parameter THEN usage is flagged`() {
        val spacer = "Spacer(modifier = Modifier.padding(all = 32.dp))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(1, findings.size)
        Assert.assertEquals(
            "Hardcoded padding '32.dp' detected",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN raw padding is applied with top and bottom parameters THEN each usage is flagged`() {
        val spacer =
            "Spacer(modifier = Modifier.padding(top = 5.dp, bottom = 10.dp, start = 20.dp, end = 25.dp))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(4, findings.size)
        Assert.assertEquals(
            "Hardcoded padding '5.dp' detected",
            findings.first().message,
        )
        Assert.assertEquals(
            "Hardcoded padding '10.dp' detected",
            findings[1].message,
        )
        Assert.assertEquals(
            "Hardcoded padding '20.dp' detected",
            findings[2].message,
        )
        Assert.assertEquals(
            "Hardcoded padding '25.dp' detected",
            findings[3].message,
        )
    }

    @Test
    fun `WHEN PaddingValues is constructed with raw Dp values THEN usage is flagged`() {
        val spacer =
            "Spacer(modifier = Modifier.padding(PaddingValues(4.dp)))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(1, findings.size)
        Assert.assertEquals(
            "Hardcoded padding '4.dp' detected",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN PaddingValues is constructed with raw Dp values in all parameter THEN usage is flagged`() {
        val spacer =
            "Spacer(modifier = Modifier.padding(PaddingValues(all = 4.dp))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(1, findings.size)
        Assert.assertEquals(
            "Hardcoded padding '4.dp' detected",
            findings.first().message,
        )
    }

    @Test
    fun `WHEN PaddingValues is constructed with horizontal or vertical parameters THEN each usage is flagged`() {
        val spacer =
            "Spacer(modifier = Modifier.padding(PaddingValues(horizontal = 4.dp, vertical = 10.dp))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(2, findings.size)
        Assert.assertEquals(
            "Hardcoded padding '4.dp' detected",
            findings.first().message,
        )
        Assert.assertEquals(
            "Hardcoded padding '10.dp' detected",
            findings[1].message,
        )
    }

    @Test
    fun `WHEN PaddingValues is constructed with start and end parameters THEN each usage is flagged`() {
        val spacer =
            "Spacer(modifier = Modifier.padding(PaddingValues(start = 5.dp, bottom = 10.dp, top = 15.dp, end = 20.dp))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(4, findings.size)
        Assert.assertEquals(
            "Hardcoded padding '5.dp' detected",
            findings.first().message,
        )
        Assert.assertEquals(
            "Hardcoded padding '10.dp' detected",
            findings[1].message,
        )
        Assert.assertEquals(
            "Hardcoded padding '15.dp' detected",
            findings[2].message,
        )
        Assert.assertEquals(
            "Hardcoded padding '20.dp' detected",
            findings[3].message,
        )
    }

    @Test
    fun `WHEN 0 dp is used as a padding parameter THEN code is not flagged`() {
        val spacer = "Spacer(modifier = Modifier.padding(0.dp))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN 0f dp is used as a padding parameter THEN code is not flagged`() {
        val spacer = "Spacer(modifier = Modifier.padding(0f.dp)"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN 1 dp is used as a padding parameter THEN code is not flagged`() {
        val spacer = "Spacer(modifier = Modifier.padding(1.dp))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN 1f dp is used as a padding parameter THEN code is not flagged`() {
        val spacer = "Spacer(modifier = Modifier.padding(1f.dp)"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN 2dp is used as a padding parameter THEN code is not flagged`() {
        val spacer = "Spacer(modifier = Modifier.padding(2.dp))"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(0, findings.size)
    }

    @Test
    fun `WHEN 2fdp is used as a padding parameter THEN code is not flagged`() {
        val spacer = "Spacer(modifier = Modifier.padding(2f.dp)"
        val code = fileContent(spacer)

        val findings = AcornPaddingRule().lint(code)

        assertEquals(0, findings.size)
    }
}

private fun fileContent(injectedComposableString: String) = """
    |package my.package
    |
    |/** My awesome class */
    |class ComposableClass () {
    |    @Composable
    |    fun FooScreen () {
    |       Column(
    |            modifier = modifier
    |                .wrapContentSize())
    |                {
    |                   $injectedComposableString
    |               }
    |      }
    |}
""".trimMargin()
