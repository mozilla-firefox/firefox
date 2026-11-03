/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::{
    breakpad_crash_generator::{BreakpadCrashGenerator, BreakpadProcessId},
    phc::{self, StackTrace},
};

#[cfg(any(target_os = "android", target_os = "linux"))]
mod linux;
#[cfg(any(target_os = "android", target_os = "linux"))]
pub(crate) use linux::get_auxv_info;
#[cfg(any(target_os = "android", target_os = "linux"))]
use linux::{create_platform_specific_annotations, process_uptime};

#[cfg(any(target_os = "ios", target_os = "macos"))]
mod macos;
#[cfg(any(target_os = "ios", target_os = "macos"))]
use macos::{create_platform_specific_annotations, process_uptime};

#[cfg(target_os = "windows")]
mod windows;
#[cfg(target_os = "windows")]
use windows::{create_platform_specific_annotations, process_uptime};

use anyhow::{Context, Result};
use crash_helper_common::{
    ApplicationInfo, AsRawProcessHandle, AsRawThreadHandle, BreakpadChar, BreakpadString,
    ExtraCrashData, GeckoChildId, Pid, ProcessHandle, RawProcessHandle, ThreadHandle,
    crash_annotations::{
        CrashAnnotation, CrashAnnotationType, should_include_annotation, type_of_annotation,
    },
};
use mozannotation_server::{AnnotationData, CAnnotation, errors::AnnotationsRetrievalError};
use num_traits::FromPrimitive;
use std::{
    collections::HashMap,
    convert::TryInto,
    ffi::{CString, OsStr, OsString, c_void},
    fs::File,
    io::{Seek, SeekFrom, Write},
    mem::size_of,
    path::{Path, PathBuf},
    sync::{Mutex, OnceLock},
    time::{Duration, SystemTime, UNIX_EPOCH},
};

pub(crate) struct CrashReport {
    pub(crate) path: OsString,
    pub(crate) error: Option<CString>,
}

impl CrashReport {
    fn new(path: &OsStr, error: &Option<CString>) -> CrashReport {
        CrashReport {
            path: path.to_owned(),
            error: error.to_owned(),
        }
    }
}

/******************************************************************************
 * Crash generator                                                            *
 ******************************************************************************/

#[derive(PartialEq)]
enum ProcessType {
    Parent,
    Child,
}

pub(crate) struct CrashGenerator
where
    // A reference to the `CrashGenerator` object is stored in the
    // `BreakpadContext` object and transferred in turn to the Breakpad crash
    // generation thread, so it needs to be `Send`.
    Self: Send,
{
    main_process_handle: ProcessHandle,
    minidump_path: PathBuf,
    reports_by_pid: HashMap<Pid, Vec<CrashReport>>,
    reports_by_id: HashMap<GeckoChildId, CrashReport>,
}

impl CrashGenerator {
    pub(crate) fn new(
        main_process_handle: ProcessHandle,
        minidump_path: OsString,
    ) -> CrashGenerator {
        CrashGenerator {
            main_process_handle,
            minidump_path: PathBuf::from(minidump_path),
            reports_by_pid: HashMap::<Pid, Vec<CrashReport>>::new(),
            reports_by_id: HashMap::<GeckoChildId, CrashReport>::new(),
        }
    }

    pub(crate) fn set_minidump_path(&mut self, path: &OsString) {
        self.minidump_path = PathBuf::from(path);
    }

    pub(crate) fn get_minidump_path(&self) -> OsString {
        self.minidump_path.as_os_str().to_owned()
    }

    pub(crate) fn move_report_to_id(&mut self, pid: Pid, id: GeckoChildId) {
        if let Some(crash_report) = self.retrieve_minidump_by_pid(pid) {
            self.reports_by_id.insert(id, crash_report);
        }
    }

