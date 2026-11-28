/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import android.app.Activity
import android.content.Context
import android.graphics.Bitmap
import android.net.Uri
import android.service.chooser.ChooserAction
import com.google.zxing.WriterException
import io.mockk.every
import io.mockk.just
import io.mockk.mockk
import io.mockk.runs
import io.mockk.slot
import io.mockk.verify
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.engine.prompt.ShareData
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.menu.share.QRCodeGenerator
import org.mozilla.fenix.components.share.CacheHelper
import org.mozilla.fenix.components.share.DefaultShareSheetLauncher
import org.mozilla.fenix.components.share.ShareDelegate
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
class ShareSheetLauncherTest {

    private val mockContext = mockk<Context>(relaxed = true)
    private val mockShareDelegate: ShareDelegate =
        mockk(relaxed = true) {
            every { share(any(), any()) } just runs
            every { shareWithChooserActions(any(), any(), any()) } just runs
        }

    private val mockCacheHelper =
        mockk<CacheHelper> {
            every { saveBitmapToCache(any(), any(), any()) } returns Uri.parse("content://cacheDir/qr_code.png")
        }
    private val mockQRCodeGenerator =
        mockk<QRCodeGenerator> {
            every { generateQRCodeImage(any(), any(), any(), any()) } returns mockk<Bitmap>()
        }
    private val mockCrashReporter = mockk<CrashReporting>(relaxed = true)

    @OptIn(kotlinx.coroutines.ExperimentalCoroutinesApi::class) private val testDispatcher = UnconfinedTestDispatcher()

    private val launcher =
        DefaultShareSheetLauncher(
            applicationContext = mockContext,
            qrCodeGenerator = mockQRCodeGenerator,
            cacheHelper = mockCacheHelper,
            scope = CoroutineScope(testDispatcher),
            ioDispatcher = testDispatcher,
            homeActivityClass = Activity::class.java,
            shareDelegate = mockShareDelegate,
            crashReporter = mockCrashReporter,
        )

    @Config(sdk = [33])
    @Test
    fun `WHEN native share sheet triggered on older API THEN share is invoked`() {
        launcher.showSystemShareSheet(
            id = "123",
            url = "https://www.mozilla.org",
            title = "Mozilla",
            isCustomTab = false,
        )

        verify { mockShareDelegate.share(any(), any()) }
    }

