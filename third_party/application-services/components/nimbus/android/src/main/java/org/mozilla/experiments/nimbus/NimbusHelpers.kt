/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.experiments.nimbus

import org.json.JSONObject
import org.mozilla.experiments.nimbus.internal.NimbusStringHelperInterface
import org.mozilla.experiments.nimbus.internal.NimbusTargetingHelperInterface

/**
 * Instances of this class are useful for implementing a messaging service based upon
 * Nimbus.
 */
interface NimbusMessagingInterface {
    fun createMessageHelper(additionalContext: JSONObject? = null): NimbusMessagingHelperInterface =
        NimbusMessagingHelper(
            AlwaysFalseTargetingHelper(),
            NonStringHelper(),
        )

    val events: NimbusEventStore
}

typealias GleanPlumbInterface = NimbusMessagingInterface
typealias GleanPlumbMessageHelper = NimbusMessagingHelper

interface NimbusMessagingHelperInterface : NimbusTargetingHelperInterface, NimbusStringHelperInterface {
    /**
     * The backing native object needs to be cleaned up after use. This method fees the memory used
     * by Rust.
     *
     * Once this has been destroyed, then no other methods should be called.
     */
    fun destroy() = Unit

    /**
     * Clears the JEXL cache
     */
    fun clearCache() = Unit
}

/**
 * A helper object to make working with Strings uniform across multiple implementations of the messaging
 * system.
 *
 * This object provides access to a JEXL evaluator which runs against the same context as provided by
 * Nimbus targeting.
 *
 * It should also provide a similar function for String substitution, though this scheduled for EXP-2159.
 */
class NimbusMessagingHelper(
    private val targetingHelper: NimbusTargetingHelperInterface,
    private val stringHelper: NimbusStringHelperInterface,
    private val cache: MutableMap<String, Boolean> = mutableMapOf(),
) : NimbusStringHelperInterface by stringHelper, NimbusTargetingHelperInterface, NimbusMessagingHelperInterface {

    override fun evalJexl(expression: String): Boolean =
        cache.getOrPut(expression) {
            targetingHelper.evalJexl(expression)
        }

    override fun evalJexlDebug(expression: String): String =
        targetingHelper.evalJexlDebug(expression)

    override fun clearCache() = cache.clear()
}

internal class AlwaysFalseTargetingHelper : NimbusTargetingHelperInterface {
    override fun evalJexl(expression: String): Boolean = false

    override fun evalJexlDebug(expression: String): String =
        """{"success": false, "error": "Not implemented"}"""
}

internal class NonStringHelper : NimbusStringHelperInterface {
    override fun stringFormat(
        template: String,
        uuid: String?,
    ): String = template

    override fun getUuid(template: String) = null
}
