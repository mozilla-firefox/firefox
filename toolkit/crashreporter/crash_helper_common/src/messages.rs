/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use bytes::{Buf, BufMut, Bytes, BytesMut, TryGetError};
#[cfg(any(target_os = "android", target_os = "linux"))]
use minidump_writer::minidump_writer::{AuxvType, DirectAuxvDumpInfo};
use num_derive::{FromPrimitive, ToPrimitive};
use num_traits::FromPrimitive;
use std::{
    array::TryFromSliceError,
    ffi::{CString, FromBytesWithNulError, NulError, OsString},
    mem::size_of,
};
use thiserror::Error;
#[cfg(target_os = "windows")]
use windows_sys::Win32::System::Diagnostics::Debug::{CONTEXT, EXCEPTION_RECORD};

use crate::{
    breakpad::Pid, ipc_connector::CONNECTOR_ANCILLARY_DATA_LEN,
    platform::PROCESS_RENDEZVOUS_ANCILLARY_DATA_LEN, AncillaryData, BreakpadString, GeckoChildId,
    ProcessHandle, RawThreadHandle, ThreadHandle,
};
#[cfg(not(any(target_os = "ios", target_os = "macos")))]
use crate::{AsRawThreadHandle, FromRawThreadHandle};

#[derive(Debug, Error)]
pub enum MessageError {
    #[error("Nul terminator found within a string")]
    InteriorNul(#[from] NulError),
    #[error("The message contained invalid ancillary data")]
    InvalidAncillary,
    #[error("The message contained an invalid payload")]
    InvalidData,
    #[error("Message kind is invalid")]
    InvalidKind,
    #[error("Invalid message size")]
    InvalidSize(#[from] TryFromSliceError),
    #[error("Missing ancillary data")]
    MissingAncillary,
    #[error("Missing nul terminator")]
    MissingNul(#[from] FromBytesWithNulError),
    #[error("Truncated message")]
    Truncated(#[from] TryGetError),
    #[error("Unexpected ancillary data")]
    UnexpectedAncillaryData,
}

#[repr(u8)]
#[derive(Copy, Clone, Debug, FromPrimitive, ToPrimitive, PartialEq)]
pub enum Kind {
    /// Set the path of a file or folder used by the crash helper
    SetPath = 1,
    /// Get the path of a file or folder used by the crash helper
    GetPath = 2,
    /// Holds a path, used as a reply for `GetPath`
    PathReply = 3,
    /// Request the transfer of an already generated minidump of the specified
    /// process back to the client.
    TransferMinidump = 4,
    /// Request the generation of a minidump of the specified process.
    GenerateMinidump = 5,
    /// Reply holding information about a minidump, can be used as a response
    /// for both a `TransferMinidump` and `GenerateMinidump` message.
    MinidumpReply = 6,
    /// Request the generation of a minidump based on data obtained via the
    /// Windows Error Reporting runtime exception module. The reply is empty
    /// and only used to inform the WER module that it's time to shut down the
    /// crashed process. This is only enabled on Windows.
    #[cfg(target_os = "windows")]
    WindowsErrorReporting = 7,
    #[cfg(target_os = "windows")]
    WindowsErrorReportingReply = 8,
    /// Register and unregister additional information for the auxiliary
    /// vector of a process.
    #[cfg(any(target_os = "android", target_os = "linux"))]
    RegisterAuxvInfo = 9,
    #[cfg(any(target_os = "android", target_os = "linux"))]
    UnregisterAuxvInfo = 10,
    /// Register a new child process and carry over the IPC channel it will use
    /// to talk to the crash helper. This is sent from the main process to the
    /// crash helper.
    RegisterChildProcess = 11,
    /// Message sent by all processes checking-in with the crash helper. After
    /// this message has been received the crash helper is capable of dumping
    /// the sending process.
    ProcessRendezVous = 12,
}

// Bytes helpers to serialize/deserialize values not supported directly by the
// `bytes` crate. Note that all of these could be implemented by simply
// transmuting a generic type T into an appropriate byte-sized slice. However
// doing so for arbitrary objects would also copy whatever data might be in
// the padding between the fields or at the end of the object, potentially
// creating a channel to leak information from a process, which is why we only
// have helpers for types that we know are not padded.

trait BytesMutExtensions {
    fn put_usize_ne(&mut self, n: usize);
    fn put_pid_ne(&mut self, pid: Pid);
    fn put_vec(&mut self, vec: &[u8]);
    #[cfg(target_os = "windows")]
    fn put_context(&mut self, contextref: &CONTEXT);
    #[cfg(target_os = "windows")]
    fn put_exception_record(&mut self, recordref: &EXCEPTION_RECORD);
}

impl BytesMutExtensions for BytesMut {
    fn put_usize_ne(&mut self, n: usize) {
        #[cfg(target_pointer_width = "32")]
        self.put_u32_ne(n as u32);
        #[cfg(target_pointer_width = "64")]
        self.put_u64_ne(n as u64);
    }

    fn put_pid_ne(&mut self, pid: Pid) {
        #[cfg(target_os = "windows")]
        self.put_u32_ne(pid);
        #[cfg(not(target_os = "windows"))]
        self.put_i32_ne(pid);
    }

    fn put_vec(&mut self, vec: &[u8]) {
        self.put_usize_ne(vec.len());
        self.put_slice(vec);
    }

    #[cfg(target_os = "windows")]
    fn put_context(&mut self, contextref: &CONTEXT) {
        // SAFETY: The `CONTEXT` structure does not contain padding and can
        // thus be stored as a simple slice of bytes of the same size.
        let bytes: [u8; size_of::<CONTEXT>()] = unsafe { std::mem::transmute(*contextref) };
        self.put_slice(&bytes);
    }

    #[cfg(target_os = "windows")]
    fn put_exception_record(&mut self, recordref: &EXCEPTION_RECORD) {
        self.put_i32_ne(recordref.ExceptionCode);
        self.put_u32_ne(recordref.ExceptionFlags);
        // We skip the `ExceptionRecord` field because it's a pointer and it
        // would be invalid in the destination process, we rebuild it there.
        self.put_usize_ne(recordref.ExceptionAddress as usize);
        self.put_u32_ne(recordref.NumberParameters);
        for info in recordref.ExceptionInformation {
            self.put_usize_ne(info);
        }
    }
}

trait BytesExtensions {
    fn try_get_usize_ne(&mut self) -> Result<usize, TryGetError>;
    fn try_get_pid_ne(&mut self) -> Result<Pid, TryGetError>;
    fn try_get_vec(&mut self) -> Result<Vec<u8>, TryGetError>;
    #[cfg(target_os = "windows")]
    fn try_get_context(&mut self) -> Result<CONTEXT, TryGetError>;
    #[cfg(target_os = "windows")]
    fn try_get_exception_record(&mut self) -> Result<EXCEPTION_RECORD, TryGetError>;
}

impl BytesExtensions for Bytes {
    fn try_get_usize_ne(&mut self) -> Result<usize, TryGetError> {
        #[cfg(target_pointer_width = "32")]
        return self.try_get_u32_ne().map(|v| v as usize);
        #[cfg(target_pointer_width = "64")]
        return self.try_get_u64_ne().map(|v| v as usize);
    }

    fn try_get_pid_ne(&mut self) -> Result<Pid, TryGetError> {
        #[cfg(target_os = "windows")]
        return self.try_get_u32_ne();
        #[cfg(not(target_os = "windows"))]
        return self.try_get_i32_ne();
    }

    fn try_get_vec(&mut self) -> Result<Vec<u8>, TryGetError> {
        let len = self.try_get_usize_ne()?;
        let mut vec = vec![0u8; len];
        self.try_copy_to_slice(vec.as_mut_slice())?;
        Ok(vec)
    }

    #[cfg(target_os = "windows")]
    fn try_get_context(&mut self) -> Result<CONTEXT, TryGetError> {
        let mut buffer = vec![0u8; size_of::<CONTEXT>()];
        self.try_copy_to_slice(buffer.as_mut_slice())?;
        // SAFETY: The `CONTEXT` structure has no padding, so it can be
        // populated by copying over its contents from a slice of bytes.
        // Unrwapping the result of the slice `try_into()` will also never
        // fail as the size is guaranteed to be right.
        let context = unsafe {
            std::mem::transmute::<[u8; size_of::<CONTEXT>()], CONTEXT>(
                (*buffer.as_slice()).try_into().unwrap(),
            )
        };

        Ok(context)
    }

    #[cfg(target_os = "windows")]
    fn try_get_exception_record(&mut self) -> Result<EXCEPTION_RECORD, TryGetError> {
        let exception_code = self.try_get_i32_ne()?;
        let exception_flags = self.try_get_u32_ne()?;
        let exception_address = self.try_get_usize_ne()?;
        let number_parameters = self.try_get_u32_ne()?;
        let mut exception_information: [usize; 15] = [0usize; 15];
        for ei in exception_information.iter_mut() {
            *ei = self.try_get_usize_ne()?;
        }

        Ok(EXCEPTION_RECORD {
            ExceptionCode: exception_code,
            ExceptionFlags: exception_flags,
            ExceptionRecord: std::ptr::null_mut(),
            ExceptionAddress: exception_address as *mut std::ffi::c_void,
            NumberParameters: number_parameters,
            ExceptionInformation: exception_information,
        })
    }
}

pub trait Message {
    fn kind() -> Kind;
    fn payload_size(&self) -> usize;
    fn ancillary_data_len(&self) -> usize;
    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>);
    fn decode(data: Vec<u8>, ancillary_data: Vec<AncillaryData>) -> Result<Self, MessageError>
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
    fn encode(kind: Kind, size: usize) -> Bytes {
        let mut buffer = BytesMut::with_capacity(HEADER_SIZE);

        buffer.put_u8(kind as u8);
        buffer.put_usize_ne(size);
        debug_assert!(buffer.len() == HEADER_SIZE, "Header size mismatch");
        buffer.freeze()
    }

    pub fn decode(buffer: Vec<u8>) -> Result<Header, MessageError> {
        let mut buffer = Bytes::from(buffer);
        let kind = buffer.try_get_u8()?;
        let kind = Kind::from_u8(kind).ok_or(MessageError::InvalidKind)?;
        let size = buffer.try_get_usize_ne()?;

        Ok(Header { kind, size })
    }
}

/* Message used to change a path used by the crash helper. */

#[repr(u8)]
#[derive(Copy, Clone, Debug, FromPrimitive, ToPrimitive, PartialEq)]
pub enum PathType {
    /// The folder where crash reports are generated.
    CrashReports = 1,
}

pub struct SetPath {
    pub path_type: PathType,
    pub path: OsString,
}

impl SetPath {
    pub fn new(path_type: PathType, path: OsString) -> Self {
        Self { path_type, path }
    }
}

impl Message for SetPath {
    fn kind() -> Kind {
        Kind::SetPath
    }

    fn payload_size(&self) -> usize {
        let path_len = self.path.bytes_len();
        (size_of::<PathType>() + size_of::<usize>())
            .checked_add(path_len)
            .unwrap()
    }

    fn ancillary_data_len(&self) -> usize {
        0
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());
        let mut payload = BytesMut::with_capacity(self.payload_size());
        let path = self.path.serialize();
        payload.put_u8(self.path_type as u8);
        payload.put_vec(&path);

        (header, payload.freeze(), vec![])
    }

    fn decode(data: Vec<u8>, ancillary_data: Vec<AncillaryData>) -> Result<Self, MessageError> {
        if !ancillary_data.is_empty() {
            return Err(MessageError::UnexpectedAncillaryData);
        }

        let mut data = Bytes::from(data);

        let path_type = data.try_get_u8()?;
        let path_type = PathType::from_u8(path_type).ok_or(MessageError::InvalidData)?;
        let path = data.try_get_vec()?;
        let path = <OsString as BreakpadString>::deserialize(path)
            .map_err(|_| MessageError::InvalidData)?;

        Ok(Self { path_type, path })
    }
}

/* Message used to retrieve a path used by the crash helper. */

pub struct GetPath {
    pub path_type: PathType,
}

impl GetPath {
    pub fn new(path_type: PathType) -> Self {
        Self { path_type }
    }
}

impl Message for GetPath {
    fn kind() -> Kind {
        Kind::GetPath
    }

    fn payload_size(&self) -> usize {
        size_of::<PathType>()
    }

    fn ancillary_data_len(&self) -> usize {
        0
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());
        let mut payload = BytesMut::with_capacity(self.payload_size());
        payload.put_u8(self.path_type as u8);

        (header, payload.freeze(), vec![])
    }

    fn decode(data: Vec<u8>, ancillary_data: Vec<AncillaryData>) -> Result<Self, MessageError> {
        if !ancillary_data.is_empty() {
            return Err(MessageError::UnexpectedAncillaryData);
        }

        let mut data = Bytes::from(data);

        let path_type = data.try_get_u8()?;
        let path_type = PathType::from_u8(path_type).ok_or(MessageError::InvalidData)?;

        Ok(Self { path_type })
    }
}

/* Message used as a reply to a `GetPath` message. */

pub struct PathReply {
    pub path: OsString,
}

impl PathReply {
    pub fn new(path: OsString) -> Self {
        Self { path }
    }
}

impl Message for PathReply {
    fn kind() -> Kind {
        Kind::PathReply
    }

