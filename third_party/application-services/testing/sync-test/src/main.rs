/* Any copyright is dedicated to the Public Domain.
http://creativecommons.org/publicdomain/zero/1.0/ */

#![allow(unknown_lints)]
#![warn(rust_2018_idioms)]

use clap::Parser;
use cli_support::fxa_creds::{get_default_fxa_config, CliFxa, SYNC_SCOPE};
use nss_as::ensure_initialized;
use std::sync::Arc;
use std::{collections::HashSet, process};

mod auth;
mod autofill;
mod logins;
mod sync15;
mod tabs;
mod testing;

use crate::auth::{FxaConfigUrl, TestUser};
use crate::testing::TestGroup;

macro_rules! cleanup_clients {
    ($($client:expr),+) => {
        crate::auth::cleanup_server(vec![$($client),+]).expect("Remote cleanup failed");
        $($client.fully_reset_local_db().expect("Failed to reset client");)+
    };
}

pub fn init_testing() {
    viaduct_backend_rust::viaduct_init_backend_rust();
    ensure_initialized();

    // Enable backtraces.
    std::env::set_var("RUST_BACKTRACE", "1");
    // Turn on trace logging for everything except for a few crates (mostly from
    // our network stack) that happen to be particularly noisy (even on `info`
    // level), which get turned on at the warn level. This can still be
    // overridden with RUST_LOG, however.
    let log_filter = "trace,\
        reqwest=info,hyper=warn,want=warn,mio=warn,\
        tokio=warn,tokio_threadpool=warn,tokio_reactor=warn,tokio_core=warn,\
        trust_dns_proto=warn,trust_dns_resolver=warn";
    env_logger::init_from_env(env_logger::Env::default().filter_or("RUST_LOG", log_filter));
    // also need our local tracing-support log initialize, not sure how you might bump these to trace!?
    cli_support::init_logging_with("tabs=debug,sync15=debug,sync_manager=debug");
}

// Runs each test group with a fresh Firefox account.
pub fn run_test_groups(opts: &Opts, groups: Vec<TestGroup>) {
    let all_names = groups
        .iter()
        .map(|group| group.name)
        .collect::<HashSet<_>>();
    let requested_names = if opts.groups.is_empty() {
        all_names.clone()
    } else {
        opts.groups
            .iter()
            .map(|name| name.as_str())
            .collect::<HashSet<_>>()
    };
    let unsupported_names = requested_names.difference(&all_names).collect::<Vec<_>>();
    if !unsupported_names.is_empty() {
        log::error!("+ Unknown test groups: {:?}", unsupported_names);
        process::exit(1);
    }
    let groups = groups
        .into_iter()
        .filter(|group| requested_names.contains(&group.name))
        .collect::<Vec<_>>();
    log::info!("+ Testing {} groups", groups.len());
    for group in groups {
        run_test_group(opts, group);
    }
    log::info!("+ Test groups finished");
}

pub fn run_test_group(opts: &Opts, group: TestGroup) {
    if opts.helper_debug {
        // What are these used for?
        std::env::set_var("DEBUG", "nightmare");
        std::env::set_var("HELPER_SHOW_BROWSER", "1");
    }

    let cfg = get_default_fxa_config();
    let mut cli = CliFxa::new(cfg, opts.credential_file.as_deref()).expect("can't create CliFxa");
    cli.ensure_logged_in(&[SYNC_SCOPE]).expect("can't log in");
    let acct = Arc::new(cli);

    let mut user = TestUser::new(acct, 2).expect("Failed to get test user.");
    let (c0, c1) = {
        let (c0s, c1s) = user.clients.split_at_mut(1);
        (&mut c0s[0], &mut c1s[0])
    };
    log::info!("++ TestGroup begin {}", group.name);
    // The account is reused across runs, and a failed test skips cleanup, so start clean.
    crate::auth::cleanup_server(vec![&mut *c0, &mut *c1]).expect("Initial server wipe failed");
    for (name, test) in group.tests {
        log::info!("+++ Test begin {}::{}", group.name, name);
        test(c0, c1);
        log::info!("+++ Test cleanup {}::{}", group.name, name);
        cleanup_clients!(c0, c1);
        log::info!("+++ Test finish {}::{}", group.name, name);
    }
    log::info!("++ TestGroup end {}", group.name);
}

// Note: this uses doc comments to generate the help text.
#[derive(Clone, Debug, Parser)]
#[command(name = "sync-test", about = "Sync integration tests")]
pub struct Opts {
    #[arg(long = "oauth-retries", short = 'r', default_value = "0")]
    /// Number of times to retry authentication with FxA if automatically
    /// logging in with OAuth fails (Sadly, it seems inherently finnicky).
    pub oauth_retries: u64,

    #[arg(long = "oauth-retry-delay", default_value = "5000")]
    /// Number of milliseconds to wait between retries. Does nothing if
    /// `oauth-retries` is 0.
    pub oauth_delay_time: u64,

    #[arg(long = "oauth-retry-delay-backoff", default_value = "2000")]
    /// Number of milliseconds to increase `oauth-retry-delay` with after each
    /// failure. Does nothing if `oauth-retries` is 0.
    pub oauth_retry_backoff: u64,

    #[arg(long = "fxa-stack", short = 's', default_value = "stable-dev")]
    /// Either 'release', 'stage', 'stable-dev', or a URL.
    pub fxa_stack: FxaConfigUrl,

    #[arg(long = "credentials")]
    credential_file: Option<String>,

    #[arg(long = "helper-debug")]
    /// Run the helper browser as non-headless, and enable extra logging
    pub helper_debug: bool,

    #[arg(long = "show-groups")]
    /// Show the test groups and exit.
    pub show_groups: bool,

    /// The test groups to run - execute with `--show-groups` to see the group names.
    pub groups: Vec<String>,
}

pub fn main() {
    let opts = Opts::parse();
    println!("### Running sync integration tests ###");
    init_testing();
    let groups = vec![
        crate::logins::get_test_group(),
        crate::tabs::get_test_group(),
        crate::sync15::get_test_group(),
        crate::autofill::get_test_group(),
    ];
    if opts.show_groups {
        println!(
            "The following test groups exist: {}",
            groups
                .into_iter()
                .map(|g| g.name.to_string())
                .collect::<Vec<String>>()
                .join(",")
        );
        return;
    }
    run_test_groups(&opts, groups);

    println!("\n### Sync integration tests passed!");
}
