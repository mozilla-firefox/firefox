/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

package org.mozilla.geckoview.test

import android.graphics.Bitmap
import android.graphics.Color
import android.graphics.Color.rgb
import android.graphics.pdf.PdfRenderer
import android.os.ParcelFileDescriptor
import androidx.core.graphics.createBitmap
import androidx.core.graphics.get
import androidx.core.graphics.scale
import androidx.test.ext.junit.rules.ActivityScenarioRule
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.filters.LargeTest
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.io.InputStream
import kotlin.math.roundToInt
import org.hamcrest.Matchers.equalTo
import org.junit.After
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.RuleChain
import org.junit.runner.RunWith
import org.mozilla.geckoview.Autofill
import org.mozilla.geckoview.GeckoSessionSettings
import org.mozilla.geckoview.GeckoViewPrintDocumentAdapter
import org.mozilla.geckoview.test.rule.GeckoSessionTestRule.NullDelegate

@RunWith(AndroidJUnit4::class)
@LargeTest
class PdfCreationTest : BaseSessionTest() {
    private val activityRule = ActivityScenarioRule(GeckoViewTestActivity::class.java)
    var deviceHeight = 0
    var deviceWidth = 0
    var scaledHeight = 0
    var scaledWidth = 12
    private val uiAutomation = InstrumentationRegistry.getInstrumentation().uiAutomation

    @get:Rule override val rules: RuleChain = RuleChain.outerRule(activityRule).around(sessionRule)

    @Before
    fun setup() {
        activityRule.scenario.onActivity {
            it.view.setSession(mainSession)
            deviceHeight = it.resources.displayMetrics.heightPixels
            deviceWidth = it.resources.displayMetrics.widthPixels
            scaledHeight = (scaledWidth * (deviceHeight / deviceWidth.toDouble())).roundToInt()
        }
    }

    @After
    fun cleanup() {
        activityRule.scenario.onActivity {
            it.view.releaseSession()
        }
    }