    pub(crate) fn retrieve_minidump_by_pid(&mut self, pid: Pid) -> Option<CrashReport> {
        if let Some(mut entry) = self.reports_by_pid.remove(&pid) {
            let crash_report = entry.remove(0);

            if !entry.is_empty() {
                self.reports_by_pid.insert(pid, entry);
            }

            return Some(crash_report);
        }

        None
    }

    pub(crate) fn retrieve_minidump_by_id(&mut self, id: GeckoChildId) -> Option<CrashReport> {
        self.reports_by_id.remove(&id)
    }

    pub(crate) fn generate_minidump(
        &mut self,
        id: GeckoChildId,
        target_process: &ProcessHandle,
        target_thread: &ThreadHandle,
    ) -> Option<CrashReport> {
        let path = BreakpadCrashGenerator::generate_minidump(
            id,
            AsRawProcessHandle::as_raw_handle(target_process),
            AsRawThreadHandle::as_raw_handle(target_thread),
            self.minidump_path.clone(),
        );

        if let Some(path) = path {
            let error = self.finalize_crash_report(
                AsRawProcessHandle::as_raw_handle(target_process),
                /* extra_data */ None,
                &path,
                if id == 0 { ProcessType::Parent } else { ProcessType::Child },
            );
            Some(CrashReport::new(path.as_os_str(), &error))
        } else {
            None
        }
    }

    // This function assumes that the crashing process remains alive for the
    // entire duration of the call. This is currently guaranteed by calling
    // it from the Breakpad minidump callback which is guaranteed to run
    // synchronously before the process is killed. If the process has already
    // been killed this function will encounter and produce a partial report.
    fn finalize_crash_report(
        &self,
        process: RawProcessHandle,
        extra_data: Option<&ExtraCrashData>,
        minidump_path: &Path,
        process_type: ProcessType,
    ) -> Option<CString> {
        let (error, extra_annotations) = extra_data
            .map(|d| (d.error.clone(), d.annotations.clone()))
            .unwrap_or_default();
        let global_annotations = self.retrieve_main_process_annotations();
        let annotations = retrieve_annotations(process, process_type);
        let annotations = [
            STATIC_ANNOTATIONS.get().cloned().context("MissingStaticAnnotations"),
            Ok(self.crash_time_annotations(process)),
            global_annotations.context("MissingMainProcessAnnotations"),
            annotations.context("MissingChildProcessAnnotations"),
            Ok(extra_annotations),
        ]
        .into_iter()
        .fold(HashMap::new(), fold_annotations);

        let mut extra_path = PathBuf::from(minidump_path);
        extra_path.set_extension("extra");
        let extra_file_written = write_extra_file(annotations, &extra_path).is_ok();

        if !extra_file_written {
            Some(c"MissingAnnotations".to_owned())
        } else {
            error
        }
    }

    fn insert_crash_report(
        &mut self,
        process_id: &BreakpadProcessId,
        path: &Path,
        error: Option<CString>,
    ) {
        let path = path.as_os_str();
        let entry = self.reports_by_pid.entry(process_id.pid);
        entry
            .and_modify(|entry| entry.push(CrashReport::new(path, &error)))
            .or_insert_with(|| vec![CrashReport::new(path, &error)]);
    }

    fn retrieve_main_process_annotations(
        &self,
    ) -> Result<Vec<CAnnotation>, AnnotationsRetrievalError> {
        mozannotation_server::retrieve_annotations(
            AsRawProcessHandle::as_raw_handle(&self.main_process_handle),
            CrashAnnotation::Count as usize,
        )
    }

    fn crash_time_annotations(&self, process: RawProcessHandle) -> Vec<CAnnotation> {
        let crash_event_id = new_unique_crash_event_id();
        let crash_time = crash_time();
        let uptime_ts = process_uptime_ts(process);

        vec![
            make_annotation(CrashAnnotation::CrashEventID, &crash_event_id),
            make_annotation(CrashAnnotation::CrashTime, &crash_time),
            make_annotation(CrashAnnotation::UptimeTS, &uptime_ts),
        ]
    }
}

