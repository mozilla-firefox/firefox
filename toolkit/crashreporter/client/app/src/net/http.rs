/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Interface to HTTP requests/responses which will transparently use various backends to send the
//! data.
//!
//! This is far from a general implementation, instead implementing the sort of requests that we
//! need.

use crate::config::installation_program_path;
use crate::std::{
    self, env,
    fs::{File, OpenOptions},
    io::{Read, Seek},
    mem::ManuallyDrop,
    path::{Path, PathBuf},
    process::Child,
    sync::atomic::{AtomicUsize, Ordering::Relaxed},
};
use anyhow::Context;
use once_cell::sync::Lazy;
use serde::Serialize;

#[cfg(mock)]
use crate::std::mock::{mock_key, MockKey};

#[cfg(mock)]
mock_key! {
    /// The outer Result is for RequestBuilder::build(), the inner is for Request::send().
    pub struct MockHttp => Box<dyn Fn(&RequestBuilder, &str) -> std::io::Result<std::io::Result<Vec<u8>>> + Send + Sync>
}

#[cfg(mock)]
impl MockHttp {
    /// If returned from a MockHttp callback, other transports will be attempted.
    #[allow(unused)]
    pub fn try_others() -> std::io::Result<std::io::Result<Vec<u8>>> {
        Err(std::io::ErrorKind::Interrupted.into())
    }
}

/// The user agent used by this application.
pub fn user_agent() -> &'static str {
    static USER_AGENT: Lazy<String> = Lazy::new(|| {
        format!(
            "{}/{} ({} {})",
            env!("CARGO_PKG_NAME"),
            mozbuild::config::MOZ_APP_VERSION,
            std::env::consts::OS,
            std::env::consts::ARCH,
        )
    });
    &*USER_AGENT
}

pub(crate) struct BackgroundTaskAttempts {
    remaining: AtomicUsize,
}

impl BackgroundTaskAttempts {
    #[cfg_attr(all(mock, not(test)), allow(unused))]
    pub const fn new(count: usize) -> Self {
        BackgroundTaskAttempts {
            remaining: AtomicUsize::new(count),
        }
    }

    /// Returns whether the background task should be attempted again.
    pub fn should_attempt(&self) -> bool {
        self.remaining.load(Relaxed) != 0
    }

    /// Record a failed background task attempt.
    pub fn failed(&self) {
        self.remaining.fetch_sub(1, Relaxed);
    }

    /// Remove all background task attempts.
    pub fn drain(&self) {
        self.remaining.store(0, Relaxed);
    }
}

std::mock::mocked_static! {
    /// How many times the background task may fail before discontinuing use.
    ///
    /// The background task may repeatedly fail for many reasons, for example due to a crash or
    /// misconfigured network settings.
    static BACKGROUND_TASK_ATTEMPTS: BackgroundTaskAttempts = BackgroundTaskAttempts::new(2);
    mocked type = std::sync::Arc<BackgroundTaskAttempts>;

    impl BACKGROUND_TASK_ATTEMPTS {
        fn should_attempt(self) -> bool {
            Self.try_get(|v| v.should_attempt()).unwrap_or(true)
        }

        fn failed(self) {
            Self.try_get(|v| v.failed());
        }

        fn drain(self) {
            Self.try_get(|v| v.drain());
        }
    }
}

// TODO set reasonable connect timeout and low speed limit?

/*
 * IMPORTANT! Keep the serialized JSON format for RequestBuilder compatible with
 * toolkit/crashreporter/networking/BackgroundTask_crashNetwork.sys.mjs
 */

/// Types of requests that can be created.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(tag = "type")]
pub enum RequestBuilder<'a> {
    /// Send a POST with multiple mime parts.
    MimePost { parts: Vec<MimePart<'a>> },
    /// Send a POST.
    Post {
        body: &'a [u8],
        headers: &'a [(String, String)],
    },
}

/// A single mime part to send.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct MimePart<'a> {
    pub name: &'a str,
    pub content: MimePartContent<'a>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub filename: Option<&'a str>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub mime_type: Option<&'a str>,
}

/// The content of a mime part.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(tag = "type", content = "value")]
pub enum MimePartContent<'a> {
    /// Send a file's contents.
    File(&'a Path),
    /// Send a specific string as the contents.
    String(&'a str),
}

/// A request that is ready to be sent.
pub enum Request<'a> {
    BackgroundTaskChild {
        child: Child,
        file: TempRequestFile,
        builder: RequestBuilder<'a>,
        url: &'a str,
    },
    CurlChild {
        child: Child,
        stdin: Option<Box<dyn Read + Send + 'static>>,
    },
    LibCurl {
        easy: super::libcurl::Easy<'static>,
    },
    #[cfg(mock)]
    Mock {
        response: std::io::Result<Vec<u8>>,
    },
}

