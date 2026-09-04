/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! A centralized set of stylesheets for a document.

use crate::derives::*;
use crate::device::Device;
use crate::invalidation::stylesheets::{RuleChangeKind, StylesheetInvalidationSet};
use crate::shared_lock::{SharedRwLockReadGuard, StylesheetGuards};
use crate::stylesheets::{
    CssRule, CssRuleRef, CustomMediaMap, Origin, OriginSet, PerOrigin, StylesheetInDocument,
};
use std::mem;

/// Entry for a StylesheetSet.
#[derive(MallocSizeOf)]
struct StylesheetSetEntry<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// The sheet.
    sheet: S,

    /// Whether this sheet has been part of at least one flush.
    committed: bool,
}

impl<S> StylesheetSetEntry<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    fn new(sheet: S) -> Self {
        Self {
            sheet,
            committed: false,
        }
    }
}

/// The validity of the data in a given cascade origin.
#[derive(Clone, Copy, Debug, Default, Eq, MallocSizeOf, Ord, PartialEq, PartialOrd)]
pub enum DataValidity {
    /// The origin is clean, all the data already there is valid, though we may
    /// have new sheets at the end.
    #[default]
    Valid = 0,

    /// The cascade data is invalid, but not the invalidation data (which is
    /// order-independent), and thus only the cascade data should be inserted.
    CascadeInvalid = 1,

    /// Everything needs to be rebuilt.
    FullyInvalid = 2,
}

/// A struct to iterate over the different stylesheets to be flushed.
pub struct DocumentStylesheetFlusher<'a, S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    collections: &'a mut PerOrigin<SheetCollection<S>>,
}

/// The type of rebuild that we need to do for a given stylesheet.
#[derive(Clone, Copy, Debug)]
pub enum SheetRebuildKind {
    /// A full rebuild, of both cascade data and invalidation data.
    Full,
    /// A partial rebuild, of only the cascade data.
    CascadeOnly,
}

impl SheetRebuildKind {
    /// Whether the stylesheet invalidation data should be rebuilt.
    pub fn should_rebuild_invalidation(&self) -> bool {
        matches!(*self, SheetRebuildKind::Full)
    }
}

impl<'a, S> DocumentStylesheetFlusher<'a, S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// Returns a flusher for `origin`.
    pub fn flush_origin(&mut self, origin: Origin) -> SheetCollectionFlusher<'_, S> {
        self.collections.borrow_mut_for_origin(&origin).flush()
    }

    /// Returns the list of stylesheets for `origin`.
    ///
    /// Only used for UA sheets.
    pub fn origin_sheets(&self, origin: Origin) -> impl Iterator<Item = &S> {
        self.collections.borrow_for_origin(&origin).iter()
    }
}

/// A flusher struct for a given collection, that takes care of returning the
/// appropriate stylesheets that need work.
pub struct SheetCollectionFlusher<'a, S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    // TODO: This can be made an iterator again once
    // https://github.com/rust-lang/rust/pull/82771 lands on stable.
    entries: &'a mut [StylesheetSetEntry<S>],
    validity: DataValidity,
    dirty: bool,
}

impl<'a, S> SheetCollectionFlusher<'a, S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// Whether the collection was originally dirty.
    #[inline]
    pub fn dirty(&self) -> bool {
        self.dirty
    }

    /// What the state of the sheet data is.
    #[inline]
    pub fn data_validity(&self) -> DataValidity {
        self.validity
    }

    /// Returns an iterator over the remaining list of sheets to consume.
    pub fn sheets(&self) -> impl Iterator<Item = &S> {
        self.entries.iter().map(|entry| &entry.sheet)
    }
}

impl<'a, S> SheetCollectionFlusher<'a, S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// Iterates over all sheets and values that we have to invalidate.
    ///
    /// TODO(emilio): This would be nicer as an iterator but we can't do that
    /// until https://github.com/rust-lang/rust/pull/82771 stabilizes.
    ///
    /// Since we don't have a good use-case for partial iteration, this does the
    /// trick for now.
    pub fn each(self, mut callback: impl FnMut(usize, &S, SheetRebuildKind) -> bool) {
        for (index, potential_sheet) in self.entries.iter_mut().enumerate() {
            let committed = mem::replace(&mut potential_sheet.committed, true);
            let rebuild_kind = if !committed {
                // If the sheet was uncommitted, we need to do a full rebuild
                // anyway.
                SheetRebuildKind::Full
            } else {
                match self.validity {
                    DataValidity::Valid => continue,
                    DataValidity::CascadeInvalid => SheetRebuildKind::CascadeOnly,
                    DataValidity::FullyInvalid => SheetRebuildKind::Full,
                }
            };

            if !callback(index, &potential_sheet.sheet, rebuild_kind) {
                return;
            }
        }
    }
}

