/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Gamut mapping - Raytrace algorithm.
//! <https://drafts.csswg.org/css-color-4/#gamut-mapping>

use crate::color::{AbsoluteColor, ColorComponents, ColorSpace, gamut::MIN_PRECISION};

impl AbsoluteColor {
    /// 13.2.5. The Ray Trace Gamut Mapping
    /// <https://drafts.csswg.org/css-color-4/#GMA-Raytrace>
    pub fn gamut_map_raytrace(&self, dest_color_space: ColorSpace) -> Self {
        macro_rules! in_range {
            ($l:expr, $c:expr, $h:expr) => {{ $c >= $l && $c <= $h }};
        }

        const MIN_L: f32 = MIN_PRECISION;
        const MAX_L: f32 = 1.0;

        // Get the linear version of the destination color space;
        // else, use the same space for the destination.
        // NOTE that this will cause temporary approximations for wide-gamut
        // RGB spaces beyond DisplayP3 (A98RGB, ProPhoto, and Rec2020) that
        // currently don't have linear versions built-out for them.
        // See comments/notes on [`ColorSpace::get_linear_color_space()`].
        let dest_linear_color_space = dest_color_space
            .get_linear_color_space()
            .unwrap_or(dest_color_space);

        // 1. if destination has no gamut limits (XYZ-D65, XYZ-D50, Lab, LCH,
        //    Oklab, OkLCh) convert origin to destination and return it as the
        //    gamut mapped color
        if matches!(
            dest_color_space,
            ColorSpace::Lab
                | ColorSpace::Lch
                | ColorSpace::Oklab
                | ColorSpace::Oklch
                | ColorSpace::XyzD50
                | ColorSpace::XyzD65
        ) {
            return self.to_color_space(dest_color_space);
        }

        // 2. let `origin_OkLCh` be `origin` converted from `origin color
        //    space` to the OkLCh color space
        let origin_oklch = self.to_color_space(ColorSpace::Oklch);

        // 3. if the Lightness of `origin_OkLCh` is greater than or equal to
        //    100%, convert `oklab(1 0 0 / origin.alpha)` to `destination` and
        //    return it as the gamut mapped color
        if origin_oklch.components.0 >= MAX_L {
            return AbsoluteColor::new(ColorSpace::Oklab, 1.0, 0.0, 0.0, self.alpha)
                .to_color_space(dest_color_space);
        }

        // 4. if the Lightness of `origin_OkLCh` is less than than or equal to
        //    0%, convert `oklab(0 0 0 / origin.alpha)` to `destination` and
        //    return it as the gamut mapped color
        if origin_oklch.components.0 <= MIN_L {
            return AbsoluteColor::new(ColorSpace::Oklab, 0.0, 0.0, 0.0, self.alpha)
                .to_color_space(dest_color_space);
        }

        // 5. let `l_origin` be the OkLCh lightness component of `origin_OkLCh`
        // 6. let `h_origin` be the OkLCh hue component of `origin_OkLCh`
        let ColorComponents(l_origin, _, h_origin) = origin_oklch.components;

        // 7. let `anchor` be an achromatic OkLCh color formed with `l_origin`
        //    as lightness, 0 as chroma, and `h_origin` as hue, converted to
        //    the linear-light form of destination
        let mut anchor = AbsoluteColor::new(ColorSpace::Oklch, l_origin, 0.0, h_origin, self.alpha)
            .to_color_space(dest_linear_color_space)
            .clip();

        // 8. let `origin_rgb` be `origin_OkLCh` converted to the linear-light
        //    form of destination
        let mut origin_rgb = origin_oklch.to_color_space(dest_linear_color_space);

        // 9. if origin_rgb is not in gamut
        if !origin_rgb.in_gamut() {
            // 9.1. let `low` be 1E-5 [^1]
            const LOW: f32 = 1.0e-5;

            // 9.2 let `high` be 1.0 - `low` [^2]
            const HIGH: f32 = 1.0 - LOW;

            // 9.3 let `last` be `origin_rgb`
            let mut last = origin_rgb;

            // We're doing 4 cycles of ray tracing.
            // 9.4 `for (i=0; i<4; i++)`:
            for i in 0..4 {
                // 9.4.1. if (`i > 0`)
                if i > 0 {
                    // 9.4.1.1. let `current_OkLCh` be `origin_rgb` converted to OkLCh
                    let mut current_oklch = origin_rgb.to_color_space(ColorSpace::Oklch);

                    // 9.4.1.2. let the lightness of `current_OkLCh` be `l_origin`
                    current_oklch.components.0 = l_origin;
                    // 9.4.1.3. let the hue of `current_OkLCh` be `h_origin` [^3]
                    current_oklch.components.2 = h_origin;

                    // 9.4.1.4. let `origin_rgb` be `current_OkLCh` converted to the
                    //         linear-light form of destination
                    origin_rgb = current_oklch.to_color_space(dest_linear_color_space);
                }

                // 9.4.2. **Cast a ray** from `anchor` to `origin_rgb` and let
                //        `intersection` be the intersection of this ray with the
                //        gamut boundary
                let intersection = Self::cast_ray(&anchor.components, &origin_rgb.components);

                // 9.4.3. if an intersection was not found, let `origin_rgb` be
                //        `last` and exit the loop [^5]
                let Some(intersection) = intersection else {
                    origin_rgb = last;
                    break;
                };

                // 9.4.4. if (`i > 0`) AND (each component of `origin_rgb` is
                //        between `low` and `high`) then let `anchor` be
                //        `origin_rgb` [^4]
                if (i > 0)
                    && in_range!(LOW, origin_rgb.components.0, HIGH)
                    && in_range!(LOW, origin_rgb.components.1, HIGH)
                    && in_range!(LOW, origin_rgb.components.2, HIGH)
                {
                    anchor = origin_rgb;
                }

                // 9.4.5. let `origin_rgb` be `intersection`
                origin_rgb.components = intersection;

                // 9.4.6. let `last` be `intersection`
                last.components = intersection;
            }
        }

        // 10. let clip(color) be a function which converts color to
        //     destination, clamps each component to the bounds of the
        //     reference range for that component, and returns the result
        //     See [`Self::clip`] and [`Self::clip_to_dest_space`].

        // NOTE: in the 2026-02-27 draft, this line doesn't make sense.
        //       I've posted a question/issue to the CSS Color 4 team about it.
        //       See <https://github.com/w3c/csswg-drafts/issues/10579#issuecomment-4122677476>
        //       Isaac (@facelessuser) replied that it is ambiguous, but ATOW this hasn't
        //       been fixed in the working copy pseudo-code.
        //       See <https://github.com/w3c/csswg-drafts/issues/10579#issuecomment-4123490623>
        // 11. set clipped to clip(current)
        //
        // In the meantime, this was the logic from the 2026-02-02 draft that worked:
        // 13. let `clipped` be `origin_rgb` clipped to gamut (components in
        //     the range 0 to 1), thus trimming off any noise due to floating
        //     point inaccuracy
        let clipped = origin_rgb.clip_to_dest_space(dest_color_space);

        // 12. return `clipped` as the gamut mapped color
        clipped
    }

