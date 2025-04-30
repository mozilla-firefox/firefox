/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::db::RelevancyDao;
use crate::rs::{
    from_json, from_json_slice, RelevancyAttachmentData, RelevancyRecord,
    RelevancyRemoteSettingsClient,
};
use crate::url_hash::UrlHash;
use crate::{Error, Interest, RelevancyDb, Result};
use base64::{engine::general_purpose::STANDARD, Engine};
use remote_settings::RemoteSettingsRecord;

// Number of rows to write when inserting interest data before checking for interruption
const WRITE_CHUNK_SIZE: usize = 100;

pub fn ensure_interest_data_populated<C: RelevancyRemoteSettingsClient>(
    db: &RelevancyDb,
    client: C,
) -> Result<()> {
    if !db.read(|dao| dao.need_to_load_url_interests())? {
        return Ok(());
    }

    match fetch_interest_data_inner(client) {
        Ok(data) => {
            db.read_write(move |dao| insert_interest_data(data, dao))?;
        }
        Err(e) => {
            log::warn!("error fetching interest data: {e}");
            return Err(Error::FetchInterestDataError);
        }
    }
    Ok(())
}

/// Fetch the interest data
fn fetch_interest_data_inner<C: RelevancyRemoteSettingsClient>(
    client: C,
) -> Result<Vec<(Interest, UrlHash)>> {
    let remote_settings_response = client.get_records()?;
    let mut result = vec![];

    for record in remote_settings_response {
        let attachment_data = client.get_attachment(&record)?;
        let interest = get_interest(&record)?;
        let urls = get_hash_urls(attachment_data)?;
        result.extend(std::iter::repeat(interest).zip(urls));
    }
    Ok(result)
}

fn get_hash_urls(attachment_data: Vec<u8>) -> Result<Vec<UrlHash>> {
    let mut hash_urls = vec![];

    let parsed_attachment_data: Vec<RelevancyAttachmentData> = from_json_slice(&attachment_data)?;

    for attachment_data in parsed_attachment_data {
        let hash_url = STANDARD
            .decode(attachment_data.domain)
            .map_err(|_| Error::Base64DecodeError("Invalid base64 error".to_string()))?;
        let url_hash = hash_url.try_into().map_err(|_| {
            Error::Base64DecodeError("Base64 string has wrong number of bytes".to_string())
        })?;
        hash_urls.push(url_hash);
    }
    Ok(hash_urls)
}

/// Extract Interest from the record info
fn get_interest(record: &RemoteSettingsRecord) -> Result<Interest> {
    let record_fields: RelevancyRecord =
        from_json(serde_json::Value::Object(record.fields.clone()))?;
    let custom_details = record_fields.record_custom_details;
    let category_code = custom_details.category_to_domains.category_code;
    Interest::try_from(category_code as u32)
}

/// Insert Interests into Db
fn insert_interest_data(data: Vec<(Interest, UrlHash)>, dao: &mut RelevancyDao) -> Result<()> {
    for chunk in data.chunks(WRITE_CHUNK_SIZE) {
        dao.err_if_interrupted()?;
        for (interest, hash_url) in chunk {
            dao.add_url_interest(*hash_url, *interest)?;
        }
    }

    Ok(())
}

#[cfg(test)]
mod test {

    use std::{cell::RefCell, collections::HashMap};

    use anyhow::Context;
    use serde_json::json;

    use super::*;
    use crate::{rs::RelevancyRemoteSettingsClient, url_hash::hash_url, InterestVector};

    /// A snapshot containing fake Remote Settings records and attachments for
    /// the store to ingest. We use snapshots to test the store's behavior in a
    /// data-driven way.
    struct Snapshot {
        records: Vec<RemoteSettingsRecord>,
        attachments: HashMap<&'static str, Vec<u8>>,
    }