#[derive(MallocSizeOf)]
struct SheetCollection<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// The actual list of stylesheets.
    ///
    /// This is only a list of top-level stylesheets, and as such it doesn't
    /// include recursive `@import` rules.
    entries: Vec<StylesheetSetEntry<S>>,

    /// The validity of the data that was already there for a given origin.
    ///
    /// Note that an origin may appear on `origins_dirty`, but still have
    /// `DataValidity::Valid`, if only sheets have been appended into it (in
    /// which case the existing data is valid, but the origin needs to be
    /// rebuilt).
    data_validity: DataValidity,

    /// Whether anything in the collection has changed. Note that this is
    /// different from `data_validity`, in the sense that after a sheet append,
    /// the data validity is still `Valid`, but we need to be marked as dirty.
    dirty: bool,
}

impl<S> Default for SheetCollection<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    fn default() -> Self {
        Self {
            entries: vec![],
            data_validity: DataValidity::Valid,
            dirty: false,
        }
    }
}

impl<S> SheetCollection<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// Returns the number of stylesheets in the set.
    fn len(&self) -> usize {
        self.entries.len()
    }

    /// Returns the `index`th stylesheet in the set if present.
    fn get(&self, index: usize) -> Option<&S> {
        self.entries.get(index).map(|e| &e.sheet)
    }

    fn find_sheet_index(&self, sheet: &S) -> Option<usize> {
        let rev_pos = self
            .entries
            .iter()
            .rev()
            .position(|entry| entry.sheet == *sheet);
        rev_pos.map(|i| self.entries.len() - i - 1)
    }

    /// Removes a sheet from the collection, returning whether the sheet had
    /// been committed (that is, whether it has been part of a flush).
    fn remove(&mut self, sheet: &S) -> bool {
        let index = self.find_sheet_index(sheet);
        if cfg!(feature = "gecko") && index.is_none() {
            // FIXME(emilio): Make Gecko's PresShell::AddUserSheet not suck.
            return false;
        }
        let sheet = self.entries.remove(index.unwrap());
        // Removing sheets makes us tear down the whole cascade and invalidation
        // data, but only if the sheet has been involved in at least one flush.
        // Checking whether the sheet has been committed allows us to avoid
        // rebuilding the world when sites quickly append and remove a
        // stylesheet.
        //
        // See bug 1434756.
        if sheet.committed {
            self.set_data_validity_at_least(DataValidity::FullyInvalid);
        } else {
            self.dirty = true;
        }
        sheet.committed
    }

    fn contains(&self, sheet: &S) -> bool {
        self.entries.iter().any(|e| e.sheet == *sheet)
    }

    /// Appends a given sheet into the collection.
    fn append(&mut self, sheet: S) {
        debug_assert!(!self.contains(&sheet));
        self.entries.push(StylesheetSetEntry::new(sheet));
        // Appending sheets doesn't alter the validity of the existing data, so
        // we don't need to change `data_validity` here.
        //
        // But we need to be marked as dirty, otherwise we'll never add the new
        // sheet!
        self.dirty = true;
    }

    fn insert_before(&mut self, sheet: S, before_sheet: &S) {
        debug_assert!(!self.contains(&sheet));

        let index = self
            .find_sheet_index(before_sheet)
            .expect("`before_sheet` stylesheet not found");

        // Inserting stylesheets somewhere but at the end changes the validity
        // of the cascade data, but not the invalidation data.
        self.set_data_validity_at_least(DataValidity::CascadeInvalid);
        self.entries.insert(index, StylesheetSetEntry::new(sheet));
    }

    fn set_data_validity_at_least(&mut self, validity: DataValidity) {
        use std::cmp;

        debug_assert_ne!(validity, DataValidity::Valid);

        self.dirty = true;
        self.data_validity = cmp::max(validity, self.data_validity);
    }

    /// Returns an iterator over the current list of stylesheets.
    fn iter(&self) -> impl Iterator<Item = &S> {
        self.entries.iter().map(|e| &e.sheet)
    }

    /// Returns a mutable iterator over the current list of stylesheets.
    fn iter_mut(&mut self) -> impl Iterator<Item = &mut S> {
        self.entries.iter_mut().map(|e| &mut e.sheet)
    }

    /// Collects invalidations for the sheets that have been added (i.e. sheets not yet committed)
    /// since the last flush.
    fn collect_pending_invalidations(
        &self,
        invalidations: &mut StylesheetInvalidationSet,
        device: &Device,
        custom_media: &CustomMediaMap,
        guard: &SharedRwLockReadGuard,
    ) {
        if !self.dirty {
            return;
        }
        for entry in &self.entries {
            if entry.committed {
                continue;
            }
            invalidations.collect_invalidations_for(device, custom_media, &entry.sheet, guard);
        }
    }

    fn flush(&mut self) -> SheetCollectionFlusher<'_, S> {
        let dirty = mem::replace(&mut self.dirty, false);
        let validity = mem::replace(&mut self.data_validity, DataValidity::Valid);

        SheetCollectionFlusher {
            entries: &mut self.entries,
            dirty,
            validity,
        }
    }
}

