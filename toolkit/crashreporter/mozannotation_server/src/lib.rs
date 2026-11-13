/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

pub mod errors;

use crate::errors::*;
#[cfg(any(target_os = "linux", target_os = "android"))]
use std::mem::offset_of;
use process_reader::error::ProcessReaderError;
use process_reader::ProcessReader;

#[cfg(any(target_os = "windows", target_os = "macos"))]
use mozannotation_client::ANNOTATION_SECTION;
use mozannotation_client::{
    Annotation, AnnotationMutex, ANNOTATION_CONTENTS_BYTEBUFFER, ANNOTATION_CONTENTS_CSTRING,
    ANNOTATION_CONTENTS_CSTRINGPOINTER, ANNOTATION_CONTENTS_EMPTY,
    ANNOTATION_CONTENTS_NSCSTRINGPOINTER, ANNOTATION_CONTENTS_OWNEDBYTEBUFFER,
};
#[cfg(any(target_os = "linux", target_os = "android"))]
use mozannotation_client::{MozAnnotationNote, ANNOTATION_NOTE_NAME, ANNOTATION_TYPE};
use std::cmp::min;
use std::ffi::CString;
use std::mem::{size_of, ManuallyDrop};

// No annotation comes close to this size. The lengths are read from the crashed
// process, which may have been updating its annotations at the time, so a
// larger value means the entry is garbage.
const MAX_ANNOTATION_SIZE: usize = 1024 * 1024;

#[derive(Clone, Debug, PartialEq)]
pub enum AnnotationData {
    Empty,
    ByteBuffer(Vec<u8>),
    String(CString),
}

#[derive(Clone, Debug, PartialEq)]
pub struct CAnnotation {
    #[allow(dead_code)] // This is implicitly stored to and used externally
    pub id: u32,
    pub data: AnnotationData,
}

pub type ProcessHandle = process_reader::ProcessHandle;

pub fn retrieve_annotations(
    process: ProcessHandle,
    max_annotations: usize,
) -> Result<Vec<CAnnotation>, AnnotationsRetrievalError> {
    read_annotations(&ProcessReader::new(process)?, max_annotations)
}

/// Retrieve the annotations of a process through a remote executor reachable
/// via `socket`. For best performance, `buffer_len` should match the executor's output buffer.
#[cfg(any(target_os = "linux", target_os = "android"))]
pub fn retrieve_annotations_remote(
    socket: std::os::fd::OwnedFd,
    buffer_len: usize,
    max_annotations: usize,
) -> Result<Vec<CAnnotation>, AnnotationsRetrievalError> {
    read_annotations(
        &ProcessReader::new_remote(socket, buffer_len),
        max_annotations,
    )
}

fn read_annotations(
    reader: &ProcessReader,
    max_annotations: usize,
) -> Result<Vec<CAnnotation>, AnnotationsRetrievalError> {
    let address = find_annotations(reader)?;

    let mut mutex = reader
        .copy_object_shallow::<AnnotationMutex>(address)
        .map_err(ProcessReaderError::from)?;
    let mutex = unsafe { mutex.assume_init_mut() };

    // TODO: we should clear the poison value here before getting the mutex
    // contents. Right now we have to fail if the mutex was poisoned.
    let annotation_table = mutex
        .get_mut()
        .map_err(|_e| AnnotationsRetrievalError::InvalidData)?;

    if !annotation_table.verify() {
        return Err(AnnotationsRetrievalError::InvalidAnnotationTable);
    }

    let vec_pointer = annotation_table.get_ptr();
    let length = min(annotation_table.len(), max_annotations);
    let mut annotations = Vec::<CAnnotation>::with_capacity(length);

    for i in 0..length {
        let annotation_address = unsafe { vec_pointer.add(i) };
        if let Ok(annotation) = read_annotation(reader, annotation_address as usize) {
            annotations.push(annotation);
        }
    }

    Ok(annotations)
}

