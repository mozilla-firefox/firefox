/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

pub mod crash_annotations {
    include!(concat!(env!("OUT_DIR"), "/crash_annotations.rs"));
}

#[cfg(target_os = "windows")]
mod windows;

use anyhow::{bail, Result};
use crash_annotations::{
    should_include_annotation, type_of_annotation, CrashAnnotation, CrashAnnotationType,
};
use crash_helper_common::{
    messages::{self, Message},
    AncillaryData, BreakpadChar, BreakpadData, BreakpadString, Pid,
};
#[cfg(any(target_os = "android", target_os = "linux"))]
use minidump_writer::minidump_writer::DirectAuxvDumpInfo;
use mozannotation_server::{AnnotationData, CAnnotation};
use num_traits::FromPrimitive;
use once_cell::sync::Lazy;
use std::{
    collections::HashMap,
    convert::TryInto,
    ffi::{c_char, CStr, CString, OsStr, OsString},
    fs::File,
    io::{Seek, SeekFrom, Write},
    mem::size_of,
    path::{Path, PathBuf},
    sync::Mutex,
};
#[cfg(target_os = "windows")]
use windows_sys::Win32::Foundation::HANDLE;

use crate::{
    breakpad_crash_generator::BreakpadCrashGenerator,
    phc::{self, StackTrace},
};

struct CrashReport {
    path: OsString,
    error: Option<CString>,
}

impl CrashReport {
    fn new(path: &OsStr, error: &Option<CString>) -> CrashReport {
        CrashReport {
            path: path.to_owned(),
            error: error.to_owned(),
        }
    }
}

// Table holding all the crash reports we've generated. It's indexed by PID and
// new crash reports are insterted in the corresponding vector in order of
// arrival. When crashes are retrieved they're similarly pulled out in the
// order they've arrived.
static CRASH_REPORTS: Lazy<Mutex<HashMap<Pid, Vec<CrashReport>>>> = Lazy::new(Default::default);

// Table holding the information about the auxiliary vector of potentially
// every process registered with the crash helper.
#[cfg(any(target_os = "android", target_os = "linux"))]
static AUXV_INFO_MAP: Lazy<Mutex<HashMap<Pid, DirectAuxvDumpInfo>>> = Lazy::new(Default::default);

/******************************************************************************
 * Crash generator                                                            *
 ******************************************************************************/

#[derive(PartialEq)]
enum MinidumpOrigin {
    Breakpad,
    WindowsErrorReporting,
}

pub(crate) struct CrashGenerator {
    // This will be used for generating hangs
    _minidump_path: OsString,
    breakpad_server: BreakpadCrashGenerator,
    client_pid: Pid,
}

impl CrashGenerator {
    pub(crate) fn new(
        client_pid: Pid,
        breakpad_data: BreakpadData,
        minidump_path: OsString,
    ) -> Result<CrashGenerator> {
        let breakpad_server = BreakpadCrashGenerator::new(
            breakpad_data,
            minidump_path.clone(),
            finalize_breakpad_minidump,
            #[cfg(any(target_os = "android", target_os = "linux"))]
            get_auxv_info,
        )?;

        Ok(CrashGenerator {
            _minidump_path: minidump_path,
            breakpad_server,
            client_pid,
        })
    }

    // Process a message received from the client. Return an optional reply
    // that will be sent back to the client.
    pub(crate) fn client_message(
        &mut self,
        kind: messages::Kind,
        data: &[u8],
        ancillary_data: Option<AncillaryData>,
        pid: Pid,
    ) -> Result<Option<Box<dyn Message>>> {
        match kind {
            messages::Kind::SetCrashReportPath => {
                if pid != self.client_pid {
                    panic!("Not connected or attempting to set the path from the wrong process");
                }

                let message = messages::SetCrashReportPath::decode(data, ancillary_data)?;
                self.set_path(message.path);
                Ok(None)
            }
            messages::Kind::TransferMinidump => {
                if pid != self.client_pid {
                    panic!(
                        "Not connected or attempting to request a minidump from a child process"
                    );
                }

                let message = messages::TransferMinidump::decode(data, ancillary_data)?;
                Ok(Some(Box::new(self.transfer_minidump(message.pid))))
            }
            messages::Kind::GenerateMinidump => {
                todo!("Implement all messages");
            }
            #[cfg(target_os = "windows")]
            messages::Kind::WindowsErrorReporting => {
                let message =
                    messages::WindowsErrorReportingMinidump::decode(data, ancillary_data)?;
                let _ = self.generate_wer_minidump(message);
                Ok(Some(Box::new(
                    messages::WindowsErrorReportingMinidumpReply::new(),
                )))
            }
            #[cfg(any(target_os = "android", target_os = "linux"))]
            messages::Kind::RegisterAuxvInfo => {
                if pid != self.client_pid {
                    panic!(
                        "Attempting to register some auxiliary information from the wrong process"
                    );
                }

                let message = messages::RegisterAuxvInfo::decode(data, ancillary_data)?;
                let map = &mut AUXV_INFO_MAP.lock().unwrap();
                map.insert(message.pid, message.auxv_info);

                Ok(None)
            }
            #[cfg(any(target_os = "android", target_os = "linux"))]
            messages::Kind::UnregisterAuxvInfo => {
                if pid != self.client_pid {
                    panic!("Attempting to unregister auxiliary information from the wrong process");
                }

                let message = messages::UnregisterAuxvInfo::decode(data, ancillary_data)?;
                let map = &mut AUXV_INFO_MAP.lock().unwrap();
                map.remove(&message.pid);

                Ok(None)
            }
            kind => {
                bail!("Unexpected message {:?}", kind);
            }
        }
    }

