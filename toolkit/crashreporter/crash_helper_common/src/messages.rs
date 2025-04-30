/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#[cfg(any(target_os = "android", target_os = "linux"))]
use minidump_writer::minidump_writer::{AuxvType, DirectAuxvDumpInfo};
use num_derive::{FromPrimitive, ToPrimitive};
use num_traits::FromPrimitive;
use std::{
    ffi::{CString, OsString},
    mem::size_of,
};
#[cfg(target_os = "windows")]
use windows_sys::Win32::System::Diagnostics::Debug::{CONTEXT, EXCEPTION_RECORD};

use crate::{
    breakpad::{AncillaryData, Pid},
    errors::MessageError,
    BreakpadString,
};

#[repr(u8)]
#[derive(Copy, Clone, Debug, FromPrimitive, ToPrimitive, PartialEq)]
pub enum Kind {
    /// Changes the folder where crash reports are generated
    SetCrashReportPath = 1,
    /// Request the transfer of an already generated minidump for the specified
    /// PID back to the client. The message type is followed by a 32-bit
    /// integer containing the PID.
    TransferMinidump = 2,
    TransferMinidumpReply = 3,
    /// Request the generation of a minidump of the specified process.
    GenerateMinidump = 4,
    GenerateMinidumpReply = 5,
    /// Request the generation of a minidump based on data obtained via the
    /// Windows Error Reporting runtime exception module. The reply is empty
    /// and only used to inform the WER module that it's time to shut down the
    /// crashed process. This is only enabled on Windows.
    #[cfg(target_os = "windows")]
    WindowsErrorReporting = 6,
    #[cfg(target_os = "windows")]
    WindowsErrorReportingReply = 7,
    /// Register and unregister additional information for the auxiliary
    /// vector of a process.
    #[cfg(any(target_os = "android", target_os = "linux"))]
    RegisterAuxvInfo = 8,
    #[cfg(any(target_os = "android", target_os = "linux"))]
    UnregisterAuxvInfo = 9,
}

pub trait Message {
    fn kind() -> Kind
    where
        Self: Sized;
    fn header(&self) -> Vec<u8>;
    fn payload(&self) -> Vec<u8>;
    fn ancillary_payload(&self) -> Option<AncillaryData>;
    fn decode(data: &[u8], ancillary_data: Option<AncillaryData>) -> Result<Self, MessageError>
    where
        Self: Sized;
}

/* Message header, all messages are prefixed with this. The header is sent as
 * a single message over the underlying transport and contains the size of the
 * message payload as well as the type of the message. This allows the receiver
 * to validate and prepare for the reception of the payload. */

pub const HEADER_SIZE: usize = size_of::<Kind>() + size_of::<usize>();

pub struct Header {
    pub kind: Kind,
    pub size: usize,
}

impl Header {
    fn encode(&self) -> Vec<u8> {
        let mut buffer = Vec::with_capacity(HEADER_SIZE);
        buffer.push(self.kind as u8);
        buffer.extend(&self.size.to_ne_bytes());
        debug_assert!(buffer.len() == HEADER_SIZE, "Header size mismatch");
        buffer
    }

    pub fn decode(buffer: &[u8]) -> Result<Header, MessageError> {
        let kind = buffer.first().ok_or(MessageError::Truncated)?;
        let kind = Kind::from_u8(*kind).ok_or(MessageError::InvalidKind)?;
        let size_bytes: [u8; size_of::<usize>()] =
            buffer[size_of::<Kind>()..size_of::<Kind>() + size_of::<usize>()].try_into()?;
        let size = usize::from_ne_bytes(size_bytes);

        Ok(Header { kind, size })
    }
}

/* Message used to change the path where crash reports are generated. */

pub struct SetCrashReportPath {
    pub path: OsString,
}

impl SetCrashReportPath {
    pub fn new(path: OsString) -> SetCrashReportPath {
        SetCrashReportPath { path }
    }

    fn payload_size(&self) -> usize {
        let path_len = self.path.serialize().len();
        size_of::<usize>() + path_len
    }
}

