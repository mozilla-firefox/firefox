/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This helps you perform a sync of multiple engines and helps you manage
// global and local state between syncs.

use super::state::{EngineChangesNeeded, GlobalState, PersistedGlobalState, SetupStateMachine};
use super::status::{ServiceStatus, SyncResult};
use super::storage_client::{BackoffListener, Sync15StorageClient, Sync15StorageClientInit};
use crate::clients_engine::{self, CLIENTS_TTL_REFRESH, CommandProcessor};
use crate::engine::{EngineSyncAssociation, SyncEngine};
use crate::error::{Error, debug, info, trace, warn};
use crate::telemetry;
use crate::{EngineChoicesModel, KeyBundle};
use interrupt_support::Interruptee;
use std::collections::HashMap;
use std::result;
use std::time::{Duration, SystemTime};

/// Info about the client to use. We reuse the client unless
/// we discover the client_init has changed, in which case we re-create one.
#[derive(Debug)]
struct ClientInfo {
    // the client_init used to create `client`.
    client_init: Sync15StorageClientInit,
    // the client (our tokenserver state machine state, and our http library's state)
    client: Sync15StorageClient,
}

impl ClientInfo {
    fn new(ci: &Sync15StorageClientInit) -> Result<Self, Error> {
        Ok(Self {
            client_init: ci.clone(),
            client: Sync15StorageClient::new(ci.clone())?,
        })
    }
}

/// Info we want callers to engine *in memory* for us so that subsequent
/// syncs are faster. This should never be persisted to storage as it holds
/// sensitive information, such as the sync decryption keys.
#[derive(Debug, Default)]
pub struct MemoryCachedState {
    last_client_info: Option<ClientInfo>,
    last_global_state: Option<GlobalState>,
    // These are just engined in memory, as persisting an invalid value far in the
    // future has the potential to break sync for good.
    next_sync_after: Option<SystemTime>,
    next_client_refresh_after: Option<SystemTime>,
}

impl MemoryCachedState {
    // Called we notice the cached state is stale.
    pub fn clear_sensitive_info(&mut self) {
        self.last_client_info = None;
        self.last_global_state = None;
        // Leave the backoff time, as there's no reason to think it's not still
        // true.
    }
    pub fn get_next_sync_after(&self) -> Option<SystemTime> {
        self.next_sync_after
    }
    pub fn should_refresh_client(&self) -> bool {
        match self.next_client_refresh_after {
            Some(t) => SystemTime::now() > t,
            None => true,
        }
    }
    pub fn note_client_refresh(&mut self) {
        self.next_client_refresh_after =
            Some(SystemTime::now() + Duration::from_secs(CLIENTS_TTL_REFRESH));
    }
}

/// Sync multiple engines
/// * `engines` - The engines to sync
/// * `persisted_global_state` - The global state to use, or None if never
///   before provided. At the end of the sync, and even when the sync fails,
///   the value in this cell should be persisted to permanent storage and
///   provided next time the sync is called.
/// * `last_client_info` - The client state to use, or None if never before
///   provided. At the end of the sync, the value should be persisted
///   *in memory only* - it should not be persisted to disk.
/// * `storage_init` - Information about how the sync http client should be
///   configured.
/// * `root_sync_key` - The KeyBundle used for encryption.
///
/// Returns a map, keyed by name and holding an error value - if any engine
/// fails, the sync will continue on to other engines, but the error will be
/// places in this map. The absence of a name in the map implies the engine
/// succeeded.
pub fn sync_multiple(
    engines: &[&dyn SyncEngine],
    persisted_global_state: &mut Option<String>,
    mem_cached_state: &mut MemoryCachedState,
    storage_init: &Sync15StorageClientInit,
    root_sync_key: &KeyBundle,
    interruptee: &dyn Interruptee,
    req_info: Option<SyncRequestInfo>,
) -> SyncResult {
    sync_multiple_with_command_processor(
        None,
        engines,
        persisted_global_state,
        mem_cached_state,
        storage_init,
        root_sync_key,
        interruptee,
        req_info,
    )
}