    /// To **cast a ray** through a linear-light RGB space from `start` to
    /// `end` (in gamut mapping, `start` is an anchor within the RGB gamut and
    /// `end` is the gamut mapped color, on the cubical gamut surface)
    /// <https://drafts.csswg.org/css-color-4/#GMA-Raytrace>
    fn cast_ray(start: &ColorComponents, end: &ColorComponents) -> Option<ColorComponents> {
        // 1. let `bmin` and `bmax` be 3-element arrays with the gamut’s lower
        //    and upper bounds, respectively [^6]
        // NOTE: assuming these are always tied to RGB bounds [0.0, 1.0]
        let bmin = [0.0, 0.0, 0.0];
        let bmax = [1.0, 1.0, 1.0];

        // 2. let `tfar` be `infinity` (or some very large number)
        let mut tfar = f32::INFINITY;

        // 3. let `tnear` be `-infinity` (or some very large, negative number)
        let mut tnear = f32::NEG_INFINITY;

        // 4. let `direction` be a 3-element array
        let mut direction = [0.0, 0.0, 0.0];

        // reshape start and end so we can iterate over them
        let start_array = start.to_array();
        let end_array = end.to_array();

        // 5. let `longest` be 0
        let mut longest: f32 = 0.0;

        // 6. let `magnitude` be 0
        let mut magnitude: f32 = 0.0;

        // 7. `for (i = 0; i < 3; i++)`:
        for i in 0..3 {
            // 7.1. let `a` be `start[i]`
            let a = start_array[i];

            // 7.2. let `b` be `end[i]`
            let b = end_array[i];

            // 7.3. let `d` be `b - a`
            let d = b - a;

            // 7.4. let `direction[i]` be `d`
            direction[i] = d;

            // 7.5. let `longest` be `max(longest, abs(d))`
            longest = longest.max(d.abs());

            // 7.6. let `magnitude` be `max(magnitude, abs(a), abs(b))`
            magnitude = magnitude.max(a.abs()).max(b.abs());

            // 7.7. if `d` is not 0
            if d != 0.0 {
                // 7.7.1. let `t1` be `(bmin[i] - a) / d`
                let t1 = (bmin[i] - a) / d;

                // 7.7.2. let `t2` be `(bmax[i] - a) / d`
                let t2 = (bmax[i] - a) / d;

                // 7.7.3. let `tnear` be `max(min(t1, t2), tnear)`
                tnear = t1.min(t2).max(tnear);

                // 7.7.4. let `tfar` be `min(max(t1, t2), tfar)`
                tfar = t1.max(t2).min(tfar);
            }
            // 7.8. else if (`a < bmin[i]` or `a > bmax[i]`)
            //      * return `INTERSECTION NOT FOUND`
            else if a < bmin[i] || a > bmax[i] {
                return None;
            }
        }

        // 8. if `longest` is less than or equal to `magnitude` * MIN_THRESHOLD
        //    * return `INTERSECTION NOT FOUND`
        const MIN_THRESHOLD: f32 = 1e-6;
        if longest <= magnitude * MIN_THRESHOLD {
            return None;
        }

        // 9. if (`tnear > tfar` or `tfar < 0`)
        //    * return `INTERSECTION NOT FOUND`
        if (tnear > tfar) || (tfar < 0.0) {
            return None;
        }

        // 10. if `tnear < 0`
        //    * let `tnear` be `tfar` [^7]
        if tnear < 0.0 {
            tnear = tfar;
        }

        // 11. if tnear is infinite (or matches the initial very large value)
        //    * return `INTERSECTION NOT FOUND`
        if tnear.is_infinite() {
            return None;
        }

        let mut result = [0.0, 0.0, 0.0];
        // 12. for (`i = 0; i < 3; i++`):
        //    * let `result[i]` be `start[i] + direction[i] * tnear`
        for i in 0..3 {
            result[i] = start_array[i] + direction[i] * tnear;
        }

        // 13. return `result`
        Some(ColorComponents(result[0], result[1], result[2]))
    }
}
