/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::error::*;
use indicatif::{MultiProgress, ProgressBar, ProgressStyle};
use remote_settings::RemoteSettingsServer;
use serde::de::Error;
use serde::{Deserialize, Serialize};
use serde_json::Value;
use sha2::{Digest, Sha256};
use std::collections::HashMap;
use std::{path::PathBuf, sync::Arc};
use url::Url;
use viaduct::{Request, Response};
use walkdir::WalkDir;

const DUMPS_DIR: &str = "dumps";

pub struct CollectionDownloader {
    multi_progress: Arc<MultiProgress>,
    output_dir: PathBuf,
    url: Url,
}

#[derive(Clone)]
pub struct CollectionUpdate {
    collection_key: String,
    attachments_updated: usize,
}

#[derive(Deserialize, Serialize)]
pub struct CollectionData {
    data: Vec<Value>,
    timestamp: u64,
}

pub struct UpdateResult {
    updated: Vec<String>,
    up_to_date: Vec<String>,
    not_found: Vec<String>,
}

#[derive(Debug, Deserialize, Serialize)]
pub struct AttachmentMetadata {
    pub location: String,
    pub hash: String,
    pub size: u64,
}

#[derive(Debug, Deserialize)]
struct ServerInfo {
    capabilities: Capabilities,
}

#[derive(Debug, Deserialize)]
struct Capabilities {
    attachments: AttachmentsCapability,
}

#[derive(Debug, Deserialize)]
struct AttachmentsCapability {
    base_url: String,
}

// fn sort_search_config_collection() {

// }

impl CollectionDownloader {
    pub fn new(root_path: PathBuf) -> Self {
        let url = RemoteSettingsServer::Prod
            .get_url()
            .expect("Cannot set RemoteSettingsServer url");

        let output_dir = if root_path.ends_with("components/remote_settings") {
            root_path
        } else {
            root_path.join("components").join("remote_settings")
        };

        Self {
            multi_progress: Arc::new(MultiProgress::new()),
            output_dir,
            url,
        }
    }

    pub fn run(&self, dry_run: bool) -> Result<()> {
        let result = self.download_all(dry_run)?;

        if dry_run {
            println!("\nDry run summary:");
            println!("- Would update {} collections", result.updated.len());
            println!(
                "- {} collections already up to date",
                result.up_to_date.len()
            );
            println!(
                "- {} collections not found on remote",
                result.not_found.len()
            );
            return Ok(());
        }

        println!("\nExecution summary:");
        if !result.updated.is_empty() {
            println!("Updated collections:");
            for collection in &result.updated {
                println!("  - {}", collection);
            }
        }

        if !result.up_to_date.is_empty() {
            println!("Collections already up to date:");
            for collection in &result.up_to_date {
                println!("  - {}", collection);
            }
        }

        if !result.not_found.is_empty() {
            println!("Collections not found on remote:");
            for collection in &result.not_found {
                println!("  - {}", collection);
            }
        }

        Ok(())
    }

    fn scan_local_dumps(&self) -> Result<HashMap<String, (String, u64)>> {
        let mut collections = HashMap::new();
        let dumps_dir = self.output_dir.join(DUMPS_DIR);

        for entry in WalkDir::new(dumps_dir).min_depth(2).max_depth(2) {
            let entry = entry?;
            if entry.file_type().is_file()
                && entry.path().extension().is_some_and(|ext| ext == "json")
            {
                // Get bucket name from parent directory
                let bucket = entry
                    .path()
                    .parent()
                    .and_then(|p| p.file_name())
                    .and_then(|n| n.to_str())
                    .ok_or_else(|| RemoteSettingsError::Path("Invalid bucket path".into()))?;

                // Get collection name from filename
                let collection_name = entry
                    .path()
                    .file_stem()
                    .and_then(|n| n.to_str())
                    .ok_or_else(|| RemoteSettingsError::Path("Invalid collection name".into()))?;

                // Read and parse the file to get timestamp
                let content = std::fs::read_to_string(entry.path())?;
                let data: serde_json::Value = serde_json::from_str(&content)?;
                let timestamp = data["timestamp"].as_u64().ok_or_else(|| {
                    RemoteSettingsError::Json(serde_json::Error::custom("No timestamp found"))
                })?;

                collections.insert(
                    format!("{}/{}", bucket, collection_name),
                    (bucket.to_string(), timestamp),
                );
            }
        }
        Ok(collections)
    }