/// Like `sync_multiple`, but specifies an optional command processor to handle
/// commands from the clients collection. This function is called by the sync
/// manager, which provides its own processor.
#[allow(clippy::too_many_arguments)]
pub fn sync_multiple_with_command_processor(
    command_processor: Option<&dyn CommandProcessor>,
    engines: &[&dyn SyncEngine],
    persisted_global_state: &mut Option<String>,
    mem_cached_state: &mut MemoryCachedState,
    storage_init: &Sync15StorageClientInit,
    root_sync_key: &KeyBundle,
    interruptee: &dyn Interruptee,
    req_info: Option<SyncRequestInfo>,
) -> SyncResult {
    info!("Syncing {} engines", engines.len());
    let mut sync_result = SyncResult {
        service_status: ServiceStatus::OtherError,
        result: Ok(()),
        declined: None,
        next_sync_after: None,
        engine_results: HashMap::with_capacity(engines.len()),
        telemetry: telemetry::SyncTelemetryPing::new(),
    };
    let backoff = super::storage_client::new_backoff_listener();
    let req_info = req_info.unwrap_or_default();
    let driver = SyncMultipleDriver {
        command_processor,
        engines,
        storage_init,
        interruptee,
        engine_choices_model: req_info.engine_choices_model,
        backoff: backoff.clone(),
        root_sync_key,
        result: &mut sync_result,
        persisted_global_state,
        mem_cached_state,
        saw_auth_error: false,
        ignore_soft_backoff: req_info.is_user_action,
    };
    match driver.sync() {
        Ok(()) => {
            debug!(
                "sync was successful, final status={:?}",
                sync_result.service_status
            );
        }
        Err(e) => {
            warn!(
                "sync failed: {}, final status={:?}",
                e, sync_result.service_status,
            );
            sync_result.result = Err(e);
        }
    }
    // Respect `backoff` value when computing the next sync time even if we were
    // ignoring it during the sync
    sync_result.set_sync_after(backoff.get_required_wait(false).unwrap_or_default());
    mem_cached_state.next_sync_after = sync_result.next_sync_after;
    trace!("Sync result: {:?}", sync_result);
    sync_result
}

/// This is essentially a bag of information that the sync manager knows, but
/// otherwise we won't. It should probably be rethought if it gains many more
/// fields.
#[derive(Debug, Default)]
pub struct SyncRequestInfo {
    pub engine_choices_model: EngineChoicesModel,
    pub is_user_action: bool,
}

// The sync multiple driver
struct SyncMultipleDriver<'info, 'res, 'pgs, 'mcs> {
    command_processor: Option<&'info dyn CommandProcessor>,
    engines: &'info [&'info dyn SyncEngine],
    storage_init: &'info Sync15StorageClientInit,
    root_sync_key: &'info KeyBundle,
    interruptee: &'info dyn Interruptee,
    backoff: BackoffListener,
    engine_choices_model: EngineChoicesModel,
    result: &'res mut SyncResult,
    persisted_global_state: &'pgs mut Option<String>,
    mem_cached_state: &'mcs mut MemoryCachedState,
    ignore_soft_backoff: bool,
    saw_auth_error: bool,
}

