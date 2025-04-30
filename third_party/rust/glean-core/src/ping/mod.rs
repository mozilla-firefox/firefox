// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Ping collection, assembly & submission.

use std::fs::{self, create_dir_all, File};
use std::io::{BufRead, BufReader, Write};
use std::path::{Path, PathBuf};

use log::info;
use serde_json::{json, Value as JsonValue};

use crate::common_metric_data::{CommonMetricData, Lifetime};
use crate::metrics::{CounterMetric, DatetimeMetric, Metric, MetricType, PingType, TimeUnit};
use crate::storage::{StorageManager, INTERNAL_STORAGE};
use crate::upload::{HeaderMap, PingMetadata};
use crate::util::{get_iso_time_string, local_now_with_offset};
use crate::{Glean, Result, DELETION_REQUEST_PINGS_DIRECTORY, PENDING_PINGS_DIRECTORY};

/// Holds everything you need to store or send a ping.
pub struct Ping<'a> {
    /// The unique document id.
    pub doc_id: &'a str,
    /// The ping's name.
    pub name: &'a str,
    /// The path on the server to use when uplaoding this ping.
    pub url_path: &'a str,
    /// The payload, including `*_info` fields.
    pub content: JsonValue,
    /// The headers to upload with the payload.
    pub headers: HeaderMap,
    /// Whether the content contains {client|ping}_info sections.
    pub includes_info_sections: bool,
    /// Other pings that should be scheduled when this ping is sent.
    pub schedules_pings: Vec<String>,
    /// Capabilities the uploader must have in order to uplaoad this ping.
    pub uploader_capabilities: Vec<String>,
}

/// Collect a ping's data, assemble it into its full payload and store it on disk.
pub struct PingMaker;

fn merge(a: &mut JsonValue, b: &JsonValue) {
    match (a, b) {
        (&mut JsonValue::Object(ref mut a), JsonValue::Object(b)) => {
            for (k, v) in b {
                merge(a.entry(k.clone()).or_insert(JsonValue::Null), v);
            }
        }
        (a, b) => {
            *a = b.clone();
        }
    }
}

impl Default for PingMaker {
    fn default() -> Self {
        Self::new()
    }
}

impl PingMaker {
    /// Creates a new [`PingMaker`].
    pub fn new() -> Self {
        Self
    }

    /// Gets, and then increments, the sequence number for a given ping.
    fn get_ping_seq(&self, glean: &Glean, storage_name: &str) -> usize {
        // Don't attempt to increase sequence number for disabled ping
        if !glean.is_ping_enabled(storage_name) {
            return 0;
        }

        // Sequence numbers are stored as a counter under a name that includes the storage name
        let seq = CounterMetric::new(CommonMetricData {
            name: format!("{}#sequence", storage_name),
            // We don't need a category, the name is already unique
            category: "".into(),
            send_in_pings: vec![INTERNAL_STORAGE.into()],
            lifetime: Lifetime::User,
            ..Default::default()
        });

        let current_seq = match StorageManager.snapshot_metric(
            glean.storage(),
            INTERNAL_STORAGE,
            &seq.meta().identifier(glean),
            seq.meta().inner.lifetime,
        ) {
            Some(Metric::Counter(i)) => i,
            _ => 0,
        };

        // Increase to next sequence id
        seq.add_sync(glean, 1);

        current_seq as usize
    }

    /// Gets the formatted start and end times for this ping and update for the next ping.
    fn get_start_end_times(
        &self,
        glean: &Glean,
        storage_name: &str,
        time_unit: TimeUnit,
    ) -> (String, String) {
        let start_time = DatetimeMetric::new(
            CommonMetricData {
                name: format!("{}#start", storage_name),
                category: "".into(),
                send_in_pings: vec![INTERNAL_STORAGE.into()],
                lifetime: Lifetime::User,
                ..Default::default()
            },
            time_unit,
        );

        // "start_time" is the time the ping was generated the last time.
        // If not available, we use the date the Glean object was initialized.
        let start_time_data = start_time
            .get_value(glean, INTERNAL_STORAGE)
            .unwrap_or_else(|| glean.start_time());
        let end_time_data = local_now_with_offset();

        // Update the start time with the current time.
        start_time.set_sync_chrono(glean, end_time_data);

        // Format the times.
        let start_time_data = get_iso_time_string(start_time_data, time_unit);
        let end_time_data = get_iso_time_string(end_time_data, time_unit);
        (start_time_data, end_time_data)
    }

