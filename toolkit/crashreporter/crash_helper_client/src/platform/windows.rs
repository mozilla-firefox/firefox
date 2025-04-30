/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use anyhow::{bail, Result};
use crash_helper_common::{
    BreakpadChar, BreakpadData, BreakpadString, IPCChannel, IPCConnector, IPCListener,
};
use std::{
    ffi::{OsStr, OsString},
    mem::{size_of, zeroed},
    os::windows::ffi::{OsStrExt, OsStringExt},
    ptr::{null, null_mut},
};
use windows_sys::Win32::{
    Foundation::{CloseHandle, FALSE, TRUE},
    System::Threading::{
        CreateProcessW, GetCurrentProcessId, CREATE_UNICODE_ENVIRONMENT, DETACHED_PROCESS,
        PROCESS_INFORMATION, STARTUPINFOW,
    },
};

use crate::CrashHelperClient;

impl CrashHelperClient {
    pub(crate) fn new(
        program: *const BreakpadChar,
        breakpad_data: BreakpadData,
        minidump_path: *const BreakpadChar,
    ) -> Result<CrashHelperClient> {
        // SAFETY: `program` points to a valid string passed in by Firefox
        let program = unsafe { <OsString as BreakpadString>::from_ptr(program) };
        // SAFETY: `minidump_path` points to a valid string passed in by Firefox
        let minidump_path = unsafe { <OsString as BreakpadString>::from_ptr(minidump_path) };

        let channel = IPCChannel::new()?;
        let (listener, server_endpoint, client_endpoint) = channel.deconstruct();

        let _ = std::thread::spawn(move || {
            // If this fails we have no way to tell, but the IPC won't work so
            // it's fine to ignore the return value.
            let _ = CrashHelperClient::spawn_crash_helper(
                program,
                breakpad_data,
                minidump_path,
                listener,
                server_endpoint,
            );
        });

        Ok(CrashHelperClient {
            connector: client_endpoint,
        })
    }

    fn spawn_crash_helper(
        program: OsString,
        breakpad_data: BreakpadData,
        minidump_path: OsString,
        listener: IPCListener,
        endpoint: IPCConnector,
    ) -> Result<()> {
        // SAFETY: `GetCurrentProcessId()` takes no arguments and should always work
        let pid = OsString::from(unsafe { GetCurrentProcessId() }.to_string());

        let mut cmd_line = escape_cmd_line_arg(&program);
        cmd_line.push(" ");
        cmd_line.push(escape_cmd_line_arg(&pid));
        cmd_line.push(" ");
        cmd_line.push(escape_cmd_line_arg(breakpad_data.as_ref()));
        cmd_line.push(" ");
        cmd_line.push(escape_cmd_line_arg(&minidump_path));
        cmd_line.push(" ");
        cmd_line.push(escape_cmd_line_arg(&listener.serialize()));
        cmd_line.push(" ");
        cmd_line.push(escape_cmd_line_arg(&endpoint.serialize()));
        cmd_line.push("\0");
        let mut cmd_line: Vec<u16> = cmd_line.encode_wide().collect();

        let mut pi = unsafe { zeroed::<PROCESS_INFORMATION>() };
        let si = STARTUPINFOW {
            cb: size_of::<STARTUPINFOW>().try_into().unwrap(),
            ..unsafe { zeroed() }
        };

        let res = unsafe {
            CreateProcessW(
                /* lpApplicationName */ null(),
                cmd_line.as_mut_ptr(),
                /* lpProcessAttributes */ null_mut(),
                /* lpThreadAttributes */ null_mut(),
                /* bInheritHandles */ TRUE,
                CREATE_UNICODE_ENVIRONMENT | DETACHED_PROCESS,
                /* lpEnvironment */ null_mut(),
                /* lpCurrentDirectory */ null_mut(),
                &si,
                &mut pi,
            )
        };

        if res == FALSE {
            bail!("Could not create the crash helper process");
        }

        // SAFETY: We just successfully populated the `PROCESS_INFORMATION`
        // structure and the `hProcess` field contains a valid handle.
        unsafe { CloseHandle(pi.hProcess) };
        Ok(())
    }
}

/// Escape an argument so that it is suitable for use in the command line
/// parameter of a CreateProcess() call. This involves escaping all inner
/// double-quote characters as well as trailing backslashes. The algorithm
/// used is described in this article:
/// https://learn.microsoft.com/it-it/archive/blogs/twistylittlepassagesallalike/everyone-quotes-command-line-arguments-the-wrong-way
fn escape_cmd_line_arg(arg: &OsStr) -> OsString {
    const DOUBLE_QUOTES: u16 = '"' as u16;
    const BACKSLASH: u16 = '\\' as u16;

    let encoded_arg: Vec<u16> = arg.encode_wide().collect();
    let mut escaped_arg = Vec::<u16>::new();
    escaped_arg.push(DOUBLE_QUOTES as u16);

    let mut it = encoded_arg.iter().peekable();
    loop {
        let mut backslash_num = 0;

        while let Some(&&_c @ BACKSLASH) = it.peek() {
            it.next();
            backslash_num += 1;
        }

        match it.peek() {
            None => {
                for _ in 0..backslash_num {
                    escaped_arg.extend([BACKSLASH, BACKSLASH]);
                }
                break;
            }
            Some(&&_c @ DOUBLE_QUOTES) => {
                for _ in 0..backslash_num {
                    escaped_arg.extend([BACKSLASH, BACKSLASH]);
                }
                escaped_arg.extend([BACKSLASH, DOUBLE_QUOTES]);
            }
            Some(&&c) => {
                for _ in 0..backslash_num {
                    escaped_arg.push(BACKSLASH);
                }
                escaped_arg.push(c)
            }
        }

        it.next();
    }

    escaped_arg.push(DOUBLE_QUOTES);
    OsString::from_wide(&escaped_arg)
}
