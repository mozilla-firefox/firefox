/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use serde_json::Value;

use crate::data::ContainersData;
use crate::defaults;
use crate::definitions;

/// Bug 1419591: nothing to rewrite, the version alone had to move.
pub(crate) fn migrate_2_to_3(data: &mut ContainersData) {
    data.version = 3;
}

/// Bug 1406181: reserve the identity backing the extension storage.local API.
pub(crate) fn migrate_3_to_4(data: &mut ContainersData) {
    data.identities
        .push(defaults::webext_storage_local_identity());
    data.version = 4;
}

/// Bug 1814969: StringBundle labels give way to Fluent identifiers.
pub(crate) fn migrate_4_to_5(data: &mut ContainersData) {
    for identity in &mut data.identities {
        let legacy = identity.extra.remove("l10nID");
        identity.extra.remove("accessKey");

        let Some(Value::String(legacy)) = legacy else {
            continue;
        };

        // Anything outside the four shipped labels keeps whatever it had.
        let fluent = match legacy.as_str() {
            "userContextPersonal.label" => Some("user-context-personal"),
            "userContextWork.label" => Some("user-context-work"),
            "userContextBanking.label" => Some("user-context-banking"),
            "userContextShopping.label" => Some("user-context-shopping"),
            _ => None,
        };

        if let Some(fluent) = fluent {
            identity
                .extra
                .insert("l10nId".to_string(), Value::String(fluent.to_string()));
        }
    }

    data.version = 5;
}

/// The color refresh: stored identities keep only canonical names.
pub(crate) fn migrate_5_to_6(data: &mut ContainersData) {
    for identity in &mut data.identities {
        if !identity.color.is_empty() {
            identity.color = definitions::resolve_color(&identity.color);
        }
    }

    data.version = 6;
}

/// Bug 2071753: the shipped labels' Fluent identifiers were renamed when the
/// container menu items lost their accesskeys.
pub(crate) fn migrate_6_to_7(data: &mut ContainersData) {
    for identity in &mut data.identities {
        let Some(Value::String(l10n_id)) = identity.extra.get_mut("l10nId") else {
            continue;
        };

        let renamed = match l10n_id.as_str() {
            "user-context-personal" => "user-context-personal2",
            "user-context-work" => "user-context-work2",
            "user-context-banking" => "user-context-banking2",
            "user-context-shopping" => "user-context-shopping2",
            _ => continue,
        };

        *l10n_id = renamed.to_string();
    }

    data.version = 7;
}

/// Bug 2072625: the default containers' Fluent identifiers are no longer
/// stored. They come from the default identities the store was opened with, so
/// renaming one no longer needs a migration.
pub(crate) fn migrate_7_to_8(data: &mut ContainersData) {
    for identity in &mut data.identities {
        identity.extra.remove("l10nId");
    }

    data.version = 8;
}
