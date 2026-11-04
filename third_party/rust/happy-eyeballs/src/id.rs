/// Unique identifier for DNS queries and connection attempts.
///
/// Used to correlate requests (Output events) with their responses (Input events).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Id(u64);

impl From<u64> for Id {
    fn from(value: u64) -> Self {
        Self(value)
    }
}

impl From<Id> for u64 {
    fn from(id: Id) -> Self {
        id.0
    }
}

/// Generator for unique IDs.
///
/// Uses a simple incrementing counter. Internal to the crate.
pub struct IdGenerator {
    next: u64,
}

impl IdGenerator {
    /// Creates a new ID generator starting from 0.
    pub(crate) const fn new() -> Self {
        Self { next: 0 }
    }

    /// Generates the next unique ID.
    ///
    /// Uses wrapping arithmetic to handle overflow (though u64 overflow is extremely unlikely).
    pub(crate) const fn next_id(&mut self) -> Id {
        let id = self.next;
        self.next = self.next.wrapping_add(1);
        Id(id)
    }
}
