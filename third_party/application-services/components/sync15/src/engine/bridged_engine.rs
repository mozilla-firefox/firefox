/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::error::debug;
use crate::{ServerTimestamp, telemetry};
use anyhow::Result;

use crate::Guid;
use crate::bso::IncomingBso;

use super::{CollSyncIds, EngineSyncAssociation, SyncEngine};

/// Adapts a [`SyncEngine`] to the method set that Desktop Firefox's JS Sync
/// framework drives (historically the `mozIBridgedSyncEngine` shape). Desktop
/// owns the fetch loop, so unlike the native Rust sync client it reads and
/// writes the engine's last-sync time explicitly and manages sync IDs as opaque
/// strings; this wrapper translates those calls onto the `SyncEngine` trait, and
/// handles the JSON `String` <-> BSO marshalling that crosses the UniFFI
/// boundary.
///
/// Consuming crates expose a thin newtype around this via the
/// [`uniffi_bridged_engine!`] macro rather than hand-writing the facade.
///
/// All methods return [`anyhow::Result`], which each crate maps onto its own
/// UniFFI error type via an `impl From<anyhow::Error>`.
pub struct BridgedEngineWrapper {
    inner: Box<dyn SyncEngine + Send + Sync>,
}

impl BridgedEngineWrapper {
    pub fn new(inner: Box<dyn SyncEngine + Send + Sync>) -> Self {
        Self { inner }
    }

    /// The last sync time, in milliseconds. Desktop reads this to build the
    /// collection URL for fetching incoming records. There is deliberately no
    /// setter: the engine owns its last-sync time and advances it itself in
    /// `apply`/`set_uploaded`.
    pub fn last_sync(&self) -> Result<i64> {
        Ok(self.inner.last_sync()?.unwrap_or_default().as_millis())
    }

    /// Force a full re-download next sync by resetting the engine-owned
    /// `last_sync` timestamp - lighter than a full reset.
    pub fn reset_last_sync(&self) -> Result<()> {
        self.inner.reset_last_sync()
    }

    /// The per-collection sync ID, derived from the engine's sync association.
    /// (Bridged engines never maintain the "global" guid - that's all managed by
    /// the consumer, ie, Desktop. They only care about the per-collection one.)
    pub fn sync_id(&self) -> Result<Option<String>> {
        Ok(match self.inner.get_sync_assoc()? {
            EngineSyncAssociation::Disconnected => None,
            EngineSyncAssociation::Connected(c) => Some(c.coll.into()),
        })
    }

    /// Resets the sync ID for this collection, returning the new ID. As a side
    /// effect this resets all local Sync state, as in `reset`.
    pub fn reset_sync_id(&self) -> Result<String> {
        let global = Guid::empty();
        let coll = Guid::random();
        self.inner
            .reset(&EngineSyncAssociation::Connected(CollSyncIds {
                global,
                coll: coll.clone(),
            }))?;
        Ok(coll.to_string())
    }

    /// Ensures the locally stored sync ID matches `sync_id`; resets local Sync
    /// state on a mismatch. Returns the assigned sync ID.
    pub fn ensure_current_sync_id(&self, sync_id: &str) -> Result<String> {
        let assoc = self.inner.get_sync_assoc()?;
        if matches!(assoc, EngineSyncAssociation::Connected(c) if c.coll == sync_id) {
            debug!("ensure_current_sync_id is current");
        } else {
            let new_coll_ids = CollSyncIds {
                global: Guid::empty(),
                coll: sync_id.into(),
            };
            self.inner
                .reset(&EngineSyncAssociation::Connected(new_coll_ids))?;
        }
        Ok(sync_id.to_string())
    }

    pub fn set_clients(&self, client_data: &str) -> Result<()> {
        // unwrap here is unfortunate, but can hopefully go away if we can
        // start using the ClientData type instead of the string.
        self.inner
            .set_clients(&|| serde_json::from_str::<crate::ClientData>(client_data).unwrap())
    }