    fn payload_size(&self) -> usize {
        let path_len = self.path.bytes_len();
        size_of::<usize>().checked_add(path_len).unwrap()
    }

    fn ancillary_data_len(&self) -> usize {
        0
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());
        let mut payload = BytesMut::with_capacity(self.payload_size());
        let path = self.path.serialize();
        payload.put_vec(&path);

        (header, payload.freeze(), vec![])
    }

    fn decode(data: Vec<u8>, ancillary_data: Vec<AncillaryData>) -> Result<Self, MessageError> {
        if !ancillary_data.is_empty() {
            return Err(MessageError::UnexpectedAncillaryData);
        }

        let mut data = Bytes::from(data);

        let path = data.try_get_vec()?;
        let path = <OsString as BreakpadString>::deserialize(path)
            .map_err(|_| MessageError::InvalidData)?;

        Ok(Self { path })
    }
}

/* Transfer minidump message, used to request the minidump which has been
 * generated for the specified process id. */

pub struct TransferMinidump {
    pub id: GeckoChildId,
}

impl TransferMinidump {
    pub fn new(id: GeckoChildId) -> TransferMinidump {
        TransferMinidump { id }
    }
}

impl Message for TransferMinidump {
    fn kind() -> Kind {
        Kind::TransferMinidump
    }

