// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

use jxl::api::{
    Endianness, JxlBitstreamInput, JxlColorEncoding, JxlColorProfile, JxlColorType, JxlDataFormat,
    JxlDecoderInner, JxlDecoderOptions, JxlOutputBuffer, JxlPixelFormat, JxlPrimaries,
    JxlTransferFunction, JxlWhitePoint, ProcessingResult, VisibleFrameInfo,
};
use jxl::headers::extra_channels::ExtraChannel;

pub struct JxlApiDecoder {
    pub inner: JxlDecoderInner,
    metadata_only: bool,
    pixel_format_set: bool,
    has_cms: bool,
    pub use_f16: bool,
    pub frame_ready: bool,
    pub frame_duration: Option<f64>,
    icc_profile_cache: Vec<u8>,
    has_black_channel: bool,
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

fn is_gray_profile(profile: &JxlColorProfile) -> bool {
    match profile {
        JxlColorProfile::Simple(JxlColorEncoding::GrayscaleColorSpace { .. }) => true,
        JxlColorProfile::Icc(bytes) => {
            // ICC.1:2022 §7.2: bytes 16–19 of the profile header are the Color Space
            // signature. 'GRAY' (0x47524159) identifies a grayscale data colorspace.
            // https://www.color.org/specification/ICC.1-2022-05.pdf
            bytes
                .get(16..20)
                .and_then(|b| b.try_into().ok())
                .map(|b: [u8; 4]| u32::from_be_bytes(b) == 0x47524159)
                .unwrap_or(false)
        }
        _ => false,
    }
}

enum BufMode<'a> {
    None,
    Single(JxlOutputBuffer<'a>),
    Two([JxlOutputBuffer<'a>; 2]),
}

impl JxlApiDecoder {
    pub fn new(metadata_only: bool, has_cms: bool) -> Self {
        log::debug!("JxlApiDecoder::new metadata_only={metadata_only} has_cms={has_cms}");
        let options = JxlDecoderOptions::default();
        let inner = JxlDecoderInner::new(options);

        Self {
            inner,
            metadata_only,
            pixel_format_set: false,
            has_cms,
            use_f16: false,
            frame_ready: false,
            frame_duration: None,
            icc_profile_cache: Vec::new(),
            has_black_channel: false,
        }
    }

    pub fn new_scanner() -> Self {
        log::debug!("JxlApiDecoder::new_scanner");
        let mut options = JxlDecoderOptions::default();
        options.scan_frames_only = true;

        let inner = JxlDecoderInner::new(options);

        Self {
            inner,
            metadata_only: false,
            pixel_format_set: false,
            has_cms: false,
            use_f16: false,
            frame_ready: false,
            frame_duration: None,
            icc_profile_cache: Vec::new(),
            has_black_channel: false,
        }
    }

    pub fn scanned_frames(&self) -> &[VisibleFrameInfo] {
        self.inner.scanned_frames()
    }

    fn bytes_per_pixel(&self) -> usize {
        let Some(fmt) = self.inner.current_pixel_format() else {
            return 4;
        };
        let bytes_per_sample = match &fmt.color_data_format {
            Some(JxlDataFormat::F16 { .. }) => 2,
            _ => 1,
        };
        fmt.color_type.samples_per_pixel() * bytes_per_sample
    }

    pub fn is_gray(&self) -> bool {
        self.inner
            .current_pixel_format()
            .map(|fmt| fmt.color_type.is_grayscale())
            .unwrap_or(false)
    }

    pub fn has_black_channel(&self) -> bool {
        debug_assert!(self.pixel_format_set);
        self.has_black_channel
    }

    pub fn get_output_icc_profile(&mut self) -> &[u8] {
        if self.icc_profile_cache.is_empty() {
            if let Some(profile) = self.inner.output_color_profile() {
                self.icc_profile_cache = profile.as_icc().into_owned();
            }
        }
        &self.icc_profile_cache
    }

    /// Returns the CICP (colour_primaries, transfer_characteristics,
    /// rendering_intent) codes for the output color profile when it is a simple
    /// RGB encoding that maps to standard CICP values. Returns None for embedded
    /// ICC profiles, grayscale, XYB, and custom primaries / white points / gamma
    /// that don't correspond to a CICP enum value. The rendering_intent uses the
    /// ICC codes (0=perceptual, 1=relative, 2=saturation, 3=absolute).
    pub fn get_output_cicp(&self) -> Option<(u8, u8, u8)> {
        let JxlColorProfile::Simple(JxlColorEncoding::RgbColorSpace {
            white_point,
            primaries,
            transfer_function,
            rendering_intent,
        }) = self.inner.output_color_profile()?
        else {
            return None;
        };

        let primaries_val: u8 = match (white_point, primaries) {
            (JxlWhitePoint::D65, JxlPrimaries::SRGB) => 1,
            (JxlWhitePoint::D65, JxlPrimaries::BT2100) => 9,
            (JxlWhitePoint::D65, JxlPrimaries::P3) => 12,
            (JxlWhitePoint::DCI, JxlPrimaries::P3) => 11,
            _ => return None,
        };
        let transfer_val: u8 = match transfer_function {
            JxlTransferFunction::BT709 => 1,
            JxlTransferFunction::Linear => 8,
            JxlTransferFunction::SRGB => 13,
            JxlTransferFunction::PQ => 16,
            JxlTransferFunction::DCI => 17,
            JxlTransferFunction::HLG => 18,
            // Custom gamma has no CICP code.
            JxlTransferFunction::Gamma(_) => return None,
        };

        Some((primaries_val, transfer_val, *rendering_intent as u8))
    }

    /// Flush partially-decoded pixels into `output_buffer`. Returns true if any new
    /// pixels were written since the previous call; false if nothing new was
    /// rendered and the buffer is unchanged.
    /// `k_buffer` receives the K (Black) channel (1 byte/pixel) for CMYK images.
    pub fn flush_pixels(
        &mut self,
        output_buffer: &mut [u8],
        k_buffer: Option<&mut [u8]>,
    ) -> Result<bool, Error> {
        let (width, height) = self
            .inner
            .basic_info()
            .map(|bi| (bi.size.0, bi.size.1))
            .unwrap_or((0, 0));
        let bytes_per_row = width
            .checked_mul(self.bytes_per_pixel())
            .ok_or(Error::Overflow)?;

        log::trace!("flush_pixels: {width}x{height} bytes_per_row={bytes_per_row}");

        let result = match k_buffer {
            Some(k) if self.has_black_channel => {
                let mut bufs = [
                    JxlOutputBuffer::new(output_buffer, height, bytes_per_row),
                    JxlOutputBuffer::new(k, height, width),
                ];
                self.inner.flush_pixels(&mut bufs, None).map_err(Error::from)
            }
            _ => {
                let mut buf = JxlOutputBuffer::new(output_buffer, height, bytes_per_row);
                self.inner
                    .flush_pixels(std::slice::from_mut(&mut buf), None)
                    .map_err(Error::from)
            }
        };
        match &result {
            Ok(rendered) => log::trace!("flush_pixels: rendered_new={rendered}"),
            Err(e) => log::debug!("flush_pixels: error: {e:?}"),
        }
        result
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

    fn set_pixel_format(&mut self) {
        debug_assert!(self.inner.basic_info().is_some());
        let basic_info = self.inner.basic_info().unwrap();

        let is_hdr = basic_info.bit_depth.bits_per_sample() > 8;

        // Detect Black (K) extra channel for CMYK images.
        let extra_channel_format: Vec<_> = basic_info
            .extra_channels
            .iter()
            .map(|ec| {
                if ec.ec_type == ExtraChannel::Black {
                    Some(JxlDataFormat::U8 { bit_depth: 8 })
                } else {
                    None
                }
            })
            .collect();
        self.has_black_channel = extra_channel_format.iter().any(|f| f.is_some());

        // For SDR grayscale images with CMS, request Gray/GrayAlpha output so the
        // C++ gray CMS path receives compact pixels. Without CMS, use RGBA so the
        // data passes through directly.
        let color_type = if !is_hdr
            && self.has_cms
            && self
                .inner
                .embedded_color_profile()
                .map(is_gray_profile)
                .unwrap_or(false)
        {
            let has_alpha = basic_info
                .extra_channels
                .iter()
                .any(|ec| ec.ec_type == ExtraChannel::Alpha);
            if has_alpha {
                JxlColorType::GrayscaleAlpha
            } else {
                JxlColorType::Grayscale
            }
        } else {
            JxlColorType::Rgba
        };

        // Request f16 output only when CMS is available; without CMS, u8 output
        // lets jxl-rs convert HDR → sRGB u8 so the user sees something.
        self.use_f16 = is_hdr && self.has_cms;
        log::debug!(
            "set_pixel_format: {}x{} is_hdr={is_hdr} has_cms={} has_black_channel={} \
             color_type={color_type:?} use_f16={}",
            basic_info.size.0,
            basic_info.size.1,
            self.has_cms,
            self.has_black_channel,
            self.use_f16
        );
        let pixel_format = JxlPixelFormat {
            color_type,
            color_data_format: Some(if self.use_f16 {
                JxlDataFormat::F16 {
                    endianness: Endianness::native(),
                }
            } else {
                JxlDataFormat::U8 { bit_depth: 8 }
            }),
            extra_channel_format,
        };
        self.inner.set_pixel_format(pixel_format);
        self.pixel_format_set = true;
    }

    /// Process JXL data. Pass output_buffer once frame_ready is true.
    /// k_buffer receives K (Black) channel data (1 byte/pixel) for CMYK images.
    /// Returns Ok(true) when frame_ready changes state.
    pub fn process_data<'a>(
        &mut self,
        data: &mut impl JxlBitstreamInput,
        output_buffer: Option<&'a mut [u8]>,
        k_buffer: Option<&'a mut [u8]>,
    ) -> Result<bool, Error> {
        let has_output_buffer = output_buffer.is_some();
        debug_assert!(!has_output_buffer || self.pixel_format_set);

        // Create output buffer wrapper(s) if provided.
        // When output_buffer is provided, dimensions must already be known.
        let (width, height) = self
            .inner
            .basic_info()
            .map(|bi| (bi.size.0, bi.size.1))
            .unwrap_or((0, 0));
        let bytes_per_row = width
            .checked_mul(self.bytes_per_pixel())
            .ok_or(Error::Overflow)?;

        let mut buf_mode: BufMode<'a> = match (output_buffer, k_buffer) {
            (Some(out), Some(k)) if self.has_black_channel => BufMode::Two([
                JxlOutputBuffer::new(out, height, bytes_per_row),
                JxlOutputBuffer::new(k, height, width),
            ]),
            (Some(out), _) => BufMode::Single(JxlOutputBuffer::new(out, height, bytes_per_row)),
            _ => BufMode::None,
        };

        loop {
            let bufs: Option<&mut [JxlOutputBuffer]> = match &mut buf_mode {
                BufMode::Two(arr) => Some(arr.as_mut_slice()),
                BufMode::Single(buf) => Some(std::slice::from_mut(buf)),
                BufMode::None => None,
            };
            let result = self.inner.process(data, bufs, None);

            let need_more = match result {
                Err(e) => {
                    log::debug!("process_data: decode error: {e:?}");
                    return Err(e.into());
                }
                Ok(ProcessingResult::Complete { .. }) => false,
                Ok(ProcessingResult::NeedsMoreInput { .. }) => true,
            };
            log::trace!("process_data: inner.process -> need_more={need_more}");

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

            // jxl-rs guarantees that once basic_info() returns Some, the
            // embedded color profile has also been parsed; set_pixel_format
            // relies on the latter (update_default_output_color_profile
            // unwraps it).
            if !self.pixel_format_set && self.inner.basic_info().is_some() {
                debug_assert!(self.inner.embedded_color_profile().is_some());
                self.set_pixel_format();
                debug_assert!(self.pixel_format_set);
                debug_assert!(self.inner.current_pixel_format().is_some());
                if !need_more {
                    // Re-enter to let jxl-rs make further progress now that the
                    // pixel format is configured if we don't need more input.
                    continue;
                }
            }

            if need_more {
                return Ok(false);
            }

            let frame_header = self.inner.frame_header();
            if let Some(frame_header) = frame_header {
                self.frame_duration = frame_header.duration.or(Some(0.0));
                self.frame_ready = true;
                log::debug!(
                    "process_data: frame header ready, duration={:?}ms",
                    self.frame_duration
                );
                return Ok(true);
            } else if self.frame_ready {
                // Frame was rendered
                self.frame_ready = false;
                log::trace!("process_data: frame rendered");
                return Ok(true);
            }
            // No frame yet, need more data
            return Ok(false);
        }
    }
}