    impl Snapshot {
        /// Creates a snapshot from a JSON value that represents a collection of
        /// Relevancy Remote Settings records.
        ///
        /// You can use the [`serde_json::json!`] macro to construct the JSON
        /// value, then pass it to this function. It's easier to use the
        /// `Snapshot::with_records(json!(...))` idiom than to construct the
        /// records by hand.
        fn with_records(value: serde_json::Value) -> anyhow::Result<Self> {
            Ok(Self {
                records: serde_json::from_value(value)
                    .context("Couldn't create snapshot with Remote Settings records")?,
                attachments: HashMap::new(),
            })
        }

        /// Adds a data attachment to the snapshot.
        fn with_data(
            mut self,
            location: &'static str,
            value: serde_json::Value,
        ) -> anyhow::Result<Self> {
            self.attachments.insert(
                location,
                serde_json::to_vec(&value).context("Couldn't add data attachment to snapshot")?,
            );
            Ok(self)
        }
    }

    /// A fake Remote Settings client that returns records and attachments from
    /// a snapshot.
    struct SnapshotSettingsClient {
        /// The current snapshot. You can modify it using
        /// [`RefCell::borrow_mut()`] to simulate remote updates in tests.
        snapshot: RefCell<Snapshot>,
    }

    impl SnapshotSettingsClient {
        /// Creates a client with an initial snapshot.
        fn with_snapshot(snapshot: Snapshot) -> Self {
            Self {
                snapshot: RefCell::new(snapshot),
            }
        }
    }

    impl RelevancyRemoteSettingsClient for SnapshotSettingsClient {
        fn get_records(&self) -> Result<Vec<RemoteSettingsRecord>> {
            Ok(self.snapshot.borrow().records.clone())
        }

        fn get_attachment(&self, record: &RemoteSettingsRecord) -> Result<Vec<u8>> {
            let location = record.attachment.as_ref().unwrap().location.as_str();
            Ok(self
                .snapshot
                .borrow()
                .attachments
                .get(location)
                .unwrap_or_else(|| unreachable!("Unexpected request for attachment `{}`", location))
                .clone())
        }

        fn close(&self) {}
    }

    #[test]
    fn test_interest_vectors() {
        let db = RelevancyDb::new_for_test();
        db.read_write(|dao| {
            // Test that the interest data matches the values we started from in
            // `bin/generate-test-data.rs`

            dao.add_url_interest(hash_url("https://espn.com").unwrap(), Interest::Sports)?;
            dao.add_url_interest(hash_url("https://dogs.com").unwrap(), Interest::Animals)?;
            dao.add_url_interest(hash_url("https://cars.com").unwrap(), Interest::Autos)?;
            dao.add_url_interest(
                hash_url("https://www.vouge.com").unwrap(),
                Interest::Fashion,
            )?;
            dao.add_url_interest(hash_url("https://slashdot.org").unwrap(), Interest::Tech)?;
            dao.add_url_interest(hash_url("https://www.nascar.com").unwrap(), Interest::Autos)?;
            dao.add_url_interest(
                hash_url("https://www.nascar.com").unwrap(),
                Interest::Sports,
            )?;
            dao.add_url_interest(
                hash_url("https://unknown.url").unwrap(),
                Interest::Inconclusive,
            )?;

            assert_eq!(
                dao.get_url_interest_vector("https://espn.com/").unwrap(),
                InterestVector {
                    sports: 1,
                    ..InterestVector::default()
                }
            );
            assert_eq!(
                dao.get_url_interest_vector("https://dogs.com/").unwrap(),
                InterestVector {
                    animals: 1,
                    ..InterestVector::default()
                }
            );
            assert_eq!(
                dao.get_url_interest_vector("https://cars.com/").unwrap(),
                InterestVector {
                    autos: 1,
                    ..InterestVector::default()
                }
            );
            assert_eq!(
                dao.get_url_interest_vector("https://www.vouge.com/")
                    .unwrap(),
                InterestVector {
                    fashion: 1,
                    ..InterestVector::default()
                }
            );
            assert_eq!(
                dao.get_url_interest_vector("https://slashdot.org/")
                    .unwrap(),
                InterestVector {
                    tech: 1,
                    ..InterestVector::default()
                }
            );
            assert_eq!(
                dao.get_url_interest_vector("https://www.nascar.com/")
                    .unwrap(),
                InterestVector {
                    autos: 1,
                    sports: 1,
                    ..InterestVector::default()
                }
            );
            assert_eq!(
                dao.get_url_interest_vector("https://unknown.url/").unwrap(),
                InterestVector {
                    inconclusive: 1,
                    ..InterestVector::default()
                }
            );
            Ok(())
        })
        .unwrap();
    }

