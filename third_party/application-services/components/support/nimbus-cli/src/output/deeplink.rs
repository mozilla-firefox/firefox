// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use anyhow::{anyhow, Result};
use percent_encoding::{AsciiSet, CONTROLS};

use crate::protocol::StartAppProtocol;
use crate::{AppOpenArgs, LaunchableApp};

impl LaunchableApp {
    pub(crate) fn copy_to_clipboard(
        &self,
        app_protocol: &StartAppProtocol,
        open: &AppOpenArgs,
    ) -> Result<usize> {
        let url = self.longform_url(app_protocol, open)?;
        let len = url.len();
        if let Err(e) = set_clipboard(url) {
            anyhow::bail!("Can't copy URL to clipboard: {}", e)
        };

        Ok(len)
    }

    pub(crate) fn longform_url(
        &self,
        app_protocol: &StartAppProtocol,
        open: &AppOpenArgs,
    ) -> Result<String> {
        let deeplink = match (&open.deeplink, self.app_opening_deeplink()) {
            (Some(deeplink), _) => deeplink.to_owned(),
            (_, Some(deeplink)) => join_query(deeplink, "--nimbus-cli&--is-launcher"),
            _ => anyhow::bail!("A deeplink must be provided"),
        };

        let url = longform_deeplink_url(deeplink.as_str(), app_protocol)?;

        self.prepend_scheme(url.as_str())
    }

    fn app_opening_deeplink(&self) -> Option<&str> {
        match self {
            Self::Android { open_deeplink, .. } => open_deeplink.as_deref(),
            Self::Ios { .. } => Some("noop"),
        }
    }

    pub(crate) fn deeplink(&self, open: &AppOpenArgs) -> Result<Option<String>> {
        let deeplink = &open.deeplink;
        if deeplink.is_none() {
            return Ok(None);
        }
        let deeplink = self.prepend_scheme(deeplink.as_ref().unwrap())?;
        Ok(Some(deeplink))
    }

    fn prepend_scheme(&self, deeplink: &str) -> Result<String> {
        Ok(if deeplink.contains("://") {
            deeplink.to_string()
        } else {
            let scheme = self.mandatory_scheme()?;
            format!("{scheme}://{deeplink}")
        })
    }

    fn mandatory_scheme(&self) -> Result<&str> {
        match self {
            Self::Android { scheme, .. } | Self::Ios { scheme, .. } => scheme
                .as_deref()
                .ok_or_else(|| anyhow!("A scheme is not defined for this app")),
        }
    }
}

// The following are the special query percent encode set.
// https://url.spec.whatwg.org/#query-percent-encode-set
const QUERY: &AsciiSet = &CONTROLS
    .add(b' ')
    .add(b'"')
    .add(b'<')
    .add(b'>')
    .add(b'#')
    .add(b'\'')
    // Additionally, we've added '{' and '}' to make  sure iOS simctl works with it.
    .add(b'{')
    .add(b'}')
    // Then some belt and braces: we're quoting a single query attribute value.
    .add(b':')
    .add(b'/')
    .add(b'?')
    .add(b'&');

/// Construct a URL from the deeplink and the protocol object.
pub(crate) fn longform_deeplink_url(
    deeplink: &str,
    app_protocol: &StartAppProtocol,
) -> Result<String> {
    let StartAppProtocol {
        reset_db,
        experiments,
        log_state,
        jexl_expression,
    } = app_protocol;
    if !reset_db && experiments.is_none() && !log_state && jexl_expression.is_none() {
        return Ok(deeplink.to_string());
    }

    let mut parts: Vec<_> = Default::default();
    if !deeplink.contains("--nimbus-cli") {
        parts.push("--nimbus-cli".to_string());
    }
    if let Some(v) = experiments {
        let json = serde_json::to_string(v)?;
        let string = percent_encoding::utf8_percent_encode(&json, QUERY).to_string();
        parts.push(format!("--experiments={string}"));
    }

    if *reset_db {
        parts.push("--reset-db".to_string());
    }
    if *log_state {
        parts.push("--log-state".to_string());
    }
    if let Some(jexl_expr) = jexl_expression {
        let string = percent_encoding::utf8_percent_encode(jexl_expr, QUERY).to_string();
        parts.push(format!("--eval-jexl={string}"));
    }

    Ok(join_query(deeplink, &parts.join("&")))
}

fn join_query(url: &str, item: &str) -> String {
    let suffix = if url.contains('?') { '&' } else { '?' };
    format!("{url}{suffix}{item}")
}