    @Config(sdk = [33])
    @Test
    fun `GIVEN API level below 34 WHEN native share sheet triggered THEN basic share is used`() {
        launcher.showSystemShareSheet(
            id = "123",
            url = "https://www.mozilla.org",
            title = "Mozilla",
        )

        verify { mockShareDelegate.share(any(), any()) }
        verify(exactly = 0) { mockShareDelegate.shareWithChooserActions(any(), any(), any()) }
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN API level 34 and valid tab id WHEN native share sheet triggered THEN chooser actions share is used`() {
        launcher.showSystemShareSheet(
            id = "123",
            url = "https://www.mozilla.org",
            title = "Mozilla",
        )

        verify { mockShareDelegate.shareWithChooserActions(any(), any(), any()) }
        verify(exactly = 0) { mockShareDelegate.share(any(), any()) }
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN API level 34 and null tab id WHEN native share sheet triggered THEN basic share is used`() {
        launcher.showSystemShareSheet(
            id = null,
            url = "https://www.mozilla.org",
            title = "Mozilla",
        )

        verify { mockShareDelegate.share(any(), any()) }
        verify(exactly = 0) { mockShareDelegate.shareWithChooserActions(any(), any(), any()) }
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN text and subject WHEN single url share is triggered THEN share is invoked with combined text and subject`() {
        launcher.showSystemShareSheet(
            id = null,
            url = "https://www.mozilla.org",
            title = "Mozilla",
            text = "Check this out",
            subject = "A subject",
        )

        verify {
            mockShareDelegate.share(
                text = "Check this out\nhttps://www.mozilla.org",
                subject = "A subject",
            )
        }
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN a subject but no text WHEN single url share is triggered THEN share uses the url as text and subject over title`() {
        launcher.showSystemShareSheet(
            id = null,
            url = "https://www.mozilla.org",
            title = "Mozilla",
            subject = "A subject",
        )

        verify {
            mockShareDelegate.share(
                text = "https://www.mozilla.org",
                subject = "A subject",
            )
        }
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN text and subject and a valid tab id WHEN native share sheet triggered THEN chooser actions share receives combined text and subject`() {
        launcher.showSystemShareSheet(
            id = "123",
            url = "https://www.mozilla.org",
            title = "Mozilla",
            text = "Check this out",
            subject = "A subject",
        )

        verify {
            mockShareDelegate.shareWithChooserActions(
                text = "Check this out\nhttps://www.mozilla.org",
                subject = "A subject",
                actions = any(),
            )
        }
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN an empty subject but a title WHEN single url share is triggered THEN share falls back to the title`() {
        launcher.showSystemShareSheet(
            id = null,
            url = "https://www.mozilla.org",
            title = "Mozilla",
            subject = "",
        )

        verify {
            mockShareDelegate.share(
                text = "https://www.mozilla.org",
                subject = "Mozilla",
            )
        }
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN a private tab WHEN native share sheet triggered THEN chooser actions share is still used`() {
        launcher.showSystemShareSheet(
            id = "123",
            url = "https://www.mozilla.org",
            title = "Mozilla",
            isPrivate = true,
        )

        verify { mockShareDelegate.shareWithChooserActions(any(), any(), any()) }
        verify(exactly = 0) { mockShareDelegate.share(any(), any()) }
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN API 34 and valid id WHEN native share sheet triggered THEN four chooser actions are passed`() {
        val actionsSlot = slot<Array<ChooserAction>>()
        every { mockShareDelegate.shareWithChooserActions(any(), any(), capture(actionsSlot)) } just runs

        launcher.showSystemShareSheet(
            id = "123",
            url = "https://www.mozilla.org",
            title = "Mozilla",
        )

        assertEquals(4, actionsSlot.captured.size)
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN QR code generation fails WHEN native share sheet triggered THEN remaining 3 chooser actions are still passed`() {
        every { mockQRCodeGenerator.generateQRCodeImage(any(), any(), any(), any()) } throws
            WriterException("Data too big")
        val actionsSlot = slot<Array<ChooserAction>>()
        every { mockShareDelegate.shareWithChooserActions(any(), any(), capture(actionsSlot)) } just runs

        launcher.showSystemShareSheet(
            id = "123",
            url = "https://www.mozilla.org",
            title = "Mozilla",
        )

        verify { mockShareDelegate.shareWithChooserActions(any(), any(), any()) }
        assertEquals(3, actionsSlot.captured.size)
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN QR code generation fails WHEN native share sheet triggered THEN the exception is reported`() {
        val exception = WriterException("Data too big")
        every { mockQRCodeGenerator.generateQRCodeImage(any(), any(), any(), any()) } throws exception

        launcher.showSystemShareSheet(
            id = "123",
            url = "https://www.mozilla.org",
            title = "Mozilla",
        )

        verify { mockCrashReporter.recordCrashBreadcrumb(any<Breadcrumb>()) }
        verify { mockCrashReporter.submitCaughtException(exception) }
    }

    @Config(sdk = [34])
    @Test
    fun `WHEN showSystemShareSheet is called with multiple items THEN chooser actions share is invoked with numbered urls joined by newlines`() {
        val items =
            listOf(
                ShareData(url = "https://mozilla.org", title = "Mozilla", private = false),
                ShareData(url = "https://firefox.com", title = "Firefox", private = false),
            )

        launcher.showSystemShareSheet(items = items)

        verify {
            mockShareDelegate.shareWithChooserActions(
                text = "1. https://mozilla.org\n2. https://firefox.com",
                subject = "Mozilla",
                actions = any(),
            )
        }
    }

    @Config(sdk = [34])
    @Test
    fun `WHEN showSystemShareSheet is called with multiple items THEN only the send-to-devices chooser action is passed`() {
        val actionsSlot = slot<Array<ChooserAction>>()
        every { mockShareDelegate.shareWithChooserActions(any(), any(), capture(actionsSlot)) } just runs
        val items =
            listOf(
                ShareData(url = "https://mozilla.org", title = "Mozilla", private = false),
                ShareData(url = "https://firefox.com", title = "Firefox", private = false),
            )

        launcher.showSystemShareSheet(items = items)

        assertEquals(1, actionsSlot.captured.size)
    }

    @Config(sdk = [34])
    @Test
    fun `WHEN showSystemShareSheet is called with a single item THEN chooser actions share is invoked with that url`() {
        val items = listOf(ShareData(url = "https://mozilla.org", title = "Mozilla", private = false))

        launcher.showSystemShareSheet(items = items)

        verify {
            mockShareDelegate.shareWithChooserActions(
                text = "https://mozilla.org",
                subject = "Mozilla",
                actions = any(),
            )
        }
    }

    @Config(sdk = [34])
    @Test
    fun `GIVEN text WHEN showSystemShareSheet is called with a single item THEN chooser actions share is invoked with the text and url`() {
        val items = listOf(ShareData(url = "https://mozilla.org", title = "Mozilla", private = false))

        launcher.showSystemShareSheet(items = items, text = "Check this out")

        verify {
            mockShareDelegate.shareWithChooserActions(
                text = "Check this out\nhttps://mozilla.org",
                subject = "Mozilla",
                actions = any(),
            )
        }
    }

    @Config(sdk = [34])
    @Test
    fun `WHEN showSystemShareSheet is called with items containing null urls THEN null urls are excluded from share text`() {
        val items =
            listOf(
                ShareData(url = "https://mozilla.org", title = "Mozilla", private = false),
                ShareData(url = null, title = "No URL", private = false),
            )

        launcher.showSystemShareSheet(items = items)

        verify {
            mockShareDelegate.shareWithChooserActions(
                text = "https://mozilla.org",
                subject = "Mozilla",
                actions = any(),
            )
        }
    }

    @Config(sdk = [34])
    @Test
    fun `WHEN showSystemShareSheet is called with empty items THEN share is invoked with empty text`() {
        launcher.showSystemShareSheet(items = emptyList())

        verify { mockShareDelegate.shareWithChooserActions(text = "", subject = "", actions = any()) }
    }

    @Config(sdk = [34])
    @Test
    fun `WHEN showSystemShareSheet is called with multiple items and a subject THEN chooser actions share is invoked with the subject`() {
        val items =
            listOf(
                ShareData(url = "https://mozilla.org", title = "Mozilla", private = false),
                ShareData(url = "https://firefox.com", title = "Firefox", private = false),
            )

        launcher.showSystemShareSheet(items = items, subject = "My collection")

        verify {
            mockShareDelegate.shareWithChooserActions(
                text = "1. https://mozilla.org\n2. https://firefox.com",
                subject = "My collection",
                actions = any(),
            )
        }
    }

    @Config(sdk = [33])
    @Test
    fun `GIVEN API level below 34 WHEN showSystemShareSheet is called with multiple items THEN basic share is used`() {
        val items =
            listOf(
                ShareData(url = "https://mozilla.org", title = "Mozilla", private = false),
                ShareData(url = "https://firefox.com", title = "Firefox", private = false),
            )

        launcher.showSystemShareSheet(items = items)

        verify {
            mockShareDelegate.share(
                text = "1. https://mozilla.org\n2. https://firefox.com",
                subject = "Mozilla",
            )
        }
        verify(exactly = 0) { mockShareDelegate.shareWithChooserActions(any(), any(), any()) }
    }
}
