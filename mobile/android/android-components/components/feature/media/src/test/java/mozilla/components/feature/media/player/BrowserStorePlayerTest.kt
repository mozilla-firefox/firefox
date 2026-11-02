/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.media.player

import android.graphics.Bitmap
import android.os.HandlerThread
import android.os.Looper
import androidx.media3.common.C
import androidx.media3.common.MediaMetadata
import androidx.media3.common.Player
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.time.Duration
import kotlin.coroutines.ContinuationInterceptor
import kotlin.test.assertNotNull
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.isActive
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.MediaSessionAction
import mozilla.components.browser.state.action.TabListAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.MediaSessionState
import mozilla.components.browser.state.state.createTab
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.mediasession.MediaSession
import mozilla.components.feature.media.R
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.ArgumentMatchers.anyBoolean
import org.mockito.ArgumentMatchers.anyDouble
import org.mockito.Mockito.never
import org.mockito.Mockito.verify
import org.robolectric.Shadows.shadowOf
import org.robolectric.shadows.ShadowSystemClock

@RunWith(AndroidJUnit4::class)
class BrowserStorePlayerTest {

    @Test
    fun `GIVEN an active playing tab WHEN state is read THEN playback state is ready and playWhenReady is true`() =
        runTest {
            val tab = mediaTab(title = "Mozilla")
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()

            assertEquals(Player.STATE_READY, player.playbackState)
            assertTrue(player.playWhenReady)
        }

    @Test
    fun `GIVEN an active paused tab WHEN state is read THEN playback state is ready and playWhenReady is false`() =
        runTest {
            val tab = mediaTab(MediaSession.PlaybackState.PAUSED)
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()

            assertEquals(Player.STATE_READY, player.playbackState)
            assertFalse(player.playWhenReady)
        }

    @Test
    fun `GIVEN no active media tab WHEN state is read THEN playback state is idle and playlist is empty`() = runTest {
        val store = BrowserStore(BrowserState())

        val player = newPlayer(store)
        drain()

        assertEquals(Player.STATE_IDLE, player.playbackState)
        assertEquals(0, player.mediaItemCount)
    }

    @Test
    fun `GIVEN a stopped media tab WHEN state is read THEN playback state is idle`() = runTest {
        val tab = mediaTab(MediaSession.PlaybackState.STOPPED)
        val store = BrowserStore(BrowserState(tabs = listOf(tab)))

        val player = newPlayer(store)
        drain()

        assertEquals(Player.STATE_IDLE, player.playbackState)
    }

    @Test
    fun `WHEN play is called THEN the active tab's controller play is invoked`() = runTest {
        val controller: MediaSession.Controller = mock()
        val tab = mediaTab(MediaSession.PlaybackState.PAUSED, controller)
        val store = BrowserStore(BrowserState(tabs = listOf(tab)))

        val player = newPlayer(store)
        drain()

        player.play()
        drain()

        verify(controller).play()
    }

    @Test
    fun `WHEN pause is called THEN the active tab's controller pause is invoked`() = runTest {
        val controller: MediaSession.Controller = mock()
        val tab = mediaTab(controller = controller)
        val store = BrowserStore(BrowserState(tabs = listOf(tab)))

        val player = newPlayer(store)
        drain()

        player.pause()
        drain()

        verify(controller).pause()
    }

    @Test
    fun `GIVEN an active tab WHEN state is read THEN the expected available commands are advertised`() = runTest {
        val tab = mediaTab()
        val store = BrowserStore(BrowserState(tabs = listOf(tab)))

        val player = newPlayer(store)
        drain()

        val commands = player.availableCommands
        assertTrue(commands.contains(Player.COMMAND_PLAY_PAUSE))
        assertTrue(commands.contains(Player.COMMAND_RELEASE))
        assertTrue(commands.contains(Player.COMMAND_GET_CURRENT_MEDIA_ITEM))
        assertTrue(commands.contains(Player.COMMAND_GET_METADATA))
        assertTrue(commands.contains(Player.COMMAND_GET_TIMELINE))
        assertFalse(commands.contains(Player.COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM))
        assertFalse(commands.contains(Player.COMMAND_SEEK_TO_NEXT))
        assertFalse(commands.contains(Player.COMMAND_SEEK_TO_PREVIOUS))
        assertFalse(commands.contains(Player.COMMAND_SET_MEDIA_ITEM))
    }

    @Test
    fun `GIVEN a tab with no metadata title WHEN state is read THEN the media item title falls back to the page title`() =
        runTest {
            val tab = mediaTab(title = "Mozilla")
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()

            val metadata = player.mediaMetadata
            assertEquals("Mozilla", metadata.title.toString())
            assertEquals("https://www.mozilla.org", metadata.artist.toString())
        }

    @Test
    fun `GIVEN a tab with no metadata title WHEN the page title loads THEN the media item title updates`() = runTest {
        val tab = mediaTab()
        val store = BrowserStore(BrowserState(tabs = listOf(tab)))

        val player = newPlayer(store)
        drain()
        assertEquals("https://www.mozilla.org", player.mediaMetadata.title.toString())

        store.dispatch(ContentAction.UpdateTitleAction(tab.id, "Mozilla"))
        drain()

        assertEquals("Mozilla", player.mediaMetadata.title.toString())
    }

