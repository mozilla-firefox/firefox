/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

pub mod builder;
pub mod connection_initializer;
pub mod store;

use crate::{
    ads::PlacementId,
    ads_store::{builder::AdsStoreBuilder, store::AdsStoreHolder},
    bytesize::ByteSize,
};
use sql_support::open_database;
use std::path::Path;

pub struct AdsStore {
    holder: AdsStoreHolder,
    #[expect(
        dead_code,
        reason = "validated by the builder, but nothing trims the store to it yet"
    )]
    max_size: ByteSize,
}

impl AdsStore {
    pub fn builder<P: AsRef<Path>>(db_path: P) -> AdsStoreBuilder {
        AdsStoreBuilder::new(db_path.as_ref())
    }

    pub fn clear(&self) -> Result<(), open_database::Error> {
        self.holder.clear_all()?;
        Ok(())
    }

    pub fn invalidate_by_id(&self, placement_id: &PlacementId) -> Result<(), open_database::Error> {
        self.holder.invalidate_ad_by_id(placement_id)?;
        Ok(())
    }

    pub fn is_memory(&self) -> Result<bool, open_database::Error> {
        self.holder.is_memory()
    }

    pub fn shutdown_db(&self) {
        self.holder.close();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_utils::get_example_happy_image_ads;
    use crate::IN_MEMORY_DB_PATH;

    #[test]
    fn test_ads_store_creation() {
        // Test that AdsStore can be created successfully with test config
        let store: Result<AdsStore, _> = AdsStore::builder(IN_MEMORY_DB_PATH).build();
        assert!(store.is_ok());
    }

    #[test]
    fn test_clear_store() {
        let store: AdsStore = AdsStore::builder(IN_MEMORY_DB_PATH).build().unwrap();

        let (placement_id, ads) = get_example_happy_image_ads("mock_billboard_1");
        store.holder.store_ads(&placement_id, &ads).unwrap();

        // Verify it's cached
        let retrieved = store.holder.lookup(&placement_id).unwrap();
        assert!(retrieved.is_some());

        // Clear the cache
        store.clear().unwrap();

        // Verify it's cleared
        let retrieved_after_clear = store.holder.lookup(&placement_id).unwrap();
        assert!(retrieved_after_clear.is_none());
    }

    #[test]
    fn test_invalidate_by_id() {
        let store: AdsStore = AdsStore::builder(IN_MEMORY_DB_PATH).build().unwrap();

        let (placement_1, ads_1) = get_example_happy_image_ads("mock_billboard_1");
        let (placement_2, ads_2) = get_example_happy_image_ads("mock_billboard_2");
        store.holder.store_ads(&placement_1, &ads_1).unwrap();
        store.holder.store_ads(&placement_2, &ads_2).unwrap();

        assert!(store.holder.lookup(&placement_1).unwrap().is_some());
        assert!(store.holder.lookup(&placement_2).unwrap().is_some());

        store.invalidate_by_id(&placement_1).unwrap();

        assert!(store.holder.lookup(&placement_1).unwrap().is_none());
        assert!(store.holder.lookup(&placement_2).unwrap().is_some());
    }
}
