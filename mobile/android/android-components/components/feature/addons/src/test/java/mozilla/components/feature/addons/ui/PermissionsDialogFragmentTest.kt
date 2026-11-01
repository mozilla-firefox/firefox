/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons.ui

import android.view.Gravity.TOP
import android.view.ViewGroup
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import androidx.appcompat.widget.AppCompatCheckBox
import androidx.core.view.isVisible
import androidx.fragment.app.FragmentManager
import androidx.fragment.app.FragmentTransaction
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleRegistry
import androidx.recyclerview.widget.RecyclerView
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.R
import mozilla.components.feature.addons.ui.AddonDialogFragment.PromptsStyling
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.utils.ext.getParcelableCompat
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.doAnswer
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify

@RunWith(AndroidJUnit4::class)
class PermissionsDialogFragmentTest {

    @Test
    fun `build dialog`() {
        val addon =
            Addon(
                "id",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"),
                permissions = listOf("privacy", "<all_urls>", "tabs"),
                optionalOrigins =
                    listOf(
                        Addon.Permission("https://*.test1.com/*", false),
                        Addon.Permission("https://www.mozilla.org/*", false),
                        Addon.Permission("*://*.youtube.com/*", false),
                    ),
            )
        val fragment =
            createPermissionsDialogFragment(
                addon,
                permissions = addon.permissions,
                origins =
                    addon.optionalOrigins.map {
                        it.name
                    },
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val name = addon.translateName(testContext)
        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val permissionList = fragment.buildPermissionsList(isAllUrlsPermissionFound = false)
        val allowedInPrivateBrowsing = dialog.findViewById<AppCompatCheckBox>(R.id.allow_in_private_browsing)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        assertFalse(dialog.findViewById<TextView>(R.id.intro_text).isVisible)
        assertTrue(titleTextView.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_dialog_title_2,
                name,
            ),
            titleTextView.text,
        )
        assertTrue(optionalOrRequiredTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_required_permissions),
            optionalOrRequiredTextView.text,
        )

        assertTrue(
            permissionList.contains(
                testContext.getString(R.string.mozac_feature_addons_permissions_privacy_description)
            )
        )
        assertTrue(
            permissionList.contains(
                testContext.getString(R.string.mozac_feature_addons_permissions_all_urls_description)
            )
        )
        assertTrue(
            permissionList.contains(testContext.getString(R.string.mozac_feature_addons_permissions_tabs_description))
        )
        assertTrue(allowedInPrivateBrowsing.isVisible)
        // T&I checkbox is not shown, unless the extension declares it in its manifest.
        assertFalse(technicalAndInteractionDataCheckbox.isVisible)

        assertNotNull(recyclerAdapter)
        assertEquals(3, recyclerAdapter.itemCount)

