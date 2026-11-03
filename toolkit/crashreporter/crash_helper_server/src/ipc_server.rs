/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use anyhow::{bail, Context, Result};
use crash_helper_common::{
    messages::{self, Header, Message, PathType, ProcessRendezVous},
    AncillaryData, GeckoChildId, IPCConnector, IPCConnectorKey, IPCEvent, IPCListener, IPCQueue,
    Pid, ProcessHandle,
};
use std::{collections::HashMap, ffi::OsString, mem, rc::Rc, sync::Mutex};

use crate::{
    breakpad_crash_generator::BreakpadCrashGenerator, crash_generation::CrashGenerator,
    finalize_breakpad_minidump, platform, BreakpadData,
};

#[derive(PartialEq)]
pub enum IPCServerState {
    Running,
    ClientDisconnected,
}

#[derive(PartialEq)]
enum IPCEndpoint {
    /// A connection to the parent process
    Parent,
    /// A connection to the child process
    Child,
    #[allow(dead_code)]
    /// A connection to an external process
    External,
}

struct ProcessId {
    /// The pid of a process.
    pid: Pid,
    #[allow(unused)]
    /// A handle natively representing a process.
    handle: ProcessHandle,
}

impl ProcessId {
    fn new(pid: Pid, handle: ProcessHandle) -> ProcessId {
        ProcessId { pid, handle }
    }
}

struct IPCConnection {
    /// The platform-specific connector used for this connection
    connector: Rc<IPCConnector>,
    /// The type of process on the other side of this connection
    endpoint: IPCEndpoint,
    /// The Gecko-assigned id of this process. This has a 0 value for the main
    /// process and `None`` for external connections.
    id: Option<GeckoChildId>,
    /// The native identifiers of a process, this is the pid plus a
    /// platform-dependent type. It is `None` upon child process registration
    /// then gets populated by a `ProcessRendezVous` message. It remains as
    /// `None` for external processes.
    process: Option<ProcessId>,
}

pub(crate) struct IPCServer {
    /// Platform-specific mechanism to wait for events. This will contain
    /// references to the connectors so needs to be the first element in
    /// the structure so that it's dropped first.
    queue: IPCQueue,
    connections: HashMap<IPCConnectorKey, IPCConnection>,
    /// The Breakpad server will contain a reference to the crash generator,
    /// hence it must be dropped first.
    breakpad_server: BreakpadCrashGenerator,
    generator: Box<Mutex<CrashGenerator>>,
}

impl IPCServer {
    pub(crate) fn new(
        client_pid: Pid,
        client_handle: Option<ProcessHandle>,
        listener: IPCListener,
        mut connector: IPCConnector,
        breakpad_data: BreakpadData,
        minidump_path: OsString,
    ) -> Result<IPCServer> {
        // If the client process handle was not provided at launch then it will
        // be sent by the client using a regular `ProcessRendezVous` message.
        let client_handle = match client_handle {
            Some(handle) => handle,
            None => connector
                .recv_reply::<ProcessRendezVous>()
                .context("Client failed to rendez-vous")?
                .get_process_handle(),
        };
        connector.set_process(client_handle.clone());

        let crash_generator = Box::new(Mutex::new(CrashGenerator::new(
            client_handle.clone(),
            minidump_path.clone(),
        )));

        // SAFETY: We widen the lifetime of this crash generator reference
        // as we guarantee that the underlying object will outlive the Breakpad
        // server which will be holding this reference.
        let crash_generator_ref = unsafe {
            mem::transmute::<&Mutex<CrashGenerator>, &'static Mutex<CrashGenerator>>(
                crash_generator.as_ref(),
            )
        };

        let breakpad_server = BreakpadCrashGenerator::new(
            breakpad_data,
            minidump_path,
            crash_generator_ref,
            finalize_breakpad_minidump,
        )?;

        let connector = Rc::new(connector);
        let mut queue = IPCQueue::new(listener)?;
        queue.add_connector(&connector)?;

