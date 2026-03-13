// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

use crate::cms::QcmsCms;
use jxl::api::{
    JxlBitstreamInput, JxlColorEncoding, JxlColorProfile, JxlColorType, JxlDataFormat,
    JxlDecoderInner, JxlDecoderOptions, JxlOutputBuffer, JxlPixelFormat, ProcessingResult,
    VisibleFrameInfo, VisibleFrameSeekTarget,
};
use jxl::headers::extra_channels::ExtraChannel;

pub struct JxlApiDecoder {
    pub inner: JxlDecoderInner,
    metadata_only: bool,
    pixel_format_set: bool,
    pub frame_ready: bool,
    pub frame_duration: Option<f64>,
}

pub struct BasicInfo {
    pub width: u32,
    pub height: u32,
    pub has_alpha: bool,
    pub alpha_premultiplied: bool,
    pub is_animated: bool,
    pub num_loops: u32,
}

#[derive(Debug)]
pub enum Error {
    JXL(jxl::error::Error),
    Overflow,
}

impl From<jxl::error::Error> for Error {
    fn from(err: jxl::error::Error) -> Error {
        Error::JXL(err)
    }
}

impl JxlApiDecoder {
    pub fn new(metadata_only: bool, premultiply: bool) -> Self {
        let mut options = JxlDecoderOptions::default();
        options.premultiply_output = premultiply;
        options.cms = Some(Box::new(QcmsCms) as Box<dyn jxl::api::JxlCms>);

        let mut inner = JxlDecoderInner::new(options);

        // Request gamma-encoded sRGB output for proper display
        let srgb_output =
            JxlColorProfile::Simple(JxlColorEncoding::srgb(/* grayscale */ false));
        inner
            .set_output_color_profile(srgb_output)
            .expect("Simple sRGB profile should always be valid");

        Self {
            inner,
            metadata_only,
            pixel_format_set: false,
            frame_ready: false,
            frame_duration: None,
        }
    }

    pub fn new_scanner() -> Self {
        let mut options = JxlDecoderOptions::default();
        options.scan_frames_only = true;
        options.cms = None;

        let inner = JxlDecoderInner::new(options);

        Self {
            inner,
            metadata_only: false,
            pixel_format_set: false,
            frame_ready: false,
            frame_duration: None,
        }
    }

    pub fn scanned_frames(&self) -> &[VisibleFrameInfo] {
        self.inner.scanned_frames()
    }

    pub fn flush_pixels(&mut self, output_buffer: &mut [u8]) -> Result<(), Error> {
        let (width, height) = self
            .inner
            .basic_info()
            .map(|bi| (bi.size.0, bi.size.1))
            .unwrap_or((0, 0));
        let bytes_per_row = width.checked_mul(4).ok_or(Error::Overflow)?;
        let mut output_buf = JxlOutputBuffer::new(output_buffer, height, bytes_per_row);
        self.inner
            .flush_pixels(std::slice::from_mut(&mut output_buf))
            .map_err(Error::from)
    }

    pub fn num_completed_passes(&self) -> usize {
        self.inner.num_completed_passes().unwrap_or(0)
    }

    pub fn start_new_frame(&mut self, seek_target: VisibleFrameSeekTarget) {
        self.inner.start_new_frame(seek_target);
        self.frame_ready = false;
        self.frame_duration = None;
    }

    pub fn get_basic_info(&self) -> Option<BasicInfo> {
        let basic_info = self.inner.basic_info()?;

        let alpha_channel = basic_info
            .extra_channels
            .iter()
            .find(|ec| ec.ec_type == ExtraChannel::Alpha);

        let (is_animated, num_loops) = basic_info
            .animation
            .as_ref()
            .map(|anim| (true, anim.num_loops))
            .unwrap_or((false, 0));

        Some(BasicInfo {
            width: basic_info.size.0 as u32,
            height: basic_info.size.1 as u32,
            has_alpha: alpha_channel.is_some(),
            alpha_premultiplied: alpha_channel.is_some_and(|ec| ec.alpha_associated),
            is_animated,
            num_loops,
        })
    }

    /// Process JXL data. Pass output_buffer once frame_ready is true.
    /// Returns Ok(true) when frame_ready changes state.
    pub fn process_data(
        &mut self,
        data: &mut impl JxlBitstreamInput,
        output_buffer: Option<&mut [u8]>,
    ) -> Result<bool, Error> {
        let has_output_buffer = output_buffer.is_some();

        // Create output buffer wrapper if provided.
        // When output_buffer is provided, dimensions must already be known.
        let (width, height) = self
            .inner
            .basic_info()
            .map(|bi| (bi.size.0, bi.size.1))
            .unwrap_or((0, 0));
        let bytes_per_row = width.checked_mul(4).ok_or(Error::Overflow)?;
        let mut output_buf: Option<JxlOutputBuffer<'_>> =
            output_buffer.map(|buf| JxlOutputBuffer::new(buf, height, bytes_per_row));

        loop {
            let result = self
                .inner
                .process(data, output_buf.as_mut().map(std::slice::from_mut));

            match result {
                Err(e) => {
                    return Err(e.into());
                }
                Ok(r) => match r {
                    ProcessingResult::Complete { .. } => {
                        // For metadata-only decode of non-animated images, return once
                        // we have basic_info. For animated images, continue until frame
                        // header is available to get the first frame's duration.
                        if self.metadata_only {
                            if let Some(basic_info) = self.inner.basic_info() {
                                if basic_info.animation.is_none() {
                                    return Ok(true);
                                }
                            }
                        }

                        if !self.pixel_format_set {
                            if let Some(basic_info) = self.inner.basic_info() {
                                let pixel_format = JxlPixelFormat {
                                    color_type: JxlColorType::Rgba,
                                    color_data_format: Some(JxlDataFormat::U8 { bit_depth: 8 }),
                                    extra_channel_format: vec![
                                        None;
                                        basic_info.extra_channels.len()
                                    ],
                                };
                                self.inner.set_pixel_format(pixel_format);
                                self.pixel_format_set = true;
                                // Continue processing - don't return, let jxl-rs continue
                                continue;
                            }
                        }

                        // Check if we have a frame header ready
                        let frame_header = self.inner.frame_header();
                        if let Some(frame_header) = frame_header {
                            self.frame_duration = frame_header.duration.or(Some(0.0));
                            self.frame_ready = true;
                            // process() with a buffer should have consumed the frame header
                            assert!(
                                !has_output_buffer,
                                "frame_header present with output buffer"
                            );
                            return Ok(true);
                        } else if self.frame_ready {
                            // Frame was rendered
                            self.frame_ready = false;
                            return Ok(true);
                        }
                        // No frame yet, need more data
                        return Ok(false);
                    }
                    ProcessingResult::NeedsMoreInput { .. } => {
                        return Ok(false);
                    }
                },
            }
        }
    }
}