    fn set_path(&mut self, path: OsString) {
        self.breakpad_server.set_path(path);
    }

    fn transfer_minidump(&self, pid: Pid) -> messages::TransferMinidumpReply {
        let mut map = CRASH_REPORTS.lock().unwrap();
        if let Some(mut entry) = map.remove(&pid) {
            let crash_report = entry.remove(0);

            if !entry.is_empty() {
                map.insert(pid, entry);
            }

            messages::TransferMinidumpReply::new(crash_report.path, crash_report.error)
        } else {
            // Report not found, reply with a zero length path
            messages::TransferMinidumpReply::new(OsString::new(), None)
        }
    }
}

/******************************************************************************
 * Crash annotations                                                          *
 ******************************************************************************/

macro_rules! read_numeric_annotation {
    ($t:ty,$d:expr) => {
        if let AnnotationData::ByteBuffer(buff) = $d {
            if buff.len() == size_of::<$t>() {
                let value = buff.get(0..size_of::<$t>()).map(|bytes| {
                    let bytes: [u8; size_of::<$t>()] = bytes.try_into().unwrap();
                    <$t>::from_ne_bytes(bytes)
                });
                value.map(|value| value.to_string().into_bytes())
            } else {
                None
            }
        } else {
            None
        }
    };
}

fn write_phc_annotations(file: &mut File, buff: &[u8]) -> Result<()> {
    let addr_info = phc::AddrInfo::from_bytes(buff)?;
    if addr_info.kind == phc::Kind::Unknown {
        return Ok(());
    }

    write!(
        file,
        "\"PHCKind\":\"{}\",\
            \"PHCBaseAddress\":\"{}\",\
            \"PHCUsableSize\":\"{}\",",
        addr_info.kind_as_str(),
        addr_info.base_addr as usize,
        addr_info.usable_size,
    )?;

    if addr_info.alloc_stack.has_stack != 0 {
        write!(
            file,
            "\"PHCAllocStack\":\"{}\",",
            serialize_phc_stack(&addr_info.alloc_stack)
        )?;
    }

    if addr_info.free_stack.has_stack != 0 {
        write!(
            file,
            "\"PHCFreeStack\":\"{}\",",
            serialize_phc_stack(&addr_info.free_stack)
        )?;
    }

    Ok(())
}

fn serialize_phc_stack(stack_trace: &StackTrace) -> String {
    let mut string = String::new();
    for i in 0..stack_trace.length {
        string.push_str(&(stack_trace.pcs[i] as usize).to_string());
        string.push(',');
    }

    string.pop();
    string
}

#[repr(C)]
pub struct BreakpadProcessId {
    pub pid: Pid,
    #[cfg(target_os = "macos")]
    pub task: u32,
    #[cfg(target_os = "windows")]
    pub handle: HANDLE,
}

/// This reads the crash annotations, writes them to the .extra file and
/// finally stores the resulting minidump in the global hash table.
extern "C" fn finalize_breakpad_minidump(
    process_id: BreakpadProcessId,
    error_ptr: *const c_char,
    minidump_path_ptr: *const BreakpadChar,
) {
    let minidump_path =
        PathBuf::from(unsafe { <OsString as BreakpadString>::from_ptr(minidump_path_ptr) });
    let error = if !error_ptr.is_null() {
        // SAFETY: The string is a valid C string we passed in ourselves.
        Some(unsafe { CStr::from_ptr(error_ptr) }.to_owned())
    } else {
        None
    };

    finalize_crash_report(process_id, error, &minidump_path, MinidumpOrigin::Breakpad);
}

fn finalize_crash_report(
    process_id: BreakpadProcessId,
    error: Option<CString>,
    minidump_path: &Path,
    origin: MinidumpOrigin,
) {
    let mut extra_path = PathBuf::from(minidump_path);
    extra_path.set_extension("extra");

    let annotations = retrieve_annotations(&process_id, origin);
    let extra_file_written = annotations
        .map(|annotations| write_extra_file(&annotations, &extra_path))
        .is_ok();

    let path = minidump_path.as_os_str();
    let error = if !extra_file_written {
        Some(CString::new("MissingAnnotations").unwrap())
    } else {
        error
    };

    let map = &mut CRASH_REPORTS.lock().unwrap();
    let entry = map.entry(process_id.pid);
    entry
        .and_modify(|entry| entry.push(CrashReport::new(path, &error)))
        .or_insert_with(|| vec![CrashReport::new(path, &error)]);
}

