/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons

import android.content.Context
import android.graphics.Bitmap
import android.icu.text.ListFormatter
import android.os.Build
import android.os.Parcelable
import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import java.text.ParseException
import java.text.SimpleDateFormat
import java.util.Locale
import java.util.TimeZone
import kotlinx.parcelize.Parcelize
import mozilla.components.concept.engine.webextension.Incognito
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.support.base.log.logger.Logger

typealias GeckoIncognito = Incognito

val logger = Logger("Addon")

/**
 * Represents an add-on based on the AMO store: https://addons.mozilla.org/en-US/firefox/
 *
 * @property id The unique ID of this add-on.
 * @property author Information about the add-on author.
 * @property downloadUrl The (absolute) URL to download the latest version of the add-on.
 * @property version The add-on version e.g "1.23.0".
 * @property permissions A single list with all the API and origin permissions for this add-on.
 * @property optionalPermissions Optional permissions requested or granted to this add-on.
 * @property optionalOrigins Optional origin permissions requested or granted to this add-on.
 * @property translatableName A map containing the different translations for the add-on name, where the key is the
 *   language and the value is the actual translated text.
 * @property translatableDescription A map containing the different translations for the add-on description, where the
 *   key is the language and the value is the actual translated text.
 * @property translatableSummary A map containing the different translations for the add-on name, where the key is the
 *   language and the value is the actual translated text.
 * @property iconUrl The URL to icon for the add-on.
 * @property homepageUrl The add-on homepage.
 * @property rating The rating information of this add-on.
 * @property createdAt The date the add-on was created.
 * @property updatedAt The date of the last time the add-on was updated by its developer(s).
 * @property icon the icon of the this [Addon], available when the icon is loaded.
 * @property installedState Holds the state of the installed web extension for this add-on. Null, if the [Addon] is not
 *   installed.
 * @property defaultLocale Indicates which locale will be always available to display translatable fields.
 * @property ratingUrl The link to the ratings page (user reviews) for this [Addon].
 * @property detailUrl The link to the detail page for this [Addon].
 * @property incognito Indicates how the extension works with private browsing windows.
 */
