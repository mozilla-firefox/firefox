/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use api::{ColorF, GradientStop};
use crate::pattern::gradient::MAX_GRADIENT_STOPS;

mod linear;
mod radial;
mod conic;

pub use linear::*;
pub use radial::*;
pub use conic::*;

// `GradientStopKey` now lives in `webrender_api` so builder-side interning keys
// can reference it. Re-exported here to keep existing references working.
pub use api::key_types::GradientStopKey;

// Convert `stop_keys` into a vector of `GradientStop`s, which is a more
// convenient representation for the current gradient builder. Compute the
// minimum stop alpha along the way. Gradients with more than
// `MAX_GRADIENT_STOPS` stops are evenly subsampled (keeping the first and last
// stops) so that their GPU data fits in a single GPU buffer row.
fn stops_and_min_alpha(stop_keys: &[GradientStopKey]) -> (Vec<GradientStop>, f32) {
    let mut min_alpha: f32 = 1.0;
    let to_stop = |stop_key: &GradientStopKey| {
        let color: ColorF = stop_key.color.into();
        min_alpha = min_alpha.min(color.a);

        GradientStop {
            offset: stop_key.offset,
            color,
        }
    };

    let count = stop_keys.len();
    let stops = if count <= MAX_GRADIENT_STOPS {
        stop_keys.iter().map(to_stop).collect()
    } else {
        let last = MAX_GRADIENT_STOPS - 1;
        (0..MAX_GRADIENT_STOPS)
            .map(|i| &stop_keys[(i * (count - 1) + last / 2) / last])
            .map(to_stop)
            .collect()
    };

    (stops, min_alpha)
}

#[test]
fn stops_are_subsampled_to_fit() {
    use api::ColorU;

    let keys: Vec<GradientStopKey> = (0..2000).map(|i| GradientStopKey {
        offset: i as f32 / 1999.0,
        color: ColorU::new(0, 0, 0, if i == 1 { 0 } else { 255 }),
    }).collect();

    let (stops, min_alpha) = stops_and_min_alpha(&keys);
    assert_eq!(stops.len(), MAX_GRADIENT_STOPS);
    assert_eq!(stops.first().unwrap().offset, 0.0);
    assert_eq!(stops.last().unwrap().offset, 1.0);
    assert!(stops.windows(2).all(|w| w[0].offset < w[1].offset));
    assert_eq!(min_alpha, 1.0);

    let (stops, _) = stops_and_min_alpha(&keys[..MAX_GRADIENT_STOPS]);
    assert_eq!(stops.len(), MAX_GRADIENT_STOPS);
}

#[test]
#[cfg(target_pointer_width = "64")]
fn test_struct_sizes() {
    use std::mem;
    // The sizes of these structures are critical for performance on a number of
    // talos stress tests. If you get a failure here on CI, there's two possibilities:
    // (a) You made a structure smaller than it currently is. Great work! Update the
    //     test expectations and move on.
    // (b) You made a structure larger. This is not necessarily a problem, but should only
    //     be done with care, and after checking if talos performance regresses badly.
    assert_eq!(mem::size_of::<api::interned_prims::LinearGradient>(), 72, "LinearGradient size changed");
    assert_eq!(mem::size_of::<LinearGradientTemplate>(), 104, "LinearGradientTemplate size changed");
    assert_eq!(mem::size_of::<LinearGradientKey>(), 104, "LinearGradientKey size changed");

    assert_eq!(mem::size_of::<api::interned_prims::RadialGradient>(), 72, "RadialGradient size changed");
    assert_eq!(mem::size_of::<RadialGradientTemplate>(), 112, "RadialGradientTemplate size changed");
    assert_eq!(mem::size_of::<RadialGradientKey>(), 112, "RadialGradientKey size changed");

    assert_eq!(mem::size_of::<api::interned_prims::ConicGradient>(), 72, "ConicGradient size changed");
    assert_eq!(mem::size_of::<ConicGradientTemplate>(), 112, "ConicGradientTemplate size changed");
    assert_eq!(mem::size_of::<ConicGradientKey>(), 112, "ConicGradientKey size changed");
}
