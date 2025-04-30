/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Benchmarking support
//!
//! Benchmarks are split up into two parts: the functions to be benchmarked live here, which the benchmarking code itself lives in `benches/bench.rs`.
//! It's easier to write benchmarking code inside the main crate, where we have access to private items.
//! However, it's easier to integrate with Cargo and criterion if benchmarks live in a separate crate.
//!
//! All benchmarks are defined as structs that implement either the [Benchmark] or [BenchmarkWithInput]

use std::{
    path::PathBuf,
    sync::{
        atomic::{AtomicU32, Ordering},
        Mutex,
    },
};
use tempfile::TempDir;

use crate::{SuggestIngestionConstraints, SuggestStore};
use remote_settings::{RemoteSettingsConfig2, RemoteSettingsContext, RemoteSettingsService};

use std::sync::Arc;

pub mod client;
pub mod geoname;
pub mod ingest;
pub mod query;

/// Trait for simple benchmarks
///
/// This supports simple benchmarks that don't require any input.  Note: global setup can be done
/// in the `new()` method for the struct.
pub trait Benchmark {
    /// Perform the operations that we're benchmarking.
    fn benchmarked_code(&self);
}

/// Trait for benchmarks that require input
///
/// This will run using Criterion's `iter_batched` function.  Criterion will create a batch of
/// inputs, then pass each one to the benchmark's iterations.
///
/// This supports simple benchmarks that don't require any input.
pub trait BenchmarkWithInput {
    /// Input that will be created once and then passed by reference to each
    /// of the benchmark's iterations.
    type GlobalInput;

    /// Input that will be created for each of the benchmark's iterations.
    type IterationInput;

    /// Generate the global input (not included in the benchmark time)
    fn global_input(&self) -> Self::GlobalInput;

    /// Generate the per-iteration input (not included in the benchmark time)
    fn iteration_input(&self) -> Self::IterationInput;

    /// Perform the operations that we're benchmarking.
    fn benchmarked_code(&self, g_input: &Self::GlobalInput, i_input: Self::IterationInput);
}

fn unique_db_filename() -> String {
    static COUNTER: AtomicU32 = AtomicU32::new(0);
    format!("db{}.sqlite", COUNTER.fetch_add(1, Ordering::Relaxed))
}

fn unique_remote_settings_dir() -> String {
    static COUNTER: AtomicU32 = AtomicU32::new(0);
    format!(
        "remote-settings-{}",
        COUNTER.fetch_add(1, Ordering::Relaxed)
    )
}

// Create a "starter" store that will do an initial ingest, and then
// initialize every returned store with a copy of its DB so that each one
// doesn't need to reingest.
static STARTER: Mutex<Option<(TempDir, PathBuf)>> = Mutex::new(None);

/// Creates a new store that will contain all provider data currently in remote
/// settings.
fn new_store() -> SuggestStore {
    let mut starter = STARTER.lock().unwrap();
    let (starter_dir, starter_db_path) = starter.get_or_insert_with(|| {
        let temp_dir = tempfile::tempdir().unwrap();
        let db_path = temp_dir.path().join(unique_db_filename());
        let remote_settings_dir = temp_dir.path().join(unique_remote_settings_dir());
        let rs_config = RemoteSettingsConfig2 {
            bucket_name: None,
            server: None,
            app_context: Some(RemoteSettingsContext::default()),
        };
        let remote_settings_service = Arc::new(RemoteSettingsService::new(
            remote_settings_dir.to_string_lossy().to_string(),
            rs_config,
        ));
        let store = SuggestStore::new(&db_path.to_string_lossy(), remote_settings_service);
        store
            .ingest(SuggestIngestionConstraints::all_providers())
            .expect("Error during ingestion");
        store.checkpoint();
        (temp_dir, db_path)
    });

    let db_path = starter_dir.path().join(unique_db_filename());
    let rs_config = RemoteSettingsConfig2 {
        bucket_name: None,
        server: None,
        app_context: Some(RemoteSettingsContext::default()),
    };
    let remote_settings_service = Arc::new(RemoteSettingsService::new("".to_string(), rs_config));
    std::fs::copy(starter_db_path, &db_path).expect("Error copying starter DB file");
    SuggestStore::new(&db_path.to_string_lossy(), remote_settings_service)
}

/// Cleanup the temp directory created for SuggestStore instances used in the benchmarks.
pub fn cleanup() {
    *STARTER.lock().unwrap() = None;
}
