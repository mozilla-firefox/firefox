/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.helpers

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.uiautomator.Configurator
import androidx.test.uiautomator.UiDevice
import kotlinx.coroutines.runBlocking
import mockwebserver3.MockWebServer
import mozilla.components.support.android.test.rules.MockWebServerRule
import org.junit.rules.TestRule
import org.junit.runner.Description
import org.junit.runners.model.Statement
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.helpers.AppAndSystemHelper.allowOrPreventSystemUIFromReadingTheClipboard
import org.mozilla.fenix.helpers.AppAndSystemHelper.clearDownloadsFolder
import org.mozilla.fenix.helpers.AppAndSystemHelper.disableDebugDrawer
import org.mozilla.fenix.helpers.AppAndSystemHelper.enableDataSaverSystemSetting
import org.mozilla.fenix.helpers.AppAndSystemHelper.ensureNetworkConnected
import org.mozilla.fenix.helpers.AppAndSystemHelper.runWithCondition
import org.mozilla.fenix.helpers.NetworkConnectionStatusHelper.getNetworkDetails
import org.mozilla.fenix.helpers.TestHelper.appContext
import org.mozilla.fenix.helpers.TestHelper.mDevice
import org.mozilla.fenix.ui.efficiency.logging.TestLogging
import org.mozilla.fenix.ui.robots.notificationShade

class EfficiencyTestRule(
    private val context: (Description) -> EfficiencyExecutionContext,
    private val onCleanupFailure: (String) -> Unit,
) : TestRule {
    private val mockWebServerRule = MockWebServerRule()

    val mockWebServer: MockWebServer
        get() = mockWebServerRule.server

    override fun apply(base: Statement, description: Description): Statement {
        val executionContext = context(description)
        val testStatement =
            when (executionContext.requirements.mockWebServer) {
                MockWebServerRequirement.AVAILABLE -> mockWebServerRule.apply(base, description)
                MockWebServerRequirement.NOT_NEEDED -> base
            }

        return object : Statement() {
            override fun evaluate() {
                val environment = EfficiencyTestEnvironment(executionContext)
                var failure: Throwable? = null
                try {
                    environment.prepare()
                    testStatement.evaluate()
                } catch (throwable: Throwable) {
                    failure = throwable
                } finally {
                    runCatching(environment::restore).onFailure { cleanupFailure ->
                        onCleanupFailure("environment")
                        failure?.addSuppressed(cleanupFailure) ?: run { failure = cleanupFailure }
                    }
                }
                failure?.let { throw it }
            }
        }
    }
}

private class EfficiencyTestEnvironment(private val context: EfficiencyExecutionContext) {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val device = UiDevice.getInstance(instrumentation)
    private var initialSnapshot: EnvironmentSnapshot? = null

    fun prepare() {
        val before = snapshot()
        initialSnapshot = before
        applyRequirements()
        // Recover connectivity a prior network-toggling test may have left disabled, before this one
        // launches the app. Best-effort and logged; the subsequent getNetworkDetails() in
        // clearExternalState() then reports the recovered state.
        ensureNetworkConnected()
        clearExternalState()
        val after = snapshot()
        record("environmentPreflight", before, after)
        verifyRequirements(after)
    }

    fun restore() {
        val before = snapshot()
        restoreSnapshot(checkNotNull(initialSnapshot) { "Environment was not sampled before restore" })
        // Symmetry with prepare(): recover connectivity a network-toggling test may have left disabled
        // (e.g. if it died before its own cleanup) so the test ends connected and the end-of-test
        // getNetworkDetails() below reports the restored state.
        ensureNetworkConnected()
        clearExternalState()
        val after = snapshot()
        record("environmentRestore", before, after)
        verifyRestored(after)
    }

    private fun applyRequirements() {
        val requirements = context.requirements
        applyState(requirements.backGestureNavigation, ::setBackGestureNavigation)
        applyState(requirements.dataSaver, ::enableDataSaverSystemSetting)
        if (Build.VERSION.SDK_INT == Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            applyState(requirements.systemUiClipboardAccess) {
                allowOrPreventSystemUIFromReadingTheClipboard(it)
            }
        }
        applyPostNotificationPermission(requirements.postNotificationPermission)
        requirements.uiAutomatorIdleTimeoutMillis?.let { Configurator.getInstance().waitForIdleTimeout = it }
    }

