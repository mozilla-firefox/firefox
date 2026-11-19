// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::fmt;

use neqo_transport::StreamId;
use sfv::{BareItem, Dictionary, Integer, Item, ListEntry, Parser};

use crate::{Error, Res, frames::HFrame};

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub struct Priority {
    urgency: u8,
    incremental: bool,
}

impl Default for Priority {
    fn default() -> Self {
        Self {
            urgency: 3,
            incremental: false,
        }
    }
}

impl Priority {
    /// # Panics
    ///
    /// If an invalid urgency (>7 is given)
    #[must_use]
    pub fn new(urgency: u8, incremental: bool) -> Self {
        assert!(urgency < 8);
        let priority = Self {
            urgency,
            incremental,
        };
        #[cfg(feature = "build-fuzzing-corpus")]
        neqo_common::write_item_to_fuzzing_corpus("priority", priority.to_string().as_bytes());
        priority
    }

    /// Constructs a priority from raw bytes (either a field value of frame content).
    ///
    /// # Errors
    ///
    /// When the contained syntax is invalid.
    pub fn from_bytes(bytes: &[u8]) -> Res<Self> {
        #[cfg(feature = "build-fuzzing-corpus")]
        neqo_common::write_item_to_fuzzing_corpus("priority", bytes);

        let dict: Dictionary = Parser::new(bytes).parse().map_err(|_| Error::HttpFrame)?;
        let urgency = match dict.get("u") {
            Some(ListEntry::Item(Item {
                bare_item: BareItem::Integer(u),
                ..
            })) if (Integer::constant(0)..=Integer::constant(7)).contains(u) => {
                u8::try_from(*u).map_err(|_| Error::Internal)?
            }
            _ => 3,
        };
        let incremental = match dict.get("i") {
            Some(ListEntry::Item(Item {
                bare_item: BareItem::Boolean(i),
                ..
            })) => *i,
            _ => false,
        };
        Ok(Self {
            urgency,
            incremental,
        })
    }
}

impl fmt::Display for Priority {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self {
                urgency: 3,
                incremental: false,
            } => Ok(()),
            Self {
                urgency: 3,
                incremental: true,
            } => write!(f, "i"),
            Self {
                urgency,
                incremental: false,
            } => write!(f, "u={urgency}"),
            Self {
                urgency,
                incremental: true,
            } => write!(f, "u={urgency},i"),
        }
    }
}

#[derive(Debug)]
pub struct PriorityHandler {
    priority: Priority,
    last_sent_priority_update: Option<Priority>,
}

impl PriorityHandler {
    pub const fn new(priority: Priority, send_initial_priority_update: bool) -> Self {
        Self {
            priority,
            last_sent_priority_update: if send_initial_priority_update {
                None
            } else {
                Some(priority)
            },
        }
    }

    /*pub fn priority(&self) -> Priority {
        self.priority
    }*/

    /// Returns if an priority update will be issued
    pub fn maybe_update_priority(&mut self, priority: Priority) -> bool {
        if priority == self.priority {
            false
        } else {
            self.priority = priority;
            true
        }
    }

    pub const fn priority_update_sent(&mut self) {
        self.last_sent_priority_update = Some(self.priority);
    }

    /// Returns `HFrame` if an priority update is outstanding or an initial `PRIORITY_UPDATE` frame
    /// should be sent.
    pub fn maybe_encode_frame(&self, stream_id: StreamId) -> Option<HFrame> {
        if Some(self.priority) == self.last_sent_priority_update {
            None
        } else {
            Some(HFrame::PriorityUpdateRequest {
                element_id: stream_id.as_u64(),
                priority: self.priority,
            })
        }
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod test {
    use neqo_transport::StreamId;

    use crate::{HFrame, Priority, priority::PriorityHandler};

    #[test]
    fn priority_updates_ignore_same() {
        let mut p = PriorityHandler::new(Priority::new(5, false), false);
        assert!(!p.maybe_update_priority(Priority::new(5, false)));
        // updating with the same priority -> there should not be any priority frame sent
        assert!(p.maybe_encode_frame(StreamId::new(4)).is_none());
    }

    #[test]
    fn priority_updates_send_update() {
        let mut p = PriorityHandler::new(Priority::new(5, false), false);
        assert!(p.maybe_update_priority(Priority::new(6, false)));
        // updating with the a different priority -> there should be a priority frame sent
        assert!(p.maybe_encode_frame(StreamId::new(4)).is_some());
    }

    #[test]
    fn multiple_priority_updates_ignore_same() {
        let mut p = PriorityHandler::new(Priority::new(5, false), false);
        assert!(p.maybe_update_priority(Priority::new(6, false)));
        assert!(p.maybe_update_priority(Priority::new(5, false)));
        // initial and last priority same -> there should not be any priority frame sent
        assert!(p.maybe_encode_frame(StreamId::new(4)).is_none());
    }

    #[test]
    fn multiple_priority_updates_send_update() {
        let mut p = PriorityHandler::new(Priority::new(5, false), false);
        assert!(p.maybe_update_priority(Priority::new(6, false)));
        assert!(p.maybe_update_priority(Priority::new(7, false)));
        // updating two times with a different priority -> the last priority update should be in the
        // next frame
        let expected = HFrame::PriorityUpdateRequest {
            element_id: 4,
            priority: Priority::new(7, false),
        };
        assert_eq!(p.maybe_encode_frame(StreamId::new(4)), Some(expected));
    }

    #[test]
    fn priority_updates_incremental() {
        let mut p = PriorityHandler::new(Priority::new(5, false), false);
        assert!(p.maybe_update_priority(Priority::new(5, true)));
        // updating the incremental parameter -> there should be a priority frame sent
        let expected = HFrame::PriorityUpdateRequest {
            element_id: 4,
            priority: Priority::new(5, true),
        };
        assert_eq!(p.maybe_encode_frame(StreamId::new(4)), Some(expected));
    }

    #[test]
    fn priority_update_sent_clears_pending() {
        let mut p = PriorityHandler::new(Priority::new(5, false), false);
        assert!(p.maybe_update_priority(Priority::new(6, false)));
        assert!(p.maybe_encode_frame(StreamId::new(4)).is_some());
        p.priority_update_sent();
        // After sending, no more pending update.
        assert!(p.maybe_encode_frame(StreamId::new(4)).is_none());
    }

    #[test]
    fn initial_priority_update_sent() {
        let mut p = PriorityHandler::new(Priority::new(5, false), true);
        // an initial priority update frame should always be sent
        let expected = HFrame::PriorityUpdateRequest {
            element_id: 4,
            priority: Priority::new(5, false),
        };
        assert_eq!(p.maybe_encode_frame(StreamId::new(4)), Some(expected));
        p.priority_update_sent();
        assert!(p.maybe_encode_frame(StreamId::new(4)).is_none());
    }

    #[test]
    fn from_bytes_invalid_urgency_defaults() {
        // Urgency outside 0-7 should default to 3.
        let p = Priority::from_bytes(b"u=8").unwrap();
        assert_eq!(p, Priority::default());

        let p = Priority::from_bytes(b"u=-1").unwrap();
        assert_eq!(p, Priority::default());
    }

    #[test]
    fn priority_display_urgency_and_incremental() {
        assert_eq!(Priority::new(5, true).to_string(), "u=5,i");
    }
}
