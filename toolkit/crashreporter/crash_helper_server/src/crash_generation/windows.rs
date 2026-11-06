/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::{BreakpadProcessId, CrashGenerator};

use anyhow::Result;
use crash_helper_common::{
    crash_annotations::CrashAnnotation, messages, ApplicationInfo, ExtraCrashData, RawProcessHandle,
};
use mozannotation_server::{AnnotationData, CAnnotation};
use std::{
    convert::TryInto,
    env,
    fs::{create_dir_all, File},
    mem::{size_of, zeroed, MaybeUninit},
    os::windows::io::AsRawHandle,
    path::PathBuf,
    ptr::{null, null_mut},
};
use uuid::Uuid;
use win32_process_mitigations::MitigationOptions;
use windows_sys::Win32::{
    Foundation::{GetLastError, FALSE, FILETIME, HANDLE},
    System::{
        Diagnostics::Debug::{
            MiniDumpWithFullMemory, MiniDumpWithFullMemoryInfo, MiniDumpWithHandleData,
            MiniDumpWithIndirectlyReferencedMemory, MiniDumpWithProcessThreadData,
            MiniDumpWithUnloadedModules, MiniDumpWriteDump, EXCEPTION_POINTERS, EXCEPTION_RECORD,
            MINIDUMP_EXCEPTION_INFORMATION, MINIDUMP_TYPE,
        },
        SystemInformation::{
            GetSystemTimeAsFileTime, VerSetConditionMask, VerifyVersionInfoW, OSVERSIONINFOEXW,
            VER_MAJORVERSION, VER_MINORVERSION, VER_SERVICEPACKMAJOR, VER_SERVICEPACKMINOR,
        },
        SystemServices::VER_GREATER_EQUAL,
        Threading::{GetProcessTimes, OpenProcess, PROCESS_ALL_ACCESS},
    },
};

impl CrashGenerator {
    pub(crate) fn generate_wer_minidump(
        &mut self,
        message: messages::WindowsErrorReportingMinidump,
    ) -> Result<(), ()> {
        let (minidump_file, path) = self.create_minidump_file()?;

        let minidump_type: MINIDUMP_TYPE = self.get_minidump_type();
        let mut context = message.context;
        let mut exception_records = message.exception_records;
        let exception_records_ptr = link_exception_records(&mut exception_records);

        let pid = message.process_id;
        let mut exception_pointers = EXCEPTION_POINTERS {
            ExceptionRecord: exception_records_ptr,
            ContextRecord: &mut context as *mut _,
        };

        let exception = MINIDUMP_EXCEPTION_INFORMATION {
            ThreadId: message.thread_id,
            ExceptionPointers: &mut exception_pointers,
            ClientPointers: FALSE,
        };

        let handle = get_process_handle(pid)?;

        // SAFETY: `handle` is guaranteed to be a valid handle and so is
        // `minidump_file`. The remaining pointers going into this function are
        // taken from objects allocated on the stack.
        let res = unsafe {
            MiniDumpWriteDump(
                handle,
                pid,
                minidump_file.as_raw_handle() as _,
                minidump_type,
                &exception,
                /* UserStreamParam */ null(),
                /* CallbackParam */ null(),
            )
        };

        if res != FALSE {
            let process_id = BreakpadProcessId { pid, handle };
            let extra_data = ExtraCrashData {
                error: None,
                annotations: vec![CAnnotation {
                    id: CrashAnnotation::WindowsErrorReporting as u32,
                    data: AnnotationData::ByteBuffer(vec![1]),
                }],
            };

            let error = self.finalize_crash_report(
                handle,
                Some(&extra_data),
                &path,
                super::ProcessType::Child,
            );
            self.insert_crash_report(&process_id, &path, error);
        }

        Ok(())
    }

    fn get_minidump_type(&self) -> MINIDUMP_TYPE {
        if env::var("MOZ_CRASHREPORTER_FULLDUMP").is_ok_and(|v| !v.is_empty()) {
            return MiniDumpWithFullMemory;
        }

        let mut minidump_type =
            MiniDumpWithFullMemoryInfo | MiniDumpWithUnloadedModules | MiniDumpWithHandleData;
        if mozbuild::config::NIGHTLY_BUILD {
            // This is Nightly only because this doubles the size of minidumps based
            // on the experimental data.
            minidump_type |= MiniDumpWithProcessThreadData;

            // dbghelp.dll on Win7 can't handle overlapping memory regions so we only
            // enable this feature on Win8 or later.
            if is_windows8_or_later() {
                // This allows us to examine heap objects referenced from stack objects
                // at the cost of further doubling the size of minidumps.
                minidump_type |= MiniDumpWithIndirectlyReferencedMemory;
            }
        }
        minidump_type
    }

