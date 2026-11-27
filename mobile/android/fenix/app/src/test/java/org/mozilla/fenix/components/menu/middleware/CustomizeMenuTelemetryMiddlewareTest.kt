/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.store.CustomizableMenuAction.BackPressed
import mozilla.components.compose.menu.store.CustomizableMenuAction.EditGroupItemsClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultConfirmed
import mozilla.components.compose.menu.store.CustomizableMenuAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.CustomizeMenu
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.LIBRARY_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.PAGE_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.QUIT_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TABS_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TOOLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.VPN_GROUP_ID
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class CustomizeMenuTelemetryMiddlewareTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    @Test
    fun `WHEN the menu structure is read for the first time THEN record the names of the shown and the removed sections from top to bottom`() {
        val store = createStore()
        assertNull(CustomizeMenu.shown.testGetValue())

        store.dispatch(
            update(
                shown = listOf(PAGE_CONTROLS_GROUP_ID, TOOLS_GROUP_ID, VPN_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
                hidden = listOf(TABS_CONTROLS_GROUP_ID, QUIT_GROUP_ID),
            )
        )

        assertShownRecorded(
            shownSections = "page_controls,tools,vpn,accounts_and_settings",
            hiddenSections = "tabs_controls,quit",
        )
    }

    @Test
    fun `WHEN no section is removed THEN record no removed sections`() {
        val store = createStore()

        store.dispatch(update(shown = listOf(PAGE_CONTROLS_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID)))

        assertShownRecorded(shownSections = "page_controls,accounts_and_settings", hiddenSections = "")
    }

    @Test
    fun `WHEN the menu structure is read again THEN don't record the screen as shown again`() {
        val store = createStore()
        store.dispatch(update(shown = listOf(PAGE_CONTROLS_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID)))

        store.dispatch(update(shown = listOf(ACCOUNTS_AND_SETTINGS_GROUP_ID, PAGE_CONTROLS_GROUP_ID)))

        assertEquals(1, CustomizeMenu.shown.testGetValue()?.size)
    }

    @Test
    fun `GIVEN the screen is recreated with the menu structure it already showed WHEN the menu structure is read again THEN don't record the screen as shown again`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuState(menuGroups = groups(PAGE_CONTROLS_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID))
            )

        store.dispatch(update(shown = listOf(PAGE_CONTROLS_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID)))

        assertNull(CustomizeMenu.shown.testGetValue())
    }

    @Test
    fun `WHEN the user moves a shown section to the hidden ones THEN record that it was hidden and its position among them`() {
        val store =
            createStore(
                shown = listOf(PAGE_CONTROLS_GROUP_ID, TOOLS_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
                hidden = listOf(QUIT_GROUP_ID),
            )
        assertNull(CustomizeMenu.sectionHidden.testGetValue())

        store.dispatch(
            structureChanged(
                shown = listOf(PAGE_CONTROLS_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
                hidden = listOf(QUIT_GROUP_ID, TOOLS_GROUP_ID),
                movedGroupId = TOOLS_GROUP_ID,
            )
        )

        assertSectionHiddenRecorded(section = "tools", position = 2)
        assertNull(CustomizeMenu.sectionUnhidden.testGetValue())
        assertNull(CustomizeMenu.sectionReordered.testGetValue())
        assertNull(CustomizeMenu.shown.testGetValue())
    }

    @Test
    fun `WHEN the user moves a hidden section to the shown ones THEN record that it was unhidden and its position among them`() {
        val store =
            createStore(
                shown = listOf(PAGE_CONTROLS_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
                hidden = listOf(TABS_CONTROLS_GROUP_ID, QUIT_GROUP_ID),
            )
        assertNull(CustomizeMenu.sectionUnhidden.testGetValue())

        store.dispatch(
            structureChanged(
                shown = listOf(PAGE_CONTROLS_GROUP_ID, TABS_CONTROLS_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
                hidden = listOf(QUIT_GROUP_ID),
                movedGroupId = TABS_CONTROLS_GROUP_ID,
            )
        )

        assertSectionUnhiddenRecorded(section = "tabs_controls", position = 2)
        assertNull(CustomizeMenu.sectionHidden.testGetValue())
        assertNull(CustomizeMenu.sectionReordered.testGetValue())
        assertNull(CustomizeMenu.shown.testGetValue())
    }

    @Test
    fun `WHEN the user moves a section among the shown sections THEN record that it was reordered and its position counted from 1`() {
        val store = createStore(shown = listOf(PAGE_CONTROLS_GROUP_ID, TOOLS_GROUP_ID))
        assertNull(CustomizeMenu.sectionReordered.testGetValue())

        store.dispatch(
            structureChanged(
                shown = listOf(TOOLS_GROUP_ID, PAGE_CONTROLS_GROUP_ID),
                movedGroupId = TOOLS_GROUP_ID,
            )
        )

        assertSectionReorderedRecorded(section = "tools", position = 1, hidden = false)
        assertNull(CustomizeMenu.sectionHidden.testGetValue())
        assertNull(CustomizeMenu.sectionUnhidden.testGetValue())
        assertNull(CustomizeMenu.shown.testGetValue())
    }

    @Test
    fun `WHEN the user moves an already hidden section to the top of the hidden ones THEN record that it was reordered among the hidden sections`() {
        val store =
            createStore(
                shown = listOf(PAGE_CONTROLS_GROUP_ID),
                hidden = listOf(QUIT_GROUP_ID, LIBRARY_GROUP_ID),
            )

        store.dispatch(
            structureChanged(
                shown = listOf(PAGE_CONTROLS_GROUP_ID),
                hidden = listOf(LIBRARY_GROUP_ID, QUIT_GROUP_ID),
                movedGroupId = LIBRARY_GROUP_ID,
            )
        )

        assertSectionReorderedRecorded(section = "library", position = 1, hidden = true)
        assertNull(CustomizeMenu.sectionHidden.testGetValue())
        assertNull(CustomizeMenu.sectionUnhidden.testGetValue())
        assertNull(CustomizeMenu.shown.testGetValue())
    }

    @Test
    fun `GIVEN the sections were loaded with a hidden section WHEN the user moves it to the shown ones THEN record that it was unhidden`() {
        val store = createStore()
        store.dispatch(
            update(
                shown = listOf(PAGE_CONTROLS_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
                hidden = listOf(QUIT_GROUP_ID),
            )
        )

        store.dispatch(
            structureChanged(
                shown = listOf(PAGE_CONTROLS_GROUP_ID, QUIT_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
                movedGroupId = QUIT_GROUP_ID,
            )
        )

        assertSectionUnhiddenRecorded(section = "quit", position = 2)
        assertNull(CustomizeMenu.sectionHidden.testGetValue())
        assertNull(CustomizeMenu.sectionReordered.testGetValue())
    }

    @Test
    fun `WHEN the user hides a section and then moves it among the hidden ones THEN record one hide and one reorder`() {
        val store =
            createStore(
                shown = listOf(PAGE_CONTROLS_GROUP_ID, TOOLS_GROUP_ID),
                hidden = listOf(QUIT_GROUP_ID),
            )

        store.dispatch(
            structureChanged(
                shown = listOf(PAGE_CONTROLS_GROUP_ID),
                hidden = listOf(QUIT_GROUP_ID, TOOLS_GROUP_ID),
                movedGroupId = TOOLS_GROUP_ID,
            )
        )
        store.dispatch(
            structureChanged(
                shown = listOf(PAGE_CONTROLS_GROUP_ID),
                hidden = listOf(TOOLS_GROUP_ID, QUIT_GROUP_ID),
                movedGroupId = TOOLS_GROUP_ID,
            )
        )

        assertSectionHiddenRecorded(section = "tools", position = 2)
        assertSectionReorderedRecorded(section = "tools", position = 1, hidden = true)
        assertNull(CustomizeMenu.sectionUnhidden.testGetValue())
        assertNull(CustomizeMenu.shown.testGetValue())
    }

    @Test
    fun `WHEN the user presses back THEN record it`() {
        val store = createStore()
        assertNull(CustomizeMenu.backPressed.testGetValue())

        store.dispatch(BackPressed)

        assertEquals(1, CustomizeMenu.backPressed.testGetValue()?.size)
    }

    @Test
    fun `WHEN the user resets to default THEN record it`() {
        val store = createStore()
        assertNull(CustomizeMenu.resetToDefaultClicked.testGetValue())

        store.dispatch(ResetToDefaultConfirmed)

        assertEquals(1, CustomizeMenu.resetToDefaultClicked.testGetValue()?.size)
    }

    @Test
    fun `WHEN the user taps preview THEN record it`() {
        val store = createStore()
        assertNull(CustomizeMenu.previewClicked.testGetValue())

        store.dispatch(PreviewClicked)

        assertEquals(1, CustomizeMenu.previewClicked.testGetValue()?.size)
    }

    @Test
    fun `WHEN the user taps the button to edit the items of a section THEN record its name`() {
        val store = createStore()
        assertNull(CustomizeMenu.editItemsClicked.testGetValue())

        store.dispatch(EditGroupItemsClicked(TOOLS_GROUP_ID))

        val events = CustomizeMenu.editItemsClicked.testGetValue()
        assertNotNull(events)
        assertEquals(TOOLS_GROUP_ID, events.single().extra?.getValue("section"))
    }

    private fun createStore(initialState: CustomizableMenuState = CustomizableMenuState(emptyList())) =
        CustomizableMenuStore(initialState = initialState, middleware = listOf(CustomizeMenuTelemetryMiddleware()))

    private fun createStore(shown: List<String>, hidden: List<String> = emptyList()) =
        createStore(
            initialState =
                CustomizableMenuState(
                    menuGroups = groups(*(shown + hidden).toTypedArray()),
                    hiddenGroupIds = hidden.toSet(),
                )
        )

    private fun update(shown: List<String>, hidden: List<String> = emptyList()) =
        Update(
            items = groups(*(shown + hidden).toTypedArray()),
            mandatoryGroupIds = emptySet(),
            hiddenGroupIds = hidden.toSet(),
            isCustomized = false,
        )

    private fun structureChanged(shown: List<String>, movedGroupId: String, hidden: List<String> = emptyList()) =
        MenuStructureChanged(
            items = groups(*(shown + hidden).toTypedArray()),
            hiddenGroupIds = hidden.toSet(),
            movedGroupId = movedGroupId,
        )

    private fun groups(vararg ids: String) = ids.map { MenuItemsGroup.Row(id = it, items = emptyList()) }

    private fun assertSectionHiddenRecorded(section: String, position: Int) {
        val events = CustomizeMenu.sectionHidden.testGetValue()
        assertNotNull(events)

        val extras = events.single().extra
        assertEquals(section, extras?.getValue("section"))
        assertEquals(position.toString(), extras?.getValue("position"))
    }

    private fun assertSectionUnhiddenRecorded(section: String, position: Int) {
        val events = CustomizeMenu.sectionUnhidden.testGetValue()
        assertNotNull(events)

        val extras = events.single().extra
        assertEquals(section, extras?.getValue("section"))
        assertEquals(position.toString(), extras?.getValue("position"))
    }

    private fun assertSectionReorderedRecorded(section: String, position: Int, hidden: Boolean) {
        val events = CustomizeMenu.sectionReordered.testGetValue()
        assertNotNull(events)

        val extras = events.single().extra
        assertEquals(section, extras?.getValue("section"))
        assertEquals(position.toString(), extras?.getValue("position"))
        assertEquals(hidden.toString(), extras?.getValue("hidden"))
    }

    private fun assertShownRecorded(shownSections: String, hiddenSections: String) {
        val events = CustomizeMenu.shown.testGetValue()
        assertNotNull(events)

        val extras = events.single().extra
        assertEquals(shownSections, extras?.getValue("shown_sections"))
        assertEquals(hiddenSections, extras?.getValue("hidden_sections"))
    }
}