    fn get_ping_info(
        &self,
        glean: &Glean,
        storage_name: &str,
        reason: Option<&str>,
        precision: TimeUnit,
    ) -> JsonValue {
        let (start_time, end_time) = self.get_start_end_times(glean, storage_name, precision);
        let mut map = json!({
            "seq": self.get_ping_seq(glean, storage_name),
            "start_time": start_time,
            "end_time": end_time,
        });

        if let Some(reason) = reason {
            map.as_object_mut()
                .unwrap() // safe unwrap, we created the object above
                .insert("reason".to_string(), JsonValue::String(reason.to_string()));
        };

        // Get the experiment data, if available.
        if let Some(experiment_data) =
            StorageManager.snapshot_experiments_as_json(glean.storage(), INTERNAL_STORAGE)
        {
            map.as_object_mut()
                .unwrap() // safe unwrap, we created the object above
                .insert("experiments".to_string(), experiment_data);
        };

        map
    }

    fn get_client_info(&self, glean: &Glean, include_client_id: bool) -> JsonValue {
        // Add the "telemetry_sdk_build", which is the glean-core version.
        let mut map = json!({
            "telemetry_sdk_build": crate::GLEAN_VERSION,
        });

        // Flatten the whole thing.
        if let Some(client_info) =
            StorageManager.snapshot_as_json(glean.storage(), "glean_client_info", true)
        {
            let client_info_obj = client_info.as_object().unwrap(); // safe unwrap, snapshot always returns an object.
            for (_metric_type, metrics) in client_info_obj {
                merge(&mut map, metrics);
            }
            let map = map.as_object_mut().unwrap(); // safe unwrap, we created the object above.
            let mut attribution = serde_json::Map::new();
            let mut distribution = serde_json::Map::new();
            map.retain(|name, value| {
                // Only works because we ensure no client_info metric categories contain '.'.
                let mut split = name.split('.');
                let category = split.next();
                let name = split.next();
                if let (Some(category), Some(name)) = (category, name) {
                    if category == "attribution" {
                        attribution.insert(name.into(), value.take());
                        false
                    } else if category == "distribution" {
                        distribution.insert(name.into(), value.take());
                        false
                    } else {
                        true
                    }
                } else {
                    true
                }
            });
            if !attribution.is_empty() {
                map.insert("attribution".into(), serde_json::Value::from(attribution));
            }
            if !distribution.is_empty() {
                map.insert("distribution".into(), serde_json::Value::from(distribution));
            }
        } else {
            log::warn!("Empty client info data.");
        }

        if !include_client_id {
            // safe unwrap, we created the object above
            map.as_object_mut().unwrap().remove("client_id");
        }

        json!(map)
    }

    /// Build the headers to be persisted and sent with a ping.
    ///
    /// Currently the only headers we persist are `X-Debug-ID` and `X-Source-Tags`.
    ///
    /// # Arguments
    ///
    /// * `glean` - the [`Glean`] instance to collect headers from.
    ///
    /// # Returns
    ///
    /// A map of header names to header values.
    /// Might be empty if there are no extra headers to send.
    /// ```
    fn get_headers(&self, glean: &Glean) -> HeaderMap {
        let mut headers_map = HeaderMap::new();

        if let Some(debug_view_tag) = glean.debug_view_tag() {
            headers_map.insert("X-Debug-ID".to_string(), debug_view_tag.to_string());
        }

        if let Some(source_tags) = glean.source_tags() {
            headers_map.insert("X-Source-Tags".to_string(), source_tags.join(","));
        }

        headers_map
    }