/// The set of stylesheets effective for a given document.
#[derive(MallocSizeOf)]
pub struct DocumentStylesheetSet<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// The collections of sheets per each origin.
    collections: PerOrigin<SheetCollection<S>>,

    /// The invalidations for stylesheets added or removed from this document.
    invalidations: StylesheetInvalidationSet,
}

/// This macro defines methods common to DocumentStylesheetSet and
/// AuthorStylesheetSet.
///
/// We could simplify the setup moving invalidations to SheetCollection, but
/// that would imply not sharing invalidations across origins of the same
/// documents, which is slightly annoying.
macro_rules! sheet_set_methods {
    ($set_name:expr) => {
        fn collect_invalidations_for(
            &mut self,
            device: Option<&Device>,
            custom_media: &CustomMediaMap,
            sheet: &S,
            guard: &SharedRwLockReadGuard,
        ) {
            if let Some(device) = device {
                self.invalidations
                    .collect_invalidations_for(device, custom_media, sheet, guard);
            }
        }

        /// Appends a new stylesheet to the current set.
        ///
        /// Invalidations for the new sheet are collected when flushing.
        pub fn append_stylesheet(&mut self, sheet: S, guard: &SharedRwLockReadGuard) {
            debug!(concat!($set_name, "::append_stylesheet"));
            let collection = self.collection_for(&sheet, guard);
            collection.append(sheet);
        }

        /// Insert a given stylesheet before another stylesheet in the document.
        ///
        /// Invalidations for the new sheet are collected when flushing.
        pub fn insert_stylesheet_before(
            &mut self,
            sheet: S,
            before_sheet: S,
            guard: &SharedRwLockReadGuard,
        ) {
            debug!(concat!($set_name, "::insert_stylesheet_before"));
            let collection = self.collection_for(&sheet, guard);
            collection.insert_before(sheet, &before_sheet);
        }

        /// Remove a given stylesheet from the set.
        ///
        /// No device implies not computing invalidations.
        pub fn remove_stylesheet(
            &mut self,
            device: Option<&Device>,
            custom_media: &CustomMediaMap,
            sheet: S,
            guard: &SharedRwLockReadGuard,
        ) {
            debug!(concat!($set_name, "::remove_stylesheet"));
            let collection = self.collection_for(&sheet, guard);
            // If the sheet never made it to a flush, it never affected the document styles, so
            // nothing to invalidate.
            if collection.remove(&sheet) {
                self.collect_invalidations_for(device, custom_media, &sheet, guard);
            }
        }

        /// Notify the set that a rule from a given stylesheet has changed
        /// somehow.
        pub fn rule_changed(
            &mut self,
            device: Option<&Device>,
            custom_media: &CustomMediaMap,
            sheet: &S,
            rule: &CssRule,
            guard: &SharedRwLockReadGuard,
            change_kind: RuleChangeKind,
            ancestors: &[CssRuleRef],
        ) {
            if let Some(device) = device {
                let quirks_mode = device.quirks_mode();
                self.invalidations.rule_changed(
                    sheet,
                    rule,
                    guard,
                    device,
                    quirks_mode,
                    custom_media,
                    change_kind,
                    ancestors,
                );
            }

            let validity = match change_kind {
                // Insertion / Removals need to rebuild both the cascade and
                // invalidation data. For generic changes this is conservative,
                // could be optimized on a per-case basis.
                RuleChangeKind::Generic | RuleChangeKind::Insertion | RuleChangeKind::Removal => {
                    DataValidity::FullyInvalid
                },
                // TODO(emilio): This, in theory, doesn't need to invalidate
                // style data, if the rule we're modifying is actually in the
                // CascadeData already.
                //
                // But this is actually a bit tricky to prove, because when we
                // copy-on-write a stylesheet we don't bother doing a rebuild,
                // so we may still have rules from the original stylesheet
                // instead of the cloned one that we're modifying. So don't
                // bother for now and unconditionally rebuild, it's no worse
                // than what we were already doing anyway.
                //
                // Maybe we could record whether we saw a clone in this flush,
                // and if so do the conservative thing, otherwise just
                // early-return.
                RuleChangeKind::PositionTryDeclarations | RuleChangeKind::StyleRuleDeclarations => {
                    DataValidity::FullyInvalid
                },
            };

            let collection = self.collection_for(&sheet, guard);
            collection.set_data_validity_at_least(validity);
        }
    };
}

