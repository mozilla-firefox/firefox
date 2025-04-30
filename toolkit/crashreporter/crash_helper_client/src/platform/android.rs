/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use anyhow::Result;
use crash_helper_common::IPCConnector;
use std::os::fd::{FromRawFd, OwnedFd, RawFd};

use crate::CrashHelperClient;

impl CrashHelperClient {
    pub(crate) fn new(server_socket: RawFd) -> Result<CrashHelperClient> {
        // SAFETY: The `server_socket` passed in from the application is valid
        let server_socket = unsafe { OwnedFd::from_raw_fd(server_socket) };
        let connector = IPCConnector::from_fd(server_socket)?;

        Ok(CrashHelperClient { connector })
    }
}