fn find_annotations(reader: &ProcessReader) -> Result<usize, AnnotationsRetrievalError> {
    #[cfg(any(target_os = "linux", target_os = "android"))]
    {
        let libxul_address = reader.find_module("libxul.so")?;
        let note_address = reader.find_program_note(
            libxul_address,
            ANNOTATION_TYPE,
            size_of::<MozAnnotationNote>(),
            ANNOTATION_NOTE_NAME,
        )?;

        let note = reader
            .copy_object::<MozAnnotationNote>(note_address)
            .map_err(ProcessReaderError::from)?;
        let desc = note.desc;
        let ehdr = (-note.ehdr) as usize;
        let offset = desc + ehdr
            - (offset_of!(MozAnnotationNote, ehdr) - offset_of!(MozAnnotationNote, desc));

        usize::checked_add(libxul_address, offset).ok_or(AnnotationsRetrievalError::InvalidAddress)
    }
    #[cfg(target_os = "macos")]
    {
        let libxul_address = reader.find_module("XUL")?;
        reader
            .find_section(libxul_address, ANNOTATION_SECTION)
            .map_err(AnnotationsRetrievalError::from)
    }
    #[cfg(target_os = "windows")]
    {
        let libxul_address = reader.find_module("xul.dll")?;
        reader
            .find_section(libxul_address, ANNOTATION_SECTION)
            .map_err(AnnotationsRetrievalError::from)
    }
}

// Read an annotation from the given address
fn read_annotation(
    reader: &ProcessReader,
    address: usize,
) -> Result<CAnnotation, AnnotationsRetrievalError> {
    let raw_annotation = ManuallyDrop::new(reader.copy_object::<Annotation>(address)?);
    let mut annotation = CAnnotation {
        id: raw_annotation.id,
        data: AnnotationData::Empty,
    };

    if raw_annotation.address == 0 {
        return Ok(annotation);
    }

    match raw_annotation.contents {
        ANNOTATION_CONTENTS_EMPTY => {}
        ANNOTATION_CONTENTS_NSCSTRINGPOINTER => {
            let string = copy_nscstring(reader, raw_annotation.address)?;
            if !string.is_empty() {
                annotation.data = AnnotationData::String(string);
            }
        }
        ANNOTATION_CONTENTS_CSTRINGPOINTER => {
            let string = copy_null_terminated_string_pointer(reader, raw_annotation.address)?;
            if !string.is_empty() {
                annotation.data = AnnotationData::String(string);
            }
        }
        ANNOTATION_CONTENTS_CSTRING => {
            let string = reader.copy_null_terminated_string(raw_annotation.address)?;
            if !string.is_empty() {
                annotation.data = AnnotationData::String(string);
            }
        }
        ANNOTATION_CONTENTS_BYTEBUFFER | ANNOTATION_CONTENTS_OWNEDBYTEBUFFER => {
            if raw_annotation.len > MAX_ANNOTATION_SIZE {
                return Err(AnnotationsRetrievalError::InvalidData);
            }

            if raw_annotation.len > 0 {
                let buffer = copy_bytebuffer(reader, raw_annotation.address, raw_annotation.len)?;
                annotation.data = AnnotationData::ByteBuffer(buffer);
            }
        }
        _ => return Err(AnnotationsRetrievalError::InvalidData),
    };

    Ok(annotation)
}

fn copy_null_terminated_string_pointer(
    reader: &ProcessReader,
    address: usize,
) -> Result<CString, process_reader::error::ReadError> {
    let buffer_address = reader.copy_object::<usize>(address)?;
    reader.copy_null_terminated_string(buffer_address)
}

fn copy_nscstring(
    reader: &ProcessReader,
    address: usize,
) -> Result<CString, process_reader::error::ReadError> {
    // HACK: This assumes the layout of the nsCString object
    let length_address = address + size_of::<usize>();
    let length = reader.copy_object::<u32>(length_address)?;

    if length as usize > MAX_ANNOTATION_SIZE {
        return Err(process_reader::error::ReadError::TooLarge);
    }

    if length > 0 {
        let data_address = reader.copy_object::<usize>(address)?;
        let mut vec = reader.copy_array::<u8>(data_address, length as _)?;

        // We can't assume the string is a valid C string, as it might have
        // been corrupted during the crash. Ensure that it contains no nul
        // characters.
        let nul_byte_pos = vec.iter().position(|&c| c == 0);
        if let Some(nul_byte_pos) = nul_byte_pos {
            vec.truncate(nul_byte_pos);
        }

        // SAFETY: This is safe because we verified that there are no nul
        // characters inside the string.
        Ok(unsafe { CString::from_vec_unchecked(vec) })
    } else {
        Ok(CString::default())
    }
}

fn copy_bytebuffer(
    reader: &ProcessReader,
    address: usize,
    size: usize,
) -> Result<Vec<u8>, process_reader::error::ReadError> {
    reader.copy_array::<u8>(address, size)
}
