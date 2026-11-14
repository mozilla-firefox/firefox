// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::path::{Path, PathBuf};

use anyhow::{Error, Result, bail};

use crate::intermediate_representation::TargetLanguage;
use crate::util::loaders::LoaderConfig;

pub(crate) enum CliCmd {
    Generate(GenerateStructCmd),
    GenerateExperimenter(GenerateExperimenterManifestCmd),
    GenerateSingleFileManifest(GenerateSingleFileManifestCmd),
    FetchFile(LoaderConfig, String),
    Validate(ValidateCmd),
    Lint(LintCmd),
    ListLints,
    PrintChannels(PrintChannelsCmd),
    PrintInfo(PrintInfoCmd),
}

#[derive(Clone)]
pub(crate) struct GenerateStructCmd {
    pub(crate) manifest: String,
    pub(crate) output: PathBuf,
    pub(crate) language: TargetLanguage,
    pub(crate) load_from_ir: bool,
    pub(crate) channel: String,
    pub(crate) loader: LoaderConfig,
}

pub(crate) struct GenerateExperimenterManifestCmd {
    pub(crate) manifest: String,
    pub(crate) output: PathBuf,
    pub(crate) language: TargetLanguage,
    pub(crate) load_from_ir: bool,
    pub(crate) loader: LoaderConfig,
}

pub(crate) struct GenerateSingleFileManifestCmd {
    pub(crate) manifest: String,
    pub(crate) output: PathBuf,
    pub(crate) channel: String,
    pub(crate) loader: LoaderConfig,
}

pub(crate) struct ValidateCmd {
    pub(crate) manifest: String,
    pub(crate) loader: LoaderConfig,
}

#[derive(Default)]
pub(crate) struct LintCmd {
    pub(crate) manifest: String,
    pub(crate) loader: LoaderConfig,
    pub(crate) code_review_json: Option<PathBuf>,
}

pub(crate) struct PrintChannelsCmd {
    pub(crate) manifest: String,
    pub(crate) loader: LoaderConfig,
    pub(crate) as_json: bool,
}

pub(crate) struct PrintInfoCmd {
    pub(crate) manifest: String,
    pub(crate) loader: LoaderConfig,
    pub(crate) channel: Option<String>,
    pub(crate) as_json: bool,
    pub(crate) feature: Option<String>,
}

impl TryFrom<&std::ffi::OsStr> for TargetLanguage {
    type Error = Error;
    fn try_from(value: &std::ffi::OsStr) -> Result<Self> {
        if let Some(s) = value.to_str() {
            TryFrom::try_from(s)
        } else {
            bail!("Unreadable target language")
        }
    }
}

impl TryFrom<&Path> for TargetLanguage {
    type Error = Error;
    fn try_from(value: &Path) -> Result<Self> {
        TryFrom::try_from(
            value
                .extension()
                .ok_or_else(|| anyhow::anyhow!("No extension available to determine language"))?,
        )
    }
}

impl TryFrom<String> for TargetLanguage {
    type Error = Error;
    fn try_from(value: String) -> Result<Self> {
        TryFrom::try_from(value.as_str())
    }
}
