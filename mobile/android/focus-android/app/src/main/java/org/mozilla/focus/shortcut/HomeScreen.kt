/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.shortcut

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.text.TextUtils
import androidx.annotation.VisibleForTesting
import androidx.core.content.pm.ShortcutInfoCompat
import androidx.core.content.pm.ShortcutManagerCompat
import androidx.core.graphics.drawable.IconCompat
import androidx.core.net.toUri
import java.util.UUID
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import mozilla.components.support.ktx.kotlin.stripCommonSubdomains
import org.mozilla.focus.activity.MainActivity
import org.mozilla.focus.ext.components
import org.mozilla.focus.state.AppAction

/** Helper methods for adding shortcuts to device's home screen. */
object HomeScreen {
    const val ADD_TO_HOMESCREEN_TAG = "add_to_homescreen"
    private const val BLOCKING_ENABLED = "blocking_enabled"
    const val REQUEST_DESKTOP = "request_desktop"

    /** Checks if the launcher supports pinning shortcuts. */
    fun checkIfPinningSupported(
        context: Context,
        scope: CoroutineScope,
        ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    ) {
        if (context.components.appStore.state.isPinningSupported == null) {
            scope.launch {
                val isPinningSupported =
                    withContext(ioDispatcher) {
                        ShortcutManagerCompat.isRequestPinShortcutSupported(context)
                    }

                context.components.appStore.dispatch(AppAction.UpdateIsPinningSupported(isPinningSupported))
            }
        }
    }

    /** Create a shortcut for the given website on the device's home screen. */
    fun installShortCut(
        context: Context,
        icon: Bitmap,
        url: String,
        title: String,
        blockingEnabled: Boolean,
        requestDesktop: Boolean,
    ) {
        val shortcutTitle =
            if (TextUtils.isEmpty(title.trim())) {
                generateTitleFromUrl(url)
            } else {
                title
            }

        installShortCutViaManager(context, icon, url, shortcutTitle, blockingEnabled, requestDesktop)
    }

    /**
     * Create a shortcut via the [ShortcutManagerCompat].
     *
     * On Android 8+ the user will have the ability to add the shortcut manually or let the system place it
     * automatically.
     */
    private fun installShortCutViaManager(
        context: Context,
        bitmap: Bitmap,
        url: String,
        title: String,
        blockingEnabled: Boolean,
        requestDesktop: Boolean,
    ) {
        val icon = IconCompat.createWithAdaptiveBitmap(bitmap)
        val shortcut =
            ShortcutInfoCompat.Builder(context, UUID.randomUUID().toString())
                .setShortLabel(title)
                .setLongLabel(title)
                .setIcon(icon)
                .setIntent(createShortcutIntent(context, url, blockingEnabled, requestDesktop))
                .build()
        ShortcutManagerCompat.requestPinShortcut(context, shortcut, null)
    }

    private fun createShortcutIntent(
        context: Context,
        url: String,
        blockingEnabled: Boolean,
        requestDesktop: Boolean,
    ): Intent {
        val shortcutIntent = Intent(context, MainActivity::class.java)
        shortcutIntent.action = Intent.ACTION_VIEW
        shortcutIntent.data = url.toUri()
        shortcutIntent.putExtra(BLOCKING_ENABLED, blockingEnabled)
        shortcutIntent.putExtra(REQUEST_DESKTOP, requestDesktop)
        shortcutIntent.putExtra(ADD_TO_HOMESCREEN_TAG, ADD_TO_HOMESCREEN_TAG)
        return shortcutIntent
    }

    /** Generates a new default title based on the URL. */
    @VisibleForTesting
    fun generateTitleFromUrl(url: String): String {
        // For now we just use the host name and strip common subdomains like "www" or "m".
        return url.toUri().host?.stripCommonSubdomains() ?: ""
    }
}
