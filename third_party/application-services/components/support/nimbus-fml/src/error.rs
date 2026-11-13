/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * */

use crate::intermediate_representation::ModuleId;

#[derive(Debug, thiserror::Error)]
pub enum FMLError {
    #[error("IO error: {0}")]
    IOError(#[from] std::io::Error),
    #[error("JSON Error: {0}")]
    JSONError(#[from] serde_json::Error),
    #[error("YAML Error: {0}")]
    YAMLError(#[from] serde_yaml::Error),
    #[error("URL Error: {0}")]
    UrlError(#[from] url::ParseError),
    #[error("Regex Error: {0}")]
    RegexError(#[from] regex::Error),

    #[error("Fetch Error: {0}")]
    FetchError(#[from] viaduct::ViaductError),
    #[error("Invalid Response Status: {0}")]
    ResponseStatusError(#[from] viaduct::UnexpectedStatus),
    #[error("UTF8 Decoding Error: {0}")]
    Utf8DecodingError(#[from] std::string::FromUtf8Error),

    #[error("Can't find file: {0}")]
    InvalidPath(String),

    #[error("Unexpected template problem: {0}")]
    TemplateProblem(#[from] askama::Error),

    #[error("Fatal error: {0}")]
    Fatal(#[from] anyhow::Error),

    #[error("Internal error: {0}")]
    InternalError(&'static str),
    #[error("Validation Error at {0}: {1}")]
    ValidationError(String, String),
    #[error("Type Parsing Error: {0}")]
    TypeParsingError(String),
    #[error("Invalid Channel error: The channel `{0}` is specified, but only {1:?} are supported for the file")]
    InvalidChannelError(String, Vec<String>),

    #[error("Problem with {0}: {1}")]
    FMLModuleError(ModuleId, String),

    #[error("{0}")]
    CliError(String),

    #[cfg(feature = "client-lib")]
    #[error("{0}")]
    ClientError(#[from] ClientError),

    #[error("Feature `{0}` not found on manifest")]
    InvalidFeatureError(String),

    #[error("Invalid API token GITHUB_BEARER_TOKEN")]
    InvalidApiToken,
}

#[cfg(feature = "client-lib")]
#[derive(Debug, thiserror::Error)]
pub enum ClientError {
    #[error("{0}")]
    InvalidFeatureConfig(String),
    #[error("{0}")]
    InvalidFeatureId(String),
    #[error("{0}")]
    InvalidFeatureValue(String),
    #[error("{0}")]
    JsonMergeError(String),
}

pub type Result<T, E = FMLError> = std::result::Result<T, E>;
