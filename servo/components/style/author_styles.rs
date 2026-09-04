/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! A set of author stylesheets and their computed representation, such as the
//! ones used for ShadowRoot.

use crate::derives::*;
use crate::invalidation::stylesheets::StylesheetInvalidationSet;
use crate::shared_lock::SharedRwLockReadGuard;
use crate::stylesheet_set::AuthorStylesheetSet;
use crate::stylesheets::StylesheetInDocument;
use crate::stylist::CascadeData;
use crate::stylist::Stylist;
use servo_arc::Arc;
use std::sync::LazyLock;

/// A set of author stylesheets and their computed representation, such as the
/// ones used for ShadowRoot.
#[derive(MallocSizeOf)]
pub struct GenericAuthorStyles<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// The sheet collection, which holds the sheet pointers, the invalidations,
    /// and all that stuff.
    pub stylesheets: AuthorStylesheetSet<S>,
    /// The actual cascade data computed from the stylesheets.
    #[ignore_malloc_size_of = "Measured as part of the stylist"]
    pub data: Arc<CascadeData>,
}

pub use self::GenericAuthorStyles as AuthorStyles;

static EMPTY_CASCADE_DATA: LazyLock<Arc<CascadeData>> =
    LazyLock::new(|| Arc::new_leaked(CascadeData::new()));

impl<S> Default for GenericAuthorStyles<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    fn default() -> Self {
        Self::new()
    }
}

impl<S> GenericAuthorStyles<S>
where
    S: StylesheetInDocument + PartialEq + 'static,
{
    /// Create an empty AuthorStyles.
    #[inline]
    pub fn new() -> Self {
        Self {
            stylesheets: AuthorStylesheetSet::new(),
            data: EMPTY_CASCADE_DATA.clone(),
        }
    }

    /// Flush the pending sheet changes, updating `data` as appropriate.
    #[inline]
    pub fn flush(
        &mut self,
        stylist: &mut Stylist,
        guard: &SharedRwLockReadGuard,
    ) -> StylesheetInvalidationSet {
        // TODO(emilio): We don't collect invalidations for shadow trees, see
        // Servo_StyleSet_FlushStyleSheets.
        let (flusher, mut invalidations) =
            self.stylesheets
                .flush(None, self.data.custom_media_map(), guard);
        let result = stylist.rebuild_author_data(
            &self.data,
            flusher.sheets,
            guard,
            &mut invalidations.cascade_data_difference,
        );
        if let Ok(Some(new_data)) = result {
            self.data = new_data;
        }
        invalidations
    }
}
