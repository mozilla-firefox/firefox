/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * A Telemetry helper to report uptake of Remote Settings content.
 */
export class UptakeTelemetry {
  /**
   * Return the list of reported uptake statuses.
   * Refer to `metrics.yaml` for details about semantics.
   *
   * @type {object}
   */
  static get STATUS() {
    return {
      UP_TO_DATE: "up_to_date",
      SUCCESS: "success",
      BACKOFF: "backoff",
      PARSE_ERROR: "parse_error",
      CONTENT_ERROR: "content_error",
      PREF_DISABLED: "pref_disabled",
      SIGNATURE_ERROR: "sign_error",
      SIGNATURE_RETRY_ERROR: "sign_retry_error",
      CONFLICT_ERROR: "conflict_error",
      CORRUPTION_ERROR: "corruption_error",
      SYNC_START: "sync_start",
      SYNC_ERROR: "sync_error",
      APPLY_ERROR: "apply_error",
      SERVER_ERROR: "server_error",
      CERTIFICATE_ERROR: "certificate_error",
      DOWNLOAD_START: "download_start",
      DOWNLOAD_ERROR: "download_error",
      TIMEOUT_ERROR: "timeout_error",
      NETWORK_ERROR: "network_error",
      NETWORK_OFFLINE_ERROR: "offline_error",
      SHUTDOWN_ERROR: "shutdown_error",
      UNKNOWN_ERROR: "unknown_error",
      CLEANUP_ERROR: "cleanup_error",
      SYNC_BROKEN_ERROR: "sync_broken_error",
      CUSTOM_1_ERROR: "custom_1_error",
      STALE_TIMESTAMP_ERROR: "stale_timestamp_error",
    };
  }

  /**
   * Reports the uptake status for the specified source.
   *
   * @param {string} status        the uptake status (eg. "network_error")
   * @param {object} extra         extra values to report
   * @param {string} extra.source  the update source (eg. "recipe-42").
   * @param {string} extra.trigger what triggered the polling/fetching (eg. "broadcast", "timer").
   * @param {int}    extra.age     age of pulled data in seconds
   */
  static async report(status, extra = {}) {
    const { source } = extra;

    if (!source) {
      throw new Error("`source` value is mandatory.");
    }

    if (!Object.values(UptakeTelemetry.STATUS).includes(status)) {
      throw new Error(`Unknown status '${status}'`);
    }

    extra.value = status;
    const stringExtra = Object.fromEntries(
      Object.entries(extra).map(([k, v]) => [k, `${v}`])
    );
    Glean.uptakeRemotecontentResult.uptakeRemotesettings.record(stringExtra);
  }
}
