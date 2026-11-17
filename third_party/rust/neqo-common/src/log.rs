// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::sync::Once;

use tracing::level_filters::LevelFilter;
use tracing_subscriber::{
    Layer as _, filter::Targets, layer::SubscriberExt as _, util::SubscriberInitExt as _,
};

/// Initialize the logging system with optional level filtering.
///
/// Neqo emits through `tracing`, printed by `tracing-subscriber`'s default
/// formatter and filtered by `RUST_LOG`. With the `log-bridge` feature, records
/// from dependencies that still use `log` are bridged into `tracing`. It can be
/// called multiple times safely.
pub fn init(level_filter: Option<LevelFilter>) {
    static INIT_ONCE: Once = Once::new();

    INIT_ONCE.call_once(|| {
        init_tracing(level_filter);
    });
}

fn init_tracing(level_filter: Option<LevelFilter>) {
    if tracing::level_filters::STATIC_MAX_LEVEL == LevelFilter::OFF {
        return;
    }
    let mut targets = std::env::var("RUST_LOG")
        .ok()
        .and_then(|directives| {
            directives
                .parse::<Targets>()
                .map_err(|e| eprintln!("Invalid RUST_LOG {directives:?}: {e}"))
                .ok()
        })
        .unwrap_or_else(|| Targets::new().with_default(LevelFilter::ERROR));
    if let Some(filter) = level_filter {
        targets = targets.with_default(filter);
    }
    // Write via `eprint!`, which the test harness captures.
    let layer = tracing_subscriber::fmt::layer()
        .with_test_writer()
        .with_filter(targets);
    if let Err(e) = tracing_subscriber::registry().with(layer).try_init() {
        eprintln!("Tracing initialization error {e:?}");
    } else {
        tracing::debug!("Logging initialized");
    }
}
