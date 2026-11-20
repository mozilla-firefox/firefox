// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::collections::HashMap;
use std::hash::Hash;

use crate::event_database::RecordedEvent;
use crate::TestGetValue;

/// Extra keys for events.
///
/// Extra keys need to be pre-defined and map to a string representation.
///
/// For user-defined `EventMetric`s these will be defined as `struct`s.
/// Each extra key will be a field in that struct.
/// Each field will correspond to an entry in the `ALLOWED_KEYS` list.
/// The Glean SDK requires the keys as strings for submission in pings,
/// whereas in code we want to provide users a type to work with
/// (e.g. to avoid typos or misuse of the API).
pub trait ExtraKeys {
    /// List of allowed extra keys as strings.
    const ALLOWED_KEYS: &'static [&'static str];

    /// Convert the event extras into a hashmap of extra key to extra value.
    fn into_ffi_extra(self) -> HashMap<String, String>;
}

/// Default of no extra keys for events.
///
/// An enum with no values for convenient use as the default set of extra keys
/// that an [`EventMetric`](crate::metrics::EventMetric) can accept.
///
/// *Note*: There exist no values for this enum, it can never exist.
/// It its equivalent to the [`never / !` type](https://doc.rust-lang.org/std/primitive.never.html).
#[derive(Clone, Copy, Debug, Hash, Eq, PartialEq)]
pub enum NoExtraKeys {}

impl ExtraKeys for NoExtraKeys {
    const ALLOWED_KEYS: &'static [&'static str] = &[];

    fn into_ffi_extra(self) -> HashMap<String, String> {
        unimplemented!("non-existing extra keys can't be turned into a list")
    }
}

/// The possible errors when parsing to an extra key.
pub enum EventRecordingError {
    /// The id doesn't correspond to a valid extra key
    InvalidId,
    /// The value doesn't correspond to a valid extra key
    InvalidExtraKey,
}

/// A description for the [`EventMetric`](crate::metrics::EventMetric) type.
///
/// When changing this trait, make sure all the operations are
/// implemented in the related type in `../metrics/`.
pub trait Event: TestGetValue<Output = Vec<RecordedEvent>> {
    /// The type of the allowed extra keys for this event.
    type Extra: ExtraKeys;

    /// Records an event.
    ///
    /// # Arguments
    ///
    /// * `extra` - (optional) An object for the extra keys.
    fn record<M: Into<Option<Self::Extra>>>(&self, extra: M);
}