    @Test
    fun `GIVEN a tab with no metadata WHEN its URL changes THEN the media item artist updates`() = runTest {
        val tab = mediaTab()
        val store = BrowserStore(BrowserState(tabs = listOf(tab)))

        val player = newPlayer(store)
        drain()

        store.dispatch(ContentAction.UpdateUrlAction(tab.id, "https://www.mozilla.org/next"))
        drain()

        assertEquals("https://www.mozilla.org/next", player.mediaMetadata.artist.toString())
    }

    @Test
    fun `GIVEN a tab with metadata title WHEN state is read THEN the media item uses the metadata title`() = runTest {
        val tab =
            mediaTab(
                title = "Mozilla",
                metadata = MediaSession.Metadata(title = "Song", artist = "Artist", album = null, getArtwork = null),
            )
        val store = BrowserStore(BrowserState(tabs = listOf(tab)))

        val player = newPlayer(store)
        drain()

        val metadata = player.mediaMetadata
        assertEquals("Song", metadata.title.toString())
        assertEquals("Artist", metadata.artist.toString())
    }

    @Test
    fun `GIVEN a private tab WHEN state is read THEN the title is the private mode label and artist is empty`() =
        runTest {
            val tab = mediaTab(title = "Secret", private = true)
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()

            val privateLabel = testContext.getString(R.string.mozac_feature_media_notification_private_mode)
            val metadata = player.mediaMetadata
            assertEquals(privateLabel, metadata.title.toString())
            assertEquals("", metadata.artist.toString())
            assertNull(metadata.artworkData)
        }

    @Test
    fun `GIVEN a playing tab with artwork WHEN artwork is fetched THEN it is exposed for that track and the change is reported`() =
        runTest {
            val bitmap = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
            val tab =
                mediaTab(
                    metadata =
                        MediaSession.Metadata(title = "Song", artist = "Artist", album = null, getArtwork = { bitmap })
                )
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            var artworkChanges = 0

            val player = newPlayer(store, onArtworkChanged = { artworkChanges++ })
            drain()

            assertNotNull(player.mediaMetadata.artworkData)
            assertSame(bitmap, player.artworkFor(tab)?.bitmap)
            assertEquals(1, artworkChanges)
            val otherTrack =
                tab.copy(
                    mediaSessionState =
                        tab.mediaSessionState!!.copy(
                            metadata = MediaSession.Metadata(title = "Other", getArtwork = null)
                        )
                )
            assertNull(player.artworkFor(otherTrack))
        }

    @Test
    fun `GIVEN a tab with no session artwork WHEN its favicon loads THEN state exposes artwork bytes`() = runTest {
        val tab = mediaTab()
        val store = BrowserStore(BrowserState(tabs = listOf(tab)))

        val player = newPlayer(store)
        drain()
        assertNull(player.mediaMetadata.artworkData)

        store.dispatch(
            ContentAction.UpdateIconAction(tab.id, tab.content.url, Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888))
        )
        drain()

