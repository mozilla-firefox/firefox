/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.pdf

/** Reasons a signature could not be added to a PDF for telemetry purposes. */
internal sealed interface SignatureFailure {
    /** The telemetry name to use for reporting. */
    val telemetryName: String

    /** The selected tab had no engine session. */
    data object NoEngineSession : SignatureFailure {
        override val telemetryName = "no_engine_session"
    }

    /** The engine could not pass the signature to the PDF viewer. */
    data object EngineError : SignatureFailure {
        override val telemetryName = "engine_error"
    }

    /** When adding a signature, the request didn't complete within the expected timeframe. */
    data object Timeout : SignatureFailure {
        override val telemetryName = "timeout"
    }
}
