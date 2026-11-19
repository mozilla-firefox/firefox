/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.settingssearch

import android.annotation.SuppressLint
import android.content.Context
import android.content.res.Resources
import android.content.res.XmlResourceParser
import androidx.annotation.VisibleForTesting
import java.io.IOException
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.R

/**
 * Indexes Settings preferences for the Settings Search screen.
 *
 * All the preference files that are parsed and indexed are listed in the companion object.
 *
 * @param context [Context] needed for various Android interactions.
 * @param preferenceFileInformationList List of XML details backing preference screens which will be indexed for
 *   building the in-memory data for what settings can be searched through.
 * @param additionalProviders List of additional providers of [SettingsSearchItem]s to be included in the in-memory data
 *   for what settings can be searched through.
 * @param excludedPreferenceKeys Returns the set of preference keys to exclude from the search index, evaluated at index
 *   time. Used to keep runtime/Nimbus-gated preferences out of search when their feature is disabled.
 */
class DefaultFenixSettingsIndexer(
    private val context: Context,
    private val preferenceFileInformationList: List<PreferenceFileInformation> =
        defaultPreferenceFileInformationList(includeAutofillPreferences = true),
    private val additionalProviders: List<SettingsSearchProvider> = emptyList(),
    private val excludedPreferenceKeys: () -> Set<String> = { emptySet() },
) : SettingsIndexer {
    private val settings = AtomicReference<List<SettingsSearchItem>>(emptyList())

    /** Index all settings. */
    override suspend fun indexAllSettings() =
        withContext(Dispatchers.IO) {
            val newSettings = mutableListOf<SettingsSearchItem>()

            for (preferenceFileInformation in preferenceFileInformationList) {
                val settingFileParser = preferenceFileInformation.xmlResourceId?.let(::getXmlParserForFile)
                if (settingFileParser != null) {
                    parseXmlFile(settingFileParser, preferenceFileInformation, newSettings)
                }
            }

            for (provider in additionalProviders) {
                newSettings.addAll(provider.getSearchItems(context))
            }

            val excluded = excludedPreferenceKeys()
            settings.set(
                if (excluded.isEmpty()) {
                    newSettings
                } else {
                    newSettings.filterNot { it.preferenceKey in excluded }
                }
            )
        }

    /**
     * Get settings filtered by query.
     *
     * @param query Query [String] to filter by.
     * @return List of [SettingsSearchItem]s that match the query.
     */
    override suspend fun getSettingsWithQuery(query: String): List<SettingsSearchItem> {
        if (query.isBlank()) return emptyList()

        val trimmedQuery = query.trim()

        return withContext(Dispatchers.Default) {
            settings
                .get()
                .filter { item ->
                    item.title.contains(trimmedQuery, ignoreCase = true)
                }
                .distinctBy { it.preferenceKey }
        }
    }

    override fun getIndexedItem(preferenceKey: String): SettingsSearchItem? =
        settings.get().firstOrNull { it.preferenceKey == preferenceKey }

    /**
     * Get every indexed setting, unfiltered by any query.
     *
     * @return List of all [SettingsSearchItem]s built by the last [indexAllSettings] call, or an empty list if it has
     *   not run yet.
     */
    @VisibleForTesting internal fun indexedSettings(): List<SettingsSearchItem> = settings.get()

    private fun getXmlParserForFile(xmlResourceId: Int): XmlResourceParser? {
        try {
            return context.resources.getXml(xmlResourceId)
        } catch (e: Resources.NotFoundException) {
            logger.error("Failed to find XML resource $xmlResourceId", e)
        } catch (e: IOException) {
            logger.error("I/O exception while parsing XML resource $xmlResourceId", e)
        }
        return null
    }

    @Suppress("NestedBlockDepth", "CognitiveComplexMethod")
    private fun parseXmlFile(
        parser: XmlResourceParser,
        preferenceFileInformation: PreferenceFileInformation,
        settingsList: MutableList<SettingsSearchItem>,
    ) {
        try {
            var eventType = parser.next()
            var categoryItem: SettingsSearchItem? = null
            var categoryItemAdded = false

            while (eventType != XmlResourceParser.END_DOCUMENT) {
                when (eventType) {
                    XmlResourceParser.START_TAG -> {
                        when (parser.name) {
                            PREFERENCE_CATEGORY_TAG -> {
                                categoryItem =
                                    createCategoryItem(
                                        parser,
                                        preferenceFileInformation,
                                    )
                            }
                            CHECKBOX_PREFERENCE_TAG,
                            DEFAULT_BROWSER_PREFERENCE_TAG,
                            PREFERENCE_TAG,
                            SWITCH_PREFERENCE_TAG,
                            SWITCH_PREFERENCE_PLAIN_TAG,
                            TOGGLE_RADIO_BUTTON_PREFERENCE_TAG -> {
                                val item =
                                    createSettingsSearchItemFromAttributes(
                                        parser = parser,
                                        preferenceFileInformation = preferenceFileInformation,
                                    )
                                if (item != null) {
                                    settingsList.add(item)
                                }
                            }
                            RADIO_BUTTON_PREFERENCE_TAG -> {
                                // Special handling for categories that contain only radio buttons:
                                // In this case, we want the category itself to be searchable,
                                // but use the first radio button's key for navigation. This allows
                                // users to search for the category (e.g., "Theme") and be taken
                                // to the correct preferences screen where they can select from the
                                // radio button options.
                                //
                                // Subsequent radio buttons in the same category are handled as
                                // regular preferences in the else branch below.
                                if (categoryItem != null && !categoryItemAdded) {
                                    categoryItemAdded = true
                                    val preferenceKey = getPreferenceKeyForRadioButtonPref(parser)
                                    if (preferenceKey != null) {
                                        settingsList.add(categoryItem.copy(preferenceKey = preferenceKey))
                                    }
                                    // Clear categoryItem to prevent reusing it for subsequent radio buttons
                                    categoryItem = null
                                } else {
                                    val item =
                                        createSettingsSearchItemFromAttributes(
                                            parser,
                                            preferenceFileInformation,
                                        )
                                    if (item != null) {
                                        settingsList.add(item)
                                    }
                                }
                            }
                        }
                    }
                    XmlResourceParser.END_TAG -> {
                        when (parser.name) {
                            PREFERENCE_CATEGORY_TAG -> {
                                categoryItem = null
                                categoryItemAdded = false
                            }
                        }
                    }
                }
                eventType = parser.next()
            }
        } catch (e: IOException) {
            logger.error("I/O exception while parsing XML file", e)
        } finally {
            parser.close()
        }
    }

    private fun createSettingsSearchItemFromAttributes(
        parser: XmlResourceParser,
        preferenceFileInformation: PreferenceFileInformation,
    ): SettingsSearchItem? {
        var key: String? = null
        var title: String? = null
        var summary = ""

        for (i in 0 until parser.attributeCount) {
            val attributeName = parser.getAttributeName(i)
            val attributeValue = parser.getAttributeValue(i)

            when (attributeName) {
                KEY_ATTRIBUTE_NAME -> {
                    key = attributeValue.takeIf { it.isNotBlank() }?.substring(1)?.let { getStringResource(it) }
                }
                TITLE_ATTRIBUTE_NAME -> {
                    title = attributeValue.takeIf { it.isNotBlank() }?.substring(1)?.let { getStringResource(it) }
                }
                SUMMARY_ATTRIBUTE_NAME -> {
                    summary =
                        attributeValue.takeIf { it.isNotBlank() }?.substring(1)?.let { getStringResource(it) } ?: ""
                }
                IS_VISIBLE_ATTRIBUTE_NAME -> {
                    if (!parser.resolveBooleanAttribute(i)) {
                        return null
                    }
                }
            }
        }

        val categoryHeader = context.getString(preferenceFileInformation.categoryHeaderResourceId)

        if (key == null || title == null) return null

        return SettingsSearchItem(
            preferenceKey = key,
            title = title,
            summary = summary,
            categoryHeader = categoryHeader,
            preferenceFileInformation = preferenceFileInformation,
        )
    }

    /**
     * Create a category item in case the category contains only radio buttons.
     *
     * The category item will be the reference for searching and the first radio button in the category will be used for
     * navigation.
     *
     * @param parser [XmlResourceParser] for the category.
     * @param preferenceFileInformation [PreferenceFileInformation] for the category.
     */
    private fun createCategoryItem(
        parser: XmlResourceParser,
        preferenceFileInformation: PreferenceFileInformation,
    ): SettingsSearchItem? {
        var key: String? = null
        var title: String? = null
        var summary = ""

        for (i in 0 until parser.attributeCount) {
            val attributeName = parser.getAttributeName(i)
            val attributeValue = parser.getAttributeValue(i)

            when (attributeName) {
                KEY_ATTRIBUTE_NAME -> key = getStringResource(attributeValue.substring(1))
                TITLE_ATTRIBUTE_NAME -> title = getStringResource(attributeValue.substring(1))
                SUMMARY_ATTRIBUTE_NAME -> summary = getStringResource(attributeValue.substring(1))
                IS_VISIBLE_ATTRIBUTE_NAME,
                IS_ENABLED_ATTRIBUTE_NAME -> {
                    if (!parser.resolveBooleanAttribute(i)) {
                        return null
                    }
                }
            }
        }

        val categoryHeader = context.getString(preferenceFileInformation.categoryHeaderResourceId)

        return SettingsSearchItem(
            preferenceKey = key ?: "",
            title = title ?: "",
            summary = summary,
            categoryHeader = categoryHeader,
            preferenceFileInformation = preferenceFileInformation,
        )
    }

    /**
     * Resolve a boolean attribute that is either a literal or a `@bool` resource reference. Defaults to true when the
     * value can't be read as a boolean.
     */
    private fun XmlResourceParser.resolveBooleanAttribute(index: Int): Boolean {
        val resourceId = getAttributeResourceValue(index, 0)
        return if (resourceId != 0) {
            try {
                context.resources.getBoolean(resourceId)
            } catch (e: Resources.NotFoundException) {
                true
            }
        } else {
            getAttributeBooleanValue(index, true)
        }
    }

    /**
     * Get the preference key for a radio button preference.
     *
     * @param parser [XmlResourceParser] for the radio button preference.
     */
    private fun getPreferenceKeyForRadioButtonPref(parser: XmlResourceParser): String? {
        var key: String? = null
        for (i in 0 until parser.attributeCount) {
            val attributeName = parser.getAttributeName(i)
            val attributeValue = parser.getAttributeValue(i)

            when (attributeName) {
                KEY_ATTRIBUTE_NAME -> key = getStringResource(attributeValue.substring(1))
            }
        }
        return key
    }

    /**
     * Get the string resource from the given resource name. Uses the locale context.
     *
     * @param resourceName The name of the resource.
     */
    @SuppressLint("DiscouragedApi")
    private fun getStringResource(resourceName: String): String {
        return try {
            val resourceId =
                context.resources.getIdentifier(
                    resourceName,
                    "string",
                    context.packageName,
                )
            if (resourceId == 0) {
                logger.warn("Could not resolve string resource: $resourceName")
                return resourceName
            }

            if (stringsWithRequiredFormatting.contains(resourceId)) {
                val appName = context.getString(R.string.app_name)
                context.getString(resourceId, appName)
            } else {
                context.getString(resourceId)
            }
        } catch (e: Resources.NotFoundException) {
            logger.warn("String resource not found: $resourceName", e)
            resourceName
        }
    }

    companion object {
        private const val TAG = "SettingsIndexer"
        private val logger = Logger(TAG)

        // Attribute names
        private const val PREFERENCE_CATEGORY_TAG = "androidx.preference.PreferenceCategory"
        private const val CHECKBOX_PREFERENCE_TAG = "androidx.preference.CheckBoxPreference"
        private const val PREFERENCE_TAG = "androidx.preference.Preference"
        private const val SWITCH_PREFERENCE_TAG = "androidx.preference.SwitchPreferenceCompat"
        private const val SWITCH_PREFERENCE_PLAIN_TAG = "SwitchPreferenceCompat"
        private const val DEFAULT_BROWSER_PREFERENCE_TAG = "org.mozilla.fenix.settings.DefaultBrowserPreference"
        private const val RADIO_BUTTON_PREFERENCE_TAG = "org.mozilla.fenix.settings.RadioButtonPreference"
        private const val TOGGLE_RADIO_BUTTON_PREFERENCE_TAG = "org.mozilla.fenix.settings.ToggleRadioButtonPreference"
        private const val KEY_ATTRIBUTE_NAME = "key"
        private const val TITLE_ATTRIBUTE_NAME = "title"
        private const val SUMMARY_ATTRIBUTE_NAME = "summary"
        private const val IS_VISIBLE_ATTRIBUTE_NAME = "isPreferenceVisible"
        private const val IS_ENABLED_ATTRIBUTE_NAME = "enabled"

        /**
         * All the preference xml files to load with information for the indexer. In a [List] of
         * [PreferenceFileInformation]s.
         *
         * @param includeAutofillPreferences Whether to include the autofill and saved logins preference screens.
         */
        fun defaultPreferenceFileInformationList(includeAutofillPreferences: Boolean): List<PreferenceFileInformation> =
            buildList {
                add(PreferenceFileInformation.GeneralPreferences)
                add(PreferenceFileInformation.AccessibilityPreferences)
                if (includeAutofillPreferences) {
                    add(PreferenceFileInformation.AutofillPreferences)
                }
                add(PreferenceFileInformation.CustomizationPreferences)
                add(PreferenceFileInformation.DefaultSearchEnginePreferences)
                add(PreferenceFileInformation.DownloadsSettingsPreferences)
                add(PreferenceFileInformation.HomePreferences)
                add(PreferenceFileInformation.OpenLinksInAppsPreferences)
                add(PreferenceFileInformation.PrivateBrowsingPreferences)
                add(PreferenceFileInformation.SearchSettingsPreferences)
                add(PreferenceFileInformation.SiteSettingsPreferences)
                add(PreferenceFileInformation.TabsPreferences)
                add(PreferenceFileInformation.TrackingProtectionPreferences)
                if (includeAutofillPreferences) {
                    add(PreferenceFileInformation.SaveLoginsPreferences)
                }
            }

        /**
         * List of strings that require format args.
         *
         * All of them require the app name.
         */
        val stringsWithRequiredFormatting =
            listOf(
                R.string.preferences_show_nonsponsored_suggestions,
                R.string.preferences_about,
            )
    }
}
