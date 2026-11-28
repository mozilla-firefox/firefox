/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.usecases.fake

import android.net.Uri
import mozilla.components.concept.engine.prompt.ShareData
import org.mozilla.fenix.components.share.ShareSheetChooserAction
import org.mozilla.fenix.components.share.ShareSheetLauncher

class FakeShareSheetLauncher : ShareSheetLauncher {

    data class UrlShare(
        val id: String?,
        val longUrl: String,
        val title: String?,
        val isPrivate: Boolean,
        val isCustomTab: Boolean,
        val text: String = "",
        val subject: String? = null,
        val chooserActions: List<ShareSheetChooserAction> = listOf(),
    )

    data class ItemsShare(
        val items: List<ShareData>,
        val isPrivate: Boolean,
        val subject: String?,
        val chooserActions: List<ShareSheetChooserAction> = listOf(),
        val thumbnailUri: Uri? = null,
        val text: String? = null,
    )

    val urlShares: MutableList<UrlShare> = mutableListOf()
    val itemsShares: MutableList<ItemsShare> = mutableListOf()

    override fun showSystemShareSheet(
        id: String?,
        url: String,
        title: String?,
        text: String,
        subject: String?,
        isPrivate: Boolean,
        isCustomTab: Boolean,
        chooserActions: List<ShareSheetChooserAction>,
    ) {
        urlShares += UrlShare(id, url, title, isPrivate, isCustomTab, text, subject, chooserActions)
    }

    override fun showSystemShareSheet(
        items: List<ShareData>,
        isPrivate: Boolean,
        subject: String?,
        text: String?,
        chooserActions: List<ShareSheetChooserAction>,
        thumbnailUri: Uri?,
    ) {
        itemsShares += ItemsShare(items, isPrivate, subject, chooserActions, thumbnailUri, text)
    }
}