impl<'a> RequestBuilder<'a> {
    /// Build the request with the given url.
    pub fn build(&self, url: &'a str) -> std::io::Result<Request<'a>> {
        log::debug!("starting request to {url}: {self:?}");

        // When mocking is enabled, check for that first.
        #[cfg(mock)]
        if let Some(r) = self.try_send_with_mock(url) {
            return r;
        }

        // First we try to invoke a firefox background task to send the request. This is
        // preferrable because it will respect the user's network settings, however it is less
        // reliable if the cause of our crash is a bug in early firefox startup or network code.
        if BACKGROUND_TASK_ATTEMPTS.should_attempt() {
            match self.send_with_background_task(url) {
                Ok(r) => return Ok(r),
                Err(e) => {
                    log::info!(
                        "failed to invoke background task ({e}), falling back to curl backend"
                    );
                    // A failure to spawn the background task more than likely indicates it will
                    // never work in the future.
                    BACKGROUND_TASK_ATTEMPTS.drain();
                }
            };
        }

        self.send_with_curl(url)
    }

    /// Send the request with the firefox `crashreporterNetworkBackend` background task.
    fn send_with_background_task(&self, url: &'a str) -> std::io::Result<Request<'a>> {
        let path = installation_program_path(mozbuild::config::MOZ_APP_NAME);
        let mut cmd = crate::process::background_command(path);
        cmd.args(["--backgroundtask", "crashreporterNetworkBackend"]);
        cmd.arg(url);
        cmd.arg(user_agent());
        // Disable crash reporting in the background task. We don't want a crash in the background
        // task to launch another crash reporter flow. See bugs 1991491/1987145.
        cmd.env("MOZ_CRASHREPORTER_DISABLE", "1")
            .env_remove("MOZ_CRASHREPORTER");

        let mut file = TempRequestFile::new()?;
        serde_json::to_writer(&mut *file, self)?;
        cmd.arg(&file.path);

        cmd.stdin(std::process::Stdio::null());
        cmd.stdout(std::process::Stdio::piped());
        cmd.stderr(std::process::Stdio::piped());

        cmd.spawn().map(move |child| Request::BackgroundTaskChild {
            child,
            file,
            builder: self.clone(),
            url,
        })
    }

    /// Send the request with the curl backend.
    pub fn send_with_curl(&self, url: &str) -> std::io::Result<Request<'static>> {
        // Windows 10+ and macOS 10.15+ contain `curl` 7.64.1+ as a system-provided executable, so
        // `send_with_curl_executable` should not fail.
        //
        // Linux distros generally do not contain `curl`, but `libcurl` is very likely to be
        // incidentally installed (if not outright part of the distro base packages). Based on a
        // cursory look at the debian repositories as an exemplar, the curl executable (rather than
        // library) is much less likely to be incidentally installed.
        //
        // For uniformity, we always will try the curl executable first, then try libcurl if that
        // fails.

        let curl_err = match self.send_with_curl_executable(url) {
            Ok(r) => return Ok(r),
            Err(e) => e,
        };

        // When mocking is enabled, default to _not_ using libcurl (because there is no mock
        // interface; it will really use libcurl). However we add a hook to use libcurl for tests
        // which want to send real data to some server (which may be a local test server).
        #[cfg(mock)]
        if !crate::std::mock::try_hook(false, "use_system_libcurl") {
            log::error!("use_system_libcurl not enabled and curl failed: {curl_err}");
            panic!("no mock handler available to build http request");
        }

        log::info!("failed to invoke curl ({curl_err}), trying libcurl");

        self.send_with_libcurl(url)
    }

    /// Send the request with the `curl` executable.
    fn send_with_curl_executable(&self, url: &str) -> std::io::Result<Request<'static>> {
        let mut cmd = crate::process::background_command("curl");
        let mut stdin: Option<Box<dyn Read + Send + 'static>> = None;

        cmd.arg("--fail");
        cmd.args(["--user-agent", user_agent()]);

        match self {
            Self::MimePost { parts } => {
                for part in parts {
                    part.curl_command_args(&mut cmd, &mut stdin)?;
                }
            }
            Self::Post { body, headers } => {
                for (k, v) in headers.iter() {
                    cmd.args(["--header", &format!("{k}: {v}")]);
                }

                cmd.args(["--data-binary", "@-"]);
                stdin = Some(Box::new(std::io::Cursor::new(body.to_vec())));
            }
        }

        cmd.arg(url);

        cmd.stdin(std::process::Stdio::piped());
        cmd.stdout(std::process::Stdio::piped());
        cmd.stderr(std::process::Stdio::piped());

        cmd.spawn()
            .map(move |child| Request::CurlChild { child, stdin })
    }

