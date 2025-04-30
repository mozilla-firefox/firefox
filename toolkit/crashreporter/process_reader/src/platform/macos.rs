/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use goblin::mach::{
    header::{Header64, MH_DYLIB, MH_EXECUTE, MH_MAGIC_64},
    load_command::{LoadCommandHeader, Section64, SegmentCommand64, LC_SEGMENT_64},
};
use mach2::{
    kern_return::KERN_SUCCESS,
    task::task_info,
    task_info::{task_dyld_info, TASK_DYLD_ALL_IMAGE_INFO_64, TASK_DYLD_INFO},
    vm::mach_vm_read_overwrite,
};
use std::mem::{size_of, MaybeUninit};

use crate::{
    error::{ProcessReaderError, ReadError},
    ProcessHandle, ProcessReader,
};

#[repr(C)]
#[derive(Copy, Clone, Debug)]
struct AllImagesInfo {
    // VERSION 1
    pub version: u32,
    /// The number of [`ImageInfo`] structs at that following address
    info_array_count: u32,
    /// The address in the process where the array of [`ImageInfo`] structs is
    info_array_addr: u64,
    /// A function pointer, unused
    _notification: u64,
    /// Unused
    _process_detached_from_shared_region: bool,
    // VERSION 2
    lib_system_initialized: bool,
    // Note that crashpad adds a 32-bit int here to get proper alignment when
    // building on 32-bit targets...but we explicitly don't care about 32-bit
    // targets since Apple doesn't
    pub dyld_image_load_address: u64,
}

/// `dyld_image_info` from <usr/include/mach-o/dyld_images.h>
#[repr(C)]
#[derive(Debug, Clone, Copy)]
struct ImageInfo {
    /// The address in the process where the image is loaded
    pub load_address: u64,
    /// The address in the process where the image's file path can be read
    pub file_path: u64,
    /// Timestamp for when the image's file was last modified
    pub file_mod_date: u64,
}

const DATA_SEGMENT: &[u8; 16] = b"__DATA\0\0\0\0\0\0\0\0\0\0";

impl ProcessReader {
    pub fn new(process: ProcessHandle) -> Result<ProcessReader, ProcessReaderError> {
        Ok(ProcessReader { process })
    }

    pub fn find_module(&self, module_name: &str) -> Result<usize, ProcessReaderError> {
        let dyld_info = self.task_info()?;
        if (dyld_info.all_image_info_format as u32) != TASK_DYLD_ALL_IMAGE_INFO_64 {
            return Err(ProcessReaderError::ImageFormatError);
        }

        let all_image_info_size = dyld_info.all_image_info_size;
        let all_image_info_addr = dyld_info.all_image_info_addr;
        if (all_image_info_size as usize) < size_of::<AllImagesInfo>() {
            return Err(ProcessReaderError::ImageFormatError);
        }

        let all_images_info = self.copy_object::<AllImagesInfo>(all_image_info_addr as _)?;

        // Load the images
        let images = self.copy_array::<ImageInfo>(
            all_images_info.info_array_addr as _,
            all_images_info.info_array_count as _,
        )?;

        images
            .iter()
            .find(|&image| {
                let image_path = self.copy_null_terminated_string(image.file_path as usize);

                if let Ok(image_path) = image_path {
                    if let Some(image_name) = image_path.into_bytes().rsplit(|&b| b == b'/').next()
                    {
                        image_name.eq(module_name.as_bytes())
                    } else {
                        false
                    }
                } else {
                    false
                }
            })
            .map(|image| image.load_address as usize)
            .ok_or(ProcessReaderError::ModuleNotFound)
    }

    pub fn find_section(
        &self,
        module_address: usize,
        section_name: &[u8; 16],
    ) -> Result<usize, ProcessReaderError> {
        let header = self.copy_object::<Header64>(module_address)?;
        let mut address = module_address + size_of::<Header64>();

        if header.magic == MH_MAGIC_64
            && (header.filetype == MH_EXECUTE || header.filetype == MH_DYLIB)
        {
            let end_of_commands = address + (header.sizeofcmds as usize);

            while address < end_of_commands {
                let command = self.copy_object::<LoadCommandHeader>(address)?;

                if command.cmd == LC_SEGMENT_64 {
                    if let Ok(offset) = self.find_section_in_segment(address, section_name) {
                        return module_address
                            .checked_add(offset)
                            .ok_or(ProcessReaderError::InvalidAddress);
                    }
                }

                address += command.cmdsize as usize;
            }
        }

        Err(ProcessReaderError::SectionNotFound)
    }

    fn find_section_in_segment(
        &self,
        segment_address: usize,
        section_name: &[u8; 16],
    ) -> Result<usize, ProcessReaderError> {
        let segment = self.copy_object::<SegmentCommand64>(segment_address)?;

        if segment.segname.eq(DATA_SEGMENT) {
            let sections_addr = segment_address + size_of::<SegmentCommand64>();
            let sections = self.copy_array::<Section64>(sections_addr, segment.nsects as usize)?;
            for section in &sections {
                if section.sectname.eq(section_name) {
                    return Ok(section.offset as usize);
                }
            }
        }

        Err(ProcessReaderError::SectionNotFound)
    }

    fn task_info(&self) -> Result<task_dyld_info, ProcessReaderError> {
        let mut info = std::mem::MaybeUninit::<task_dyld_info>::uninit();
        let mut count = (std::mem::size_of::<task_dyld_info>() / std::mem::size_of::<u32>()) as u32;

        let res = unsafe {
            task_info(
                self.process,
                TASK_DYLD_INFO,
                info.as_mut_ptr().cast(),
                &mut count,
            )
        };

        if res == KERN_SUCCESS {
            // SAFETY: this will be initialized if the call succeeded
            unsafe { Ok(info.assume_init()) }
        } else {
            Err(ProcessReaderError::TaskInfoError)
        }
    }

    pub fn copy_object_shallow<T>(&self, src: usize) -> Result<MaybeUninit<T>, ReadError> {
        let mut object = MaybeUninit::<T>::uninit();
        let mut size: u64 = 0;
        let res = unsafe {
            mach_vm_read_overwrite(
                self.process,
                src as u64,
                size_of::<T>() as u64,
                object.as_mut_ptr() as _,
                &mut size as _,
            )
        };

        if res == KERN_SUCCESS {
            Ok(object)
        } else {
            Err(ReadError::MachError)
        }
    }

    pub fn copy_object<T>(&self, src: usize) -> Result<T, ReadError> {
        let object = self.copy_object_shallow(src)?;
        Ok(unsafe { object.assume_init() })
    }

    pub fn copy_array<T>(&self, src: usize, num: usize) -> Result<Vec<T>, ReadError> {
        let mut array: Vec<MaybeUninit<T>> = Vec::with_capacity(num);
        let mut size: u64 = 0;
        let res = unsafe {
            mach_vm_read_overwrite(
                self.process,
                src as u64,
                (num * size_of::<T>()) as u64,
                array.as_mut_ptr() as _,
                &mut size as _,
            )
        };

        if res == KERN_SUCCESS {
            unsafe {
                array.set_len(num);
                Ok(std::mem::transmute::<Vec<MaybeUninit<T>>, Vec<T>>(array))
            }
        } else {
            Err(ReadError::MachError)
        }
    }
}

impl Drop for ProcessReader {
    fn drop(&mut self) {}
}
