/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.tooling.detekt.acorn

import io.gitlab.arturbosch.detekt.api.Config
import io.gitlab.arturbosch.detekt.api.RuleSet
import io.gitlab.arturbosch.detekt.api.RuleSetProvider

/**
 * Set of rules to enforce the Acorn design system.
 */
class AcornRuleSetProvider : RuleSetProvider {
    override val ruleSetId: String
        get() = "acorn-detekt-rules"

    override fun instance(config: Config): RuleSet = RuleSet(
        id = ruleSetId,
        rules = listOf(
            AcornPaddingRule(config),
        ),
    )
}