fn set_clipboard(contents: String) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    use copypasta::{ClipboardContext, ClipboardProvider};
    let mut ctx = ClipboardContext::new()?;
    ctx.set_contents(contents)?;
    Ok(())
}

#[cfg(test)]
mod unit_tests {

    use super::*;
    use serde_json::json;

    #[test]
    fn test_url_noop() -> Result<()> {
        let p = StartAppProtocol {
            reset_db: false,
            experiments: None,
            log_state: false,
            jexl_expression: None,
        };
        assert_eq!("host".to_string(), longform_deeplink_url("host", &p)?);
        assert_eq!(
            "host?query=1".to_string(),
            longform_deeplink_url("host?query=1", &p)?
        );
        Ok(())
    }

    #[test]
    fn test_url_reset_db() -> Result<()> {
        let p = StartAppProtocol {
            reset_db: true,
            experiments: None,
            log_state: false,
            jexl_expression: None,
        };
        assert_eq!(
            "host?--nimbus-cli&--reset-db".to_string(),
            longform_deeplink_url("host", &p)?
        );
        assert_eq!(
            "host?query=1&--nimbus-cli&--reset-db".to_string(),
            longform_deeplink_url("host?query=1", &p)?
        );

        Ok(())
    }

    #[test]
    fn test_url_log_state() -> Result<()> {
        let p = StartAppProtocol {
            reset_db: false,
            experiments: None,
            log_state: true,
            jexl_expression: None,
        };
        assert_eq!(
            "host?--nimbus-cli&--log-state".to_string(),
            longform_deeplink_url("host", &p)?
        );
        assert_eq!(
            "host?query=1&--nimbus-cli&--log-state".to_string(),
            longform_deeplink_url("host?query=1", &p)?
        );

        Ok(())
    }

    #[test]
    fn test_url_experiments() -> Result<()> {
        let v = json!({"data": []});
        let p = StartAppProtocol {
            reset_db: false,
            experiments: Some(&v),
            log_state: false,
            jexl_expression: None,
        };
        assert_eq!(
            "host?--nimbus-cli&--experiments=%7B%22data%22%3A[]%7D".to_string(),
            longform_deeplink_url("host", &p)?
        );
        assert_eq!(
            "host?query=1&--nimbus-cli&--experiments=%7B%22data%22%3A[]%7D".to_string(),
            longform_deeplink_url("host?query=1", &p)?
        );

        Ok(())
    }

    #[test]
    fn test_url_jexl_expression() -> Result<()> {
        let p = StartAppProtocol {
            reset_db: false,
            experiments: None,
            log_state: false,
            jexl_expression: Some("locale == 'en-US'"),
        };
        assert_eq!(
            "host?--nimbus-cli&--eval-jexl=locale%20==%20%27en-US%27".to_string(),
            longform_deeplink_url("host", &p)?
        );
        assert_eq!(
            "host?query=1&--nimbus-cli&--eval-jexl=locale%20==%20%27en-US%27".to_string(),
            longform_deeplink_url("host?query=1", &p)?
        );

        Ok(())
    }

    #[test]
    fn test_deeplink_has_is_launcher_param_if_no_deeplink_is_specified() -> Result<()> {
        let app =
            LaunchableApp::try_from_app_channel_device(Some("fenix"), Some("developer"), None)?;

        // No payload, or command line param for deeplink.
        let payload: StartAppProtocol = Default::default();
        let open: AppOpenArgs = Default::default();
        assert_eq!(
            "fenix-dev://open?--nimbus-cli&--is-launcher".to_string(),
            app.longform_url(&payload, &open)?
        );

        // A command line param for deeplink.
        let open = AppOpenArgs {
            deeplink: Some("deeplink".to_string()),
            ..Default::default()
        };
        assert_eq!(
            "fenix-dev://deeplink".to_string(),
            app.longform_url(&payload, &open)?
        );

        // A parameter from the payload, but no deeplink.
        let payload = StartAppProtocol {
            log_state: true,
            ..Default::default()
        };
        assert_eq!(
            "fenix-dev://open?--nimbus-cli&--is-launcher&--log-state".to_string(),
            app.longform_url(&payload, &Default::default())?
        );

        // A deeplink from the command line, and an extra param from the payload.
        let open = AppOpenArgs {
            deeplink: Some("deeplink".to_string()),
            ..Default::default()
        };
        assert_eq!(
            "fenix-dev://deeplink?--nimbus-cli&--log-state".to_string(),
            app.longform_url(&payload, &open)?
        );

        Ok(())
    }
}