impl Message for SetCrashReportPath {
    fn kind() -> Kind {
        Kind::SetCrashReportPath
    }

    fn header(&self) -> Vec<u8> {
        Header {
            kind: Self::kind(),
            size: self.payload_size(),
        }
        .encode()
    }

    fn payload(&self) -> Vec<u8> {
        let mut payload = Vec::with_capacity(self.payload_size());
        let path = self.path.serialize();
        payload.extend(path.len().to_ne_bytes());
        payload.extend(self.path.serialize());
        payload
    }

    fn ancillary_payload(&self) -> Option<AncillaryData> {
        None
    }

    fn decode(
        data: &[u8],
        ancillary_data: Option<AncillaryData>,
    ) -> Result<SetCrashReportPath, MessageError> {
        debug_assert!(
            ancillary_data.is_none(),
            "SetCrashReportPath messages cannot carry ancillary data"
        );

        let path_len_bytes: [u8; size_of::<usize>()] = data[0..size_of::<usize>()].try_into()?;
        let path_len = usize::from_ne_bytes(path_len_bytes);
        let offset = size_of::<usize>();

        let path = <OsString as BreakpadString>::deserialize(&data[offset..offset + path_len])
            .map_err(|_| MessageError::InvalidData)?;

        Ok(SetCrashReportPath { path })
    }
}

/* Transfer minidump message, used to request the minidump which has been
 * generated for the specified pid. */

pub struct TransferMinidump {
    pub pid: Pid,
}

impl TransferMinidump {
    pub fn new(pid: Pid) -> TransferMinidump {
        TransferMinidump { pid }
    }
}

impl Message for TransferMinidump {
    fn kind() -> Kind {
        Kind::TransferMinidump
    }

    fn header(&self) -> Vec<u8> {
        Header {
            kind: Self::kind(),
            size: size_of::<Pid>(),
        }
        .encode()
    }

    fn payload(&self) -> Vec<u8> {
        self.pid.to_ne_bytes().to_vec()
    }

    fn ancillary_payload(&self) -> Option<AncillaryData> {
        None
    }

    fn decode(
        data: &[u8],
        ancillary_data: Option<AncillaryData>,
    ) -> Result<TransferMinidump, MessageError> {
        debug_assert!(
            ancillary_data.is_none(),
            "TransferMinidump messages cannot carry ancillary data"
        );
        let bytes: [u8; size_of::<Pid>()] = data[0..size_of::<Pid>()].try_into()?;
        let pid = Pid::from_ne_bytes(bytes);

        Ok(TransferMinidump { pid })
    }
}

/* Transfer minidump reply, received from the server after having sent a
 * TransferMinidump message. */

pub struct TransferMinidumpReply {
    pub path: OsString,
    pub error: Option<CString>,
}

impl TransferMinidumpReply {
    pub fn new(path: OsString, error: Option<CString>) -> TransferMinidumpReply {
        TransferMinidumpReply { path, error }
    }

    fn payload_size(&self) -> usize {
        let path_len = self.path.serialize().len();
        // TODO: We should use checked arithmetic here
        (size_of::<usize>() * 2)
            + path_len
            + self
                .error
                .as_ref()
                .map_or(0, |error| error.as_bytes().len())
    }
}

impl Message for TransferMinidumpReply {
    fn kind() -> Kind {
        Kind::TransferMinidumpReply
    }

    fn header(&self) -> Vec<u8> {
        Header {
            kind: Self::kind(),
            size: self.payload_size(),
        }
        .encode()
    }

    fn payload(&self) -> Vec<u8> {
        let path_bytes = self.path.serialize();
        let mut buffer = Vec::with_capacity(self.payload_size());
        buffer.extend(path_bytes.len().to_ne_bytes());
        buffer.extend(
            (self
                .error
                .as_ref()
                .map_or(0, |error| error.as_bytes().len()))
            .to_ne_bytes(),
        );
        buffer.extend(path_bytes);
        buffer.extend(
            self.error
                .as_ref()
                .map_or(Vec::new(), |error| Vec::from(error.as_bytes())),
        );
        buffer
    }

