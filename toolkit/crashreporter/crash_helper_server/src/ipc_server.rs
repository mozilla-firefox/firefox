/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use anyhow::Result;
use crash_helper_common::{
    errors::IPCError, messages, wait_for_events, IPCConnector, IPCEvent, IPCListener, Pid,
};

use crate::crash_generation::CrashGenerator;

#[derive(PartialEq)]
pub enum IPCServerState {
    Running,
    ClientDisconnected,
}

pub(crate) struct IPCServer {
    listener: IPCListener,
    connectors: Vec<IPCConnector>,
    client_pid: Pid,
}

impl IPCServer {
    pub(crate) fn new(
        client_pid: Pid,
        listener: IPCListener,
        connector: IPCConnector,
    ) -> IPCServer {
        IPCServer {
            listener,
            connectors: vec![connector],
            client_pid,
        }
    }

    pub(crate) fn run(
        &mut self,
        generator: &mut CrashGenerator,
    ) -> Result<IPCServerState, IPCError> {
        let events = wait_for_events(&mut self.listener, &mut self.connectors)?;

        for event in events {
            match event {
                IPCEvent::Connect(connector) => {
                    self.connectors.push(connector);
                }
                IPCEvent::Header(index, header) => {
                    let connector = self
                        .connectors
                        .get_mut(index)
                        .expect("Invalid connector index");
                    let res = Self::handle_message(connector, &header, generator);
                    if let Err(error) = res {
                        log::error!(
                            "Error {error} while handling a message of {:?} kind",
                            header.kind
                        );
                    }
                }
                IPCEvent::Disconnect(index) => {
                    let connector = self
                        .connectors
                        .get_mut(index)
                        .expect("Invalid connector index");
                    if connector.endpoint_pid() == self.client_pid {
                        // The main process disconnected, leave
                        return Ok(IPCServerState::ClientDisconnected);
                    } else {
                        // This closes the connection
                        let _ = self.connectors.remove(index);
                    }
                }
            }
        }

        Ok(IPCServerState::Running)
    }

    fn handle_message(
        connector: &mut IPCConnector,
        header: &messages::Header,
        generator: &mut CrashGenerator,
    ) -> Result<()> {
        let (data, ancillary_data) = connector.recv(header.size)?;

        let reply = generator.client_message(
            header.kind,
            &data,
            ancillary_data,
            connector.endpoint_pid(),
        )?;

        if let Some(reply) = reply {
            connector.send_message(reply.as_ref())?;
        }

        Ok(())
    }
}