    fn payload_size(&self) -> usize {
        size_of::<GeckoChildId>()
    }

    fn ancillary_data_len(&self) -> usize {
        0
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());
        let mut payload = BytesMut::with_capacity(self.payload_size());
        payload.put_i32_ne(self.id);

        (header, payload.freeze(), vec![])
    }

    fn decode(data: Vec<u8>, ancillary_data: Vec<AncillaryData>) -> Result<Self, MessageError> {
        if !ancillary_data.is_empty() {
            return Err(MessageError::UnexpectedAncillaryData);
        }

        let mut data = Bytes::from(data);
        let id = data.try_get_i32_ne()?;

        Ok(TransferMinidump { id })
    }
}

/* Generate minidump message, used to request the generation of a minidump for
 * the specified process id. */

const GENERATE_MINIDUMP_ANCILLARY_DATA_LEN: usize =
    if cfg!(any(target_os = "ios", target_os = "macos")) {
        1
    } else {
        0
    };

pub struct GenerateMinidump {
    pub id: GeckoChildId,
    pub target_thread: ThreadHandle,
}

impl GenerateMinidump {
    pub fn new(id: GeckoChildId, target_thread: ThreadHandle) -> Self {
        Self { id, target_thread }
    }
}

impl Message for GenerateMinidump {
    fn kind() -> Kind {
        Kind::GenerateMinidump
    }