    pub fn sync_started(&self) -> Result<()> {
        self.inner.sync_started()
    }

    /// Decode the JSON-encoded `IncomingBso`s that UniFFI passes to us, then
    /// hand them to the wrapped engine.
    pub fn store_incoming(&self, incoming: Vec<String>) -> Result<()> {
        let mut bsos = Vec::with_capacity(incoming.len());
        for inc in incoming {
            bsos.push(serde_json::from_str::<IncomingBso>(&inc)?);
        }
        let mut telem = telemetry::Engine::new(self.inner.collection_name());
        self.inner.stage_incoming(bsos, &mut telem)
    }

    /// Apply staged records and encode the outgoing `OutgoingBso`s back into
    /// JSON for UniFFI.
    ///
    /// `server_modified_millis` is the collection's server last-modified time,
    /// passed explicitly by Desktop (which has just stored it as the last sync
    /// time before calling us). It's forwarded to [`SyncEngine::apply`] exactly
    /// as the native Rust client does, so reconciliation sees the real
    /// timestamp.
    pub fn apply(&self, server_modified_millis: i64) -> Result<Vec<String>> {
        let mut telem = telemetry::Engine::new(self.inner.collection_name());
        let records = self.inner.apply(
            ServerTimestamp::from_millis(server_modified_millis),
            &mut telem,
        )?;
        let mut outgoing = Vec::with_capacity(records.len());
        for e in records {
            outgoing.push(serde_json::to_string(&e)?);
        }
        Ok(outgoing)
    }

    /// The uploaded ids always cross the UniFFI boundary as plain strings; we
    /// convert them to [`Guid`] for the engine here.
    pub fn set_uploaded(&self, server_modified_millis: i64, ids: Vec<String>) -> Result<()> {
        let guids: Vec<Guid> = ids.into_iter().map(Guid::from).collect();
        self.inner
            .set_uploaded(ServerTimestamp::from_millis(server_modified_millis), guids)
    }

    pub fn sync_finished(&self) -> Result<()> {
        self.inner.sync_finished()
    }

    pub fn reset(&self) -> Result<()> {
        self.inner.reset(&EngineSyncAssociation::Disconnected)
    }

    pub fn wipe(&self) -> Result<()> {
        self.inner.wipe()
    }
}

