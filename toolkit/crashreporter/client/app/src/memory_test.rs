/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! A frontend for memory testing.

use {
    crate::std::{env, io::Write, mem::size_of},
    anyhow::Context,
    memtest::{Outcome, Runner, RunnerArgs, TestKind},
    rand::{seq::SliceRandom, thread_rng},
    serde_json,
};

// Writing to stdout/stderr should only fail with parent fatal errors (like BrokenPipe), in which
// case there's no point in trying to print related error information, panicking, etc.
macro_rules! quiet_println {
    ( $($e:expr),* ) => {{
        let _ = writeln!(std::io::stdout(), $($e),*);
    }};
}

macro_rules! quiet_eprintln {
    ( $($e:expr),* ) => {{
        let _ = writeln!(std::io::stderr(), $($e),*);
    }};
}

/// Usage: crashreporter --memtest <memsize_mb> <json formatted memtest_runner_args>
///        Set the env var `MOZ_CRASHREPORTER_MEMTEST_KINDS` to specify prioritized memtest kinds
pub fn main() {
    let (mem_usize_count, memtest_runner_args, memtest_kinds) = match parse_args() {
        Ok(parsed) => parsed,
        Err(e) => {
            quiet_eprintln!("Error: {e:?}");
            std::process::exit(1);
        }
    };
    let mut memory = allocate_memory(mem_usize_count);

    let memtest_report_list = Runner::from_test_kinds(&memtest_runner_args, memtest_kinds)
        .run(&mut memory)
        .expect("failed to run memtest-runner");

    let mut output_json = serde_json::to_value(&memtest_report_list)
        .expect("memtest report list failed to serialize");
    output_json["memtest_failed"] = memtest_report_list
        .iter()
        .any(|report| matches!(report.outcome, Ok(Outcome::Fail(_))))
        .into();

    quiet_println!("{}", output_json.to_string());
}

/// Parse command line arguments and environment to return the parameters for running memtest,
/// including a usize for the requested memory vector length, RunnerArgs and a vector of
/// TestKinds to run.
fn parse_args() -> anyhow::Result<(usize, RunnerArgs, Vec<TestKind>)> {
    const KIB: usize = 1024;
    const MIB: usize = 1024 * KIB;

    let mut iter = env::args_os().skip(2).map_while(|s| s.into_string().ok());

    let memsize_mb: usize = iter
        .next()
        .and_then(|s| s.parse().ok())
        .context("missing/invalid memsize_MB")?;
    let memtest_runner_args = iter
        .next()
        .and_then(|s| serde_json::from_str(&s).ok())
        .context("missing/invalid memtest_runner_args")?;

    Ok((
        memsize_mb * MIB / size_of::<usize>(),
        memtest_runner_args,
        get_memtest_kinds()?,
    ))
}

/// Returns a vector of TestKind that contains all kinds, but prioritizes the given kinds.
fn get_memtest_kinds() -> anyhow::Result<Vec<TestKind>> {
    let env_string = env::var(ekey!("MEMTEST_KINDS")).unwrap_or_default();

    let specified = env_string
        .split_whitespace()
        .map(|s| s.parse().context("failed to parse memtest kinds"))
        .collect::<anyhow::Result<Vec<TestKind>>>()?;

    let mut remaining: Vec<_> = TestKind::ALL
        .iter()
        .filter(|k| !specified.contains(k))
        .cloned()
        .collect();
    remaining.shuffle(&mut thread_rng());

    let mut kinds = [specified, remaining].concat();

    // The Block Move Test requires special care. It only performs timeout checking during its
    // starting and ending phase. To avoid violating the timeout limit, we only run it if it is the
    // first test, where we are certain it has enough time to run.
    let block_move_idx = kinds
        .iter()
        .position(|k| matches!(k, TestKind::BlockMove))
        .expect("BlockMove should exist in TestKind::ALL");
    if block_move_idx != 0 {
        kinds.remove(block_move_idx);
    }

    Ok(kinds)
}

fn allocate_memory(mut mem_usize_count: usize) -> Vec<usize> {
    let mut memory = Vec::new();
    while mem_usize_count > 0 && memory.try_reserve_exact(mem_usize_count).is_err() {
        mem_usize_count /= 2;
    }
    memory.resize(mem_usize_count, 0);
    memory
}

/// Encapsulated logic for launching and interacting with a memory testing process.
pub mod child {
    use crate::std::{
        env,
        mem::ManuallyDrop,
        process::{Child, Command, Stdio},
        time::Duration,
    };
    use anyhow::Context;
    use memtest::RunnerArgs;

    /// The memtest child process.
    pub struct Memtest {
        child: ManuallyDrop<Child>,
    }

    impl Memtest {
        /// Try to start an asynchronous memory test.
        pub fn spawn() -> Option<Self> {
            match spawn_memtest() {
                Ok(child) => Some(Memtest {
                    child: ManuallyDrop::new(child),
                }),
                Err(e) => {
                    log::error!("failed to spawn memtest process: {e:#}");
                    None
                }
            }
        }

        /// Return the memtest output, waiting for testing to complete.
        pub fn collect_output_for_submission(mut self) -> anyhow::Result<String> {
            let child = unsafe { ManuallyDrop::take(&mut self.child) };
            std::mem::forget(self);

            let output = child
                .wait_with_output()
                .context("failed to wait on memtest process")?;
            if output.status.success() {
                String::from_utf8(output.stdout)
                    .context("failed to get valid string from memtest stdout")
            } else {
                String::from_utf8(output.stderr)
                    .context("failed to get valid string from memtest stderr")
            }
        }
    }

    impl Drop for Memtest {
        fn drop(&mut self) {
            if let Err(e) = self.child.kill() {
                log::warn!("failed to kill memtest process: {e}");
            }
            if let Err(e) = self.child.wait() {
                log::warn!("failed to wait on memtest process after kill: {e}");
            }
            unsafe { ManuallyDrop::drop(&mut self.child) };
        }
    }

    fn spawn_memtest() -> anyhow::Result<Child> {
        let memsize_mb = 1024;
        let memtest_runner_args = RunnerArgs {
            timeout: Duration::from_secs(3),
            mem_lock_mode: memtest::MemLockMode::Resizable,
            allow_working_set_resize: true,
            allow_multithread: true,
            allow_early_termination: true,
        };

        let curr_exe = env::current_exe().context("failed to get current exe path")?;
        Command::new(curr_exe)
            .arg("--memtest")
            .arg(memsize_mb.to_string())
            .arg(
                serde_json::to_string(&memtest_runner_args)
                    .expect("memtest_runner_args conversion to json string should not fail"),
            )
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|e| e.into())
    }
}
