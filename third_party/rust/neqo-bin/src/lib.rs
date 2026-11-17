// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg_attr(coverage_nightly, feature(coverage_attribute))]

use std::{
    fs::OpenOptions,
    io::Write as _,
    net::{SocketAddr, ToSocketAddrs as _},
    path::{Component, Path, PathBuf},
    time::{Duration, Instant},
};

use clap::{Parser, builder::TypedValueParser as _};
use neqo_transport::{
    CongestionControl, ConnectionParameters, DEFAULT_INITIAL_RTT, SlowStart, Stats, StreamType,
    Version, tparams::PreferredAddress,
};
use strum::VariantNames as _;
use thiserror::Error;
use tracing::{error, info, level_filters::LevelFilter};

pub mod client;
mod send_data;
pub mod server;
pub mod udp;

/// Firefox default value
///
/// See `network.buffer.cache.size` pref <https://searchfox.org/mozilla-central/rev/f6e3b81aac49e602f06c204f9278da30993cdc8a/modules/libpref/init/all.js#3212>
const STREAM_IO_BUFFER_SIZE: usize = 32 * 1024;

#[derive(Clone, Debug, Parser)]
pub struct SharedArgs {
    #[command(flatten)]
    verbose: Option<clap_verbosity_flag::Verbosity>,

    #[arg(short = 'a', long, default_value = "h3")]
    /// ALPN labels to negotiate.
    ///
    /// This client still only does HTTP/3 no matter what the ALPN says.
    alpn: String,

    #[arg(name = "qlog-dir", long, value_parser=clap::value_parser!(PathBuf))]
    /// Enable QLOG logging and QLOG traces to this directory
    qlog_dir: Option<PathBuf>,

    #[arg(name = "encoder-table-size", long, default_value = "16384")]
    max_table_size_encoder: u64,

    #[arg(name = "decoder-table-size", long, default_value = "16384")]
    max_table_size_decoder: u64,

    #[arg(name = "max-blocked-streams", short = 'b', long, default_value = "10")]
    max_blocked_streams: u16,

    #[arg(short = 'c', long, number_of_values = 1)]
    /// The set of TLS cipher suites to enable.
    /// From: `TLS_AES_128_GCM_SHA256`, `TLS_AES_256_GCM_SHA384`, `TLS_CHACHA20_POLY1305_SHA256`.
    ciphers: Vec<String>,

    #[arg(name = "qns-test", long)]
    /// Enable special behavior for use with QUIC Network Simulator
    qns_test: Option<String>,

    #[command(flatten)]
    quic_parameters: QuicParameters,

    /// Log connection stats as JSON when a connection closes.
    #[arg(name = "stats", long)]
    stats: bool,

    /// Append connection stats to this file as JSON, one record per line. Implies `--stats`.
    #[arg(name = "stats-file", long)]
    stats_file: Option<PathBuf>,
}

#[cfg(any(test, feature = "bench"))]
impl Default for SharedArgs {
    fn default() -> Self {
        Self {
            verbose: None,
            alpn: "h3".into(),
            qlog_dir: None,
            max_table_size_encoder: 16384,
            max_table_size_decoder: 16384,
            max_blocked_streams: 10,
            ciphers: vec![],
            qns_test: None,
            quic_parameters: QuicParameters::default(),
            stats: false,
            stats_file: None,
        }
    }
}

impl SharedArgs {
    fn level_filter(&self) -> Option<LevelFilter> {
        use clap_verbosity_flag::VerbosityFilter;
        self.verbose.as_ref().map(|v| match v.filter() {
            VerbosityFilter::Off => LevelFilter::OFF,
            VerbosityFilter::Error => LevelFilter::ERROR,
            VerbosityFilter::Warn => LevelFilter::WARN,
            VerbosityFilter::Info => LevelFilter::INFO,
            VerbosityFilter::Debug => LevelFilter::DEBUG,
            VerbosityFilter::Trace => LevelFilter::TRACE,
        })
    }

    #[must_use]
    pub fn get_alpn(&self) -> &str {
        &self.alpn
    }