impl<S> Default for DocumentStylesheetSet<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<S> DocumentStylesheetSet<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// Create a new empty DocumentStylesheetSet.
    pub fn new() -> Self {
        Self {
            collections: Default::default(),
            invalidations: StylesheetInvalidationSet::new(),
        }
    }

    fn collection_for(
        &mut self,
        sheet: &S,
        guard: &SharedRwLockReadGuard,
    ) -> &mut SheetCollection<S> {
        let origin = sheet.contents(guard).origin;
        self.collections.borrow_mut_for_origin(&origin)
    }

    sheet_set_methods!("DocumentStylesheetSet");

    /// Returns the number of stylesheets in the set.
    pub fn len(&self) -> usize {
        self.collections
            .iter_origins()
            .fold(0, |s, (item, _)| s + item.len())
    }

    /// Returns the count of stylesheets for a given origin.
    #[inline]
    pub fn sheet_count(&self, origin: Origin) -> usize {
        self.collections.borrow_for_origin(&origin).len()
    }

    /// Returns the `index`th stylesheet in the set for the given origin.
    #[inline]
    pub fn get(&self, origin: Origin, index: usize) -> Option<&S> {
        self.collections.borrow_for_origin(&origin).get(index)
    }

    /// Returns whether the given set has changed from the last flush.
    pub fn has_changed(&self) -> bool {
        !self.invalidations.is_empty()
            || self
                .collections
                .iter_origins()
                .any(|(collection, _)| collection.dirty)
    }

    /// Flush the current set, unmarking it as dirty, and returns a `DocumentStylesheetFlusher` in
    /// order to rebuild the stylist and the invalidation set.
    ///
    /// `custom_media_for_origin` is used to compute the invalidations for the sheets added since
    /// the last flush.
    pub fn flush<'a>(
        &mut self,
        device: &Device,
        guards: &StylesheetGuards,
        custom_media_for_origin: impl Fn(Origin) -> &'a CustomMediaMap,
    ) -> (DocumentStylesheetFlusher<'_, S>, StylesheetInvalidationSet) {
        debug!("DocumentStylesheetSet::flush");
        for (collection, origin) in self.collections.iter_origins() {
            collection.collect_pending_invalidations(
                &mut self.invalidations,
                device,
                custom_media_for_origin(origin),
                guards.for_origin(origin),
            );
        }
        (
            DocumentStylesheetFlusher {
                collections: &mut self.collections,
            },
            std::mem::take(&mut self.invalidations),
        )
    }

    /// Flush stylesheets, but without running any of the invalidation passes.
    #[cfg(feature = "servo")]
    pub fn flush_without_invalidation(&mut self) -> OriginSet {
        debug!("DocumentStylesheetSet::flush_without_invalidation");

        let mut origins = OriginSet::empty();
        std::mem::take(&mut self.invalidations);

        for (collection, origin) in self.collections.iter_mut_origins() {
            if collection.flush().dirty() {
                origins |= origin;
            }
        }

        origins
    }

    /// Return an iterator over the flattened view of all the stylesheets.
    pub fn iter(&self) -> impl Iterator<Item = (&S, Origin)> {
        self.collections
            .iter_origins()
            .flat_map(|(c, o)| c.iter().map(move |s| (s, o)))
    }

    /// Return an iterator over the flattened view of all the stylesheets, mutably.
    pub fn iter_mut(&mut self) -> impl Iterator<Item = (&mut S, Origin)> {
        self.collections
            .iter_mut_origins()
            .flat_map(|(c, o)| c.iter_mut().map(move |s| (s, o)))
    }

    /// Mark the stylesheets for the specified origin as dirty, because
    /// something external may have invalidated it.
    pub fn force_dirty(&mut self, origins: OriginSet) {
        self.invalidations.invalidate_fully();
        for origin in origins.iter_origins() {
            // We don't know what happened, assume the worse.
            self.collections
                .borrow_mut_for_origin(&origin)
                .set_data_validity_at_least(DataValidity::FullyInvalid);
        }
    }
}

