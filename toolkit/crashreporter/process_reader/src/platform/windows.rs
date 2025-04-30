/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use std::{
    convert::TryInto,
    ffi::OsString,
    mem::{size_of, MaybeUninit},
    os::windows::ffi::OsStringExt,
    ptr::null_mut,
};

use windows_sys::Win32::{
    Foundation::{FALSE, HMODULE, MAX_PATH},
    System::{
        Diagnostics::Debug::ReadProcessMemory,
        ProcessStatus::{
            K32EnumProcessModules, K32GetModuleBaseNameW, K32GetModuleInformation, MODULEINFO,
        },
    },
};

use crate::{
    error::{ProcessReaderError, ReadError},
    ProcessHandle, ProcessReader,
};

impl ProcessReader {
    pub fn new(process: ProcessHandle) -> Result<ProcessReader, ProcessReaderError> {
        Ok(ProcessReader { process })
    }

    pub fn find_module(&self, module_name: &str) -> Result<usize, ProcessReaderError> {
        let modules = self.get_module_list()?;

        let module = modules.iter().find_map(|&module| {
            let name = self.get_module_name(module);
            // Crude way of mimicking Windows lower-case comparisons but
            // sufficient for our use-cases.
            if name.is_some_and(|name| name.eq_ignore_ascii_case(module_name)) {
                self.get_module_info(module)
                    .map(|module| module.lpBaseOfDll as usize)
            } else {
                None
            }
        });

        module.ok_or(ProcessReaderError::ModuleNotFound)
    }

    pub fn find_section(
        &self,
        module_address: usize,
        section_name: &[u8; 8],
    ) -> Result<usize, ProcessReaderError> {
        // We read only the first page from the module, this should be more than
        // enough to read the header and section list. In the future we might do
        // this incrementally but for now goblin requires an array to parse
        // so we can't do it just yet.
        const PAGE_SIZE: usize = 4096;
        let bytes = self.copy_array(module_address as _, PAGE_SIZE)?;
        let header = goblin::pe::header::Header::parse(&bytes)?;

        // Skip the PE header so we can parse the sections
        let optional_header_offset = header.dos_header.pe_pointer as usize
            + goblin::pe::header::SIZEOF_PE_MAGIC
            + goblin::pe::header::SIZEOF_COFF_HEADER;
        let offset =
            &mut (optional_header_offset + header.coff_header.size_of_optional_header as usize);

        let sections = header.coff_header.sections(&bytes, offset)?;

        for section in sections {
            if section.name.eq(section_name) {
                let address = module_address.checked_add(section.virtual_address as usize);
                return address.ok_or(ProcessReaderError::InvalidAddress);
            }
        }

        Err(ProcessReaderError::SectionNotFound)
    }

    fn get_module_list(&self) -> Result<Vec<HMODULE>, ProcessReaderError> {
        let mut module_num: usize = 100;
        let mut required_buffer_size: u32 = 0;
        let mut module_array = Vec::<HMODULE>::with_capacity(module_num);

        loop {
            let buffer_size: u32 = (module_num * size_of::<HMODULE>()).try_into()?;
            let res = unsafe {
                K32EnumProcessModules(
                    self.process,
                    module_array.as_mut_ptr() as *mut _,
                    buffer_size,
                    &mut required_buffer_size as *mut _,
                )
            };

            module_num = required_buffer_size as usize / size_of::<HMODULE>();

            if res == 0 {
                if required_buffer_size > buffer_size {
                    module_array = Vec::<HMODULE>::with_capacity(module_num);
                } else {
                    return Err(ProcessReaderError::EnumProcessModulesError);
                }
            } else {
                break;
            }
        }

        // SAFETY: module_array has been filled by K32EnumProcessModules()
        unsafe {
            module_array.set_len(module_num);
        };

        Ok(module_array)
    }

    fn get_module_name(&self, module: HMODULE) -> Option<String> {
        let mut path: [u16; MAX_PATH as usize] = [0; MAX_PATH as usize];
        let res =
            unsafe { K32GetModuleBaseNameW(self.process, module, path.as_mut_ptr(), MAX_PATH) };

        if res == 0 {
            None
        } else {
            let name = OsString::from_wide(&path[0..res as usize]);
            let name = name.to_str()?;
            Some(name.to_string())
        }
    }

    fn get_module_info(&self, module: HMODULE) -> Option<MODULEINFO> {
        let mut info: MaybeUninit<MODULEINFO> = MaybeUninit::uninit();
        let res = unsafe {
            K32GetModuleInformation(
                self.process,
                module,
                info.as_mut_ptr(),
                size_of::<MODULEINFO>() as u32,
            )
        };

        if res == 0 {
            None
        } else {
            let info = unsafe { info.assume_init() };
            Some(info)
        }
    }

    pub fn copy_object_shallow<T>(&self, src: usize) -> Result<MaybeUninit<T>, ReadError> {
        let mut object = MaybeUninit::<T>::uninit();
        let res = unsafe {
            ReadProcessMemory(
                self.process,
                src as _,
                object.as_mut_ptr() as _,
                size_of::<T>(),
                null_mut(),
            )
        };

        if res != FALSE {
            Ok(object)
        } else {
            Err(ReadError::ReadProcessMemoryError)
        }
    }

    pub fn copy_object<T>(&self, src: usize) -> Result<T, ReadError> {
        let object = self.copy_object_shallow(src)?;
        Ok(unsafe { object.assume_init() })
    }

    pub fn copy_array<T>(&self, src: usize, num: usize) -> Result<Vec<T>, ReadError> {
        let num_of_bytes = num * size_of::<T>();
        let mut array: Vec<T> = Vec::with_capacity(num);
        let res = unsafe {
            ReadProcessMemory(
                self.process,
                src as _,
                array.as_mut_ptr() as _,
                num_of_bytes,
                null_mut(),
            )
        };

        if res != FALSE {
            unsafe {
                array.set_len(num);
            }

            Ok(array)
        } else {
            Err(ReadError::ReadProcessMemoryError)
        }
    }
}
