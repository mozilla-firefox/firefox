/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.top.sites

import android.content.Context
import androidx.arch.core.executor.testing.InstantTaskExecutorRule
import androidx.room.Room
import androidx.room.testing.MigrationTestHelper
import androidx.test.core.app.ApplicationProvider
import androidx.test.platform.app.InstrumentationRegistry
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import kotlin.test.assertIs
import kotlin.test.assertNotNull
import kotlinx.coroutines.test.runTest
import mozilla.components.feature.top.sites.db.Migrations
import mozilla.components.feature.top.sites.db.TopSiteDatabase
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Rule
import org.junit.Test

private const val MIGRATION_TEST_DB = "migration-test"

@Suppress("LargeClass")
class OnDevicePinnedSitesStorageTest {
    private lateinit var context: Context
    private lateinit var storage: PinnedSiteStorage
    private lateinit var executor: ExecutorService

    @get:Rule var instantTaskExecutorRule = InstantTaskExecutorRule()

    @get:Rule
    val helper: MigrationTestHelper =
        MigrationTestHelper(
            InstrumentationRegistry.getInstrumentation(),
            TopSiteDatabase::class.java,
        )

    @Before
    fun setUp() {
        executor = Executors.newSingleThreadExecutor()

        context = ApplicationProvider.getApplicationContext()
        val database = Room.inMemoryDatabaseBuilder(context, TopSiteDatabase::class.java).build()

        storage = PinnedSiteStorage(context)
        storage.database = lazy { database }
    }

    @After
    fun tearDown() {
        executor.shutdown()
    }

    @Test
    fun testAddingAllDefaultSites() = runTest {
        val defaultTopSites =
            listOf(
                Pair("Mozilla", "https://www.mozilla.org"),
                Pair("Firefox", "https://www.firefox.com"),
                Pair("Wikipedia", "https://www.wikipedia.com"),
                Pair("Pocket", "https://www.getpocket.com"),
            )

        storage.addAllPinnedSites(defaultTopSites, isDefault = true)

        val topSites = storage.getPinnedSites()

        assertEquals(4, topSites.size)
        assertEquals(4, storage.getPinnedSitesCount())

        assertEquals("Mozilla", topSites[0].title)
        assertEquals("https://www.mozilla.org", topSites[0].url)
        assertIs<TopSite.DefaultPinned>(topSites[0])
        assertEquals("Firefox", topSites[1].title)
        assertEquals("https://www.firefox.com", topSites[1].url)
        assertIs<TopSite.DefaultPinned>(topSites[1])
        assertEquals("Wikipedia", topSites[2].title)
        assertEquals("https://www.wikipedia.com", topSites[2].url)
        assertIs<TopSite.DefaultPinned>(topSites[2])
        assertEquals("Pocket", topSites[3].title)
        assertEquals("https://www.getpocket.com", topSites[3].url)
        assertIs<TopSite.DefaultPinned>(topSites[3])
    }

    @Test
    fun testAddingPinnedSite() = runTest {
        storage.addPinnedSite("Mozilla", "https://www.mozilla.org")
        storage.addPinnedSite("Firefox", "https://www.firefox.com", isDefault = true)

        val topSites = storage.getPinnedSites()

        assertEquals(2, topSites.size)
        assertEquals(2, storage.getPinnedSitesCount())

        assertEquals("Mozilla", topSites[0].title)
        assertEquals("https://www.mozilla.org", topSites[0].url)
        assertIs<TopSite.Pinned>(topSites[0])
        assertEquals("Firefox", topSites[1].title)
        assertEquals("https://www.firefox.com", topSites[1].url)
        assertIs<TopSite.DefaultPinned>(topSites[1])
    }

