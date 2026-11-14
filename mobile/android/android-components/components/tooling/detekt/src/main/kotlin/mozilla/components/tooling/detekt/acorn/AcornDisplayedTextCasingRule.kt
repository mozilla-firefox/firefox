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
import org.jetbrains.kotlin.com.intellij.psi.PsiElement
import org.jetbrains.kotlin.psi.KtCallExpression
import org.jetbrains.kotlin.psi.KtCallableReferenceExpression
import org.jetbrains.kotlin.psi.KtQualifiedExpression
import org.jetbrains.kotlin.psi.KtValueArgument
import org.jetbrains.kotlin.psi.KtValueArgumentList

/**
 * Flags casing transformations applied to displayed text: anywhere inside the `text` argument of a Compose `Text()`
 * call, or as the whole value of a `text =` argument to any other call, such as `PrimaryButton(text =
 * label.uppercase())`.
 */
class AcornDisplayedTextCasingRule(config: Config = Config.empty) : Rule(config) {
    override val issue: Issue
        get() =
            Issue(
                id = "DisplayedTextCasing",
                severity = Severity.Defect,
                description =
                    "Automatic string casing transformations are prohibited because they result in incorrect " +
                        "localized values in some languages. Prefer defining your string in strings.xml as it " +
                        "should display to the user and allow the localization team to address on a per language " +
                        "basis.",
                debt = Debt.FIVE_MINS,
            )

    private val forbiddenCalls =
        setOf(
            "uppercase",
            "lowercase",
            "titlecase",
            "uppercaseChar",
            "lowercaseChar",
            "titlecaseChar",
            "capitalize",
            "decapitalize",
            "toUpperCase",
            "toLowerCase",
            "toTitleCase",
        )

    /** Report calls such as `Text(text = status.uppercase())` or `PrimaryButton(text = label.uppercase())`. */
    override fun visitCallExpression(expression: KtCallExpression) {
        super.visitCallExpression(expression)

        val name = expression.calleeExpression?.text ?: return
        if (name !in forbiddenCalls) return

        if (expression.isInsideTextArgumentOfText() || expression.isTextArgumentValue()) {
            reportCodeSmell(expression, name)
        }
    }

    /** Report references such as `Text(text = statuses.joinToString(transform = String::uppercase))`. */
    override fun visitCallableReferenceExpression(expression: KtCallableReferenceExpression) {
        super.visitCallableReferenceExpression(expression)

        val name = expression.callableReference.text
        if (name !in forbiddenCalls) return

        if (expression.isInsideTextArgumentOfText()) {
            reportCodeSmell(expression, name)
        }
    }

    /** True when this element is somewhere inside the `text` argument of an enclosing `Text()` call. */
    private fun PsiElement.isInsideTextArgumentOfText(): Boolean {
        for (argument in generateSequence(parent) { it.parent }.filterIsInstance<KtValueArgument>()) {
            val call = (argument.parent as? KtValueArgumentList)?.parent as? KtCallExpression ?: continue
            if (call.calleeExpression?.text == TEXT_COMPOSABLE) {
                return argument.isTextSlotOf(call)
            }
        }
        return false
    }

    private fun KtValueArgument.isTextSlotOf(call: KtCallExpression): Boolean {
        val argumentName = getArgumentName()?.asName?.asString() ?: return call.valueArguments.firstOrNull() == this
        return argumentName == TEXT_ARGUMENT
    }

    /** True when this call, including its receiver if it has one, is the entire value of a `text =` argument. */
    private fun KtCallExpression.isTextArgumentValue(): Boolean {
        val value = (parent as? KtQualifiedExpression)?.takeIf { it.selectorExpression == this } ?: this
        val argument = value.parent as? KtValueArgument ?: return false
        return argument.getArgumentExpression() == value &&
            argument.getArgumentName()?.asName?.asString() == TEXT_ARGUMENT
    }

    private fun reportCodeSmell(element: PsiElement, name: String) {
        report(
            CodeSmell(
                issue = issue,
                entity = Entity.from(element),
                message = "Avoid '$name()': casing transformations break localization.",
            )
        )
    }

    private companion object {
        const val TEXT_COMPOSABLE = "Text"
        const val TEXT_ARGUMENT = "text"
    }
}