    /// Send the request with the `curl` library.
    fn send_with_libcurl(&self, url: &str) -> std::io::Result<Request<'static>> {
        let curl = super::libcurl::load()?;
        let mut easy = curl.easy()?;

        easy.set_url(url)?;
        easy.set_user_agent(user_agent())?;
        easy.set_max_redirs(30)?;

        match self {
            Self::MimePost { parts } => {
                let mut mime = easy.mime()?;

                for part in parts {
                    part.curl_mime(&mut mime)?;
                }

                easy.set_mime_post(mime)?;
            }
            Self::Post { body, headers } => {
                let mut header_list = easy.slist();
                for (k, v) in headers.iter() {
                    header_list.append(&format!("{k}: {v}"))?;
                }
                easy.set_headers(header_list)?;

                easy.set_postfields(*body)?;
            }
        }

        Ok(Request::LibCurl { easy })
    }

    #[cfg(mock)]
    fn try_send_with_mock(&self, url: &str) -> Option<std::io::Result<Request<'static>>> {
        let result = MockHttp.try_get(|f| f(self, url))?;
        if result
            .as_ref()
            .err()
            .map(|e| e.kind() == std::io::ErrorKind::Interrupted)
            .unwrap_or(false)
        {
            None
        } else {
            Some(result.map(|response| Request::Mock { response }))
        }
    }
}

pub struct TempRequestFile {
    path: PathBuf,
    file: ManuallyDrop<File>,
}

static REQUEST_NUM: std::sync::atomic::AtomicUsize = std::sync::atomic::AtomicUsize::new(0);

impl TempRequestFile {
    fn new() -> std::io::Result<Self> {
        // Incorporate process id and a process-unique id to avoid runtime conflicts.
        let path = std::env::temp_dir().join(format!(
            "{}{}-request{}.json",
            env!("CARGO_PKG_NAME"),
            std::process::id(),
            REQUEST_NUM.fetch_add(1, std::sync::atomic::Ordering::Relaxed)
        ));
        if let Some(p) = path.parent() {
            std::fs::create_dir_all(p)?;
        }
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(true)
            .open(&path)?;
        Ok(TempRequestFile {
            path,
            file: ManuallyDrop::new(file),
        })
    }
}

impl std::ops::Deref for TempRequestFile {
    type Target = File;
    fn deref(&self) -> &Self::Target {
        &self.file
    }
}

impl std::ops::DerefMut for TempRequestFile {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.file
    }
}

impl Drop for TempRequestFile {
    fn drop(&mut self) {
        // # Safety
        // We do not use self.file after this
        unsafe { ManuallyDrop::drop(&mut self.file) };
        let _ = std::fs::remove_file(&self.path);
    }
}

impl MimePart<'_> {
    fn curl_command_args(
        &self,
        cmd: &mut crate::std::process::Command,
        stdin: &mut Option<Box<dyn Read + Send + 'static>>,
    ) -> std::io::Result<()> {
        use std::fmt::Write;
        let mut formarg = format!("{}=", self.name);
        match self.content {
            MimePartContent::File(f) => {
                write!(formarg, "@{}", CurlQuote(&f.display().to_string())).unwrap()
            }
            MimePartContent::String(s) => {
                // `@-` causes the data to be read from stdin, which is desirable to
                // not have to worry about process argument string length limitations
                // (though they are generally pretty high limits).
                write!(formarg, "@-").unwrap();
                if stdin
                    .replace(Box::new(std::io::Cursor::new(s.to_owned())))
                    .is_some()
                {
                    return Err(std::io::Error::other(
                        "only one MimePartContent::String supported",
                    ));
                }
            }
        }
        if let Some(filename) = self.filename {
            write!(formarg, ";filename={}", filename).unwrap();
        }
        if let Some(mime_type) = self.mime_type {
            write!(formarg, ";type={}", mime_type).unwrap();
        }

        cmd.arg("--form");
        cmd.arg(formarg);

        Ok(())
    }

    fn curl_mime(&self, mime: &mut super::libcurl::Mime) -> std::io::Result<()> {
        let mut p = mime.add_part()?;
        p.set_name(&self.name)?;
        match self.content {
            MimePartContent::File(f) => {
                p.set_filename(&f.display().to_string())?;
                p.set_filedata(f)?;
            }
            MimePartContent::String(s) => {
                p.set_data(s.as_bytes())?;
            }
        }
        if let Some(filename) = self.filename {
            p.set_filename(filename)?;
        }
        if let Some(mime_type) = self.mime_type {
            p.set_type(mime_type)?;
        }
        Ok(())
    }
}

