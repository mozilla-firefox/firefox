/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use anyhow::Result;
use crash_helper_common::{BreakpadChar, BreakpadData, IPCChannel, IPCConnector, IPCListener};
use nix::unistd::{execv, fork, getpid, ForkResult};
use std::ffi::{CStr, CString};

use crate::CrashHelperClient;

impl CrashHelperClient {
    pub(crate) fn new(
        program: *const BreakpadChar,
        breakpad_data: BreakpadData,
        minidump_path: *const BreakpadChar,
    ) -> Result<CrashHelperClient> {
        let channel = IPCChannel::new()?;
        let (listener, server_endpoint, client_endpoint) = channel.deconstruct();
        let _pid = CrashHelperClient::spawn_crash_helper(
            program,
            breakpad_data,
            minidump_path,
            listener,
            server_endpoint,
        )?;

        Ok(CrashHelperClient {
            connector: client_endpoint,
            #[cfg(target_os = "linux")]
            pid: _pid,
        })
    }

    fn spawn_crash_helper(
        program: *const BreakpadChar,
        breakpad_data: BreakpadData,
        minidump_path: *const BreakpadChar,
        listener: IPCListener,
        endpoint: IPCConnector,
    ) -> Result<nix::libc::pid_t> {
        let parent_pid = getpid().to_string();
        let parent_pid_arg = unsafe { CString::from_vec_unchecked(parent_pid.into_bytes()) };
        let pid = unsafe { fork() }?;

        // TODO: daemonize the helper by double fork()'ing and waiting on the child
        match pid {
            ForkResult::Child => {
                let program = unsafe { CStr::from_ptr(program) };
                let breakpad_data_arg =
                    unsafe { CString::from_vec_unchecked(breakpad_data.to_string().into_bytes()) };
                let minidump_path = unsafe { CStr::from_ptr(minidump_path) };
                let listener_arg = listener.serialize();
                let endpoint_arg = endpoint.serialize();

                let _ = execv(
                    program,
                    &[
                        program,
                        &parent_pid_arg,
                        &breakpad_data_arg,
                        minidump_path,
                        &listener_arg,
                        &endpoint_arg,
                    ],
                );

                // This point should be unreachable, but let's play it safe
                unsafe { nix::libc::_exit(1) };
            }
            ForkResult::Parent { child } => Ok(child.as_raw()),
        }
    }
}