        val firstItem = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstItem is RequiredPermissionsListItem.PermissionItem &&
                firstItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_all_urls_description)
                )
        )

        val secondItem = recyclerAdapter.getItemAtPosition(1)
        assertTrue(
            secondItem is RequiredPermissionsListItem.PermissionItem &&
                secondItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_privacy_description)
                )
        )

        val thirdItem = recyclerAdapter.getItemAtPosition(2)
        assertTrue(
            thirdItem is RequiredPermissionsListItem.PermissionItem &&
                thirdItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_tabs_description)
                )
        )
    }

    @Test
    fun `clicking on dialog buttons notifies lambdas`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))

        val fragment = createPermissionsDialogFragment(addon)
        var allowedWasExecuted = false
        var denyWasExecuted = false
        var learnMoreWasExecuted = false

        fragment.onPositiveButtonClicked = { _, _, _ ->
            allowedWasExecuted = true
        }

        fragment.onNegativeButtonClicked = {
            denyWasExecuted = true
        }

        fragment.onLearnMoreClicked = {
            learnMoreWasExecuted = true
        }

        doReturn(testContext).`when`(fragment).requireContext()

        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val positiveButton = dialog.findViewById<Button>(R.id.allow_button)
        val negativeButton = dialog.findViewById<Button>(R.id.deny_button)
        val learnMoreLink = dialog.findViewById<TextView>(R.id.learn_more_link)

        positiveButton.performClick()
        negativeButton.performClick()
        learnMoreLink.performClick()

        assertTrue(allowedWasExecuted)
        assertTrue(denyWasExecuted)
        assertTrue(learnMoreWasExecuted)
    }

    @Test
    fun `allow button is disabled immediately after the dialog is shown`() = runTest {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        val fragment =
            createPermissionsDialogFragment(
                addon,
                dispatcher = StandardTestDispatcher(testScheduler),
            )

        doReturn(testContext).`when`(fragment).requireContext()

        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val positiveButton = dialog.findViewById<Button>(R.id.allow_button)
        assertFalse(positiveButton.isEnabled)
    }

    @Test
    fun `allow button becomes enabled after the initial delay`() = runTest {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        val fragment =
            createPermissionsDialogFragment(
                addon,
                dispatcher = StandardTestDispatcher(testScheduler),
            )

        doReturn(testContext).`when`(fragment).requireContext()

        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val positiveButton = dialog.findViewById<Button>(R.id.allow_button)
        assertFalse(positiveButton.isEnabled)

        testScheduler.advanceUntilIdle()

        assertTrue(positiveButton.isEnabled)
    }

    @Test
    fun `dismissing the dialog notifies deny lambda`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))

        val fragment = createPermissionsDialogFragment(addon)
        var denyWasExecuted = false

        fragment.onNegativeButtonClicked = {
            denyWasExecuted = true
        }

        doReturn(testContext).`when`(fragment).requireContext()

        doReturn(mockFragmentManager()).`when`(fragment).parentFragmentManager

        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        fragment.onCancel(mock())

        assertTrue(denyWasExecuted)
    }

    @Test
    fun `dialog must have all the styles of the feature promptsStyling object`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        val styling = PromptsStyling(TOP, true)
        val fragment = createPermissionsDialogFragment(addon, promptsStyling = styling)

        doReturn(testContext).`when`(fragment).requireContext()

        val dialog = fragment.onCreateDialog(null)

        val dialogAttributes = dialog.window!!.attributes

        assertTrue(dialogAttributes.gravity == TOP)
        assertTrue(dialogAttributes.width == ViewGroup.LayoutParams.MATCH_PARENT)
    }

    @Test
    fun `handles add-ons without permissions`() {
        val addon =
            Addon(
                "id",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"),
                incognito = Addon.Incognito.NOT_ALLOWED,
            )
        val fragment = createPermissionsDialogFragment(addon)

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val name = addon.translateName(testContext)
        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val permissionList = fragment.buildPermissionsList(isAllUrlsPermissionFound = false)
        val optionalSettingsTitle = dialog.findViewById<TextView>(R.id.optional_settings_title)
        val privateBrowsingCheckbox = dialog.findViewById<AppCompatCheckBox>(R.id.allow_in_private_browsing)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_dialog_title_2,
                name,
            ),
            titleTextView.text,
        )

        assertEquals(0, recyclerAdapter.itemCount)
        assertFalse(
            permissionList.contains(
                testContext.getString(R.string.mozac_feature_addons_permissions_privacy_description)
            )
        )
        assertFalse(
            permissionList.contains(
                testContext.getString(R.string.mozac_feature_addons_permissions_all_urls_description)
            )
        )
        assertFalse(
            permissionList.contains(testContext.getString(R.string.mozac_feature_addons_permissions_tabs_description))
        )
        assertFalse(optionalOrRequiredTextView.isVisible)
        assertFalse(optionalSettingsTitle.isVisible)
        assertFalse(privateBrowsingCheckbox.isVisible)
        assertFalse(technicalAndInteractionDataCheckbox.isVisible)
    }

    @Test
    fun `dialog with origin permissions shows the first five domains at the top of the list`() {
        val addon =
            Addon(
                "id",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"),
            )

        val origins =
            listOf(
                "https://*.test1.com/*",
                "https://*.test2.com/*",
                "https://*.test3.com/*",
                "https://*.test4.com/*",
                "https://*.test5.com/*",
                "https://*.test6.com/*",
                "https://*.test7.com/*",
                "https://*.test8.com/*",
            )

        val fragment =
            createPermissionsDialogFragment(
                addon,
                permissions = listOf("privacy", "tabs"),
                origins = origins,
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val permissionList = fragment.buildPermissionsList(isAllUrlsPermissionFound = false)

        val name = addon.translateName(testContext)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_dialog_title_2,
                name,
            ),
            titleTextView.text,
        )
        assertTrue(optionalOrRequiredTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_required_permissions),
            optionalOrRequiredTextView.text,
        )

        // Testing the list sent to the adapter
        assertTrue(
            permissionList.contains(
                testContext.getString(R.string.mozac_feature_addons_permissions_privacy_description)
            )
        )
        assertTrue(
            permissionList.contains(testContext.getString(R.string.mozac_feature_addons_permissions_tabs_description))
        )

        assertTrue(
            optionalOrRequiredTextView.text.contains(
                testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_required_permissions)
            )
        )

        // Test the ordering of the list with origins first
        val firstItem = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstItem is RequiredPermissionsListItem.PermissionItem &&
                firstItem.permissionText.contains(
                    testContext.resources.getQuantityString(
                        R.plurals.mozac_feature_addons_permissions_all_domain_count_description_2,
                        origins.size,
                        origins.size,
                    )
                )
        )

        // Test the domains shown
        assertTrue(
            recyclerAdapter.getItemAtPosition(1) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(1) as RequiredPermissionsListItem.DomainItem).domain == "test1.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(2) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(2) as RequiredPermissionsListItem.DomainItem).domain == "test2.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(3) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(3) as RequiredPermissionsListItem.DomainItem).domain == "test3.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(4) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(4) as RequiredPermissionsListItem.DomainItem).domain == "test4.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(5) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(5) as RequiredPermissionsListItem.DomainItem).domain == "test5.com"
        )

        // Show more button shown
        val showMoreItem = recyclerAdapter.getItemAtPosition(6)
        assertTrue(showMoreItem is RequiredPermissionsListItem.ShowHideDomainAction && showMoreItem.isShowAction)

        val tabsItem = recyclerAdapter.getItemAtPosition(7)
        assertTrue(
            tabsItem is RequiredPermissionsListItem.PermissionItem &&
                tabsItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_privacy_description)
                )
        )

        // Test remaining required permissions are shown
        val privacyItem = recyclerAdapter.getItemAtPosition(8)
        assertTrue(
            privacyItem is RequiredPermissionsListItem.PermissionItem &&
                privacyItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_tabs_description)
                )
        )
    }

    @Test
    fun `dialog with origin permissions allows for toggling all domains shown`() {
        val addon =
            Addon(
                "id",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"),
            )

        val origins =
            listOf(
                "https://*.test1.com/*",
                "https://*.test2.com/*",
                "https://*.test3.com/*",
                "https://*.test4.com/*",
                "https://*.test5.com/*",
                "https://*.test6.com/*",
                "https://*.test7.com/*",
                "https://*.test8.com/*",
            )

        val fragment =
            createPermissionsDialogFragment(
                addon,
                permissions = listOf("privacy", "tabs"),
                origins = origins,
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val permissionList = fragment.buildPermissionsList(isAllUrlsPermissionFound = false)

        val name = addon.translateName(testContext)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_dialog_title_2,
                name,
            ),
            titleTextView.text,
        )
        assertTrue(optionalOrRequiredTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_required_permissions),
            optionalOrRequiredTextView.text,
        )

        assertTrue(
            permissionList.contains(
                testContext.getString(R.string.mozac_feature_addons_permissions_privacy_description)
            )
        )
        assertTrue(
            permissionList.contains(testContext.getString(R.string.mozac_feature_addons_permissions_tabs_description))
        )

        assertTrue(
            optionalOrRequiredTextView.text.contains(
                testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_required_permissions)
            )
        )

        // Test the ordering of the list with origins first
        val firstItem = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstItem is RequiredPermissionsListItem.PermissionItem &&
                firstItem.permissionText.contains(
                    testContext.resources.getQuantityString(
                        R.plurals.mozac_feature_addons_permissions_all_domain_count_description_2,
                        origins.size,
                        origins.size,
                    )
                )
        )

        // Test the domains shown
        assertTrue(
            recyclerAdapter.getItemAtPosition(1) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(1) as RequiredPermissionsListItem.DomainItem).domain == "test1.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(2) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(2) as RequiredPermissionsListItem.DomainItem).domain == "test2.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(3) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(3) as RequiredPermissionsListItem.DomainItem).domain == "test3.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(4) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(4) as RequiredPermissionsListItem.DomainItem).domain == "test4.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(5) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(5) as RequiredPermissionsListItem.DomainItem).domain == "test5.com"
        )

        // Show more button shown
        val showMoreItem = recyclerAdapter.getItemAtPosition(6)
        assertTrue(showMoreItem is RequiredPermissionsListItem.ShowHideDomainAction && showMoreItem.isShowAction)

        val tabsItem = recyclerAdapter.getItemAtPosition(7)
        assertTrue(
            tabsItem is RequiredPermissionsListItem.PermissionItem &&
                tabsItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_privacy_description)
                )
        )

        // Test remaining required permissions are shown
        val privacyItem = recyclerAdapter.getItemAtPosition(8)
        assertTrue(
            privacyItem is RequiredPermissionsListItem.PermissionItem &&
                privacyItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_tabs_description)
                )
        )

        // Show all sites click
        recyclerAdapter.toggleDomainSection()

        // Test the ordering of the list with origins first
        val firstToggled = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstToggled is RequiredPermissionsListItem.PermissionItem &&
                firstToggled.permissionText.contains(
                    testContext.resources.getQuantityString(
                        R.plurals.mozac_feature_addons_permissions_all_domain_count_description_2,
                        origins.size,
                        origins.size,
                    )
                )
        )

        // Hide sites button shown
        val hideSitesToggle = recyclerAdapter.getItemAtPosition(1)
        assertTrue(hideSitesToggle is RequiredPermissionsListItem.ShowHideDomainAction && !hideSitesToggle.isShowAction)

        // Test the domains shown
        assertTrue(
            recyclerAdapter.getItemAtPosition(2) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(2) as RequiredPermissionsListItem.DomainItem).domain == "test1.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(3) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(3) as RequiredPermissionsListItem.DomainItem).domain == "test2.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(4) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(4) as RequiredPermissionsListItem.DomainItem).domain == "test3.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(5) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(5) as RequiredPermissionsListItem.DomainItem).domain == "test4.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(6) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(6) as RequiredPermissionsListItem.DomainItem).domain == "test5.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(7) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(7) as RequiredPermissionsListItem.DomainItem).domain == "test6.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(8) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(8) as RequiredPermissionsListItem.DomainItem).domain == "test7.com"
        )
        assertTrue(
            recyclerAdapter.getItemAtPosition(9) is RequiredPermissionsListItem.DomainItem &&
                (recyclerAdapter.getItemAtPosition(9) as RequiredPermissionsListItem.DomainItem).domain == "test8.com"
        )

        val newTabsItem = recyclerAdapter.getItemAtPosition(10)
        assertTrue(
            newTabsItem is RequiredPermissionsListItem.PermissionItem &&
                newTabsItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_privacy_description)
                )
        )

        // Test remaining required permissions are shown
        val newPrivacyItem = recyclerAdapter.getItemAtPosition(11)
        assertTrue(
            newPrivacyItem is RequiredPermissionsListItem.PermissionItem &&
                newPrivacyItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_tabs_description)
                )
        )
    }

    @Test
    fun `build dialog for an update`() {
        val addon =
            Addon(
                "id",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"),
                permissions = listOf("privacy"),
            )
        val fragment =
            createPermissionsDialogFragment(
                addon,
                mode = PermissionsDialogMode.UPDATE,
                permissions = addon.permissions,
                dataCollectionPermissions = listOf("healthInfo"),
                promptInstanceId = "an-instance-id",
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val introTextView = dialog.findViewById<TextView>(R.id.intro_text)
        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val dataCollectionTextView = dialog.findViewById<TextView>(R.id.optional_or_required_data_collection_text)
        val allowButton = dialog.findViewById<Button>(R.id.allow_button)
        val denyButton = dialog.findViewById<Button>(R.id.deny_button)
        val optionalSettingsTitle = dialog.findViewById<TextView>(R.id.optional_settings_title)
        val privateBrowsingCheckbox = dialog.findViewById<AppCompatCheckBox>(R.id.allow_in_private_browsing)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_updater_permissions_dialog_title,
                addon.translateName(testContext),
            ),
            titleTextView.text,
        )
        assertTrue(introTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_updater_permissions_dialog_description),
            introTextView.text,
        )
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_optional_permissions),
            optionalOrRequiredTextView.text,
        )
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_optional_data_collection),
            dataCollectionTextView.text,
        )
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_data_collection_optional_description,
                Addon.formatLocalizedDataCollectionPermissions(
                    Addon.localizeDataCollectionPermissions(listOf("healthInfo"), testContext)
                ),
            ),
            dialog.findViewById<TextView>(R.id.data_collection_permissions).text,
        )
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_updater_permissions_dialog_update_button),
            allowButton.text,
        )
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_updater_permissions_dialog_cancel_button),
            denyButton.text,
        )
        assertFalse(optionalSettingsTitle.isVisible)
        assertFalse(privateBrowsingCheckbox.isVisible)
        assertFalse(technicalAndInteractionDataCheckbox.isVisible)
        assertEquals(
            listOf(testContext.getString(R.string.mozac_feature_addons_permissions_privacy_description)),
            fragment.buildPermissionsList(isAllUrlsPermissionFound = false),
        )
        assertEquals("an-instance-id", fragment.promptInstanceId)
    }

    @Test
    fun `build dialog for optional permissions`() {
        val addon =
            Addon(
                "id",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"),
                permissions = listOf("privacy", "https://example.org/", "tabs"),
            )
        val fragment =
            createPermissionsDialogFragment(
                addon,
                mode = PermissionsDialogMode.OPTIONAL,
                permissions = addon.permissions,
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val name = addon.translateName(testContext)
        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val allowButton = dialog.findViewById<Button>(R.id.allow_button)
        val denyButton = dialog.findViewById<Button>(R.id.deny_button)
        val permissionsList = fragment.buildPermissionsList(isAllUrlsPermissionFound = false)
        val optionalSettingsTitle = dialog.findViewById<TextView>(R.id.optional_settings_title)
        val privateBrowsingCheckbox = dialog.findViewById<AppCompatCheckBox>(R.id.allow_in_private_browsing)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        assertFalse(dialog.findViewById<TextView>(R.id.intro_text).isVisible)
        assertTrue(titleTextView.isVisible)
        assertEquals(
            titleTextView.text,
            testContext.getString(
                R.string.mozac_feature_addons_optional_permissions_with_data_collection_dialog_title,
                name,
            ),
        )
        assertTrue(optionalOrRequiredTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_optional_permissions),
            optionalOrRequiredTextView.text,
        )
        assertTrue(
            permissionsList.contains(
                testContext.getString(R.string.mozac_feature_addons_permissions_privacy_description)
            )
        )
        assertTrue(
            permissionsList.contains(
                testContext.getString(
                    R.string.mozac_feature_addons_permissions_one_site_description,
                    "example.org",
                )
            )
        )
        assertTrue(
            permissionsList.contains(testContext.getString(R.string.mozac_feature_addons_permissions_tabs_description))
        )

        val firstItem = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstItem is RequiredPermissionsListItem.PermissionItem &&
                firstItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_privacy_description)
                )
        )

        val secondItem = recyclerAdapter.getItemAtPosition(1)
        assertTrue(
            secondItem is RequiredPermissionsListItem.PermissionItem &&
                secondItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_tabs_description)
                )
        )

        val thirdItem = recyclerAdapter.getItemAtPosition(2)
        assertTrue(
            thirdItem is RequiredPermissionsListItem.PermissionItem &&
                thirdItem.permissionText.contains(
                    testContext.getString(
                        R.string.mozac_feature_addons_permissions_one_site_description,
                        "example.org",
                    )
                )
        )

        assertFalse(optionalSettingsTitle.isVisible)
        assertFalse(privateBrowsingCheckbox.isVisible)
        assertFalse(technicalAndInteractionDataCheckbox.isVisible)
        assertEquals(
            allowButton.text,
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_allow),
        )
        assertEquals(
            denyButton.text,
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_deny),
        )
    }

    @Test
    fun `require double confirmation for userScripts optional permission`() = runTest {
        // Most of the "userScripts" optional permission request is rendered as an optional permission,
        // which is already covered by the "build dialog for optional permissions" test above.
        // Here, we check aspects specific to the userScripts permission.
        // The unit tests for the desktop counterpart are at:
        // toolkit/mozapps/extensions/test/browser/browser_permission_prompt_userScripts.js

        val addon =
            Addon(
                "id",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"),
                optionalPermissions = listOf(Addon.Permission("userScripts", false)),
            )
        val fragment =
            createPermissionsDialogFragment(
                addon,
                mode = PermissionsDialogMode.OPTIONAL,
                // Gecko enforces that "userScripts" is the only permission
                // included in an optional permission request. This is verified by
                // the at_most_one_optional_only_permission_in_request test at:
                // https://searchfox.org/mozilla-central/rev/fcfb558f8946f3648d962576125af46bf6e2910a/toolkit/components/extensions/test/xpcshell/test_ext_permissions_optional_only.js#251-268
                permissions = listOf("userScripts"),
                dispatcher = StandardTestDispatcher(testScheduler),
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val allowButton = dialog.findViewById<Button>(R.id.allow_button)
        val denyButton = dialog.findViewById<Button>(R.id.deny_button)

        val name = addon.translateName(testContext)
        assertTrue(titleTextView.isVisible)
        assertEquals(
            titleTextView.text,
            testContext.getString(
                R.string.mozac_feature_addons_optional_permissions_with_data_collection_dialog_title,
                name,
            ),
        )
        assertTrue(optionalOrRequiredTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_optional_permissions),
            optionalOrRequiredTextView.text,
        )

        assertEquals(recyclerAdapter.itemCount, 2)

        val firstItem = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstItem is RequiredPermissionsListItem.OptInPermissionItem &&
                firstItem.permissionText ==
                    testContext.getString(R.string.mozac_feature_addons_permissions_user_scripts_description)
        )

        val holder = recyclerAdapter.onCreateViewHolder(permissionsRecyclerView, 3)
        assertIs<RequiredPermissionsAdapter.OptInPermissionViewHolder>(holder)
        recyclerAdapter.onBindViewHolder(holder, 0)

        val permissionOptInCheckbox = holder.itemView.findViewById<AppCompatCheckBox>(R.id.permission_opt_in_item)
        assertEquals(
            permissionOptInCheckbox.text,
            testContext.getString(R.string.mozac_feature_addons_permissions_user_scripts_description),
        )

        // Initial state: checkbox unchecked, "allow" button disabled by default.
        assertFalse(permissionOptInCheckbox.isChecked)
        assertFalse(allowButton.isEnabled)
        assertTrue(denyButton.isEnabled)

        // Even after the initial delay elapses, the "allow" button must remain
        // disabled until the opt-in checkbox is checked.
        testScheduler.advanceUntilIdle()
        assertFalse(allowButton.isEnabled)

        // Toggling checkbox should enable "allow" button.
        permissionOptInCheckbox.performClick()
        assertTrue(permissionOptInCheckbox.isChecked)
        assertTrue(allowButton.isEnabled)
        assertTrue(denyButton.isEnabled)

        // Unchecking checkbox should disable "allow" button.
        permissionOptInCheckbox.performClick()
        assertFalse(permissionOptInCheckbox.isChecked)
        assertFalse(allowButton.isEnabled)

        // Toggling checkbox should enable "allow" button again.
        permissionOptInCheckbox.performClick()
        assertTrue(permissionOptInCheckbox.isChecked)
        assertTrue(allowButton.isEnabled)

        val secondItem = recyclerAdapter.getItemAtPosition(1)
        assertTrue(
            secondItem is RequiredPermissionsListItem.ExtraWarningItem &&
                secondItem.warningText ==
                    testContext.getString(R.string.mozac_feature_addons_permissions_user_scripts_extra_warning)
        )
    }

    @Test
    fun `userScripts allow button stays disabled if opt-in is checked before the initial delay elapses`() = runTest {
        val addon =
            Addon(
                "id",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"),
                optionalPermissions = listOf(Addon.Permission("userScripts", false)),
            )
        val fragment =
            createPermissionsDialogFragment(
                addon,
                mode = PermissionsDialogMode.OPTIONAL,
                permissions = listOf("userScripts"),
                dispatcher = StandardTestDispatcher(testScheduler),
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val allowButton = dialog.findViewById<Button>(R.id.allow_button)

        val holder = recyclerAdapter.onCreateViewHolder(permissionsRecyclerView, 3)
        assertIs<RequiredPermissionsAdapter.OptInPermissionViewHolder>(holder)
        recyclerAdapter.onBindViewHolder(holder, 0)
        val permissionOptInCheckbox = holder.itemView.findViewById<AppCompatCheckBox>(R.id.permission_opt_in_item)

        // Initial state: both gates closed; button disabled.
        assertFalse(allowButton.isEnabled)

        // Satisfy the opt-in gate before the delay elapses. Button must stay disabled.
        permissionOptInCheckbox.performClick()
        assertTrue(permissionOptInCheckbox.isChecked)
        assertFalse(allowButton.isEnabled)

        // Once the delay also elapses, both gates are satisfied and the button enables.
        testScheduler.advanceUntilIdle()
        assertTrue(allowButton.isEnabled)
    }

    @Test
    fun `hide private browsing checkbox when the add-on does not allow running in private windows`() {
        val permissions = listOf("privacy", "<all_urls>", "tabs")
        val addon =
            Addon(
                "id",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"),
                permissions = permissions,
                incognito = Addon.Incognito.NOT_ALLOWED,
            )
        val fragment = createPermissionsDialogFragment(addon, permissions)

        assertSame(
            addon,
            fragment.arguments?.getParcelableCompat(KEY_ADDON, Addon::class.java),
        )

        doReturn(testContext).`when`(fragment).requireContext()

        val dialog = fragment.onCreateDialog(null)

        dialog.show()

        val name = addon.translateName(testContext)
        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val allowedInPrivateBrowsing = dialog.findViewById<AppCompatCheckBox>(R.id.allow_in_private_browsing)

        assertTrue(titleTextView.text.contains(name))
        assertFalse(allowedInPrivateBrowsing.isVisible)
    }

    @Test
    fun `dismiss the permissions dialog when an origin permission does not match the normalization requirements`() {
        val permissions = listOf("privacy", "<all_urls>", "tabs")
        val origins = listOf("https://www.testnopath.org") // Note the missing / for the path
        val addon =
            Addon(
                "id",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"),
                permissions = permissions,
                incognito = Addon.Incognito.NOT_ALLOWED,
            )

        val fragment = createPermissionsDialogFragment(addon, permissions, origins)

        assertSame(
            addon,
            fragment.arguments?.getParcelableCompat(KEY_ADDON, Addon::class.java),
        )

        doReturn(testContext).`when`(fragment).requireContext()

        val dialog = fragment.onCreateDialog(null)

        dialog.show()

        verify(fragment).dismiss()
    }

    @Test
    fun `build dialog with required data collection permissions`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        val dataCollectionPermissions = listOf("healthInfo", "locationInfo")
        val fragment = createPermissionsDialogFragment(addon, dataCollectionPermissions = dataCollectionPermissions)

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val dataCollectionPermissionsItem = dialog.findViewById<LinearLayout>(R.id.data_collection_permissions_item)
        val optionalSettingsTitle = dialog.findViewById<TextView>(R.id.optional_settings_title)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        val name = addon.translateName(testContext)
        assertTrue(titleTextView.isVisible)
        assertEquals(
            titleTextView.text,
            testContext.getString(
                R.string.mozac_feature_addons_permissions_dialog_title_2,
                name,
            ),
        )

        // There is no required (API or host) permission.
        assertFalse(optionalOrRequiredTextView.isVisible)
        assertEquals(0, recyclerAdapter.itemCount)

        assertTrue(dataCollectionPermissionsItem.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_required_data_collection_description_2,
                Addon.formatLocalizedDataCollectionPermissions(
                    Addon.localizeDataCollectionPermissions(
                        dataCollectionPermissions,
                        testContext,
                    )
                ),
            ),
            dataCollectionPermissionsItem.findViewById<TextView>(R.id.data_collection_permissions).text,
        )

        assertTrue(optionalSettingsTitle.isVisible)
        assertFalse(technicalAndInteractionDataCheckbox.isVisible)
    }

    @Test
    fun `build dialog with none data collection permission`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        val dataCollectionPermissions = listOf("none")
        val fragment = createPermissionsDialogFragment(addon, dataCollectionPermissions = dataCollectionPermissions)

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val dataCollectionPermissionsItem = dialog.findViewById<LinearLayout>(R.id.data_collection_permissions_item)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        val name = addon.translateName(testContext)
        assertTrue(titleTextView.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_dialog_title_2,
                name,
            ),
            titleTextView.text,
        )

        // There is no required (API or host) permission.
        assertFalse(optionalOrRequiredTextView.isVisible)
        assertEquals(0, recyclerAdapter.itemCount)

        assertTrue(dataCollectionPermissionsItem.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_none_required_data_collection_description),
            dataCollectionPermissionsItem.findViewById<TextView>(R.id.data_collection_permissions).text,
        )

        assertFalse(technicalAndInteractionDataCheckbox.isVisible)
    }

    @Test
    fun `build dialog with both required API and data collection permissions`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        val dataCollectionPermissions = listOf("bookmarksInfo")
        val fragment =
            createPermissionsDialogFragment(
                addon,
                permissions = listOf("bookmarks"),
                dataCollectionPermissions = dataCollectionPermissions,
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val dataCollectionPermissionsItem = dialog.findViewById<LinearLayout>(R.id.data_collection_permissions_item)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        val name = addon.translateName(testContext)
        assertTrue(titleTextView.isVisible)
        assertEquals(
            titleTextView.text,
            testContext.getString(
                R.string.mozac_feature_addons_permissions_dialog_title_2,
                name,
            ),
        )
        assertTrue(optionalOrRequiredTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_required_permissions),
            optionalOrRequiredTextView.text,
        )

        assertEquals(1, recyclerAdapter.itemCount)
        val firstItem = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstItem is RequiredPermissionsListItem.PermissionItem &&
                firstItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_bookmarks_description)
                )
        )

        assertTrue(dataCollectionPermissionsItem.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_required_data_collection_description_2,
                Addon.formatLocalizedDataCollectionPermissions(
                    Addon.localizeDataCollectionPermissions(
                        dataCollectionPermissions,
                        testContext,
                    )
                ),
            ),
            dataCollectionPermissionsItem.findViewById<TextView>(R.id.data_collection_permissions).text,
        )

        assertFalse(technicalAndInteractionDataCheckbox.isVisible)
    }

    @Test
    fun `build dialog with technical and interaction data collection permission`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        val dataCollectionPermissions = listOf("technicalAndInteraction")
        val fragment =
            createPermissionsDialogFragment(
                addon,
                permissions = listOf("bookmarks"),
                dataCollectionPermissions = dataCollectionPermissions,
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val optionalOrRequiredDataCollectionTextView =
            dialog.findViewById<TextView>(R.id.optional_or_required_data_collection_text)
        val dataCollectionPermissionsItem = dialog.findViewById<LinearLayout>(R.id.data_collection_permissions_item)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        assertTrue(optionalOrRequiredTextView.isVisible)
        // We list the API permissions first, under the `optionalOrRequiredTextView` title.
        val firstItem = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstItem is RequiredPermissionsListItem.PermissionItem &&
                firstItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_bookmarks_description)
                )
        )

        // We shouldn't show the data collection item when there is no data collection we can display there. This is the
        // case because the `technicalAndInteraction` data collection permission is rendered as a checkbox.
        assertFalse(optionalOrRequiredDataCollectionTextView.isVisible)
        assertFalse(dataCollectionPermissionsItem.isVisible)

        assertTrue(technicalAndInteractionDataCheckbox.isVisible)
        assertTrue(technicalAndInteractionDataCheckbox.isChecked)
    }

    @Test
    fun `build dialog with technical and interaction data collection permission and incognito set to NOT_ALLOWED`() {
        val addon =
            Addon(
                "id",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"),
                incognito = Addon.Incognito.NOT_ALLOWED,
            )
        val dataCollectionPermissions = listOf("technicalAndInteraction")
        val fragment =
            createPermissionsDialogFragment(
                addon,
                permissions = listOf("bookmarks"),
                dataCollectionPermissions = dataCollectionPermissions,
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val optionalOrRequiredDataCollectionTextView =
            dialog.findViewById<TextView>(R.id.optional_or_required_data_collection_text)
        val dataCollectionPermissionsItem = dialog.findViewById<LinearLayout>(R.id.data_collection_permissions_item)
        val optionalSettingsTitle = dialog.findViewById<TextView>(R.id.optional_settings_title)
        val privateBrowsingCheckbox = dialog.findViewById<AppCompatCheckBox>(R.id.allow_in_private_browsing)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        assertTrue(optionalOrRequiredTextView.isVisible)
        // We list the API permissions first, under the `optionalOrRequiredTextView` title.
        val firstItem = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstItem is RequiredPermissionsListItem.PermissionItem &&
                firstItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_bookmarks_description)
                )
        )

        // We shouldn't show the data collection item when there is no data collection we can display there. This is the
        // case because the `technicalAndInteraction` data collection permission is rendered as a checkbox.
        assertFalse(optionalOrRequiredDataCollectionTextView.isVisible)
        assertFalse(dataCollectionPermissionsItem.isVisible)

        assertTrue(optionalSettingsTitle.isVisible)
        assertFalse(privateBrowsingCheckbox.isVisible)
        assertTrue(technicalAndInteractionDataCheckbox.isVisible)
        assertTrue(technicalAndInteractionDataCheckbox.isChecked)
    }

    @Test
    fun `build dialog with required API, data collection permissions, and technical and interaction data`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        val dataCollectionPermissions = listOf("bookmarksInfo")
        val fragment =
            createPermissionsDialogFragment(
                addon,
                permissions = listOf("bookmarks"),
                // We split the list because we expect only the list of `dataCollectionPermissions` to be shown in
                // `dataCollectionPermissionsItem`. `technicalAndInteraction` gets its own checkbox instead.
                dataCollectionPermissions = dataCollectionPermissions + listOf("technicalAndInteraction"),
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val dataCollectionPermissionsItem = dialog.findViewById<LinearLayout>(R.id.data_collection_permissions_item)
        val optionalSettingsTitle = dialog.findViewById<TextView>(R.id.optional_settings_title)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        assertTrue(optionalOrRequiredTextView.isVisible)
        assertEquals(1, recyclerAdapter.itemCount)
        val firstItem = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstItem is RequiredPermissionsListItem.PermissionItem &&
                firstItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_bookmarks_description)
                )
        )

        assertTrue(dataCollectionPermissionsItem.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_required_data_collection_description_2,
                Addon.formatLocalizedDataCollectionPermissions(
                    Addon.localizeDataCollectionPermissions(
                        dataCollectionPermissions,
                        testContext,
                    )
                ),
            ),
            dataCollectionPermissionsItem.findViewById<TextView>(R.id.data_collection_permissions).text,
        )

        assertTrue(optionalSettingsTitle.isVisible)
        assertTrue(technicalAndInteractionDataCheckbox.isVisible)
    }

    @Test
    fun `build optional dialog with required API, data collection permissions, and technical and interaction data`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        // This is similar to "build dialog with required API, data collection permissions, and technical
        // and interaction data" "build dialog with required API, data collection permissions, and technical
        // and interaction data" but the expectation is different: the technicalAndInteraction permission
        // should be listed along with the other data collection permissions in the optional dialog.
        val dataCollectionPermissions = listOf("bookmarksInfo", "technicalAndInteraction")
        val fragment =
            createPermissionsDialogFragment(
                addon,
                permissions = listOf("bookmarks"),
                dataCollectionPermissions = dataCollectionPermissions,
                mode = PermissionsDialogMode.OPTIONAL,
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val optionalOrRequiredDataCollectionTextView =
            dialog.findViewById<TextView>(R.id.optional_or_required_data_collection_text)
        val dataCollectionPermissionsItem = dialog.findViewById<LinearLayout>(R.id.data_collection_permissions_item)
        val optionalSettingsTitle = dialog.findViewById<TextView>(R.id.optional_settings_title)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        val name = addon.translateName(testContext)
        assertTrue(titleTextView.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_optional_permissions_with_data_collection_dialog_title,
                name,
            ),
            titleTextView.text,
        )
        assertTrue(optionalOrRequiredTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_optional_permissions),
            optionalOrRequiredTextView.text,
        )
        assertEquals(1, recyclerAdapter.itemCount)
        val firstItem = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstItem is RequiredPermissionsListItem.PermissionItem &&
                firstItem.permissionText.contains(
                    testContext.getString(R.string.mozac_feature_addons_permissions_bookmarks_description)
                )
        )

        assertTrue(optionalOrRequiredDataCollectionTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_optional_data_collection),
            optionalOrRequiredDataCollectionTextView.text,
        )
        assertTrue(dataCollectionPermissionsItem.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_data_collection_optional_description,
                Addon.formatLocalizedDataCollectionPermissions(
                    Addon.localizeDataCollectionPermissions(
                        dataCollectionPermissions,
                        testContext,
                    )
                ),
            ),
            dataCollectionPermissionsItem.findViewById<TextView>(R.id.data_collection_permissions).text,
        )

        assertFalse(optionalSettingsTitle.isVisible)
        assertFalse(technicalAndInteractionDataCheckbox.isVisible)
    }

    @Test
    fun `build optional dialog with required origins and data collection permissions`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        val dataCollectionPermissions = listOf("bookmarksInfo")
        val fragment =
            createPermissionsDialogFragment(
                addon,
                origins = listOf("*://*.mozilla.org/*"),
                dataCollectionPermissions = dataCollectionPermissions,
                mode = PermissionsDialogMode.OPTIONAL,
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val optionalOrRequiredDataCollectionTextView =
            dialog.findViewById<TextView>(R.id.optional_or_required_data_collection_text)
        val dataCollectionPermissionsItem = dialog.findViewById<LinearLayout>(R.id.data_collection_permissions_item)
        val optionalSettingsTitle = dialog.findViewById<TextView>(R.id.optional_settings_title)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        val name = addon.translateName(testContext)
        assertTrue(titleTextView.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_optional_permissions_with_data_collection_dialog_title,
                name,
            ),
            titleTextView.text,
        )
        assertTrue(optionalOrRequiredTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_optional_permissions),
            optionalOrRequiredTextView.text,
        )
        assertEquals(2, recyclerAdapter.itemCount)
        val firstItem = recyclerAdapter.getItemAtPosition(0)
        assertTrue(
            firstItem is RequiredPermissionsListItem.PermissionItem &&
                firstItem.permissionText.contains(
                    testContext.resources.getQuantityString(
                        R.plurals.mozac_feature_addons_permissions_all_domain_count_description_2,
                        1,
                        1,
                    )
                )
        )
        val secondItem = recyclerAdapter.getItemAtPosition(1)
        assertTrue(secondItem is RequiredPermissionsListItem.DomainItem && secondItem.domain == "mozilla.org")

        assertTrue(optionalOrRequiredDataCollectionTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_optional_data_collection),
            optionalOrRequiredDataCollectionTextView.text,
        )
        assertTrue(dataCollectionPermissionsItem.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_data_collection_optional_description,
                Addon.formatLocalizedDataCollectionPermissions(
                    Addon.localizeDataCollectionPermissions(
                        dataCollectionPermissions,
                        testContext,
                    )
                ),
            ),
            dataCollectionPermissionsItem.findViewById<TextView>(R.id.data_collection_permissions).text,
        )

        assertFalse(optionalSettingsTitle.isVisible)
        assertFalse(technicalAndInteractionDataCheckbox.isVisible)
    }

    @Test
    fun `build optional dialog with data collection permissions only`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        val dataCollectionPermissions = listOf("bookmarksInfo", "technicalAndInteraction")
        val fragment =
            createPermissionsDialogFragment(
                addon,
                dataCollectionPermissions = dataCollectionPermissions,
                mode = PermissionsDialogMode.OPTIONAL,
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val optionalOrRequiredDataCollectionTextView =
            dialog.findViewById<TextView>(R.id.optional_or_required_data_collection_text)
        val dataCollectionPermissionsItem = dialog.findViewById<LinearLayout>(R.id.data_collection_permissions_item)
        val optionalSettingsTitle = dialog.findViewById<TextView>(R.id.optional_settings_title)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        val name = addon.translateName(testContext)
        assertTrue(titleTextView.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_optional_permissions_with_data_collection_only_dialog_title,
                name,
            ),
            titleTextView.text,
        )
        assertFalse(optionalOrRequiredTextView.isVisible)
        assertEquals(0, recyclerAdapter.itemCount)

        assertTrue(optionalOrRequiredDataCollectionTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_optional_data_collection),
            optionalOrRequiredDataCollectionTextView.text,
        )
        assertTrue(dataCollectionPermissionsItem.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_data_collection_optional_description,
                Addon.formatLocalizedDataCollectionPermissions(
                    Addon.localizeDataCollectionPermissions(
                        dataCollectionPermissions,
                        testContext,
                    )
                ),
            ),
            dataCollectionPermissionsItem.findViewById<TextView>(R.id.data_collection_permissions).text,
        )

        assertFalse(optionalSettingsTitle.isVisible)
        assertFalse(technicalAndInteractionDataCheckbox.isVisible)
    }

    @Test
    fun `build optional dialog with technical and interaction data collection permission only`() {
        val addon = Addon("id", translatableName = mapOf(Addon.DEFAULT_LOCALE to "my_addon"))
        val dataCollectionPermissions = listOf("technicalAndInteraction")
        val fragment =
            createPermissionsDialogFragment(
                addon,
                dataCollectionPermissions = dataCollectionPermissions,
                mode = PermissionsDialogMode.OPTIONAL,
            )

        doReturn(testContext).`when`(fragment).requireContext()
        val dialog = fragment.onCreateDialog(null)
        dialog.show()

        val titleTextView = dialog.findViewById<TextView>(R.id.title)
        val optionalOrRequiredTextView = dialog.findViewById<TextView>(R.id.optional_or_required_text)
        val permissionsRecyclerView = dialog.findViewById<RecyclerView>(R.id.permissions)
        val recyclerAdapter = permissionsRecyclerView.adapter!! as RequiredPermissionsAdapter
        val optionalOrRequiredDataCollectionTextView =
            dialog.findViewById<TextView>(R.id.optional_or_required_data_collection_text)
        val dataCollectionPermissionsItem = dialog.findViewById<LinearLayout>(R.id.data_collection_permissions_item)
        val optionalSettingsTitle = dialog.findViewById<TextView>(R.id.optional_settings_title)
        val technicalAndInteractionDataCheckbox =
            dialog.findViewById<AppCompatCheckBox>(R.id.technical_and_interaction_data)

        val name = addon.translateName(testContext)
        assertTrue(titleTextView.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_optional_permissions_with_data_collection_only_dialog_title,
                name,
            ),
            titleTextView.text,
        )
        assertFalse(optionalOrRequiredTextView.isVisible)
        assertEquals(0, recyclerAdapter.itemCount)

        assertTrue(optionalOrRequiredDataCollectionTextView.isVisible)
        assertEquals(
            testContext.getString(R.string.mozac_feature_addons_permissions_dialog_heading_optional_data_collection),
            optionalOrRequiredDataCollectionTextView.text,
        )
        assertTrue(dataCollectionPermissionsItem.isVisible)
        assertEquals(
            testContext.getString(
                R.string.mozac_feature_addons_permissions_data_collection_optional_description,
                Addon.formatLocalizedDataCollectionPermissions(
                    Addon.localizeDataCollectionPermissions(
                        dataCollectionPermissions,
                        testContext,
                    )
                ),
            ),
            dataCollectionPermissionsItem.findViewById<TextView>(R.id.data_collection_permissions).text,
        )

        assertFalse(optionalSettingsTitle.isVisible)
        assertFalse(technicalAndInteractionDataCheckbox.isVisible)
    }

    private fun createPermissionsDialogFragment(
        addon: Addon,
        permissions: List<String> = emptyList(),
        origins: List<String> = emptyList(),
        dataCollectionPermissions: List<String> = emptyList(),
        promptsStyling: PromptsStyling? = null,
        mode: PermissionsDialogMode = PermissionsDialogMode.INSTALL,
        promptInstanceId: String? = null,
        dispatcher: CoroutineDispatcher? = null,
    ): PermissionsDialogFragment {
        return spy(
                PermissionsDialogFragment.newInstance(
                    addon = addon,
                    permissions = permissions,
                    origins = origins,
                    dataCollectionPermissions = dataCollectionPermissions,
                    promptsStyling = promptsStyling,
                    mode = mode,
                    promptInstanceId = promptInstanceId,
                    mainDispatcher = dispatcher,
                )
            )
            .apply {
                doNothing().`when`(this).dismiss()

                val lifecycle = LifecycleRegistry(this)
                doReturn(lifecycle).`when`(this).lifecycle
                doAnswer { invocation ->
                        val dialog = invocation.callRealMethod()
                        lifecycle.handleLifecycleEvent(Lifecycle.Event.ON_RESUME)
                        dialog
                    }
                    .`when`(this)
                    .onCreateDialog(any())
            }
    }

    private fun mockFragmentManager(): FragmentManager {
        val fragmentManager: FragmentManager = mock()
        val transaction: FragmentTransaction = mock()
        doReturn(transaction).`when`(fragmentManager).beginTransaction()
        return fragmentManager
    }
}