impl SyncMultipleDriver<'_, '_, '_, '_> {
    /// The actual worker for sync_multiple.
    fn sync(mut self) -> result::Result<(), Error> {
        info!("Loading/initializing persisted state");
        let mut pgs = self.prepare_persisted_state();

        info!("Preparing client info");
        let client_info = self.prepare_client_info()?;

        if self.was_interrupted() {
            return Ok(());
        }

        info!("Entering sync state machine");
        // Advance the state machine to the point where it can perform a full
        // sync. This may involve uploading meta/global, crypto/keys etc.
        let mut global_state = self.run_state_machine(&client_info, &mut pgs)?;

        if self.was_interrupted() {
            return Ok(());
        }

        // Set the service status to OK here - we may adjust it based on an individual
        // engine failing.
        self.result.service_status = ServiceStatus::Ok;

        let clients_engine = if let Some(command_processor) = self.command_processor {
            info!("Synchronizing clients engine");
            let should_refresh = self.mem_cached_state.should_refresh_client();
            let mut engine = clients_engine::Engine::new(command_processor, self.interruptee);
            if let Err(e) = engine.sync(
                &client_info.client,
                &global_state,
                self.root_sync_key,
                should_refresh,
            ) {
                // Record telemetry with the error just in case...
                let mut telem_sync = telemetry::SyncTelemetry::new();
                let mut telem_engine = telemetry::Engine::new("clients");
                telem_engine.failure(&e);
                telem_sync.engine(telem_engine);
                self.result.service_status = ServiceStatus::from_err(&e);

                // ...And bail, because a clients engine sync failure is fatal.
                return Err(e);
            }
            // We don't record telemetry for successful clients engine
            // syncs, since we only keep client records in memory, we
            // expect the counts to be the same most times, and a
            // failure aborts the entire sync.
            if self.was_interrupted() {
                return Ok(());
            }
            self.mem_cached_state.note_client_refresh();
            Some(engine)
        } else {
            None
        };

        info!("Synchronizing engines");

        let telem_sync =
            self.sync_engines(&client_info, &mut global_state, clients_engine.as_ref());
        self.result.telemetry.sync(telem_sync);

        info!("Finished syncing engines.");

        if !self.saw_auth_error {
            trace!("Updating persisted global state");
            self.mem_cached_state.last_client_info = Some(client_info);
            self.mem_cached_state.last_global_state = Some(global_state);
        }

        Ok(())
    }

    fn was_interrupted(&mut self) -> bool {
        if self.interruptee.was_interrupted() {
            info!("Interrupted, bailing out");
            self.result.service_status = ServiceStatus::Interrupted;
            true
        } else {
            false
        }
    }

    fn sync_engines(
        &mut self,
        client_info: &ClientInfo,
        global_state: &mut GlobalState,
        clients: Option<&clients_engine::Engine<'_>>,
    ) -> telemetry::SyncTelemetry {
        let mut telem_sync = telemetry::SyncTelemetry::new();
        for engine in self.engines {
            let name = engine.collection_name();
            if self
                .backoff
                .get_required_wait(self.ignore_soft_backoff)
                .is_some()
            {
                warn!("Got backoff, bailing out of sync early");
                break;
            }
            if global_state.global.declined.iter().any(|e| e == &*name) {
                info!("The {} engine is declined. Skipping", name);
                continue;
            }
            info!("Syncing {} engine!", name);

            let mut telem_engine = telemetry::Engine::new(&*name);
            let result = super::sync::synchronize_with_clients_engine(
                &client_info.client,
                global_state,
                self.root_sync_key,
                clients,
                *engine,
                true,
                &mut telem_engine,
                self.interruptee,
            );

            match result {
                Ok(()) => info!("Sync of {} was successful!", name),
                Err(ref e) => {
                    warn!("Sync of {} failed! {:?}", name, e);
                    let this_status = ServiceStatus::from_err(e);
                    // The only error which forces us to discard our state is an
                    // auth error.
                    self.saw_auth_error =
                        self.saw_auth_error || this_status == ServiceStatus::AuthenticationError;
                    telem_engine.failure(e);
                    // If the failure from the engine looks like anything other than
                    // a "engine error" we don't bother trying the others.
                    if this_status != ServiceStatus::OtherError {
                        telem_sync.engine(telem_engine);
                        self.result.engine_results.insert(name.into(), result);
                        self.result.service_status = this_status;
                        break;
                    }
                }
            }
            telem_sync.engine(telem_engine);
            self.result.engine_results.insert(name.into(), result);
            if self.was_interrupted() {
                break;
            }
        }
        telem_sync
    }