        assertNotNull(player.mediaMetadata.artworkData)
    }

    @Test
    fun `GIVEN a tab showing its favicon as artwork WHEN the favicon changes THEN the new favicon is exposed`() =
        runTest {
            val tab = mediaTab()
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()
            store.dispatch(
                ContentAction.UpdateIconAction(
                    tab.id,
                    tab.content.url,
                    Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888),
                )
            )
            drain()

            val newIcon = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
            store.dispatch(ContentAction.UpdateIconAction(tab.id, tab.content.url, newIcon))
            drain()

            assertSame(newIcon, player.artworkFor(store.state.tabs.single())?.bitmap)
        }

    @Test
    fun `GIVEN a tab showing its favicon as artwork WHEN a favicon with the same pixels loads THEN no change is reported`() =
        runTest {
            val tab = mediaTab()
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            var artworkChanges = 0
            newPlayer(store, onArtworkChanged = { artworkChanges++ })
            store.dispatch(
                ContentAction.UpdateIconAction(
                    tab.id,
                    tab.content.url,
                    Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888),
                )
            )
            drain()
            assertEquals(1, artworkChanges)

            store.dispatch(
                ContentAction.UpdateIconAction(
                    tab.id,
                    tab.content.url,
                    Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888),
                )
            )
            drain()

            assertEquals(1, artworkChanges)
        }

    @Test
    fun `GIVEN a tab showing its favicon as artwork WHEN the favicon is removed THEN the artwork is dropped and the change is reported`() =
        runTest {
            val tab = mediaTab()
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            var artworkChanges = 0
            val player = newPlayer(store, onArtworkChanged = { artworkChanges++ })
            store.dispatch(
                ContentAction.UpdateIconAction(
                    tab.id,
                    tab.content.url,
                    Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888),
                )
            )
            drain()
            assertEquals(1, artworkChanges)

            store.dispatch(ContentAction.RemoveIconAction(tab.id))
            drain()

            assertNull(player.mediaMetadata.artworkData)
            assertEquals(2, artworkChanges)
        }

    @Test
    fun `GIVEN a tab whose session artwork has arrived WHEN its favicon loads THEN the artwork is not fetched again`() =
        runTest {
            val bitmap = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
            var fetches = 0
            val tab =
                mediaTab(
                    metadata =
                        MediaSession.Metadata(
                            title = "Song",
                            getArtwork = {
                                fetches++
                                bitmap
                            },
                        )
                )
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()

            store.dispatch(
                ContentAction.UpdateIconAction(
                    tab.id,
                    tab.content.url,
                    Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888),
                )
            )
            drain()

            assertEquals(1, fetches)
            assertSame(bitmap, player.artworkFor(store.state.tabs.single())?.bitmap)
        }

    @Test
    fun `GIVEN a tab whose session artwork fails WHEN its favicon loads THEN the favicon is exposed as artwork`() =
        runTest {
            val tab = mediaTab(metadata = MediaSession.Metadata(title = "Song", getArtwork = { null }))
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()
            assertNull(player.mediaMetadata.artworkData)

            store.dispatch(
                ContentAction.UpdateIconAction(
                    tab.id,
                    tab.content.url,
                    Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888),
                )
            )
            drain()

            assertNotNull(player.mediaMetadata.artworkData)
        }

    @Test
    fun `GIVEN a private tab with artwork WHEN artwork is fetched THEN state does not expose artwork bytes`() =
        runTest {
            val bitmap = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
            val tab =
                mediaTab(
                    private = true,
                    metadata =
                        MediaSession.Metadata(title = "Song", artist = "Artist", album = null, getArtwork = { bitmap }),
                )
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()

            assertNull(player.mediaMetadata.artworkData)
        }

    @Test
    fun `GIVEN a playing tab with cached artwork WHEN its media session is deactivated THEN the cached artwork is cleared`() =
        runTest {
            val bitmap = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
            val tab =
                mediaTab(
                    metadata =
                        MediaSession.Metadata(title = "Song", artist = "Artist", album = null, getArtwork = { bitmap })
                )
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()
            assertNotNull(player.mediaMetadata.artworkData)

            store.dispatch(MediaSessionAction.DeactivatedMediaSessionAction(tabId = tab.id))
            drain()

            assertNull(player.cachedArtwork)
            assertNull(player.mediaMetadata.artworkData)
        }

    @Test
    fun `GIVEN a playing tab WHEN its playback state transitions to paused THEN playWhenReady flips to false`() =
        runTest {
            val tab = mediaTab()
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()
            assertTrue(player.playWhenReady)

            store.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(
                    tabId = tab.id,
                    playbackState = MediaSession.PlaybackState.PAUSED,
                )
            )
            drain()

            assertEquals(Player.STATE_READY, player.playbackState)
            assertFalse(player.playWhenReady)
        }

    @Test
    fun `GIVEN two tabs with artwork WHEN the active tab changes THEN the cached artwork is not shown for the new tab until its fetch completes`() =
        runTest {
            val bitmapA = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
            val pendingB = CompletableDeferred<Bitmap?>()
            val tabA =
                mediaTab(
                        url = "https://a.example",
                        metadata =
                            MediaSession.Metadata(title = "A", artist = null, album = null, getArtwork = { bitmapA }),
                    )
                    .copy(id = "tab-a")
            val tabB =
                mediaTab(
                        MediaSession.PlaybackState.PAUSED,
                        url = "https://b.example",
                        metadata =
                            MediaSession.Metadata(
                                title = "B",
                                artist = null,
                                album = null,
                                getArtwork = { pendingB.await() },
                            ),
                    )
                    .copy(id = "tab-b")
            val store = BrowserStore(BrowserState(tabs = listOf(tabA, tabB)))

            val player = newPlayer(store)
            drain()
            assertNotNull(player.mediaMetadata.artworkData)

            store.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(
                    tabId = tabA.id,
                    playbackState = MediaSession.PlaybackState.STOPPED,
                )
            )
            drain()

            assertEquals("B", player.mediaMetadata.title.toString())
            assertNull(player.mediaMetadata.artworkData)
        }

    @Test
    fun `GIVEN a tab with artwork WHEN its metadata changes THEN the old artwork is not shown until the new fetch completes`() =
        runTest {
            val bitmapA = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
            val pendingB = CompletableDeferred<Bitmap?>()
            val tab =
                mediaTab(
                    metadata = MediaSession.Metadata(title = "A", artist = null, album = null, getArtwork = { bitmapA })
                )
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()
            assertNotNull(player.mediaMetadata.artworkData)

            store.dispatch(
                MediaSessionAction.UpdateMediaMetadataAction(
                    tabId = tab.id,
                    metadata =
                        MediaSession.Metadata(
                            title = "B",
                            artist = null,
                            album = null,
                            getArtwork = { pendingB.await() },
                        ),
                )
            )
            drain()

            assertEquals("B", player.mediaMetadata.title.toString())
            assertNull(player.mediaMetadata.artworkData)

            pendingB.complete(bitmapA)
            drain()
            assertNotNull(player.mediaMetadata.artworkData)
        }

    @Test
    fun `GIVEN a fetch in flight for new metadata WHEN the metadata reverts THEN the earlier artwork is kept`() =
        runTest {
            val bitmapA = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
            val pendingB = CompletableDeferred<Bitmap?>()
            val metadataA = MediaSession.Metadata(title = "A", getArtwork = { bitmapA })
            val tab = mediaTab(metadata = metadataA)
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()

            store.dispatch(
                MediaSessionAction.UpdateMediaMetadataAction(
                    tab.id,
                    MediaSession.Metadata(title = "B", getArtwork = { pendingB.await() }),
                )
            )
            drain()
            store.dispatch(MediaSessionAction.UpdateMediaMetadataAction(tab.id, metadataA))
            drain()
            pendingB.complete(Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888))
            drain()

            assertSame(bitmapA, player.artworkFor(store.state.tabs.single())?.bitmap)
        }

    @Test
    fun `GIVEN no active media tab WHEN play is called THEN no crash and playback state remains idle`() = runTest {
        val store = BrowserStore(BrowserState())

        val player = newPlayer(store)
        drain()

        player.play()
        drain()

        assertEquals(Player.STATE_IDLE, player.playbackState)
    }

    @Test
    fun `GIVEN two media tabs WHEN pause is called THEN only the active tab pauses`() = runTest {
        val controllerA: MediaSession.Controller = mock()
        val controllerB: MediaSession.Controller = mock()
        val activeTab = mediaTab(controller = controllerA, url = "https://a.example").copy(id = "tab-a")
        val inactiveTab =
            mediaTab(MediaSession.PlaybackState.STOPPED, controllerB, url = "https://b.example").copy(id = "tab-b")
        val store = BrowserStore(BrowserState(tabs = listOf(activeTab, inactiveTab)))

        val player = newPlayer(store)
        drain()

        player.pause()
        drain()

        verify(controllerA).pause()
        verify(controllerB, never()).pause()
    }

    @Test
    fun `WHEN release is called THEN the store flow scope is cancelled`() = runTest {
        val tab = mediaTab()
        val store = BrowserStore(BrowserState(tabs = listOf(tab)))

        val player = newPlayer(store)
        drain()
        assertTrue(player.scope.isActive)

        player.release()
        drain()

        assertFalse(player.scope.isActive)
    }

    @Test
    fun `GIVEN a looper that is not the current thread WHEN constructed THEN it throws IllegalStateException`() =
        runTest {
            val otherThread = HandlerThread("BrowserStorePlayerTest-other").apply { start() }
            try {
                val store = BrowserStore(BrowserState())
                val dispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher
                assertThrows(IllegalStateException::class.java) {
                    BrowserStorePlayer(
                        context = testContext,
                        store = store,
                        mainDispatcher = dispatcher,
                        encodingDispatcher = dispatcher,
                        looper = otherThread.looper,
                    )
                }
            } finally {
                otherThread.quitSafely()
            }
        }

    @Test
    fun `GIVEN a tab with empty title and no metadata WHEN state is read THEN the title falls back to the URL`() =
        runTest {
            val tab = mediaTab(title = "")
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()

            assertEquals("https://www.mozilla.org", player.mediaMetadata.title.toString())
        }

    @Test
    fun `GIVEN a listener is added WHEN playback transitions to paused THEN onIsPlayingChanged fires with false`() =
        runTest {
            val tab = mediaTab()
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()

            val observed = mutableListOf<Boolean>()
            player.addListener(
                object : Player.Listener {
                    override fun onIsPlayingChanged(isPlaying: Boolean) {
                        observed.add(isPlaying)
                    }
                }
            )

            store.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(
                    tabId = tab.id,
                    playbackState = MediaSession.PlaybackState.PAUSED,
                )
            )
            drain()

            assertEquals(listOf(false), observed)
        }

    @Test
    fun `GIVEN a playing tab WHEN its metadata is updated THEN artwork is re-fetched and mediaMetadata reflects the new title`() =
        runTest {
            val bitmap = Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888)
            var artworkCalls = 0
            val tab =
                mediaTab(
                    metadata =
                        MediaSession.Metadata(
                            title = "A",
                            artist = null,
                            album = null,
                            getArtwork = {
                                artworkCalls++
                                bitmap
                            },
                        )
                )
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()
            assertEquals(1, artworkCalls)
            assertEquals("A", player.mediaMetadata.title.toString())

            store.dispatch(
                MediaSessionAction.UpdateMediaMetadataAction(
                    tabId = tab.id,
                    metadata =
                        MediaSession.Metadata(
                            title = "B",
                            artist = null,
                            album = null,
                            getArtwork = {
                                artworkCalls++
                                bitmap
                            },
                        ),
                )
            )
            drain()

            assertEquals(2, artworkCalls)
            assertEquals("B", player.mediaMetadata.title.toString())
        }

    @Test
    fun `GIVEN a listener is added WHEN metadata is updated THEN onMediaMetadataChanged fires once with the new title`() =
        runTest {
            val tab =
                mediaTab(metadata = MediaSession.Metadata(title = "A", artist = null, album = null, getArtwork = null))
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()

            val observed = mutableListOf<String>()
            player.addListener(
                object : Player.Listener {
                    override fun onMediaMetadataChanged(mediaMetadata: MediaMetadata) {
                        observed.add(mediaMetadata.title.toString())
                    }
                }
            )

            store.dispatch(
                MediaSessionAction.UpdateMediaMetadataAction(
                    tabId = tab.id,
                    metadata =
                        MediaSession.Metadata(
                            title = "B",
                            artist = null,
                            album = null,
                            getArtwork = null,
                        ),
                )
            )
            drain()

            assertEquals(listOf("B"), observed)
        }

    @Test
    fun `GIVEN release has been called WHEN release is called again THEN it does not throw`() = runTest {
        val tab = mediaTab()
        val store = BrowserStore(BrowserState(tabs = listOf(tab)))

        val player = newPlayer(store)
        drain()

        player.release()
        drain()
        player.release()
        drain()

        assertFalse(player.scope.isActive)
    }

    @Test
    fun `GIVEN a released player WHEN a store action is dispatched THEN Media3 listener cleanup prevents events`() =
        runTest {
            val tab = mediaTab()
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))

            val player = newPlayer(store)
            drain()

            val events = mutableListOf<String>()
            player.addListener(
                object : Player.Listener {
                    override fun onIsPlayingChanged(isPlaying: Boolean) {
                        events.add("isPlaying=$isPlaying")
                    }

                    override fun onMediaMetadataChanged(mediaMetadata: MediaMetadata) {
                        events.add("metadata=${mediaMetadata.title}")
                    }
                }
            )

            player.release()
            drain()

            store.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(
                    tabId = tab.id,
                    playbackState = MediaSession.PlaybackState.PAUSED,
                )
            )
            drain()

            assertTrue(events.isEmpty())
        }

    @Test
    fun `GIVEN the active media tab changes WHEN setPlayWhenReady is called THEN the new active tab's controller is invoked`() =
        runTest {
            val controllerA: MediaSession.Controller = mock()
            val controllerB: MediaSession.Controller = mock()
            val tabA = mediaTab(controller = controllerA, url = "https://a.example").copy(id = "tab-a")
            val tabB =
                mediaTab(MediaSession.PlaybackState.UNKNOWN, controllerB, url = "https://b.example").copy(id = "tab-b")
            val store = BrowserStore(BrowserState(tabs = listOf(tabA, tabB)))

            val player = newPlayer(store)
            drain()

            store.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(
                    tabId = tabA.id,
                    playbackState = MediaSession.PlaybackState.UNKNOWN,
                )
            )
            store.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(
                    tabId = tabB.id,
                    playbackState = MediaSession.PlaybackState.PAUSED,
                )
            )
            drain()

            player.play()
            drain()

            verify(controllerB).play()
            verify(controllerA, never()).play()
        }

    @Test
    fun `GIVEN a tab whose artwork fetch throws WHEN artwork is fetched THEN no artwork is exposed and no change is reported`() =
        runTest {
            val tab =
                mediaTab(
                    metadata =
                        MediaSession.Metadata(
                            title = "Song",
                            artist = null,
                            album = null,
                            getArtwork = { throw IllegalStateException("artwork failed") },
                        )
                )
            var artworkChanges = 0
            val player =
                newPlayer(BrowserStore(BrowserState(tabs = listOf(tab))), onArtworkChanged = { artworkChanges++ })
            drain()

            assertNull(player.mediaMetadata.artworkData)
            assertEquals(Player.STATE_READY, player.playbackState)
            assertEquals(0, artworkChanges)
        }

    @Test
    fun `GIVEN a tab advertising NEXT_TRACK THEN only the next-track seek command is advertised`() = runTest {
        val tab = mediaTab(features = MediaSession.Feature(MediaSession.Feature.NEXT_TRACK))
        val player = newPlayer(BrowserStore(BrowserState(tabs = listOf(tab))))
        drain()

        assertTrue(player.availableCommands.contains(Player.COMMAND_SEEK_TO_NEXT))
        assertFalse(player.availableCommands.contains(Player.COMMAND_SEEK_TO_PREVIOUS))
    }

    @Test
    fun `GIVEN a tab with no track features THEN no skip commands are advertised`() = runTest {
        val tab = mediaTab()
        val player = newPlayer(BrowserStore(BrowserState(tabs = listOf(tab))))
        drain()

        assertFalse(player.availableCommands.contains(Player.COMMAND_SEEK_TO_NEXT))
        assertFalse(player.availableCommands.contains(Player.COMMAND_SEEK_TO_PREVIOUS))
    }

    @Test
    fun `GIVEN a single-item playlist WHEN seeking to next or previous THEN the active tab's controller is used`() =
        runTest {
            val controller: MediaSession.Controller = mock()
            val tab =
                mediaTab(
                    controller = controller,
                    features =
                        MediaSession.Feature(MediaSession.Feature.NEXT_TRACK or MediaSession.Feature.PREVIOUS_TRACK),
                )
            val player = newPlayer(BrowserStore(BrowserState(tabs = listOf(tab))))
            drain()

            player.seekToNext()
            drain()
            player.seekToPrevious()
            drain()

            verify(controller).nextTrack()
            verify(controller).previousTrack()
        }

    @Test
    fun `GIVEN a paused tab with position state THEN position, duration, speed and seekability are reported`() =
        runTest {
            val tab =
                mediaTab(
                    MediaSession.PlaybackState.PAUSED,
                    positionState = MediaSession.PositionState(duration = 100.0, position = 30.0, playbackRate = 1.5),
                )
            val player = newPlayer(BrowserStore(BrowserState(tabs = listOf(tab))))
            drain()

            assertEquals(30_000L, player.currentPosition)
            assertEquals(100_000L, player.duration)
            assertEquals(1.5f, player.playbackParameters.speed)
            assertTrue(player.isCurrentMediaItemSeekable)
            assertTrue(player.availableCommands.contains(Player.COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM))
        }

    @Test
    fun `GIVEN a tab with neither a duration nor the SEEK_TO feature THEN it is not seekable`() = runTest {
        val tab = mediaTab()
        val player = newPlayer(BrowserStore(BrowserState(tabs = listOf(tab))))
        drain()

        assertEquals(C.TIME_UNSET, player.duration)
        assertEquals(1f, player.playbackParameters.speed)
        assertFalse(player.isCurrentMediaItemSeekable)
        assertFalse(player.availableCommands.contains(Player.COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM))
    }

    @Test
    fun `GIVEN a tab advertising SEEK_TO without a duration THEN the seek command is still advertised`() = runTest {
        val tab = mediaTab(features = MediaSession.Feature(MediaSession.Feature.SEEK_TO))
        val player = newPlayer(BrowserStore(BrowserState(tabs = listOf(tab))))
        drain()

        assertTrue(player.isCurrentMediaItemSeekable)
        assertTrue(player.availableCommands.contains(Player.COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM))
    }

    @Test
    fun `GIVEN a seekable tab WHEN seeking to a position THEN the controller seeks there and the position follows`() =
        runTest {
            val controller: MediaSession.Controller = mock()
            val tab =
                mediaTab(
                    controller = controller,
                    positionState = MediaSession.PositionState(duration = 100.0, position = 30.0),
                )
            val player = newPlayer(BrowserStore(BrowserState(tabs = listOf(tab))))
            drain()

            player.seekTo(45_500L)
            drain()

            verify(controller).seekTo(45.5, fast = false)
            assertEquals(45_500L, player.currentPosition)

            ShadowSystemClock.advanceBy(Duration.ofSeconds(5))
            drain()

            assertEquals(50_500L, player.currentPosition)
        }

    @Test
    fun `GIVEN a paused tab WHEN seeking to a position THEN the position holds there until playback resumes`() =
        runTest {
            val tab =
                mediaTab(
                    MediaSession.PlaybackState.PAUSED,
                    positionState = MediaSession.PositionState(duration = 100.0, position = 30.0),
                )
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()

            player.seekTo(45_000L)
            drain()
            ShadowSystemClock.advanceBy(Duration.ofSeconds(10))
            drain()

            assertEquals(45_000L, player.currentPosition)

            store.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(
                    tabId = tab.id,
                    playbackState = MediaSession.PlaybackState.PLAYING,
                )
            )
            drain()
            ShadowSystemClock.advanceBy(Duration.ofSeconds(10))
            drain()

            assertEquals(55_000L, player.currentPosition)
        }

    @Test
    fun `GIVEN the title changes WHEN the position state is still the old track's THEN position 0 is reported until it refreshes`() =
        runTest {
            val position = MediaSession.PositionState(duration = 100.0, position = 30.0)
            val tab =
                mediaTab(
                    MediaSession.PlaybackState.PAUSED,
                    metadata = MediaSession.Metadata(title = "A", getArtwork = null),
                    positionState = position,
                )
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()
            assertEquals(30_000L, player.currentPosition)

            store.dispatch(
                MediaSessionAction.UpdateMediaMetadataAction(
                    tab.id,
                    MediaSession.Metadata(title = "B", getArtwork = null),
                )
            )
            drain()
            assertEquals(0L, player.currentPosition)

            store.dispatch(MediaSessionAction.UpdateMediaPositionStateAction(tab.id, position.copy(position = 2.0)))
            drain()
            assertEquals(2_000L, player.currentPosition)
        }

    @Test
    fun `GIVEN a seekable tab WHEN seeking to TIME_UNSET THEN the controller is not asked to seek`() = runTest {
        val controller: MediaSession.Controller = mock()
        val tab =
            mediaTab(
                controller = controller,
                positionState = MediaSession.PositionState(duration = 100.0, position = 30.0),
            )
        val player = newPlayer(BrowserStore(BrowserState(tabs = listOf(tab))))
        drain()

        player.seekTo(C.TIME_UNSET)
        drain()

        verify(controller, never()).seekTo(anyDouble(), anyBoolean())
    }

    @Test
    fun `GIVEN a playing tab WHEN the clock advances THEN the reported position advances at the playback rate`() =
        runTest {
            fun tabAt(state: MediaSession.PlaybackState, rate: Double) =
                mediaTab(
                    state,
                    positionState = MediaSession.PositionState(duration = 100.0, position = 30.0, playbackRate = rate),
                )
            val playing =
                newPlayer(BrowserStore(BrowserState(tabs = listOf(tabAt(MediaSession.PlaybackState.PLAYING, 2.0)))))
            val paused =
                newPlayer(BrowserStore(BrowserState(tabs = listOf(tabAt(MediaSession.PlaybackState.PAUSED, 2.0)))))
            drain()

            ShadowSystemClock.advanceBy(Duration.ofSeconds(5))
            drain()

            assertEquals(40_000L, playing.currentPosition)
            assertEquals(30_000L, paused.currentPosition)
        }

    @Test
    fun `GIVEN a playing tab WHEN an unrelated state change arrives mid-track THEN the position keeps advancing`() =
        runTest {
            val tab = mediaTab(positionState = MediaSession.PositionState(duration = 100.0, position = 30.0))
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()

            ShadowSystemClock.advanceBy(Duration.ofSeconds(5))
            store.dispatch(
                MediaSessionAction.UpdateMediaFeatureAction(
                    tab.id,
                    MediaSession.Feature(MediaSession.Feature.NEXT_TRACK),
                )
            )
            drain()

            assertEquals(35_000L, player.currentPosition)
        }

    @Test
    fun `GIVEN a paused tab WHEN playback resumes with an unchanged position state THEN the position advances from the resume`() =
        runTest {
            val tab =
                mediaTab(
                    MediaSession.PlaybackState.PAUSED,
                    positionState = MediaSession.PositionState(duration = 100.0, position = 30.0),
                )
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()

            ShadowSystemClock.advanceBy(Duration.ofSeconds(10))
            store.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(tab.id, MediaSession.PlaybackState.PLAYING)
            )
            drain()
            ShadowSystemClock.advanceBy(Duration.ofSeconds(5))

            assertEquals(35_000L, player.currentPosition)
        }

    @Test
    fun `GIVEN a playing tab WHEN it pauses with an unchanged position state THEN the position holds where it was`() =
        runTest {
            val tab = mediaTab(positionState = MediaSession.PositionState(duration = 100.0, position = 30.0))
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()

            ShadowSystemClock.advanceBy(Duration.ofSeconds(20))
            store.dispatch(MediaSessionAction.UpdateMediaPlaybackStateAction(tab.id, MediaSession.PlaybackState.PAUSED))
            drain()
            ShadowSystemClock.advanceBy(Duration.ofSeconds(5))

            assertEquals(50_000L, player.currentPosition)
        }

    @Test
    fun `GIVEN a playing track WHEN the next track also reports position 0 THEN its position restarts from 0`() =
        runTest {
            val tab =
                mediaTab(
                    metadata = MediaSession.Metadata(title = "A", getArtwork = null),
                    positionState = MediaSession.PositionState(duration = 100.0, position = 0.0),
                )
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()

            ShadowSystemClock.advanceBy(Duration.ofSeconds(60))
            store.dispatch(
                MediaSessionAction.UpdateMediaMetadataAction(
                    tab.id,
                    MediaSession.Metadata(title = "B", getArtwork = null),
                )
            )
            store.dispatch(
                MediaSessionAction.UpdateMediaPositionStateAction(
                    tab.id,
                    MediaSession.PositionState(duration = 100.0, position = 0.0),
                )
            )
            drain()

            assertEquals(0L, player.currentPosition)
        }

    @Test
    fun `GIVEN a playing tab WHEN another tab reporting the same position state takes over THEN its position starts at 0`() =
        runTest {
            val position = MediaSession.PositionState(duration = 100.0, position = 0.0)
            val tabA = mediaTab(url = "https://a.example", positionState = position).copy(id = "tab-a")
            val store = BrowserStore(BrowserState(tabs = listOf(tabA)))
            val player = newPlayer(store)
            drain()

            ShadowSystemClock.advanceBy(Duration.ofSeconds(60))
            val tabB = mediaTab(url = "https://b.example", positionState = position).copy(id = "tab-b")
            store.dispatch(
                TabListAction.AddTabAction(
                    tabB.copy(
                        mediaSessionState =
                            tabB.mediaSessionState!!.copy(timestamp = tabA.mediaSessionState!!.timestamp + 1)
                    )
                )
            )
            drain()

            assertEquals("tab-b", player.currentMediaItem?.mediaId)
            assertEquals(0L, player.currentPosition)
        }

    @Test
    fun `GIVEN playback goes unknown WHEN it resumes with an unchanged position state THEN the gap is not counted`() =
        runTest {
            val tab = mediaTab(positionState = MediaSession.PositionState(duration = 100.0, position = 30.0))
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()

            store.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(tab.id, MediaSession.PlaybackState.UNKNOWN)
            )
            drain()
            ShadowSystemClock.advanceBy(Duration.ofSeconds(10))
            store.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(tab.id, MediaSession.PlaybackState.PLAYING)
            )
            drain()

            assertEquals(30_000L, player.currentPosition)
        }

    @Test
    fun `GIVEN the active media tab changes THEN the new tab's position is reported immediately`() = runTest {
        val tabA =
            mediaTab(
                MediaSession.PlaybackState.PAUSED,
                url = "https://a.example",
                metadata = MediaSession.Metadata(title = "A", getArtwork = null),
                positionState = MediaSession.PositionState(duration = 100.0, position = 30.0),
            )
        val tabB =
            mediaTab(
                url = "https://b.example",
                metadata = MediaSession.Metadata(title = "B", getArtwork = null),
                positionState = MediaSession.PositionState(duration = 200.0, position = 50.0),
            )
        val store = BrowserStore(BrowserState(tabs = listOf(tabA)))
        val player = newPlayer(store)
        drain()
        assertEquals(30_000L, player.currentPosition)

        store.dispatch(TabListAction.AddTabAction(tabB))
        drain()

        assertEquals(tabB.id, player.currentMediaItem?.mediaId)
        assertEquals(50_000L, player.currentPosition)
    }

    @Test
    fun `GIVEN a tab reporting an infinite duration THEN the duration is unset and it is not seekable`() = runTest {
        val tab =
            mediaTab(positionState = MediaSession.PositionState(duration = Double.POSITIVE_INFINITY, position = 30.0))
        val player = newPlayer(BrowserStore(BrowserState(tabs = listOf(tab))))
        drain()

        assertEquals(C.TIME_UNSET, player.duration)
        assertFalse(player.isCurrentMediaItemSeekable)
        assertFalse(player.availableCommands.contains(Player.COMMAND_SEEK_IN_CURRENT_MEDIA_ITEM))
    }

    @Test
    fun `GIVEN a tab reporting a non-finite position and rate THEN position 0 and speed 1 are reported`() = runTest {
        fun tabAt(rate: Double) =
            mediaTab(
                MediaSession.PlaybackState.PAUSED,
                positionState =
                    MediaSession.PositionState(
                        duration = Double.POSITIVE_INFINITY,
                        position = Double.POSITIVE_INFINITY,
                        playbackRate = rate,
                    ),
            )
        val nan = newPlayer(BrowserStore(BrowserState(tabs = listOf(tabAt(Double.NaN)))))
        val negative = newPlayer(BrowserStore(BrowserState(tabs = listOf(tabAt(-1.0)))))
        drain()

        assertEquals(0L, nan.currentPosition)
        assertEquals(1f, nan.playbackParameters.speed)
        assertEquals(1f, negative.playbackParameters.speed)
    }

    @Test
    fun `GIVEN playback goes unknown and resumes with a new title THEN the old position is held back until it refreshes`() =
        runTest {
            val position = MediaSession.PositionState(duration = 100.0, position = 30.0)
            val tab =
                mediaTab(
                    MediaSession.PlaybackState.PAUSED,
                    metadata = MediaSession.Metadata(title = "A", getArtwork = null),
                    positionState = position,
                )
            val store = BrowserStore(BrowserState(tabs = listOf(tab)))
            val player = newPlayer(store)
            drain()
            assertEquals(30_000L, player.currentPosition)

            store.dispatch(
                MediaSessionAction.UpdateMediaPlaybackStateAction(tab.id, MediaSession.PlaybackState.UNKNOWN)
            )
            drain()
            store.dispatch(
                MediaSessionAction.UpdateMediaMetadataAction(
                    tab.id,
                    MediaSession.Metadata(title = "B", getArtwork = null),
                )
            )
            store.dispatch(MediaSessionAction.UpdateMediaPlaybackStateAction(tab.id, MediaSession.PlaybackState.PAUSED))
            drain()
            assertEquals(0L, player.currentPosition)

            store.dispatch(MediaSessionAction.UpdateMediaPositionStateAction(tab.id, position.copy(position = 2.0)))
            drain()
            assertEquals(2_000L, player.currentPosition)
        }

    @Test
    fun `GIVEN only the element metadata carries a duration THEN it is reported and the tab is seekable`() = runTest {
        fun tabWithElementDuration(duration: Double) =
            mediaTab(
                MediaSession.PlaybackState.PAUSED,
                elementMetadata = MediaSession.ElementMetadata(duration = duration),
            )
        val finite = newPlayer(BrowserStore(BrowserState(tabs = listOf(tabWithElementDuration(100.0)))))
        val infinite =
            newPlayer(BrowserStore(BrowserState(tabs = listOf(tabWithElementDuration(Double.POSITIVE_INFINITY)))))
        drain()

        assertEquals(100_000L, finite.duration)
        assertTrue(finite.isCurrentMediaItemSeekable)
        assertEquals(C.TIME_UNSET, infinite.duration)
        assertFalse(infinite.isCurrentMediaItemSeekable)
    }

    private fun kotlinx.coroutines.test.TestScope.newPlayer(
        store: BrowserStore,
        onArtworkChanged: () -> Unit = {},
    ): BrowserStorePlayer {
        val dispatcher = coroutineContext[ContinuationInterceptor] as CoroutineDispatcher
        return BrowserStorePlayer(
            context = testContext,
            store = store,
            mainDispatcher = dispatcher,
            encodingDispatcher = dispatcher,
            looper = Looper.getMainLooper(),
            onArtworkChanged = onArtworkChanged,
        )
    }

    private fun kotlinx.coroutines.test.TestScope.drain() {
        testScheduler.advanceUntilIdle()
        shadowOf(Looper.getMainLooper()).idle()
    }

    private fun mediaTab(
        playbackState: MediaSession.PlaybackState = MediaSession.PlaybackState.PLAYING,
        controller: MediaSession.Controller = mock(),
        url: String = "https://www.mozilla.org",
        title: String = "",
        private: Boolean = false,
        metadata: MediaSession.Metadata? = null,
        elementMetadata: MediaSession.ElementMetadata? = null,
        features: MediaSession.Feature = MediaSession.Feature(),
        positionState: MediaSession.PositionState = MediaSession.PositionState(),
    ) =
        createTab(
            url = url,
            title = title,
            private = private,
            mediaSessionState =
                MediaSessionState(
                    controller = controller,
                    playbackState = playbackState,
                    metadata = metadata,
                    elementMetadata = elementMetadata,
                    features = features,
                    positionState = positionState,
                ),
        )
}
