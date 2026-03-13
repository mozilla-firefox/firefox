/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::decoder::JxlApiDecoder;
use std::slice;

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum JxlDecoderStatus {
    Ok = 0,
    NeedMoreData = 1,
    Error = 2,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct JxlBasicInfo {
    pub width: u32,
    pub height: u32,
    pub has_alpha: bool,
    pub alpha_premultiplied: bool,
    pub is_animated: bool,
    pub num_loops: u32,
    pub valid: bool,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, Default)]
pub struct JxlFrameInfo {
    pub duration_ms: i32,
    pub frame_duration_valid: bool,
    pub is_last: bool,
    pub is_keyframe: bool,
    pub decode_start_file_offset: usize,
    pub remaining_in_box: u64,
    pub visible_frames_to_skip: usize,
}

#[no_mangle]
pub extern "C" fn jxl_decoder_new(metadata_only: bool, premultiply: bool) -> *mut JxlApiDecoder {
    Box::into_raw(Box::new(JxlApiDecoder::new(metadata_only, premultiply)))
}

/// # Safety
/// `decoder` must be a valid pointer returned by `jxl_decoder_new` and must not
/// have been previously destroyed. After this call, `decoder` is invalid.
#[no_mangle]
pub unsafe extern "C" fn jxl_decoder_destroy(decoder: *mut JxlApiDecoder) {
    if !decoder.is_null() {
        // SAFETY: Caller guarantees `decoder` is a valid pointer from `jxl_decoder_new`
        // and has not been destroyed. We take ownership and drop it.
        let _ = unsafe { Box::from_raw(decoder) };
    }
}

/// # Safety
/// - `decoder` must be a valid pointer returned by `jxl_decoder_new`.
/// - `data` must be a valid pointer to a `*const u8` pointer.
/// - `data_len` must be a valid pointer to a `usize`.
/// - `*data` must point to a valid byte slice of length `*data_len`, or be null
///   when `*data_len` is 0.
/// - If `output_buffer` is non-null, it must point to a valid writable buffer
///   of at least `output_buffer_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn jxl_decoder_process_data(
    decoder: *mut JxlApiDecoder,
    data: *mut *const u8,
    data_len: *mut usize,
    output_buffer: *mut u8,
    output_buffer_len: usize,
) -> JxlDecoderStatus {
    debug_assert!(!decoder.is_null() && !data.is_null() && !data_len.is_null());

    // SAFETY: Caller guarantees `decoder` is a valid, non-null pointer from `jxl_decoder_new`.
    let decoder = unsafe { &mut *decoder };

    // SAFETY: Caller guarantees `data` and `data_len` are valid pointers, and that
    // `*data` points to a valid byte slice of length `*data_len` (or is null when
    // `*data_len` is 0).
    let mut data_slice = if unsafe { (*data).is_null() } {
        &[]
    } else {
        // SAFETY: See above.
        unsafe { slice::from_raw_parts(*data, *data_len) }
    };

    let output_slice = if output_buffer.is_null() {
        None
    } else {
        // SAFETY: Caller guarantees that when `output_buffer` is non-null, it points
        // to a valid writable buffer of at least `output_buffer_len` bytes.
        Some(unsafe { slice::from_raw_parts_mut(output_buffer, output_buffer_len) })
    };

    let result = decoder.process_data(&mut data_slice, output_slice);

    // SAFETY: Caller guarantees `data` and `data_len` are valid, writable pointers.
    // We update them to reflect how much data was consumed.
    unsafe {
        *data = data_slice.as_ptr();
        *data_len = data_slice.len();
    }

    match result {
        Ok(true) => JxlDecoderStatus::Ok,
        Ok(false) => JxlDecoderStatus::NeedMoreData,
        Err(_) => JxlDecoderStatus::Error,
    }
}

/// # Safety
/// `decoder` must be a valid pointer returned by `jxl_decoder_new`.
#[no_mangle]
pub unsafe extern "C" fn jxl_decoder_get_basic_info(decoder: *const JxlApiDecoder) -> JxlBasicInfo {
    debug_assert!(!decoder.is_null());

    // SAFETY: Caller guarantees `decoder` is a valid, non-null pointer from `jxl_decoder_new`.
    let decoder = unsafe { &*decoder };

    let Some(info) = decoder.get_basic_info() else {
        return JxlBasicInfo::default();
    };

    JxlBasicInfo {
        width: info.width,
        height: info.height,
        has_alpha: info.has_alpha,
        alpha_premultiplied: info.alpha_premultiplied,
        is_animated: info.is_animated,
        num_loops: info.num_loops,
        valid: true,
    }
}

/// # Safety
/// `decoder` must be a valid pointer returned by `jxl_decoder_new`.
#[no_mangle]
pub unsafe extern "C" fn jxl_decoder_get_frame_info(decoder: *const JxlApiDecoder) -> JxlFrameInfo {
    debug_assert!(!decoder.is_null());

    // SAFETY: Caller guarantees `decoder` is a valid, non-null pointer from `jxl_decoder_new`.
    let decoder = unsafe { &*decoder };

    match decoder.frame_duration {
        Some(duration) => JxlFrameInfo {
            duration_ms: duration.clamp(0.0, i32::MAX as f64) as i32,
            frame_duration_valid: true,
            ..JxlFrameInfo::default()
        },
        None => JxlFrameInfo::default(),
    }
}

