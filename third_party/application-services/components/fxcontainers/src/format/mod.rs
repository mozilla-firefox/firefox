/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Turning stored bytes into a [`ContainersData`] and back, applying the
//! migrations between the document's versions along the way.

use crate::data::{ContainersData, LATEST_VERSION};
use crate::error::ParseError;

mod migrations;

#[cfg(test)]
mod tests;

/// Reads a stored document, applying every migration needed to reach
/// [`LATEST_VERSION`]. The flag reports whether a migration ran, and the
/// document therefore has to be written back even though nothing the user did
/// changed it.
///
/// An error means the stored data is unusable and the store has to be seeded
/// from the defaults, discarding the data held by the previous containers.
pub(crate) fn parse(bytes: &[u8]) -> Result<(ContainersData, bool), ParseError> {
    let mut data: ContainersData = serde_json::from_slice(bytes)?;

    // Version 1 predates every migration path.
    if data.version == 1 {
        return Err(ParseError::UnsupportedVersion(1));
    }

    let mut migrated = false;

    if data.version == 2 {
        migrations::migrate_2_to_3(&mut data);
        migrated = true;
    }

    if data.version == 3 {
        migrations::migrate_3_to_4(&mut data);
        migrated = true;
    }

    if data.version == 4 {
        migrations::migrate_4_to_5(&mut data);
        migrated = true;
    }

    if data.version == 5 {
        migrations::migrate_5_to_6(&mut data);
        migrated = true;
    }

    if data.version == 6 {
        migrations::migrate_6_to_7(&mut data);
        migrated = true;
    }

    if data.version == 7 {
        migrations::migrate_7_to_8(&mut data);
        migrated = true;
    }

    if data.version != LATEST_VERSION {
        return Err(ParseError::UnsupportedVersion(data.version));
    }

    Ok((data, migrated))
}

pub(crate) fn serialize(data: &ContainersData) -> Vec<u8> {
    serde_json::to_vec(data).expect("containers data is always serializable")
}
