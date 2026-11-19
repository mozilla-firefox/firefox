/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.settings.privacy

import java.security.cert.X509Certificate
import mozilla.components.concept.engine.EngineSession

/**
 * Requests the Qualified Website Authentication Certificate (QWAC) status of a website once, and passes the result to
 * every observer, including observers added after it arrived.
 *
 * @param engineSession The [EngineSession] of the website, or null if there is none, in which case no result arrives.
 */
class QwacStatusRequest(engineSession: EngineSession?) {
    private var hasResult = false
    private var qwac: X509Certificate? = null
    private val observers = mutableListOf<(X509Certificate?) -> Unit>()

    init {
        engineSession?.qwacStatus { result ->
            qwac = result
            hasResult = true
            observers.forEach { it(result) }
            observers.clear()
        }
    }

    /**
     * Invokes [onResult] with the QWAC presented by the website, or null if it presents none, once the status is known.
     */
    fun observe(onResult: (X509Certificate?) -> Unit) {
        if (hasResult) onResult(qwac) else observers += onResult
    }
}
