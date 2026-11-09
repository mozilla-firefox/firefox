/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.downloads.temporary

import android.content.Context
import androidx.test.ext.junit.runners.AndroidJUnit4
import java.io.File
import java.nio.charset.StandardCharsets
import kotlin.test.assertIs
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.TimeoutCancellationException
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.runTest
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ShareResourceAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.state.content.ShareResourceState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Headers.Names.CONTENT_DISPOSITION
import mozilla.components.concept.fetch.Headers.Names.CONTENT_TYPE
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Request
import mozilla.components.concept.fetch.Response
import mozilla.components.feature.downloads.facts.ShareResourceFacts
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.processor.CollectionProcessor
import mozilla.components.support.test.any
import mozilla.components.support.test.argumentCaptor
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import org.junit.runner.RunWith
import org.mockito.Mockito.doAnswer
import org.mockito.Mockito.doNothing
import org.mockito.Mockito.doReturn
import org.mockito.Mockito.doThrow
import org.mockito.Mockito.never
import org.mockito.Mockito.spy
import org.mockito.Mockito.verify

/** The 89a gif header as seen on https://www.w3.org/Graphics/GIF/spec-gif89a.txt */
private const val GIF_HEADER = "GIF89a"

@RunWith(AndroidJUnit4::class)
class ShareResourceFeatureTest {

    @get:Rule val temporaryFolder = TemporaryFolder()

    private lateinit var context: Context
    private val testDispatcher = StandardTestDispatcher()

    @Before
    fun setup() {
        context = spy(testContext)
        doReturn(temporaryFolder.root).`when`(context).cacheDir
    }

    @Test
    fun `cleanupCache should automatically be called when this class is initialized`() =
        runTest(testDispatcher) {
            val cacheDir =
                File(temporaryFolder.root, cacheDirName).apply {
                    mkdirs()
                    File(this, "leftoverFile").createNewFile()
                }

            assertTrue(cacheDir.listFiles()?.isNotEmpty() == true)

            createFeature()
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(cacheDir.listFiles()?.isEmpty() == true)
        }

    @Test
    fun `ShareFeature starts the share process for AddShareAction which is immediately consumed`() {
        val captureActionsMiddleware = CaptureActionsMiddleware<BrowserState, BrowserAction>()
        val store =
            BrowserStore(
                BrowserState(
                    tabs =
                        listOf(
                            TabSessionState(
                                "123",
                                ContentState(url = "https://www.mozilla.org"),
                            )
                        )
                ),
                middleware = listOf(captureActionsMiddleware),
            )

        val shareFeature = spy(createFeature(store = store, tabId = "123"))
        doNothing().`when`(shareFeature).startSharing(any())
        val download = ShareResourceState.InternetResource(url = "testDownload")
        val action = ShareResourceAction.AddShareAction("123", download)
        shareFeature.start()
        testDispatcher.scheduler.advanceUntilIdle()

        store.dispatch(action)
        testDispatcher.scheduler.advanceUntilIdle()

        verify(shareFeature).startSharing(download)
        captureActionsMiddleware.assertFirstAction(ShareResourceAction.ConsumeShareAction::class) { action ->
            assertEquals("123", action.tabId)
        }
    }

    @Test
    fun `cleanupCache should delete all files from the cache directory`() =
        runTest(testDispatcher) {
            val shareFeature = createFeature()

            // Clear the 'init' block's coroutine from the queue
            testDispatcher.scheduler.runCurrent()

            val testDir = temporaryFolder.newFolder(cacheDirName)
            File(testDir, "testFile").createNewFile()

            assertTrue(
                "File should exist before cleanup",
                testDir.listFiles()?.isNotEmpty() == true,
            )

            shareFeature.cleanupCache()

            assertTrue(
                "Directory should be empty after manual cleanup",
                testDir.listFiles()?.isEmpty() == true,
            )
        }

