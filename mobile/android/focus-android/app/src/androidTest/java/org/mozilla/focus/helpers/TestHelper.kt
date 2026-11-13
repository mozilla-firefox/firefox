/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
package org.mozilla.focus.helpers

import android.app.PendingIntent
import android.app.UiAutomation
import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.os.Build
import android.os.storage.StorageManager
import android.os.storage.StorageVolume
import android.util.Log
import android.view.KeyEvent
import android.view.inputmethod.InputMethodManager
import androidx.browser.customtabs.CustomTabColorSchemeParams
import androidx.browser.customtabs.CustomTabsIntent
import androidx.browser.customtabs.CustomTabsIntent.SHARE_STATE_ON
import androidx.core.graphics.createBitmap
import androidx.core.net.toUri
import androidx.test.espresso.Espresso
import androidx.test.espresso.Espresso.onView
import androidx.test.espresso.action.ViewActions.click
import androidx.test.espresso.intent.Intents.intended
import androidx.test.espresso.intent.matcher.IntentMatchers.toPackage
import androidx.test.espresso.matcher.ViewMatchers
import androidx.test.espresso.matcher.ViewMatchers.withId
import androidx.test.espresso.matcher.ViewMatchers.withText
import androidx.test.espresso.web.sugar.Web
import androidx.test.filters.SdkSuppress
import androidx.test.platform.app.InstrumentationRegistry.getInstrumentation
import androidx.test.uiautomator.UiDevice
import androidx.test.uiautomator.UiSelector
import java.io.File
import java.io.FileInputStream
import java.io.IOException
import java.io.InputStream
import java.util.concurrent.TimeUnit
import junit.framework.AssertionFailedError
import mozilla.components.support.utils.ext.packageManagerCompatHelper
import okio.Buffer
import org.hamcrest.Matchers
import org.hamcrest.Matchers.allOf
import org.junit.Assert
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.mozilla.focus.R
import org.mozilla.focus.activity.IntentReceiverActivity

object TestHelper {
    @JvmField var mDevice = UiDevice.getInstance(getInstrumentation())
    val waitingTime = TimeUnit.SECONDS.toMillis(15)
    val pageLoadingTime = TimeUnit.SECONDS.toMillis(25)
    val waitingTimeShort: Long = TimeUnit.SECONDS.toMillis(3)

    private val charPool: List<Char> = ('a'..'z') + ('A'..'Z') + ('0'..'9')

    fun randomString(stringLength: Int) =
        (1..stringLength).map { kotlin.random.Random.nextInt(0, charPool.size) }.map(charPool::get).joinToString("")

    @JvmStatic val getTargetContext: Context = getInstrumentation().targetContext

    @JvmStatic val packageName: String = getTargetContext.packageName

    @JvmStatic val appName: String = getTargetContext.getString(R.string.app_name)

    fun getStringResource(id: Int) = getTargetContext.resources.getString(id, appName)

    fun verifySnackBarText(text: String) {
        val snackbarText = mDevice.findObject(UiSelector().textContains(text))
        assertTrue(snackbarText.waitForExists(waitingTime))
    }

    fun clickSnackBarActionButton(action: String) {
        val snackbarActionButton =
            onView(
                allOf(
                    withId(R.id.snackbar_action),
                    withText(action),
                )
            )
        snackbarActionButton.perform(click())
    }

    fun waitUntilSnackBarGone() {
        mDevice.findObject(UiSelector().resourceId("$appName:id/snackbar_layout")).waitUntilGone(waitingTime)
    }

    fun isPackageInstalled(packageName: String): Boolean {
        return try {
            val packageManager = getInstrumentation().context.packageManagerCompatHelper
            packageManager.getApplicationInfoCompat(packageName, 0).enabled
        } catch (exception: PackageManager.NameNotFoundException) {
            Log.d("TestLog", exception.message.toString())
            false
        }
    }

    fun restartApp(activity: MainActivityFirstrunTestRule) {
        with(activity) {
            finishActivity()
            mDevice.waitForIdle()
            launchActivity(null)
        }
    }

