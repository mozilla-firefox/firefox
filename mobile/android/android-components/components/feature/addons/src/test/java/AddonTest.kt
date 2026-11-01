/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertNotNull
import mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
import mozilla.components.concept.engine.webextension.DisabledFlags
import mozilla.components.concept.engine.webextension.Incognito
import mozilla.components.concept.engine.webextension.Metadata
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.feature.addons.Addon.Companion.localizeOptionalPermissions
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AddonTest {

    @Test
    fun `translatePermissions - must return the expected string ids per permission category`() {
        val addon =
            Addon(
                id = "id",
                downloadUrl = "downloadUrl",
                version = "version",
                permissions =
                    listOf(
                        "bookmarks",
                        "browserSettings",
                        "browsingData",
                        "clipboardRead",
                        "clipboardWrite",
                        "declarativeNetRequest",
                        "declarativeNetRequestFeedback",
                        "downloads",
                        "downloads.open",
                        "find",
                        "geolocation",
                        "history",
                        "management",
                        "nativeMessaging",
                        "notifications",
                        "pkcs11",
                        "privacy",
                        "proxy",
                        "sessions",
                        "tabHide",
                        "tabs",
                        "topSites",
                        "trialML",
                        "userScripts",
                        "webNavigation",
                        "devtools",
                    ),
                createdAt = "",
                updatedAt = "",
            )

        val translatedPermissions = addon.translatePermissions(testContext)
        val expectedPermissions =
            listOf(
                    R.string.mozac_feature_addons_permissions_bookmarks_description,
                    R.string.mozac_feature_addons_permissions_browser_setting_description,
                    R.string.mozac_feature_addons_permissions_browser_data_description,
                    R.string.mozac_feature_addons_permissions_clipboard_read_description,
                    R.string.mozac_feature_addons_permissions_clipboard_write_description,
                    R.string.mozac_feature_addons_permissions_declarative_net_request_description,
                    R.string.mozac_feature_addons_permissions_declarative_net_request_feedback_description,
                    R.string.mozac_feature_addons_permissions_downloads_description,
                    R.string.mozac_feature_addons_permissions_downloads_open_description,
                    R.string.mozac_feature_addons_permissions_find_description,
                    R.string.mozac_feature_addons_permissions_geolocation_description,
                    R.string.mozac_feature_addons_permissions_history_description,
                    R.string.mozac_feature_addons_permissions_management_description,
                    R.string.mozac_feature_addons_permissions_native_messaging_description,
                    R.string.mozac_feature_addons_permissions_notifications_description,
                    R.string.mozac_feature_addons_permissions_pkcs11_description,
                    R.string.mozac_feature_addons_permissions_privacy_description,
                    R.string.mozac_feature_addons_permissions_proxy_description,
                    R.string.mozac_feature_addons_permissions_sessions_description,
                    R.string.mozac_feature_addons_permissions_tab_hide_description,
                    R.string.mozac_feature_addons_permissions_tabs_description,
                    R.string.mozac_feature_addons_permissions_top_sites_description,
                    R.string.mozac_feature_addons_permissions_trial_ml_description,
                    R.string.mozac_feature_addons_permissions_user_scripts_description,
                    R.string.mozac_feature_addons_permissions_web_navigation_description,
                    R.string.mozac_feature_addons_permissions_devtools_description,
                )
                .map { testContext.getString(it) }
        assertEquals(expectedPermissions, translatedPermissions)
    }

    @Test
    fun `isInstalled - true if installed state present and otherwise false`() {
        val addon =
            Addon(
                id = "id",
                downloadUrl = "downloadUrl",
                version = "version",
                permissions = emptyList(),
                createdAt = "",
                updatedAt = "",
            )
        assertFalse(addon.isInstalled())

        val installedAddon = addon.copy(installedState = Addon.InstalledState("id", "1.0", ""))
        assertTrue(installedAddon.isInstalled())
    }

    @Test
    fun `isEnabled - true if installed state enabled and otherwise false`() {
        val addon =
            Addon(
                id = "id",
                downloadUrl = "downloadUrl",
                version = "version",
                permissions = emptyList(),
                createdAt = "",
                updatedAt = "",
            )
        assertFalse(addon.isEnabled())

        val installedAddon = addon.copy(installedState = Addon.InstalledState("id", "1.0", ""))
        assertFalse(installedAddon.isEnabled())

        val enabledAddon = addon.copy(installedState = Addon.InstalledState("id", "1.0", "", enabled = true))
        assertTrue(enabledAddon.isEnabled())
    }

    @Test
    fun `classifyOriginPermissions - normalizes origins and returns HostPermissions`() {
        val origins =
            listOf(
                "https://developer.mozilla.org/",
                "*://*.developer.mozilla.org/*",
                "*://developer.mozilla.org/*",
                "https://wikipedia.org/",
                "*://*.wikipedia.org/*",
                "*://wikipedia.org/*",
            )

        val hostNormalizationResult = Addon.classifyOriginPermissions(origins)
        val hostPermissions = hostNormalizationResult.getOrNull()
        assertNotNull(hostPermissions)

        assertEquals(hostPermissions.sites.size, 2)
        assertEquals(hostPermissions.wildcards.size, 2)

        val displayDomainList = hostPermissions.wildcards + hostPermissions.sites

        assertEquals(displayDomainList.size, 2)

        assertEquals(displayDomainList.first(), "developer.mozilla.org")
        assertTrue(displayDomainList.contains("wikipedia.org"))
    }

    @Test
    fun `permissionsListContainsAllUrls - true if any permission in the list maps to the All Urls permission`() {
        val permissionsWithoutAllUrls = listOf("privacy", "tabs")
        val permissionsWithAllUrls = listOf("privacy", "<all_urls>", "tabs")

        val domainsWithoutAllUrls =
            listOf(
                "https://www.mozilla.org",
                "http://testsite.com",
                "testing.com",
                "testing.com/*",
                "*.testing.com/*",
            )

        val domainsWithAllUrls =
            listOf(
                "testing.com",
                "*://*/*",
            )

        assertFalse(
            "Found all_urls permission when none exists",
            Addon.permissionsListContainsAllUrls(permissionsWithoutAllUrls),
        )
        assertTrue(
            "Did not find the all_urls permission in the list",
            Addon.permissionsListContainsAllUrls(permissionsWithAllUrls),
        )

        assertFalse(
            "Found all_urls permission when none exists",
            Addon.permissionsListContainsAllUrls(domainsWithoutAllUrls),
        )
        assertTrue(
            "Did not find the all_urls permission in the list",
            Addon.permissionsListContainsAllUrls(domainsWithAllUrls),
        )
    }

    @Test
    fun `filterTranslations - only keeps specified translations`() {
        val addon =
            Addon(
                id = "id",
                downloadUrl = "downloadUrl",
                version = "version",
                permissions = emptyList(),
                createdAt = "",
                updatedAt = "",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "name", "de" to "Name", "es" to "nombre"),
                translatableDescription =
                    mapOf(
                        Addon.DEFAULT_LOCALE to "description",
                        "de" to "Beschreibung",
                        "es" to "descripción",
                    ),
                translatableSummary =
                    mapOf(Addon.DEFAULT_LOCALE to "summary", "de" to "Kurzfassung", "es" to "resumen"),
            )

        val addonEn = addon.filterTranslations(listOf())
        assertEquals(1, addonEn.translatableName.size)
        assertTrue(addonEn.translatableName.contains(addonEn.defaultLocale))

        val addonEs = addon.filterTranslations(listOf("es"))
        assertEquals(2, addonEs.translatableName.size)
        assertTrue(addonEs.translatableName.contains(addonEn.defaultLocale))
        assertTrue(addonEs.translatableName.contains("es"))
    }

    @Test
    fun `localizeURLAccessPermissions - must translate all_urls access permission`() {
        val expectedString = testContext.getString(R.string.mozac_feature_addons_permissions_all_urls_description)
        val permissions =
            listOf(
                "webRequest",
                "webRequestBlocking",
                "<all_urls>",
            )

        val result = Addon.localizeURLAccessPermissions(testContext, permissions).first()

        assertEquals(expectedString, result)
    }

    @Test
    fun `localizeURLAccessPermissions - must translate all urls access permission`() {
        val expectedString = testContext.getString(R.string.mozac_feature_addons_permissions_all_urls_description)
        val permissions =
            listOf(
                "webRequest",
                "webRequestBlocking",
                "*://*/*",
            )

        val result = Addon.localizeURLAccessPermissions(testContext, permissions).first()

        assertEquals(expectedString, result)
    }

    @Test
    fun `localizeURLAccessPermissions - must translate domain access permissions`() {
        val expectedString =
            listOf("tweetdeck.twitter.com", "twitter.com").map {
                testContext.getString(R.string.mozac_feature_addons_permissions_one_site_description, it)
            }
        val permissions =
            listOf(
                "webRequest",
                "webRequestBlocking",
                "*://tweetdeck.twitter.com/*",
                "*://twitter.com/*",
            )

        val result = Addon.localizeURLAccessPermissions(testContext, permissions)

        assertEquals(expectedString, result)
    }

    @Test
    fun `localizeURLAccessPermissions - must translate one site access permissions`() {
        val expectedString =
            listOf("youtube.com", "youtube-nocookie.com", "vimeo.com").map {
                testContext.getString(R.string.mozac_feature_addons_permissions_sites_in_domain_description, it)
            }
        val permissions =
            listOf(
                "webRequest",
                "*://*.youtube.com/*",
                "*://*.youtube-nocookie.com/*",
                "*://*.vimeo.com/*",
            )

        val result = Addon.localizeURLAccessPermissions(testContext, permissions)

        assertEquals(expectedString, result)
    }

    @Test
    fun `localizeURLAccessPermissions - must collapse host permissions`() {
        var expectedString =
            listOf("youtube.com", "youtube-nocookie.com", "vimeo.com", "google.co.ao").map {
                testContext.getString(R.string.mozac_feature_addons_permissions_sites_in_domain_description, it)
            } + testContext.getString(R.string.mozac_feature_addons_permissions_one_extra_domain_description_2)

        var permissions =
            listOf(
                "webRequest",
                "*://*.youtube.com/*",
                "*://*.youtube-nocookie.com/*",
                "*://*.vimeo.com/*",
                "*://*.google.co.ao/*",
                "*://*.google.com.do/*",
            )

        var result = Addon.localizeURLAccessPermissions(testContext, permissions)

        // 1 domain permissions must be collapsed
        assertEquals(expectedString, result)

        expectedString =
            listOf("youtube.com", "youtube-nocookie.com", "vimeo.com", "google.co.ao").map {
                testContext.getString(R.string.mozac_feature_addons_permissions_sites_in_domain_description, it)
            } + testContext.getString(R.string.mozac_feature_addons_permissions_extra_domains_description_plural_2)

        permissions =
            listOf(
                "webRequest",
                "*://*.youtube.com/*",
                "*://*.youtube-nocookie.com/*",
                "*://*.vimeo.com/*",
                "*://*.google.co.ao/*",
                "*://*.google.com.do/*",
                "*://*.google.co.ar/*",
            )

        result = Addon.localizeURLAccessPermissions(testContext, permissions)

        // 2 domain permissions must be collapsed
        assertEquals(expectedString, result)

        permissions =
            listOf(
                "webRequest",
                "*://www.youtube.com/*",
                "*://www.youtube-nocookie.com/*",
                "*://www.vimeo.com/*",
                "https://mozilla.org/a/b/c/",
                "*://www.google.com.do/*",
            )

        expectedString =
            listOf("www.youtube.com", "www.youtube-nocookie.com", "www.vimeo.com", "mozilla.org").map {
                testContext.getString(R.string.mozac_feature_addons_permissions_one_site_description, it)
            } + testContext.getString(R.string.mozac_feature_addons_permissions_one_extra_site_description_2)

        result = Addon.localizeURLAccessPermissions(testContext, permissions)

        // 1 site permissions must be Collapsed
        assertEquals(expectedString, result)

        permissions =
            listOf(
                "webRequest",
                "*://www.youtube.com/*",
                "*://www.youtube-nocookie.com/*",
                "*://www.vimeo.com/*",
                "https://mozilla.org/a/b/c/",
                "*://www.google.com.do/*",
                "*://www.google.co.ar/*",
            )

        expectedString =
            listOf("www.youtube.com", "www.youtube-nocookie.com", "www.vimeo.com", "mozilla.org").map {
                testContext.getString(R.string.mozac_feature_addons_permissions_one_site_description, it)
            } + testContext.getString(R.string.mozac_feature_addons_permissions_extra_sites_description_2)

        result = Addon.localizeURLAccessPermissions(testContext, permissions)

        // 2 site permissions must be Collapsed
        assertEquals(expectedString, result)

        permissions =
            listOf(
                "webRequest",
                "*://www.youtube.com/*",
                "*://www.youtube-nocookie.com/*",
                "*://www.vimeo.com/*",
                "https://mozilla.org/a/b/c/",
            )

        expectedString =
            listOf("www.youtube.com", "www.youtube-nocookie.com", "www.vimeo.com", "mozilla.org").map {
                testContext.getString(R.string.mozac_feature_addons_permissions_one_site_description, it)
            }

        result = Addon.localizeURLAccessPermissions(testContext, permissions)

        // None permissions must be collapsed
        assertEquals(expectedString, result)
    }

    @Test
    fun `localizeURLAccessPermission - must provide the correct localized string`() {
        val siteAccess =
            listOf(
                "*://twitter.com/*",
                "*://tweetdeck.twitter.com/*",
                "https://mozilla.org/a/b/c/",
                "https://www.google.com.ag/*",
                "https://www.google.co.ck/*",
            )

        siteAccess.forEach {
            val stringId = Addon.getStringIdForHostPermission(it)
            assertEquals(R.string.mozac_feature_addons_permissions_one_site_description, stringId)
        }

        val domainAccess =
            listOf(
                "*://*.youtube.com/*",
                "*://*.youtube.com/*",
                "*://*.youtube-nocookie.com/*",
                "*://*.vimeo.com/*",
                "*://*.facebookcorewwwi.onion/*",
            )

        domainAccess.forEach {
            val stringId = Addon.getStringIdForHostPermission(it)
            assertEquals(R.string.mozac_feature_addons_permissions_sites_in_domain_description, stringId)
        }

        val allUrlsAccess =
            listOf(
                "*://*/*",
                "http://*/*",
                "https://*/*",
                "file://*/*",
                "<all_urls>",
            )

        allUrlsAccess.forEach {
            val stringId = Addon.getStringIdForHostPermission(it)
            assertEquals(R.string.mozac_feature_addons_permissions_all_urls_description, stringId)
        }
    }

    @Test
    fun `localizeOptionalPermissions - should provide LocalizedPermission list`() {
        val expectedLocalizedPermissions =
            listOf(
                Addon.LocalizedPermission(
                    testContext.getString(R.string.mozac_feature_addons_permissions_all_urls_description),
                    Addon.Permission("<all_urls>", false),
                ),
                Addon.LocalizedPermission(
                    testContext.getString(R.string.mozac_feature_addons_permissions_web_navigation_description),
                    Addon.Permission("webNavigation", false),
                ),
                Addon.LocalizedPermission(
                    testContext.getString(R.string.mozac_feature_addons_permissions_clipboard_read_description),
                    Addon.Permission("clipboardRead", false),
                ),
                Addon.LocalizedPermission(
                    testContext.getString(R.string.mozac_feature_addons_permissions_clipboard_write_description),
                    Addon.Permission("clipboardWrite", false),
                ),
            )

        val permissions =
            listOf(
                Addon.Permission("<all_urls>", false),
                Addon.Permission("webNavigation", false),
                Addon.Permission("clipboardRead", false),
                Addon.Permission("clipboardWrite", false),
            )

        val localizedResult = localizeOptionalPermissions(permissions, testContext)
        assertEquals(expectedLocalizedPermissions, localizedResult)
    }

    @Test
    fun `newFromWebExtension - must return an Addon instance`() {
        val version = "1.2.3"
        val permissions = listOf("scripting", "activeTab")
        val origins = listOf("https://example.org/")
        val name = "some name"
        val description = "some description"
        val extension: WebExtension = mock()
        val metadata: Metadata = mock()
        whenever(extension.id).thenReturn("some-id")
        whenever(extension.url).thenReturn("some-url")
        whenever(extension.getMetadata()).thenReturn(metadata)
        whenever(metadata.version).thenReturn(version)
        whenever(metadata.requiredPermissions).thenReturn(permissions)
        whenever(metadata.optionalPermissions).thenReturn(listOf("clipboardRead"))
        whenever(metadata.grantedOptionalPermissions).thenReturn(listOf("clipboardRead"))
        whenever(metadata.optionalOrigins).thenReturn(listOf("*://*.example.com/*", "*://opt-host-perm.example.com/*"))
        // NOTE: in grantedOptionalOrigins we are including one host permission that isn't part of the optionaOrigins
        // list
        // and so it wasn't explicitly listed in the extension manifest.json but  something granted by the user on
        // an extension call to browser.permissions.request (allowed on the Gecko side because "*://sub.example.com" is
        // a
        // subset of the "*://*.example.com" host permission, which was explicitly included in the manifest).
        whenever(metadata.grantedOptionalOrigins).thenReturn(listOf("*://*.example.com/*", "*://sub.example.com/*"))
        whenever(metadata.requiredOrigins).thenReturn(origins)
        whenever(metadata.name).thenReturn(name)
        whenever(metadata.description).thenReturn(description)
        whenever(metadata.disabledFlags).thenReturn(DisabledFlags.select(0))
        whenever(metadata.baseUrl).thenReturn("some-base-url")
        whenever(metadata.developerName).thenReturn("developer-name")
        whenever(metadata.developerUrl).thenReturn("developer-url")
        whenever(metadata.fullDescription).thenReturn("fullDescription")
        whenever(metadata.homepageUrl).thenReturn("some-url")
        whenever(metadata.downloadUrl).thenReturn("some-download-url")
        whenever(metadata.updateDate).thenReturn("1970-01-01T00:00:00Z")
        whenever(metadata.reviewUrl).thenReturn("some-review-url")
        whenever(metadata.reviewCount).thenReturn(0)
        whenever(metadata.averageRating).thenReturn(0f)
        whenever(metadata.detailUrl).thenReturn("detail-url")
        whenever(metadata.incognito).thenReturn(Incognito.NOT_ALLOWED)

        val addon = Addon.newFromWebExtension(extension)
        assertEquals("some-id", addon.id)
        assertEquals("some-url", addon.homepageUrl)
        assertEquals("some-download-url", addon.downloadUrl)
        assertEquals(permissions + origins, addon.permissions)
        assertEquals(
            listOf(Addon.Permission(name = "clipboardRead", granted = true)),
            addon.optionalPermissions,
        )
        assertEquals(
            listOf(
                Addon.Permission(name = "*://*.example.com/*", granted = true),
                Addon.Permission(name = "*://opt-host-perm.example.com/*", granted = false),
                Addon.Permission(name = "*://sub.example.com/*", granted = true),
            ),
            addon.optionalOrigins,
        )
        assertEquals("", addon.updatedAt)
        assertEquals("some name", addon.translatableName[Addon.DEFAULT_LOCALE])
        assertEquals("fullDescription", addon.translatableDescription[Addon.DEFAULT_LOCALE])
        assertEquals("some description", addon.translatableSummary[Addon.DEFAULT_LOCALE])
        assertEquals("developer-name", addon.author?.name)
        assertEquals("developer-url", addon.author?.url)
        assertEquals("some-download-url", addon.downloadUrl)
        assertEquals("some-review-url", addon.ratingUrl)
        assertEquals(0, addon.rating!!.reviews)
        assertEquals("detail-url", addon.detailUrl)
        assertEquals(Addon.Incognito.NOT_ALLOWED, addon.incognito)
    }

    @Test
    fun `fromMetadataToAddonDate - must return an valid addon formatted date`() {
        val expectedDate = "2023-09-28T00:37:43Z"
        val inputDate = "2023-09-28T00:37:43.983Z"

        val result = Addon.fromMetadataToAddonDate(inputDate)

        assertEquals(expectedDate, result)
    }

    @Test
    fun `fromMetadataToAddonDate - must return  handle invalid date formats`() {
        val expectedDate = ""
        val inputDate = "202xd3-09-28T00:37:43.98993Z"

        val result = Addon.fromMetadataToAddonDate(inputDate)

        assertEquals(expectedDate, result)
    }

    @Test
    fun `fromMetadataToAddonDate - must return empty strings`() {
        val expectedDate = ""
        val inputDate = ""

        val result = Addon.fromMetadataToAddonDate(inputDate)

        assertEquals(expectedDate, result)
    }

    @Test
    fun `isDisabledAsBlocklisted - true if installed state disabled status equals to BLOCKLISTED and otherwise false`() {
        val addon = Addon(id = "id")
        val blockListedAddon =
            addon.copy(
                installedState =
                    Addon.InstalledState(
                        id = "id",
                        version = "1.0",
                        optionsPageUrl = "",
                        disabledReason = Addon.DisabledReason.BLOCKLISTED,
                    )
            )

        assertFalse(addon.isDisabledAsBlocklisted())
        assertTrue(blockListedAddon.isDisabledAsBlocklisted())
    }

    @Test
    fun `isDisabledAsNotCorrectlySigned - true if installed state disabled status equals to NOT_CORRECTLY_SIGNED and otherwise false`() {
        val addon = Addon(id = "id")
        val blockListedAddon =
            addon.copy(
                installedState =
                    Addon.InstalledState(
                        id = "id",
                        version = "1.0",
                        optionsPageUrl = "",
                        disabledReason = Addon.DisabledReason.NOT_CORRECTLY_SIGNED,
                    )
            )

        assertFalse(addon.isDisabledAsNotCorrectlySigned())
        assertTrue(blockListedAddon.isDisabledAsNotCorrectlySigned())
    }

    @Test
    fun `isDisabledAsIncompatible - true if installed state disabled status equals to INCOMPATIBLE and otherwise false`() {
        val addon = Addon(id = "id")
        val blockListedAddon =
            addon.copy(
                installedState =
                    Addon.InstalledState(
                        id = "id",
                        version = "1.0",
                        optionsPageUrl = "",
                        disabledReason = Addon.DisabledReason.INCOMPATIBLE,
                    )
            )

        assertFalse(addon.isDisabledAsIncompatible())
        assertTrue(blockListedAddon.isDisabledAsIncompatible())
    }

    @Test
    fun `provideIcon - should provide the icon from either addon or installedState`() {
        val addonWithoutIcon = Addon(id = "id")

        assertNull(addonWithoutIcon.icon)
        assertNull(addonWithoutIcon.installedState?.icon)
        assertNull(addonWithoutIcon.provideIcon())

        val addonWithIcon = addonWithoutIcon.copy(icon = mock())

        assertNotNull(addonWithIcon.icon)
        assertNull(addonWithIcon.installedState?.icon)
        assertNotNull(addonWithIcon.provideIcon())

        val addonWithInstalledStateIcon =
            addonWithoutIcon.copy(installedState = Addon.InstalledState("id", "1.0", "", icon = mock()))

        assertNull(addonWithInstalledStateIcon.icon)
        assertNotNull(addonWithInstalledStateIcon.installedState?.icon)
        assertNotNull(addonWithInstalledStateIcon.provideIcon())
    }

    @Test
    fun `isSoftBlocked - true if installed state disabled status equals to SOFT_BLOCKED and otherwise false`() {
        val addon = Addon(id = "id")
        val softBlockedAddon =
            addon.copy(
                installedState =
                    Addon.InstalledState(
                        id = "id",
                        version = "1.0",
                        optionsPageUrl = "",
                        disabledReason = Addon.DisabledReason.SOFT_BLOCKED,
                    )
            )

        assertFalse(addon.isSoftBlocked())
        assertTrue(softBlockedAddon.isSoftBlocked())
    }

    @Test
    fun `localizeDataCollectionPermissions - should return a localized string for each data collection permission`() {
        // The "none" permission is special and it doesn't get localized strings like the other data permissions.
        GeckoWebExtension.DATA_COLLECTION_PERMISSIONS.filter { permission -> permission != "none" }
            .map { permission ->
                val list = Addon.localizeDataCollectionPermissions(listOf(permission), testContext)
                assertTrue("expected a localized string for $permission", list.size == 1)
            }

        assertTrue(
            "expected non existing entries to be filtered out",
            Addon.localizeDataCollectionPermissions(
                    listOf("nonExisting", "healthInfo", "locationInfo"),
                    testContext,
                )
                .size == 2,
        )
    }

    @Test
    fun `translateRequiredDataCollectionPermissions - should return localized strings for the required data collection permissions`() {
        val addon =
            Addon(
                id = "addon-id",
                requiredDataCollectionPermissions = listOf("bookmarksInfo", "browsingActivity"),
            )

        assertEquals(
            listOf(
                    R.string.mozac_feature_addons_permissions_data_collection_bookmarksInfo_short_description,
                    R.string.mozac_feature_addons_permissions_data_collection_browsingActivity_short_description,
                )
                .map { testContext.getString(it) },
            addon.translateRequiredDataCollectionPermissions(testContext),
        )
    }

    @Test
    fun `localizeOptionalDataCollectionPermissions - should return a LocalizedPermission for each data collection permission`() {
        // The "none" permission is special and it doesn't get localized strings like the other data permissions.
        GeckoWebExtension.DATA_COLLECTION_PERMISSIONS.filter { permission -> permission != "none" }
            .map { permission ->
                val list =
                    Addon.localizeOptionalDataCollectionPermissions(
                        listOf(Addon.Permission(permission, false)),
                        testContext,
                    )
                assertTrue("expected a localized string for $permission", list.size == 1)
            }
    }

    @Test
    fun `translateOptionalDataCollectionPermissions - should return localized permissions for the optional data collection permissions`() {
        val addon =
            Addon(
                id = "addon-id",
                optionalDataCollectionPermissions =
                    listOf(
                        Addon.Permission("bookmarksInfo", false),
                        Addon.Permission("browsingActivity", false),
                    ),
            )

        assertEquals(
            listOf(
                    R.string.mozac_feature_addons_permissions_data_collection_bookmarksInfo_long_description,
                    R.string.mozac_feature_addons_permissions_data_collection_browsingActivity_long_description,
                )
                .map { testContext.getString(it) },
            // Only return the localized name for each LocalizedPermission.
            addon.translateOptionalDataCollectionPermissions(testContext).map { it.localizedName },
        )
    }
}
