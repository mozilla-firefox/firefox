/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::collections::HashMap;

use crate::util::loaders::LoaderConfig;

#[derive(Debug, Default)]
pub struct FmlLoaderConfig {
    pub cache: Option<String>,
    pub refs: HashMap<String, String>,
    pub ref_files: Vec<String>,
    pub lax_gecko_pref_validation: bool,
}

impl From<FmlLoaderConfig> for LoaderConfig {
    fn from(value: FmlLoaderConfig) -> Self {
        let cwd = std::env::current_dir().expect("Current Working Directory is not set");
        let cache = value.cache.map(|v| cwd.join(v));
        Self {
            cwd,
            refs: value.refs.into_iter().collect(),
            repo_files: value.ref_files,
            cache_dir: cache,
            lax_gecko_pref_validation: value.lax_gecko_pref_validation,
        }
    }
}