    fn ancillary_payload(&self) -> Option<AncillaryData> {
        None
    }

    fn decode(
        data: &[u8],
        ancillary_data: Option<AncillaryData>,
    ) -> Result<TransferMinidumpReply, MessageError> {
        debug_assert!(
            ancillary_data.is_none(),
            "TransferMinidumpReply messages cannot carry ancillary data"
        );
        let path_len_bytes: [u8; size_of::<usize>()] = data[0..size_of::<usize>()].try_into()?;
        let path_len = usize::from_ne_bytes(path_len_bytes);
        let offset = size_of::<usize>();

        let error_len_bytes: [u8; size_of::<usize>()] =
            data[offset..offset + size_of::<usize>()].try_into()?;
        let error_len = usize::from_ne_bytes(error_len_bytes);
        let offset = offset + size_of::<usize>();

        let path = <OsString as BreakpadString>::deserialize(&data[offset..offset + path_len])
            .map_err(|_| MessageError::InvalidData)?;
        let offset = offset + path_len;

        let error = if error_len > 0 {
            Some(CString::new(&data[offset..offset + error_len])?)
        } else {
            None
        };

        Ok(TransferMinidumpReply::new(path, error))
    }
}

/* Generate a minidump based on information captured by the Windows Error Reporting runtime exception module. */

#[cfg(target_os = "windows")]
pub struct WindowsErrorReportingMinidump {
    pub pid: Pid,
    pub tid: Pid, // TODO: This should be a different type
    pub exception_records: Vec<EXCEPTION_RECORD>,
    pub context: CONTEXT,
}

#[cfg(target_os = "windows")]
impl WindowsErrorReportingMinidump {
    pub fn new(
        pid: Pid,
        tid: Pid,
        exception_records: Vec<EXCEPTION_RECORD>,
        context: CONTEXT,
    ) -> WindowsErrorReportingMinidump {
        WindowsErrorReportingMinidump {
            pid,
            tid,
            exception_records,
            context,
        }
    }

    fn payload_size(&self) -> usize {
        (size_of::<Pid>() * 2)
            + size_of::<usize>()
            + (size_of::<EXCEPTION_RECORD>() * self.exception_records.len())
            + size_of::<CONTEXT>()
    }
}

#[cfg(target_os = "windows")]
impl Message for WindowsErrorReportingMinidump {
    fn kind() -> Kind {
        Kind::WindowsErrorReporting
    }

    fn header(&self) -> Vec<u8> {
        Header {
            kind: Self::kind(),
            size: self.payload_size(),
        }
        .encode()
    }

    fn payload(&self) -> Vec<u8> {
        let mut buffer = Vec::<u8>::with_capacity(self.payload_size());
        buffer.extend(self.pid.to_ne_bytes());
        buffer.extend(self.tid.to_ne_bytes());
        buffer.extend(self.exception_records.len().to_ne_bytes());
        for exception_record in self.exception_records.iter() {
            let bytes: [u8; size_of::<EXCEPTION_RECORD>()] =
                unsafe { std::mem::transmute(*exception_record) };
            buffer.extend(bytes);
        }
        let bytes: [u8; size_of::<CONTEXT>()] = unsafe { std::mem::transmute(self.context) };
        buffer.extend(bytes);
        buffer
    }

    fn ancillary_payload(&self) -> Option<AncillaryData> {
        None
    }