    private fun createFileDescriptor(pdfInputStream: InputStream): ParcelFileDescriptor {
        val file = File.createTempFile("temp", null)
        pdfInputStream.use { input ->
            file.outputStream().use { output ->
                input.copyTo(output)
            }
        }
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)
    }

    private fun pdfToBitmap(pdfInputStream: InputStream): ArrayList<Bitmap>? {
        val bitmaps: ArrayList<Bitmap> = ArrayList()
        try {
            val pdfRenderer = PdfRenderer(createFileDescriptor(pdfInputStream))
            for (pageNo in 0 until pdfRenderer.pageCount) {
                val page = pdfRenderer.openPage(pageNo)
                var bitmap = createBitmap(deviceWidth, deviceHeight, Bitmap.Config.ARGB_8888)
                page.render(bitmap, null, null, PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY)
                bitmaps.add(bitmap)
                page.close()
            }
            pdfRenderer.close()
        } catch (e: Exception) {
            e.printStackTrace()
        }
        return bitmaps
    }

    @NullDelegate(Autofill.Delegate::class)
    @Test
    fun singleColorPdf() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(COLOR_ORANGE_BACKGROUND_HTML_PATH)
            mainSession.waitForPageStop()
            val pdfInputStream = mainSession.saveAsPdf()
            sessionRule.waitForResult(pdfInputStream).let {
                val bitmap = pdfToBitmap(it)!![0]
                val scaled = bitmap.scale(scaledWidth, scaledHeight, filter = false)
                val centerPixel = scaled[scaledWidth / 2, scaledHeight / 2]
                val orange = rgb(255, 113, 57)
                assertTrue("The PDF orange color matches.", centerPixel == orange)
            }
        }
    }

    @NullDelegate(Autofill.Delegate::class)
    @Test
    fun ftFontPdf() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(FT_FONT_HTML_PATH)
            mainSession.waitForPageStop()
            val pdfInputStream = mainSession.saveAsPdf()
            sessionRule.waitForResult(pdfInputStream).let {
                val bitmap = pdfToBitmap(it)!![0]
                val scaled = bitmap.scale(scaledWidth, scaledHeight, filter = false)
                val centerPixel = scaled[scaledWidth / 2, scaledHeight / 2]
                val orange = rgb(255, 113, 57)
                assertTrue("The PDF orange color matches.", centerPixel == orange)
            }
        }
    }

    @NullDelegate(Autofill.Delegate::class)
    @Test
    fun rgbColorsPdf() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(COLOR_GRID_HTML_PATH)
            mainSession.waitForPageStop()
            val pdfInputStream = mainSession.saveAsPdf()
            sessionRule.waitForResult(pdfInputStream).let {
                val bitmap = pdfToBitmap(it)!![0]
                val scaled = bitmap.scale(scaledWidth, scaledHeight, filter = false)
                val redPixel = scaled[2, scaledHeight / 2]
                assertTrue("The PDF red color matches.", redPixel == Color.RED)
                val greenPixel = scaled[scaledWidth / 2, scaledHeight / 2]
                assertTrue("The PDF green color matches.", greenPixel == Color.GREEN)
                val bluePixel = scaled[scaledWidth - 2, scaledHeight / 2]
                assertTrue("The PDF blue color matches.", bluePixel == Color.BLUE)
                val doPixelsMatch = (redPixel == Color.RED && greenPixel == Color.GREEN && bluePixel == Color.BLUE)
                assertTrue("The PDF generated RGB colors.", doPixelsMatch)
            }
        }
    }

    @NullDelegate(Autofill.Delegate::class)
    @Test
    fun makeTempPdfFileTest() {
        activityRule.scenario.onActivity { activity ->
            mainSession.loadTestPath(COLOR_ORANGE_BACKGROUND_HTML_PATH)
            mainSession.waitForPageStop()
            val pdfInputStream = mainSession.saveAsPdf()
            sessionRule.waitForResult(pdfInputStream).let { stream ->
                val file = GeckoViewPrintDocumentAdapter.makeTempPdfFile(stream, activity)!!
                assertTrue("PDF File exists.", file.exists())
                assertTrue("PDF File is not empty.", file.length() > 0L)
                file.delete()
            }
        }
    }

    @NullDelegate(Autofill.Delegate::class)
    @Test
    fun saveAPdfDocument() {
        activityRule.scenario.onActivity {
            mainSession.loadTestPath(HELLO_PDF_WORLD_PDF_PATH)
            mainSession.waitForPageStop()
            val pdfInputStream = mainSession.saveAsPdf()
            val originalBytes = getTestBytes(HELLO_PDF_WORLD_PDF_PATH)
            sessionRule.waitForResult(pdfInputStream).let {
                assertThat("The PDF File must the same as the original one.", it!!.readBytes(), equalTo(originalBytes))
            }
        }
    }

    @NullDelegate(Autofill.Delegate::class)
    @Test
    fun saveAContentPdfDocument() {
        activityRule.scenario.onActivity {
            val originalBytes = getTestBytes(HELLO_PDF_WORLD_PDF_PATH)
            TestContentProvider.setTestData(originalBytes, "application/pdf")
            mainSession.loadUri("content://org.mozilla.geckoview.test.provider/pdf")
            mainSession.waitForPageStop()

            val response = mainSession.pdfFileSaver.save()
            sessionRule.waitForResult(response).let {
                assertThat(
                    "The PDF File must the same as the original one.",
                    it.body?.readBytes(),
                    equalTo(originalBytes),
                )
            }
        }
    }

    @NullDelegate(Autofill.Delegate::class)
    @Test
    fun saveAContentPdfDocumentWithSaveAsPdf() {
        activityRule.scenario.onActivity {
            val originalBytes = getTestBytes(HELLO_PDF_WORLD_PDF_PATH)
            TestContentProvider.setTestData(originalBytes, "application/pdf")
            mainSession.loadUri("content://org.mozilla.geckoview.test.provider/pdf")
            mainSession.waitForPageStop()

            val pdfInputStream = mainSession.saveAsPdf()
            sessionRule.waitForResult(pdfInputStream).let {
                assertThat(
                    "The PDF File must the same as the original one.",
                    it!!.readBytes(),
                    equalTo(originalBytes),
                )
            }
        }
    }

    @NullDelegate(Autofill.Delegate::class)
    @Test
    fun dontTryToOpenNullContent() {
        activityRule.scenario.onActivity {
            TestContentProvider.setNullTestData("application/pdf")
            mainSession.loadUri("content://org.mozilla.geckoview.test.provider/pdf")
            mainSession.waitForPageStop()
        }
    }

    @NullDelegate(Autofill.Delegate::class)
    @Test
    fun testIfPdfIsNotScaledInDesktopMode() {
        activityRule.scenario.onActivity {
            mainSession.settings.userAgentMode = GeckoSessionSettings.USER_AGENT_MODE_DESKTOP
            mainSession.settings.viewportMode = GeckoSessionSettings.VIEWPORT_MODE_DESKTOP

            mainSession.loadTestPath(HELLO_PDF_WORLD_PDF_PATH)
            mainSession.waitForPageStop()
            assertTrue("Is a PDF document", sessionRule.waitForResult(mainSession.isPdfJs))

            val scaledScreenshot = uiAutomation.takeScreenshot().scale(scaledWidth, scaledHeight, filter = false)
            val topHalfPixel = scaledScreenshot[scaledWidth / 2, scaledHeight / 5]
            val toolbarColor = rgb(249, 249, 251)
            assertTrue("The PDF toolbar rendered as the correct size.", topHalfPixel == toolbarColor)
        }
    }
}
