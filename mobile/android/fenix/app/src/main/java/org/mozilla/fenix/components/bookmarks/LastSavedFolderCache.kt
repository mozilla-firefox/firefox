/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.bookmarks

import org.mozilla.fenix.utils.Settings

/**
 * An interface to store and retrieve the guid of the folder we last saved a bookmark in.
 */
interface LastSavedFolderCache {
    /**
     * Retrieves the guid of the folder we last saved a bookmark in.
     *
     * @return the guid as a string or null
     */
    suspend fun getGuid(): String?

    /**
     * Stores the guid of the folder we last saved a bookmark in.
     *
     * @param guid The guid.
     */
    suspend fun setGuid(guid: String?)
}

val Settings.lastSavedFolderCache: LastSavedFolderCache
    get() {
        return SettingsBackedLastSavedFolderCache(this)
    }

private class SettingsBackedLastSavedFolderCache(
    private val settings: Settings,
) : LastSavedFolderCache {
    override suspend fun getGuid(): String? {
        if (settings.lastSavedInFolderGuid.isEmpty()) {
            return null
        }

        return settings.lastSavedInFolderGuid
    }

    override suspend fun setGuid(guid: String?) {
        settings.lastSavedInFolderGuid = guid ?: ""
    }
}
