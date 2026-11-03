/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import java.util.concurrent.ConcurrentHashMap

/** A simple controller for overriding behavior for specific tabs interacting with L2P. */
class ListenDebugController {
    // Written from the debug drawer and read where the article is extracted, which are different threads.
    private val extractionFailingTabIds = ConcurrentHashMap.newKeySet<String>()

    /** Makes every extraction of the article in [tabId] fail. */
    fun failExtractionFor(tabId: String) {
        extractionFailingTabIds.add(tabId)
    }

    /** Whether extraction of the article in [tabId] has been made to fail. */
    fun isExtractionFailing(tabId: String): Boolean = tabId in extractionFailingTabIds
}
