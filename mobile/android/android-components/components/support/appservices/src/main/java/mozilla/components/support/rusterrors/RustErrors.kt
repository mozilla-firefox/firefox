/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.rusterrors

import mozilla.appservices.errorsupport.RustComponentsErrorTelemetry
import mozilla.appservices.tracing.EventSink
import mozilla.appservices.tracing.EventSinkSpecification
import mozilla.appservices.tracing.EventTarget
import mozilla.appservices.tracing.TracingEvent
import mozilla.appservices.tracing.TracingLevel
import mozilla.appservices.tracing.registerEventSink
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting

/**
 * Report a Rust component error
 *
 * This is used when the Kotlin code sees an error in Rust that Rust can't report itself, for example a UniFFI
 * InternalException.
 */
fun reportRustError(typeName: String, exception: Throwable) {
    RustComponentsErrorTelemetry.submitErrorPing(typeName, exception.toString())
}

/**
 * Initialize application services error reporting
 *
 * Errors reports and breadcrumbs from Application Services will be forwarded to the CrashReporting instance. Error
 * counting, which is used for expected errors like network errors, will be counted with Glean.
 */
public fun initializeRustErrors(crashReporter: CrashReporting) {
    val spec = EventSinkSpecification(targets = listOf(EventTarget("app-services-error-reporter", TracingLevel.DEBUG)))
    registerEventSink(spec, ErrorEventSink(crashReporter))
}

private class ErrorEventSink(val crashReporter: CrashReporting) : EventSink {
    override fun onEvent(event: TracingEvent) {
        if (event.target == "app-services-error-reporter::breadcrumb") {
            // Ugly hack to prevent Sentry from redacting the breadcrumbs data
            // The Rust components are responsible for ensuring there's no PII in the breadcrumbs.
            val message = event.message.replace("Auth", "Ath").replace("auth", "ath")
            crashReporter.recordCrashBreadcrumb(Breadcrumb(message))
        }
    }
}