fn new_unique_crash_event_id() -> String {
    uuid::Uuid::new_v4()
        .as_hyphenated()
        .encode_lower(&mut uuid::Uuid::encode_buffer())
        .to_string()
}

fn crash_time() -> String {
    // This is unlikely to fail, but in the odd case use 0 as a crash time,
    // it will stick out like a sore thumb among the other crash reports so
    // we'll notice.
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or(Duration::from_secs(0u64))
        .as_secs()
        .to_string()
}

fn process_uptime_ts(process: RawProcessHandle) -> String {
    // Ensure we never return negative values
    process_uptime(process).max(0.0).to_string()
}

/******************************************************************************
 * Crash annotations                                                          *
 ******************************************************************************/

fn make_annotation(id: CrashAnnotation, data: &str) -> CAnnotation {
    CAnnotation {
        id: id as u32,
        data: AnnotationData::String(CString::new(data).expect("Should be a valid C string")),
    }
}

fn make_error_annotation(error: impl std::fmt::Display) -> CAnnotation {
    make_annotation(CrashAnnotation::DumperError, &format!("{}", error))
}

static STATIC_ANNOTATIONS: OnceLock<Vec<CAnnotation>> = OnceLock::new();

/// Initialize if needed the static annotations that will get included in every crash report.
///
/// Any potential error will be recorded as an annotation rather than returned.
pub(crate) fn initialize_static_annotations(app_info: &ApplicationInfo) {
    let _ = STATIC_ANNOTATIONS.get_or_init(|| {
        let mut annotations = required_annotations(app_info);
        match create_platform_specific_annotations(app_info) {
            Ok(mut platform_annotations) => annotations.append(&mut platform_annotations),
            Err(error) => annotations.push(make_error_annotation(error)),
        }
        annotations
    });
}