    fn payload_size(&self) -> usize {
        size_of::<GeckoChildId>()
            + if GENERATE_MINIDUMP_ANCILLARY_DATA_LEN == 0 {
                size_of::<RawThreadHandle>()
            } else {
                0
            }
    }

    fn ancillary_data_len(&self) -> usize {
        GENERATE_MINIDUMP_ANCILLARY_DATA_LEN
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());
        let mut payload = BytesMut::with_capacity(self.payload_size());
        payload.put_i32_ne(self.id);
        #[cfg(not(any(target_os = "ios", target_os = "macos")))]
        payload.put_i32_ne(self.target_thread.as_raw_handle());

        #[cfg(any(target_os = "ios", target_os = "macos"))]
        let ancillary_data = vec![crate::MachPortRight::Send(self.target_thread)];
        #[cfg(not(any(target_os = "ios", target_os = "macos")))]
        let ancillary_data = vec![];

        (header, payload.freeze(), ancillary_data)
    }

    fn decode(data: Vec<u8>, ancillary_data: Vec<AncillaryData>) -> Result<Self, MessageError> {
        #[allow(clippy::absurd_extreme_comparisons)]
        if ancillary_data.len() > GENERATE_MINIDUMP_ANCILLARY_DATA_LEN {
            return Err(MessageError::UnexpectedAncillaryData);
        }

        let mut data = Bytes::from(data);
        let id = data.try_get_i32_ne()?;
        #[cfg(not(any(target_os = "ios", target_os = "macos")))]
        let target_thread = unsafe { ThreadHandle::from_raw_handle(data.try_get_i32_ne()?) };
        #[cfg(any(target_os = "ios", target_os = "macos"))]
        let target_thread = {
            let Some(task_right) = ancillary_data.into_iter().next() else {
                return Err(MessageError::MissingAncillary);
            };

            match task_right {
                crate::MachPortRight::Send(task_right) => task_right,
                _ => {
                    return Err(MessageError::InvalidAncillary);
                }
            }
        };

        Ok(Self { id, target_thread })
    }
}

