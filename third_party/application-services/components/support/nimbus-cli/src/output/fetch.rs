// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::path::Path;

use anyhow::Result;
use serde_json::Value;

use crate::{sources::ExperimentListSource, value_utils};

impl ExperimentListSource {
    pub(crate) fn fetch_list<P>(&self, file: Option<P>) -> Result<bool>
    where
        P: AsRef<Path>,
    {
        let value: Value = self.try_into()?;
        value_utils::write_to_file_or_print(file, &value)?;

        Ok(true)
    }
}
