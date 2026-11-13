/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.tooling.detekt.health

import io.gitlab.arturbosch.detekt.api.Detektion
import io.gitlab.arturbosch.detekt.api.OutputReport

/** A reporter that prints out an output file */
class SuppressionCountReport : OutputReport() {
    override val id: String
        get() = "suppression-count"

    override val ending: String
        get() = "txt"

    override val name = "Suppressions report"

    override fun render(detektion: Detektion): String? {
        val sb = StringBuilder("Suppression metrics:").appendLine().appendLine()

        detektion.getData(SuppressionCountProcessor.suppressionKey)?.entries?.forEach { (suppression, count) ->
            sb.appendLine("$suppression: $count")
        }

        return sb.toString()
    }
}