        let mut connections = HashMap::with_capacity(10);
        connections.insert(
            connector.key(),
            IPCConnection {
                connector,
                endpoint: IPCEndpoint::Parent,
                id: Some(0),
                process: Some(ProcessId {
                    pid: client_pid,
                    handle: client_handle,
                }),
            },
        );

        Ok(IPCServer {
            queue,
            connections,
            breakpad_server,
            generator: crash_generator,
        })
    }

    pub(crate) fn run(&mut self) -> Result<IPCServerState> {
        let events = self.queue.wait_for_events()?;

        for event in events.into_iter() {
            match event {
                IPCEvent::Connect(connector) => {
                    self.connections.insert(
                        connector.key(),
                        IPCConnection {
                            connector,
                            endpoint: IPCEndpoint::External,
                            id: None,
                            process: None,
                        },
                    );
                }
                IPCEvent::Message(key, header, payload, ancillary_data) => {
                    if let Err(error) = self.handle_message(key, &header, payload, ancillary_data) {
                        log::error!(
                            "Error {error:#} when handling a message of kind {:?}",
                            header.kind
                        );
                    }
                }
                IPCEvent::Disconnect(key) => {
                    let connection = self
                        .connections
                        .remove(&key)
                        .expect("Disconnection event but no corresponding connection");

                    if let Some(process) = connection.process {
                        // `connection.id` always contains a value if `process` did.
                        self.generator
                            .lock()
                            .unwrap()
                            .move_report_to_id(process.pid, connection.id.unwrap());
                    }

                    if connection.endpoint == IPCEndpoint::Parent {
                        // The main process disconnected, leave
                        return Ok(IPCServerState::ClientDisconnected);
                    }
                }
            }
        }

        Ok(IPCServerState::Running)
    }

    fn handle_message(
        &mut self,
        key: IPCConnectorKey,
        header: &Header,
        data: Vec<u8>,
        ancillary_data: Vec<AncillaryData>,
    ) -> Result<()> {
        let connection = self
            .connections
            .get(&key)
            .expect("Event received on non-existing connection");
        let connector = &connection.connector;

        match connection.endpoint {
            IPCEndpoint::Parent => match header.kind {
                messages::Kind::SetPath => {
                    let message = messages::SetPath::decode(data, ancillary_data)?;
                    self.handle_set_path(message);
                }
                messages::Kind::GetPath => {
                    let message = messages::GetPath::decode(data, ancillary_data)?;
                    let path = self.handle_get_path(message);
                    let reply = messages::PathReply::new(path);
                    connector.send_message(reply)?;
                }
                messages::Kind::TransferMinidump => {
                    let message = messages::TransferMinidump::decode(data, ancillary_data)?;
                    let mut generator_lock = self.generator.lock().unwrap();
                    let crash_report = {
                        if let Some(crash_report) =
                            generator_lock.retrieve_minidump_by_id(message.id)
                        {
                            Some(crash_report)
                        } else if let Some(pid) = self.find_pid(message.id) {
                            generator_lock.retrieve_minidump_by_pid(pid)
                        } else {
                            None
                        }
                    };

                    let reply = crash_report
                        .map_or(messages::MinidumpReply::new(OsString::new(), None), |cr| {
                            messages::MinidumpReply::new(cr.path, cr.error)
                        });

                    connector.send_message(reply)?;
                }
                messages::Kind::GenerateMinidump => {
                    let message = messages::GenerateMinidump::decode(data, ancillary_data)?;
                    let crash_report = self.find_connection(message.id).and_then(|connection| {
                        if let Some(process) = &connection.process {
                            self.generator.lock().unwrap().generate_minidump(
                                message.id,
                                &process.handle,
                                &message.target_thread,
                            )
                        } else {
                            None
                        }
                    });

                    let reply = crash_report
                        .map_or(messages::MinidumpReply::new(OsString::new(), None), |cr| {
                            messages::MinidumpReply::new(cr.path, cr.error)
                        });

                    connector.send_message(reply)?;
                }
                messages::Kind::RegisterChildProcess => {
                    let message = messages::RegisterChildProcess::decode(data, ancillary_data)?;
                    self.register_child_process(message)?;
                }
                #[cfg(any(target_os = "android", target_os = "linux"))]
                messages::Kind::RegisterAuxvInfo => {
                    let message = messages::RegisterAuxvInfo::decode(data, ancillary_data)?;
                    self.generator.lock().unwrap().register_auxv_info(message)?;
                }
                #[cfg(any(target_os = "android", target_os = "linux"))]
                messages::Kind::UnregisterAuxvInfo => {
                    let message = messages::UnregisterAuxvInfo::decode(data, ancillary_data)?;
                    self.generator
                        .lock()
                        .unwrap()
                        .unregister_auxv_info(message)?;
                }
                #[cfg(target_os = "windows")]
                messages::Kind::ProcessRendezVous => {
                    let message = messages::ProcessRendezVous::decode(data, ancillary_data)?;
                    for connection in self.connections.values_mut() {
                        if connection.id.is_some_and(|value| value == message.id) {
                            connection.process = Some(ProcessId {
                                pid: message.child_pid,
                                handle: message.get_process_handle(),
                            });
                            break;
                        }
                    }
                }
                kind => {
                    bail!("Unexpected message {kind:?} from parent process");
                }
            },
            IPCEndpoint::Child => {
                bail!("Unexpected message {:?} from child process", header.kind);
            }
            IPCEndpoint::External => match header.kind {
                #[cfg(target_os = "windows")]
                messages::Kind::WindowsErrorReporting => {
                    let message =
                        messages::WindowsErrorReportingMinidump::decode(data, ancillary_data)?;
                    let res = self
                        .generator
                        .lock()
                        .unwrap()
                        .generate_wer_minidump(message);
                    match res {
                        Ok(_) => {}
                        Err(error) => log::error!(
                            "Could not generate a minidump requested via WER, error: {error:?}"
                        ),
                    }
                    connector.send_message(messages::WindowsErrorReportingMinidumpReply::new())?;
                }
                kind => {
                    bail!("Unexpected message {kind:?} from external process");
                }
            },
        };

        Ok(())
    }

    fn handle_set_path(&mut self, message: messages::SetPath) {
        let mut generator = self.generator.lock().unwrap();
        match message.path_type {
            PathType::CrashReports => {
                generator.set_minidump_path(&message.path);
                self.breakpad_server.set_path(message.path);
            }
        }
    }

    fn handle_get_path(&self, message: messages::GetPath) -> OsString {
        let generator = self.generator.lock().unwrap();
        match message.path_type {
            PathType::CrashReports => generator.get_minidump_path(),
        }
    }

    fn register_child_process(&mut self, message: messages::RegisterChildProcess) -> Result<()> {
        let connector = IPCConnector::from_ancillary(message.ancillary_data)?;

        let process = if platform::PROXY_RENDEZ_VOUS {
            None
        } else {
            let reply = connector.recv_reply::<messages::ProcessRendezVous>()?;

            if !reply.dumpable {
                bail!("Child process {} is not dumpable", reply.id);
            }

            if reply.id != message.id {
                bail!(
                    "Child process id {} does not match the one sent from the parent {}",
                    reply.id,
                    message.id
                );
            }

            Some(ProcessId::new(reply.child_pid, reply.get_process_handle()))
        };

        let connector = Rc::new(connector);
        self.queue.add_connector(&connector)?;
        self.connections.insert(
            connector.key(),
            IPCConnection {
                connector,
                endpoint: IPCEndpoint::Child,
                id: Some(message.id),
                process,
            },
        );

        Ok(())
    }

    fn find_pid(&self, id: GeckoChildId) -> Option<Pid> {
        for connection in self.connections.values() {
            if connection.id.is_some_and(|value| value == id) {
                return connection.process.as_ref().map(|p| p.pid);
            }
        }

        None
    }

    fn find_connection(&self, id: GeckoChildId) -> Option<&IPCConnection> {
        self.connections
            .values()
            .find(|&connection| connection.id.is_some_and(|value| value == id))
    }
}
