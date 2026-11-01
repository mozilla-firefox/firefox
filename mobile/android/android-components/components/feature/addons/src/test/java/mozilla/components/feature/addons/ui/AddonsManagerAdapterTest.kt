/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons.ui

import android.graphics.Paint
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.ImageView
import android.widget.TextView
import androidx.compose.ui.platform.ComposeView
import androidx.core.content.ContextCompat
import androidx.core.view.isVisible
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.util.Locale
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.WebExtensionState
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.R
import mozilla.components.feature.addons.ui.AddonsManagerAdapter.DifferCallback
import mozilla.components.feature.addons.ui.AddonsManagerAdapter.NotYetSupportedSection
import mozilla.components.feature.addons.ui.AddonsManagerAdapter.Section
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.any
import org.mockito.Mockito.argThat
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`

@RunWith(AndroidJUnit4::class)
class AddonsManagerAdapterTest {

    // We must pass these variables to `bindAddon()` because looking up the version name
    // requires package info that we do not have in the test context.
    private val appName = "Firefox"
    private val appVersion = "1.2.3"

    @Before
    fun setUp() {
        Locale.setDefault(Locale.ENGLISH)
    }

    @Test
    fun `createListWithSections`() {
        val adapter = AddonsManagerAdapter(mock(), emptyList(), mock(), emptyList(), BrowserStore())

        val installedAddon: Addon = mock()
        val recommendedAddon: Addon = mock()
        val unsupportedAddon: Addon = mock()
        val disabledAddon: Addon = mock()

        `when`(installedAddon.isInstalled()).thenReturn(true)
        `when`(installedAddon.isEnabled()).thenReturn(true)
        `when`(installedAddon.isSupported()).thenReturn(true)
        `when`(unsupportedAddon.isInstalled()).thenReturn(true)
        `when`(unsupportedAddon.isSupported()).thenReturn(false)
        `when`(disabledAddon.isEnabled()).thenReturn(false)
        `when`(disabledAddon.isInstalled()).thenReturn(true)
        `when`(disabledAddon.isSupported()).thenReturn(true)

        val addons = listOf(installedAddon, recommendedAddon, unsupportedAddon, disabledAddon)

        assertEquals(0, adapter.itemCount)

        val itemsWithSections = adapter.createListWithSections(addons)

        assertEquals(7, itemsWithSections.size)
        assertEquals(
            R.string.mozac_feature_addons_enabled,
            (itemsWithSections[0] as Section).title,
        )
        assertEquals(installedAddon, itemsWithSections[1])
        assertEquals(
            R.string.mozac_feature_addons_disabled_section,
            (itemsWithSections[2] as Section).title,
        )
        assertEquals(disabledAddon, itemsWithSections[3])
        assertEquals(
            R.string.mozac_feature_addons_recommended_section,
            (itemsWithSections[4] as Section).title,
        )
        assertEquals(recommendedAddon, itemsWithSections[5])
        assertEquals(
            R.string.mozac_feature_addons_unavailable_section,
            (itemsWithSections[6] as NotYetSupportedSection).title,
        )

        // Test if excluededAddonIDs are excluded from recommended section
        val excludedAddonIDs = listOf(recommendedAddon.id)
        val itemsWithSections2 = adapter.createListWithSections(addons, excludedAddonIDs)

        // The only recommended addon should be excluded, so the recommended section should be null
        // Section size should shrink from 7 to 6
        assertEquals(6, itemsWithSections2.size)
        // There should be no section between the titles of Recommended & NotYetSupported
        assertEquals(
            R.string.mozac_feature_addons_recommended_section,
            (itemsWithSections2[4] as Section).title,
        )
        assertEquals(
            R.string.mozac_feature_addons_unavailable_section,
            (itemsWithSections2[5] as NotYetSupportedSection).title,
        )
    }

    @Test
    fun `bind add-on`() {
        val contentWrapperView = View(testContext)
        val titleView: TextView = mock()
        val summaryView: TextView = mock()
        val ratingAccessibleView: TextView = mock()
        val reviewCountView: TextView = mock()
        val addButton = ImageView(testContext)
        val view = View(testContext)
        val allowedInPrivateBrowsingLabel = ImageView(testContext)
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        val iconView = mock<ImageView>()
        whenever(iconView.context).thenReturn(testContext)
        val messageBarErrorView: View = mock()
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_text)).thenReturn(mock())
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_learn_more_link))
            .thenReturn(mock())
        val addonViewHolder =
            CustomViewHolder.AddonViewHolder(
                view = view,
                contentWrapperView = contentWrapperView,
                iconView = iconView,
                titleView = titleView,
                summaryView = summaryView,
                ratingView = mock(),
                ratingAccessibleView = ratingAccessibleView,
                reviewCountView = reviewCountView,
                addButton = addButton,
                allowedInPrivateBrowsingLabel = allowedInPrivateBrowsingLabel,
                messageBarWarningView = mock(),
                messageBarErrorView = messageBarErrorView,
                messageBarUpdateView = mock(),
            )
        val addon =
            Addon(
                id = "id",
                downloadUrl = "downloadUrl",
                version = "version",
                permissions = emptyList(),
                rating = Addon.Rating(4.5f, 1000),
                createdAt = "",
                updatedAt = "",
                translatableName = mapOf(Addon.DEFAULT_LOCALE to "name", "de" to "Name", "es" to "nombre"),
                translatableDescription =
                    mapOf(Addon.DEFAULT_LOCALE to "description", "de" to "Beschreibung", "es" to "descripción"),
                translatableSummary =
                    mapOf(Addon.DEFAULT_LOCALE to "summary", "de" to "Kurzfassung", "es" to "resumen"),
            )

        whenever(titleView.context).thenReturn(testContext)
        whenever(summaryView.context).thenReturn(testContext)

        val style =
            AddonsManagerAdapter.Style(
                sectionsTextColor = android.R.color.black,
                addonNameTextColor = android.R.color.transparent,
                addonSummaryTextColor = android.R.color.white,
            )
        val adapter =
            AddonsManagerAdapter(addonsManagerAdapterDelegate, emptyList(), style, emptyList(), BrowserStore())

        adapter.bindAddon(addonViewHolder, addon, appName, appVersion)

        verify(ratingAccessibleView).setText("Rating: 4.50 out of 5")
        verify(titleView).setText("name")
        verify(titleView).setTextColor(ContextCompat.getColor(testContext, style.addonNameTextColor!!))
        verify(summaryView).setText("summary")
        verify(summaryView).setTextColor(ContextCompat.getColor(testContext, style.addonSummaryTextColor!!))
        assertNotNull(addonViewHolder.itemView.tag)

        addonViewHolder.contentWrapperView.performClick()
        verify(addonsManagerAdapterDelegate).onAddonItemClicked(addon)
        addButton.performClick()
        verify(addonsManagerAdapterDelegate).onInstallAddonButtonClicked(addon)
    }

    @Test
    fun `bind section`() {
        val titleView: TextView = mock()
        val divider: View = mock()
        val sectionViewHolder = CustomViewHolder.SectionViewHolder(View(testContext), titleView, divider)
        val position = 0

        whenever(titleView.context).thenReturn(testContext)

        val style =
            AddonsManagerAdapter.Style(
                sectionsTextColor = android.R.color.black,
                sectionsTypeFace = mock(),
            )
        val adapter = AddonsManagerAdapter(mock(), emptyList(), style, emptyList(), BrowserStore())
        // Force-add a Section item in the list.
        adapter.submitList(null)
        adapter.submitList(listOf(Section(R.string.mozac_feature_addons_disabled_section)))
        // Make sure we have the Footer item in the list.
        assertEquals(1, adapter.itemCount)

        // Use the "public" method to bind the section.
        adapter.bindViewHolder(sectionViewHolder, position)

        verify(titleView).setText(R.string.mozac_feature_addons_disabled_section)
        verify(titleView).typeface = style.sectionsTypeFace
        verify(titleView).setTextColor(ContextCompat.getColor(testContext, style.sectionsTextColor!!))
        verify(divider).isVisible = style.visibleDividers && position != 0
        assertNotNull(sectionViewHolder.itemView.accessibilityDelegate)

        val nodeInfo: AccessibilityNodeInfo = mock()
        sectionViewHolder.itemView.accessibilityDelegate.onInitializeAccessibilityNodeInfo(mock(), nodeInfo)
        verify(nodeInfo).collectionItemInfo = argThat {
            // We cannot verify `rowIndex` because we're using `bindingAdapterPosition`.
            @Suppress("DEPRECATION")
            it.rowSpan == 1 && it.columnIndex == 1 && it.columnSpan == 1 && it.isHeading
        }
    }

    @Test
    fun `default section divider visibility is 'GONE' for position 0`() {
        val titleView: TextView = mock()
        val divider: View = mock()
        val addonViewHolder = CustomViewHolder.SectionViewHolder(View(testContext), titleView, divider)
        val position = 0

        whenever(titleView.context).thenReturn(testContext)

        val style =
            AddonsManagerAdapter.Style(
                sectionsTextColor = android.R.color.black,
                sectionsTypeFace = mock(),
            )
        val adapter = AddonsManagerAdapter(mock(), emptyList(), style, emptyList(), BrowserStore())

        adapter.bindSection(addonViewHolder, Section(R.string.mozac_feature_addons_disabled_section), position)

        verify(divider).visibility = View.GONE
    }

    @Test
    fun `default section divider visibility is 'VISIBLE' for other position than 0`() {
        val titleView: TextView = mock()
        val divider: View = mock()
        val addonViewHolder = CustomViewHolder.SectionViewHolder(View(testContext), titleView, divider)
        val position = 2

        whenever(titleView.context).thenReturn(testContext)

        val style =
            AddonsManagerAdapter.Style(
                sectionsTextColor = android.R.color.black,
                sectionsTypeFace = mock(),
            )
        val adapter = AddonsManagerAdapter(mock(), emptyList(), style, emptyList(), BrowserStore())

        adapter.bindSection(addonViewHolder, Section(R.string.mozac_feature_addons_disabled_section), position)

        verify(divider).visibility = View.VISIBLE
    }

    @Test
    fun `section divider visibility is 'GONE' when set as such in style`() {
        val titleView: TextView = mock()
        val divider: View = mock()
        val addonViewHolder = CustomViewHolder.SectionViewHolder(View(testContext), titleView, divider)
        val position = 2

        whenever(titleView.context).thenReturn(testContext)

        val style =
            AddonsManagerAdapter.Style(
                sectionsTextColor = android.R.color.black,
                sectionsTypeFace = mock(),
                visibleDividers = false,
            )
        val adapter = AddonsManagerAdapter(mock(), emptyList(), style, emptyList(), BrowserStore())

        adapter.bindSection(addonViewHolder, Section(R.string.mozac_feature_addons_disabled_section), position)

        verify(divider).visibility = View.GONE
    }

    @Test
    fun `section divider style is set when arguments are passed in style`() {
        val titleView: TextView = mock()
        val divider: View = mock()
        val addonViewHolder = CustomViewHolder.SectionViewHolder(View(testContext), titleView, divider)
        val position = 2
        val dividerHeight = android.R.dimen.notification_large_icon_height
        val dividerColor = android.R.color.white

        whenever(titleView.context).thenReturn(testContext)
        whenever(divider.context).thenReturn(testContext)
        whenever(divider.layoutParams).thenReturn(mock())

        val style =
            AddonsManagerAdapter.Style(
                sectionsTextColor = android.R.color.black,
                sectionsTypeFace = mock(),
                visibleDividers = true,
                dividerColor = dividerColor,
                dividerHeight = dividerHeight,
            )

        val adapter = AddonsManagerAdapter(mock(), emptyList(), style, emptyList(), BrowserStore())

        adapter.bindSection(addonViewHolder, Section(R.string.mozac_feature_addons_disabled_section), position)

        verify(divider).visibility = View.VISIBLE
        verify(divider).setBackgroundColor(dividerColor)
        verify(divider).layoutParams
        assertEquals(testContext.resources.getDimensionPixelOffset(dividerHeight), divider.layoutParams.height)
    }

    @Test
    fun `bind add-on with no available translatable name`() {
        val titleView: TextView = mock()
        val summaryView: TextView = mock()
        val view = View(testContext)
        val allowedInPrivateBrowsingLabel = ImageView(testContext)
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        val iconView = mock<ImageView>()
        whenever(iconView.context).thenReturn(testContext)
        val messageBarErrorView: View = mock()
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_text)).thenReturn(mock())
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_learn_more_link))
            .thenReturn(mock())
        val addonViewHolder =
            CustomViewHolder.AddonViewHolder(
                view = view,
                contentWrapperView = mock(),
                iconView = iconView,
                titleView = titleView,
                summaryView = summaryView,
                ratingView = mock(),
                ratingAccessibleView = mock(),
                reviewCountView = mock(),
                addButton = mock(),
                allowedInPrivateBrowsingLabel = allowedInPrivateBrowsingLabel,
                messageBarWarningView = mock(),
                messageBarErrorView = messageBarErrorView,
                messageBarUpdateView = mock(),
            )
        val addon =
            Addon(
                id = "id",
                downloadUrl = "downloadUrl",
                version = "version",
                permissions = emptyList(),
                createdAt = "",
                updatedAt = "",
            )
        val adapter =
            AddonsManagerAdapter(addonsManagerAdapterDelegate, emptyList(), mock(), emptyList(), BrowserStore())

        adapter.bindAddon(addonViewHolder, addon, appName, appVersion)
        verify(titleView).setText("id")
        verify(summaryView).setVisibility(View.GONE)
    }

    @Test
    fun updateAddon() {
        var addon =
            Addon(
                id = "id",
                downloadUrl = "downloadUrl",
                version = "version",
                permissions = emptyList(),
                createdAt = "",
                updatedAt = "",
            )
        val adapter = spy(AddonsManagerAdapter(mock(), listOf(addon), mock(), emptyList(), BrowserStore()))

        assertEquals(addon, adapter.addonsMap[addon.id])

        addon = addon.copy(version = "newVersion")
        adapter.updateAddon(addon)

        assertEquals(addon.version, adapter.addonsMap[addon.id]!!.version)
        verify(adapter).submitList(any())

        // Once the list is submitted, we should have the right item count on the adapter.
        // In this case, we have 1 heading and 1 add-on.
        assertEquals(2, adapter.itemCount)
    }

    @Test
    fun updateAddons() {
        var addon1 =
            Addon(
                id = "addon1",
                downloadUrl = "downloadUrl",
                version = "version",
                permissions = emptyList(),
                createdAt = "",
                updatedAt = "",
            )

        val addon2 =
            Addon(
                id = "addon2",
                downloadUrl = "downloadUrl",
                version = "version",
                permissions = emptyList(),
                createdAt = "",
                updatedAt = "",
            )
        val adapter = spy(AddonsManagerAdapter(mock(), listOf(addon1, addon2), mock(), emptyList(), BrowserStore()))

        assertEquals(addon1, adapter.addonsMap[addon1.id])
        assertEquals(addon2, adapter.addonsMap[addon2.id])

        addon1 = addon1.copy(version = "newVersion")
        adapter.updateAddons(listOf(addon1, addon2))

        // Only addon1 must be updated
        assertEquals(addon1.version, adapter.addonsMap[addon1.id]!!.version)
        assertEquals(addon2, adapter.addonsMap[addon2.id])
        verify(adapter).submitList(any())
    }

    @Test
    fun differCallback() {
        var addon1 =
            Addon(
                id = "addon1",
                downloadUrl = "downloadUrl",
                version = "version",
                permissions = emptyList(),
                createdAt = "",
                updatedAt = "",
            )

        var addon2 =
            Addon(
                id = "addon1",
                downloadUrl = "downloadUrl",
                version = "version",
                permissions = emptyList(),
                createdAt = "",
                updatedAt = "",
            )

        assertTrue(DifferCallback.areItemsTheSame(addon1, addon2))

        addon2 = addon2.copy(id = "addon2")

        assertFalse(DifferCallback.areItemsTheSame(addon1, addon2))

        addon2 = addon2.copy(id = "addon1")

        assertTrue(DifferCallback.areItemsTheSame(addon1, addon2))

        addon1 = addon1.copy(version = "newVersion")

        assertFalse(DifferCallback.areContentsTheSame(addon1, addon2))
    }

    @Test
    fun bindNotYetSupportedSection() {
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        val titleView: TextView = mock()
        val descriptionView: TextView = mock()
        val view = View(testContext)
        val unsupportedSectionViewHolder =
            CustomViewHolder.UnsupportedSectionViewHolder(
                view,
                titleView,
                descriptionView,
            )
        val unsupportedAddon =
            Addon(
                id = "id",
                installedState =
                    Addon.InstalledState(
                        id = "id",
                        version = "version",
                        optionsPageUrl = "optionsPageUrl",
                        supported = false,
                    ),
            )
        val unsupportedAddonTwo =
            Addon(
                id = "id2",
                installedState =
                    Addon.InstalledState(
                        id = "id2",
                        version = "version2",
                        optionsPageUrl = "optionsPageUrl2",
                        supported = false,
                    ),
            )
        val unsupportedAddons = arrayListOf(unsupportedAddon, unsupportedAddonTwo)
        val adapter =
            AddonsManagerAdapter(
                addonsManagerAdapterDelegate,
                unsupportedAddons,
                mock(),
                emptyList(),
                BrowserStore(),
            )

        adapter.bindNotYetSupportedSection(unsupportedSectionViewHolder, mock())
        verify(unsupportedSectionViewHolder.descriptionView)
            .setText(
                testContext.getString(
                    R.string.mozac_feature_addons_unsupported_caption_plural_2,
                    unsupportedAddons.size,
                )
            )

        unsupportedSectionViewHolder.itemView.performClick()
        verify(addonsManagerAdapterDelegate).onNotYetSupportedSectionClicked(unsupportedAddons)
    }

    @Test
    fun bindFooterButton() {
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        val adapter =
            AddonsManagerAdapter(addonsManagerAdapterDelegate, emptyList(), mock(), emptyList(), BrowserStore())
        val view = View(testContext)
        val viewHolder = CustomViewHolder.FooterViewHolder(view)
        // Make sure we have the Footer item in the list.
        adapter.submitList(null)
        adapter.submitList(listOf(AddonsManagerAdapter.FooterSection))
        assertEquals(1, adapter.itemCount)

        // Use the "public" method to bind the footer.
        adapter.onBindViewHolder(viewHolder, 0)

        viewHolder.itemView.performClick()
        verify(addonsManagerAdapterDelegate).onFindMoreAddonsButtonClicked()
        assertNotNull(viewHolder.itemView.accessibilityDelegate)

        val nodeInfo: AccessibilityNodeInfo = mock()
        viewHolder.itemView.accessibilityDelegate.onInitializeAccessibilityNodeInfo(mock(), nodeInfo)
        verify(nodeInfo).collectionItemInfo = argThat {
            // We cannot verify `rowIndex` because we're using `bindingAdapterPosition`.
            @Suppress("DEPRECATION")
            it.rowSpan == 1 && it.columnIndex == 1 && it.columnSpan == 1 && !it.isHeading
        }
    }

    @Test
    fun bindHeaderButton() = runTest {
        val store = BrowserStore(initialState = BrowserState(extensionsProcessDisabled = true))
        val adapter = spy(AddonsManagerAdapter(mock(), emptyList(), mock(), emptyList(), store))

        val restartButton = TextView(testContext)
        val viewHolder = CustomViewHolder.HeaderViewHolder(View(testContext), restartButton)
        adapter.bindHeaderButton(viewHolder)
        assertEquals(1, adapter.currentList.size)

        viewHolder.restartButton.performClick()
        testScheduler.advanceUntilIdle()

        assertFalse(store.state.extensionsProcessDisabled)
        verify(adapter).submitList(emptyList())
    }

    @Test
    fun testNotificationShownWhenProcessIsDisabled() {
        val store = BrowserStore(initialState = BrowserState(extensionsProcessDisabled = true))
        val adapter = AddonsManagerAdapter(mock(), emptyList(), mock(), emptyList(), store)

        val itemsWithSections = adapter.createListWithSections(emptyList())
        assertEquals(AddonsManagerAdapter.HeaderSection, itemsWithSections.first())
    }

    @Test
    fun testNotificationNotShownWhenProcessIsEnabled() {
        val store = BrowserStore(initialState = BrowserState(extensionsProcessDisabled = false))
        val adapter = AddonsManagerAdapter(mock(), emptyList(), mock(), emptyList(), store)

        val itemsWithSections = adapter.createListWithSections(emptyList())
        assertTrue(itemsWithSections.isEmpty())
    }

    @Test
    fun testFindMoreAddonsButtonIsHidden() {
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        whenever(addonsManagerAdapterDelegate.shouldShowFindMoreAddonsButton()).thenReturn(false)
        val adapter =
            AddonsManagerAdapter(addonsManagerAdapterDelegate, emptyList(), mock(), emptyList(), BrowserStore())

        val itemsWithSections = adapter.createListWithSections(emptyList())
        assertTrue(itemsWithSections.isEmpty())
    }

    @Test
    fun testFindMoreAddonsButtonIsVisible() {
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        whenever(addonsManagerAdapterDelegate.shouldShowFindMoreAddonsButton()).thenReturn(true)
        val adapter =
            AddonsManagerAdapter(addonsManagerAdapterDelegate, emptyList(), mock(), emptyList(), BrowserStore())

        val itemsWithSections = adapter.createListWithSections(emptyList())
        assertFalse(itemsWithSections.isEmpty())
        assertEquals(AddonsManagerAdapter.FooterSection, itemsWithSections.last())
    }

    @Test
    fun `bind blocklisted add-on`() {
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        val titleView: TextView = mock()
        whenever(titleView.context).thenReturn(testContext)
        val summaryView: TextView = mock()
        whenever(summaryView.context).thenReturn(testContext)
        val messageBarWarningView: View = mock()
        val messageBarErrorView: View = mock()
        val messageTextView: TextView = mock()
        val learnMoreTextView = TextView(testContext)
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_text))
            .thenReturn(messageTextView)
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_learn_more_link))
            .thenReturn(learnMoreTextView)
        val iconView = mock<ImageView>()
        whenever(iconView.context).thenReturn(testContext)
        val addonViewHolder =
            CustomViewHolder.AddonViewHolder(
                view = View(testContext),
                contentWrapperView = mock(),
                iconView = iconView,
                titleView = titleView,
                summaryView = summaryView,
                ratingView = mock(),
                ratingAccessibleView = mock(),
                reviewCountView = mock(),
                addButton = mock(),
                allowedInPrivateBrowsingLabel = mock(),
                messageBarWarningView = messageBarWarningView,
                messageBarErrorView = messageBarErrorView,
                messageBarUpdateView = mock(),
            )
        val addonName = "some addon name"
        val addon = makeDisabledAddon(Addon.DisabledReason.BLOCKLISTED, addonName)
        val adapter =
            AddonsManagerAdapter(addonsManagerAdapterDelegate, emptyList(), mock(), emptyList(), BrowserStore())

        adapter.bindAddon(addonViewHolder, addon, appName, appVersion)

        verify(messageBarWarningView).isVisible = false
        verify(messageBarErrorView).isVisible = true
        verify(messageTextView).text =
            "This extension is blocked for violating Mozilla’s policies and has been disabled."

        // Since we link to a non-SUMO page, we should update the text for this link.
        assertEquals(learnMoreTextView.text, "See details")
        assertEquals(learnMoreTextView.paintFlags, Paint.UNDERLINE_TEXT_FLAG)

        // Verify that a click on the "learn more" link actually does something.
        learnMoreTextView.performClick()
        verify(addonsManagerAdapterDelegate)
            .onLearnMoreLinkClicked(
                AddonsManagerAdapterDelegate.LearnMoreLinks.BLOCKLISTED_ADDON,
                addon,
            )
    }

    @Test
    fun `bind add-on not correctly signed`() {
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        val titleView: TextView = mock()
        whenever(titleView.context).thenReturn(testContext)
        val summaryView: TextView = mock()
        whenever(summaryView.context).thenReturn(testContext)
        val messageBarWarningView: View = mock()
        val messageBarErrorView: View = mock()
        val messageTextView: TextView = mock()
        val learnMoreTextView = TextView(testContext)
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_text))
            .thenReturn(messageTextView)
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_learn_more_link))
            .thenReturn(learnMoreTextView)
        val iconView = mock<ImageView>()
        whenever(iconView.context).thenReturn(testContext)
        val addonViewHolder =
            CustomViewHolder.AddonViewHolder(
                view = View(testContext),
                contentWrapperView = mock(),
                iconView = iconView,
                titleView = titleView,
                summaryView = summaryView,
                ratingView = mock(),
                ratingAccessibleView = mock(),
                reviewCountView = mock(),
                addButton = mock(),
                allowedInPrivateBrowsingLabel = mock(),
                messageBarWarningView = messageBarWarningView,
                messageBarErrorView = messageBarErrorView,
                messageBarUpdateView = mock(),
            )
        val addonName = "some addon name"
        val addon = makeDisabledAddon(Addon.DisabledReason.NOT_CORRECTLY_SIGNED, addonName)
        val adapter =
            AddonsManagerAdapter(addonsManagerAdapterDelegate, emptyList(), mock(), emptyList(), BrowserStore())

        adapter.bindAddon(addonViewHolder, addon, appName, appVersion)

        verify(messageBarWarningView).isVisible = false
        verify(messageBarErrorView).isVisible = true
        verify(messageTextView).text = "$addonName could not be verified as secure and has been disabled."
        assertEquals(learnMoreTextView.paintFlags, Paint.UNDERLINE_TEXT_FLAG)

        // Verify that a click on the "learn more" link actually does something.
        learnMoreTextView.performClick()
        verify(addonsManagerAdapterDelegate)
            .onLearnMoreLinkClicked(
                AddonsManagerAdapterDelegate.LearnMoreLinks.ADDON_NOT_CORRECTLY_SIGNED,
                addon,
            )
    }

    @Test
    fun `bind add-on not correctly signed and without a name`() {
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        val titleView: TextView = mock()
        whenever(titleView.context).thenReturn(testContext)
        val summaryView: TextView = mock()
        whenever(summaryView.context).thenReturn(testContext)
        val messageBarWarningView: View = mock()
        val messageBarErrorView: View = mock()
        val messageTextView: TextView = mock()
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_text))
            .thenReturn(messageTextView)
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_learn_more_link))
            .thenReturn(mock())

        val iconView = mock<ImageView>()
        whenever(iconView.context).thenReturn(testContext)

        val addonViewHolder =
            CustomViewHolder.AddonViewHolder(
                view = View(testContext),
                contentWrapperView = mock(),
                iconView = iconView,
                titleView = titleView,
                summaryView = summaryView,
                ratingView = mock(),
                ratingAccessibleView = mock(),
                reviewCountView = mock(),
                addButton = mock(),
                allowedInPrivateBrowsingLabel = mock(),
                messageBarWarningView = messageBarWarningView,
                messageBarErrorView = messageBarErrorView,
                messageBarUpdateView = mock(),
            )
        val addon = makeDisabledAddon(Addon.DisabledReason.NOT_CORRECTLY_SIGNED)
        val adapter =
            AddonsManagerAdapter(addonsManagerAdapterDelegate, emptyList(), mock(), emptyList(), BrowserStore())

        adapter.bindAddon(addonViewHolder, addon, appName, appVersion)

        verify(messageBarWarningView).isVisible = false
        verify(messageBarErrorView).isVisible = true
        verify(messageTextView).text = "${addon.id} could not be verified as secure and has been disabled."
    }

    @Test
    fun `bind incompatible add-on`() {
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        val titleView: TextView = mock()
        whenever(titleView.context).thenReturn(testContext)
        val summaryView: TextView = mock()
        whenever(summaryView.context).thenReturn(testContext)
        val messageBarWarningView: View = mock()
        val messageBarErrorView: View = mock()
        val messageTextView: TextView = mock()
        val learnMoreTextView: TextView = mock()
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_text))
            .thenReturn(messageTextView)
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_learn_more_link))
            .thenReturn(learnMoreTextView)

        val iconView = mock<ImageView>()
        whenever(iconView.context).thenReturn(testContext)

        val addonViewHolder =
            CustomViewHolder.AddonViewHolder(
                view = View(testContext),
                contentWrapperView = mock(),
                iconView = iconView,
                titleView = titleView,
                summaryView = summaryView,
                ratingView = mock(),
                ratingAccessibleView = mock(),
                reviewCountView = mock(),
                addButton = mock(),
                allowedInPrivateBrowsingLabel = mock(),
                messageBarWarningView = messageBarWarningView,
                messageBarErrorView = messageBarErrorView,
                messageBarUpdateView = mock(),
            )
        val addonName = "some addon name"
        val addon = makeDisabledAddon(Addon.DisabledReason.INCOMPATIBLE, addonName)
        val adapter =
            AddonsManagerAdapter(addonsManagerAdapterDelegate, emptyList(), mock(), emptyList(), BrowserStore())

        adapter.bindAddon(addonViewHolder, addon, appName, appVersion)

        verify(messageBarWarningView).isVisible = false
        verify(messageBarErrorView).isVisible = true
        verify(messageTextView).text =
            "$addonName is not compatible with your version of $appName (version $appVersion)."
        verify(learnMoreTextView).isVisible = false
    }

    @Test
    fun `bind incompatible add-on and without a name`() {
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        val titleView: TextView = mock()
        whenever(titleView.context).thenReturn(testContext)
        val summaryView: TextView = mock()
        whenever(summaryView.context).thenReturn(testContext)
        val messageBarWarningView: View = mock()
        val messageBarErrorView: View = mock()
        val messageTextView: TextView = mock()
        val learnMoreTextView: TextView = mock()
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_text))
            .thenReturn(messageTextView)
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_learn_more_link))
            .thenReturn(learnMoreTextView)
        val iconView = mock<ImageView>()
        whenever(iconView.context).thenReturn(testContext)

        val addonViewHolder =
            CustomViewHolder.AddonViewHolder(
                view = View(testContext),
                contentWrapperView = mock(),
                iconView = iconView,
                titleView = titleView,
                summaryView = summaryView,
                ratingView = mock(),
                ratingAccessibleView = mock(),
                reviewCountView = mock(),
                addButton = mock(),
                allowedInPrivateBrowsingLabel = mock(),
                messageBarWarningView = messageBarWarningView,
                messageBarErrorView = messageBarErrorView,
                messageBarUpdateView = mock(),
            )
        val addon = makeDisabledAddon(Addon.DisabledReason.INCOMPATIBLE)
        val adapter =
            AddonsManagerAdapter(addonsManagerAdapterDelegate, emptyList(), mock(), emptyList(), BrowserStore())

        adapter.bindAddon(addonViewHolder, addon, appName, appVersion)

        verify(messageBarWarningView).isVisible = false
        verify(messageBarErrorView).isVisible = true
        verify(messageTextView).text =
            "${addon.id} is not compatible with your version of $appName (version $appVersion)."
        verify(learnMoreTextView).isVisible = false
    }

    @Test
    fun `bind soft-blocked add-on`() {
        val addon = makeDisabledAddon(Addon.DisabledReason.SOFT_BLOCKED)
        whenever(addon.isEnabled()).thenReturn(false)
        val expectedMessage =
            "This extension is restricted and has been disabled. You can enable it, but this may be risky."

        bindSoftBlockedAddon(addon, expectedMessage)
    }

    @Test
    fun `bind soft-blocked add-on that has been re-enabled by user`() {
        val addon = makeDisabledAddon(Addon.DisabledReason.SOFT_BLOCKED)
        whenever(addon.isEnabled()).thenReturn(true)
        val expectedMessage = "This extension is restricted. Using it may be risky."

        bindSoftBlockedAddon(addon, expectedMessage)
    }

    private fun bindSoftBlockedAddon(addon: Addon, expectedMessage: String) {
        val addonsManagerAdapterDelegate: AddonsManagerAdapterDelegate = mock()
        val titleView: TextView = mock()
        whenever(titleView.context).thenReturn(testContext)
        val summaryView: TextView = mock()
        whenever(summaryView.context).thenReturn(testContext)
        val messageBarWarningView: View = mock()
        val messageTextView: TextView = mock()
        val learnMoreTextView = TextView(testContext)
        whenever(messageBarWarningView.findViewById<TextView>(R.id.add_on_messagebar_warning_text))
            .thenReturn(messageTextView)
        whenever(messageBarWarningView.findViewById<TextView>(R.id.add_on_messagebar_warning_learn_more_link))
            .thenReturn(learnMoreTextView)
        val messageBarErrorView: View = mock()
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_text)).thenReturn(mock())
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_learn_more_link))
            .thenReturn(mock())
        val iconView = mock<ImageView>()
        whenever(iconView.context).thenReturn(testContext)
        val addonViewHolder =
            CustomViewHolder.AddonViewHolder(
                view = View(testContext),
                contentWrapperView = mock(),
                iconView = iconView,
                titleView = titleView,
                summaryView = summaryView,
                ratingView = mock(),
                ratingAccessibleView = mock(),
                reviewCountView = mock(),
                addButton = mock(),
                allowedInPrivateBrowsingLabel = mock(),
                messageBarWarningView = messageBarWarningView,
                messageBarErrorView = messageBarErrorView,
                messageBarUpdateView = mock(),
            )
        val adapter =
            AddonsManagerAdapter(addonsManagerAdapterDelegate, emptyList(), mock(), emptyList(), BrowserStore())

        adapter.bindAddon(addonViewHolder, addon, appName, appVersion)

        verify(messageBarErrorView).isVisible = false
        verify(messageBarWarningView).isVisible = true
        verify(messageTextView).text = expectedMessage
        assertEquals(learnMoreTextView.paintFlags, Paint.UNDERLINE_TEXT_FLAG)

        // Verify that a click on the "see details" link actually does something.
        learnMoreTextView.performClick()
        verify(addonsManagerAdapterDelegate)
            .onLearnMoreLinkClicked(
                // This is the same link for both hard and soft blocked add-ons.
                AddonsManagerAdapterDelegate.LearnMoreLinks.BLOCKLISTED_ADDON,
                addon,
            )
    }

    @Test
    fun `bindMessageBars shows the update banner only when there is a pending update message`() {
        val messageBarErrorView: View = mock()
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_text)).thenReturn(mock())
        whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_learn_more_link))
            .thenReturn(mock())
        val messageBarUpdateView = ComposeView(testContext)
        val addon = Addon(id = "some-id")

        AddonsManagerAdapter.bindMessageBars(
            testContext,
            messageBarWarningView = mock(),
            messageBarErrorView = messageBarErrorView,
            messageBarUpdateView = messageBarUpdateView,
            onLearnMoreLinkClicked = {},
            addon = addon,
            addonName = "name",
            appName = appName,
            appVersion = appVersion,
            pendingUpdateMessage = "name requires new settings to update.",
        )
        assertTrue(messageBarUpdateView.isVisible)

        AddonsManagerAdapter.bindMessageBars(
            testContext,
            messageBarWarningView = mock(),
            messageBarErrorView = messageBarErrorView,
            messageBarUpdateView = messageBarUpdateView,
            onLearnMoreLinkClicked = {},
            addon = addon,
            addonName = "name",
            appName = appName,
            appVersion = appVersion,
        )
        assertFalse(messageBarUpdateView.isVisible)
    }

    @Test
    fun `bind add-on shows the update banner only for an enabled extension with a pending update`() {
        val store =
            BrowserStore(
                BrowserState(
                    extensions =
                        mapOf(
                            "enabled" to WebExtensionState("enabled", enabled = true),
                            "disabled" to WebExtensionState("disabled", enabled = false),
                        ),
                    pendingExtensionUpdates =
                        listOf("enabled", "disabled").associateWith {
                            PendingExtensionUpdate(
                                extensionId = it,
                                permissions = listOf("privacy"),
                                origins = emptyList(),
                                dataCollectionPermissions = emptyList(),
                                onPermissionsGranted = {},
                            )
                        },
                )
            )
        val adapter = AddonsManagerAdapter(mock(), emptyList(), mock(), emptyList(), store)

        fun bannerVisibleFor(addonId: String): Boolean {
            val messageBarErrorView: View = mock()
            whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_text)).thenReturn(mock())
            whenever(messageBarErrorView.findViewById<TextView>(R.id.add_on_messagebar_error_learn_more_link))
                .thenReturn(mock())
            val iconView = mock<ImageView>()
            whenever(iconView.context).thenReturn(testContext)
            val messageBarUpdateView = ComposeView(testContext)
            val holder =
                CustomViewHolder.AddonViewHolder(
                    view = View(testContext),
                    contentWrapperView = mock(),
                    iconView = iconView,
                    titleView = TextView(testContext),
                    summaryView = TextView(testContext),
                    ratingView = mock(),
                    ratingAccessibleView = mock(),
                    reviewCountView = mock(),
                    addButton = mock(),
                    allowedInPrivateBrowsingLabel = mock(),
                    messageBarWarningView = mock(),
                    messageBarErrorView = messageBarErrorView,
                    messageBarUpdateView = messageBarUpdateView,
                )
            adapter.bindAddon(holder, Addon(id = addonId), appName, appVersion)
            return messageBarUpdateView.isVisible
        }

        assertTrue(bannerVisibleFor("enabled"))
        assertFalse(bannerVisibleFor("disabled"))
        assertFalse(bannerVisibleFor("no-update"))
    }

    private fun makeDisabledAddon(disabledReason: Addon.DisabledReason, name: String? = null): Addon {
        val installedState: Addon.InstalledState = mock()
        whenever(installedState.disabledReason).thenReturn(disabledReason)
        return Addon(
            id = "@some-addon-id",
            translatableName =
                if (name != null) {
                    mapOf(Addon.DEFAULT_LOCALE to name)
                } else {
                    emptyMap()
                },
            installedState = installedState,
        )
    }
}