    // exit to the main view
    fun exitToTop() {
        val homeScreen = mDevice.findObject(UiSelector().resourceId("$packageName:id/landingLayout"))
        var homeScreenVisible = false
        while (!homeScreenVisible) {
            mDevice.pressBack()
            homeScreenVisible = homeScreen.waitForExists(2000)
        }
    }

    // exit to the browser view
    fun exitToBrowser() {
        val browserScreen = mDevice.findObject(UiSelector().resourceId("$packageName:id/main_content"))
        var browserScreenVisible = false
        while (!browserScreenVisible) {
            mDevice.pressBack()
            browserScreenVisible = browserScreen.waitForExists(2000)
        }
    }

    fun setNetworkEnabled(enabled: Boolean) {
        when (enabled) {
            true -> {
                mDevice.executeShellCommand("svc data enable")
                mDevice.executeShellCommand("svc wifi enable")
            }

            false -> {
                mDevice.executeShellCommand("svc data disable")
                mDevice.executeShellCommand("svc wifi disable")
            }
        }
        mDevice.waitForIdle(waitingTime)
    }

    // verifies localized strings in different UIs
    fun verifyTranslatedTextExists(text: String) =
        assertTrue(mDevice.findObject(UiSelector().text(text)).waitForExists(waitingTime))