/* Minidump generation reply, received from the server after having sent a
 * `TransferMinidump` or `GenerateMinidump` message. */

pub struct MinidumpReply {
    pub path: OsString,
    pub error: Option<CString>,
}

impl MinidumpReply {
    pub fn new(path: OsString, error: Option<CString>) -> Self {
        Self { path, error }
    }
}

impl Message for MinidumpReply {
    fn kind() -> Kind {
        Kind::MinidumpReply
    }

    fn payload_size(&self) -> usize {
        let path_len = self.path.bytes_len();
        (size_of::<usize>() * 2)
            .checked_add(path_len)
            .and_then(|l| {
                l.checked_add(
                    self.error
                        .as_ref()
                        .map_or(0, |error| error.as_bytes().len()),
                )
            })
            .unwrap()
    }

    fn ancillary_data_len(&self) -> usize {
        0
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());
        let mut payload = BytesMut::with_capacity(self.payload_size());

        let path_bytes = self.path.serialize();
        let error_bytes = self
            .error
            .as_ref()
            .map_or(Vec::new(), |error| Vec::from(error.as_bytes()));

        payload.put_vec(&path_bytes);
        payload.put_vec(&error_bytes);

        (header, payload.freeze(), vec![])
    }

    fn decode(data: Vec<u8>, ancillary_data: Vec<AncillaryData>) -> Result<Self, MessageError> {
        if !ancillary_data.is_empty() {
            return Err(MessageError::UnexpectedAncillaryData);
        }

        let mut data = Bytes::from(data);

        let path = data.try_get_vec()?;
        let path = <OsString as BreakpadString>::deserialize(path)
            .map_err(|_| MessageError::InvalidData)?;

        let error = data.try_get_vec()?;
        let error = if error.is_empty() {
            None
        } else {
            Some(CString::new(error)?)
        };

        Ok(Self::new(path, error))
    }
}

/* Generate a minidump based on information captured by the Windows Error Reporting runtime exception module. */

#[cfg(target_os = "windows")]
pub struct WindowsErrorReportingMinidump {
    pub process_id: u32,
    pub thread_id: u32,
    pub exception_records: Vec<EXCEPTION_RECORD>,
    pub context: CONTEXT,
}

#[cfg(target_os = "windows")]
impl WindowsErrorReportingMinidump {
    pub fn new(
        process_id: u32,
        thread_id: u32,
        exception_records: Vec<EXCEPTION_RECORD>,
        context: CONTEXT,
    ) -> WindowsErrorReportingMinidump {
        WindowsErrorReportingMinidump {
            process_id,
            thread_id,
            exception_records,
            context,
        }
    }
}

// The size of the structure minus the `ExceptionRecord` pointer and the
// 4-bytes padding after the `NumberParameters` field.
#[cfg(all(target_os = "windows", target_pointer_width = "64"))]
const EXCEPTION_RECORD_SERIALIZED_SIZE: usize =
    size_of::<EXCEPTION_RECORD>() - (size_of::<usize>() + size_of::<u32>());