    @Test
    fun `startSharing() will download and then share the selected download`() =
        runTest(testDispatcher) {
            val shareFeature = spy(createFeature())
            val shareState = ShareResourceState.InternetResource(url = "testUrl", contentType = "contentType")
            val downloadedFile = File("filePath")
            doReturn(downloadedFile).`when`(shareFeature).download(any())
            doReturn(true).`when`(shareFeature).shareInternetResource(any(), any(), any(), any())

            shareFeature.scope = this

            CollectionProcessor.withFactCollection { facts ->
                shareFeature.startSharing(shareState)
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(
                    ShareResourceFacts.ShareType.INTERNET,
                    facts.single().metadata?.get(ShareResourceFacts.MetadataKeys.SHARE_TYPE),
                )
            }

            verify(shareFeature).download(shareState)
            verify(shareFeature).shareInternetResource(downloadedFile.canonicalPath, "contentType", null, null)
            verify(shareFeature, never()).shareLocalPdf(any(), any())
        }

    @Test
    fun `startSharing() will persist and share a direct resource without downloading`() =
        runTest(testDispatcher) {
            val shareFeature = spy(createFeature())
            val response = mock<Response>()
            val shareState =
                ShareResourceState.DirectResource(url = "testUrl", contentType = "contentType", response = response)
            val downloadedFile = File("filePath")
            doReturn(downloadedFile).`when`(shareFeature).downloadDirectResource(any())
            doReturn(true).`when`(shareFeature).shareInternetResource(any(), any(), any(), any())

            shareFeature.scope = this

            CollectionProcessor.withFactCollection { facts ->
                shareFeature.startSharing(shareState)
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(
                    ShareResourceFacts.ShareType.DIRECT,
                    facts.single().metadata?.get(ShareResourceFacts.MetadataKeys.SHARE_TYPE),
                )
            }

            verify(shareFeature).downloadDirectResource(response)
            verify(shareFeature).shareInternetResource(downloadedFile.canonicalPath, "contentType", null, null)
            verify(shareFeature, never()).shareLocalPdf(any(), any())
        }

    @Test
    fun `startSharing() will share the local PDF without downloading`() =
        runTest(testDispatcher) {
            val shareFeature = spy(createFeature())
            val shareState = ShareResourceState.LocalResource(url = "content://pdf.pdf", contentType = "contentType")
            shareFeature.scope = this

            CollectionProcessor.withFactCollection { facts ->
                shareFeature.startSharing(shareState)
                testDispatcher.scheduler.advanceUntilIdle()

                assertEquals(
                    ShareResourceFacts.ShareType.LOCAL,
                    facts.single().metadata?.get(ShareResourceFacts.MetadataKeys.SHARE_TYPE),
                )
            }

            verify(shareFeature, never()).download(any())
            verify(shareFeature).shareLocalPdf("content://pdf.pdf", "contentType")
        }

    @Test
    fun `startSharing() emits a share duration fact with the share type once sharing has finished`() =
        runTest(testDispatcher) {
            CollectionProcessor.withFactCollection { facts ->
                val shareFeature = spy(createFeature())
                doReturn(true).`when`(shareFeature).shareLocalPdf(any(), any())
                shareFeature.scope = this

                shareFeature.startSharing(
                    ShareResourceState.LocalResource(url = "content://pdf.pdf", contentType = "application/pdf")
                )
                testDispatcher.scheduler.advanceUntilIdle()

                with(facts.single()) {
                    assertEquals(Component.FEATURE_DOWNLOADS, component)
                    assertEquals(ShareResourceFacts.Items.SHARE_DURATION, item)
                    assertEquals(
                        ShareResourceFacts.ShareType.LOCAL,
                        metadata?.get(ShareResourceFacts.MetadataKeys.SHARE_TYPE),
                    )
                    assertIs<Long>(metadata?.get(ShareResourceFacts.MetadataKeys.DURATION_MS))
                    assertEquals(
                        ShareResourceFacts.ShareContentType.PDF,
                        metadata?.get(ShareResourceFacts.MetadataKeys.CONTENT_TYPE),
                    )
                }
            }
        }

