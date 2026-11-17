// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

use std::{
    collections::VecDeque,
    fmt::{self, Display, Formatter},
    rc::Rc,
};

use neqo_common::to_u64;
use tracing::trace;

use crate::{
    Error, Res,
    static_table::{HEADER_STATIC_TABLE, StaticTableEntry},
};

pub const ADDITIONAL_TABLE_ENTRY_SIZE: usize = 32;

pub struct LookupResult {
    pub index: u64,
    pub static_table: bool,
    pub value_matches: bool,
}

#[derive(Debug)]
pub struct DynamicTableEntry {
    base: u64,
    name: Rc<[u8]>,
    value: Rc<[u8]>,
    /// Number of header blocks that refer this entry.
    /// This is only used by the encoder.
    refs: u64,
}

impl DynamicTableEntry {
    pub const fn can_reduce(&self, first_not_acked: u64) -> bool {
        self.refs == 0 && self.base < first_not_acked
    }

    pub fn size(&self) -> usize {
        self.name.len() + self.value.len() + ADDITIONAL_TABLE_ENTRY_SIZE
    }

    pub const fn add_ref(&mut self) {
        self.refs += 1;
    }

    pub fn remove_ref(&mut self) {
        assert!(self.refs > 0);
        self.refs -= 1;
    }

    pub fn name(&self) -> &[u8] {
        &self.name
    }

    pub fn value(&self) -> &[u8] {
        &self.value
    }

    pub const fn index(&self) -> u64 {
        self.base
    }
}

#[derive(Debug)]
pub struct HeaderTable {
    dynamic: VecDeque<DynamicTableEntry>,
    /// The total capacity (in QPACK bytes) of the table. This is set by
    /// configuration.
    capacity: u64,
    /// The amount of used capacity.
    used: u64,
    /// The total number of inserts thus far.
    base: u64,
    /// This is number of inserts that are acked. this correspond to index of the first not acked.
    /// This is only used by the encoder.
    acked_inserts_cnt: u64,
}

impl Display for HeaderTable {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        write!(
            f,
            "HeaderTable for (base={} acked_inserts_cnt={} capacity={})",
            self.base, self.acked_inserts_cnt, self.capacity
        )
    }
}

impl HeaderTable {
    pub const fn new(encoder: bool) -> Self {
        Self {
            dynamic: VecDeque::new(),
            capacity: 0,
            used: 0,
            base: 0,
            acked_inserts_cnt: if encoder { 0 } else { u64::MAX },
        }
    }

    /// Returns number of inserts.
    pub const fn base(&self) -> u64 {
        self.base
    }

    /// Returns capacity of the dynamic table
    pub const fn capacity(&self) -> u64 {
        self.capacity
    }

    /// Change the dynamic table capacity.
    ///
    /// # Errors
    ///
    /// [`Error::ChangeCapacity`] if table capacity cannot be reduced.
    /// The table cannot be reduced if there are entries that are referred to at
    /// the moment, or whose inserts are unacked.
    pub fn set_capacity(&mut self, cap: u64) -> Res<()> {
        trace!("[{self}] set capacity to {cap}");
        if !self.evict_to(cap) {
            return Err(Error::ChangeCapacity);
        }
        self.capacity = cap;
        Ok(())
    }