@Parcelize
data class Addon(
    val id: String,
    val author: Author? = null,
    val downloadUrl: String = "",
    val version: String = "",
    val permissions: List<String> = emptyList(),
    val optionalPermissions: List<Permission> = emptyList(),
    val optionalOrigins: List<Permission> = emptyList(),
    val requiredDataCollectionPermissions: List<String> = emptyList(),
    val optionalDataCollectionPermissions: List<Permission> = emptyList(),
    val translatableName: Map<String, String> = emptyMap(),
    val translatableDescription: Map<String, String> = emptyMap(),
    val translatableSummary: Map<String, String> = emptyMap(),
    val iconUrl: String = "",
    val homepageUrl: String = "",
    val rating: Rating? = null,
    val createdAt: String = "",
    val updatedAt: String = "",
    val icon: Bitmap? = null,
    val installedState: InstalledState? = null,
    val defaultLocale: String = DEFAULT_LOCALE,
    val ratingUrl: String = "",
    val detailUrl: String = "",
    val incognito: Incognito = Incognito.SPANNING,
) : Parcelable {

    /** Returns an icon for this [Addon], either from the [Addon] or [installedState]. */
    fun provideIcon(): Bitmap? {
        return icon ?: installedState?.icon
    }

    /**
     * Represents an add-on author.
     *
     * @property name The name of the author.
     * @property url The link to the profile page of the author.
     */
    @Parcelize
    data class Author(
        val name: String,
        val url: String,
    ) : Parcelable

    /**
     * Holds all the rating information of this add-on.
     *
     * @property average An average score from 1 to 5 of how users scored this add-on.
     * @property reviews The number of users that has scored this add-on.
     */
    @Parcelize
    data class Rating(
        val average: Float,
        val reviews: Int,
    ) : Parcelable

    /**
     * Required or optional permission.
     *
     * @property name The name of this permission.
     * @property granted Indicate if this permission is granted or not.
     */
    @Parcelize
    data class Permission(
        val name: String,
        val granted: Boolean,
    ) : Parcelable

    /**
     * Localized permission from [Permission]
     *
     * @property localizedName The localized name of the permission to show in the UI.
     * @property permission The [Permission] that was localized.
     */
    @Parcelize
    data class LocalizedPermission(
        val localizedName: String,
        val permission: Permission,
    ) : Parcelable

    /**
     * Returns a list of id resources per each item on the [Addon.permissions] list. Holds the state of the installed
     * web extension of this add-on.
     *
     * @property id The ID of the installed web extension.
     * @property version The installed version.
     * @property enabled Indicates if this [Addon] is enabled to interact with web content or not, defaults to false.
     * @property supported Indicates if this [Addon] is supported by the browser or not, defaults to true.
     * @property disabledReason Indicates why the [Addon] was disabled.
     * @property optionsPageUrl the URL of the page displaying the options page (options_ui in the extension's
     *   manifest).
     * @property allowedInPrivateBrowsing true if this addon should be allowed to run in private browsing pages, false
     *   otherwise.
     * @property icon the icon of the installed extension.
     */
    @Parcelize
    data class InstalledState(
        val id: String,
        val version: String,
        val optionsPageUrl: String?,
        val openOptionsPageInTab: Boolean = false,
        val enabled: Boolean = false,
        val supported: Boolean = true,
        val disabledReason: DisabledReason? = null,
        val allowedInPrivateBrowsing: Boolean = false,
        val icon: Bitmap? = null,
    ) : Parcelable

    /** Enum containing all reasons why an [Addon] was disabled. */
    enum class DisabledReason {

        /** The [Addon] was disabled because it is unsupported. */
        UNSUPPORTED,

        /** The [Addon] was disabled because is it is blocklisted. */
        BLOCKLISTED,

        /** The [Addon] was disabled by the user. */
        USER_REQUESTED,

        /** The [Addon] was disabled because it isn't correctly signed. */
        NOT_CORRECTLY_SIGNED,

        /** The [Addon] was disabled because it isn't compatible with the application version. */
        INCOMPATIBLE,

        /** The [Addon] was disabled because it is soft-blocked. */
        SOFT_BLOCKED,
    }

    /** Incognito values that control how an [Addon] works with private browsing windows. */
    enum class Incognito {
        /** The [Addon] will see events from private and non-private windows and tabs. */
        SPANNING,

        /** The [Addon] will be split between private and non-private windows. */
        SPLIT,

        /** Private tabs and windows are invisible to the [Addon]. */
        NOT_ALLOWED,
    }

    /**
     * Returns a list of localized Strings per each item on the [permissions] list.
     *
     * @param context A context reference.
     */
    fun translatePermissions(context: Context): List<String> {
        return localizePermissions(permissions, context)
    }

    /**
     * Returns a [LocalizedPermission] list of the optional permissions.
     *
     * @param context Context for resource lookup
     */
    fun translateOptionalPermissions(context: Context): List<LocalizedPermission> {
        return localizeOptionalPermissions(optionalPermissions, context)
    }

    /**
     * Returns a list of localized Strings for each of the required data collection permissions.
     *
     * @param context Context for resource lookup
     */
    fun translateRequiredDataCollectionPermissions(context: Context): List<String> {
        return localizeDataCollectionPermissions(requiredDataCollectionPermissions, context)
    }

    /**
     * Returns a list of [LocalizedPermission] for each of the optional data collection permissions.
     *
     * @param context Context for resource lookup
     */
    fun translateOptionalDataCollectionPermissions(context: Context): List<LocalizedPermission> {
        return localizeOptionalDataCollectionPermissions(optionalDataCollectionPermissions, context)
    }

    /** Returns whether or not this [Addon] is currently installed. */
    fun isInstalled() = installedState != null

    /** Returns whether or not this [Addon] is currently enabled. */
    fun isEnabled() = installedState?.enabled == true

    /** Returns whether or not this [Addon] is currently supported by the browser. */
    fun isSupported() = installedState?.supported == true

    /**
     * Returns whether or not this [Addon] is currently disabled because it is not supported. This is based on the
     * installed extension state in the engine. An addon can be disabled as unsupported and later become supported, so
     * both [isSupported] and [isDisabledAsUnsupported] can be true.
     */
    fun isDisabledAsUnsupported() = installedState?.disabledReason == DisabledReason.UNSUPPORTED

    /**
     * Returns whether or not this [Addon] is currently disabled because it is part of the blocklist. This is based on
     * the installed extension state in the engine.
     */
    fun isDisabledAsBlocklisted() = installedState?.disabledReason == DisabledReason.BLOCKLISTED

    /**
     * Returns whether this [Addon] is currently soft-blocked. While we're cheking the disabled reason, the user still
     * has the opportunity to re-enable the [Addon].
     */
    fun isSoftBlocked() = installedState?.disabledReason == DisabledReason.SOFT_BLOCKED

    /** Returns whether this [Addon] is currently disabled because it isn't correctly signed. */
    fun isDisabledAsNotCorrectlySigned() = installedState?.disabledReason == DisabledReason.NOT_CORRECTLY_SIGNED

    /** Returns whether this [Addon] is currently disabled because it isn't compatible with the application version. */
    fun isDisabledAsIncompatible() = installedState?.disabledReason == DisabledReason.INCOMPATIBLE

    /** Returns whether or not this [Addon] is allowed in private browsing mode. */
    fun isAllowedInPrivateBrowsing() = installedState?.allowedInPrivateBrowsing == true

    /**
     * Returns a copy of this [Addon] containing only translations (description, name, summary) of the provided locales.
     * All other translations except the [defaultLocale] will be removed.
     *
     * @param locales list of locales to keep.
     * @return copy of the addon with all other translations removed.
     */
    fun filterTranslations(locales: List<String>): Addon {
        val internalLocales = locales + defaultLocale
        val descriptions = translatableDescription.filterKeys { internalLocales.contains(it) }
        val names = translatableName.filterKeys { internalLocales.contains(it) }
        val summaries = translatableSummary.filterKeys { internalLocales.contains(it) }
        return copy(
            translatableName = names,
            translatableDescription = descriptions,
            translatableSummary = summaries,
        )
    }

    companion object {
        /** A map of permissions to translation string ids. */
        @Suppress("MaxLineLength")
        val permissionToTranslation =
            mapOf(
                "<all_urls>" to R.string.mozac_feature_addons_permissions_all_urls_description,
                "bookmarks" to R.string.mozac_feature_addons_permissions_bookmarks_description,
                "browserSettings" to R.string.mozac_feature_addons_permissions_browser_setting_description,
                "browsingData" to R.string.mozac_feature_addons_permissions_browser_data_description,
                "clipboardRead" to R.string.mozac_feature_addons_permissions_clipboard_read_description,
                "clipboardWrite" to R.string.mozac_feature_addons_permissions_clipboard_write_description,
                "declarativeNetRequest" to
                    R.string.mozac_feature_addons_permissions_declarative_net_request_description,
                "declarativeNetRequestFeedback" to
                    R.string.mozac_feature_addons_permissions_declarative_net_request_feedback_description,
                "devtools" to R.string.mozac_feature_addons_permissions_devtools_description,
                "downloads" to R.string.mozac_feature_addons_permissions_downloads_description,
                "downloads.open" to R.string.mozac_feature_addons_permissions_downloads_open_description,
                "find" to R.string.mozac_feature_addons_permissions_find_description,
                "geolocation" to R.string.mozac_feature_addons_permissions_geolocation_description,
                "history" to R.string.mozac_feature_addons_permissions_history_description,
                "management" to R.string.mozac_feature_addons_permissions_management_description,
                "nativeMessaging" to R.string.mozac_feature_addons_permissions_native_messaging_description,
                "notifications" to R.string.mozac_feature_addons_permissions_notifications_description,
                "pkcs11" to R.string.mozac_feature_addons_permissions_pkcs11_description,
                "privacy" to R.string.mozac_feature_addons_permissions_privacy_description,
                "proxy" to R.string.mozac_feature_addons_permissions_proxy_description,
                "sessions" to R.string.mozac_feature_addons_permissions_sessions_description,
                "tabHide" to R.string.mozac_feature_addons_permissions_tab_hide_description,
                "tabs" to R.string.mozac_feature_addons_permissions_tabs_description,
                "topSites" to R.string.mozac_feature_addons_permissions_top_sites_description,
                "trialML" to R.string.mozac_feature_addons_permissions_trial_ml_description,
                "userScripts" to R.string.mozac_feature_addons_permissions_user_scripts_description,
                "webNavigation" to R.string.mozac_feature_addons_permissions_web_navigation_description,
            )

        /**
         * A map of data collection permissions to short translation string ids. This should be kept in sync with
         * `DATA_COLLECTION_PERMISSIONS` in `ExtensionPermissionMessages.sys.mjs`.
         */
        @Suppress("MaxLineLength")
        private val dataCollectionPermissionToShortTranslation =
            mapOf(
                "authenticationInfo" to
                    R.string.mozac_feature_addons_permissions_data_collection_authenticationInfo_short_description,
                "bookmarksInfo" to
                    R.string.mozac_feature_addons_permissions_data_collection_bookmarksInfo_short_description,
                "browsingActivity" to
                    R.string.mozac_feature_addons_permissions_data_collection_browsingActivity_short_description,
                "financialAndPaymentInfo" to
                    R.string.mozac_feature_addons_permissions_data_collection_financialAndPaymentInfo_short_description,
                "healthInfo" to R.string.mozac_feature_addons_permissions_data_collection_healthInfo_short_description,
                "locationInfo" to
                    R.string.mozac_feature_addons_permissions_data_collection_locationInfo_short_description,
                "personalCommunications" to
                    R.string.mozac_feature_addons_permissions_data_collection_personalCommunications_short_description,
                "personallyIdentifyingInfo" to
                    R.string
                        .mozac_feature_addons_permissions_data_collection_personallyIdentifyingInfo_short_description,
                "searchTerms" to
                    R.string.mozac_feature_addons_permissions_data_collection_searchTerms_short_description,
                "technicalAndInteraction" to
                    R.string.mozac_feature_addons_permissions_data_collection_technicalAndInteraction_short_description,
                "websiteActivity" to
                    R.string.mozac_feature_addons_permissions_data_collection_websiteActivity_short_description,
                "websiteContent" to
                    R.string.mozac_feature_addons_permissions_data_collection_websiteContent_short_description,
            )

        /**
         * A map of data collection permissions to long translation string ids. This should be kept in sync with
         * `DATA_COLLECTION_PERMISSIONS` in `ExtensionPermissionMessages.sys.mjs`.
         */
        @Suppress("MaxLineLength")
        private val dataCollectionPermissionToLongTranslation =
            mapOf(
                "authenticationInfo" to
                    R.string.mozac_feature_addons_permissions_data_collection_authenticationInfo_long_description,
                "bookmarksInfo" to
                    R.string.mozac_feature_addons_permissions_data_collection_bookmarksInfo_long_description,
                "browsingActivity" to
                    R.string.mozac_feature_addons_permissions_data_collection_browsingActivity_long_description,
                "financialAndPaymentInfo" to
                    R.string.mozac_feature_addons_permissions_data_collection_financialAndPaymentInfo_long_description,
                "healthInfo" to R.string.mozac_feature_addons_permissions_data_collection_healthInfo_long_description,
                "locationInfo" to
                    R.string.mozac_feature_addons_permissions_data_collection_locationInfo_long_description,
                "personalCommunications" to
                    R.string.mozac_feature_addons_permissions_data_collection_personalCommunications_long_description,
                "personallyIdentifyingInfo" to
                    R.string
                        .mozac_feature_addons_permissions_data_collection_personallyIdentifyingInfo_long_description,
                "searchTerms" to R.string.mozac_feature_addons_permissions_data_collection_searchTerms_long_description,
                "technicalAndInteraction" to
                    R.string.mozac_feature_addons_permissions_data_collection_technicalAndInteraction_long_description,
                "websiteActivity" to
                    R.string.mozac_feature_addons_permissions_data_collection_websiteActivity_long_description,
                "websiteContent" to
                    R.string.mozac_feature_addons_permissions_data_collection_websiteContent_long_description,
            )

        /**
         * Takes a list of [permissions] and returns a list of id resources per each item.
         *
         * @param permissions The list of permissions to be localized. Valid permissions can be found in
         *   https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/permissions#API_permissions
         * @param context The application context used to access the string resources.
         */
        fun localizePermissions(permissions: List<String>, context: Context): List<String> {
            var localizedUrlAccessPermissions = emptyList<String>()
            val requireAllUrlsAccess = permissions.contains("<all_urls>")
            val notFoundPermissions = mutableListOf<String>()

            val localizedNormalPermissions =
                permissions
                    .mapNotNull {
                        val id = permissionToTranslation[it]
                        if (id == null) notFoundPermissions.add(it)
                        id
                    }
                    .map { context.getString(it) }

            if (!requireAllUrlsAccess && notFoundPermissions.isNotEmpty()) {
                localizedUrlAccessPermissions = localizeURLAccessPermissions(context, notFoundPermissions)
            }

            return localizedNormalPermissions + localizedUrlAccessPermissions
        }

        /**
         * Takes a list of data collection [permissions] and returns a list of localized strings.
         *
         * @param permissions The list of data collection permissions to be localized.
         */
        fun localizeDataCollectionPermissions(permissions: List<String>, context: Context): List<String> {
            return permissions
                .mapNotNull {
                    dataCollectionPermissionToShortTranslation[it]
                }
                .map { context.getString(it) }
        }

        /**
         * Takes a list of optional data collection [permissions] and returns a list of [LocalizedPermission].
         *
         * @param permissions The list of optional data collection permissions to be localized.
         * @param context The context for resource lookup.
         */
        fun localizeOptionalDataCollectionPermissions(
            permissions: List<Permission>,
            context: Context,
        ): List<LocalizedPermission> {
            return permissions.mapNotNull {
                val resourceId = dataCollectionPermissionToLongTranslation[it.name]
                if (resourceId != null) {
                    LocalizedPermission(context.getString(resourceId), it)
                } else {
                    null
                }
            }
        }

        /**
         * Takes a list of localized permission [String] values and formats it to return a single string.
         *
         * We want to render the list of data collection permissions as a sentence in the UI. The localized string
         * expects a unique string parameter that is a formatted list of permission names. For example:
         * ```
         * The developer says this extension collects: x, y, z
         * ```
         *
         * We have to account for a fairly limited API prior to API level 33. That essentially means:
         *
         * - For API level 33 and above (TIRAMISU), we will return `x, y, z` because we use the "AND" type and the
         *   "NARROW" width.
         *
         * - For API level below 33, we will use the list formatter that is configured with the "AND" type (good) and
         *   the "WIDE" width (not ideal). We will therefore return `x, y and z` for the same list of permissions. It's
         *   still better to use a list formatter for localization.
         *
         * @param localizedPermissions The list of localized permission [String]
         */
        fun formatLocalizedDataCollectionPermissions(localizedPermissions: List<String>): String {
            val formattedList =
                when {
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU -> {
                        ListFormatter.getInstance(
                                Locale.getDefault(),
                                ListFormatter.Type.AND,
                                ListFormatter.Width.NARROW,
                            )
                            .format(localizedPermissions)
                    }
                    else -> {
                        ListFormatter.getInstance(Locale.getDefault()).format(localizedPermissions)
                    }
                }
            return formattedList
        }

        /**
         * Takes a list of optional permissions and returns the list of [LocalizedPermission].
         *
         * @param optionalPermissions The list of optional permissions
         * @param context The context for resource lookup
         */
        fun localizeOptionalPermissions(
            optionalPermissions: List<Permission>,
            context: Context,
        ): List<LocalizedPermission> {
            var allUrlAccessPermissionFound = false
            val notFoundPermissions = mutableListOf<Permission>()
            val localizedURLAccessPermissions = mutableListOf<LocalizedPermission>()

            val localizedOptionalPermissions: List<LocalizedPermission> = optionalPermissions.mapNotNull {
                val resourceId = permissionToTranslation[it.name]
                if (resourceId != null) {
                    if (resourceId.isAllURLsPermission()) allUrlAccessPermissionFound = true
                    LocalizedPermission(context.getString(resourceId), it)
                } else {
                    notFoundPermissions.add(it)
                    null
                }
            }

            if (!allUrlAccessPermissionFound && notFoundPermissions.isNotEmpty()) {
                notFoundPermissions.mapNotNullTo(localizedURLAccessPermissions) { permission ->
                    when (val localizedResourceId = getStringIdForHostPermission(permission.name)) {
                        null -> {
                            // Hide if we can't find a string resource to localize the permission
                            null
                        }

                        else -> {
                            val localizedName = context.getString(localizedResourceId)
                            LocalizedPermission(localizedName, permission)
                        }
                    }
                }
            }

            return localizedOptionalPermissions + localizedURLAccessPermissions
        }

        /**
         * Creates an [Addon] object from a [WebExtension] one. The resulting object might have an installed state when
         * the second method's argument is used.
         *
         * @param extension a WebExtension instance.
         * @param installedState optional - an installed state.
         */
        fun newFromWebExtension(
            extension: WebExtension,
            installedState: InstalledState? = null,
        ): Addon {
            val metadata = extension.getMetadata()
            val name = metadata?.name ?: extension.id
            val description = metadata?.description ?: extension.id
            val permissions = metadata?.requiredPermissions.orEmpty() + metadata?.requiredOrigins.orEmpty()
            val averageRating = metadata?.averageRating ?: 0f
            val reviewCount = metadata?.reviewCount ?: 0
            val homepageUrl = metadata?.homepageUrl.orEmpty()
            val ratingUrl = metadata?.reviewUrl.orEmpty()
            val developerName = metadata?.developerName.orEmpty()
            val author =
                if (developerName.isNotBlank()) {
                    Author(name = developerName, url = metadata?.developerUrl.orEmpty())
                } else {
                    null
                }
            val detailUrl = metadata?.detailUrl.orEmpty()
            val incognito =
                when (metadata?.incognito) {
                    GeckoIncognito.NOT_ALLOWED -> Incognito.NOT_ALLOWED
                    GeckoIncognito.SPLIT -> Incognito.SPLIT
                    else -> Incognito.SPANNING
                }

            val grantedOptionalPermissions = metadata?.grantedOptionalPermissions ?: emptyList()
            val optionalPermissions =
                metadata?.optionalPermissions?.map { permission ->
                    Permission(
                        name = permission,
                        granted = grantedOptionalPermissions.contains(permission),
                    )
                } ?: emptyList()

            val allOrigins = metadata?.optionalOrigins?.toMutableSet() ?: mutableSetOf()
            val grantedOptionalOrigins = metadata?.grantedOptionalOrigins ?: emptyList()
            allOrigins.addAll(grantedOptionalOrigins)
            val optionalOrigins = allOrigins.map { origin ->
                Permission(
                    name = origin,
                    granted = grantedOptionalOrigins.contains(origin),
                )
            }

            val requiredDataCollectionPermissions = metadata?.requiredDataCollectionPermissions ?: emptyList()
            val grantedOptionalDataCollectionPermissions =
                metadata?.grantedOptionalDataCollectionPermissions ?: emptyList()
            val optionalDataCollectionPermissions =
                metadata?.optionalDataCollectionPermissions?.map { permission ->
                    Permission(
                        name = permission,
                        granted = grantedOptionalDataCollectionPermissions.contains(permission),
                    )
                } ?: emptyList()

            return Addon(
                id = extension.id,
                author = author,
                version = metadata?.version.orEmpty(),
                permissions = permissions,
                optionalPermissions = optionalPermissions,
                optionalOrigins = optionalOrigins,
                requiredDataCollectionPermissions = requiredDataCollectionPermissions,
                optionalDataCollectionPermissions = optionalDataCollectionPermissions,
                downloadUrl = metadata?.downloadUrl.orEmpty(),
                rating = Rating(averageRating, reviewCount),
                homepageUrl = homepageUrl,
                translatableName = mapOf(DEFAULT_LOCALE to name),
                translatableDescription = mapOf(DEFAULT_LOCALE to metadata?.fullDescription.orEmpty()),
                // We don't have a summary when we create an add-on from a WebExtension instance so let's
                // re-use description...
                translatableSummary = mapOf(DEFAULT_LOCALE to description),
                updatedAt = fromMetadataToAddonDate(metadata?.updateDate.orEmpty()),
                ratingUrl = ratingUrl,
                detailUrl = detailUrl,
                incognito = incognito,
                installedState = installedState,
            )
        }

        /**
         * Returns a new [String] formatted in "yyyy-MM-dd'T'HH:mm:ss'Z'". [Metadata] uses
         * "yyyy-MM-dd'T'HH:mm:ss.SSS'Z'" which is in simplified 8601 format while [Addon] uses
         * "yyyy-MM-dd'T'HH:mm:ss'Z'"
         *
         * @param inputDate The string data to be formatted.
         */
        @VisibleForTesting
        internal fun fromMetadataToAddonDate(inputDate: String): String {
            val updatedAt: String =
                try {
                    val zone = TimeZone.getTimeZone("GMT")
                    val metadataFormat =
                        SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", Locale.ROOT).apply {
                            timeZone = zone
                        }
                    val addonFormat =
                        SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.ROOT).apply {
                            timeZone = zone
                        }
                    val formattedDate = metadataFormat.parse(inputDate)

                    if (formattedDate !== null) {
                        addonFormat.format(formattedDate)
                    } else {
                        ""
                    }
                } catch (e: ParseException) {
                    logger.error("Unable to format $inputDate", e)
                    ""
                }
            return updatedAt
        }

        internal fun localizeURLAccessPermissions(context: Context, accessPermissions: List<String>): List<String> {
            val localizedSiteAccessPermissions = mutableListOf<String>()
            val permissionsToTranslations = mutableMapOf<String, Int>()

            accessPermissions.forEach { permission ->
                val id = getStringIdForHostPermission(permission)
                if (id != null) {
                    permissionsToTranslations[permission] = id
                }
            }

            if (permissionsToTranslations.values.any { it.isAllURLsPermission() }) {
                localizedSiteAccessPermissions.add(
                    context.getString(R.string.mozac_feature_addons_permissions_all_urls_description)
                )
            } else {
                formatURLAccessPermission(permissionsToTranslations, localizedSiteAccessPermissions, context)
            }

            return localizedSiteAccessPermissions
        }

        @Suppress("MagicNumber", "CognitiveComplexMethod", "CyclomaticComplexMethod")
        private fun formatURLAccessPermission(
            permissionsToTranslations: MutableMap<String, Int>,
            localizedSiteAccessPermissions: MutableList<String>,
            context: Context,
        ) {
            val maxShownPermissionsEntries = 4
            fun addExtraEntriesIfNeeded(collapsedPermissions: Int, oneExtraPermission: Int, multiplePermissions: Int) {
                if (collapsedPermissions == 1) {
                    localizedSiteAccessPermissions.add(context.getString(oneExtraPermission))
                } else {
                    localizedSiteAccessPermissions.add(context.getString(multiplePermissions))
                }
            }

            var domainCount = 0
            var siteCount = 0

            loop@ for ((permission, stringId) in permissionsToTranslations) {
                var host = permission.toUri().host ?: ""
                when {
                    stringId.isDomainAccessPermission() -> {
                        ++domainCount
                        host = host.removePrefix("*.")

                        if (domainCount > maxShownPermissionsEntries) continue@loop
                    }

                    stringId.isSiteAccessPermission() -> {
                        ++siteCount
                        if (siteCount > maxShownPermissionsEntries) continue@loop
                    }
                }
                localizedSiteAccessPermissions.add(context.getString(stringId, host))
            }

            // If we have [maxPermissionsEntries] or fewer permissions, display them all, otherwise we
            // display the first [maxPermissionsEntries] followed by an item that says "...plus N others"
            if (domainCount > maxShownPermissionsEntries) {
                val onePermission = R.string.mozac_feature_addons_permissions_one_extra_domain_description_2
                val multiplePermissions = R.string.mozac_feature_addons_permissions_extra_domains_description_plural_2
                addExtraEntriesIfNeeded(domainCount - maxShownPermissionsEntries, onePermission, multiplePermissions)
            }
            if (siteCount > maxShownPermissionsEntries) {
                val onePermission = R.string.mozac_feature_addons_permissions_one_extra_site_description_2
                val multiplePermissions = R.string.mozac_feature_addons_permissions_extra_sites_description_2
                addExtraEntriesIfNeeded(siteCount - maxShownPermissionsEntries, onePermission, multiplePermissions)
            }
        }

        private fun Int.isSiteAccessPermission() =
            this == R.string.mozac_feature_addons_permissions_one_site_description

        private fun Int.isDomainAccessPermission() =
            this == R.string.mozac_feature_addons_permissions_sites_in_domain_description

        private fun Int.isAllURLsPermission() = this == R.string.mozac_feature_addons_permissions_all_urls_description

        /** Check if a permission is considered [Int.isAllURLsPermission] based on the name */
        fun Permission.isAllURLsPermission(): Boolean {
            return permissionToTranslation[name]?.isAllURLsPermission() == true ||
                getStringIdForHostPermission(name)?.isAllURLsPermission() == true
        }

        /**
         * Checks the permissions list input for any permission that maps to <all_urls>
         *
         * @param permissions the list of permissions to check
         */
        fun permissionsListContainsAllUrls(permissions: List<String>): Boolean = permissions.any {
            getStringIdForHostPermission(it)?.isAllURLsPermission() == true
        }

        /**
         * Data class representing host permissions.
         *
         * @property allUrls Permission string for accessing all URLs.
         * @property wildcards Set of permissions with wildcards.
         * @property sites Set of explicit host permissions.
         */
        data class HostPermissions(
            val allUrls: String?,
            val wildcards: Set<String>,
            val sites: Set<String>,
        )

        /**
         * Classify host permissions. This is a direct conversion of the desktop function found at:
         * https://searchfox.org/mozilla-central/rev/b765e2890b0eb85b24f54bc7ff04491fd0704e30/toolkit/components/extensions/Extension.sys.mjs#2367
         *
         * @param origins List of permission origins.
         * @param ignoreNonWebSchemes Whether to return only schemes like *, http, https, ws, wss.
         * @return [HostPermissions] containing categorized permissions.
         */
        fun classifyOriginPermissions(
            origins: List<String> = emptyList(),
            ignoreNonWebSchemes: Boolean = false,
        ): Result<HostPermissions> {
            var allUrls: String? = null
            val wildcards = mutableSetOf<String>()
            val sites = mutableSetOf<String>()

            val wildcardSchemes = listOf("*", "http", "https", "ws", "wss")

            for (permission in origins) {
                if (permission == "<all_urls>") {
                    allUrls = permission
                    continue
                }

                val match =
                    Regex("^([a-z*]+)://([^/]*)/|^about:").find(permission)
                        ?: return Result.failure(
                            IllegalArgumentException("Illegal origin permission pattern found: $permission")
                        )

                val scheme = match.groups[1]?.value
                val host = match.groups[2]?.value

                if (ignoreNonWebSchemes && scheme !in wildcardSchemes) {
                    continue
                }

                if (host == null || host == "*") {
                    allUrls = allUrls ?: permission
                } else if (host.startsWith("*.")) {
                    wildcards.add(host.substring(2))
                } else {
                    sites.add(host)
                }
            }

            return Result.success(HostPermissions(allUrls, wildcards, sites))
        }

        internal fun getStringIdForHostPermission(urlAccess: String): Int? {
            val uri = urlAccess.toUri()
            val host = (uri.host ?: "").trim()
            val path = (uri.path ?: "").trim()

            return when {
                host == "*" || urlAccess == "<all_urls>" ->
                    R.string.mozac_feature_addons_permissions_all_urls_description
                host.isEmpty() || path.isEmpty() -> null
                host.startsWith(prefix = "*.") -> R.string.mozac_feature_addons_permissions_sites_in_domain_description
                else -> R.string.mozac_feature_addons_permissions_one_site_description
            }
        }

        /** The default fallback locale in case the [Addon] does not have its own [Addon.defaultLocale]. */
        const val DEFAULT_LOCALE = "en-us"
    }
}
