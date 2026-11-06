/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#![cfg(feature = "stateful")]

use crate::NimbusClient;
use crate::error::Result;
use crate::tests::helpers::TestMetrics;

#[test]
fn test_null_client() -> Result<()> {
    error_support::init_for_tests();

    let tmp_dir = tempfile::tempdir()?;

    let client = NimbusClient::new(
        Default::default(),
        Default::default(),
        Default::default(),
        tmp_dir.path(),
        TestMetrics::new(),
        None,
        None,
    )?;
    client.fetch_experiments()?;
    client.apply_pending_experiments()?;

    let experiments = client.get_all_experiments()?;
    assert_eq!(experiments.len(), 0);
    Ok(())
}
