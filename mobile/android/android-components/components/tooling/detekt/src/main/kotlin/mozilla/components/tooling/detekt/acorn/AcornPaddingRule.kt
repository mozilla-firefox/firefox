/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.tooling.detekt.acorn

import io.gitlab.arturbosch.detekt.api.CodeSmell
import io.gitlab.arturbosch.detekt.api.Config
import io.gitlab.arturbosch.detekt.api.Debt
import io.gitlab.arturbosch.detekt.api.Entity
import io.gitlab.arturbosch.detekt.api.Issue
import io.gitlab.arturbosch.detekt.api.Rule
import io.gitlab.arturbosch.detekt.api.Severity
import org.jetbrains.kotlin.psi.KtImportDirective
import org.jetbrains.kotlin.psi.KtValueArgument
import org.jetbrains.kotlin.psi.KtValueArgumentList
import org.jetbrains.kotlin.psi.psiUtil.getParentOfType

/**
 * Verifies that padding uses Acorn design system spacing tokens.
 */
class AcornPaddingRule(config: Config = Config.Companion.empty) : Rule(config) {
    private val namedParameterRegex = "^[a-zA-Z0-9]+ =".toRegex()
    private val dpRegex = ".[d,D]p$".toRegex()
    override val issue: Issue
        get() = Issue(
            id = "HardcodedPaddingUsage",
            severity = Severity.Maintainability,
            description = "Hardcoded Dp values are prohibited.  Please use Acorn spacing tokens.",
            debt = Debt.Companion.FIVE_MINS,
        )

    private val allowedValues: List<String>
        get() = valueOrDefault(
            "allowedValues",
            listOf("0", "0f", "1", "1f", "2", "2f"),
        )

    /**
     * Look for arguments for padding() or PaddingValues(), but exclude
     * padding it recursively calls PaddingValues to avoid duplicate reports.
     */
    private fun KtValueArgument.isPaddingArgument(): Boolean {
        return (this.parent.parent.firstChild.text == "padding" && !this.text.contains("PaddingValues")) ||
            this.parent.parent.firstChild.text == "PaddingValues"
    }

    override fun visitValueArgumentList(list: KtValueArgumentList) {
        super.visitValueArgumentList(list)
        // Skip import statements
        if (list.getParentOfType<KtImportDirective>(false) != null) return

        // Find a hardcoded Dp padding argument
        list.arguments.filter { it.text.contains(".Dp", ignoreCase = true) }.forEach {
            val value = it.text
                .replaceFirst(namedParameterRegex, "")
                .replaceFirst(dpRegex, "")
                .trim()
            if (it.isPaddingArgument() && value !in allowedValues) {
                report(
                    CodeSmell(
                        issue = issue,
                        entity = Entity.Companion.from(list),
                        message = "Hardcoded padding '$value.dp' detected",
                    ),
                )
            }
        }
    }
}
