/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.annotation.SuppressLint
import android.content.Context
import androidx.core.net.toUri
import mozilla.components.feature.push.AutoPushFeature
import mozilla.components.feature.push.Protocol
import mozilla.components.feature.push.PushConfig
import mozilla.components.lib.crash.CrashReporter
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.push.FirebasePushService

/**
 * Component group for push services. These components use services that strongly depend on push messaging (e.g.
 * WebPush, SendTab).
 */
class Push(val context: Context, crashReporter: CrashReporter) {
    val feature by lazy {
        pushConfig?.let { config ->
            AutoPushFeature(
                context = context,
                service = pushService,
                config = config,
                crashReporter = crashReporter,
            )
        }
    }

    private val pushConfig: PushConfig? by lazy {
        val logger = Logger("PushConfig")
        val projectIdKey = context.getString(R.string.pref_key_push_project_id)
        @SuppressLint("DiscouragedApi")
        val resId = context.resources.getIdentifier(projectIdKey, "string", context.packageName)
        if (resId == 0) {
            logger.warn("No firebase configuration found; cannot support push service.")
            return@lazy null
        }

        logger.debug("Creating push configuration for autopush.")
        val projectId = context.resources.getString(resId)
        val serverOverride = context.components.settings.overridePushServer
        if (serverOverride.isEmpty()) {
            PushConfig(projectId)
        } else {
            val uri = serverOverride.toUri()
            PushConfig(
                projectId,
                serverHost = uri.getHost() ?: "",
                protocol =
                    if (uri.getScheme() == "http") {
                        Protocol.HTTP
                    } else {
                        // Treat any non "http" value as HTTPS, since those are the only 2 options.
                        Protocol.HTTPS
                    },
            )
        }
    }

    private val pushService by lazy { FirebasePushService() }
}