    fn decode(
        data: &[u8],
        ancillary_data: Option<AncillaryData>,
    ) -> Result<WindowsErrorReportingMinidump, MessageError> {
        debug_assert!(
            ancillary_data.is_none(),
            "WindowsErrorReportingMinidump messages cannot carry ancillary data"
        );
        let bytes: [u8; size_of::<Pid>()] = data[0..size_of::<Pid>()].try_into()?;
        let pid = Pid::from_ne_bytes(bytes);
        let offset = size_of::<Pid>();

        let bytes: [u8; size_of::<Pid>()] = data[offset..(offset + size_of::<Pid>())].try_into()?;
        let tid = Pid::from_ne_bytes(bytes);
        let offset = offset + size_of::<Pid>();

        let bytes: [u8; size_of::<usize>()] =
            data[offset..(offset + size_of::<usize>())].try_into()?;
        let exception_records_n = usize::from_ne_bytes(bytes);
        let offset = offset + size_of::<usize>();

        let mut exception_records = Vec::<EXCEPTION_RECORD>::with_capacity(exception_records_n);
        for i in 0..exception_records_n {
            let element_offset = offset + (i * size_of::<EXCEPTION_RECORD>());
            let bytes: [u8; size_of::<EXCEPTION_RECORD>()] = data
                [element_offset..(element_offset + size_of::<EXCEPTION_RECORD>())]
                .try_into()?;
            let exception_record = unsafe {
                std::mem::transmute::<[u8; size_of::<EXCEPTION_RECORD>()], EXCEPTION_RECORD>(bytes)
            };
            exception_records.push(exception_record);
        }

        let bytes: [u8; size_of::<CONTEXT>()] =
            data[offset..(offset + size_of::<CONTEXT>())].try_into()?;
        let context = unsafe { std::mem::transmute::<[u8; size_of::<CONTEXT>()], CONTEXT>(bytes) };

        Ok(WindowsErrorReportingMinidump {
            pid,
            tid,
            exception_records,
            context,
        })
    }
}

/* Windows Error Reporting minidump reply, received from the server after
 * having sent a WindowsErrorReportingMinidumpReply. Informs the client that
 * it can tear down the crashed process. */

#[cfg(target_os = "windows")]
pub struct WindowsErrorReportingMinidumpReply {}

#[cfg(target_os = "windows")]
impl Default for WindowsErrorReportingMinidumpReply {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(target_os = "windows")]
impl WindowsErrorReportingMinidumpReply {
    pub fn new() -> WindowsErrorReportingMinidumpReply {
        WindowsErrorReportingMinidumpReply {}
    }

    fn payload_size(&self) -> usize {
        0
    }
}

#[cfg(target_os = "windows")]
impl Message for WindowsErrorReportingMinidumpReply {
    fn kind() -> Kind {
        Kind::WindowsErrorReportingReply
    }

    fn header(&self) -> Vec<u8> {
        Header {
            kind: Self::kind(),
            size: self.payload_size(),
        }
        .encode()
    }

    fn payload(&self) -> Vec<u8> {
        Vec::<u8>::new()
    }

    fn ancillary_payload(&self) -> Option<AncillaryData> {
        None
    }

    fn decode(
        data: &[u8],
        ancillary_data: Option<AncillaryData>,
    ) -> Result<WindowsErrorReportingMinidumpReply, MessageError> {
        if ancillary_data.is_some() || !data.is_empty() {
            return Err(MessageError::InvalidData);
        }

        Ok(WindowsErrorReportingMinidumpReply::new())
    }
}

/* Message used to send information about a process' auxiliary vector. */

#[cfg(any(target_os = "android", target_os = "linux"))]
pub struct RegisterAuxvInfo {
    pub pid: Pid,
    pub auxv_info: DirectAuxvDumpInfo,
}

#[cfg(any(target_os = "android", target_os = "linux"))]
impl RegisterAuxvInfo {
    pub fn new(pid: Pid, auxv_info: DirectAuxvDumpInfo) -> RegisterAuxvInfo {
        RegisterAuxvInfo { pid, auxv_info }
    }

    fn payload_size(&self) -> usize {
        // A bit hacky but we'll change this when we make
        // serialization/deserialization later.
        size_of::<Pid>() + (size_of::<AuxvType>() * 4)
    }
}

#[cfg(any(target_os = "android", target_os = "linux"))]
impl Message for RegisterAuxvInfo {
    fn kind() -> Kind {
        Kind::RegisterAuxvInfo
    }

    fn header(&self) -> Vec<u8> {
        Header {
            kind: Self::kind(),
            size: self.payload_size(),
        }
        .encode()
    }

