/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.bookmarks

import mozilla.appservices.places.BookmarkRoot
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class BookmarksReducerTest {
    @Test
    fun `WHEN bookmarks are loaded THEN isLoading state member reflects it has completed`() {
        val state = BookmarksState.default
        val items = List(0) {
            BookmarkItem.Folder("$it", "guid$it", position = it.toUInt())
        }
        val newFolder = BookmarkItem.Folder(
            guid = "guid",
            title = "Bookmarks",
            position = 1U,
        )
        assertTrue(state.isLoading)
        val result = bookmarksReducer(
            state,
            BookmarksLoaded(
                folder = newFolder,
                bookmarkItems = items,
            ),
        )
        val expected = state.copy(
            currentFolder = newFolder,
            bookmarkItems = items,
            isLoading = false,
        )
        assertEquals(expected, result)
    }

    @Test
    fun `WHEN view appeared action dispatches THEN no changes to state`() {
        val state = BookmarksState.default

        assertEquals(state, bookmarksReducer(state, ViewAppeared()))
    }

    @Test
    fun `GIVEN the SelectAll action is received, all bookmark items are selected`() {
        val bookmark1 = generateBookmark(1)
        val bookmark2 = generateBookmark(2)
        val bookmark3 = generateBookmark(3)
        val items = listOf(bookmark1, bookmark2, bookmark3)

        val result = bookmarksReducer(
            state = BookmarksState.default.copy(bookmarkItems = items),
            action = BookmarksListMenuAction.SelectAll,
        )

        assertEquals(result.bookmarkItems, result.selectedItems)
    }

    @Test
    fun `WHEN bookmarks are loaded THEN they are added to state with their parent folder data`() {
        val state = BookmarksState.default
        val items = List(5) {
            BookmarkItem.Folder("$it", "guid$it", position = it.toUInt())
        }
        val newFolder = BookmarkItem.Folder(
            guid = "guid",
            title = "Bookmarks",
            position = 1U,
        )

        val result = bookmarksReducer(
            state,
            BookmarksLoaded(
                folder = newFolder,
                bookmarkItems = items,
            ),
        )

        val expected = state.copy(
            currentFolder = newFolder,
            bookmarkItems = items,
            isLoading = false,
        )
        assertEquals(expected, result)
    }

    @Test
    fun `WHEN a user clicks add a folder THEN initialize add folder sub screen state`() {
        val state = BookmarksState.default.copy(
            currentFolder = BookmarkItem.Folder(
                guid = "guid",
                title = "mozilla",
                position = null,
            ),
        )

        val result = bookmarksReducer(state, AddFolderClicked).bookmarksAddFolderState
        val expected = BookmarksAddFolderState(
            parent = BookmarkItem.Folder(
                guid = "guid",
                title = "mozilla",
                position = null,
            ),
            folderBeingAddedTitle = "",
        )

        assertEquals(expected, result)
    }

    @Test
    fun `WHEN a user clicks on the search button THEN update the search state`() {
        val state = BookmarksState.default.copy(
            isSearching = false,
        )

        val result = bookmarksReducer(state, SearchClicked).isSearching

        assertTrue(result)
    }

    @Test
    fun `WHEN search ends THEN update the search state`() {
        val state = BookmarksState.default.copy(
            isSearching = true,
        )

        val result = bookmarksReducer(state, SearchDismissed).isSearching

        assertFalse(result)
    }

    @Test
    fun `GIVEN a bookmark is not selected WHEN long clicking a bookmark THEN it is added to selected items`() {
        val items = listOf(generateBookmark())
        val state = BookmarksState.default.copy(bookmarkItems = items)

        val result = bookmarksReducer(state, BookmarkLongClicked(items[0]))

        assertEquals(items[0], result.selectedItems[0])
    }

    @Test
    fun `GIVEN a bookmark is already selected WHEN long clicking a bookmark THEN it is added to selected items`() {
        val items = listOf(generateBookmark())
        val state = BookmarksState.default.copy(bookmarkItems = items, selectedItems = items)

        val result = bookmarksReducer(state, BookmarkLongClicked(items[0]))

        assertTrue(result.selectedItems.isEmpty())
    }

    @Test
    fun `GIVEN a folder is not selected WHEN long clicking a folder THEN it is added to selected items`() {
        val items = listOf(generateFolder())
        val state = BookmarksState.default.copy(bookmarkItems = items)

        val result = bookmarksReducer(state, FolderLongClicked(items[0]))

        assertEquals(items[0], result.selectedItems[0])
    }

    @Test
    fun `GIVEN a folder is already selected WHEN long clicking a folder THEN it is added to selected items`() {
        val items = listOf(generateFolder())
        val state = BookmarksState.default.copy(bookmarkItems = items, selectedItems = items)

        val result = bookmarksReducer(state, FolderLongClicked(items[0]))

        assertTrue(result.selectedItems.isEmpty())
    }

    @Test
    fun `GIVEN there are already selected items WHEN clicking an unselected bookmark THEN it is added to selected items`() {
        val bookmark1 = generateBookmark(1)
        val bookmark2 = generateBookmark(2)
        val items = listOf(bookmark1, bookmark2)
        val selectedItems = listOf(bookmark1)
        val state = BookmarksState.default.copy(bookmarkItems = items, selectedItems = selectedItems)

        val result = bookmarksReducer(state, BookmarkClicked(bookmark2))

        assertTrue(result.selectedItems.contains(bookmark2))
    }

    @Test
    fun `GIVEN a bookmark list WHEN the sort menu items are clicked THEN resort the bookmark list`() {
        val firefox = generateBookmark(1, title = "Firefox", dateAdded = 1, position = 5u)
        val mozilla = generateBookmark(2, title = "Mozilla", dateAdded = 3, position = 3u)
        val lockwise = generateFolder(3, title = "Lockwise", dateAdded = 2, position = 1u)
        val items = listOf(firefox, lockwise, mozilla)
        val state = BookmarksState.default.copy(bookmarkItems = items)

        val positional = bookmarksReducer(state, BookmarksListMenuAction.SortMenu.CustomSortClicked)
        assertEquals(listOf(lockwise, mozilla, firefox), positional.bookmarkItems)

        val zToA = bookmarksReducer(state, BookmarksListMenuAction.SortMenu.ZtoAClicked)
        assertEquals(listOf(mozilla, lockwise, firefox), zToA.bookmarkItems)

        val aToZ = bookmarksReducer(zToA, BookmarksListMenuAction.SortMenu.AtoZClicked)
        assertEquals(listOf(firefox, lockwise, mozilla), aToZ.bookmarkItems)

        val newest = bookmarksReducer(aToZ, BookmarksListMenuAction.SortMenu.NewestClicked)
        assertEquals(listOf(mozilla, lockwise, firefox), newest.bookmarkItems)

        val oldest = bookmarksReducer(newest, BookmarksListMenuAction.SortMenu.OldestClicked)
        assertEquals(listOf(firefox, lockwise, mozilla), oldest.bookmarkItems)
    }

    @Test
    fun `GIVEN the select folder screen WHEN the sort menu items are clicked THEN resort the bookmark list`() {
        val b1 = generateFolder(1, title = "z", dateAdded = 1, position = 0u)
        val b2 = generateFolder(2, title = "a", dateAdded = 2, position = 1u)
        val b3 = generateFolder(3, title = "z", dateAdded = 3, position = 1u)
        val b4 = generateFolder(4, title = "b", dateAdded = 4, position = 1u)
        val b5 = generateFolder(5, title = "d", dateAdded = 5, position = 2u)
        val b6 = generateFolder(6, title = "j", dateAdded = 6, position = 3u)

        val items = listOf(b1, b2, b3, b4, b5, b6)
        val state = BookmarksState.default.copy(bookmarkItems = items)

        val positional = bookmarksReducer(state, SelectFolderAction.SortMenu.CustomSortClicked)
        assertEquals(listOf(b1, b2, b3, b4, b5, b6), positional.bookmarkItems)

        val zToA = bookmarksReducer(state, BookmarksListMenuAction.SortMenu.ZtoAClicked)
        assertEquals(listOf(b1, b3, b6, b5, b4, b2), zToA.bookmarkItems)

        val aToZ = bookmarksReducer(zToA, BookmarksListMenuAction.SortMenu.AtoZClicked)
        assertEquals(listOf(b2, b4, b5, b6, b1, b3), aToZ.bookmarkItems)

        val newest = bookmarksReducer(aToZ, BookmarksListMenuAction.SortMenu.NewestClicked)
        assertEquals(listOf(b6, b5, b4, b3, b2, b1), newest.bookmarkItems)

        val oldest = bookmarksReducer(newest, BookmarksListMenuAction.SortMenu.OldestClicked)
        assertEquals(listOf(b1, b2, b3, b4, b5, b6), oldest.bookmarkItems)
    }

    @Test
    fun `GIVEN there are already selected items WHEN clicking an unselected folder THEN it is added to selected items`() {
        val folder1 = generateFolder(1)
        val folder2 = generateFolder(2)
        val items = listOf(folder1, folder2)
        val selectedItems = listOf(folder1)
        val state = BookmarksState.default.copy(bookmarkItems = items, selectedItems = selectedItems)

        val result = bookmarksReducer(state, FolderClicked(folder2))

        assertTrue(result.selectedItems.contains(folder2))
    }

    @Test
    fun `GIVEN there are already selected items WHEN clicking a selected bookmark THEN it is removed from selected items`() {
        val bookmark1 = generateBookmark(1)
        val bookmark2 = generateBookmark(2)
        val items = listOf(bookmark1, bookmark2)
        val selectedItems = listOf(bookmark1, bookmark2)
        val state = BookmarksState.default.copy(bookmarkItems = items, selectedItems = selectedItems)

        val result = bookmarksReducer(state, BookmarkClicked(bookmark2))

        assertFalse(result.selectedItems.contains(bookmark2))
    }

    @Test
    fun `GIVEN there are already selected items WHEN clicking a selected folder THEN it is removed from selected items`() {
        val folder1 = generateFolder(1)
        val folder2 = generateFolder(2)
        val items = listOf(folder1, folder2)
        val selectedItems = listOf(folder1, folder2)
        val state = BookmarksState.default.copy(bookmarkItems = items, selectedItems = selectedItems)

        val result = bookmarksReducer(state, FolderClicked(folder2))

        assertFalse(result.selectedItems.contains(folder2))
    }

    @Test
    fun `WHEN the title of a folder is changed on the add folder screen THEN that is reflected in state`() {
        val state = BookmarksState.default.copy(
            bookmarksAddFolderState = BookmarksAddFolderState(
                parent = BookmarkItem.Folder(
                    guid = BookmarkRoot.Mobile.id,
                    title = "Bookmarks",
                    position = 1U,
                ),
                folderBeingAddedTitle = "",
            ),
        )

        val titleChange = "test"

        val result = bookmarksReducer(state, AddFolderAction.TitleChanged(titleChange))

        assertEquals(titleChange, result.bookmarksAddFolderState?.folderBeingAddedTitle)
    }

    @Test
    fun `WHEN a folder is created THEN remove the select folder state and update the edit bookmark state`() {
        val state = BookmarksState.default.copy(
            bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "guid"),
            bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                bookmark = BookmarkItem.Bookmark("url", "title", "url", "guid", position = null),
                folder = BookmarkItem.Folder("parentTitle", "parentGuid", position = 1U),
            ),
        )

        val folder = BookmarkItem.Folder("New Bookmark", "guid", position = null)
        val result = bookmarksReducer(state, AddFolderAction.FolderCreated(folder))

        assertEquals(folder, result.bookmarksEditBookmarkState?.folder)
        assertNull(result.bookmarksSelectFolderState)
    }

    @Test
    fun `GIVEN we are on the add folder screen WHEN back is clicked THEN add folder state is removed`() {
        val state = BookmarksState.default.copy(
            bookmarksAddFolderState = BookmarksAddFolderState(
                parent = BookmarkItem.Folder(
                    guid = BookmarkRoot.Mobile.id,
                    title = "Bookmarks",
                    position = null,
                ),
                folderBeingAddedTitle = "",
            ),
        )

        val result = bookmarksReducer(state, BackClicked)

        assertEquals(BookmarksState.default, result)
    }

    @Test
    fun `GIVEN we are on the add folder screen WHEN parent folder is clicked THEN initialize the select folder state`() {
        val state = BookmarksState.default.copy(
            bookmarksAddFolderState = BookmarksAddFolderState(
                parent = BookmarkItem.Folder(
                    guid = BookmarkRoot.Mobile.id,
                    title = "Bookmarks",
                    position = null,
                ),
                folderBeingAddedTitle = "",
            ),
        )

        val result = bookmarksReducer(state, AddFolderAction.ParentFolderClicked)

        assertEquals(BookmarkRoot.Mobile.id, result.bookmarksSelectFolderState?.outerSelectionGuid)
    }

    @Test
    fun `GIVEN the add folder screen has been reached from the select folder screen WHEN back clicked from selecting a parent for the new folder THEN only inner selection is removed`() {
        val state = BookmarksState.default.copy(
            bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                bookmark = BookmarkItem.Bookmark("url", "title", "url", "guid", position = null),
                folder = BookmarkItem.Folder("parentTitle", "parentGuid", position = 1U),
            ),
            bookmarksAddFolderState = BookmarksAddFolderState(
                parent = BookmarkItem.Folder(
                    guid = BookmarkRoot.Mobile.id,
                    title = "Bookmarks",
                    position = null,
                ),
                folderBeingAddedTitle = "",
            ),
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "outerGuid",
                innerSelectionGuid = "innerGuid",
            ),
        )

        val result = bookmarksReducer(state, BackClicked)

        assertNull(result.bookmarksSelectFolderState?.innerSelectionGuid)
        assertEquals("outerGuid", result.bookmarksSelectFolderState?.outerSelectionGuid)
    }

    @Test
    fun `GIVEN the add folder screen has been reached from the select folder screen while moving bookmarks WHEN back clicked from add folder screen THEN exit fully out of the selection state`() {
        val state = BookmarksState.default.copy(
            bookmarksAddFolderState = BookmarksAddFolderState(
                parent = BookmarkItem.Folder(
                    guid = BookmarkRoot.Mobile.id,
                    title = "Bookmarks",
                    position = null,
                ),
                folderBeingAddedTitle = "test",
            ),
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "outerGuid",
            ),
            bookmarksMultiselectMoveState = MultiselectMoveState(
                listOf("guid1", "guid2"),
                "guid to move into",
            ),
        )

        val result = bookmarksReducer(state, BackClicked)

        assertNull(result.bookmarksSelectFolderState)
        assertNull(result.bookmarksMultiselectMoveState)
    }

    @Test
    fun `WHEN the title of a folder is changed on the edit folder screen THEN that is reflected in state`() {
        val state = BookmarksState.default.copy(
            bookmarksEditFolderState = BookmarksEditFolderState(
                parent = BookmarkItem.Folder(
                    guid = BookmarkRoot.Mobile.id,
                    title = "Bookmarks",
                    position = 2U,
                ),
                folder = BookmarkItem.Folder(
                    guid = "guid1",
                    title = "New Bookmarks",
                    position = 1U,
                ),
            ),
        )

        val titleChange = "New Bookmarks 2"

        val result = bookmarksReducer(state, EditFolderAction.TitleChanged(titleChange))

        assertEquals(titleChange, result.bookmarksEditFolderState?.folder?.title)
    }

    @Test
    fun `GIVEN we are on the edit folder screen WHEN back is clicked THEN add folder state is removed`() {
        val state = BookmarksState.default.copy(
            bookmarksEditFolderState = BookmarksEditFolderState(
                parent = BookmarkItem.Folder(
                    guid = BookmarkRoot.Mobile.id,
                    title = "Bookmarks",
                    position = null,
                ),
                folder = BookmarkItem.Folder(
                    guid = "guid1",
                    title = "New Bookmarks",
                    position = null,
                ),
            ),
        )

        val result = bookmarksReducer(state, BackClicked)

        assertEquals(BookmarksState.default, result)
    }

    @Test
    fun `GIVEN we are on the edit folder screen WHEN parent folder is clicked THEN initialize the select folder state`() {
        val state = BookmarksState.default.copy(
            bookmarksEditFolderState = BookmarksEditFolderState(
                parent = BookmarkItem.Folder(
                    guid = BookmarkRoot.Mobile.id,
                    title = "Bookmarks",
                    position = null,
                ),
                folder = BookmarkItem.Folder(
                    guid = "guid1",
                    title = "New Bookmarks",
                    position = null,
                ),
            ),
        )

        val result = bookmarksReducer(state, EditFolderAction.ParentFolderClicked)

        assertEquals(BookmarkRoot.Mobile.id, result.bookmarksSelectFolderState?.outerSelectionGuid)
    }

    @Test
    fun `GIVEN we are on the edit bookmark screen WHEN folder is clicked THEN initialize the select folder state`() {
        val state = BookmarksState.default.copy(
            bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                bookmark = BookmarkItem.Bookmark("", "", "", "", null),
                folder = BookmarkItem.Folder(
                    guid = "1",
                    title = "Bookmarks",
                    position = null,
                ),
            ),
        )

        val result = bookmarksReducer(state, EditBookmarkAction.FolderClicked)

        assertEquals("1", result.bookmarksSelectFolderState?.outerSelectionGuid)
    }

    @Test
    fun `GIVEN there is no substate screen present WHEN back is clicked THEN state is unchanged`() {
        val state = BookmarksState.default

        val result = bookmarksReducer(state, BackClicked)

        assertEquals(BookmarksState.default, result)
    }

    @Test
    fun `GIVEN we are on the select folder screen WHEN back is clicked THEN the select folder state is removed`() {
        val state = BookmarksState.default.copy(
            bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "guid"),
        )

        val result = bookmarksReducer(state, BackClicked)

        val expected = state.copy(
            bookmarksSelectFolderState = null,
        )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN we are on the select folder screen with multi-select state WHEN back is clicked THEN the select folder and multi-select state is removed`() {
        val state = BookmarksState.default.copy(
            bookmarksMultiselectMoveState = MultiselectMoveState(
                guidsToMove = listOf(),
                destination = "",
            ),
            bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = ""),
        )

        val result = bookmarksReducer(state, BackClicked)

        val expected = state.copy(
            bookmarksMultiselectMoveState = null,
            bookmarksSelectFolderState = null,
        )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN we are on the select folder screen while editing a bookmark and creating a folder WHEN back is clicked THEN just the select folder guid is removed`() {
        val state = BookmarksState.default.copy(
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "abc",
                innerSelectionGuid = "123",
            ),
        )

        val result = bookmarksReducer(state, BackClicked)

        val expected = state.copy(
            bookmarksSelectFolderState = state.bookmarksSelectFolderState?.copy(
                innerSelectionGuid = null,
            ),
        )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN we are on the select folder screen WHEN search is clicked THEN select folder enters search mode`() {
        val state = BookmarksState.default.copy(
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "selection guid",
                isSearching = false,
            ),
        )

        val result = bookmarksReducer(state, SelectFolderAction.SearchClicked)

        assertTrue(result.bookmarksSelectFolderState!!.isSearching)
    }

    @Test
    fun `GIVEN select folder search is active WHEN dismissed THEN select folder exits search mode`() {
        val state = BookmarksState.default.copy(
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "selection guid",
                isSearching = true,
            ),
        )

        val result = bookmarksReducer(state, SelectFolderAction.SearchDismissed)

        assertFalse(result.bookmarksSelectFolderState!!.isSearching)
    }

    @Test
    fun `GIVEN we are on the select folder screen WHEN the search query changes THEN persist the new query`() {
        val folder1 =
            SelectFolderItem(
                0,
                BookmarkItem
                .Folder(
                    title = "one folder",
                    guid = "",
                    position = null,
                ),
                SelectFolderExpansionState.None,
            )
        val folder2 =
            SelectFolderItem(
                0,
                BookmarkItem
                .Folder(
                    title = "other",
                    guid = "",
                    position = null,
                ),
                SelectFolderExpansionState.None,
            )
        val state = BookmarksState.default.copy(
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                folders = listOf(folder1, folder2),
                outerSelectionGuid = "selection guid",
                searchQuery = "one folder",
            ),
        )

        val newQuery = "other"
        val result = bookmarksReducer(state, SelectFolderAction.SearchQueryUpdated(newQuery))

        assertEquals(newQuery, result.bookmarksSelectFolderState!!.searchQuery)
    }

    @Test
    fun `GIVEN we are on the select folder screen WHEN folders are loaded THEN attach loaded folders on the select screen state`() {
        val state = BookmarksState.default.copy(
            bookmarksSelectFolderState = BookmarksSelectFolderState(outerSelectionGuid = "selection guid"),
        )

        val folders = listOf(
            SelectFolderItem(
                0,
                BookmarkItem.Folder("Bookmarks", "guid0", null),
                SelectFolderExpansionState.Open(
                    listOf(
                        SelectFolderItem(
                            1,
                            BookmarkItem.Folder("Nested One", "guid0", null),
                            SelectFolderExpansionState.Open(
                                listOf(
                                SelectFolderItem(
                                    2,
                                    BookmarkItem.Folder("Nested Two", "guid0", null),
                                    SelectFolderExpansionState.None,
                                ),
                                SelectFolderItem(
                                    2,
                                    BookmarkItem.Folder("Nested Two", "guid0", null),
                                    SelectFolderExpansionState.None,
                                ),
                            ),
                        ),
                    ),
                        SelectFolderItem(
                            1,
                            BookmarkItem.Folder("Nested One", "guid0", null),
                            SelectFolderExpansionState.Open(
                                listOf(
                                    SelectFolderItem(
                                        2,
                                        BookmarkItem.Folder("Nested Two", "guid1", null),
                                        SelectFolderExpansionState.Open(
                                            listOf(
                                                SelectFolderItem(
                                                    3,
                                                    BookmarkItem.Folder("Nested Three", "guid0", null),
                                                    SelectFolderExpansionState.None,
                                                ),
                                            ),
                                        ),
                                    ),
                                ),
                            ),
                        ),
                    ),
                ),
            ),
            SelectFolderItem(0, BookmarkItem.Folder("Nested 0", "guid0", null), SelectFolderExpansionState.None),
        )

        val result = bookmarksReducer(state, SelectFolderAction.FoldersLoaded(folders))

        val expected = state.copy(
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "selection guid",
                folders = folders,
                filteredFolders = folders,
                isLoading = false,
                isSearching = false,
            ),
        )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN we are on the select folder screen and search is active WHEN filtered folders are loaded THEN filtered folders gets updated`() {
        val folders = listOf(
            SelectFolderItem(
                0,
                BookmarkItem.Folder("Bookmarks", "guid0", null),
                SelectFolderExpansionState.Open(
                    listOf(
                        SelectFolderItem(
                            1,
                            BookmarkItem.Folder("Nested One", "guid0", null),
                            SelectFolderExpansionState.Open(
                                listOf(
                                    SelectFolderItem(
                                        2,
                                        BookmarkItem.Folder("Nested Two", "guid0", null),
                                        SelectFolderExpansionState.None,
                                    ),
                                    SelectFolderItem(
                                        2,
                                        BookmarkItem.Folder("Nested Two", "guid0", null),
                                        SelectFolderExpansionState.None,
                                    ),
                                ),
                            ),
                        ),
                        SelectFolderItem(
                            1,
                            BookmarkItem.Folder("Nested One", "guid0", null),
                            SelectFolderExpansionState.Open(
                                listOf(
                                    SelectFolderItem(
                                        2,
                                        BookmarkItem.Folder("Nested Two", "guid1", null),
                                        SelectFolderExpansionState.Open(
                                            listOf(
                                                SelectFolderItem(
                                                    3,
                                                    BookmarkItem.Folder("Nested Three", "guid0", null),
                                                    SelectFolderExpansionState.None,
                                                ),
                                            ),
                                        ),
                                    ),
                                ),
                            ),
                        ),
                    ),
                ),
            ),
            SelectFolderItem(0, BookmarkItem.Folder("Nested 0", "guid0", null), SelectFolderExpansionState.None),
        )

        val bookmarksFolder = folders.first()

        val state = BookmarksState.default.copy(
            bookmarksSelectFolderState =
                BookmarksSelectFolderState(
                    outerSelectionGuid = "selection guid",
                    folders = folders,
                    isSearching = true,
                ),
        )

        val filteredFolders = folders.filter { it.title.startsWith("bookmarks", ignoreCase = true) }

        val result = bookmarksReducer(state, SelectFolderAction.FilteredFoldersLoaded(filteredFolders))

        val expected = state.copy(
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "selection guid",
                folders = folders,
                filteredFolders = listOf(bookmarksFolder),
                isLoading = false,
                isSearching = true,
            ),
        )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN we are on the select folder screen with a selection guid WHEN a folder is clicked THEN update the selection`() {
        val state = BookmarksState.default.copy(
            bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                bookmark = generateBookmark(),
                folder = BookmarkItem.Folder("Bookmarks", "guid0", null),
            ),
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "guid0",
                folders = listOf(
                    SelectFolderItem(
                        0,
                        BookmarkItem.Folder("Bookmarks", "guid0", null),
                        SelectFolderExpansionState.None,
                    ),
                    SelectFolderItem(0, BookmarkItem.Folder("Nested 0", "guid0", null), SelectFolderExpansionState.None),
                ),
            ),
        )

        val result = bookmarksReducer(
            state = state,
            action = SelectFolderAction.ItemClicked(
                folder = SelectFolderItem(0, BookmarkItem.Folder("Nested 0", "guid0", null), SelectFolderExpansionState.None),
            ),
        )

        val expected = state.copy(
            bookmarksEditBookmarkState = state.bookmarksEditBookmarkState?.copy(
                folder = BookmarkItem.Folder("Nested 0", "guid0", null),
                edited = true,
            ),
            bookmarksSelectFolderState = state.bookmarksSelectFolderState?.copy(
                outerSelectionGuid = "guid0",
            ),
            bookmarksSnackbarState = BookmarksSnackbarState.BookmarkMoved("title", "Nested 0"),
        )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN we are on the select folder screen and the bookmark has a url longer than 25 characters WHEN a folder is clicked THEN update the truncate the title and display a snackbar with the truncated title`() {
        val longTitle = "https://www.firefox.com/en-US/?utm_source=www.mozilla.org&utm_medium=referral&utm_campaign=nav&utm_content=firefox"
        val state = BookmarksState.default.copy(
            bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                bookmark = generateBookmark().copy(title = longTitle),
                folder = BookmarkItem.Folder("Bookmarks", "guid0", null),
            ),
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "guid0",
                folders = listOf(
                    SelectFolderItem(0, BookmarkItem.Folder("Bookmarks", "guid0", null), expansionState = SelectFolderExpansionState.Closed),
                    SelectFolderItem(0, BookmarkItem.Folder("Nested 0", "guid0", null), expansionState = SelectFolderExpansionState.Closed),
                ),
            ),
        )

        val result = bookmarksReducer(
            state = state,
            action = SelectFolderAction.ItemClicked(
                folder = SelectFolderItem(0, BookmarkItem.Folder("Nested 0", "guid0", null), expansionState = SelectFolderExpansionState.Closed),
            ),
        )

        val expected = state.copy(
            bookmarksEditBookmarkState = state.bookmarksEditBookmarkState?.copy(
                folder = BookmarkItem.Folder("Nested 0", "guid0", null),
                edited = true,
            ),
            bookmarksSelectFolderState = state.bookmarksSelectFolderState?.copy(
                outerSelectionGuid = "guid0",
            ),
            bookmarksSnackbarState = BookmarksSnackbarState.BookmarkMoved(formatBookmarkTitle(longTitle), "Nested 0"),
        )

        assertEquals(expected, result)

        val finalResult = bookmarksReducer(result, BackClicked)

        assertEquals(BookmarksSnackbarState.BookmarkMoved(formatBookmarkTitle(longTitle), "Nested 0"), finalResult.bookmarksSnackbarState)
    }

    @Test
    fun `WHEN the title of a bookmark is changed on the edit bookmark screen THEN newlines are replaced`() {
        val state = BookmarksState.default.copy(
            bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                bookmark = generateBookmark(title = "old title"),
                folder = generateFolder(title = "parent"),
            ),
        )

        val titleChange = "  New\nTitle  \n"

        val result = bookmarksReducer(state, EditBookmarkAction.TitleChanged(titleChange))

        assertEquals("  New Title   ", result.bookmarksEditBookmarkState?.bookmark?.title)
        assertEquals(true, result.bookmarksEditBookmarkState?.edited)
    }

    @Test
    fun `GIVEN a bookmark WHEN a edit is made THEN the edited state is persisted`() {
        val bookmarkItem = generateBookmark()
        val folderItem = SelectFolderItem(0, BookmarkItem.Folder("Bookmarks", "guid0", null), SelectFolderExpansionState.None)
        var state = BookmarksState.default.copy(
            bookmarkItems = listOf(bookmarkItem),
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "guid0",
                folders = listOf(
                    folderItem,
                    SelectFolderItem(0, BookmarkItem.Folder("Nested 0", "guid0", null), SelectFolderExpansionState.None),
                ),
            ),
        )

        state = bookmarksReducer(state = state, action = EditBookmarkClicked(bookmarkItem))
        state = bookmarksReducer(state = state, action = EditBookmarkAction.URLChanged("1234"))

        assertEquals(true, state.bookmarksEditBookmarkState?.edited)

        state = bookmarksReducer(state = state, action = EditBookmarkClicked(bookmarkItem))
        assertEquals(false, state.bookmarksEditBookmarkState?.edited)

        state = bookmarksReducer(state = state, action = EditBookmarkAction.TitleChanged("1234"))
        assertEquals(true, state.bookmarksEditBookmarkState?.edited)

        state = bookmarksReducer(state = state, action = EditBookmarkClicked(bookmarkItem))
        assertEquals(false, state.bookmarksEditBookmarkState?.edited)

        state = bookmarksReducer(state = state, action = SelectFolderAction.ItemClicked(folderItem))
        assertEquals(true, state.bookmarksEditBookmarkState?.edited)
    }

    @Test
    fun `GIVEN we are on the select folder screen with a selection guid and multiselect state WHEN a folder is clicked THEN update the selection`() {
        val state = BookmarksState.default.copy(
            bookmarksMultiselectMoveState = MultiselectMoveState(
                guidsToMove = listOf("guid0", "guid1"),
                destination = "folder 1",
            ),
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "folder 1",
                folders = listOf(
                    SelectFolderItem(0, BookmarkItem.Folder("Bookmarks", "guid 0", null), SelectFolderExpansionState.None),
                    SelectFolderItem(0, BookmarkItem.Folder("Nested 0", "guid 1", null), SelectFolderExpansionState.None),
                    SelectFolderItem(0, BookmarkItem.Folder("Nested 1", "folder 1", null), SelectFolderExpansionState.None),
                    SelectFolderItem(0, BookmarkItem.Folder("Nested 2", "folder 2", null), SelectFolderExpansionState.None),
                ),
            ),
        )

        val result = bookmarksReducer(
            state = state,
            action = SelectFolderAction.ItemClicked(
                folder = SelectFolderItem(0, BookmarkItem.Folder("Nested 2", "folder 2", null), SelectFolderExpansionState.None),
            ),
        )

        val expected = state.copy(
            bookmarksMultiselectMoveState = state.bookmarksMultiselectMoveState?.copy(
                destination = "folder 2",
            ),
            bookmarksSelectFolderState = state.bookmarksSelectFolderState?.copy(
                outerSelectionGuid = "folder 2",
            ),
        )

        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN we are on the select folder screen with a selection and an add folder guid WHEN a folder is clicked THEN update the add folder selection`() {
        val state = BookmarksState.default.copy(
            bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                bookmark = generateBookmark(),
                folder = BookmarkItem.Folder("Bookmarks", "0", null),
            ),
            bookmarksAddFolderState = BookmarksAddFolderState(
                parent = BookmarkItem.Folder("Bookmarks", "0", null),
                folderBeingAddedTitle = "",
            ),
            bookmarksSelectFolderState = BookmarksSelectFolderState(
                outerSelectionGuid = "0",
                innerSelectionGuid = "0",
                folders = listOf(
                    SelectFolderItem(0, BookmarkItem.Folder("Bookmarks", "0", null), SelectFolderExpansionState.None),
                    SelectFolderItem(0, BookmarkItem.Folder("Nested 0", "1", null), SelectFolderExpansionState.None),
                ),
            ),
        )

        val result = bookmarksReducer(
            state = state,
            action = SelectFolderAction.ItemClicked(
                folder = SelectFolderItem(0, BookmarkItem.Folder("Nested 0", "1", null), SelectFolderExpansionState.None),
            ),
        )

        val expected = state.copy(
            bookmarksAddFolderState = state.bookmarksAddFolderState?.copy(
                parent = BookmarkItem.Folder("Nested 0", "1", null),
            ),
            bookmarksSelectFolderState = state.bookmarksSelectFolderState?.copy(
                innerSelectionGuid = "1",
            ),
        )

        assertEquals(expected, result)
    }

    @Test
    fun `WHEN the edit button is clicked in a bookmark menu THEN bookmark edit state is created`() {
        val item = BookmarkItem.Bookmark("ur", "title", "url", "guid", null)
        val parent = BookmarkItem.Folder("title", "guid", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(item),
            currentFolder = parent,
        )

        val result = bookmarksReducer(state, BookmarksListMenuAction.Bookmark.EditClicked(item))
        assertEquals(BookmarksEditBookmarkState(bookmark = item, folder = parent), result.bookmarksEditBookmarkState)
    }

    @Test
    fun `GIVEN one selected item WHEN clicking on a desktop folder THEN display a snackbar explaining that you can't do that`() {
        val item = BookmarkItem.Bookmark("ur", "title", "url", "guid", null)
        val parent = BookmarkItem.Folder("title", "guid", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(item),
            selectedItems = listOf(item),
            currentFolder = parent,
        )

        val folder = BookmarkItem.Folder("Desktop Bookmarks", BookmarkRoot.Root.id, null)
        val result = bookmarksReducer(state, FolderClicked(folder))
        assertEquals(BookmarksSnackbarState.CantEditDesktopFolders, result.bookmarksSnackbarState)
    }

    @Test
    fun `GIVEN one selected item WHEN back is clicked THEN clear out selected items`() {
        val item = BookmarkItem.Bookmark("ur", "title", "url", "guid", null)
        val parent = BookmarkItem.Folder("title", "guid", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(item),
            selectedItems = listOf(item),
            currentFolder = parent,
        )

        val result = bookmarksReducer(state, BackClicked)
        assertTrue(result.selectedItems.isEmpty())
    }

    @Test
    fun `GIVEN a snackbar is displayed WHEN the snackbar is dismissed THEN update the state`() {
        val state = BookmarksState.default.copy(
            bookmarksSnackbarState = BookmarksSnackbarState.CantEditDesktopFolders,
        )

        val result = bookmarksReducer(state, SnackbarAction.Dismissed)
        assertEquals(BookmarksSnackbarState.None, result.bookmarksSnackbarState)
    }

    @Test
    fun `GIVEN a list of a bookmarks WHEN a folder is deleted THEN load the number of nested bookmarks`() {
        val folder = BookmarkItem.Folder("Bookmark Folder", "guid0", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(folder),
        )

        val result = bookmarksReducer(state, BookmarksListMenuAction.Folder.DeleteClicked(folder))
        assertEquals(DeletionDialogState.LoadingCount(listOf("guid0")), result.bookmarksDeletionDialogState)
    }

    @Test
    fun `WHEN folder is deleted from folder edit screen THEN load the number of nested bookmarks`() {
        val folder = BookmarkItem.Folder("Bookmark Folder", "guid0", null)
        val state = BookmarksState.default.copy(
            bookmarksEditFolderState = BookmarksEditFolderState(folder, folder),
        )

        val result = bookmarksReducer(state, EditFolderAction.DeleteClicked)
        assertEquals(DeletionDialogState.LoadingCount(listOf("guid0")), result.bookmarksDeletionDialogState)
    }

    @Test
    fun `GIVEN a deletion dialog that is loading a count WHEN we receive the count THEN present the dialog`() {
        val folder = BookmarkItem.Folder("Bookmark Folder", "guid0", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(folder),
            bookmarksDeletionDialogState = DeletionDialogState.LoadingCount(listOf("guid0")),
        )

        val result = bookmarksReducer(state, DeletionDialogAction.CountLoaded(19))
        assertEquals(DeletionDialogState.Presenting(listOf("guid0"), 19), result.bookmarksDeletionDialogState)
    }

    @Test
    fun `GIVEN a deletion dialog WHEN delete is tapped THEN dismiss the dialog and remove the deleted items`() {
        val folder = BookmarkItem.Folder("Bookmark Folder", "guid0", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(folder),
            bookmarksDeletionDialogState = DeletionDialogState.Presenting(listOf("guid0"), 1),
        )

        val result = bookmarksReducer(state, DeletionDialogAction.DeleteTapped)
        assertEquals(DeletionDialogState.None, result.bookmarksDeletionDialogState)
        assertEquals(listOf<BookmarkItem>(), result.bookmarkItems)
    }

    @Test
    fun `GIVEN a deletion dialog WHEN cancel is tapped THEN dismiss the dialog`() {
        val folder = BookmarkItem.Folder("Bookmark Folder", "guid0", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(folder),
            bookmarksDeletionDialogState = DeletionDialogState.LoadingCount(listOf("guid0")),
        )

        val result = bookmarksReducer(state, DeletionDialogAction.CancelTapped)
        assertEquals(DeletionDialogState.None, result.bookmarksDeletionDialogState)
        assertEquals(state.bookmarkItems, result.bookmarkItems)
    }

    @Test
    fun `GIVEN one selected item WHEN the edit button is clicked in a bookmark menu THEN bookmark edit state is created`() {
        val item = BookmarkItem.Bookmark("ur", "title", "url", "guid", null)
        val parent = BookmarkItem.Folder("title", "guid", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(item),
            selectedItems = listOf(item),
            currentFolder = parent,
        )

        val result = bookmarksReducer(state, BookmarksListMenuAction.MultiSelect.EditClicked)
        assertEquals(BookmarksEditBookmarkState(bookmark = item, folder = parent), result.bookmarksEditBookmarkState)
    }

    @Test
    fun `GIVEN a single folder selected WHEN tapping delete in the multi select menu THEN present the deletion dialog`() {
        val item = BookmarkItem.Folder("title", "guid", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(item),
            selectedItems = listOf(item),
        )

        val result = bookmarksReducer(state, BookmarksListMenuAction.MultiSelect.DeleteClicked)
        assertEquals(DeletionDialogState.LoadingCount(listOf("guid")), result.bookmarksDeletionDialogState)
    }

    @Test
    fun `GIVEN a multiple bookmarks selected WHEN tapping delete in the multi select menu THEN present the deletion dialog`() {
        val item = BookmarkItem.Bookmark("ur", "title", "url", "guid", null)
        val item2 = item.copy(guid = "guid2")
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(item, item2),
            selectedItems = listOf(item, item2),
        )

        val result = bookmarksReducer(state, BookmarksListMenuAction.MultiSelect.DeleteClicked)
        assertEquals(DeletionDialogState.LoadingCount(listOf("guid", "guid2")), result.bookmarksDeletionDialogState)
    }

    @Test
    fun `GIVEN a multiple bookmarks selected WHEN receiving a recursive count update THEN update the state`() {
        val item = BookmarkItem.Bookmark("ur", "title", "url", "guid", null)
        val item2 = item.copy(guid = "guid2")
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(item, item2),
            selectedItems = listOf(item, item2),
        )

        val result = bookmarksReducer(state, RecursiveSelectionCountLoaded(19))
        assertEquals(state.copy(recursiveSelectedCount = 19), result)
    }

    @Test
    fun `GIVEN a user tries to open a folder with more than 15 items WHEN we receive a Present action THEN we show the dialog`() {
        val state = BookmarksState.default
        val action = OpenTabsConfirmationDialogAction.Present(
            guid = "guid0",
            count = 19,
            isPrivate = false,
        )

        val result = bookmarksReducer(state, action)
        val expected = state.copy(
            openTabsConfirmationDialog = OpenTabsConfirmationDialog.Presenting(
                guidToOpen = "guid0",
                numberOfTabs = 19,
                isPrivate = false,
            ),
        )
        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN a user is presented with a open tab confirmation dialog WHEN the user taps cancel or open THEN we close the dialog`() {
        val state = BookmarksState.default.copy(
            openTabsConfirmationDialog = OpenTabsConfirmationDialog.Presenting(
                guidToOpen = "guid0",
                numberOfTabs = 19,
                isPrivate = false,
            ),
        )

        listOf(
            OpenTabsConfirmationDialogAction.CancelTapped,
            OpenTabsConfirmationDialogAction.ConfirmTapped,
        ).forEach {
            assertEquals(BookmarksState.default, bookmarksReducer(state, it))
        }
    }

    @Test
    fun `GIVEN a user is initializing on the edit screen WHEN the data loads THEN update the state`() {
        val state = BookmarksState.default
        val bookmark = BookmarkItem.Bookmark("ur", "title", "url", "guid", null)
        val parent = BookmarkItem.Folder("title", "guid", null)

        val result = bookmarksReducer(state, BookmarkToEditLoaded(bookmark = bookmark, folder = parent))
        val expected = state.copy(
            currentFolder = parent,
            bookmarksEditBookmarkState = BookmarksEditBookmarkState(
                bookmark = bookmark,
                folder = parent,
            ),
        )
        assertEquals(expected, result)
    }

    @Test
    fun `GIVEN selected items WHEN a multi-select menu action is taken THEN the items are unselected`() {
        val item = BookmarkItem.Bookmark("ur", "title", "url", "guid", null)
        val parent = BookmarkItem.Folder("title", "guid", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = listOf(item),
            selectedItems = listOf(item),
            currentFolder = parent,
        )

        var result = bookmarksReducer(state, BookmarksListMenuAction.MultiSelect.EditClicked)
        assertTrue(result.selectedItems.isEmpty())

        result = bookmarksReducer(state, BookmarksListMenuAction.MultiSelect.MoveClicked)
        assertTrue(result.selectedItems.isEmpty())

        result = bookmarksReducer(state, BookmarksListMenuAction.MultiSelect.DeleteClicked)
        assertTrue(result.selectedItems.isEmpty())

        result =
            bookmarksReducer(state, BookmarksListMenuAction.MultiSelect.OpenInNormalTabsClicked)
        assertTrue(result.selectedItems.isEmpty())

        result =
            bookmarksReducer(state, BookmarksListMenuAction.MultiSelect.OpenInPrivateTabsClicked)
        assertTrue(result.selectedItems.isEmpty())

        result = bookmarksReducer(state, BookmarksListMenuAction.MultiSelect.ShareClicked)
        assertTrue(result.selectedItems.isEmpty())
    }

    @Test
    fun `GIVEN a folder is not selected WHEN selecting it from the menu THEN it is added to selected items`() {
        val items = listOf(generateFolder())
        val state = BookmarksState.default.copy(bookmarkItems = items)

        val result = bookmarksReducer(state, BookmarksListMenuAction.Folder.SelectClicked(items[0]))

        assertEquals(items[0], result.selectedItems[0])
    }

    @Test
    fun `GIVEN a folder is already selected WHEN selecting it from the menu THEN it is removed from the selected items`() {
        val items = listOf(generateFolder())
        val state = BookmarksState.default.copy(bookmarkItems = items, selectedItems = items)

        val result = bookmarksReducer(state, BookmarksListMenuAction.Folder.SelectClicked(items[0]))

        assertTrue(result.selectedItems.isEmpty())
    }

    @Test
    fun `GIVEN a bookmark is not selected WHEN selecting it from the menu THEN it is added to selected items`() {
        val items = listOf(generateBookmark())
        val state = BookmarksState.default.copy(bookmarkItems = items)

        val result = bookmarksReducer(state, BookmarksListMenuAction.Bookmark.SelectClicked(items[0]))

        assertEquals(items[0], result.selectedItems[0])
    }

    @Test
    fun `GIVEN a bookmark is already selected WHEN selecting it from the menu THEN it is removed from the selected items`() {
        val items = listOf(generateBookmark())
        val state = BookmarksState.default.copy(bookmarkItems = items, selectedItems = items)

        val result = bookmarksReducer(state, BookmarksListMenuAction.Bookmark.SelectClicked(items[0]))

        assertTrue(result.selectedItems.isEmpty())
    }

    @Test
    fun `GIVEN a bookmark WHEN move is clicked THEN move state is set and selection is cleared`() {
        val items = listOf(generateBookmark())
        val currentFolder = BookmarkItem.Folder("Bookmarks", "parent", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = items,
            currentFolder = currentFolder,
        )

        val result = bookmarksReducer(state, BookmarksListMenuAction.Bookmark.MoveClicked(items[0]))

        assertTrue(result.selectedItems.isEmpty())
        assertEquals(listOf(items[0].guid), result.bookmarksMultiselectMoveState?.guidsToMove)
        assertEquals(currentFolder.guid, result.bookmarksMultiselectMoveState?.destination)
        assertEquals(currentFolder.guid, result.bookmarksSelectFolderState?.outerSelectionGuid)
    }

    @Test
    fun `GIVEN a folder WHEN move is clicked THEN move state is set and selection is cleared`() {
        val items = listOf(generateFolder())
        val currentFolder = BookmarkItem.Folder("Bookmarks", "parent", null)
        val state = BookmarksState.default.copy(
            bookmarkItems = items,
            currentFolder = currentFolder,
        )

        val result = bookmarksReducer(state, BookmarksListMenuAction.Folder.MoveClicked(items[0]))

        assertTrue(result.selectedItems.isEmpty())
        assertEquals(listOf(items[0].guid), result.bookmarksMultiselectMoveState?.guidsToMove)
        assertEquals(currentFolder.guid, result.bookmarksMultiselectMoveState?.destination)
        assertEquals(currentFolder.guid, result.bookmarksSelectFolderState?.outerSelectionGuid)
    }

    @Test
    fun `WHEN RootOverflowMenuClicked THEN rootMenuShown is true`() {
        val state = BookmarksState.default
        assertFalse(state.rootMenuShown)
        val result = bookmarksReducer(state, RootOverflowMenuClicked)
        assertTrue(result.rootMenuShown)
    }

    @Test
    fun `WHEN RootOverflowMenuDismissed THEN rootMenuShown is false`() {
        val state = BookmarksState.default.copy(rootMenuShown = true)
        val result = bookmarksReducer(state, RootOverflowMenuDismissed)
        assertFalse(result.rootMenuShown)
    }

    @Test
    fun `WHEN ImportFileClicked from menu THEN rootMenuShown is false and launchFilePicker is true`() {
        val state = BookmarksState.default.copy(rootMenuShown = true)
        val result = bookmarksReducer(state, ImportAction.ImportFileClicked.FromMenu)
        assertFalse(result.rootMenuShown)
    }

    private fun generateBookmark(
        num: Int = 0,
        url: String = "url",
        title: String = "title",
        previewImageUrl: String = "previewImageUrl",
        dateAdded: Long = 0,
        position: UInt? = null,
    ) = BookmarkItem.Bookmark(url, title, previewImageUrl, "$num", position, dateAdded)

    private fun generateFolder(
        num: Int = 0,
        title: String = "title",
        dateAdded: Long = 0,
        position: UInt? = null,
    ) = BookmarkItem.Folder(title, "$num", position, dateAdded)
}