/// The set of stylesheets effective for a given Shadow Root.
#[derive(MallocSizeOf)]
pub struct AuthorStylesheetSet<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// The actual style sheets.
    collection: SheetCollection<S>,
    /// The set of invalidations scheduled for this collection.
    invalidations: StylesheetInvalidationSet,
}

/// A struct to flush an author style sheet collection.
pub struct AuthorStylesheetFlusher<'a, S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// The actual flusher for the collection.
    pub sheets: SheetCollectionFlusher<'a, S>,
}

impl<S> Default for AuthorStylesheetSet<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<S> AuthorStylesheetSet<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// Create a new empty AuthorStylesheetSet.
    #[inline]
    pub fn new() -> Self {
        Self {
            collection: Default::default(),
            invalidations: StylesheetInvalidationSet::new(),
        }
    }

    /// Whether anything has changed since the last time this was flushed.
    pub fn dirty(&self) -> bool {
        self.collection.dirty
    }

    /// Whether the collection is empty.
    pub fn is_empty(&self) -> bool {
        self.collection.len() == 0
    }

    /// Returns the `index`th stylesheet in the collection of author styles if present.
    pub fn get(&self, index: usize) -> Option<&S> {
        self.collection.get(index)
    }

    /// Returns the number of author stylesheets.
    pub fn len(&self) -> usize {
        self.collection.len()
    }

    fn collection_for(&mut self, _: &S, _: &SharedRwLockReadGuard) -> &mut SheetCollection<S> {
        &mut self.collection
    }

    sheet_set_methods!("AuthorStylesheetSet");

    /// Iterate over the list of stylesheets.
    pub fn iter(&self) -> impl Iterator<Item = &S> {
        self.collection.iter()
    }

    /// Returns a mutable iterator over the current list of stylesheets.
    pub fn iter_mut(&mut self) -> impl Iterator<Item = &mut S> {
        self.collection.iter_mut()
    }

    /// Mark the sheet set dirty, as appropriate.
    pub fn force_dirty(&mut self) {
        self.invalidations.invalidate_fully();
        self.collection
            .set_data_validity_at_least(DataValidity::FullyInvalid);
    }

    /// Flush the stylesheets for this author set.
    ///
    /// No device implies not computing invalidations.
    pub fn flush(
        &mut self,
        device: Option<&Device>,
        custom_media: &CustomMediaMap,
        guard: &SharedRwLockReadGuard,
    ) -> (AuthorStylesheetFlusher<'_, S>, StylesheetInvalidationSet) {
        if let Some(device) = device {
            self.collection.collect_pending_invalidations(
                &mut self.invalidations,
                device,
                custom_media,
                guard,
            );
        }
        (
            AuthorStylesheetFlusher {
                sheets: self.collection.flush(),
            },
            std::mem::take(&mut self.invalidations),
        )
    }
}