impl Request<'_> {
    /// Send the request, returning the response body (if any).
    pub fn send(self) -> anyhow::Result<Vec<u8>> {
        Ok(match self {
            Self::BackgroundTaskChild {
                child,
                mut file,
                builder,
                url,
            } => {
                (move || {
                    let output = child
                        .wait_with_output()
                        .context("failed to wait on background task process")?;

                    if !output.status.success() {
                        BACKGROUND_TASK_ATTEMPTS.failed();
                        if !BACKGROUND_TASK_ATTEMPTS.should_attempt() {
                            log::info!("the background task process has exceeded the acceptable number of failures and will no longer be used");
                        }

                        anyhow::bail!(
                            "process failed (exit status {}) with stderr: {}",
                            output.status,
                            String::from_utf8_lossy(&output.stderr)
                        );
                    }

                    file.rewind().context("failed to rewind response file")?;
                    let mut ret = Vec::new();
                    file.read_to_end(&mut ret)
                        .context("failed to read response file")?;
                    Ok(ret)
                })()
                .or_else(|e| {
                    // If any error occurs, we try again with the curl backend. In theory we
                    // could do this specifically if the process returns a failure exit code,
                    // however since we want to give the best effort in sending the request, we
                    // do it for any failure.
                    log::error!("background task error: {e:#}");
                    log::info!("falling back to curl backend");

                    builder.send_with_curl(url).context("curl error")?.send()
                })?
            }
            Self::CurlChild { mut child, stdin } => {
                if let Some(mut stdin) = stdin {
                    let mut child_stdin = child
                        .stdin
                        .take()
                        .context("failed to get curl process stdin")?;
                    std::io::copy(&mut stdin, &mut child_stdin)
                        .context("failed to write to stdin of curl process")?;
                    // stdin is dropped at the end of this scope so that the stream gets an EOF,
                    // otherwise curl will wait for more input.
                }
                let output = child
                    .wait_with_output()
                    .context("failed to wait on curl process")?;
                anyhow::ensure!(
                    output.status.success(),
                    "process failed (exit status {}) with stderr: {}",
                    output.status,
                    String::from_utf8_lossy(&output.stderr)
                );
                output.stdout
            }
            Self::LibCurl { easy } => {
                let response = easy.perform()?;
                let response_code = easy.get_response_code()?;

                anyhow::ensure!(
                    response_code == 200,
                    "unexpected response code ({response_code}): {}",
                    String::from_utf8_lossy(&response).as_ref()
                );

                response
            }
            #[cfg(mock)]
            Self::Mock { response } => response?,
        })
    }
}

/// Quote a string per https://curl.se/docs/manpage.html#-F.
/// That is, add quote characters and escape " and \ with backslashes.
struct CurlQuote<'a>(&'a str);

impl CurlQuote<'_> {
    fn quoted(&self) -> String {
        let quote = std::iter::once(&b'"');
        let escaped = self.0.as_bytes().iter().flat_map(|b| match b {
            b'"' => br#"\""#.as_slice(),
            b'\\' => br#"\\"#.as_slice(),
            other => std::slice::from_ref(other),
        });
        let bytes = quote.clone().chain(escaped).chain(quote).copied().collect();
        // # Safety
        // The source bytes came from a `str`, so must have been valid utf8. We are inserting valid
        // utf8 characters (quotes and backslashes, which are single bytes in utf8) at character
        // boundaries (at the beginning/end of the string and before other backslash/quote
        // characters).
        unsafe { String::from_utf8_unchecked(bytes) }
    }
}

impl std::fmt::Display for CurlQuote<'_> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.quoted())
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn curl_quote() {
        assert_eq!(
            CurlQuote(r#"hello "world""#).to_string(),
            r#""hello \"world\"""#
        );
        assert_eq!(
            CurlQuote(r#"C:\dir\\"\""dir""#).to_string(),
            r#""C:\\dir\\\\\"\\\"\"dir\"""#
        );
    }
}
