/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::error::*;
use crate::types::{ServiceStatus, SyncEngineSelection, SyncParams, SyncReason, SyncResult};
use crate::{reset, reset_all, wipe};
use error_support::{breadcrumb, debug, info, warn};
use parking_lot::Mutex;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::convert::TryFrom;
use std::time::SystemTime;
use sync15::client::{
    sync_multiple_with_command_processor, MemoryCachedState, Sync15StorageClientInit,
    SyncRequestInfo,
};
use sync15::clients_engine::{Command, CommandProcessor, CommandStatus, Settings};
use sync15::engine::{EngineSyncAssociation, SyncEngine, SyncEngineId};

#[derive(Default, uniffi::Object)]
pub struct SyncManager {
    mem_cached_state: Mutex<Option<MemoryCachedState>>,
}

#[uniffi::export]
impl SyncManager {
    #[uniffi::constructor]
    pub fn new() -> Self {
        Self::default()
    }

    /// Disconnect engines from sync, deleting/resetting the sync-related data
    pub fn disconnect(&self) {
        breadcrumb!("SyncManager disconnect()");
        for engine_id in SyncEngineId::iter() {
            if let Some(engine) = Self::get_engine(&engine_id) {
                if let Err(e) = engine.reset(&EngineSyncAssociation::Disconnected) {
                    error_support::report_error!(
                        "sync-manager-reset",
                        "Failed to reset {}: {}",
                        engine_id,
                        e
                    );
                }
            } else {
                warn!("Unable to reset {}, be sure to call register_with_sync_manager before disconnect if this is surprising", engine_id);
            }
        }
    }

    /// Perform a sync.  See [SyncParams] and [SyncResult] for details on how this works
    ///
    /// Fails with [SyncManagerError::Busy] if a sync is already in progress.
    pub fn sync(&self, params: SyncParams) -> Result<SyncResult> {
        breadcrumb!("SyncManager::sync started");
        let Some(mut state) = self.mem_cached_state.try_lock() else {
            breadcrumb!("SyncManager::sync is already in progress, bailing out early");
            return Err(SyncManagerError::Busy);
        };
        let engines = self.calc_engines_to_sync(&params.engines)?;
        let next_sync_after = state.as_ref().and_then(|mcs| mcs.get_next_sync_after());
        let result = if !backoff_in_effect(next_sync_after, &params) {
            info!("No backoff in effect (or we decided to ignore it), starting sync");
            self.do_sync(params, &mut state, engines)
        } else {
            breadcrumb!(
                "Backoff still in effect (until {:?}), bailing out early",
                next_sync_after
            );
            Ok(SyncResult {
                status: ServiceStatus::BackedOff,
                successful: Default::default(),
                failures: Default::default(),
                declined: None,
                next_sync_allowed_at: next_sync_after,
                persisted_state: params.persisted_state.unwrap_or_default(),
                // It would be nice to record telemetry here.
                telemetry_json: None,
            })
        };
        breadcrumb!("SyncManager sync ended");
        result
    }

    /// Get a list of engine names available for syncing
    pub fn get_available_engines(&self) -> Vec<String> {
        self.iter_registered_engines()
            .map(|(name, _)| name.to_string())
            .collect()
    }
}

// additional methods used internally but not exposed over the ffi.
impl SyncManager {
    fn get_engine_id(engine_name: &str) -> Result<SyncEngineId> {
        SyncEngineId::try_from(engine_name).map_err(SyncManagerError::UnknownEngine)
    }

    fn get_engine(engine_id: &SyncEngineId) -> Option<Box<dyn SyncEngine>> {
        match engine_id {
            SyncEngineId::History => places::get_registered_sync_engine(engine_id),
            SyncEngineId::Bookmarks => places::get_registered_sync_engine(engine_id),
            SyncEngineId::Addresses => autofill::get_registered_sync_engine(engine_id),
            SyncEngineId::CreditCards => autofill::get_registered_sync_engine(engine_id),
            SyncEngineId::Passwords => logins::get_registered_sync_engine(engine_id),
            SyncEngineId::Tabs => tabs::get_registered_sync_engine(engine_id),
        }
    }

    pub fn wipe(&self, engine_name: &str) -> Result<()> {
        if let Some(engine) = Self::get_engine(&Self::get_engine_id(engine_name)?) {
            engine.wipe()?;
        }
        Ok(())
    }