#[cfg(any(target_os = "android", target_os = "linux"))]
extern "C" fn get_auxv_info(pid: Pid, auxv_info_ptr: *mut DirectAuxvDumpInfo) -> bool {
    let map = &mut AUXV_INFO_MAP.lock().unwrap();

    if let Some(auxv_info) = map.get(&pid) {
        // SAFETY: The auxv_info_ptr is guaranteed to be valid by the caller.
        unsafe { auxv_info_ptr.write(auxv_info.to_owned()) };
        true
    } else {
        false
    }
}

fn retrieve_annotations(
    process_id: &BreakpadProcessId,
    origin: MinidumpOrigin,
) -> Result<Vec<CAnnotation>> {
    #[cfg(target_os = "windows")]
    let res = mozannotation_server::retrieve_annotations(
        process_id.handle,
        CrashAnnotation::Count as usize,
    );
    #[cfg(any(target_os = "linux", target_os = "android"))]
    let res =
        mozannotation_server::retrieve_annotations(process_id.pid, CrashAnnotation::Count as usize);
    #[cfg(target_os = "macos")]
    let res = mozannotation_server::retrieve_annotations(
        process_id.task,
        CrashAnnotation::Count as usize,
    );

    let mut annotations = res?;
    if origin == MinidumpOrigin::WindowsErrorReporting {
        annotations.push(CAnnotation {
            id: CrashAnnotation::WindowsErrorReporting as u32,
            data: AnnotationData::ByteBuffer(vec![1]),
        });
    }

    Ok(annotations)
}

fn write_extra_file(annotations: &Vec<CAnnotation>, path: &Path) -> Result<()> {
    let mut annotations_written: usize = 0;
    let mut file = File::create(path)?;
    write!(&mut file, "{{")?;

    for annotation in annotations {
        if let Some(annotation_id) = CrashAnnotation::from_u32(annotation.id) {
            if annotation_id == CrashAnnotation::PHCBaseAddress {
                if let AnnotationData::ByteBuffer(buff) = &annotation.data {
                    write_phc_annotations(&mut file, buff)?;
                }

                continue;
            }

            let value = match type_of_annotation(annotation_id) {
                CrashAnnotationType::String => match &annotation.data {
                    AnnotationData::String(string) => Some(escape_value(string.as_bytes())),
                    AnnotationData::ByteBuffer(buffer) => Some(escape_value(buffer)),
                    _ => None,
                },
                CrashAnnotationType::Boolean => {
                    if let AnnotationData::ByteBuffer(buff) = &annotation.data {
                        if buff.len() == 1 {
                            Some(vec![if buff[0] != 0 { b'1' } else { b'0' }])
                        } else {
                            None
                        }
                    } else {
                        None
                    }
                }
                CrashAnnotationType::U32 => {
                    read_numeric_annotation!(u32, &annotation.data)
                }
                CrashAnnotationType::U64 => {
                    read_numeric_annotation!(u64, &annotation.data)
                }
                CrashAnnotationType::USize => {
                    read_numeric_annotation!(usize, &annotation.data)
                }
                CrashAnnotationType::Object => None, // This cannot be found in memory
            };

            if let Some(value) = value {
                if !value.is_empty() && should_include_annotation(annotation_id, &value) {
                    write!(&mut file, "\"{annotation_id:}\":\"")?;
                    file.write_all(&value)?;
                    write!(&mut file, "\",")?;
                    annotations_written += 1;
                }
            }
        }
    }

    if annotations_written > 0 {
        // Drop the last comma
        file.seek(SeekFrom::Current(-1))?;
    }
    writeln!(&mut file, "}}")?;
    Ok(())
}

// Escapes the characters of a crash annotation so that they appear correctly
// within the JSON output, escaping non-visible characters and the like. This
// does not try to make the output valid UTF-8 because the input might be
// corrupted so there's no point in that.
fn escape_value(input: &[u8]) -> Vec<u8> {
    let mut escaped = Vec::<u8>::with_capacity(input.len() + 2);
    for &c in input {
        if c <= 0x1f || c == b'\\' || c == b'"' {
            escaped.extend(b"\\u00");
            escaped.push(hex_digit_as_ascii_char((c & 0x00f0) >> 4));
            escaped.push(hex_digit_as_ascii_char(c & 0x000f));
        } else {
            escaped.push(c)
        }
    }

    escaped
}

fn hex_digit_as_ascii_char(value: u8) -> u8 {
    if value < 10 {
        b'0' + value
    } else {
        b'a' + (value - 10)
    }
}