    fn fetch_timestamps(&self) -> Result<HashMap<String, u64>> {
        let monitor_url = format!("{}/buckets/monitor/collections/changes/records", self.url);
        let monitor_response: Value = self.get(&monitor_url)?.json()?;

        Ok(monitor_response["data"]
            .as_array()
            .ok_or_else(|| {
                RemoteSettingsError::Json(serde_json::Error::custom(
                    "No data array in monitor response",
                ))
            })?
            .iter()
            .filter_map(|record| {
                let bucket = record["bucket"].as_str()?;
                let collection_name = record["collection"].as_str()?;
                Some((
                    format!("{}/{}", bucket, collection_name),
                    record["last_modified"].as_u64()?,
                ))
            })
            .collect())
    }

    fn fetch_collection(
        &self,
        collection_name: String,
        last_modified: u64,
        pb: Arc<ProgressBar>,
    ) -> Result<(String, CollectionData)> {
        let parts: Vec<&str> = collection_name.split('/').collect();
        if parts.len() != 2 {
            return Err(RemoteSettingsError::Json(serde_json::Error::custom(
                "Invalid collection name format",
            ))
            .into());
        }
        let (bucket, name) = (parts[0], parts[1]);

        let url = format!(
            "{}/buckets/{}/collections/{}/changeset?_expected={}",
            self.url, bucket, name, last_modified
        );

        pb.set_message(format!("Downloading {}", name));

        let response = self.get(&url)?;
        let changeset: Value = response.json()?;

        let timestamp = changeset["timestamp"].as_u64().ok_or_else(|| {
            RemoteSettingsError::Json(serde_json::Error::custom("No timestamp in changeset"))
        })?;

        pb.finish_with_message(format!("Downloaded {}", name));

        Ok((
            collection_name,
            CollectionData {
                data: changeset["changes"]
                    .as_array()
                    .unwrap_or(&Vec::new())
                    .to_vec(),
                timestamp,
            },
        ))
    }

    fn get_attachments_base_url(&self) -> Result<String> {
        let server_info: ServerInfo = self.get(self.url.as_str())?.json()?;
        Ok(server_info.capabilities.attachments.base_url)
    }

    fn download_attachment(
        &self,
        base_url: &str,
        record_id: &str,
        attachment: &AttachmentMetadata,
        pb: &ProgressBar,
    ) -> Result<Vec<u8>> {
        let url = format!("{}{}", base_url, attachment.location);
        pb.set_message(format!("Downloading attachment for record {}", record_id));

        let response = self.get(&url)?;
        let data = response.body;

        // Verify size
        if data.len() as u64 != attachment.size {
            return Err(RemoteSettingsError::Attachment(format!(
                "Size mismatch for attachment {}: expected {}, got {}",
                record_id,
                attachment.size,
                data.len()
            ))
            .into());
        }

        // Verify hash
        let mut hasher = Sha256::new();
        hasher.update(&data);
        let hash = format!("{:x}", hasher.finalize());
        if hash != attachment.hash {
            return Err(RemoteSettingsError::Attachment(format!(
                "Hash mismatch for attachment {}: expected {}, got {}",
                record_id, attachment.hash, hash
            ))
            .into());
        }

        pb.set_message(format!("Verified attachment for record {}", record_id));
        Ok(data)
    }

    fn get_attachment_paths(
        &self,
        bucket: &str,
        collection: &str,
        record_id: &str,
    ) -> (PathBuf, PathBuf) {
        let base_path = self
            .output_dir
            .join(DUMPS_DIR)
            .join(bucket)
            .join("attachments")
            .join(collection);

        (
            base_path.join(record_id),
            base_path.join(format!("{}.meta.json", record_id)),
        )
    }

    fn is_attachment_up_to_date(
        &self,
        bucket: &str,
        collection: &str,
        record_id: &str,
        remote_attachment: &AttachmentMetadata,
    ) -> Result<bool> {
        let (bin_path, meta_path) = self.get_attachment_paths(bucket, collection, record_id);

        // If either file doesn't exist, attachment needs update
        if !bin_path.exists() || !meta_path.exists() {
            log::debug!(
                "Attachment files missing for {}/{}/{}",
                bucket,
                collection,
                record_id
            );
            return Ok(false);
        }

        // Read and parse metadata file
        let meta_content = std::fs::read_to_string(&meta_path)?;
        let local_attachment: AttachmentMetadata = serde_json::from_str(&meta_content)?;

        // Compare metadata
        if local_attachment.hash != remote_attachment.hash
            || local_attachment.size != remote_attachment.size
        {
            log::debug!(
                "Attachment metadata mismatch for {}/{}/{}: local hash={}, size={}, remote hash={}, size={}",
                bucket, collection, record_id,
                local_attachment.hash, local_attachment.size,
                remote_attachment.hash, remote_attachment.size
            );
            return Ok(false);
        }

        Ok(true)
    }

