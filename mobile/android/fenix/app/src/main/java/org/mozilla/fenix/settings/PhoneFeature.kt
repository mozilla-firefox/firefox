/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.Manifest.permission.ACCESS_COARSE_LOCATION
import android.Manifest.permission.ACCESS_FINE_LOCATION
import android.Manifest.permission.RECORD_AUDIO
import android.content.Context
import android.os.Parcelable
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import kotlinx.parcelize.Parcelize
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.feature.sitepermissions.SitePermissionsRules
import mozilla.components.support.ktx.android.content.isPermissionGranted
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.getPreferenceKey
import org.mozilla.fenix.settings.sitepermissions.AUTOPLAY_ALLOW_ALL
import org.mozilla.fenix.settings.sitepermissions.AUTOPLAY_ALLOW_ON_WIFI
import org.mozilla.fenix.settings.sitepermissions.AUTOPLAY_BLOCK_ALL
import org.mozilla.fenix.settings.sitepermissions.AUTOPLAY_BLOCK_AUDIBLE
import org.mozilla.fenix.utils.Settings
import android.Manifest.permission.CAMERA as CAMERA_PERMISSION

@Parcelize
enum class PhoneFeature(val androidPermissionsList: Array<String>) : Parcelable {
    CAMERA(arrayOf(CAMERA_PERMISSION)),
    LOCATION(arrayOf(ACCESS_COARSE_LOCATION, ACCESS_FINE_LOCATION)),
    MICROPHONE(arrayOf(RECORD_AUDIO)),
    NOTIFICATION(emptyArray()),
    AUTOPLAY(emptyArray()),
    AUTOPLAY_AUDIBLE(emptyArray()),
    AUTOPLAY_INAUDIBLE(emptyArray()),
    PERSISTENT_STORAGE(emptyArray()),
    MEDIA_KEY_SYSTEM_ACCESS(emptyArray()),
    CROSS_ORIGIN_STORAGE_ACCESS(emptyArray()),
    ;

    fun isAndroidPermissionGranted(context: Context): Boolean {
        return context.isPermissionGranted(androidPermissionsList.asIterable())
    }

    @Suppress("ComplexMethod")
    fun getActionLabel(
        context: Context,
        sitePermissions: SitePermissions? = null,
        settings: Settings? = null,
    ): String {
        @StringRes val stringRes = if (isAndroidPermissionGranted(context)) {
            when (this) {
                AUTOPLAY_AUDIBLE ->
                    when (settings?.getAutoplayUserSetting() ?: AUTOPLAY_BLOCK_ALL) {
                        AUTOPLAY_ALLOW_ALL -> R.string.preference_option_autoplay_allowed2
                        AUTOPLAY_ALLOW_ON_WIFI -> R.string.preference_option_autoplay_allowed_wifi_only2
                        AUTOPLAY_BLOCK_AUDIBLE -> R.string.preference_option_autoplay_block_audio2
                        AUTOPLAY_BLOCK_ALL -> R.string.preference_option_autoplay_blocked3
                        else -> R.string.preference_option_autoplay_blocked3
                    }
                else -> when (getStatus(sitePermissions, settings)) {
                    SitePermissions.Status.BLOCKED -> R.string.preference_option_phone_feature_blocked
                    SitePermissions.Status.NO_DECISION -> R.string.preference_option_phone_feature_ask_to_allow
                    SitePermissions.Status.ALLOWED -> R.string.preference_option_phone_feature_allowed
                }
            }
        } else {
            R.string.phone_feature_blocked_by_android
        }
        return context.getString(stringRes)
    }

    fun getStatus(
        sitePermissions: SitePermissions? = null,
        settings: Settings? = null,
    ): SitePermissions.Status {
        val status = sitePermissions?.get(this) ?: settings?.let(::getAction)?.toStatus()
        return requireNotNull(status)
    }

    fun getLabel(context: Context): String {
        return when (this) {
            CAMERA -> context.getString(R.string.preference_phone_feature_camera)
            LOCATION -> context.getString(R.string.preference_phone_feature_location)
            MICROPHONE -> context.getString(R.string.preference_phone_feature_microphone)
            NOTIFICATION -> context.getString(R.string.preference_phone_feature_notification)
            PERSISTENT_STORAGE -> context.getString(R.string.preference_phone_feature_persistent_storage)
            CROSS_ORIGIN_STORAGE_ACCESS ->
                context.getString(R.string.preference_phone_feature_cross_origin_storage_access)
            MEDIA_KEY_SYSTEM_ACCESS -> context.getString(R.string.preference_phone_feature_media_key_system_access)
            AUTOPLAY, AUTOPLAY_AUDIBLE, AUTOPLAY_INAUDIBLE ->
                context.getString(R.string.preference_browser_feature_autoplay)
        }
    }