    /// Whether to report connection stats; `--stats-file` implies `--stats`.
    const fn stats_enabled(&self) -> bool {
        self.stats || self.stats_file.is_some()
    }
}

#[derive(Clone, Debug, Parser)]
#[expect(
    clippy::struct_excessive_bools,
    reason = "A config struct is not a good use of that lint."
)]
pub struct QuicParameters {
    #[arg(
        short = 'Q',
        long,
        num_args = 1..,
        value_delimiter = ' ',
        number_of_values = 1,
        value_parser = from_str)]
    /// A list of versions to support, in hex.
    /// The first is the version to attempt.
    /// Adding multiple values adds versions in order of preference.
    /// If the first listed version appears in the list twice, the position
    /// of the second entry determines the preference order of that version.
    pub quic_version: Vec<Version>,

    #[arg(long, default_value = "16")]
    /// Set the `MAX_STREAMS_BIDI` limit.
    pub max_streams_bidi: u64,

    #[arg(long, default_value = "16")]
    /// Set the `MAX_STREAMS_UNI` limit.
    pub max_streams_uni: u64,

    #[arg(long = "idle", default_value = "30")]
    /// The idle timeout for connections, in seconds.
    pub idle_timeout: u64,

    #[arg(long = "init_rtt", default_value_t = DEFAULT_INITIAL_RTT.as_millis() as u64)]
    /// The initial round-trip time, in milliseconds.
    pub initial_rtt_ms: u64,

    #[arg(long = "cc", default_value = "cubic",
        value_parser = clap::builder::PossibleValuesParser::new(CongestionControl::VARIANTS)
            .map(|s| s.parse::<CongestionControl>().unwrap()))]
    /// The congestion control algorithm to use.
    pub congestion_control: CongestionControl,

    #[arg(long = "ss", default_value = "classic",
        value_parser = clap::builder::PossibleValuesParser::new(SlowStart::VARIANTS)
            .map(|s| s.parse::<SlowStart>().unwrap()))]
    /// The slow start algorithm to use.
    pub slow_start: SlowStart,

    #[arg(long = "no-pacing")]
    /// Whether to disable pacing.
    pub no_pacing: bool,

    #[arg(long)]
    /// Whether to disable path MTU discovery.
    pub no_pmtud: bool,

    #[arg(long)]
    /// Whether to slice the SNI.
    pub no_sni_slicing: bool,

    #[arg(long)]
    /// Whether to disable Alternative Backoff with ECN (RFC 8511).
    pub no_abe: bool,

    #[arg(name = "preferred-address-v4", long)]
    /// An IPv4 address for the server preferred address.
    pub preferred_address_v4: Option<String>,

    #[arg(name = "preferred-address-v6", long)]
    /// An IPv6 address for the server preferred address.
    pub preferred_address_v6: Option<String>,
}

#[cfg(any(test, feature = "bench"))]
impl Default for QuicParameters {
    fn default() -> Self {
        Self {
            quic_version: vec![],
            max_streams_bidi: 16,
            max_streams_uni: 16,
            idle_timeout: 30,
            initial_rtt_ms: u64::try_from(DEFAULT_INITIAL_RTT.as_millis())
                .expect("this value will always be less than u64::MAX"),
            congestion_control: CongestionControl::Cubic,
            slow_start: SlowStart::Classic,
            no_pacing: false,
            no_pmtud: false,
            preferred_address_v4: None,
            preferred_address_v6: None,
            no_sni_slicing: false,
            no_abe: false,
        }
    }
}

impl QuicParameters {
    fn get_sock_addr<F>(opt: Option<&String>, v: &str, f: F) -> Option<SocketAddr>
    where
        F: FnMut(&SocketAddr) -> bool,
    {
        let addr = opt
            .iter()
            .filter_map(|spa| spa.to_socket_addrs().ok())
            .flatten()
            .find(f);
        assert_eq!(
            opt.is_some(),
            addr.is_some(),
            "unable to resolve '{:?}' to an {v} address",
            opt.as_ref()?,
        );
        addr
    }