// The size of the structure minus the `ExceptionRecord` pointer.
#[cfg(all(target_os = "windows", target_pointer_width = "32"))]
const EXCEPTION_RECORD_SERIALIZED_SIZE: usize = size_of::<EXCEPTION_RECORD>() - size_of::<usize>();

#[cfg(target_os = "windows")]
impl Message for WindowsErrorReportingMinidump {
    fn kind() -> Kind {
        Kind::WindowsErrorReporting
    }

    fn payload_size(&self) -> usize {
        // This is process_id (u32), plus thread_id (u32), plus number of
        // elements in the exception record array (usize), plus the size of the
        // exception record array plus the size of the context.
        (size_of::<u32>() * 2)
            + size_of::<usize>() // number of elements in the exception record array
            + (EXCEPTION_RECORD_SERIALIZED_SIZE * self.exception_records.len())
            + size_of::<CONTEXT>()
    }

    fn ancillary_data_len(&self) -> usize {
        0
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());
        let mut payload = BytesMut::with_capacity(self.payload_size());

        payload.put_u32_ne(self.process_id);
        payload.put_u32_ne(self.thread_id);
        payload.put_usize_ne(self.exception_records.len());
        for exception_record in self.exception_records.iter() {
            payload.put_exception_record(exception_record);
        }
        payload.put_context(&self.context);

        (header, payload.freeze(), vec![])
    }

    fn decode(
        data: Vec<u8>,
        ancillary_data: Vec<AncillaryData>,
    ) -> Result<WindowsErrorReportingMinidump, MessageError> {
        if !ancillary_data.is_empty() {
            return Err(MessageError::UnexpectedAncillaryData);
        }

        let mut bytes = Bytes::from(data);
        let process_id = bytes.try_get_u32_ne()?;
        let thread_id = bytes.try_get_u32_ne()?;
        let exception_records_n = bytes.try_get_usize_ne()?;

        let mut exception_records = Vec::<EXCEPTION_RECORD>::with_capacity(exception_records_n);
        for _i in 0..exception_records_n {
            let exception_record = bytes.try_get_exception_record()?;
            exception_records.push(exception_record);
        }

        let context = bytes.try_get_context()?;

        Ok(WindowsErrorReportingMinidump {
            process_id,
            thread_id,
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
}

#[cfg(target_os = "windows")]
impl Message for WindowsErrorReportingMinidumpReply {
    fn kind() -> Kind {
        Kind::WindowsErrorReportingReply
    }

    fn payload_size(&self) -> usize {
        0
    }

    fn ancillary_data_len(&self) -> usize {
        0
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());

        (header, Bytes::new(), vec![])
    }

    fn decode(
        data: Vec<u8>,
        ancillary_data: Vec<AncillaryData>,
    ) -> Result<WindowsErrorReportingMinidumpReply, MessageError> {
        if !ancillary_data.is_empty() {
            return Err(MessageError::UnexpectedAncillaryData);
        }

        if !data.is_empty() {
            return Err(MessageError::InvalidData);
        }

        Ok(WindowsErrorReportingMinidumpReply::new())
    }
}

/* Message used to send information about a process' auxiliary vector. */

#[cfg(any(target_os = "android", target_os = "linux"))]
pub struct RegisterAuxvInfo {
    pub id: GeckoChildId,
    pub auxv_info: DirectAuxvDumpInfo,
}

#[cfg(any(target_os = "android", target_os = "linux"))]
impl RegisterAuxvInfo {
    pub fn new(id: GeckoChildId, auxv_info: DirectAuxvDumpInfo) -> RegisterAuxvInfo {
        RegisterAuxvInfo { id, auxv_info }
    }
}

#[cfg(any(target_os = "android", target_os = "linux"))]
impl Message for RegisterAuxvInfo {
    fn kind() -> Kind {
        Kind::RegisterAuxvInfo
    }

    fn payload_size(&self) -> usize {
        // A bit hacky but we'll change this when we make
        // serialization/deserialization later.
        size_of::<GeckoChildId>() + (size_of::<AuxvType>() * 4)
    }

    fn ancillary_data_len(&self) -> usize {
        0
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());
        let mut payload = BytesMut::with_capacity(self.payload_size());

        payload.put_i32_ne(self.id);
        // AuxvType is the size of a pointer
        payload.put_usize_ne(self.auxv_info.program_header_count as usize);
        payload.put_usize_ne(self.auxv_info.program_header_address as usize);
        payload.put_usize_ne(self.auxv_info.linux_gate_address as usize);
        payload.put_usize_ne(self.auxv_info.entry_address as usize);

        (header, payload.freeze(), vec![])
    }

    fn decode(
        data: Vec<u8>,
        ancillary_data: Vec<AncillaryData>,
    ) -> Result<RegisterAuxvInfo, MessageError> {
        if !ancillary_data.is_empty() {
            return Err(MessageError::UnexpectedAncillaryData);
        }

        let mut data = Bytes::from(data);

        let id = data.try_get_i32_ne()?;
        let program_header_count = data.try_get_usize_ne()? as AuxvType;
        let program_header_address = data.try_get_usize_ne()? as AuxvType;
        let linux_gate_address = data.try_get_usize_ne()? as AuxvType;
        let entry_address = data.try_get_usize_ne()? as AuxvType;

        let auxv_info = DirectAuxvDumpInfo {
            program_header_count,
            program_header_address,
            entry_address,
            linux_gate_address,
        };

        Ok(RegisterAuxvInfo { id, auxv_info })
    }
}