    /**
     * Returns the label ID representing the permission corresponding to this phone feature.
     */
    @StringRes
    fun getLabelId(): Int {
        return when (this) {
            CAMERA -> R.string.preference_phone_feature_camera
            LOCATION -> R.string.preference_phone_feature_location
            MICROPHONE -> R.string.preference_phone_feature_microphone
            NOTIFICATION -> R.string.preference_phone_feature_notification
            PERSISTENT_STORAGE -> R.string.preference_phone_feature_persistent_storage
            CROSS_ORIGIN_STORAGE_ACCESS -> R.string.preference_phone_feature_cross_origin_storage_access
            MEDIA_KEY_SYSTEM_ACCESS -> R.string.preference_phone_feature_media_key_system_access
            AUTOPLAY, AUTOPLAY_AUDIBLE, AUTOPLAY_INAUDIBLE -> R.string.preference_browser_feature_autoplay
        }
    }

    /**
     * Returns the icon ID representing the permission corresponding to this phone feature.
     */
    @DrawableRes
    fun getIconId(): Int {
        return when (this) {
            CAMERA -> R.drawable.mozac_ic_camera_24
            LOCATION -> R.drawable.mozac_ic_location_24
            MICROPHONE -> R.drawable.mozac_ic_microphone_24
            NOTIFICATION -> R.drawable.mozac_ic_notification_24
            PERSISTENT_STORAGE -> R.drawable.mozac_ic_storage_24
            CROSS_ORIGIN_STORAGE_ACCESS -> R.drawable.mozac_ic_cookies_24
            MEDIA_KEY_SYSTEM_ACCESS -> R.drawable.mozac_ic_link_24
            AUTOPLAY, AUTOPLAY_AUDIBLE, AUTOPLAY_INAUDIBLE -> R.drawable.mozac_ic_autoplay_24
        }
    }

    /**
     * Returns a resource ID from preference_keys representing the preference corresponding
     * to this phone feature.
     */
    @StringRes
    fun getPreferenceId(): Int {
        return when (this) {
            CAMERA -> R.string.pref_key_phone_feature_camera
            LOCATION -> R.string.pref_key_phone_feature_location
            MICROPHONE -> R.string.pref_key_phone_feature_microphone
            NOTIFICATION -> R.string.pref_key_phone_feature_notification
            AUTOPLAY -> R.string.pref_key_browser_feature_autoplay_v2
            AUTOPLAY_AUDIBLE -> R.string.pref_key_browser_feature_autoplay_audible_v2
            AUTOPLAY_INAUDIBLE -> R.string.pref_key_browser_feature_autoplay_inaudible_v2
            PERSISTENT_STORAGE -> R.string.pref_key_browser_feature_persistent_storage
            CROSS_ORIGIN_STORAGE_ACCESS -> R.string.pref_key_browser_feature_cross_origin_storage_access
            MEDIA_KEY_SYSTEM_ACCESS -> R.string.pref_key_browser_feature_media_key_system_access
        }
    }

    /**
     * Returns the key representing the preference corresponding to this phone feature.
     */
    fun getPreferenceKey(context: Context): String = context.getPreferenceKey(getPreferenceId())

    fun getAction(settings: Settings): SitePermissionsRules.Action =
        settings.getSitePermissionsPhoneFeatureAction(this, getDefault())

    private fun getDefault(): SitePermissionsRules.Action {
        return when (this) {
            AUTOPLAY_AUDIBLE -> SitePermissionsRules.Action.BLOCKED
            AUTOPLAY_INAUDIBLE -> SitePermissionsRules.Action.ALLOWED
            else -> SitePermissionsRules.Action.ASK_TO_ALLOW
        }
    }

    companion object {
        fun findFeatureBy(permissions: Array<out String>): PhoneFeature? {
            return values().find { feature ->
                feature.androidPermissionsList.any { permission ->
                    permission == permissions.first()
                }
            }
        }
    }
}