    fn payload(&self) -> Vec<u8> {
        let mut payload = Vec::with_capacity(self.payload_size());
        payload.extend(self.pid.to_ne_bytes());
        payload.extend(self.auxv_info.program_header_count.to_ne_bytes());
        payload.extend(self.auxv_info.program_header_address.to_ne_bytes());
        payload.extend(self.auxv_info.linux_gate_address.to_ne_bytes());
        payload.extend(self.auxv_info.entry_address.to_ne_bytes());
        debug_assert!(self.payload_size() == payload.len());
        payload
    }

    fn ancillary_payload(&self) -> Option<AncillaryData> {
        None
    }

    fn decode(
        data: &[u8],
        ancillary_data: Option<AncillaryData>,
    ) -> Result<RegisterAuxvInfo, MessageError> {
        debug_assert!(
            ancillary_data.is_none(),
            "RegisterAuxvInfo messages cannot carry ancillary data"
        );

        let bytes: [u8; size_of::<Pid>()] = data[0..size_of::<Pid>()].try_into()?;
        let pid = Pid::from_ne_bytes(bytes);
        let offset = size_of::<Pid>();

        let bytes: [u8; size_of::<AuxvType>()] =
            data[offset..(offset + size_of::<AuxvType>())].try_into()?;
        let program_header_count = AuxvType::from_ne_bytes(bytes);
        let offset = offset + size_of::<AuxvType>();

        let bytes: [u8; size_of::<AuxvType>()] =
            data[offset..(offset + size_of::<AuxvType>())].try_into()?;
        let program_header_address = AuxvType::from_ne_bytes(bytes);
        let offset = offset + size_of::<AuxvType>();

        let bytes: [u8; size_of::<AuxvType>()] =
            data[offset..(offset + size_of::<AuxvType>())].try_into()?;
        let linux_gate_address = AuxvType::from_ne_bytes(bytes);
        let offset = offset + size_of::<AuxvType>();

        let bytes: [u8; size_of::<AuxvType>()] =
            data[offset..(offset + size_of::<AuxvType>())].try_into()?;
        let entry_address = AuxvType::from_ne_bytes(bytes);

        let auxv_info = DirectAuxvDumpInfo {
            program_header_count,
            program_header_address,
            entry_address,
            linux_gate_address,
        };

        Ok(RegisterAuxvInfo { pid, auxv_info })
    }
}

/* Message used to inform the crash helper that a process' auxiliary vector
 * information is not needed anymore. */

#[cfg(any(target_os = "android", target_os = "linux"))]
pub struct UnregisterAuxvInfo {
    pub pid: Pid,
}

#[cfg(any(target_os = "android", target_os = "linux"))]
impl UnregisterAuxvInfo {
    pub fn new(pid: Pid) -> UnregisterAuxvInfo {
        UnregisterAuxvInfo { pid }
    }

    fn payload_size(&self) -> usize {
        size_of::<Pid>()
    }
}

#[cfg(any(target_os = "android", target_os = "linux"))]
impl Message for UnregisterAuxvInfo {
    fn kind() -> Kind {
        Kind::UnregisterAuxvInfo
    }

    fn header(&self) -> Vec<u8> {
        Header {
            kind: Self::kind(),
            size: self.payload_size(),
        }
        .encode()
    }

    fn payload(&self) -> Vec<u8> {
        let mut payload = Vec::with_capacity(self.payload_size());
        payload.extend(self.pid.to_ne_bytes());
        debug_assert!(self.payload_size() == payload.len());
        payload
    }

    fn ancillary_payload(&self) -> Option<AncillaryData> {
        None
    }

    fn decode(
        data: &[u8],
        ancillary_data: Option<AncillaryData>,
    ) -> Result<UnregisterAuxvInfo, MessageError> {
        debug_assert!(
            ancillary_data.is_none(),
            "UnregisterAuxvInfo messages cannot carry ancillary data"
        );

        let bytes: [u8; size_of::<Pid>()] = data[0..size_of::<Pid>()].try_into()?;
        let pid = Pid::from_ne_bytes(bytes);

        Ok(UnregisterAuxvInfo { pid })
    }
}
