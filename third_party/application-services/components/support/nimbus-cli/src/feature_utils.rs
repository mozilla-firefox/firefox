// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::{
    path::{Path, PathBuf},
    time::{SystemTime, UNIX_EPOCH},
};

use anyhow::Result;
use heck::ToKebabCase;
use serde_json::{json, Value};

use crate::{value_utils, NimbusApp};

pub(crate) fn create_experiment(
    app: &NimbusApp,
    feature_id: &str,
    files: &Vec<PathBuf>,
) -> Result<Value> {
    let mut branches = Vec::new();
    for f in files {
        branches.push(branch(feature_id, f)?);
    }

    let control = slug(files.first().unwrap())?;

    let start = SystemTime::now();
    let now = start
        .duration_since(UNIX_EPOCH)
        .expect("Time went backawards");

    let user = whoami::username();
    let slug = format!(
        "{} test {} {:x}",
        user,
        feature_id,
        now.as_secs() & ((1u64 << 16) - 1)
    )
    .to_kebab_case();

    let app_name = app
        .app_name()
        .expect("An app name is expected. This is a bug in nimbus-cli");
    Ok(json!({
        "appId": &app_name,
        "appName": &app_name,
        "application": &app_name,
        "arguments": {},
        "branches": branches,
        "bucketConfig": {
            "count": 10_000,
            "namespace": format!("{}-1", &slug),
            "randomizationUnit": "nimbus_id",
            "start": 0,
            "total": 10_000
        },
        "channel": app.channel,
        "endDate": null,
        "enrollmentEndDate": null,
        "featureIds": [
          feature_id,
        ],
        "featureValidationOptOut": false,
        "id": &slug,
        "isEnrollmentPaused": false,
        "isRollout": false,
        "last_modified": now.as_secs() * 1000,
        "outcomes": [],
        "probeSets": [],
        "proposedDuration": 7,
        "proposedEnrollment": 7,
        "referenceBranch": control,
        "schemaVersion": "1.11.0",
        "slug": &slug,
        "startDate": null,
        "targeting": "true",
        "userFacingDescription": format!("Testing the {} feature from nimbus-cli", feature_id),
        "userFacingName": format!("[{}] Testing {}", &user, feature_id)
    }))
}

pub(crate) fn slug(path: &Path) -> Result<String> {
    let filename = path
        .file_stem()
        .ok_or_else(|| anyhow::Error::msg("File has no filename"))?;
    Ok(filename.to_string_lossy().to_string().to_kebab_case())
}

fn branch(feature_id: &str, file: &Path) -> Result<Value> {
    let value: Value = value_utils::read_from_file(file)?;

    let config = value.as_object().ok_or_else(|| {
        anyhow::Error::msg(format!(
            "{} does not contain a JSON object",
            file.to_str().unwrap()
        ))
    })?;

    Ok(json!({
      "feature": {
        "enabled": true,
        "featureId": feature_id,
        "value": config,
      },
      "slug": slug(file)?,
    }))
}