    #[must_use]
    pub fn preferred_address_v4(&self) -> Option<SocketAddr> {
        Self::get_sock_addr(
            self.preferred_address_v4.as_ref(),
            "IPv4",
            SocketAddr::is_ipv4,
        )
    }

    #[must_use]
    pub fn preferred_address_v6(&self) -> Option<SocketAddr> {
        Self::get_sock_addr(
            self.preferred_address_v6.as_ref(),
            "IPv6",
            SocketAddr::is_ipv6,
        )
    }

    #[must_use]
    pub fn preferred_address(&self) -> Option<PreferredAddress> {
        let v4 = self.preferred_address_v4();
        let v6 = self.preferred_address_v6();
        if v4.is_none() && v6.is_none() {
            None
        } else {
            let v4 = v4.map(|v4| {
                let SocketAddr::V4(v4) = v4 else {
                    unreachable!();
                };
                v4
            });
            let v6 = v6.map(|v6| {
                let SocketAddr::V6(v6) = v6 else {
                    unreachable!();
                };
                v6
            });
            Some(PreferredAddress::new(v4, v6))
        }
    }

    #[must_use]
    pub fn get(&self, alpn: &str) -> ConnectionParameters {
        let mut params = ConnectionParameters::default()
            .max_streams(StreamType::BiDi, self.max_streams_bidi)
            .max_streams(StreamType::UniDi, self.max_streams_uni)
            .idle_timeout(Duration::from_secs(self.idle_timeout))
            .initial_rtt(Duration::from_millis(self.initial_rtt_ms))
            .congestion_control(self.congestion_control)
            .slow_start(self.slow_start)
            .pacing(!self.no_pacing)
            .pmtud(!self.no_pmtud)
            .sni_slicing(!self.no_sni_slicing)
            .abe(!self.no_abe);
        params = if let Some(pa) = self.preferred_address() {
            params.preferred_address(pa)
        } else {
            params
        };
        if let Some(&first) = self.quic_version.first() {
            let all = if self.quic_version[1..].contains(&first) {
                &self.quic_version[1..]
            } else {
                &self.quic_version
            };
            params.versions(first, all.to_vec())
        } else {
            let version = match alpn {
                "h3" | "hq-interop" => Version::Version1,
                #[cfg(feature = "draft-29")]
                "h3-29" | "hq-29" => Version::Draft29,
                _ => Version::default(),
            };
            params.versions(version, Version::all())
        }
    }
}

fn from_str(s: &str) -> Result<Version, Error> {
    let v = u32::from_str_radix(s, 16)
        .map_err(|_| Error::Argument("versions need to be specified in hex"))?;
    Version::try_from(v).map_err(|_| Error::Argument("unknown version"))
}

#[derive(Debug, Error)]
pub enum Error {
    #[error("Error: {0}")]
    Argument(&'static str),
}

/// Wrapper for [`Instant::now()`] to manage the `disallowed_methods` override.
fn now() -> Instant {
    #![expect(clippy::disallowed_methods, reason = "This program uses the time")]
    Instant::now()
}

/// Report `stats` as JSON: a line appended to `path`, or indented into the log.
pub(crate) fn report_stats(stats: &Stats, path: Option<&Path>) {
    let json = if path.is_some() {
        serde_json::to_string(stats)
    } else {
        serde_json::to_string_pretty(stats)
    };
    let mut json = match json {
        Ok(json) => json,
        Err(e) => {
            error!("Failed to serialize stats: {e}");
            return;
        }
    };
    let Some(path) = path else {
        info!("{json}");
        return;
    };
    json.push('\n'); // Append the record in one write.
    if let Err(e) = OpenOptions::new()
        .create(true)
        .append(true)
        .open(path)
        .and_then(|mut f| f.write_all(json.as_bytes()))
    {
        error!("Failed to report stats to {}: {e}", path.display());
    }
}

/// Join `path` onto `base`, or `None` if `path` has a component other than a plain name.
pub(crate) fn contained_path(base: &Path, path: &str) -> Option<PathBuf> {
    let path = Path::new(path);
    path.components()
        .all(|c| matches!(c, Component::Normal(_)))
        .then(|| base.join(path))
}

/// A directory that deletes itself.
#[cfg(test)]
pub(crate) mod temp_dir {
    use std::{
        fs,
        path::PathBuf,
        sync::atomic::{AtomicUsize, Ordering},
        time::SystemTime,
    };

