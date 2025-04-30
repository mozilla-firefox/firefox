/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.messaging

import android.content.Context
import android.os.Build
import androidx.core.app.NotificationManagerCompat
import mozilla.components.service.nimbus.messaging.JexlAttributeProvider
import mozilla.components.support.base.ext.areNotificationsEnabledSafe
import mozilla.components.support.utils.BrowsersCache
import org.json.JSONObject
import org.mozilla.fenix.components.metrics.UTMParams.Companion.UTM_CAMPAIGN
import org.mozilla.fenix.components.metrics.UTMParams.Companion.UTM_CONTENT
import org.mozilla.fenix.components.metrics.UTMParams.Companion.UTM_MEDIUM
import org.mozilla.fenix.components.metrics.UTMParams.Companion.UTM_SOURCE
import org.mozilla.fenix.components.metrics.UTMParams.Companion.UTM_TERM
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.utils.isLargeScreenSize
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Locale

/**
 * Custom attributes that the messaging framework will use to evaluate if message is eligible
 * to be shown.
 */
object CustomAttributeProvider : JexlAttributeProvider {
    private val formatter = SimpleDateFormat("yyyy-MM-dd", Locale.US)

    /**
     * Return a [JSONObject] of custom attributes used for experiment targeting.
     *
     * These are only evaluated right at the beginning of start up, so any first run experiments needing
     * targeting attributes which aren't set until after startup e.g. are_notifications_enabled
     * will unlikely to targeted as expected.
     *
     * IMPORTANT: Attributes added here is specifically for advanced targeting purposes within an
     * experiment.  Any modifications made here must also be reflected on the experimenter side
     * to ensure that the experimenter dashboard accurately displays the updated advanced targeting
     * options.
     *
     * WARNING: Attributes not initialized during the first startup cannot be used to target
     * first-run experiments. These target attributes will only become active after the second startup.
     */
    fun getCustomTargetingAttributes(context: Context): JSONObject {
        val settings = context.settings()
        val isFirstRun = settings.isFirstNimbusRun
        return JSONObject(
            mapOf(
                // By convention, we should use snake case.
                "is_first_run" to isFirstRun,
                "install_referrer_response_utm_source" to settings.utmSource,
                "install_referrer_response_utm_medium" to settings.utmMedium,
                "install_referrer_response_utm_campaign" to settings.utmCampaign,
                "install_referrer_response_utm_term" to settings.utmTerm,
                "install_referrer_response_utm_content" to settings.utmContent,
                "number_of_app_launches" to settings.numberOfAppLaunches,
                "is_large_device" to context.isLargeScreenSize(),
                // This camelCase attribute is a boolean value represented as a string.
                // This is left for backwards compatibility.
                "isFirstRun" to isFirstRun.toString(),
                "device_manufacturer" to Build.MANUFACTURER,
                "device_model" to Build.MODEL,
            ),
        )
    }

    /**
     * Returns a [JSONObject] that contains all the custom attributes, evaluated when the function
     * was called.
     *
     * This is used to drive display triggers of messages.
     *
     * IMPORTANT: Attributes added here are used as triggers in an experiment.
     */
    override fun getCustomAttributes(context: Context): JSONObject {
        val now = Calendar.getInstance()
        val settings = context.settings()
        return JSONObject(
            mapOf(
                "is_default_browser" to BrowsersCache.all(context).isDefaultBrowser,
                "date_string" to formatter.format(now.time),
                "number_of_app_launches" to settings.numberOfAppLaunches,

                "adjust_campaign" to settings.adjustCampaignId,
                "adjust_network" to settings.adjustNetwork,
                "adjust_ad_group" to settings.adjustAdGroup,
                "adjust_creative" to settings.adjustCreative,

                UTM_SOURCE to settings.utmSource,
                UTM_MEDIUM to settings.utmMedium,
                UTM_CAMPAIGN to settings.utmCampaign,
                UTM_TERM to settings.utmTerm,
                UTM_CONTENT to settings.utmContent,

                "are_notifications_enabled" to NotificationManagerCompat.from(context)
                    .areNotificationsEnabledSafe(),

                "search_widget_is_installed" to settings.searchWidgetInstalled,

                "android_version" to android.os.Build.VERSION.SDK_INT,

                "is_fxa_signed_in" to settings.signedInFxaAccount,

                "fxa_connected_devices" to (
                    context.components.backgroundServices.syncStore.state
                        .constellationState?.otherDevices?.size ?: 0
                    ),
            ),
        )
    }
}
