/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.top.sites.db

import androidx.room.ColumnInfo
import androidx.room.Entity
import androidx.room.PrimaryKey
import mozilla.components.feature.top.sites.TopSite

/** Internal entity representing a pinned site. */
@Entity(tableName = "top_sites")
internal data class PinnedSiteEntity(
    @PrimaryKey(autoGenerate = true) @ColumnInfo(name = "id") var id: Long? = null,
    @ColumnInfo(name = "title") var title: String,
    @ColumnInfo(name = "url") var url: String,
    @ColumnInfo(name = "is_default") var isDefault: Boolean = false,
    @ColumnInfo(name = "created_at") var createdAt: Long = System.currentTimeMillis(),
) {
    internal fun toTopSite(): TopSite =
        if (isDefault) {
            TopSite.DefaultPinned(
                id = id,
                title = title,
                url = url,
                createdAt = createdAt,
            )
        } else {
            TopSite.Pinned(
                id = id,
                title = title,
                url = url,
                createdAt = createdAt,
            )
        }
}

internal fun TopSite.toPinnedSite(): PinnedSiteEntity {
    return PinnedSiteEntity(
        id = id,
        title = title ?: "",
        url = url,
        isDefault = this is TopSite.DefaultPinned,
        createdAt = createdAt ?: 0,
    )
}
