/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.session.storage.facts

import mozilla.components.browser.session.storage.facts.SessionStorageFacts.Items
import mozilla.components.browser.session.storage.facts.SessionStorageFacts.Metadata
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.Fact
import mozilla.components.support.base.facts.collect

/** Facts emitted for telemetry related to [mozilla.components.browser.session.storage.SessionStorage]. */
class SessionStorageFacts {
    /** Items that specify which session storage operation a fact is about. */
    object Items {
        const val RESTORE = "restore"
        const val SAVE = "save"
    }

    /** Metadata keys of [Items.RESTORE] and [Items.SAVE] facts. */
    object Metadata {
        const val DURATION_MS = "duration_ms"
        const val FILE_BYTES = "file_bytes"
        const val TABS_IN_FILE = "tabs_in_file"
        const val TABS_RESTORED = "tabs_restored"
    }
}

private fun emit(item: String, value: String? = null, metadata: Map<String, Any>? = null) =
    Fact(Component.BROWSER_SESSION_STORAGE, Action.IMPLEMENTATION_DETAIL, item, value, metadata).collect()

internal fun emitRestoreFact(durationMs: Long, fileBytes: Long, tabsInFile: Int, tabsRestored: Int) =
    emit(
        Items.RESTORE,
        metadata =
            mapOf(
                Metadata.DURATION_MS to durationMs,
                Metadata.FILE_BYTES to fileBytes,
                Metadata.TABS_IN_FILE to tabsInFile,
                Metadata.TABS_RESTORED to tabsRestored,
            ),
    )

internal fun emitSaveFact(durationMs: Long, fileBytes: Long, tabsInFile: Int) =
    emit(
        Items.SAVE,
        metadata =
            mapOf(
                Metadata.DURATION_MS to durationMs,
                Metadata.FILE_BYTES to fileBytes,
                Metadata.TABS_IN_FILE to tabsInFile,
            ),
    )
