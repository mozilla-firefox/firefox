/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.storage.sync

import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.test.runTest
import mozilla.appservices.remotetabs.ClientRemoteTabs
import mozilla.appservices.remotetabs.RemoteTab
import mozilla.appservices.remotetabs.TabsApiException as RemoteTabProviderException
import mozilla.appservices.remotetabs.TabsStore as RemoteTabsProvider
import mozilla.appservices.sync15.DeviceType
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.support.test.any
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.mock
import org.mockito.Mockito.spy
import org.mockito.Mockito.times
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when`

@RunWith(AndroidJUnit4::class)
class RemoteTabsStorageTest {
    private lateinit var remoteTabs: RemoteTabsStorage
    private lateinit var apiMock: RemoteTabsProvider
    private lateinit var crashReporter: CrashReporting

    @Before
    fun setup() {
        crashReporter = mock()
        remoteTabs = spy(RemoteTabsStorage(testContext, crashReporter))
        apiMock = mock(RemoteTabsProvider::class.java)
        `when`(remoteTabs.api).thenReturn(apiMock)
    }

    @After
    fun cleanup() {
        remoteTabs.cleanup()
    }

    @Test
    fun `store() translates tabs to rust format`() = runTest {
        remoteTabs.store(
            listOf(
                Tab(
                    listOf(TabEntry("Bar", "https://bar", null)),
                    0,
                    1574458165555,
                    false,
                ),
                Tab(
                    listOf(
                        TabEntry("Foo bar", "https://foo.bar", null),
                        TabEntry("Foo bar 1", "https://foo.bar/1", null),
                        TabEntry("Foo bar 2", "https://foo.bar/2", null),
                    ),
                    2,
                    0,
                    false,
                ),
                Tab(
                    listOf(
                        TabEntry("Foo 1", "https://foo", "https://foo/icon"),
                        TabEntry("Foo 2", "https://foo/1", "https://foo/icon2"),
                        TabEntry("Foo 3", "https://foo/1/1", "https://foo/icon3"),
                    ),
                    1,
                    1574457405635,
                    false,
                ),
            )
        )

        verify(apiMock)
            .setLocalTabs(
                listOf(
                    RemoteTab("Bar", listOf("https://bar"), null, 1574458165555),
                    RemoteTab(
                        "Foo bar 2",
                        listOf("https://foo.bar/2", "https://foo.bar/1", "https://foo.bar"),
                        null,
                        0,
                    ),
                    RemoteTab("Foo 2", listOf("https://foo/1", "https://foo"), "https://foo/icon2", 1574457405635),
                )
            )
    }

    @Test
    fun `store() replaces lone surrogates in titles`() = runTest {
        remoteTabs.store(
            listOf(
                Tab(
                    listOf(
                        TabEntry("Old \uDE00", "https://foo/2", null),
                        TabEntry("Foo \uD83D", "https://foo/1", "https://foo/icon"),
                    ),
                    1,
                    0,
                    false,
                )
            )
        )

        verify(apiMock)
            .setLocalTabs(
                listOf(
                    RemoteTab(
                        "Foo \uFFFD",
                        listOf("https://foo/1", "https://foo/2"),
                        "https://foo/icon",
                        0,
                    )
                )
            )
    }

    @Test
    fun `getAll() translates tabs to our format`() = runTest {
        `when`(apiMock.getAll())
            .thenReturn(
                listOf(
                    ClientRemoteTabs(
                        "client1",
                        "",
                        DeviceType.MOBILE,
                        0, // any value for the timestamp is OK for these tests.
                        listOf(
                            RemoteTab(
                                "Foo",
                                listOf("https://foo/1/1", "https://foo/1", "https://foo"),
                                "https://foo/icon",
                                1574457405635,
                            )
                        ),
                        emptyMap(), // tabGroups
                        emptyMap(), // windows
                    ),
                    ClientRemoteTabs(
                        "client2",
                        "",
                        DeviceType.MOBILE,
                        0, // any value for the timestamp is OK for these tests.
                        listOf(
                            RemoteTab("Bar", listOf("https://bar"), null, 1574458165555),
                            RemoteTab("Foo Bar", listOf("https://foo.bar"), "https://foo.bar/icon", 0),
                        ),
                        emptyMap(), // tabGroups
                        emptyMap(), // windows
                    ),
                )
            )

        assertEquals(
            mapOf(
                SyncClient("client1") to
                    listOf(
                        Tab(
                            listOf(
                                TabEntry("Foo", "https://foo", "https://foo/icon"),
                                TabEntry("Foo", "https://foo/1", "https://foo/icon"),
                                TabEntry("Foo", "https://foo/1/1", "https://foo/icon"),
                            ),
                            2,
                            1574457405635,
                            false,
                        )
                    ),
                SyncClient("client2") to
                    listOf(
                        Tab(
                            listOf(TabEntry("Bar", "https://bar", null)),
                            0,
                            1574458165555,
                            false,
                        ),
                        Tab(
                            listOf(TabEntry("Foo Bar", "https://foo.bar", "https://foo.bar/icon")),
                            0,
                            0,
                            false,
                        ),
                    ),
            ),
            remoteTabs.getAll(),
        )
    }

    @Test
    fun `exceptions from getAll are propagated to the crash reporter`() = runTest {
        val throwable = RemoteTabProviderException.UnexpectedTabsException("test")
        `when`(apiMock.getAll()).thenAnswer { throw throwable }

        remoteTabs.getAll()

        verify(crashReporter).submitCaughtException(throwable)

        `when`(apiMock.setLocalTabs(any())).thenAnswer { throw throwable }

        remoteTabs.store(emptyList())

        verify(crashReporter, times(2)).submitCaughtException(throwable)
    }
}
