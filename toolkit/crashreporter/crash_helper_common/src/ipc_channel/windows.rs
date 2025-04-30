/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::process;

use crate::{errors::IPCError, IPCConnector, IPCListener, Pid};

pub struct IPCChannel {
    listener: IPCListener,
    client_endpoint: IPCConnector,
    server_endpoint: IPCConnector,
}

impl IPCChannel {
    /// Create a new IPCChannel, this includes a listening endpoint that
    /// will use the current process PID as part of its address and two
    /// connected endpoints.
    pub fn new() -> Result<IPCChannel, IPCError> {
        let pid = process::id() as Pid;
        let mut listener = IPCListener::new(pid)?;
        let client_endpoint = IPCConnector::connect(pid)?;
        let server_endpoint = listener.accept()?;

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