    private fun clearExternalState() {
        val requirements = context.requirements
        runBlocking {
            if (requirements.clearSharedDownloads) clearDownloadsFolder()
            disableDebugDrawer()
        }
        if (requirements.clearNotifications) {
            notificationShade {
                cancelAllShownNotifications()
                mDevice.executeShellCommand("cmd statusbar collapse")
            }
        }
        runWithCondition(BuildConfig.DEBUG) { getNetworkDetails() }
    }

    private fun applyPostNotificationPermission(requirement: RequiredState) {
        if (Build.VERSION.SDK_INT < 33 || requirement == RequiredState.PRESERVE) return
        val granted = hasPostNotificationPermission()
        when (requirement) {
            RequiredState.ENABLED -> if (!granted) grantPostNotificationPermission()
            RequiredState.DISABLED ->
                check(!granted) {
                    "POST_NOTIFICATIONS must be denied before instrumentation starts for ${context.testId}"
                }
            RequiredState.PRESERVE -> Unit
        }
    }

    private fun grantPostNotificationPermission() {
        instrumentation.uiAutomation.grantRuntimePermission(
            appContext.packageName,
            Manifest.permission.POST_NOTIFICATIONS,
        )
    }

    private fun verifyRequirements(
        snapshot: EnvironmentSnapshot,
        verifyNotificationPermission: Boolean = true,
    ) {
        val requirements = context.requirements
        val violations = mutableListOf<String>()
        expectedBoolean(requirements.backGestureNavigation)?.let { expected ->
            if (snapshot.backGestureLeft != expected || snapshot.backGestureRight != expected) {
                violations += "backGesture=${snapshot.backGestureLeft}/${snapshot.backGestureRight}"
            }
        }
        expectedBoolean(requirements.dataSaver)?.let { expected ->
            if (snapshot.dataSaver != expected) violations += "dataSaver=${snapshot.dataSaver}"
        }
        if (Build.VERSION.SDK_INT == Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            expectedBoolean(requirements.systemUiClipboardAccess)?.let { expected ->
                if (snapshot.systemUiClipboardAccess != expected) {
                    violations += "systemUiClipboardAccess=${snapshot.systemUiClipboardAccess}"
                }
            }
        }
        if (verifyNotificationPermission && Build.VERSION.SDK_INT >= 33) {
            expectedBoolean(requirements.postNotificationPermission)?.let { expected ->
                if (snapshot.postNotificationPermission != expected) {
                    violations += "postNotificationPermission=${snapshot.postNotificationPermission}"
                }
            }
        }
        check(violations.isEmpty()) {
            "Execution requirements were not satisfied for ${context.testId}: ${violations.joinToString()}"
        }
    }

    private fun snapshot(): EnvironmentSnapshot {
        val backGestureLeftSetting = secureSetting("back_gesture_inset_scale_left")
        val backGestureRightSetting = secureSetting("back_gesture_inset_scale_right")
        return EnvironmentSnapshot(
            orientation = orientation(),
            backGestureLeft = backGestureLeftSetting.asBoolean(),
            backGestureRight = backGestureRightSetting.asBoolean(),
            backGestureLeftSetting = backGestureLeftSetting,
            backGestureRightSetting = backGestureRightSetting,
            dataSaver = dataSaverEnabled(),
            systemUiClipboardAccess = systemUiClipboardAccess(),
            postNotificationPermission = if (Build.VERSION.SDK_INT >= 33) hasPostNotificationPermission() else null,
            foregroundWindow = focusedWindow(),
            uiAutomatorIdleTimeoutMillis = Configurator.getInstance().waitForIdleTimeout,
        )
    }

    private fun orientation(): Int = appContext.resources.configuration.orientation

    private fun secureSetting(key: String): String? =
        device.executeShellCommand("settings get secure $key").trim().takeUnless { it == "null" || it.isEmpty() }

    private fun String?.asBoolean(): Boolean? = this?.toFloatOrNull()?.let { it > 0f }

    private fun setBackGestureNavigation(enabled: Boolean) {
        val value = if (enabled) 1 else 0
        device.executeShellCommand("settings put secure back_gesture_inset_scale_left $value")
        device.executeShellCommand("settings put secure back_gesture_inset_scale_right $value")
    }

    private fun dataSaverEnabled(): Boolean? {
        val output = device.executeShellCommand("cmd netpolicy get restrict-background").lowercase()
        return when {
            "enabled" in output -> true
            "disabled" in output -> false
            else -> null
        }
    }

    private fun systemUiClipboardAccess(): Boolean? {
        if (Build.VERSION.SDK_INT != Build.VERSION_CODES.UPSIDE_DOWN_CAKE) return null
        val output = device.executeShellCommand("appops get com.android.systemui READ_CLIPBOARD").lowercase()
        return when {
            "allow" in output -> true
            "deny" in output || "ignore" in output -> false
            else -> null
        }
    }