    fn create_minidump_file(&self) -> Result<(File, PathBuf), ()> {
        // Make sure that the target directory is present
        create_dir_all(&self.minidump_path).map_err(|_| ())?;

        let uuid = Uuid::new_v4()
            .as_hyphenated()
            .encode_lower(&mut Uuid::encode_buffer())
            .to_string();
        let path = self.minidump_path.clone().join(uuid + ".dmp");
        let file = File::create(&path).map_err(|_| ())?;
        Ok((file, path))
    }
}

fn link_exception_records(exception_records: &mut Vec<EXCEPTION_RECORD>) -> *mut EXCEPTION_RECORD {
    let mut iter = exception_records.iter_mut().peekable();
    while let Some(exception_record) = iter.next() {
        exception_record.ExceptionRecord = null_mut();

        if let Some(next) = iter.peek_mut() {
            exception_record.ExceptionRecord = *next as *mut _;
        }
    }

    if exception_records.is_empty() {
        null_mut()
    } else {
        exception_records.as_mut_ptr()
    }
}

fn is_windows8_or_later() -> bool {
    let mut info = OSVERSIONINFOEXW {
        dwOSVersionInfoSize: size_of::<OSVERSIONINFOEXW>().try_into().unwrap(),
        dwMajorVersion: 6,
        dwMinorVersion: 2,
        ..unsafe { zeroed() }
    };

    unsafe {
        let mut mask: u64 = 0;
        let ge: u8 = VER_GREATER_EQUAL.try_into().unwrap();
        mask = VerSetConditionMask(mask, VER_MAJORVERSION, ge);
        mask = VerSetConditionMask(mask, VER_MINORVERSION, ge);
        mask = VerSetConditionMask(mask, VER_SERVICEPACKMAJOR, ge);
        mask = VerSetConditionMask(mask, VER_SERVICEPACKMINOR, ge);

        VerifyVersionInfoW(
            &mut info,
            VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR | VER_SERVICEPACKMINOR,
            mask,
        ) != 0
    }
}

fn get_process_handle(pid: u32) -> Result<HANDLE, ()> {
    // SAFETY: No pointers involved, worst case we get an error
    let handle = unsafe {
        OpenProcess(PROCESS_ALL_ACCESS, /* bInheritHandle */ FALSE, pid)
    };

    if handle.is_null() {
        Err(())
    } else {
        Ok(handle)
    }
}

/// Create the annotations that are specific to Windows: the process mitigation
/// options the system is configured to apply to our executable.
pub(crate) fn create_platform_specific_annotations(
    app_info: &ApplicationInfo,
) -> Result<Vec<CAnnotation>> {
    let app_mitigations = app_info
        .get_application_path()
        .map(win32_process_mitigations::get_app_mitigation_options)
        .transpose()?
        .flatten();
    let sys_mitigations = win32_process_mitigations::get_system_mitigation_options()?;
    if let Some(mitigations) = MitigationOptions::amalgamate(sys_mitigations, app_mitigations) {
        Ok(vec![
            super::make_annotation_str(
                CrashAnnotation::WindowsProcessMitigationsBytes,
                &format!("{}", mitigations),
            ),
            super::make_annotation_str(
                CrashAnnotation::WindowsProcessMitigations,
                &mitigations.describe(),
            ),
        ])
    } else {
        Ok(vec![])
    }
}

fn filetime_to_u64(ft: &FILETIME) -> u64 {
    ((ft.dwHighDateTime as u64) << 32) + ft.dwLowDateTime as u64
}

pub(crate) fn process_uptime(handle: RawProcessHandle) -> f64 {
    let mut creation_time = MaybeUninit::<FILETIME>::uninit();
    let mut exit_time = MaybeUninit::<FILETIME>::uninit();
    let mut kernel_time = MaybeUninit::<FILETIME>::uninit();
    let mut user_time = MaybeUninit::<FILETIME>::uninit();

    // SAFETY: All the pointers we pass to `GetProcessTimes()` point to
    // stack-allocated structures and thus are valid. We assume the process
    // creation time structure to be initialized only after having verified
    // that the call was successful.
    let creation_time = unsafe {
        if GetProcessTimes(
            handle,
            creation_time.as_mut_ptr(),
            exit_time.as_mut_ptr(),
            kernel_time.as_mut_ptr(),
            user_time.as_mut_ptr(),
        ) != 0
        {
            creation_time.assume_init()
        } else {
            log::error!(
                "Could not get process time stats, error: {:x}",
                GetLastError()
            );
            return 0.0f64;
        }
    };

    let mut current_time = MaybeUninit::<FILETIME>::uninit();

    // SAFETY: The pointer we pass to `GetSystemTimeAsFileTime()` point to
    // stack-allocated memory and thus are guaranteed to be valid. The call
    // always populates the passed in structure so we can assume it has been
    // properly initialized after the call.
    let current_time = unsafe {
        GetSystemTimeAsFileTime(current_time.as_mut_ptr());
        current_time.assume_init()
    };

    let uptime =
        filetime_to_u64(&current_time).saturating_sub(filetime_to_u64(&creation_time)) as f64;
    uptime / 10_000_000.0 // 100-ns to seconds
}
