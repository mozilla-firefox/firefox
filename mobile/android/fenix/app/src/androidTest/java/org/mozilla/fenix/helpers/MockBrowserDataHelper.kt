/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.helpers

import android.content.Context
import android.os.Build
import android.os.Environment
import android.util.Log
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import kotlin.io.path.createTempDirectory
import kotlinx.coroutines.runBlocking
import mockwebserver3.MockWebServer
import mozilla.appservices.places.BookmarkRoot
import mozilla.components.browser.icons.IconRequest
import mozilla.components.browser.icons.generator.DefaultIconGenerator
import mozilla.components.browser.state.search.SearchEngine
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.storage.sync.PlacesBookmarksStorage
import mozilla.components.browser.storage.sync.PlacesHistoryStorage
import mozilla.components.concept.storage.PageVisit
import mozilla.components.concept.storage.VisitType
import mozilla.components.feature.search.ext.createSearchEngine
import mozilla.components.feature.tab.collections.TabCollectionStorage
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.helpers.Constants.TAG
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.restartApp

object MockBrowserDataHelper {
    val context: Context = InstrumentationRegistry.getInstrumentation().targetContext

    /**
     * Adds a new bookmark item, visible in the Bookmarks folder.
     *
     * @param url The URL of the bookmark item to add. URLs should use the "https://example.com" format.
     * @param title The title of the bookmark item to add.
     * @param position Example for the position param: 1u, 2u, etc.
     * @param parentGuid The parent guid of the bookmark item to add. BookmarkRoot.Mobile.id is the root id for mobile
     *   bookmarks.
     */
    fun createBookmarkItem(
        url: String,
        title: String,
        position: UInt?,
        parentGuid: String = BookmarkRoot.Mobile.id,
    ) {
        Log.i(
            TAG,
            "createBookmarkItem: Trying to add bookmark item at position: $position, with url: $url, and with title: $title",
        )
        runBlocking {
            PlacesBookmarksStorage(context)
                .addItem(
                    parentGuid,
                    url,
                    title,
                    position,
                )
        }
        Log.i(
            TAG,
            "createBookmarkItem: Added bookmark item at position: $position, with url: $url, and with title: $title",
        )
    }

    /**
     * Adds a new bookmark folder, visible in the Bookmarks folder.
     *
     * @param parentGuid The parent guid of the bookmark folder to add. BookmarkRoot.Mobile.id is the root id for mobile
     *   bookmarks.
     * @param title The title of the bookmark folder to add.
     * @param position Example for the position param: null, 1u, 2u, etc.
     * @return The guid of the newly created bookmark folder.
     */
    fun generateBookmarkFolder(
        parentGuid: String = BookmarkRoot.Mobile.id,
        title: String,
        position: UInt?,
    ): String {
        return runBlocking {
            PlacesBookmarksStorage(context)
                .addFolder(
                    parentGuid = parentGuid,
                    title = title,
                    position = position,
                )
                .getOrDefault("")
        }
    }

    /**
     * Adds a new history item, visible in the History folder.
     *
     * @param url The URL of the history item to add. URLs should use the "https://example.com" format.
     */
    fun createHistoryItem(url: String) {
        Log.i(TAG, "createHistoryItem: Trying to add history item with url: $url")
        runBlocking {
            PlacesHistoryStorage(appContext)
                .recordVisit(
                    url,
                    PageVisit(VisitType.LINK),
                )
        }
        Log.i(TAG, "createHistoryItem: Added history item with url: $url")
    }

    /**
     * Creates a new tab with a webpage, also visible in the History folder.
     *
     * URLs should use the "https://example.com" format.
     */
    fun createTabItem(url: String) {
        Log.i(TAG, "createTabItem: Trying to create a new tab with url: $url")
        runBlocking {
            appContext.components.useCases.tabsUseCases.addTab(url)
        }
        Log.i(TAG, "createTabItem: Created a new tab with url: $url")
    }

    /** Triggers a search for the provided search term in a new tab. */
    fun createSearchHistory(searchTerm: String) {
        Log.i(TAG, "createSearchHistory: Trying to perform a new search with search term: $searchTerm")
        appContext.components.useCases.searchUseCases.newTabSearch.invoke(searchTerm)
        Log.i(TAG, "createSearchHistory: Performed a new search with search term: $searchTerm")
    }