    private fun restoreSnapshot(snapshot: EnvironmentSnapshot) {
        restoreSecureSetting("back_gesture_inset_scale_left", snapshot.backGestureLeftSetting)
        restoreSecureSetting("back_gesture_inset_scale_right", snapshot.backGestureRightSetting)
        snapshot.dataSaver?.let(::enableDataSaverSystemSetting)
        if (Build.VERSION.SDK_INT == Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            snapshot.systemUiClipboardAccess?.let(::allowOrPreventSystemUIFromReadingTheClipboard)
        }
        Configurator.getInstance().waitForIdleTimeout = snapshot.uiAutomatorIdleTimeoutMillis
    }

    private fun verifyRestored(snapshot: EnvironmentSnapshot) {
        val initial = checkNotNull(initialSnapshot)
        val violations = mutableListOf<String>()
        if (snapshot.backGestureLeftSetting != initial.backGestureLeftSetting) {
            violations += "backGestureLeft=${snapshot.backGestureLeftSetting}"
        }
        if (snapshot.backGestureRightSetting != initial.backGestureRightSetting) {
            violations += "backGestureRight=${snapshot.backGestureRightSetting}"
        }
        if (initial.dataSaver != null && snapshot.dataSaver != initial.dataSaver) {
            violations += "dataSaver=${snapshot.dataSaver}"
        }
        if (
            initial.systemUiClipboardAccess != null &&
                snapshot.systemUiClipboardAccess != initial.systemUiClipboardAccess
        ) {
            violations += "systemUiClipboardAccess=${snapshot.systemUiClipboardAccess}"
        }
        if (snapshot.uiAutomatorIdleTimeoutMillis != initial.uiAutomatorIdleTimeoutMillis) {
            violations += "uiAutomatorIdleTimeoutMillis=${snapshot.uiAutomatorIdleTimeoutMillis}"
        }
        check(violations.isEmpty()) {
            "Device environment was not restored for ${context.testId}: ${violations.joinToString()}"
        }
    }

    private fun hasPostNotificationPermission(): Boolean =
        appContext.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED

    private fun focusedWindow(): String =
        device
            .executeShellCommand("dumpsys window | grep -E 'mCurrentFocus|mFocusedApp'")
            .lineSequence()
            .map(String::trim)
            .firstOrNull { it.isNotEmpty() }
            .orEmpty()

    private fun restoreSecureSetting(key: String, value: String?) {
        val command = if (value == null) "settings delete secure $key" else "settings put secure $key $value"
        device.executeShellCommand(command)
    }

    private fun record(
        event: String,
        before: EnvironmentSnapshot,
        after: EnvironmentSnapshot,
    ) {
        TestLogging.installed()
            .record(
                event,
                mapOf(
                    "testId" to context.testId,
                    "requirements" to context.requirements.asMeta(),
                    "before" to before.asMeta(),
                    "after" to after.asMeta(),
                ),
            )
    }

    private data class EnvironmentSnapshot(
        val orientation: Int,
        val backGestureLeft: Boolean?,
        val backGestureRight: Boolean?,
        val backGestureLeftSetting: String?,
        val backGestureRightSetting: String?,
        val dataSaver: Boolean?,
        val systemUiClipboardAccess: Boolean?,
        val postNotificationPermission: Boolean?,
        val foregroundWindow: String,
        val uiAutomatorIdleTimeoutMillis: Long,
    ) {
        fun asMeta(): Map<String, Any?> =
            mapOf(
                "orientation" to orientation,
                "backGestureLeft" to backGestureLeft,
                "backGestureRight" to backGestureRight,
                "dataSaver" to dataSaver,
                "systemUiClipboardAccess" to systemUiClipboardAccess,
                "postNotificationPermission" to postNotificationPermission,
                "foregroundWindow" to foregroundWindow,
            )
    }

    private companion object {
        fun applyState(requirement: RequiredState, action: (Boolean) -> Unit) {
            when (requirement) {
                RequiredState.ENABLED -> action(true)
                RequiredState.DISABLED -> action(false)
                RequiredState.PRESERVE -> Unit
            }
        }

        fun expectedBoolean(requirement: RequiredState): Boolean? =
            when (requirement) {
                RequiredState.ENABLED -> true
                RequiredState.DISABLED -> false
                RequiredState.PRESERVE -> null
            }
    }
}