/// # Safety
/// `decoder` must be a valid pointer returned by `jxl_decoder_new`.
#[no_mangle]
pub unsafe extern "C" fn jxl_decoder_is_frame_ready(decoder: *const JxlApiDecoder) -> bool {
    debug_assert!(!decoder.is_null());

    // SAFETY: Caller guarantees `decoder` is a valid, non-null pointer from `jxl_decoder_new`.
    let decoder = unsafe { &*decoder };

    decoder.frame_ready
}

/// # Safety
/// `decoder` must be a valid pointer returned by `jxl_decoder_new`.
#[no_mangle]
pub unsafe extern "C" fn jxl_decoder_has_more_frames(decoder: *const JxlApiDecoder) -> bool {
    debug_assert!(!decoder.is_null());

    // SAFETY: Caller guarantees `decoder` is a valid, non-null pointer from `jxl_decoder_new`.
    let decoder = unsafe { &*decoder };

    decoder.inner.has_more_frames()
}

#[no_mangle]
pub extern "C" fn jxl_scanner_new() -> *mut JxlApiDecoder {
    Box::into_raw(Box::new(JxlApiDecoder::new_scanner()))
}

/// # Safety
/// `decoder` must be a valid pointer returned by `jxl_scanner_new` or `jxl_decoder_new`.
#[no_mangle]
pub unsafe extern "C" fn jxl_decoder_get_scanned_frame_count(decoder: *const JxlApiDecoder) -> u32 {
    debug_assert!(!decoder.is_null());
    // SAFETY: Caller guarantees valid pointer.
    let decoder = unsafe { &*decoder };
    decoder.scanned_frames().len() as u32
}

/// # Safety
/// `decoder` must be a valid pointer returned by `jxl_scanner_new` or `jxl_decoder_new`.
/// `index` must be less than the value returned by `jxl_decoder_get_scanned_frame_count`.
#[no_mangle]
pub unsafe extern "C" fn jxl_decoder_get_scanned_frame_info(
    decoder: *const JxlApiDecoder,
    index: u32,
) -> JxlFrameInfo {
    debug_assert!(!decoder.is_null());
    // SAFETY: Caller guarantees valid pointer.
    let decoder = unsafe { &*decoder };
    let frames = decoder.scanned_frames();
    let i = index as usize;
    if i >= frames.len() {
        return JxlFrameInfo::default();
    }
    let f = &frames[i];
    JxlFrameInfo {
        duration_ms: f.duration_ms.clamp(0.0, i32::MAX as f64) as i32,
        frame_duration_valid: true,
        is_last: f.is_last,
        is_keyframe: f.is_keyframe,
        decode_start_file_offset: f.seek_target.decode_start_file_offset,
        remaining_in_box: f.seek_target.remaining_in_box,
        visible_frames_to_skip: f.seek_target.visible_frames_to_skip,
    }
}

/// # Safety
/// - `decoder` must be a valid pointer returned by `jxl_decoder_new`.
/// - If `output_buffer` is non-null, it must point to a valid writable buffer
///   of at least `output_buffer_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn jxl_decoder_flush_pixels(
    decoder: *mut JxlApiDecoder,
    output_buffer: *mut u8,
    output_buffer_len: usize,
) -> JxlDecoderStatus {
    debug_assert!(!decoder.is_null() && !output_buffer.is_null());
    // SAFETY: Caller guarantees valid pointers.
    let decoder = unsafe { &mut *decoder };
    let buf = unsafe { slice::from_raw_parts_mut(output_buffer, output_buffer_len) };
    match decoder.flush_pixels(buf) {
        Ok(()) => JxlDecoderStatus::Ok,
        Err(_) => JxlDecoderStatus::Error,
    }
}

/// # Safety
/// `decoder` must be a valid pointer returned by `jxl_decoder_new`.
#[no_mangle]
pub unsafe extern "C" fn jxl_decoder_num_completed_passes(decoder: *const JxlApiDecoder) -> u32 {
    debug_assert!(!decoder.is_null());
    // SAFETY: Caller guarantees valid pointer.
    let decoder = unsafe { &*decoder };
    decoder.num_completed_passes() as u32
}

/// # Safety
/// `decoder` must be a valid pointer returned by `jxl_decoder_new`.
#[no_mangle]
pub unsafe extern "C" fn jxl_decoder_start_new_frame(
    decoder: *mut JxlApiDecoder,
    decode_start_file_offset: usize,
    remaining_in_box: u64,
    visible_frames_to_skip: usize,
) {
    debug_assert!(!decoder.is_null());
    // SAFETY: Caller guarantees valid pointer.
    let decoder = unsafe { &mut *decoder };
    let seek_target = jxl::api::VisibleFrameSeekTarget {
        decode_start_file_offset,
        remaining_in_box,
        visible_frames_to_skip,
    };
    decoder.start_new_frame(seek_target);
}