    pub fn reset(&self, engine_name: &str) -> Result<()> {
        if let Some(engine) = Self::get_engine(&Self::get_engine_id(engine_name)?) {
            engine.reset(&EngineSyncAssociation::Disconnected)?;
        }
        Ok(())
    }

    pub fn reset_all(&self) -> Result<()> {
        for (_, engine) in self.iter_registered_engines() {
            engine.reset(&EngineSyncAssociation::Disconnected)?;
        }
        Ok(())
    }

    fn do_sync(
        &self,
        mut params: SyncParams,
        state: &mut Option<MemoryCachedState>,
        mut engines: Vec<Box<dyn SyncEngine>>,
    ) -> Result<SyncResult> {
        let key_bundle = sync15::KeyBundle::from_ksync_base64(&params.auth_info.sync_key)?;
        let tokenserver_url = url::Url::parse(&params.auth_info.tokenserver_url)?;
        let interruptee = interrupt_support::ShutdownInterruptee;
        let mut mem_cached_state = state.take().unwrap_or_default();
        let mut disk_cached_state = params.persisted_state.take();

        // tell engines about the local encryption key.
        for engine in engines.iter_mut() {
            if let Some(key) = params.local_encryption_keys.get(&*engine.collection_name()) {
                engine.set_local_encryption_key(key)?
            }
        }

        let engine_refs: Vec<&dyn SyncEngine> = engines.iter().map(|s| &**s).collect();

        let client_init = Sync15StorageClientInit {
            key_id: params.auth_info.kid.clone(),
            access_token: params.auth_info.fxa_access_token.clone(),
            tokenserver_url,
        };
        let settings = Settings {
            fxa_device_id: params.device_settings.fxa_device_id,
            device_name: params.device_settings.name,
            device_type: params.device_settings.kind,
            enabled_engines: params.device_settings.enabled_engines,
        };
        let c = SyncClient::new(settings);
        let result = sync_multiple_with_command_processor(
            Some(&c),
            &engine_refs,
            &mut disk_cached_state,
            &mut mem_cached_state,
            &client_init,
            &key_bundle,
            &interruptee,
            Some(SyncRequestInfo {
                engine_choices_model: params.engine_choices_model,
                is_user_action: matches!(params.reason, SyncReason::User),
            }),
        );
        *state = Some(mem_cached_state);

        info!("Sync finished with status {:?}", result.service_status);
        let status = ServiceStatus::from(result.service_status);
        for (engine, result) in result.engine_results.iter() {
            info!("engine {:?} status: {:?}", engine, result);
        }
        let mut successful: Vec<String> = Vec::new();
        let mut failures: HashMap<String, String> = HashMap::new();
        for (engine, result) in result.engine_results.into_iter() {
            match result {
                Ok(_) => {
                    successful.push(engine);
                }
                Err(err) => {
                    failures.insert(engine, err.to_string());
                }
            }
        }
        let telemetry_json = serde_json::to_string(&result.telemetry).unwrap();

        Ok(SyncResult {
            status,
            successful,
            failures,
            declined: result.declined,
            next_sync_allowed_at: result.next_sync_after,
            persisted_state: disk_cached_state.unwrap_or_default(),
            telemetry_json: Some(telemetry_json),
        })
    }

    fn iter_registered_engines(&self) -> impl Iterator<Item = (SyncEngineId, Box<dyn SyncEngine>)> {
        SyncEngineId::iter().filter_map(|id| Self::get_engine(&id).map(|engine| (id, engine)))
    }

    fn calc_engines_to_sync(
        &self,
        selection: &SyncEngineSelection,
    ) -> Result<Vec<Box<dyn SyncEngine>>> {
        // BTreeMap to ensure we sync the engines in priority order.
        let mut engine_map: BTreeMap<_, _> = self.iter_registered_engines().collect();
        breadcrumb!(
            "Checking engines requested ({:?}) vs local engines ({:?})",
            selection,
            engine_map
                .keys()
                .map(|engine_id| engine_id.name())
                .collect::<Vec<_>>(),
        );
        if let SyncEngineSelection::Some {
            engines: engine_names,
        } = selection
        {
            // Validate selection and convert to SyncEngineId
            let mut selected_engine_ids: HashSet<SyncEngineId> = HashSet::new();
            for name in engine_names {
                let engine_id = Self::get_engine_id(name)?;
                if !engine_map.contains_key(&engine_id) {
                    return Err(SyncManagerError::UnsupportedFeature(name.to_string()));
                }
                selected_engine_ids.insert(engine_id);
            }
            // Filter engines based on the selection
            engine_map.retain(|engine_id, _| selected_engine_ids.contains(engine_id))
        }
        Ok(engine_map.into_values().collect())
    }
}

