/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import android.content.Context
import android.graphics.BitmapFactory
import android.net.Uri
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.mozilla.fenix.R
import org.mozilla.fenix.components.share.CacheHelper

private const val SHARE_THUMBNAIL_NAME = "wrexham_share_thumbnail"

/**
 * Saves the team badge to the cache to be used as the system share sheet preview thumbnail.
 *
 * @param context [Context] used to decode the badge and access the cache directory.
 * @return The content [Uri] of the thumbnail, or null if it could not be created.
 */
internal suspend fun createSportsWidgetShareThumbnail(context: Context): Uri? =
    withContext(Dispatchers.IO) {
        runCatching {
            CacheHelper()
                .saveBitmapToCache(
                    context = context,
                    bitmap = BitmapFactory.decodeResource(context.resources, R.drawable.team_badge_wrexham),
                    name = SHARE_THUMBNAIL_NAME,
                )
        }
            .getOrNull()
    }
