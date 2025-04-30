/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

pub mod errors;

use crate::errors::*;
#[cfg(any(target_os = "linux", target_os = "android"))]
use memoffset::offset_of;
use process_reader::error::ProcessReaderError;
use process_reader::ProcessReader;

#[cfg(any(target_os = "windows", target_os = "macos"))]
use mozannotation_client::ANNOTATION_SECTION;
use mozannotation_client::{Annotation, AnnotationContents, AnnotationMutex};
#[cfg(any(target_os = "linux", target_os = "android"))]
use mozannotation_client::{MozAnnotationNote, ANNOTATION_NOTE_NAME, ANNOTATION_TYPE};
use std::cmp::min;
use std::ffi::CString;
use std::mem::{size_of, ManuallyDrop};

pub enum AnnotationData {
    Empty,
    ByteBuffer(Vec<u8>),
    String(CString),
}

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
    let reader = ProcessReader::new(process)?;
    let address = find_annotations(&reader)?;

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
    let length = annotation_table.len();
    let mut annotations = Vec::<CAnnotation>::with_capacity(min(max_annotations, length));

    for i in 0..length {
        let annotation_address = unsafe { vec_pointer.add(i) };
        if let Ok(annotation) = read_annotation(&reader, annotation_address as usize) {
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
) -> Result<CAnnotation, process_reader::error::ReadError> {
    let raw_annotation = ManuallyDrop::new(reader.copy_object::<Annotation>(address)?);
    let mut annotation = CAnnotation {
        id: raw_annotation.id,
        data: AnnotationData::Empty,
    };

    if raw_annotation.address == 0 {
        return Ok(annotation);
    }

    match raw_annotation.contents {
        AnnotationContents::Empty => {}
        AnnotationContents::NSCStringPointer => {
            let string = copy_nscstring(reader, raw_annotation.address)?;
            annotation.data = AnnotationData::String(string);
        }
        AnnotationContents::CStringPointer => {
            let string = copy_null_terminated_string_pointer(reader, raw_annotation.address)?;
            annotation.data = AnnotationData::String(string);
        }
        AnnotationContents::CString => {
            annotation.data =
                AnnotationData::String(reader.copy_null_terminated_string(raw_annotation.address)?);
        }
        AnnotationContents::ByteBuffer(size) | AnnotationContents::OwnedByteBuffer(size) => {
            let buffer = copy_bytebuffer(reader, raw_annotation.address, size)?;
            annotation.data = AnnotationData::ByteBuffer(buffer);
        }
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
    size: u32,
) -> Result<Vec<u8>, process_reader::error::ReadError> {
    reader.copy_array::<u8>(address, size as _)
}