fn backoff_in_effect(next_sync_after: Option<SystemTime>, p: &SyncParams) -> bool {
    let now = SystemTime::now();
    if let Some(nsa) = next_sync_after {
        if nsa > now {
            return if matches!(p.reason, SyncReason::User | SyncReason::EnabledChange) {
                info!(
                    "Still under backoff, but syncing anyway because reason is {:?}",
                    p.reason
                );
                false
            } else if p.engine_choices_model.enabled_changes().is_some() {
                info!("Still under backoff, but syncing because we have enabled state changes.");
                false
            } else {
                info!("Still under backoff, and there's no compelling reason for us to ignore it");
                true
            };
        }
    }
    debug!("Not under backoff");
    false
}

impl From<sync15::client::ServiceStatus> for ServiceStatus {
    fn from(s15s: sync15::client::ServiceStatus) -> Self {
        use sync15::client::ServiceStatus::*;
        match s15s {
            Ok => ServiceStatus::Ok,
            NetworkError => ServiceStatus::NetworkError,
            ServiceError => ServiceStatus::ServiceError,
            AuthenticationError => ServiceStatus::AuthError,
            BackedOff => ServiceStatus::BackedOff,
            Interrupted => ServiceStatus::OtherError, // Eh...
            OtherError => ServiceStatus::OtherError,
        }
    }
}

struct SyncClient(Settings);

impl SyncClient {
    pub fn new(settings: Settings) -> SyncClient {
        SyncClient(settings)
    }
}

impl CommandProcessor for SyncClient {
    fn settings(&self) -> &Settings {
        &self.0
    }

    fn apply_incoming_command(&self, command: Command) -> anyhow::Result<CommandStatus> {
        let result = match command {
            Command::Wipe(engine) => wipe(&engine),
            Command::Reset(engine) => reset(&engine),
            Command::ResetAll => reset_all(),
        };
        match result {
            Ok(()) => Ok(CommandStatus::Applied),
            Err(err) => match err {
                SyncManagerError::UnknownEngine(_) => Ok(CommandStatus::Unsupported),
                _ => Err(err.into()),
            },
        }
    }

    fn fetch_outgoing_commands(&self) -> anyhow::Result<HashSet<Command>> {
        Ok(HashSet::new())
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::types::{DeviceSettings, SyncAuthInfo};
    use sync15::EngineChoicesModel;

    #[test]
    fn test_engine_id_sanity() {
        for engine_id in SyncEngineId::iter() {
            assert_eq!(engine_id, SyncEngineId::try_from(engine_id.name()).unwrap());
        }
    }

    fn dummy_sync_params() -> SyncParams {
        SyncParams {
            reason: SyncReason::Scheduled,
            engines: SyncEngineSelection::All,
            engine_choices_model: EngineChoicesModel::default(),
            local_encryption_keys: HashMap::new(),
            auth_info: SyncAuthInfo {
                kid: "kid".to_string(),
                fxa_access_token: "token".to_string(),
                sync_key: "sync-key".to_string(),
                tokenserver_url: "https://example.com/token/1.0/sync/1.5".to_string(),
            },
            persisted_state: None,
            device_settings: DeviceSettings {
                fxa_device_id: "device-id".to_string(),
                name: "Test Device".to_string(),
                kind: sync15::DeviceType::Mobile,
                enabled_engines: vec![],
            },
        }
    }

    #[test]
    fn test_sync_is_busy_while_a_sync_is_in_progress() {
        let manager = SyncManager::new();
        let _in_progress = manager.mem_cached_state.lock();
        assert!(matches!(
            manager.sync(dummy_sync_params()),
            Err(SyncManagerError::Busy)
        ));
    }
}