    @Test
    fun testRemovingPinnedSites() = runTest {
        storage.addPinnedSite("Mozilla", "https://www.mozilla.org")
        storage.addPinnedSite("Firefox", "https://www.firefox.com")

        storage.getPinnedSites().let { topSites ->
            assertEquals(2, topSites.size)
            assertEquals(2, storage.getPinnedSitesCount())

            storage.removePinnedSite(topSites[0])
        }

        storage.getPinnedSites().let { topSites ->
            assertEquals(1, topSites.size)
            assertEquals(1, storage.getPinnedSitesCount())

            assertEquals("Firefox", topSites[0].title)
            assertEquals("https://www.firefox.com", topSites[0].url)
        }
    }

    @Test
    fun testGettingPinnedSites() = runTest {
        storage.addPinnedSite("Mozilla", "https://www.mozilla.org")
        storage.addPinnedSite("Firefox", "https://www.firefox.com", isDefault = true)

        val topSites = storage.getPinnedSites()

        assertNotNull(topSites)
        assertEquals(2, topSites.size)
        assertEquals(2, storage.getPinnedSitesCount())

        with(topSites[0]) {
            assertEquals("Mozilla", title)
            assertEquals("https://www.mozilla.org", url)
            assertIs<TopSite.Pinned>(this)
        }

        with(topSites[1]) {
            assertEquals("Firefox", title)
            assertEquals("https://www.firefox.com", url)
            assertIs<TopSite.DefaultPinned>(this)
        }
    }

    @Test
    fun testUpdatingPinnedSites() = runTest {
        storage.addPinnedSite("Mozilla", "https://www.mozilla.org")
        var pinnedSites = storage.getPinnedSites()

        assertEquals(1, pinnedSites.size)
        assertEquals(1, storage.getPinnedSitesCount())
        assertEquals("https://www.mozilla.org", pinnedSites[0].url)
        assertEquals("Mozilla", pinnedSites[0].title)

        storage.updatePinnedSite(pinnedSites[0], "", "")

        pinnedSites = storage.getPinnedSites()
        assertEquals(1, pinnedSites.size)
        assertEquals(1, storage.getPinnedSitesCount())
        assertEquals("", pinnedSites[0].url)
        assertEquals("", pinnedSites[0].title)

        storage.updatePinnedSite(pinnedSites[0], "Mozilla Firefox", "https://www.firefox.com")

        pinnedSites = storage.getPinnedSites()
        assertEquals(1, pinnedSites.size)
        assertEquals(1, storage.getPinnedSitesCount())
        assertEquals("https://www.firefox.com", pinnedSites[0].url)
        assertEquals("Mozilla Firefox", pinnedSites[0].title)
    }

    @Test
    fun migrate1to2() {
        val dbVersion1 =
            helper.createDatabase(MIGRATION_TEST_DB, 1).apply {
                execSQL(
                    "INSERT INTO " +
                        "top_sites " +
                        "(title, url, created_at) " +
                        "VALUES " +
                        "('Mozilla','mozilla.org',1)," +
                        "('Top Articles','https://getpocket.com/fenix-top-articles',2)," +
                        "('Wikipedia','https://www.wikipedia.org/',3)," +
                        "('YouTube','https://www.youtube.com/',4)"
                )
            }

        dbVersion1.query("SELECT * FROM top_sites").use { cursor ->
            assertEquals(4, cursor.columnCount)
        }

        val dbVersion2 =
            helper
                .runMigrationsAndValidate(
                    MIGRATION_TEST_DB,
                    2,
                    true,
                    Migrations.migration_1_2,
                )
                .apply {
                    execSQL(
                        "INSERT INTO " +
                            "top_sites " +
                            "(title, url, is_default, created_at) " +
                            "VALUES " +
                            "('Firefox','firefox.com',1,5)," +
                            "('Monitor','https://monitor.firefox.com/',0,5)"
                    )
                }

        dbVersion2.query("SELECT * FROM top_sites").use { cursor ->
            assertEquals(5, cursor.columnCount)

            // Check is_default for Mozilla
            cursor.moveToFirst()
            assertEquals(0, cursor.getInt(cursor.getColumnIndexOrThrow("is_default")))

            // Check is_default for Top Articles
            cursor.moveToNext()
            assertEquals(1, cursor.getInt(cursor.getColumnIndexOrThrow("is_default")))

            // Check is_default for Wikipedia
            cursor.moveToNext()
            assertEquals(1, cursor.getInt(cursor.getColumnIndexOrThrow("is_default")))

            // Check is_default for YouTube
            cursor.moveToNext()
            assertEquals(1, cursor.getInt(cursor.getColumnIndexOrThrow("is_default")))

            // Check is_default for Firefox
            cursor.moveToNext()
            assertEquals(1, cursor.getInt(cursor.getColumnIndexOrThrow("is_default")))

            // Check is_default for Monitor
            cursor.moveToNext()
            assertEquals(0, cursor.getInt(cursor.getColumnIndexOrThrow("is_default")))
        }
    }