/* Message used to inform the crash helper that a process' auxiliary vector
 * information is not needed anymore. */

#[cfg(any(target_os = "android", target_os = "linux"))]
pub struct UnregisterAuxvInfo {
    pub id: GeckoChildId,
}

#[cfg(any(target_os = "android", target_os = "linux"))]
impl UnregisterAuxvInfo {
    pub fn new(id: GeckoChildId) -> UnregisterAuxvInfo {
        UnregisterAuxvInfo { id }
    }
}

#[cfg(any(target_os = "android", target_os = "linux"))]
impl Message for UnregisterAuxvInfo {
    fn kind() -> Kind {
        Kind::UnregisterAuxvInfo
    }

    fn payload_size(&self) -> usize {
        size_of::<GeckoChildId>()
    }

    fn ancillary_data_len(&self) -> usize {
        0
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());
        let mut payload = BytesMut::with_capacity(self.payload_size());

        payload.put_i32_ne(self.id);

        (header, payload.freeze(), vec![])
    }

    fn decode(
        data: Vec<u8>,
        ancillary_data: Vec<AncillaryData>,
    ) -> Result<UnregisterAuxvInfo, MessageError> {
        if !ancillary_data.is_empty() {
            return Err(MessageError::UnexpectedAncillaryData);
        }

        let mut data = Bytes::from(data);

        let id = data.try_get_i32_ne()?;

        Ok(UnregisterAuxvInfo { id })
    }
}

/* Message sent from the main process to the crash helper to register a new
 * child process which is about to be spawned. This message contains the IPC
 * endpoint which the crash helper will use to talk to the child. */

pub struct RegisterChildProcess {
    pub id: GeckoChildId,
    pub ancillary_data: [AncillaryData; CONNECTOR_ANCILLARY_DATA_LEN],
}

impl RegisterChildProcess {
    pub fn new(
        id: GeckoChildId,
        ancillary_data: [AncillaryData; CONNECTOR_ANCILLARY_DATA_LEN],
    ) -> RegisterChildProcess {
        RegisterChildProcess { id, ancillary_data }
    }
}

impl Message for RegisterChildProcess {
    fn kind() -> Kind {
        Kind::RegisterChildProcess
    }

    fn payload_size(&self) -> usize {
        size_of::<GeckoChildId>()
    }

    fn ancillary_data_len(&self) -> usize {
        self.ancillary_data.len()
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());
        let mut payload = BytesMut::with_capacity(self.payload_size());

        payload.put_i32_ne(self.id);

