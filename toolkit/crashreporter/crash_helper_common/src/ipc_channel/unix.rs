/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::process;

#[cfg(any(target_os = "android", target_os = "linux"))]
use crate::platform::linux::unix_socketpair;
#[cfg(target_os = "macos")]
use crate::platform::macos::unix_socketpair;
use crate::{errors::IPCError, IPCConnector, IPCListener, Pid};

pub struct IPCChannel {
    listener: IPCListener,
    client_endpoint: IPCConnector,
    server_endpoint: IPCConnector,
}

impl IPCChannel {
    /// Create a new IPCChannel, this includes a listening endpoint that
    /// will use the current process PID as part of its address and two
    /// connected endpoints. The listener and the server-side endpoint can be
    /// inherited by a child process, the client-side endpoint cannot.
    pub fn new() -> Result<IPCChannel, IPCError> {
        let listener = IPCListener::new(process::id() as Pid)?;

        // Only the server-side socket will be left open after an exec().
        let pair = unix_socketpair().map_err(IPCError::System)?;
        let client_endpoint = IPCConnector::from_fd(pair.0)?;
        let server_endpoint = IPCConnector::from_fd_inheritable(pair.1)?;

        Ok(IPCChannel {
            listener,
            client_endpoint,
            server_endpoint,
        })
    }

    /// Deconstruct the IPC channel, returning the listening endpoint,
    /// the connected server-side endpoint and the connected client-side
    /// endpoint.
    pub fn deconstruct(self) -> (IPCListener, IPCConnector, IPCConnector) {
        (self.listener, self.server_endpoint, self.client_endpoint)
    }
}
