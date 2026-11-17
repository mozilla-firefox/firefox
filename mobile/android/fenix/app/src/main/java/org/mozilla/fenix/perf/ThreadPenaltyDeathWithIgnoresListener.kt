/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.perf

import android.os.Build
import android.os.StrictMode
import android.os.strictmode.Violation
import androidx.annotation.RequiresApi
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.BuildManufacturerChecker
import mozilla.components.support.utils.ManufacturerChecker

private const val FCQN_EDM_STORAGE_PROVIDER_BASE = "com.android.server.enterprise.storage.EdmStorageProviderBase"
private const val FREECESS_CONTROLLER_CLASS = "com.android.server.am.FreecessController"
private const val IDS_CONTROLLER_CLASS = "android.app.IdsController"
private const val INSTRUMENTED_HOOKS_CLASS = "com.android.tools.deploy.instrument.InstrumentationHooks"
private const val ACTIVITY_MANAGER_SERVICE_CLASS = "com.android.server.am.ActivityManagerService"
private const val IN_MEMORY_DEX_CLASS_LOADER_CLASS = "dalvik.system.InMemoryDexClassLoader"
private const val MIUI_MULTI_LANG_HELPER_CLASS = "miui.util.font.MultiLangHelper"
private const val PLATFORM_PREFERENCE_CLASS = "androidx.preference.Preference"

/**
 * A [StrictMode.OnThreadViolationListener] that recreates [StrictMode.ThreadPolicy.Builder.penaltyDeath] but will
 * ignore some violations. For example, sometimes OEMs will add code that violates StrictMode so we can ignore them here
 * instead of cluttering up our code with resetAfter.
 *
 * This class can only be used with Android P+ so we'd have to implement workarounds if the violations we want to ignore
 * affect older devices.
 */
@RequiresApi(Build.VERSION_CODES.P)
class ThreadPenaltyDeathWithIgnoresListener(
    private val logger: Logger = PerformanceLogger.logger,
    private val manufacturerChecker: ManufacturerChecker = BuildManufacturerChecker(),
) : StrictMode.OnThreadViolationListener {

    override fun onThreadViolation(violation: Violation?) {
        if (violation == null) return

        // Unfortunately, this method gets called many (~5+) times with the same violation so we end
        // up logging/throwing redundantly.
        if (shouldViolationBeIgnored(violation)) {
            logger.debug("Ignoring StrictMode ThreadPolicy violation", violation)
        } else {
            penaltyDeath(violation)
        }
    }

    @Suppress("TooGenericExceptionThrown") // we throw what StrictMode's penaltyDeath throws.
    private fun penaltyDeath(violation: Violation) {
        throw RuntimeException("StrictMode ThreadPolicy violation", violation)
    }

    private fun shouldViolationBeIgnored(violation: Violation): Boolean =
        isSamsungLgEdmStorageProviderStartupViolation(violation) ||
            containsInstrumentedHooksClass(violation) ||
            isSamsungIdsController(violation) ||
            isSamsungFreecessController(violation) ||
            isXiaomiMultiLangHelperViolation(violation) ||
            isFinishAttachApplication(violation) ||
            containsInMemoryDexClassLoader(violation) ||
            isInflatingPlatformPreference(violation)

    private fun isSamsungIdsController(violation: Violation): Boolean {
        // See https://bugzilla.mozilla.org/show_bug.cgi?id=1806469
        // When launching in debug on a Galaxy S22+ from Android Studio then we hit a DiskReadViolation
        // IdsController doesn't appear in Android code search so we match against it.
        // https://cs.android.com/search?q=IdsController
        return manufacturerChecker.isSamsung() && violation.stackTrace.any { it.className == IDS_CONTROLLER_CLASS }
    }

    private fun isSamsungFreecessController(violation: Violation): Boolean {
        // See https://bugzilla.mozilla.org/show_bug.cgi?id=2094273
        // Samsung's app freezer reads /proc in system_server while handling startActivity, and the
        // DiskReadViolation is propagated back over binder to the calling app thread.
        return manufacturerChecker.isSamsung() && violation.stackTrace.any { it.className == FREECESS_CONTROLLER_CLASS }
    }

    private fun isSamsungLgEdmStorageProviderStartupViolation(violation: Violation): Boolean {
        // Root issue: https://github.com/mozilla-mobile/fenix/issues/17920
        //
        // This fix may address the issues seen in this bug:
        // https://github.com/mozilla-mobile/fenix/issues/15430
        // So we might be able to back out the changes made there. However, I don't have a device to
        // test so I didn't bother.
        //
        // This issue occurs on the Galaxy S10e, Galaxy A50, Note 10, and LG G7 FIT but not the S7:
        // I'm guessing it's just a problem on recent Samsungs and LGs so it's okay being in this P+
        // listener.
        if (!manufacturerChecker.isSamsung() && !manufacturerChecker.isLG()) {
            return false
        }

        // To ignore this warning, we can inspect the stack trace. There are no parts of the
        // violation stack trace that are clearly unique to this violation but
        // EdmStorageProviderBase doesn't appear in Android code search so we match against it.
        // This class may be used in other violations that we're capable of fixing but this
        // code may ignore them. I think it's okay - we keep this code simple and if it was a serious
        // issue, we'd catch it on other manufacturers.
        return violation.stackTrace.any { it.className == FCQN_EDM_STORAGE_PROVIDER_BASE }
    }

    private fun isFinishAttachApplication(violation: Violation): Boolean {
        // On recent Android versions, the epilogue of [ActivityThread.handleBindApplication]
        // makes a Binder call that looks up settings from disk. This happens as we return
        // from our [Application.onCreate] method.
        return violation.stackTrace.any {
            it.className == ACTIVITY_MANAGER_SERVICE_CLASS && it.methodName == "finishAttachApplication"
        }
    }

    private fun containsInstrumentedHooksClass(violation: Violation): Boolean {
        // See https://github.com/mozilla-mobile/fenix/issues/21695
        // When deploying debug builds from Android Studio then we may hit a DiskReadViolation
        // occasionally. There's an upstream fix for this, but the stable version of Android Studio
        // still seems to be affected.
        // https://cs.android.com/android-studio/platform/tools/base/+/abbbe67087626460e0127d3f5377f9cf896e9941
        return violation.stackTrace.any { it.className == INSTRUMENTED_HOOKS_CLASS }
    }

    private fun containsInMemoryDexClassLoader(violation: Violation): Boolean {
        // Using some types of breakpoints in a debugger may cause DEX rewriting which
        // injects the [dalvik.system.InMemoryDexClassLoader] into call stacks leading
        // to StrictMode violations if it happens on main thread.
        return violation.stackTrace.any { it.className == IN_MEMORY_DEX_CLASS_LOADER_CLASS }
    }

    private fun isXiaomiMultiLangHelperViolation(violation: Violation): Boolean {
        return manufacturerChecker.isXiaomi() &&
            violation.stackTrace.any { it.className == MIUI_MULTI_LANG_HELPER_CLASS }
    }

    private fun isInflatingPlatformPreference(violation: Violation): Boolean {
        // See https://bugzilla.mozilla.org/show_bug.cgi?id=2030682
        // See https://issuetracker.google.com/issues/266976877
        // While inflating Preferences (on the main thread) this framework may try to read the SharedPreference file.
        // Google accepts it as an issue and won't fix it as the Preferences framework is in maintenance mode.
        return violation.stackTrace.any {
            it.className == PLATFORM_PREFERENCE_CLASS && it.methodName == "getSharedPreferences"
        }
    }
}