        (header, payload.freeze(), self.ancillary_data.into())
    }

    fn decode(
        data: Vec<u8>,
        ancillary_data: Vec<AncillaryData>,
    ) -> Result<RegisterChildProcess, MessageError> {
        let mut data = Bytes::from(data);

        let id = data.try_get_i32_ne()?;

        let mut iter = ancillary_data.into_iter();
        #[cfg(any(target_os = "ios", target_os = "macos"))]
        let ancillary_data: [AncillaryData; CONNECTOR_ANCILLARY_DATA_LEN] = {
            let send_right = iter.next().ok_or(MessageError::MissingAncillary)?;
            let receive_right = iter.next().ok_or(MessageError::MissingAncillary)?;
            [send_right, receive_right]
        };
        #[cfg(not(any(target_os = "ios", target_os = "macos")))]
        let ancillary_data: [AncillaryData; CONNECTOR_ANCILLARY_DATA_LEN] =
            [iter.next().ok_or(MessageError::MissingAncillary)?];

        Ok(RegisterChildProcess { id, ancillary_data })
    }
}

/* Message sent by all processes checking-in with the crash helper. After
 * this message has been received the crash helper is capable of dumping. The
 * message contains information on whether the sending process managed to set
 * itself up for being dumped, its PID plus platform-specific information that
 * might be needed by the crash helper to dump it. */

pub struct ProcessRendezVous {
    pub dumpable: bool,
    pub child_pid: Pid,
    pub id: GeckoChildId,
    pub ancillary_data: [AncillaryData; PROCESS_RENDEZVOUS_ANCILLARY_DATA_LEN],
}

impl ProcessRendezVous {
    pub fn new(
        dumpable: bool,
        child_pid: Pid,
        id: GeckoChildId,
        ancillary_data: [AncillaryData; PROCESS_RENDEZVOUS_ANCILLARY_DATA_LEN],
    ) -> ProcessRendezVous {
        ProcessRendezVous {
            dumpable,
            child_pid,
            id,
            ancillary_data,
        }
    }

    pub fn get_process_handle(self) -> ProcessHandle {
        #[cfg(target_os = "windows")]
        {
            let handle = self.ancillary_data.into_iter().next().unwrap();
            ProcessHandle(handle)
        }
        #[cfg(any(target_os = "android", target_os = "linux"))]
        {
            use crate::FromRawProcessHandle;
            ProcessHandle::from_raw_handle(self.child_pid)
        }
        #[cfg(any(target_os = "ios", target_os = "macos"))]
        {
            let task_right = self.ancillary_data.into_iter().next().unwrap();
            match task_right {
                crate::MachPortRight::Send(task_right) => task_right,
                _ => {
                    panic!("Wrong task right was provided")
                }
            }
        }
    }
}

impl Message for ProcessRendezVous {
    fn kind() -> Kind {
        Kind::ProcessRendezVous
    }

    fn payload_size(&self) -> usize {
        size_of::<u8>() + size_of::<Pid>() + size_of::<GeckoChildId>()
    }

    fn ancillary_data_len(&self) -> usize {
        PROCESS_RENDEZVOUS_ANCILLARY_DATA_LEN
    }

    fn encode(self) -> (Bytes, Bytes, Vec<AncillaryData>) {
        let header = Header::encode(Self::kind(), self.payload_size());
        let mut payload = BytesMut::with_capacity(self.payload_size());

        payload.put_u8(self.dumpable.into());
        payload.put_pid_ne(self.child_pid);
        payload.put_i32_ne(self.id);

        (header, payload.freeze(), self.ancillary_data.into())
    }

    fn decode(
        data: Vec<u8>,
        ancillary_data: Vec<AncillaryData>,
    ) -> Result<ProcessRendezVous, MessageError> {
        #[allow(clippy::absurd_extreme_comparisons)]
        if ancillary_data.len() < PROCESS_RENDEZVOUS_ANCILLARY_DATA_LEN {
            return Err(MessageError::MissingAncillary);
        } else if ancillary_data.len() > PROCESS_RENDEZVOUS_ANCILLARY_DATA_LEN {
            return Err(MessageError::UnexpectedAncillaryData);
        }

        let mut data = Bytes::from(data);
        let dumpable = data.try_get_u8()? != 0;
        let child_pid = data.try_get_pid_ne()?;
        let id = data.try_get_i32_ne()?;
        let ancillary_data = ancillary_data.try_into().unwrap();

        Ok(ProcessRendezVous {
            dumpable,
            child_pid,
            id,
            ancillary_data,
        })
    }
}