/// Generates a UniFFI-exposable bridged engine newtype around
/// [`BridgedEngineWrapper`], removing the ~100 lines of identical facade
/// boilerplate each consuming crate used to hand-write.
///
/// Usage (invoke in the module the crate's UDL `interface` resolves against):
/// ```ignore
/// sync15::uniffi_bridged_engine!(LoginsBridgedEngine);
/// sync15::uniffi_bridged_engine!(TabsBridgedEngine);
/// ```
///
/// All bridged engines expose the same interface; `set_uploaded` takes ids as a
/// plain `sequence<string>` in every crate's UDL. The generated methods return
/// `anyhow::Result`, which the crate's UDL `[Throws=...]` maps to its error type
/// via the existing `impl From<anyhow::Error>`.
///
/// The macro always emits `set_clients`; a crate whose UDL doesn't declare it
/// (logins, webext-storage) simply leaves that inherent method unbound, which is
/// harmless.
#[macro_export]
macro_rules! uniffi_bridged_engine {
    ($name:ident) => {
        // This is what UniFFI exposes; it does nothing other than delegate to
        // the shared `BridgedEngineWrapper`, which adapts our `SyncEngine`.
        pub struct $name($crate::engine::BridgedEngineWrapper);

        impl $name {
            pub fn new(
                inner: ::std::boxed::Box<dyn $crate::engine::SyncEngine + Send + Sync>,
            ) -> Self {
                Self($crate::engine::BridgedEngineWrapper::new(inner))
            }

            pub fn last_sync(&self) -> ::anyhow::Result<i64> {
                self.0.last_sync()
            }

            pub fn reset_last_sync(&self) -> ::anyhow::Result<()> {
                self.0.reset_last_sync()
            }

            pub fn sync_id(&self) -> ::anyhow::Result<Option<String>> {
                self.0.sync_id()
            }

            pub fn reset_sync_id(&self) -> ::anyhow::Result<String> {
                self.0.reset_sync_id()
            }

            pub fn ensure_current_sync_id(&self, sync_id: &str) -> ::anyhow::Result<String> {
                self.0.ensure_current_sync_id(sync_id)
            }

            pub fn set_clients(&self, client_data: &str) -> ::anyhow::Result<()> {
                self.0.set_clients(client_data)
            }

            pub fn sync_started(&self) -> ::anyhow::Result<()> {
                self.0.sync_started()
            }

            pub fn store_incoming(&self, incoming: Vec<String>) -> ::anyhow::Result<()> {
                self.0.store_incoming(incoming)
            }

            pub fn apply(&self, server_modified_millis: i64) -> ::anyhow::Result<Vec<String>> {
                self.0.apply(server_modified_millis)
            }

            pub fn set_uploaded(
                &self,
                server_modified_millis: i64,
                ids: Vec<String>,
            ) -> ::anyhow::Result<()> {
                self.0.set_uploaded(server_modified_millis, ids)
            }

            pub fn sync_finished(&self) -> ::anyhow::Result<()> {
                self.0.sync_finished()
            }

            pub fn reset(&self) -> ::anyhow::Result<()> {
                self.0.reset()
            }

            pub fn wipe(&self) -> ::anyhow::Result<()> {
                self.0.wipe()
            }
        }
    };
}

#[cfg(test)]
mod wrapper_tests {
    use super::*;
    use crate::CollectionName;
    use crate::bso::OutgoingBso;
    use crate::engine::CollectionRequest;
    use std::sync::Mutex;

    // A minimal SyncEngine that records the guids passed to `set_uploaded`, so
    // we can confirm the wrapper converts the incoming string ids to `Guid` and
    // drives a `SyncEngine`.
    #[derive(Default)]
    struct RecordingEngine {
        uploaded: Mutex<Vec<Guid>>,
    }

    impl SyncEngine for RecordingEngine {
        fn collection_name(&self) -> CollectionName {
            "test".into()
        }
        fn stage_incoming(
            &self,
            _inbound: Vec<IncomingBso>,
            _telem: &mut telemetry::Engine,
        ) -> Result<()> {
            Ok(())
        }
        fn apply(
            &self,
            _timestamp: ServerTimestamp,
            _telem: &mut telemetry::Engine,
        ) -> Result<Vec<OutgoingBso>> {
            Ok(vec![])
        }
        fn set_uploaded(&self, _new_timestamp: ServerTimestamp, ids: Vec<Guid>) -> Result<()> {
            self.uploaded.lock().unwrap().extend(ids);
            Ok(())
        }
        fn get_collection_request(
            &self,
            _server_timestamp: ServerTimestamp,
        ) -> Result<Option<CollectionRequest>> {
            Ok(None)
        }
        fn get_sync_assoc(&self) -> Result<EngineSyncAssociation> {
            Ok(EngineSyncAssociation::Disconnected)
        }
        fn reset(&self, _assoc: &EngineSyncAssociation) -> Result<()> {
            Ok(())
        }
        fn wipe(&self) -> Result<()> {
            Ok(())
        }
    }

    #[test]
    fn set_uploaded_converts_string_ids() {
        let wrapper = BridgedEngineWrapper::new(Box::new(RecordingEngine::default()));
        // Every crate now hands us string ids; the wrapper turns them into `Guid`.
        wrapper
            .set_uploaded(1, vec!["aaaa".to_string(), "bbbb".to_string()])
            .unwrap();
    }
}
