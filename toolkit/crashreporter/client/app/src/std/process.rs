/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

pub use std::process::*;

use crate::std::mock::{mock_key, MockKey};

use std::ffi::{OsStr, OsString};
use std::io::Result;
use std::sync::{Arc, Mutex};

mock_key! {
    // Uses PathBuf rather than OsString to avoid path separator differences.
    pub struct MockCommand(::std::path::PathBuf) => Box<dyn Fn(&Command) -> Result<Output> + Send + Sync>
}

pub const MOCK_PROCESS_ID: u32 = 1000;

#[derive(Debug)]
pub struct Command {
    pub program: OsString,
    pub args: Vec<OsString>,
    pub env: std::collections::HashMap<OsString, OsString>,
    pub stdin: Vec<u8>,
    // XXX The spawn stuff is hacky, but for now there's only one case where we really need to
    // interact with `spawn` so we live with it for testing.
    pub spawning: bool,
    pub spawned_child: Mutex<Option<::std::process::Child>>,
}

impl Command {
    pub fn mock<S: AsRef<OsStr>>(program: S) -> MockCommand {
        MockCommand(program.as_ref().into())
    }

    pub fn new<S: AsRef<OsStr>>(program: S) -> Self {
        Command {
            program: program.as_ref().into(),
            args: vec![],
            env: Default::default(),
            stdin: Default::default(),
            spawning: false,
            spawned_child: Mutex::new(None),
        }
    }

    pub fn arg<S: AsRef<OsStr>>(&mut self, arg: S) -> &mut Self {
        self.args.push(arg.as_ref().into());
        self
    }

    pub fn args<I, S>(&mut self, args: I) -> &mut Self
    where
        I: IntoIterator<Item = S>,
        S: AsRef<OsStr>,
    {
        for arg in args.into_iter() {
            self.arg(arg);
        }
        self
    }

    pub fn env<K, V>(&mut self, key: K, val: V) -> &mut Self
    where
        K: AsRef<OsStr>,
        V: AsRef<OsStr>,
    {
        self.env.insert(key.as_ref().into(), val.as_ref().into());
        self
    }

    pub fn envs<I, K, V>(&mut self, vars: I) -> &mut Command
    where
        I: IntoIterator<Item = (K, V)>,
        K: AsRef<OsStr>,
        V: AsRef<OsStr>,
    {
        self.env.clear();
        for (ref key, ref val) in vars {
            self.env.insert(key.as_ref().into(), val.as_ref().into());
        }
        self
    }

    pub fn env_remove<K>(&mut self, key: K) -> &mut Self
    where
        K: AsRef<OsStr>,
    {
        self.env.remove(key.as_ref().into());
        self
    }

    pub fn stdin<T: Into<Stdio>>(&mut self, _cfg: T) -> &mut Self {
        self
    }

    pub fn stdout<T: Into<Stdio>>(&mut self, _cfg: T) -> &mut Self {
        self
    }

    pub fn stderr<T: Into<Stdio>>(&mut self, _cfg: T) -> &mut Self {
        self
    }

    pub fn output(&mut self) -> std::io::Result<Output> {
        MockCommand(self.program.as_os_str().into())
            .try_get(|f| f(self))
            .unwrap_or(Err(std::io::ErrorKind::NotFound.into()))
    }

    pub fn spawn(&mut self) -> std::io::Result<Child> {
        self.spawning = true;
        self.output()?;
        self.spawning = false;
        let stdin = Arc::new(Mutex::new(vec![]));
        Ok(Child {
            stdin: Some(ChildStdin {
                data: stdin.clone(),
            }),
            cmd: self.clone_for_child(),
            stdin_data: Some(stdin),
        })
    }

    #[cfg(windows)]
    pub fn creation_flags(&mut self, _flags: u32) -> &mut Self {
        self
    }

    pub fn output_from_real_command(&self) -> std::io::Result<Output> {
        let mut spawned_child = self.spawned_child.lock().unwrap();
        if spawned_child.is_none() {
            *spawned_child = Some(
                ::std::process::Command::new(self.program.clone())
                    .args(self.args.clone())
                    .envs(self.env.clone())
                    .stdin(Stdio::piped())
                    .stdout(Stdio::piped())
                    .stderr(Stdio::piped())
                    .spawn()?,
            );
        }

        if self.spawning {
            return Ok(success_output());
        }

        let mut child = spawned_child.take().unwrap();
        {
            let mut input = child.stdin.take().unwrap();
            std::io::copy(&mut std::io::Cursor::new(&self.stdin), &mut input)?;
        }
        child.wait_with_output()
    }

    fn clone_for_child(&self) -> Self {
        Command {
            program: self.program.clone(),
            args: self.args.clone(),
            env: self.env.clone(),
            stdin: self.stdin.clone(),
            spawning: false,
            spawned_child: Mutex::new(self.spawned_child.lock().unwrap().take()),
        }
    }
}

pub struct Child {
    pub stdin: Option<ChildStdin>,
    cmd: Command,
    stdin_data: Option<Arc<Mutex<Vec<u8>>>>,
}

impl Child {
    pub fn wait_with_output(mut self) -> std::io::Result<Output> {
        self.ref_wait_with_output().unwrap()
    }

    pub fn wait(&mut self) -> std::io::Result<ExitStatus> {
        self.ref_wait_with_output().unwrap().map(|o| o.status)
    }

    // This function doesn't actually do anything with the mock Child.
    // It would be more accurate to affect the ExitStatus of the Child, but that requires a more
    // complete model of `MockCommand`.
    pub fn kill(&mut self) -> std::io::Result<()> {
        Ok(())
    }

    fn ref_wait_with_output(&mut self) -> Option<std::io::Result<Output>> {
        drop(self.stdin.take());
        if let Some(stdin) = self.stdin_data.take() {
            self.cmd.stdin = Arc::try_unwrap(stdin)
                .expect("stdin not dropped, wait_with_output may block")
                .into_inner()
                .unwrap();
            Some(
                MockCommand(self.cmd.program.as_os_str().into())
                    .try_get(|f| f(&self.cmd))
                    .unwrap_or(Err(std::io::ErrorKind::NotFound.into())),
            )
        } else {
            None
        }
    }
}

pub fn id() -> u32 {
    MOCK_PROCESS_ID
}

pub struct ChildStdin {
    data: Arc<Mutex<Vec<u8>>>,
}

impl std::io::Write for ChildStdin {
    fn write(&mut self, buf: &[u8]) -> Result<usize> {
        self.data.lock().unwrap().write(buf)
    }

    fn flush(&mut self) -> Result<()> {
        Ok(())
    }
}

#[cfg(unix)]
pub fn exit_status(status: i32) -> ExitStatus {
    use std::os::unix::process::ExitStatusExt;
    // ExitStatus::from_raw actually takes a *wait status*, which stores the exit status in the
    // second byte on BSDs and Linux.
    ExitStatus::from_raw(status << 8)
}

#[cfg(windows)]
pub fn exit_status(status: i32) -> ExitStatus {
    use std::os::windows::process::ExitStatusExt;
    ExitStatus::from_raw(status as u32)
}

pub fn success_output() -> Output {
    Output {
        status: exit_status(0),
        stdout: vec![],
        stderr: vec![],
    }
}