    /// Get a static entry with `index`.
    ///
    /// # Errors
    ///
    /// `HeaderLookup` if the index does not exist in the static table.
    pub fn get_static(index: u64) -> Res<&'static StaticTableEntry> {
        let inx = usize::try_from(index).or(Err(Error::HeaderLookup))?;
        HEADER_STATIC_TABLE.get(inx).ok_or(Error::HeaderLookup)
    }

    fn get_dynamic_with_abs_index(&mut self, index: u64) -> Res<&mut DynamicTableEntry> {
        if self.base <= index {
            debug_assert!(false, "This is an internal error");
            return Err(Error::HeaderLookup);
        }
        let inx = self.base - index - 1;
        let inx = usize::try_from(inx).or(Err(Error::HeaderLookup))?;
        self.dynamic.get_mut(inx).ok_or(Error::HeaderLookup)
    }

    fn get_dynamic_with_relative_index(&self, index: u64) -> Res<&DynamicTableEntry> {
        let inx = usize::try_from(index).or(Err(Error::HeaderLookup))?;
        self.dynamic.get(inx).ok_or(Error::HeaderLookup)
    }

    /// Get a entry in the  dynamic table.
    ///
    /// # Errors
    ///
    /// `HeaderLookup` if entry does not exist.
    pub fn get_dynamic(&self, index: u64, base: u64, post: bool) -> Res<&DynamicTableEntry> {
        let inx = if post {
            if self.base
                < base
                    .checked_add(index)
                    .and_then(|s| s.checked_add(1))
                    .ok_or(Error::IntegerOverflow)?
            {
                return Err(Error::HeaderLookup);
            }
            self.base - (base + index + 1)
        } else {
            if self.base.checked_add(index).ok_or(Error::IntegerOverflow)? < base {
                return Err(Error::HeaderLookup);
            }
            (self.base + index) - base
        };

        self.get_dynamic_with_relative_index(inx)
    }

    /// Remove a reference to a dynamic table entry.
    pub fn remove_ref(&mut self, index: u64) {
        trace!("[{self}] remove reference to entry {index}");
        self.get_dynamic_with_abs_index(index)
            .expect("we should have the entry")
            .remove_ref();
    }

    /// Add a reference to a dynamic table entry.
    pub fn add_ref(&mut self, index: u64) {
        trace!("[{self}] add reference to entry {index}");
        self.get_dynamic_with_abs_index(index)
            .expect("we should have the entry")
            .add_ref();
    }

    /// Look for a header pair in the static table only.
    ///
    /// Unlike [`Self::lookup`], this does not consult the dynamic table, so it borrows
    /// nothing and can never return a dynamic entry (which would need a reference to be
    /// held against eviction). The encoder uses this when it must not create a new
    /// dynamic-table reference for the stream.
    ///
    /// The function returns `LookupResult`: `index`, `static_table` (always `true` here)
    /// and `value_matches` (if the header value matches as well, not only the header name).
    pub fn static_lookup(name: &[u8], value: &[u8]) -> Option<LookupResult> {
        let mut name_match = None;
        for iter in HEADER_STATIC_TABLE {
            if iter.name() == name {
                if iter.value() == value {
                    return Some(LookupResult {
                        index: iter.index(),
                        static_table: true,
                        value_matches: true,
                    });
                }

                name_match.get_or_insert_with(|| LookupResult {
                    index: iter.index(),
                    static_table: true,
                    value_matches: false,
                });
            }
        }
        name_match
    }

    /// Look for a header pair in the static and dynamic tables.
    /// The function returns `LookupResult`: `index`, `static_table` (if it is a static table entry)
    /// and `value_matches` (if the header value matches as well not only header name)
    pub fn lookup(&self, name: &[u8], value: &[u8], can_block: bool) -> Option<LookupResult> {
        trace!("[{self}] lookup name:{name:?} value {value:?} can_block={can_block}");
        // A static value match wins outright; otherwise a static name-only match is kept
        // as a fallback that a dynamic name-only match must not displace.
        let static_match = Self::static_lookup(name, value);
        if static_match.as_ref().is_some_and(|r| r.value_matches) {
            return static_match;
        }

        let mut name_match = static_match;
        for iter in &self.dynamic {
            if !can_block && iter.index() >= self.acked_inserts_cnt {
                continue;
            }
            if iter.name() == name {
                if iter.value() == value {
                    return Some(LookupResult {
                        index: iter.index(),
                        static_table: false,
                        value_matches: true,
                    });
                }

                name_match.get_or_insert_with(|| LookupResult {
                    index: iter.index(),
                    static_table: false,
                    value_matches: false,
                });
            }
        }
        name_match
    }

    fn evict_to(&mut self, reduce: u64) -> bool {
        trace!(
            "[{self}] reduce table to {reduce}, currently used:{}",
            self.used,
        );
        while let Some(e) = self.dynamic.back() {
            if self.used <= reduce {
                break;
            }
            if !e.can_reduce(self.acked_inserts_cnt) {
                return false;
            }
            self.used -= to_u64(e.size());
            self.dynamic.pop_back();
        }
        true
    }

    pub fn can_evict_to(&self, reduce: u64) -> bool {
        let evictable_size: usize = self
            .dynamic
            .iter()
            .rev()
            .take_while(|e| e.can_reduce(self.acked_inserts_cnt))
            .map(DynamicTableEntry::size)
            .sum();

        self.used - to_u64(evictable_size) <= reduce
    }

    pub fn insert_possible(&self, size: usize) -> bool {
        let size = to_u64(size);
        size <= self.capacity && self.can_evict_to(self.capacity - size)
    }

    /// Insert a new entry.
    ///
    /// # Errors
    ///
    /// `DynamicTableFull` if an entry cannot be added to the table because there is not enough
    /// space and/or other entry cannot be evicted.
    pub fn insert(&mut self, name: &[u8], value: &[u8]) -> Res<u64> {
        self.insert_shared(name.into(), value.into())
    }

    fn insert_shared(&mut self, name: Rc<[u8]>, value: Rc<[u8]>) -> Res<u64> {
        trace!("[{self}] insert name={name:?} value={value:?}");
        let entry = DynamicTableEntry {
            name,
            value,
            base: self.base,
            refs: 0,
        };
        if to_u64(entry.size()) > self.capacity
            || !self.evict_to(self.capacity - to_u64(entry.size()))
        {
            return Err(Error::DynamicTableFull);
        }
        self.base += 1;
        self.used += to_u64(entry.size());
        let index = entry.index();
        self.dynamic.push_front(entry);
        Ok(index)
    }

    /// Insert a new entry with the name refer to by a index to static or dynamic table.
    ///
    /// # Errors
    ///
    /// `DynamicTableFull` if an entry cannot be added to the table because there is not enough
    /// space and/or other entry cannot be evicted.
    /// `HeaderLookup` if the index dos not exits in the static/dynamic table.
    pub fn insert_with_name_ref(
        &mut self,
        name_static_table: bool,
        name_index: u64,
        value: &[u8],
    ) -> Res<u64> {
        trace!(
            "[{self}] insert with ref to index={name_index} in {} value={value:?}",
            if name_static_table {
                "static"
            } else {
                "dynamic"
            },
        );
        let name = if name_static_table {
            Self::get_static(name_index)?.name().into()
        } else {
            Rc::clone(&self.get_dynamic(name_index, self.base, false)?.name)
        };
        self.insert_shared(name, value.into())
    }

    /// Duplicate an entry.
    ///
    /// # Errors
    ///
    /// `DynamicTableFull` if an entry cannot be added to the table because there is not enough
    /// space and/or other entry cannot be evicted.
    /// `HeaderLookup` if the index dos not exits in the static/dynamic table.
    pub fn duplicate(&mut self, index: u64) -> Res<u64> {
        trace!("[{self}] duplicate entry={index}");
        // need to remember name and value because insert may delete the entry.
        let entry = self.get_dynamic(index, self.base, false)?;
        let name = Rc::clone(&entry.name);
        let value = Rc::clone(&entry.value);
        self.insert_shared(name, value)
    }

    /// Increment number of acknowledge entries.
    ///
    /// # Errors
    ///
    /// `IncrementAck` if ack is greater than actual number of inserts.
    pub fn increment_acked(&mut self, increment: u64) -> Res<()> {
        trace!("[{self}] increment acked by {increment}");
        let acked = self
            .acked_inserts_cnt
            .checked_add(increment)
            .ok_or(Error::IncrementAck)?;
        if self.base < acked {
            return Err(Error::IncrementAck);
        }
        self.acked_inserts_cnt = acked;
        Ok(())
    }

    /// Return number of acknowledge inserts.
    pub const fn get_acked_inserts_cnt(&self) -> u64 {
        self.acked_inserts_cnt
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use super::*;

    /// Due to a bug in [`HeaderTable::can_evict_to`], the function would
    /// continuously subtract the size of the last entry instead of the size of
    /// each entry starting from the back.
    ///
    /// See <https://github.com/mozilla/neqo/issues/2306> for details.
    mod issue_2306 {
        use super::*;

        const VALUE: &[u8; 2] = b"42";

        /// Given two entries where the first is smaller than the second,
        /// subtracting the size of the second from the overall size twice leads
        /// to an underflow.
        #[test]
        fn can_evict_to_no_underflow() {
            let mut table = HeaderTable::new(true);
            table.set_capacity(10000).unwrap();

            table.insert(b"header1", VALUE).unwrap();
            table.insert(b"larger-header1", VALUE).unwrap();

            table.increment_acked(2).unwrap();

            assert!(table.can_evict_to(0));
        }

        /// Given two entries where only the first is acked, continuously
        /// subtracting the size of the last entry would give a false-positive
        /// on whether both entries can be evicted.
        #[test]
        fn can_evict_to_false() {
            let mut table = HeaderTable::new(true);
            table.set_capacity(10000).unwrap();

            table.insert(b"header1", VALUE).unwrap();
            table.insert(b"header2", VALUE).unwrap();

            table.increment_acked(1).unwrap();

            let first_entry_size = to_u64(table.get_dynamic_with_abs_index(0).unwrap().size());

            assert!(table.can_evict_to(first_entry_size));
            assert!(!table.can_evict_to(0));
        }
    }

    /// A peer can send an `Insert Count Increment` on the decoder stream with an
    /// increment large enough to overflow the running acknowledged-inserts count.
    /// The increment is range-checked against `base`, but only after the add, so
    /// the overflow has to be caught explicitly.
    #[test]
    fn increment_acked_overflow_is_rejected() {
        let mut table = HeaderTable::new(true);
        table.set_capacity(10000).unwrap();
        table.insert(b"header1", b"42").unwrap();
        table.insert(b"header2", b"42").unwrap();

        // Acknowledge both inserts legitimately so acked_inserts_cnt > 0.
        table.increment_acked(2).unwrap();

        // A malicious increment that would push the count past u64::MAX must be
        // reported as an error, not overflow.
        assert_eq!(table.increment_acked(u64::MAX), Err(Error::IncrementAck));
    }

    /// Duplicate and dynamic name references share the referenced entry's storage.
    #[test]
    fn referencing_an_entry_shares_its_storage() {
        let mut table = HeaderTable::new(false);
        table.set_capacity(10_000).unwrap();
        table.insert(b"name", b"value").unwrap();

        table.duplicate(0).unwrap();
        table.insert_with_name_ref(false, 0, b"other").unwrap();

        let [name_ref, duplicate, original] =
            [0, 1, 2].map(|i| table.get_dynamic_with_relative_index(i).unwrap());
        assert!(Rc::ptr_eq(&duplicate.name, &original.name));
        assert!(Rc::ptr_eq(&duplicate.value, &original.value));
        assert!(Rc::ptr_eq(&name_ref.name, &original.name));
        assert_eq!(name_ref.value(), b"other");
    }

    /// RFC 9204 Section 3.2.2: a new entry can reference an entry that is evicted to make room.
    #[test]
    fn duplicate_evicts_the_referenced_entry() {
        const NAME: &[u8] = b"name";
        const VALUE: &[u8] = b"value";
        let mut table = HeaderTable::new(false);
        let size = NAME.len() + VALUE.len() + ADDITIONAL_TABLE_ENTRY_SIZE;
        table.set_capacity(to_u64(size)).unwrap();
        table.insert(NAME, VALUE).unwrap();

        assert_eq!(table.duplicate(0).unwrap(), 1);

        let entry = table.get_dynamic(0, table.base(), false).unwrap();
        assert_eq!((entry.name(), entry.value()), (NAME, VALUE));
        assert_eq!(table.dynamic.len(), 1);
    }

    #[test]
    fn get_dynamic_max_index_returns_error_not_panic() {
        let mut table = HeaderTable::new(false);
        table.set_capacity(10_000).unwrap();
        table.insert(b"name", b"value").unwrap();

        for post in [true, false] {
            assert_eq!(
                table.get_dynamic(u64::MAX, u64::MAX, post).unwrap_err(),
                Error::IntegerOverflow
            );
        }
    }
}