    fn download_attachments_bundle(
        &self,
        bucket: &str,
        collection: &str,
        pb: &ProgressBar,
    ) -> Result<()> {
        let base_url = self.get_attachments_base_url()?;
        let url = format!("{}bundles/{}--{}.zip", base_url, bucket, collection);

        pb.set_message(format!(
            "Downloading attachments bundle for {}/{}",
            bucket, collection
        ));

        // Try to download the bundle
        match self.get(&url) {
            Ok(response) => {
                if response.status == 200 {
                    let bytes = response.body;
                    let bundle_path = self
                        .output_dir
                        .join(DUMPS_DIR)
                        .join(bucket)
                        .join("attachments")
                        .join(collection)
                        .with_extension("zip");

                    std::fs::create_dir_all(bundle_path.parent().unwrap())?;
                    std::fs::write(&bundle_path, bytes)?;

                    // Extract bundle
                    let file = std::fs::File::open(&bundle_path)?;
                    let mut archive = zip::ZipArchive::new(file)?;

                    let extract_path = bundle_path.parent().unwrap();
                    archive.extract(extract_path)?;

                    // Clean up zip file
                    std::fs::remove_file(bundle_path)?;

                    pb.finish_with_message(format!(
                        "Downloaded and extracted attachments bundle for {}/{}",
                        bucket, collection
                    ));
                    return Ok(());
                }
            }
            Err(e) => {
                log::debug!("Failed to download or extract attachments bundle: {}", e);
            }
        }

        Ok(())
    }

    fn process_collection_update(
        &self,
        collection: String,
        data: &mut CollectionData,
        dry_run: bool,
    ) -> Result<CollectionUpdate> {
        let mut attachments_updated = 0;
        let parts: Vec<&str> = collection.split('/').collect();

        if parts.len() != 2 {
            return Err(RemoteSettingsError::Path("Invalid collection path".into()).into());
        }

        let (bucket, name) = (parts[0], parts[1]);

        if !dry_run {
            // Write collection data
            let dumps_path = self
                .output_dir
                .join(DUMPS_DIR)
                .join(bucket)
                .join(format!("{}.json", name));

            std::fs::create_dir_all(dumps_path.parent().unwrap())?;
            // We sort both the keys and the records in search-config-v2 to make it
            // easier to read and to experiment with making changes via the dump file.
            if name == "search-config-v2" {
                data.data.sort_by(|a, b| {
                    if a["recordType"] == b["recordType"] {
                        a["identifier"].as_str().cmp(&b["identifier"].as_str())
                    } else {
                        a["recordType"].as_str().cmp(&b["recordType"].as_str())
                    }
                });
            } else {
                data.data.sort_by_key(|r| r["id"].to_string());
            }
            std::fs::write(&dumps_path, serde_json::to_string_pretty(&data)?)?;

            // Write timestamp file for fast timestamp checking without JSON parsing
            let timestamp_path = dumps_path.with_extension("timestamp");
            std::fs::write(&timestamp_path, data.timestamp.to_string())?;

            // Count attachments needing updates
            for record in &data.data {
                if let Some(attachment) = record.get("attachment") {
                    let record_id = record["id"].as_str().ok_or_else(|| {
                        RemoteSettingsError::Json(serde_json::Error::custom("No record id"))
                    })?;

                    let attachment: AttachmentMetadata =
                        serde_json::from_value(attachment.clone())?;
                    if !self.is_attachment_up_to_date(bucket, name, record_id, &attachment)? {
                        attachments_updated += 1;
                    }
                }
            }

            if attachments_updated > 0 {
                let pb = Arc::new(self.multi_progress.add(ProgressBar::new(100)));
                pb.set_style(
                    ProgressStyle::default_bar()
                        .template("[{elapsed_precise}] {bar:40.cyan/blue} {msg}")
                        .unwrap(),
                );

                self.process_attachments(bucket, name, &data.data, &pb)?;
            }
        }

        Ok(CollectionUpdate {
            collection_key: collection,
            attachments_updated,
        })
    }

