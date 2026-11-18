// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::path::{Path, PathBuf};

use anyhow::{Error, Result, bail};

use crate::intermediate_representation::TargetLanguage;
use crate::util::loaders::LoaderConfig;

#[derive(Debug, PartialEq)]
pub enum CliCmd {
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

#[derive(Debug, PartialEq)]
pub struct GenerateStructCmd {
    pub manifest: String,
    pub output: PathBuf,
    pub language: TargetLanguage,
    pub channel: String,
    pub loader: LoaderConfig,
}

#[derive(Debug, PartialEq)]
pub struct GenerateExperimenterManifestCmd {
    pub manifest: String,
    pub output: PathBuf,
    pub loader: LoaderConfig,
}

#[derive(Debug, PartialEq)]
pub struct GenerateSingleFileManifestCmd {
    pub manifest: String,
    pub output: PathBuf,
    pub channel: String,
    pub loader: LoaderConfig,
}

#[derive(Debug, PartialEq)]
pub struct ValidateCmd {
    pub manifest: String,
    pub loader: LoaderConfig,
}

#[derive(Debug, Default, PartialEq)]
pub struct LintCmd {
    pub manifest: String,
    pub loader: LoaderConfig,
    pub code_review_json: Option<PathBuf>,
}

#[derive(Debug, PartialEq)]
pub struct PrintChannelsCmd {
    pub manifest: String,
    pub loader: LoaderConfig,
    pub as_json: bool,
}

#[derive(Debug, PartialEq)]
pub struct PrintInfoCmd {
    pub manifest: String,
    pub loader: LoaderConfig,
    pub channel: Option<String>,
    pub as_json: bool,
    pub feature: Option<String>,
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