fn required_annotations(app_info: &ApplicationInfo) -> Vec<CAnnotation> {
    let install_time = app_info.get_install_time().to_string();

    vec![
        make_annotation(CrashAnnotation::BuildID, app_info.get_buildid()),
        make_annotation(CrashAnnotation::InstallTime, &install_time),
        make_annotation(CrashAnnotation::ProductID, app_info.get_app_id()),
        make_annotation(CrashAnnotation::ProductName, app_info.get_app_name()),
        make_annotation(
            CrashAnnotation::ReleaseChannel,
            app_info.get_release_channel(),
        ),
        make_annotation(
            CrashAnnotation::ServerURL,
            app_info.get_server_url().as_ref(),
        ),
        make_annotation(CrashAnnotation::Vendor, app_info.get_vendor()),
        make_annotation(CrashAnnotation::Version, app_info.get_version()),
    ]
}

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
    if addr_info.kind == phc::PHC_KIND_UNKNOWN {
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

/// This reads the crash annotations, writes them to the .extra file and
/// finally stores the resulting minidump in the global hash table.
///
/// # Safety
///
/// The caller must guarantee that the `generator` parameter points to a
/// Mutex<CrashGenerator> object and that `extra_data` and `minidump_path_ptr`
/// point to valid objects or are null. The ownership remains to the caller for
/// those two objects.
pub(crate) unsafe extern "C" fn finalize_breakpad_minidump(
    generator: *const c_void,
    process_id: BreakpadProcessId,
    extra_data: Option<&ExtraCrashData>,
    minidump_path_ptr: *const BreakpadChar,
) {
    let generator = generator as *const Mutex<CrashGenerator>;
    let minidump_path = PathBuf::from(<OsString as BreakpadString>::from_ptr(minidump_path_ptr));

    let mut generator = generator.as_ref().unwrap().lock().unwrap();
    let error = generator.finalize_crash_report(
        process_id.get_native(),
        extra_data,
        &minidump_path,
        ProcessType::Child,
    );
    generator.insert_crash_report(&process_id, &minidump_path, error);
}

fn retrieve_annotations(
    process: RawProcessHandle,
    process_type: ProcessType,
) -> Result<Vec<CAnnotation>> {
    if process_type == ProcessType::Parent {
        return Ok(vec![]);
    }

    Ok(mozannotation_server::retrieve_annotations(process, CrashAnnotation::Count as usize)?)
}

/// Helper function to merge a vector of annotations retrieved from a process memory into
/// a more malleable data format. This function is intended to be used through fold()ing, hence
/// the peculiar form of its arguments.
///
/// Notably, the second member of the `to_merge` argument is the error message that should be recorded
/// *as the `DumperError` annotation* should the source not be available.
fn fold_annotations(
    mut merged: HashMap<u32, AnnotationData>,
    to_merge: Result<Vec<CAnnotation>>,
) -> HashMap<u32, AnnotationData> {

    let mut merge_annotation = |annotation: CAnnotation| {
        let _ = merged.insert(annotation.id, annotation.data);
    };

    match to_merge {
        Ok(annotations) => annotations
            .into_iter()
            .filter(|annotation| !matches!(annotation.data, AnnotationData::Empty))
            .for_each(merge_annotation),
        Err(err) => merge_annotation(make_error_annotation(err)),
    }
    merged
}

fn prepare_annotation_data(id: CrashAnnotation, data: &AnnotationData) -> Option<Vec<u8>> {
    match type_of_annotation(id) {
        CrashAnnotationType::String => match data {
            AnnotationData::String(string) => Some(escape_value(string.as_bytes())),
            AnnotationData::ByteBuffer(buffer) => Some(escape_value(buffer)),
            _ => None,
        },
        CrashAnnotationType::Boolean => {
            if let AnnotationData::ByteBuffer(buff) = data {
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
            read_numeric_annotation!(u32, data)
        }
        CrashAnnotationType::U64 => {
            read_numeric_annotation!(u64, data)
        }
        CrashAnnotationType::USize => {
            read_numeric_annotation!(usize, data)
        }
        CrashAnnotationType::Object => None, // This cannot be found in memory
    }
}

fn write_extra_file(annotations: HashMap<u32, AnnotationData>, path: &Path) -> Result<()> {
    let mut annotations_written: usize = 0;
    let mut file = File::create(path)?;
    write!(&mut file, "{{")?;

    for (id, value) in annotations {
        let Some(annotation_id) = CrashAnnotation::from_u32(id) else {
            continue;
        };
        if annotation_id == CrashAnnotation::PHCBaseAddress {
            if let AnnotationData::ByteBuffer(buff) = &value {
                write_phc_annotations(&mut file, buff)?;
            }

            continue;
        }
        let Some(value) = prepare_annotation_data(annotation_id, &value) else {
            continue;
        };
        if !value.is_empty() && should_include_annotation(annotation_id, &value) {
            write!(&mut file, "\"{annotation_id:}\":\"")?;
            file.write_all(&value)?;
            write!(&mut file, "\",")?;
            annotations_written += 1;
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fold_annotations_trivial() {
        let annotations = vec![
            make_annotation(CrashAnnotation::ProductName, "Firefox"),
            make_annotation(CrashAnnotation::Vendor, "Mozilla"),
        ];

        let merged = fold_annotations(HashMap::new(), Ok(annotations));

        assert_eq!(merged.len(), 2);
        assert_eq!(
            merged.get(&(CrashAnnotation::ProductName as u32)),
            Some(&AnnotationData::String(CString::new("Firefox").unwrap()))
        );
        assert_eq!(
            merged.get(&(CrashAnnotation::Vendor as u32)),
            Some(&AnnotationData::String(CString::new("Mozilla").unwrap()))
        );
    }

    #[test]
    fn fold_annotations_error() {
        let merged = fold_annotations(HashMap::new(), None.context("MissingMainProcessAnnotations"));

        assert_eq!(merged.len(), 1);
        assert_eq!(
            merged.get(&(CrashAnnotation::DumperError as u32)),
            Some(&AnnotationData::String(
                CString::new("MissingMainProcessAnnotations").unwrap()
            ))
        );
    }
}
