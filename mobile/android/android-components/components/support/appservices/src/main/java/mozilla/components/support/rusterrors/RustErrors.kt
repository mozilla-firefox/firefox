/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.rusterrors

import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import mozilla.appservices.errorsupport.RustComponentsErrorTelemetry
import mozilla.appservices.tracing.EventSink
import mozilla.appservices.tracing.EventSinkSpecification
import mozilla.appservices.tracing.EventTarget
import mozilla.appservices.tracing.TracingEvent
import mozilla.appservices.tracing.TracingLevel
import mozilla.appservices.tracing.registerEventSink
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.support.base.log.logger.Logger

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
    private val logger = Logger("RustErrors.ErrorEventSink")
    private val json = Json { ignoreUnknownKeys = true }
    private var reportedBreadcrumbFailure = false

    override fun onEvent(event: TracingEvent) {
        if (event.target == "app-services-error-reporter::breadcrumb") {
            val fields =
                try {
                    json.decodeFromString<BreadcrumbFields>(event.fields)
                } catch (e: SerializationException) {
                    logger.error("Failed to decode breadcrumb: ${e} (fields: ${event.fields})")
                    if (!reportedBreadcrumbFailure) {
                        crashReporter.submitCaughtException(e)
                        reportedBreadcrumbFailure = true
                    }
                    return
                }
            // Ugly hack to prevent Sentry from redacting the breadcrumbs data
            // The Rust components are responsible for ensuring there's no PII in the breadcrumbs.
            val message = fields.message.replace("Auth", "Ath").replace("auth", "ath")
            crashReporter.recordCrashBreadcrumb(Breadcrumb(message))
        }
    }
}

/**
 * Represents the JSON breadcrumbs data we get from the Rust world.
 *
 * Rust sends more fields, but we only use the message.
 */
@Serializable data class BreadcrumbFields(val message: String)