    /// Collects a snapshot for the given ping from storage and attach required meta information.
    ///
    /// # Arguments
    ///
    /// * `glean` - the [`Glean`] instance to collect data from.
    /// * `ping` - the ping to collect for.
    /// * `reason` - an optional reason code to include in the ping.
    /// * `doc_id` - the ping's unique document identifier.
    /// * `url_path` - the path on the server to upload this ping to.
    ///
    /// # Returns
    ///
    /// A fully assembled representation of the ping payload and associated metadata.
    /// If there is no data stored for the ping, `None` is returned.
    pub fn collect<'a>(
        &self,
        glean: &Glean,
        ping: &'a PingType,
        reason: Option<&str>,
        doc_id: &'a str,
        url_path: &'a str,
    ) -> Option<Ping<'a>> {
        info!("Collecting {}", ping.name());
        let database = glean.storage();

        // HACK: Only for metrics pings we add the ping timings.
        // But we want that to persist until the next metrics ping is actually sent.
        let write_samples = database.write_timings.replace(Vec::with_capacity(64));
        if !write_samples.is_empty() {
            glean
                .database_metrics
                .write_time
                .accumulate_samples_sync(glean, &write_samples);
        }

        let mut metrics_data = StorageManager.snapshot_as_json(database, ping.name(), true);

        let events_data = glean
            .event_storage()
            .snapshot_as_json(glean, ping.name(), true);

        // Due to the way the experimentation identifier could link datasets that are intentionally unlinked,
        // it will not be included in pings that specifically exclude the Glean client-id, those pings that
        // should not be sent if empty, or pings that exclude the {client|ping}_info sections wholesale.
        if (!ping.include_client_id() || !ping.send_if_empty() || !ping.include_info_sections())
            && glean.test_get_experimentation_id().is_some()
            && metrics_data.is_some()
        {
            // There is a lot of unwrapping here, but that's fine because the `if` conditions above mean that the
            // experimentation id is present in the metrics.
            let metrics = metrics_data.as_mut().unwrap().as_object_mut().unwrap();
            let metrics_count = metrics.len();
            let strings = metrics.get_mut("string").unwrap().as_object_mut().unwrap();
            let string_count = strings.len();

            // Handle the send_if_empty case by checking if the experimentation id is the only metric in the data.
            let empty_payload = events_data.is_none() && metrics_count == 1 && string_count == 1;
            if !ping.include_client_id() || (!ping.send_if_empty() && empty_payload) {
                strings.remove("glean.client.annotation.experimentation_id");
            }

            if strings.is_empty() {
                metrics.remove("string");
            }

            if metrics.is_empty() {
                metrics_data = None;
            }
        }

        let is_empty = metrics_data.is_none() && events_data.is_none();
        if !ping.send_if_empty() && is_empty {
            info!("Storage for {} empty. Bailing out.", ping.name());
            return None;
        } else if ping.name() == "events" && events_data.is_none() {
            info!("No events for 'events' ping. Bailing out.");
            return None;
        } else if is_empty {
            info!(
                "Storage for {} empty. Ping will still be sent.",
                ping.name()
            );
        }

        let precision = if ping.precise_timestamps() {
            TimeUnit::Millisecond
        } else {
            TimeUnit::Minute
        };

        let mut json = if ping.include_info_sections() {
            let ping_info = self.get_ping_info(glean, ping.name(), reason, precision);
            let client_info = self.get_client_info(glean, ping.include_client_id());

            json!({
                "ping_info": ping_info,
                "client_info": client_info
            })
        } else {
            json!({})
        };

        let json_obj = json.as_object_mut()?;
        if let Some(metrics_data) = metrics_data {
            json_obj.insert("metrics".to_string(), metrics_data);
        }
        if let Some(events_data) = events_data {
            json_obj.insert("events".to_string(), events_data);
        }

        Some(Ping {
            content: json,
            name: ping.name(),
            doc_id,
            url_path,
            headers: self.get_headers(glean),
            includes_info_sections: ping.include_info_sections(),
            schedules_pings: ping.schedules_pings().to_vec(),
            uploader_capabilities: ping.uploader_capabilities().to_vec(),
        })
    }

    /// Gets the path to a directory for ping storage.
    ///
    /// The directory will be created inside the `data_path`.
    /// The `pings` directory (and its parents) is created if it does not exist.
    fn get_pings_dir(&self, data_path: &Path, ping_type: Option<&str>) -> std::io::Result<PathBuf> {
        // Use a special directory for deletion-request pings
        let pings_dir = match ping_type {
            Some("deletion-request") => data_path.join(DELETION_REQUEST_PINGS_DIRECTORY),
            _ => data_path.join(PENDING_PINGS_DIRECTORY),
        };

        create_dir_all(&pings_dir)?;
        Ok(pings_dir)
    }

    /// Gets path to a directory for temporary storage.
    ///
    /// The directory will be created inside the `data_path`.
    /// The `tmp` directory (and its parents) is created if it does not exist.
    fn get_tmp_dir(&self, data_path: &Path) -> std::io::Result<PathBuf> {
        let pings_dir = data_path.join("tmp");
        create_dir_all(&pings_dir)?;
        Ok(pings_dir)
    }

    /// Stores a ping to disk in the pings directory.
    pub fn store_ping(&self, data_path: &Path, ping: &Ping) -> std::io::Result<()> {
        let pings_dir = self.get_pings_dir(data_path, Some(ping.name))?;
        let temp_dir = self.get_tmp_dir(data_path)?;

        // Write to a temporary location and then move when done,
        // for transactional writes.
        let temp_ping_path = temp_dir.join(ping.doc_id);
        let ping_path = pings_dir.join(ping.doc_id);

        log::debug!(
            "Storing ping '{}' at '{}'",
            ping.doc_id,
            ping_path.display()
        );

        {
            let mut file = File::create(&temp_ping_path)?;
            file.write_all(ping.url_path.as_bytes())?;
            file.write_all(b"\n")?;
            file.write_all(::serde_json::to_string(&ping.content)?.as_bytes())?;
            file.write_all(b"\n")?;
            let metadata = PingMetadata {
                // We don't actually need to clone the headers except to match PingMetadata's ownership.
                // But since we're going to write a file to disk in a sec,
                // and HeaderMaps tend to have only like two things in them, tops,
                // the cost is bearable.
                headers: Some(ping.headers.clone()),
                body_has_info_sections: Some(ping.includes_info_sections),
                ping_name: Some(ping.name.to_string()),
                uploader_capabilities: Some(ping.uploader_capabilities.clone()),
            };
            file.write_all(::serde_json::to_string(&metadata)?.as_bytes())?;
        }

        if let Err(e) = std::fs::rename(&temp_ping_path, &ping_path) {
            log::warn!(
                "Unable to move '{}' to '{}",
                temp_ping_path.display(),
                ping_path.display()
            );
            return Err(e);
        }

        Ok(())
    }

    /// Clears any pending pings in the queue.
    pub fn clear_pending_pings(&self, data_path: &Path, ping_names: &[&str]) -> Result<()> {
        let pings_dir = self.get_pings_dir(data_path, None)?;

        // TODO(bug 1932909): Refactor this into its own function
        // and share it with `upload::directory`.
        let entries = pings_dir.read_dir()?;
        for entry in entries.filter_map(|entry| entry.ok()) {
            if let Ok(file_type) = entry.file_type() {
                if !file_type.is_file() {
                    continue;
                }
            } else {
                continue;
            }

            let file = match File::open(entry.path()) {
                Ok(file) => file,
                Err(_) => {
                    continue;
                }
            };

            let mut lines = BufReader::new(file).lines();
            if let (Some(Ok(path)), Some(Ok(_body)), Ok(metadata)) =
                (lines.next(), lines.next(), lines.next().transpose())
            {
                let PingMetadata { ping_name, .. } = metadata
                    .and_then(|m| crate::upload::process_metadata(&path, &m))
                    .unwrap_or_default();
                let ping_name =
                    ping_name.unwrap_or_else(|| path.split('/').nth(3).unwrap_or("").into());

                if ping_names.contains(&&ping_name[..]) {
                    _ = fs::remove_file(entry.path());
                }
            } else {
                continue;
            }
        }

        log::debug!("All pending pings deleted");

        Ok(())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::tests::new_glean;

    #[test]
    fn sequence_numbers_should_be_reset_when_toggling_uploading() {
        let (mut glean, _t) = new_glean(None);
        let ping_maker = PingMaker::new();

        assert_eq!(0, ping_maker.get_ping_seq(&glean, "store1"));
        assert_eq!(1, ping_maker.get_ping_seq(&glean, "store1"));

        glean.set_upload_enabled(false);
        assert_eq!(0, ping_maker.get_ping_seq(&glean, "store1"));
        assert_eq!(0, ping_maker.get_ping_seq(&glean, "store1"));

        glean.set_upload_enabled(true);
        assert_eq!(0, ping_maker.get_ping_seq(&glean, "store1"));
        assert_eq!(1, ping_maker.get_ping_seq(&glean, "store1"));
    }
}