    #[test]
    fn test_variations_on_the_url() {
        let db = RelevancyDb::new_for_test();
        db.read_write(|dao| {
            dao.add_url_interest(hash_url("https://espn.com").unwrap(), Interest::Sports)?;
            dao.add_url_interest(hash_url("https://nascar.com").unwrap(), Interest::Autos)?;
            dao.add_url_interest(hash_url("https://nascar.com").unwrap(), Interest::Sports)?;

            // Different paths/queries should work
            assert_eq!(
                dao.get_url_interest_vector("https://espn.com/foo/bar/?baz")
                    .unwrap(),
                InterestVector {
                    sports: 1,
                    ..InterestVector::default()
                }
            );
            // Different schemes should too
            assert_eq!(
                dao.get_url_interest_vector("http://espn.com/").unwrap(),
                InterestVector {
                    sports: 1,
                    ..InterestVector::default()
                }
            );
            // But changes to the domain shouldn't
            assert_eq!(
                dao.get_url_interest_vector("http://espn2.com/").unwrap(),
                InterestVector::default()
            );
            // However, extra components past the 2nd one in the domain are ignored
            assert_eq!(
                dao.get_url_interest_vector("https://www.nascar.com/")
                    .unwrap(),
                InterestVector {
                    autos: 1,
                    sports: 1,
                    ..InterestVector::default()
                }
            );
            Ok(())
        })
        .unwrap();
    }

    #[test]
    fn test_parse_records() -> anyhow::Result<()> {
        let snapshot = Snapshot::with_records(json!([{
            "id": "animals-0001",
            "last_modified": 15,
            "type": "category_to_domains",
            "attachment": {
                "filename": "data-1.json",
                "mimetype": "application/json",
                "location": "data-1.json",
                "hash": "",
                "size": 0
            },
            "record_custom_details": {
              "category_to_domains": {
                "category": "animals",
                "category_code": 1,
                "version": 1
              }
            }
        }]))?
        .with_data(
            "data-1.json",
            json!([
            {"domain": "J2jtyjQtYQ/+/p//xhz43Q=="},
            {"domain": "Zd4awCwGZLkat59nIWje3g=="}]),
        )?;
        let rs_client = SnapshotSettingsClient::with_snapshot(snapshot);
        assert_eq!(
            fetch_interest_data_inner(rs_client).unwrap(),
            vec![
                (Interest::Animals, hash_url("https://dogs.com").unwrap()),
                (Interest::Animals, hash_url("https://cats.com").unwrap())
            ]
        );

        Ok(())
    }

    #[test]
    fn test_parse_records_with_bad_domain_strings() -> anyhow::Result<()> {
        let snapshot = Snapshot::with_records(json!([{
            "id": "animals-0001",
            "last_modified": 15,
            "type": "category_to_domains",
            "attachment": {
                "filename": "data-1.json",
                "mimetype": "application/json",
                "location": "data-1.json",
                "hash": "",
                "size": 0
            },
            "record_custom_details": {
              "category_to_domains": {
                "category": "animals",
                "category_code": 1,
                "version": 1
              }
            }
        }]))?
        .with_data(
            "data-1.json",
            json!([
                {"domain": "badString"},
                {"domain": "notBase64"}]),
        )?;
        let rs_client = SnapshotSettingsClient::with_snapshot(snapshot);
        fetch_interest_data_inner(rs_client).expect_err("Invalid base64 error");

        Ok(())
    }
}