    fn run_state_machine(
        &mut self,
        client_info: &ClientInfo,
        pgs: &mut PersistedGlobalState,
    ) -> result::Result<GlobalState, Error> {
        let last_state = self.mem_cached_state.last_global_state.take();

        let mut state_machine = SetupStateMachine::for_full_sync(
            &client_info.client,
            self.root_sync_key,
            pgs,
            self.engine_choices_model.enabled_changes(),
            self.interruptee,
        );

        info!("Advancing state machine to ready (full)");
        let res = state_machine.run_to_ready(last_state);
        // Grab this now even though we don't need it until later to avoid a
        // lifetime issue
        let changes = state_machine.changes_needed.take();
        // The state machine might have updated our persisted_global_state, so
        // update the caller's repr of it.
        *self.persisted_global_state = Some(serde_json::to_string(&pgs)?);

        // Now that we've gone through the state machine, engine the declined list in
        // the sync_result
        self.result.declined = Some(pgs.get_declined().to_vec());
        debug!(
            "Declined engines list after state machine set to: {:?}",
            self.result.declined,
        );

        if let Some(c) = changes {
            self.wipe_or_reset_engines(c, &client_info.client)?;
        }
        let state = match res {
            Err(e) => {
                self.result.service_status = ServiceStatus::from_err(&e);
                return Err(e);
            }
            Ok(state) => state,
        };
        self.result.telemetry.uid(client_info.client.hashed_uid()?);
        // As for client_info, put None back now so we start from scratch on error.
        self.mem_cached_state.last_global_state = None;
        Ok(state)
    }

    fn wipe_or_reset_engines(
        &mut self,
        changes: EngineChangesNeeded,
        client: &Sync15StorageClient,
    ) -> result::Result<(), Error> {
        if changes.local_resets.is_empty() && changes.remote_wipes.is_empty() {
            return Ok(());
        }
        for e in &changes.remote_wipes {
            info!("Engine {:?} just got disabled locally, wiping server", e);
            client.wipe_remote_engine(e)?;
        }

        for s in self.engines {
            let name = s.collection_name();
            if changes.local_resets.contains(&*name) {
                info!("Resetting engine {}, as it was declined remotely", name);
                s.reset(&EngineSyncAssociation::Disconnected)?;
            }
        }

        Ok(())
    }

    fn prepare_client_info(&mut self) -> result::Result<ClientInfo, Error> {
        let mut client_info = match self.mem_cached_state.last_client_info.take() {
            Some(client_info) => {
                // if our storage_init has changed it probably means the user has
                // changed, courtesy of the 'kid' in the structure. Thus, we can't
                // reuse the client or the memory cached state. We do keep the disk
                // state as currently that's only the declined list.
                if client_info.client_init != *self.storage_init {
                    info!("Discarding all state as the account might have changed");
                    *self.mem_cached_state = MemoryCachedState::default();
                    ClientInfo::new(self.storage_init)?
                } else {
                    debug!("Reusing memory-cached client_info");
                    // we can reuse it (which should be the common path)
                    client_info
                }
            }
            None => {
                debug!("mem_cached_state was stale or missing, need setup");
                // We almost certainly have no other state here, but to be safe, we
                // throw away any memory state we do have.
                self.mem_cached_state.clear_sensitive_info();
                ClientInfo::new(self.storage_init)?
            }
        };
        // Ensure we use the correct listener here rather than on all the branches
        // above, since it seems less error prone.
        client_info.client.backoff = self.backoff.clone();
        Ok(client_info)
    }

    fn prepare_persisted_state(&mut self) -> PersistedGlobalState {
        // Note that any failure to use a persisted state means we also decline
        // to use our memory cached state, so that we fully rebuild that
        // persisted state for next time.
        match self.persisted_global_state {
            Some(persisted_string) if !persisted_string.is_empty() => {
                match serde_json::from_str::<PersistedGlobalState>(persisted_string) {
                    Ok(state) => {
                        trace!("Read persisted state: {:?}", state);
                        // Note that we don't set `result.declined` from the
                        // data in state - it remains None, which explicitly
                        // indicates "we don't have updated info".
                        state
                    }
                    _ => {
                        // Don't log the error since it might contain sensitive
                        // info (although currently it only contains the declined engines list)
                        error_support::report_error!(
                            "sync15-prepare-persisted-state",
                            "Failed to parse PersistedGlobalState from JSON! Falling back to default"
                        );
                        *self.mem_cached_state = MemoryCachedState::default();
                        PersistedGlobalState::default()
                    }
                }
            }
            _ => {
                info!(
                    "The application didn't give us persisted state - \
                     this is only expected on the very first run for a given user."
                );
                *self.mem_cached_state = MemoryCachedState::default();
                PersistedGlobalState::default()
            }
        }
    }
}