    @Test
    fun `startSharing() emits a share failed fact with the timeout reason when sharing times out`() =
        runTest(testDispatcher) {
            val shareFeature = spy(createFeature())
            doThrow(mock<TimeoutCancellationException>()).`when`(shareFeature).downloadDirectResource(any())
            shareFeature.scope = this

            CollectionProcessor.withFactCollection { facts ->
                shareFeature.startSharing(
                    ShareResourceState.DirectResource(
                        url = "testUrl",
                        contentType = "application/pdf",
                        response = mock(),
                    )
                )
                testDispatcher.scheduler.advanceUntilIdle()

                with(facts.single()) {
                    assertEquals(Component.FEATURE_DOWNLOADS, component)
                    assertEquals(ShareResourceFacts.Items.SHARE_FAILED, item)
                    assertEquals(
                        ShareResourceFacts.FailureReason.TIMEOUT,
                        metadata?.get(ShareResourceFacts.MetadataKeys.FAILURE_REASON),
                    )
                    assertEquals(
                        ShareResourceFacts.ShareType.DIRECT,
                        metadata?.get(ShareResourceFacts.MetadataKeys.SHARE_TYPE),
                    )
                    assertEquals(
                        ShareResourceFacts.ShareContentType.PDF,
                        metadata?.get(ShareResourceFacts.MetadataKeys.CONTENT_TYPE),
                    )
                }
            }

            verify(shareFeature, never()).shareInternetResource(any(), any(), any(), any())
        }

    @Test
    fun `startSharing() emits a share failed fact with the cancelled reason when the feature is stopped while sharing`() =
        runTest(testDispatcher) {
            val shareFeature = spy(createFeature())
            doAnswer {
                    shareFeature.stop()
                    true
                }
                .`when`(shareFeature)
                .shareLocalPdf(any(), any())
            shareFeature.scope = CoroutineScope(testDispatcher)

            CollectionProcessor.withFactCollection { facts ->
                shareFeature.startSharing(
                    ShareResourceState.LocalResource(url = "content://pdf.pdf", contentType = "application/pdf")
                )
                testDispatcher.scheduler.advanceUntilIdle()

                with(facts.single()) {
                    assertEquals(ShareResourceFacts.Items.SHARE_FAILED, item)
                    assertEquals(
                        ShareResourceFacts.FailureReason.CANCELLED,
                        metadata?.get(ShareResourceFacts.MetadataKeys.FAILURE_REASON),
                    )
                }
            }
        }

    @Test
    fun `startSharing() emits a share failed fact with the error reason when sharing fails`() =
        runTest(testDispatcher) {
            val shareFeature = spy(createFeature())
            doThrow(RuntimeException("Resource is not available to download")).`when`(shareFeature).download(any())
            shareFeature.scope = CoroutineScope(testDispatcher)

            CollectionProcessor.withFactCollection { facts ->
                shareFeature.startSharing(ShareResourceState.InternetResource(url = "testUrl"))
                testDispatcher.scheduler.advanceUntilIdle()

                with(facts.single()) {
                    assertEquals(ShareResourceFacts.Items.SHARE_FAILED, item)
                    assertEquals(
                        ShareResourceFacts.FailureReason.ERROR,
                        metadata?.get(ShareResourceFacts.MetadataKeys.FAILURE_REASON),
                    )
                    assertEquals(
                        ShareResourceFacts.ShareType.INTERNET,
                        metadata?.get(ShareResourceFacts.MetadataKeys.SHARE_TYPE),
                    )
                    assertEquals(
                        ShareResourceFacts.ShareContentType.OTHER,
                        metadata?.get(ShareResourceFacts.MetadataKeys.CONTENT_TYPE),
                    )
                }
            }
        }

    @Test
    fun `download() will persist in cache the response#body() if available`() =
        runTest(testDispatcher) {
            val shareFeature = createFeature()
            val inputStream = "test".byteInputStream(StandardCharsets.UTF_8)
            val responseFromShareState =
                mock<Response>().apply {
                    doReturn(Response.Body(inputStream)).`when`(this).body
                    doReturn(Response.SUCCESS).`when`(this).status
                    doReturn(MutableHeaders()).`when`(this).headers
                }
            val shareState = ShareResourceState.InternetResource("randomUrl.jpg", response = responseFromShareState)

            val result = shareFeature.download(shareState)

            assertTrue(result.exists())
            assertTrue(result.name.endsWith(".$DEFAULT_IMAGE_EXTENSION"))
            assertEquals(cacheDirName, result.parentFile?.name)
            assertEquals("test", result.inputStream().bufferedReader().use { it.readText() })
        }