    /**
     * Creates a new custom search engine object.
     *
     * @param mockWebServer The mockWebServer instance.
     * @param searchEngineName The name of the new search engine.
     */
    private fun createCustomSearchEngine(mockWebServer: MockWebServer, searchEngineName: String): SearchEngine {
        val searchString = "http://localhost:${mockWebServer.port}/pages/searchResults.html?search={searchTerms}"
        Log.i(
            TAG,
            "createCustomSearchEngine: Trying to create a custom search engine named: $searchEngineName and search string: $searchString",
        )
        return createSearchEngine(
            name = searchEngineName,
            url = searchString,
            icon = DefaultIconGenerator().generate(appContext, IconRequest(searchString)).bitmap,
        )
    }

    /**
     * Adds a new custom search engine to the apps Search Engines list.
     *
     * @param searchEngineName Use createCustomSearchEngine method to create one.
     */
    fun addCustomSearchEngine(mockWebServer: MockWebServer, searchEngineName: String) {
        val searchEngine = createCustomSearchEngine(mockWebServer, searchEngineName)
        Log.i(TAG, "addCustomSearchEngine: Trying to add a custom search engine named: $searchEngineName")
        appContext.components.useCases.searchUseCases.addSearchEngine(searchEngine)
        Log.i(TAG, "addCustomSearchEngine: Added a custom search engine named: $searchEngineName")
    }

    /**
     * Adds and selects as default a new custom search engine to the apps Search Engines list.
     *
     * @param searchEngineName Use createCustomSearchEngine method to create one.
     */
    fun setCustomSearchEngine(mockWebServer: MockWebServer, searchEngineName: String) {
        val searchEngine = createCustomSearchEngine(mockWebServer, searchEngineName)
        Log.i(TAG, "setCustomSearchEngine: Trying to set a custom search engine named: $searchEngineName")
        with(appContext.components.useCases.searchUseCases) {
            addSearchEngine(searchEngine)
            selectSearchEngine(searchEngine)
        }
        Log.i(TAG, "setCustomSearchEngine: A custom search engine named: $searchEngineName was set")
    }

    /**
     * Adds a new pinned site to the app home screen.
     *
     * @param webPageMetadata A pair of website title and URL.
     */
    fun addPinnedSite(vararg webPageMetadata: Pair<String, String>, activityTestRule: HomeActivityIntentTestRule) {
        runBlocking {
            for (metadata in webPageMetadata) {
                Log.i(TAG, "addTopSite: Trying to add a pinned site to the home screen.")
                appContext.components.useCases.topSitesUseCase.addPinnedSites(
                    metadata.first,
                    metadata.second,
                )
                Log.i(TAG, "addTopSite: Added a pinned site to the home screen.")
            }
            // Sometimes a restart is needed to ensure the pinned site is displayed on the home screen.
            restartApp(activityTestRule)
        }
    }

    /**
     * Creates a new collection with the provided tabs.
     *
     * @param tabInfo Pairs of URLs and titles of the tabs to add to the collection.
     * @param title The title of the collection to create.
     */
    fun createCollection(vararg tabInfo: Pair<String, String>, title: String) {
        runBlocking {
            val tabs = tabInfo.map { (tabUrl, tabTitle) ->
                createTab(url = tabUrl, title = tabTitle)
            }

            TabCollectionStorage(context).createCollection(title, tabs)
        }
    }

    /**
     * Creates a Netscape-format bookmarks HTML file in the device Downloads directory, suitable for exercising the
     * "Import bookmarks from file" flow. The file is written inside a fresh temporary directory; callers are
     * responsible for deleting that directory once the test completes (e.g. `file.parentFile?.deleteRecursively()`).
     *
     * @param bookmarks Pairs of bookmark title and URL to include in the file.
     * @return The created bookmarks HTML [File].
     */
    fun createBookmarksHtmlFile(
        vararg bookmarks: Pair<String, String> = arrayOf("Mozilla" to "https://www.mozilla.org")
    ): File {
        val downloadsDirectory =
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                requireNotNull(appContext.getExternalFilesDir(Environment.DIRECTORY_DOWNLOADS))
            } else {
                Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
            }

        downloadsDirectory.mkdirs()
        val tempFileDirectory =
            createTempDirectory(directory = downloadsDirectory.toPath(), prefix = "bookmarks_import_").toFile()

        val entries = bookmarks.joinToString("\n") { (title, url) -> """  <DT><A HREF="$url">$title</A>""" }

        return File(tempFileDirectory, "bookmarks.html").apply {
            writeText(
                """
                <!DOCTYPE NETSCAPE-Bookmark-file-1>
                <HTML>
                <DL><p>
                $entries
                </DL>
                """
                    .trimIndent()
            )
        }
    }
}
