package org.mozilla.geckoview.test.crash

import android.content.Context
import android.content.Intent
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoRuntimeSettings
import org.mozilla.geckoview.test.TestCrashHandler
import org.mozilla.geckoview.test.TestRuntimeService

class RuntimeCrashTestService : TestRuntimeService() {
    override fun createRuntime(context: Context, intent: Intent): GeckoRuntime {
        return GeckoRuntime.create(
            this.applicationContext,
            GeckoRuntimeSettings.Builder()
                .extras(intent.extras!!)
                .crashHandler(TestCrashHandler::class.java)
                .lowMemoryDetection(false) // Avoid unpredictability in tests
                .build(),
        )
    }
}