    @Test
    fun `downloadDirectResource() will name the file after its Content-Disposition`() =
        runTest(testDispatcher) {
            val shareFeature = createFeature()
            val response = createPdfResponse(MutableHeaders(CONTENT_DISPOSITION to "attachment; filename=Test.pdf;"))

            val result = shareFeature.downloadDirectResource(response)

            assertEquals("Test.pdf", result.name)
            assertEquals(cacheDirName, result.parentFile?.parentFile?.name)
            assertEquals("%PDF-1.4", result.inputStream().bufferedReader().use { it.readText() })
        }

    @Test
    fun `downloadDirectResource() will use a random name without a Content-Disposition`() =
        runTest(testDispatcher) {
            val shareFeature = createFeature()
            val response = createPdfResponse(MutableHeaders(CONTENT_TYPE to "application/pdf"))

            val result = shareFeature.downloadDirectResource(response)

            assertTrue(result.name.matches(Regex("\\d+\\.pdf")))
            assertEquals(cacheDirName, result.parentFile?.name)
        }

    @Test
    fun `downloadDirectResource() will use a random name when the Content-Disposition name is empty`() =
        runTest(testDispatcher) {
            val shareFeature = createFeature()
            val response =
                createPdfResponse(
                    MutableHeaders(
                        CONTENT_TYPE to "application/pdf",
                        CONTENT_DISPOSITION to "attachment; filename=\" \"",
                    )
                )

            val result = shareFeature.downloadDirectResource(response)

            assertTrue(result.name.matches(Regex("\\d+\\.pdf")))
            assertEquals("%PDF-1.4", result.inputStream().bufferedReader().use { it.readText() })
        }

    @Test
    fun `downloadDirectResource() will use a random name when the Content-Disposition name is too long to save`() =
        runTest(testDispatcher) {
            val shareFeature = createFeature()
            val longName = "a".repeat(300) + ".pdf"
            val response =
                createPdfResponse(
                    MutableHeaders(
                        CONTENT_TYPE to "application/pdf",
                        CONTENT_DISPOSITION to "attachment; filename=$longName;",
                    )
                )

            val result = shareFeature.downloadDirectResource(response)

            assertTrue(result.name.matches(Regex("\\d+\\.pdf")))
            assertEquals("%PDF-1.4", result.inputStream().bufferedReader().use { it.readText() })
            assertTrue(
                "No empty folder is left behind.",
                shareFeature.getMediaShareCacheDirectory().listFiles().orEmpty().none { it.isDirectory },
            )
        }

    private fun createPdfResponse(headers: MutableHeaders) =
        mock<Response>().apply {
            doReturn(Response.Body("%PDF-1.4".byteInputStream(StandardCharsets.UTF_8))).`when`(this).body
            doReturn(Response.SUCCESS).`when`(this).status
            doReturn(headers).`when`(this).headers
        }

    @Test(expected = RuntimeException::class)
    fun `download() will throw an error if the request is not successful`() =
        runTest(testDispatcher) {
            val shareFeature = createFeature()
            val responseFromShareState =
                mock<Response>().apply {
                    doReturn(Response.Body("test".byteInputStream())).`when`(this).body
                    doReturn(500).`when`(this).status
                }
            val shareState = ShareResourceState.InternetResource("randomUrl.jpg", response = responseFromShareState)

            shareFeature.download(shareState)
        }

    @Test
    fun `download() will download from the provided url the response#body() if is unavailable`() =
        runTest(testDispatcher) {
            val client: Client = mock()
            val inputStream = "clientTest".byteInputStream(StandardCharsets.UTF_8)
            doAnswer { Response("randomUrl", 200, MutableHeaders(), Response.Body(inputStream)) }
                .`when`(client)
                .fetch(any())
            val shareFeature = createFeature(client = client)
            val shareState = ShareResourceState.InternetResource("randomUrl")

            val result = shareFeature.download(shareState)

            assertTrue(result.exists())
            assertTrue(result.name.endsWith(".$DEFAULT_IMAGE_EXTENSION"))
            assertEquals(cacheDirName, result.parentFile?.name)
            assertEquals("clientTest", result.inputStream().bufferedReader().use { it.readText() })
        }