    fun openAppFromExternalLink(url: String) {
        val intent =
            Intent().apply {
                action = Intent.ACTION_VIEW
                data = url.toUri()
                `package` = packageName
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
        try {
            getTargetContext.startActivity(intent)
        } catch (ex: ActivityNotFoundException) {
            intent.setPackage(null)
            getTargetContext.startActivity(intent)
        }
    }

    @SdkSuppress(minSdkVersion = Build.VERSION_CODES.R)
    fun verifyDownloadedFileOnStorage(fileName: String) {
        val storageManager =
            getInstrumentation().targetContext.getSystemService(Context.STORAGE_SERVICE) as StorageManager
        val storageVolumes = storageManager.storageVolumes
        val storageVolume: StorageVolume = storageVolumes[0]
        val file = File("${storageVolume.directory!!.path}/Download/$fileName")
        assertTrue(file.exists())
    }

    // Method for granting app permission to access location/camera/mic
    fun grantAppPermission() {
        val permissionOption =
            mDevice.findObject(
                UiSelector()
                    .textContains(
                        when {
                            Build.VERSION.SDK_INT >= 30 -> "While using the app"
                            else -> "Allow"
                        }
                    )
            )
        permissionOption.waitForExists(waitingTime)
        permissionOption.click()
    }

    fun UiAutomation.executeShellCommandBlocking(command: String) {
        val output = executeShellCommand(command)
        FileInputStream(output.fileDescriptor).use { it.readBytes().let {} }
    }

    @JvmStatic
    fun pressEnterKey() {
        mDevice.pressKeyCode(KeyEvent.KEYCODE_ENTER)
    }

    @JvmStatic
    fun pressBackKey() {
        mDevice.pressBack()
    }

    @JvmStatic
    fun pressHomeKey() {
        mDevice.pressHome()
    }

    fun createCustomTabIntent(
        pageUrl: String,
        customMenuItemLabel: String = "",
        customActionButtonDescription: String = "",
    ): Intent {
        val appContext = getInstrumentation().targetContext.applicationContext
        val pendingIntent = PendingIntent.getActivity(appContext, 0, Intent(), PendingIntent.FLAG_IMMUTABLE)

        val customTabColorSchemeBuilder = CustomTabColorSchemeParams.Builder()
        customTabColorSchemeBuilder.setToolbarColor(Color.MAGENTA)

        val customTabsIntent =
            CustomTabsIntent.Builder()
                .addMenuItem(customMenuItemLabel, pendingIntent)
                .setShareState(SHARE_STATE_ON)
                .setActionButton(createTestBitmap(), customActionButtonDescription, pendingIntent, true)
                .setDefaultColorSchemeParams(customTabColorSchemeBuilder.build())
                .build()
        customTabsIntent.intent.data = pageUrl.toUri()
        customTabsIntent.intent.component = ComponentName(appContext, IntentReceiverActivity::class.java)
        return customTabsIntent.intent
    }

    fun assertNativeAppOpens(appPackageName: String) {
        try {
            if (isPackageInstalled(packageName)) {
                intended(toPackage(appPackageName))
            }
        } catch (e: AssertionFailedError) {
            Log.e("TestHelper", "assertNativeAppOpens: $appPackageName was not opened", e)
        }
    }

    private fun createTestBitmap(): Bitmap {
        val bitmap = createBitmap(100, 100, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bitmap)
        canvas.drawColor(Color.GREEN)
        return bitmap
    }

    /**
     * Wrapper for tests to run only when certain conditions are met. For example: this method will avoid accidentally
     * running a test on GV versions where the feature is disabled.
     */
    fun runWithCondition(condition: Boolean, testBlock: () -> Unit) {
        if (condition) {
            testBlock()
        }
    }

    /** ******* Old code locators - used only in Screenshots tests */
    // wait for web area to be visible
    @JvmStatic
    fun waitForWebContent() {
        Assert.assertTrue(geckoView.waitForExists(waitingTime))
    }

    @JvmField
    var menuButton =
        Espresso.onView(
            Matchers.allOf(
                ViewMatchers.withId(R.id.menuView),
                ViewMatchers.isDisplayed(),
            )
        )

    @JvmField var permAllowBtn = mDevice.findObject(UiSelector().textContains("Allow").clickable(true))

    @JvmField var webView = mDevice.findObject(UiSelector().className("android.webkit.WebView").enabled(true))
    var geckoView = mDevice.findObject(UiSelector().resourceId(packageName + ":id/engineView").enabled(true))

    @JvmField var progressBar = mDevice.findObject(UiSelector().resourceId(packageName + ":id/progress").enabled(true))

    @JvmField
    var addtoHSmenuItem =
        mDevice.findObject(UiSelector().resourceId(packageName + ":id/add_to_homescreen").enabled(true))

    @JvmField
    var addtoHSCancelBtn =
        mDevice.findObject(UiSelector().resourceId(packageName + ":id/addtohomescreen_dialog_cancel").enabled(true))

    @JvmField
    var securityInfoIcon = mDevice.findObject(UiSelector().resourceId(packageName + ":id/security_info").enabled(true))

    @JvmField
    var identityState =
        mDevice.findObject(UiSelector().resourceId(packageName + ":id/site_identity_state").enabled(true))

    @JvmField var shareAppList = mDevice.findObject(UiSelector().resourceId("android:id/resolver_list").enabled(true))

    @JvmStatic
    @Throws(IOException::class)
    fun readTestAsset(filename: String?): Buffer {
        getInstrumentation().getContext().assets.open(filename!!).use { stream ->
            return readStreamFile(stream)
        }
    }

    @Throws(IOException::class)
    fun readStreamFile(file: InputStream?): Buffer {
        val buffer = Buffer()
        buffer.write(file!!.readBytes())
        return buffer
    }

    @JvmStatic
    fun waitForWebSiteTitleLoad() {
        Web.onWebView(ViewMatchers.withText("focus test page"))
    }

    @JvmStatic
    fun verifyKeyboardVisibility(isExpectedToBeVisible: Boolean) {
        val imm = getTargetContext.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager

        if (isExpectedToBeVisible) {
            assertTrue(imm.isAcceptingText)
        } else {
            assertFalse(imm.isAcceptingText)
        }
    }

    // Prevent or allow the System UI from reading the clipboard content
    // By preventing, the quick share or nearby share dialog will not be displayed
    fun allowOrPreventSystemUIFromReadingTheClipboard(allowToReadClipboard: Boolean) {
        if (allowToReadClipboard) {
            mDevice.executeShellCommand("appops set com.android.systemui READ_CLIPBOARD allow")
        } else {
            mDevice.executeShellCommand("appops set com.android.systemui READ_CLIPBOARD deny")
        }
    }
}
