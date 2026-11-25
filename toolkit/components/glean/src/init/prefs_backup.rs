// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use firefox_on_glean::metrics;
use glean::ExternalBackup;
use nsstring::nsCString;

/// A Prefs-based external backup.
/// Only supports the key "client_id".
#[derive(Debug)]
pub(crate) struct PrefsBackup;

impl ExternalBackup for PrefsBackup {
    fn store(&self, key: String, value: String) {
        if &key != "client_id" {
            metrics::fog::unknown_backup_key.add(1);
            return;
        }
        // Prefs may only be written to on the main thread in the parent process.
        // Call out to C++ to make this happen:
        extern "C" {
            pub fn FOG_SetPrefOnMainThread(pref_name: &nsCString, pref_value: &nsCString) -> bool;
        }
        let pref_name = nsCString::from("telemetry.fog.backup.client_id");
        let pref_value = nsCString::from(value);
        // SAFETY NOTE:
        // We have ownership of key, value and safely transformed them to nsCStrings.
        // We handed them to C++ as pointers to const, so they shouldn't be messed with.
        // Even if they were, we don't touch the data after we give it to C++.
        unsafe {
            // This is an asynchronous call.
            // This means subsequent calls to `load` may not return the value we just stored.
            // Since we know that Glean always calls `load` before anything else in a session,
            // this is fine (at least for now).
            FOG_SetPrefOnMainThread(&pref_name, &pref_value);
        }
    }

    fn load(&self, key: String) -> Option<String> {
        if &key != "client_id" {
            metrics::fog::unknown_backup_key.add(1);
            return None;
        }
        let client_id_ns = static_prefs::pref!("telemetry.fog.backup.client_id");
        let client_id = if client_id_ns.is_empty() {
            None
        } else {
            Some(client_id_ns.to_string())
        };
        client_id
    }

    fn clear(&self, key: String) {
        if &key != "client_id" {
            metrics::fog::unknown_backup_key.add(1);
        }
        self.store(key, "".into());
    }

    fn clear_all(&self) {
        self.clear("client_id".into());
    }

    fn should_apply_stored_client_id(&self) -> bool {
        static_prefs::pref!("telemetry.fog.apply_stored_client_id")
    }
}
