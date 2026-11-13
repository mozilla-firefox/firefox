// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::fmt::Display;

use anyhow::Result;
use nimbus_fml::{
    intermediate_representation::FeatureManifest, parser::Parser, util::loaders::FileLoader,
};

use crate::{cli::ManifestArgs, config, NimbusApp};

#[derive(Debug, PartialEq)]
pub(crate) enum ManifestSource {
    FromGithub {
        channel: String,

        github_repo: String,
        ref_: String,

        manifest_file: String,
    },
    FromFile {
        channel: String,
        manifest_file: String,
    },
}

impl ManifestSource {
    fn manifest_file(&self) -> &str {
        let (Self::FromFile { manifest_file, .. } | Self::FromGithub { manifest_file, .. }) = self;
        manifest_file
    }

    fn channel(&self) -> &str {
        let (Self::FromFile { channel, .. } | Self::FromGithub { channel, .. }) = self;
        channel
    }

    fn manifest_loader(&self) -> Result<FileLoader> {
        let cwd = std::env::current_dir().expect("Current Working Directory is not set");
        let mut files = FileLoader::new(cwd, config::manifest_cache_dir(), Default::default())?;
        if let Self::FromGithub {
            ref_, github_repo, ..
        } = self
        {
            files.add_repo(github_repo, ref_)?;
        }
        Ok(files)
    }

    pub(crate) fn try_from(params: &NimbusApp, value: &ManifestArgs) -> Result<Self> {
        Ok(
            match (value.manifest.clone(), params.channel(), params.app_name()) {
                (Some(manifest_file), Some(channel), _) => Self::FromFile {
                    channel,
                    manifest_file,
                },
                (_, Some(channel), Some(_)) => {
                    let github_repo = params.github_repo(&value.version)?.to_string();
                    let ref_ = params.ref_from_version(&value.version, &value.ref_)?;
                    let manifest_file = format!(
                        "@{}/{}",
                        github_repo,
                        params.manifest_location(&value.version)?,
                    );
                    Self::FromGithub {
                        channel,
                        manifest_file,
                        ref_,
                        github_repo,
                    }
                }
                _ => anyhow::bail!("A channel and either a manifest or an app is expected"),
            },
        )
    }
}

impl Display for ManifestSource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let files = self.manifest_loader().unwrap();
        let path = files.file_path(self.manifest_file()).unwrap();
        f.write_str(&path.to_string())
    }
}

impl TryFrom<&ManifestSource> for FeatureManifest {
    type Error = anyhow::Error;

    fn try_from(value: &ManifestSource) -> Result<Self> {
        let files = value.manifest_loader()?;
        let path = files.file_path(value.manifest_file())?;
        let parser: Parser = Parser::new(files, path)?;
        let manifest = parser.get_intermediate_representation(Some(value.channel()))?;
        manifest.validate_manifest()?;
        Ok(manifest)
    }
}