    @Test
    fun `download() will create a not private Request if not in private mode`() =
        runTest(testDispatcher) {
            val client: Client = mock()
            val requestCaptor = argumentCaptor<Request>()
            val inputStream = "clientTest".byteInputStream(StandardCharsets.UTF_8)
            doAnswer { Response("randomUrl.png", 200, MutableHeaders(), Response.Body(inputStream)) }
                .`when`(client)
                .fetch(requestCaptor.capture())
            val shareFeature = createFeature(client = client)
            val shareState = ShareResourceState.InternetResource("randomUrl.png", private = false)

            shareFeature.download(shareState)
            testDispatcher.scheduler.advanceUntilIdle()

            assertFalse(requestCaptor.value.private)
        }

    @Test
    fun `download() will create a private Request if in private mode`() =
        runTest(testDispatcher) {
            val client: Client = mock()
            val requestCaptor = argumentCaptor<Request>()
            val inputStream = "clientTest".byteInputStream(StandardCharsets.UTF_8)
            doAnswer { Response("randomUrl.png", 200, MutableHeaders(), Response.Body(inputStream)) }
                .`when`(client)
                .fetch(requestCaptor.capture())
            val shareFeature = createFeature(client = client)
            val shareState = ShareResourceState.InternetResource("randomUrl.png", private = true)

            shareFeature.download(shareState)
            testDispatcher.scheduler.advanceUntilIdle()

            assertTrue(requestCaptor.value.private)
        }

    @Test
    fun `getFilename(extension) will return a String with the extension suffix`() {
        val shareFeature = createFeature()
        val testExtension = "testExtension"

        val result = shareFeature.getFilename(testExtension)

        assertTrue(result.endsWith(testExtension))
        assertTrue(result.length > testExtension.length)
    }

    @Test
    fun `getTempFile(extension) will return a File from the cache dir and with name ending in extension`() {
        val shareFeature = spy(createFeature())
        val testExtension = "testExtension"

        val result = shareFeature.getTempFile(testExtension)

        assertTrue(result.name.endsWith(testExtension))
        assertEquals(shareFeature.getCacheDirectory().absolutePath, result.parentFile?.absolutePath)
    }

    @Test
    fun `getCacheDirectory() will return a new directory in the app's cache`() {
        val shareFeature = createFeature()

        val result = shareFeature.getCacheDirectory()

        assertEquals(temporaryFolder.root.absolutePath, result.parentFile?.absolutePath)
        assertEquals(cacheDirName, result.name)
    }

    @Test
    fun `getMediaShareCacheDirectory creates the needed files if they don't exist`() {
        val shareFeature = spy(createFeature())
        val expectedDir = File(temporaryFolder.root, cacheDirName)

        expectedDir.deleteRecursively()
        assertFalse(expectedDir.exists())

        val result = shareFeature.getMediaShareCacheDirectory()

        assertEquals(expectedDir.absolutePath, result.absolutePath)
        assertTrue(result.exists())
    }

    @Test
    fun `getFileExtension returns a default extension if one cannot be extracted`() {
        val shareFeature = createFeature()

        val result = shareFeature.getFileExtension(mock(), mock())

        assertEquals(DEFAULT_IMAGE_EXTENSION, result)
    }

    @Test
    fun `getFileExtension returns an extension based on the media type inferred from the stream`() {
        val shareFeature = createFeature()
        val gifStream = (GIF_HEADER + "testImage").byteInputStream(StandardCharsets.UTF_8)
        // Add the gif mapping to a by default empty shadow of MimeTypeMap.

        val result = shareFeature.getFileExtension(mock(), gifStream)

        assertEquals("gif", result)
    }

    @Test
    fun `getFileExtension returns an extension based on the response headers`() {
        val shareFeature = createFeature()
        val gifHeaders =
            MutableHeaders().apply {
                set(CONTENT_TYPE, "image/gif")
            }
        // Add the gif mapping to a by default empty shadow of MimeTypeMap.

        val result = shareFeature.getFileExtension(gifHeaders, mock())

        assertEquals("gif", result)
    }

    private fun createFeature(
        client: Client = mock(),
        store: BrowserStore = BrowserStore(),
        tabId: String? = null,
    ): ShareResourceFeature {
        return ShareResourceFeature(
            context,
            store,
            tabId,
            client,
            testDispatcher,
            testDispatcher,
        )
    }
}