    pub struct TempDir {
        path: PathBuf,
    }

    impl TempDir {
        pub fn new() -> Self {
            // The name has to be unique per instance and process.
            static SEQ: AtomicUsize = AtomicUsize::new(0);
            let dir = std::env::temp_dir().join(format!(
                "neqo-bin-test-{}-{}-{}",
                std::process::id(),
                SystemTime::now()
                    .duration_since(SystemTime::UNIX_EPOCH)
                    .unwrap()
                    .as_millis(),
                SEQ.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir_all(&dir).unwrap();
            Self { path: dir }
        }

        pub fn path(&self) -> PathBuf {
            self.path.clone()
        }
    }

    impl Drop for TempDir {
        fn drop(&mut self) {
            if self.path.exists() {
                fs::remove_dir_all(&self.path).unwrap();
            }
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::{fs, path::Path};

    use crate::{Stats, client, contained_path, report_stats, server, temp_dir::TempDir};

    #[tokio::test]
    #[cfg_attr(target_os = "netbsd", ignore = "FIXME: Test fails on NetBSD.")]
    async fn write_qlog_file() {
        test_fixture::fixture_init();

        let temp_dir = TempDir::new();

        let mut client_args = client::Args::new(None, 1, 0, 1);
        client_args.set_qlog_dir(temp_dir.path());
        let mut server_args = server::Args::default();
        server_args.set_qlog_dir(temp_dir.path());

        let client = client::client(client_args);
        let (server, _local_addrs) = server::run(server_args).unwrap();
        tokio::select! {
            _ = client => {}
            res = server  => panic!("expect server not to terminate: {res:?}"),
        };

        // Verify that the directory contains two non-empty files
        let entries: Vec<_> = fs::read_dir(temp_dir.path())
            .unwrap()
            .filter_map(Result::ok)
            .collect();
        assert_eq!(entries.len(), 2, "expect 2 files in the directory");

        for entry in entries {
            let metadata = entry.metadata().unwrap();
            assert!(metadata.is_file(), "expect a file, found something else");
            assert!(metadata.len() > 0, "expect file not be empty");
        }
    }

    #[test]
    fn report_stats_appends_json_lines() {
        let dir = TempDir::new();
        let path = dir.path().join("stats.json");

        report_stats(&Stats::default(), Some(&path));
        let first = fs::read_to_string(&path).unwrap();
        report_stats(&Stats::default(), Some(&path));
        let second = fs::read_to_string(&path).unwrap();

        assert!(second.starts_with(&first), "must append, not truncate");
        let lines: Vec<_> = second.lines().collect();
        assert_eq!(lines.len(), 2, "one record per call");
        for line in lines {
            serde_json::from_str::<serde_json::Value>(line).expect("each line is a JSON record");
        }
    }

    #[test]
    fn report_stats_to_unwritable_path_is_not_fatal() {
        let dir = TempDir::new();
        report_stats(&Stats::default(), Some(&dir.path()));
    }

    #[test]
    fn contained_path_stays_in_base() {
        let base = Path::new("base");
        assert_eq!(contained_path(base, "a/b"), Some(base.join("a").join("b")));
        for path in ["..", "a/../../b", "/a"] {
            assert_eq!(contained_path(base, path), None, "{path}");
        }
    }

    #[test]
    #[cfg(windows)]
    fn contained_path_rejects_windows_separators_and_prefixes() {
        for path in [r"..\a", r"a\..\..\b", r"C:\a", "C:a", r"\\s\h\a"] {
            assert_eq!(contained_path(Path::new("base"), path), None, "{path}");
        }
    }
}
