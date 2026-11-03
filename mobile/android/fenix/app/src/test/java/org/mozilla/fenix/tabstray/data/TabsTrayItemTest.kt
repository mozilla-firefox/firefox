/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.data

import androidx.compose.ui.graphics.asImageBitmap
import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.mockk
import junit.framework.TestCase.assertEquals
import junit.framework.TestCase.assertFalse
import junit.framework.TestCase.assertTrue
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.createTab as createTabSessionState
import mozilla.components.compose.base.theme.layout.AcornWindowSize
import mozilla.components.concept.engine.mediasession.MediaSession
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class TabsTrayItemTest {
    @Test
    fun `WHEN the group is empty, THEN an empty list of thumbnails is returned`() {
        val group =
            TabsTrayItem.TabGroup(
                title = "Title",
                theme = TabGroupTheme.Yellow,
                tabs = mutableListOf(),
            )

        val thumbnails = group.thumbnails

        assertTrue(thumbnails.isEmpty())
    }

    @Test
    fun `WHEN the group has one tab item, THEN one thumbnail is returned`() {
        val group =
            TabsTrayItem.TabGroup(
                title = "Title",
                theme = TabGroupTheme.Yellow,
                tabs = mutableListOf(createTab(url = "www.mozilla.org")),
            )

        val thumbnails = group.thumbnails

        assertEquals(1, thumbnails.size)
    }

    @Test
    fun `WHEN the group has two tab items, THEN two thumbnails are returned`() {
        val group =
            TabsTrayItem.TabGroup(
                title = "Title",
                theme = TabGroupTheme.Yellow,
                tabs =
                    mutableListOf(
                        createTab(url = "www.mozilla.org"),
                        createTab(url = "www.wikipedia.org"),
                    ),
            )

        val thumbnails = group.thumbnails

        assertEquals(2, thumbnails.size)
    }

    @Test
    fun `WHEN the group has three tab items, THEN three thumbnails are returned`() {
        val group =
            TabsTrayItem.TabGroup(
                title = "Title",
                theme = TabGroupTheme.Yellow,
                tabs =
                    mutableListOf(
                        createTab(url = "www.mozilla.org"),
                        createTab(url = "www.wikipedia.org"),
                        createTab(url = "www.website.com"),
                    ),
            )

        val thumbnails = group.thumbnails

        assertEquals(3, thumbnails.size)
    }

    @Test
    fun `WHEN the group has four tab items, THEN four thumbnails are returned`() {
        val group =
            TabsTrayItem.TabGroup(
                title = "Title",
                theme = TabGroupTheme.Yellow,
                tabs =
                    mutableListOf(
                        createTab(url = "www.mozilla.org"),
                        createTab(url = "www.wikipedia.org"),
                        createTab(url = "www.website.com"),
                        createTab(url = "www.website.org"),
                    ),
            )

        val thumbnails = group.thumbnails

        assertEquals(4, thumbnails.size)
    }

    @Test
    fun `WHEN the group has 100 tab items, THEN four thumbnails are returned`() {
        val group =
            TabsTrayItem.TabGroup(
                title = "Title",
                theme = TabGroupTheme.Yellow,
                tabs = MutableList(100) { createTab(url = "www.mozilla.org") },
            )

        val thumbnails = group.thumbnails

        assertEquals(4, thumbnails.size)
    }

    @Test
    fun `WHEN multiple requests for thumbnails occur, THEN the list of thumbnails does not change`() {
        val group =
            TabsTrayItem.TabGroup(
                title = "Title",
                theme = TabGroupTheme.Yellow,
                tabs = MutableList(100) { createTab(url = "www.mozilla.org") },
            )

        val thumbnails = group.thumbnails
        val thumbnails2 = group.thumbnails

        assertEquals(thumbnails, thumbnails2)
    }

    @Test
    fun `WHEN a tab's thumbnail data is requested THEN it is a faithful conversion of the tab's data`() {
        val tab = createTab(url = "www.mozilla.org")
        val group =
            TabsTrayItem.TabGroup(
                title = "Title",
                theme = TabGroupTheme.Yellow,
                tabs = mutableListOf(tab),
            )

        val thumbnails = group.thumbnails
        val thumbnailImageData = thumbnails.first()

        assertEquals(tab.url, thumbnailImageData.tabUrl)
        assertEquals(tab.id, thumbnailImageData.tabId)
        assertEquals(tab.private, thumbnailImageData.isPrivate)
        assertEquals(tab.icon?.asImageBitmap(), thumbnailImageData.tabIcon)
    }

    @Test
    fun `GIVEN the window is small and the group has 4 tabs WHEN expanding the group THEN fully expand the group`() {
        val windowSize = AcornWindowSize.Small
        val tabs = MutableList(size = 4) { createTab(url = "") }
        val group = createTabGroup(tabs = tabs)

        assertTrue(group.shouldFullyExpandOnFirstOpen(windowSize = windowSize))
    }

    @Test
    fun `GIVEN the window is small and the group has 3 tabs WHEN expanding the group THEN partially expand the group`() {
        val windowSize = AcornWindowSize.Small
        val tabs = MutableList(size = 3) { createTab(url = "") }
        val group = createTabGroup(tabs = tabs)

        assertFalse(group.shouldFullyExpandOnFirstOpen(windowSize = windowSize))
    }

    @Test
    fun `GIVEN the window is medium and the group has 3 tabs WHEN expanding the group THEN partially expand the group`() {
        val windowSize = AcornWindowSize.Medium
        val tabs = MutableList(size = 3) { createTab(url = "") }
        val group = createTabGroup(tabs = tabs)

        assertFalse(group.shouldFullyExpandOnFirstOpen(windowSize = windowSize))
    }

    @Test
    fun `GIVEN the window is large and the group has 3 tabs WHEN expanding the group THEN partially expand the group`() {
        val windowSize = AcornWindowSize.Large
        val tabs = MutableList(size = 3) { createTab(url = "") }
        val group = createTabGroup(tabs = tabs)

        assertFalse(group.shouldFullyExpandOnFirstOpen(windowSize = windowSize))
    }

    @Test
    fun `GIVEN the window is medium and the group has 8 tabs WHEN expanding the group THEN fully expand the group`() {
        val windowSize = AcornWindowSize.Medium
        val tabs = MutableList(size = 8) { createTab(url = "") }
        val group = createTabGroup(tabs = tabs)

        assertTrue(group.shouldFullyExpandOnFirstOpen(windowSize = windowSize))
    }

    @Test
    fun `GIVEN the window is large and the group has 8 tabs WHEN expanding the group THEN fully expand the group`() {
        val windowSize = AcornWindowSize.Large
        val tabs = MutableList(size = 8) { createTab(url = "") }
        val group = createTabGroup(tabs = tabs)

        assertTrue(group.shouldFullyExpandOnFirstOpen(windowSize = windowSize))
    }

    @Test
    fun `GIVEN a group with at least one tab with active media WHEN checking if the group has active media THEN return true`() {
        val group =
            createTabGroup(
                tabs =
                    listOf(
                        createTab(url = "www.mozilla.org", isMediaActive = true),
                        createTab(url = "www.wikipedia.org", isMediaActive = false),
                    )
            )

        assertTrue(group.isMediaActive)
    }

    @Test
    fun `GIVEN a group with no tabs with active media WHEN checking if the group has active media THEN return false`() {
        val group =
            createTabGroup(
                tabs =
                    listOf(
                        createTab(url = "www.mozilla.org", isMediaActive = false),
                        createTab(url = "www.wikipedia.org", isMediaActive = false),
                    )
            )

        assertFalse(group.isMediaActive)
    }

    @Test
    fun `GIVEN a TabSessionState with media playing WHEN creating a Tab THEN isMediaActive is true`() {
        val mediaSessionState =
            MediaSessionState(
                controller = mockk(),
                playbackState = MediaSession.PlaybackState.PLAYING,
            )
        val tabSessionState =
            createTabSessionState(
                url = "https://www.mozilla.org",
                mediaSessionState = mediaSessionState,
            )

        val tab = TabsTrayItem.Tab(tabSessionState)

        assertTrue(tab.isMediaActive)
    }

    @Test
    fun `GIVEN a TabSessionState with media paused WHEN creating a Tab THEN isMediaActive is false`() {
        val mediaSessionState =
            MediaSessionState(
                controller = mockk(),
                playbackState = MediaSession.PlaybackState.PAUSED,
            )
        val tabSessionState =
            createTabSessionState(
                url = "https://www.mozilla.org",
                mediaSessionState = mediaSessionState,
            )

        val tab = TabsTrayItem.Tab(tabSessionState)

        assertFalse(tab.isMediaActive)
    }

    @Test
    fun `GIVEN a TabSessionState with no media WHEN creating a Tab THEN isMediaActive is false`() {
        val tabSessionState =
            createTabSessionState(
                url = "https://www.mozilla.org",
                mediaSessionState = null,
            )

        val tab = TabsTrayItem.Tab(tabSessionState)

        assertFalse(tab.isMediaActive)
    }

    @Test
    fun `GIVEN a TabSessionState being read out loud WHEN creating a Tab THEN isMediaActive is true`() {
        val tabSessionState =
            createTabSessionState(
                    url = "https://www.mozilla.org",
                    mediaSessionState = null,
                )
                .copy(listenToPageActive = true)

        val tab = TabsTrayItem.Tab(tabSessionState)

        assertTrue(tab.isMediaActive)
    }
}