    @Test
    fun migrate2to3() {
        val dbVersion2 =
            helper.createDatabase(MIGRATION_TEST_DB, 2).apply {
                execSQL(
                    "INSERT INTO " +
                        "top_sites " +
                        "(title, url, is_default, created_at) " +
                        "VALUES " +
                        "('Mozilla','mozilla.org',0,1)," +
                        "('Top Articles','https://getpocket.com/fenix-top-articles',0,2)," +
                        "('Wikipedia','https://www.wikipedia.org/',0,3)," +
                        "('YouTube','https://www.youtube.com/',0,4)"
                )
            }

        dbVersion2.query("SELECT * FROM top_sites").use { cursor ->
            assertEquals(5, cursor.columnCount)
        }

        val dbVersion3 =
            helper.runMigrationsAndValidate(
                MIGRATION_TEST_DB,
                3,
                true,
                Migrations.migration_2_3,
            )

        dbVersion3.query("SELECT * FROM top_sites").use { cursor ->
            assertEquals(5, cursor.columnCount)
            assertEquals(4, cursor.count)

            // Check isDefault for Mozilla
            cursor.moveToFirst()
            assertEquals("Mozilla", cursor.getString(cursor.getColumnIndexOrThrow("title")))
            assertEquals("mozilla.org", cursor.getString(cursor.getColumnIndexOrThrow("url")))
            assertEquals(0, cursor.getInt(cursor.getColumnIndexOrThrow("is_default")))
            assertEquals(1, cursor.getInt(cursor.getColumnIndexOrThrow("created_at")))

            // Check isDefault for Top Articles
            cursor.moveToNext()
            assertEquals("Top Articles", cursor.getString(cursor.getColumnIndexOrThrow("title")))
            assertEquals(
                "https://getpocket.com/fenix-top-articles",
                cursor.getString(cursor.getColumnIndexOrThrow("url")),
            )
            assertEquals(1, cursor.getInt(cursor.getColumnIndexOrThrow("is_default")))
            assertEquals(2, cursor.getInt(cursor.getColumnIndexOrThrow("created_at")))

            // Check isDefault for Wikipedia
            cursor.moveToNext()
            assertEquals("Wikipedia", cursor.getString(cursor.getColumnIndexOrThrow("title")))
            assertEquals(
                "https://www.wikipedia.org/",
                cursor.getString(cursor.getColumnIndexOrThrow("url")),
            )
            assertEquals(1, cursor.getInt(cursor.getColumnIndexOrThrow("is_default")))
            assertEquals(3, cursor.getInt(cursor.getColumnIndexOrThrow("created_at")))

            // Check isDefault for YouTube
            cursor.moveToNext()
            assertEquals("YouTube", cursor.getString(cursor.getColumnIndexOrThrow("title")))
            assertEquals(
                "https://www.youtube.com/",
                cursor.getString(cursor.getColumnIndexOrThrow("url")),
            )
            assertEquals(1, cursor.getInt(cursor.getColumnIndexOrThrow("is_default")))
            assertEquals(4, cursor.getInt(cursor.getColumnIndexOrThrow("created_at")))
        }
    }
}