    pub fn download_all(&self, dry_run: bool) -> Result<UpdateResult> {
        std::fs::create_dir_all(self.output_dir.join(DUMPS_DIR))?;

        let local_collections = self.scan_local_dumps()?;
        if local_collections.is_empty() {
            println!(
                "No local collections found in {:?}",
                self.output_dir.join(DUMPS_DIR)
            );
            return Ok(UpdateResult {
                updated: vec![],
                up_to_date: vec![],
                not_found: vec![],
            });
        }

        let remote_timestamps = self.fetch_timestamps()?;
        let mut updates_needed = Vec::new();
        let mut up_to_date = Vec::new();
        let mut not_found = Vec::new();

        // First pass: check what needs updating
        for (collection_key, (_, local_timestamp)) in local_collections {
            let remote_timestamp = match remote_timestamps.get(&collection_key) {
                Some(&timestamp) => timestamp,
                None => {
                    println!("Warning: Collection {} not found on remote", collection_key);
                    not_found.push(collection_key);
                    continue;
                }
            };

            if local_timestamp >= remote_timestamp {
                println!("Collection {} is up to date", collection_key);
                up_to_date.push(collection_key);
                continue;
            }

            println!("Collection {} needs update", collection_key);
            updates_needed.push((collection_key, remote_timestamp));
        }

        // If it's a dry run, return early with what would be updated
        if dry_run {
            return Ok(UpdateResult {
                updated: updates_needed.into_iter().map(|(key, _)| key).collect(),
                up_to_date,
                not_found,
            });
        }

        // Actually perform the updates
        let mut updates = Vec::new();
        let mut updated = Vec::new();

        for (collection_key, remote_timestamp) in updates_needed {
            let pb = Arc::new(self.multi_progress.add(ProgressBar::new(100)));
            pb.set_style(
                ProgressStyle::default_bar()
                    .template("[{elapsed_precise}] {bar:40.cyan/blue} {msg}")
                    .unwrap(),
            );

            let pb_clone = Arc::clone(&pb);
            let (collection, mut data) =
                self.fetch_collection(collection_key, remote_timestamp, pb_clone)?;
            let update = self.process_collection_update(collection, &mut data, dry_run)?;
            updates.push(update.clone());
            updated.push(update.collection_key.clone());
        }

        Ok(UpdateResult {
            updated,
            up_to_date,
            not_found,
        })
    }

    pub fn download_single(&self, bucket: &str, collection_name: &str) -> Result<()> {
        std::fs::create_dir_all(self.output_dir.join(DUMPS_DIR))?;

        let collection_key = format!("{}/{}", bucket, collection_name);
        let pb = Arc::new(self.multi_progress.add(ProgressBar::new(100)));
        pb.set_style(
            ProgressStyle::default_bar()
                .template("[{elapsed_precise}] {bar:40.cyan/blue} {msg}")
                .unwrap(),
        );

        let (collection, mut data) = self.fetch_collection(collection_key.clone(), 0, pb)?;
        let update = self.process_collection_update(collection, &mut data, false)?;

        println!(
            "Successfully downloaded collection to {:?}/dumps/{}/{}.json",
            self.output_dir, bucket, collection_name
        );

        if update.attachments_updated > 0 {
            println!("Updated {} attachments", update.attachments_updated);
        }

        Ok(())
    }

    fn process_attachments(
        &self,
        bucket: &str,
        collection: &str,
        records: &[Value],
        pb: &Arc<ProgressBar>,
    ) -> Result<()> {
        let base_url = self.get_attachments_base_url()?;
        let mut outdated_attachments = Vec::new();

        // First pass: check which attachments need updating
        for record in records {
            if let Some(attachment) = record.get("attachment") {
                let record_id = record["id"].as_str().ok_or_else(|| {
                    RemoteSettingsError::Json(serde_json::Error::custom("No record id"))
                })?;

                let attachment: AttachmentMetadata = serde_json::from_value(attachment.clone())?;

                if !self.is_attachment_up_to_date(bucket, collection, record_id, &attachment)? {
                    outdated_attachments.push((record_id.to_string(), attachment));
                }
            }
        }

        if outdated_attachments.is_empty() {
            pb.finish_with_message(format!(
                "All attachments up to date for {}/{}",
                bucket, collection
            ));
            return Ok(());
        }

        // Try bundle first if we have outdated attachments
        if !outdated_attachments.is_empty() {
            if let Ok(()) = self.download_attachments_bundle(bucket, collection, pb) {
                // Bundle downloaded successfully, verify all attachments now
                let mut still_outdated = Vec::new();
                for (record_id, attachment) in outdated_attachments {
                    if !self.is_attachment_up_to_date(
                        bucket,
                        collection,
                        &record_id,
                        &attachment,
                    )? {
                        still_outdated.push((record_id, attachment));
                    }
                }
                outdated_attachments = still_outdated;
            }
        }

        // Download remaining outdated attachments individually
        for (record_id, attachment) in outdated_attachments {
            let (bin_path, meta_path) = self.get_attachment_paths(bucket, collection, &record_id);
            std::fs::create_dir_all(bin_path.parent().unwrap())?;

            let data = self.download_attachment(&base_url, &record_id, &attachment, pb)?;

            std::fs::write(&bin_path, data)?;
            std::fs::write(&meta_path, serde_json::to_string_pretty(&attachment)?)?;
        }

        pb.finish_with_message(format!("Updated attachments for {}/{}", bucket, collection));

        Ok(())
    }

    fn get(&self, url: &str) -> Result<Response> {
        let url = Url::parse(url)?;
        Ok(Request::get(url).send()?)
    }
}
