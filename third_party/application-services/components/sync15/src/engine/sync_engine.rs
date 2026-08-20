/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::CollectionRequest;
use crate::bso::{IncomingBso, OutgoingBso};
use crate::client_types::ClientData;
use crate::{CollectionName, Guid, ServerTimestamp, telemetry};
use anyhow::Result;
use std::fmt;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct CollSyncIds {
    pub global: Guid,
    pub coll: Guid,
}

/// Defines how an engine is associated with a particular set of records
/// on a sync storage server. It's either disconnected, or believes it is
/// connected with a specific set of GUIDs. If the server and the engine don't
/// agree on the exact GUIDs, the engine will assume something radical happened
/// so it can't believe anything it thinks it knows about the state of the
/// server (ie, it will "reset" then do a full reconcile)
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum EngineSyncAssociation {
    /// This store is disconnected (although it may be connected in the future).
    Disconnected,
    /// Sync is connected, and has the following sync IDs.
    Connected(CollSyncIds),
}

/// The concrete `SyncEngine` implementations
#[derive(Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord)]
pub enum SyncEngineId {
    // Note that we've derived PartialOrd etc, which uses lexicographic ordering
    // of the variants. We leverage that such that the higher priority engines
    // are listed first.
    // This order matches desktop.
    Passwords,
    Tabs,
    Bookmarks,
    Addresses,
    CreditCards,
    History,
}

impl SyncEngineId {
    // Iterate over all possible engines. Note that we've made a policy decision
    // that this should enumerate in "order" as defined by PartialCmp, and tests
    // enforce this.
    pub fn iter() -> impl Iterator<Item = SyncEngineId> {
        [
            Self::Passwords,
            Self::Tabs,
            Self::Bookmarks,
            Self::Addresses,
            Self::CreditCards,
            Self::History,
        ]
        .into_iter()
    }

    // Get the string identifier for this engine.  This must match the strings in SyncEngineSelection.
    pub fn name(&self) -> &'static str {
        match self {
            Self::Passwords => "passwords",
            Self::History => "history",
            Self::Bookmarks => "bookmarks",
            Self::Tabs => "tabs",
            Self::Addresses => "addresses",
            Self::CreditCards => "creditcards",
        }
    }
}

impl fmt::Display for SyncEngineId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.name())
    }
}

impl TryFrom<&str> for SyncEngineId {
    type Error = String;

    fn try_from(value: &str) -> std::result::Result<Self, Self::Error> {
        match value {
            "passwords" => Ok(Self::Passwords),
            "history" => Ok(Self::History),
            "bookmarks" => Ok(Self::Bookmarks),
            "tabs" => Ok(Self::Tabs),
            "addresses" => Ok(Self::Addresses),
            "creditcards" => Ok(Self::CreditCards),
            _ => Err(value.into()),
        }
    }
}

/// A "sync engine" is a thing that knows how to sync. It's often implemented
/// by a "store" (which is the generic term responsible for all storage
/// associated with a component, including storage required for sync.)
///
/// The model described by this trait is that engines first "stage" sets of incoming records,
/// then apply them returning outgoing records, then handle the success (or otherwise) of each
/// batch as it's uploaded.
///
/// Staging incoming records is (or should be ;) done in batches - eg, 1000 record chunks.
/// Some engines will "stage" these into a database temp table, while ones expecting less records
/// might just store them in memory.
///
/// For outgoing records, a single vec is supplied by the engine. The sync client will use the
/// batch facilities of the server to make multiple POST requests and commit them.
/// Sadly it's not truly atomic (there's a batch size limit) - so the model reflects that in that
/// the engine gets told each time a batch is committed, which might happen more than once for the
/// supplied vec. We should upgrade this model so the engine can avoid reading every outgoing
/// record into memory at once (ie, we should try and better reflect the upload batch model at
/// this level)
///
/// Sync Engines should not assume they live for exactly one sync, so `sync_started()` should
/// clean up any state, including staged records, from previous syncs.
///
/// Different engines will produce errors of different types.  To accommodate
/// this, we force them all to return anyhow::Error.
pub trait SyncEngine {
    fn collection_name(&self) -> CollectionName;

    /// Indicates that a sync is starting.
    fn sync_started(&self) -> Result<()> {
        Ok(())
    }

    /// Supplies the engine with the current set of Sync clients (ie, other
    /// devices connected to the account). Might be called at any time.
    fn set_clients(&self, _get_client_data: &dyn Fn() -> ClientData) -> Result<()> {
        Ok(())
    }

    /// Tells the engine what the local encryption key is for the data managed
    /// by the engine. This is only used by collections that store data
    /// encrypted locally and is unrelated to the encryption used by Sync.
    /// The intent is that for such collections, this key can be used to
    /// decrypt local data before it is re-encrypted by Sync and sent to the
    /// storage servers, and similarly, data from the storage servers will be
    /// decrypted by Sync, then encrypted by the local encryption key before
    /// being added to the local database.
    ///
    /// The expectation is that the key value is being maintained by the
    /// embedding application in some secure way suitable for the environment
    /// in which the app is running - eg, the OS "keychain". The value of the
    /// key is implementation dependent - it is expected that the engine and
    /// embedding application already have some external agreement about how
    /// to generate keys and in what form they are exchanged. Finally, there's
    /// an assumption that sync engines are short-lived and only live for a
    /// single sync - this means that sync doesn't hold on to the key for an
    /// extended period. In practice, all sync engines which aren't a "bridged
    /// engine" are short lived - we might need to rethink this later if we need
    /// engines with local encryption keys to be used on desktop.
    ///
    /// This will panic if called by an engine that doesn't have explicit
    /// support for local encryption keys as that implies a degree of confusion
    /// which shouldn't be possible to ignore.
    fn set_local_encryption_key(&mut self, _key: &str) -> Result<()> {
        unimplemented!("This engine does not support local encryption");
    }

    /// Stage some incoming records. This might be called multiple times in the same sync
    /// if we fetch the incoming records in batches.
    ///
    /// Note there is no timestamp provided here, because the procedure for fetching in batches
    /// means that the timestamp advancing during a batch means we must abort and start again.
    /// The final collection timestamp after staging all records is supplied to `apply()`
    fn stage_incoming(
        &self,
        inbound: Vec<IncomingBso>,
        telem: &mut telemetry::Engine,
    ) -> Result<()>;

    /// Apply the staged records, returning outgoing records.
    /// Ideally we would adjust this model to better support batching of outgoing records
    /// without needing to keep them all in memory (ie, an iterator or similar?)
    fn apply(
        &self,
        timestamp: ServerTimestamp,
        telem: &mut telemetry::Engine,
    ) -> Result<Vec<OutgoingBso>>;

    /// Indicates that the given record IDs were uploaded successfully to the server.
    /// This may be called multiple times per sync, once for each batch. Batching is determined
    /// dynamically based on payload sizes and counts via the server's advertised limits.
    fn set_uploaded(&self, new_timestamp: ServerTimestamp, ids: Vec<Guid>) -> Result<()>;

    /// Called once the sync is finished. Not currently called if uploads fail (which
    /// seems sad, but the other batching confusion there needs sorting out first).
    /// Many engines will have nothing to do here, as most "post upload" work should be
    /// done in `set_uploaded()`
    fn sync_finished(&self) -> Result<()> {
        Ok(())
    }

    /// The engine is responsible for building a single collection request. Engines
    /// typically will store a lastModified timestamp and use that to build a
    /// request saying "give me full records since that date" - however, other
    /// engines might do something fancier. It can return None if the server timestamp
    /// has not advanced since the last sync.
    /// This could even later be extended to handle "backfills", and we might end up
    /// wanting one engine to use multiple collections (eg, as a "foreign key" via guid), etc.
    fn get_collection_request(
        &self,
        server_timestamp: ServerTimestamp,
    ) -> Result<Option<CollectionRequest>>;

    /// Get persisted sync IDs. If they don't match the global state we'll be
    /// `reset()` with the new IDs.
    fn get_sync_assoc(&self) -> Result<EngineSyncAssociation>;

    /// Reset the engine (and associated store) without wiping local data,
    /// ready for a "first sync".
    /// `assoc` defines how this store is to be associated with sync.
    fn reset(&self, assoc: &EngineSyncAssociation) -> Result<()>;

    /// Wipes the engine's local data.
    /// Triggered by a client command (only bookmarks at time of writing),
    /// or to wipe local data when disconnecting (currently only on desktop
    /// via a bridged-engine).
    fn wipe(&self) -> Result<()>;

    // A couple of desktop specific "bridged engine" helpers, where the
    // last-modified timestamps for collections are handled differently;
    // who does the `get_collection_request()` etc impacts the owner of the
    // timestamp.
    // Engines should do both or neither, longer term it should be absorbed.
    /// Return the last sync timestamp, only called for bridged engines.
    fn last_sync(&self) -> Result<Option<ServerTimestamp>> {
        unimplemented!("This engine is not used as a bridged engine");
    }

    /// Reset the last sync timestampf or the engine, only called for bridged engines.
    fn reset_last_sync(&self) -> Result<()> {
        unimplemented!("This engine is not used as a bridged engine");
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use std::iter::zip;

    #[test]
    fn test_engine_priority() {
        fn sorted(mut engines: Vec<SyncEngineId>) -> Vec<SyncEngineId> {
            engines.sort();
            engines
        }
        assert_eq!(
            vec![SyncEngineId::Passwords, SyncEngineId::Tabs],
            sorted(vec![SyncEngineId::Passwords, SyncEngineId::Tabs])
        );
        assert_eq!(
            vec![SyncEngineId::Passwords, SyncEngineId::Tabs],
            sorted(vec![SyncEngineId::Tabs, SyncEngineId::Passwords])
        );
    }

    #[test]
    fn test_engine_enum_order() {
        let unsorted = SyncEngineId::iter().collect::<Vec<SyncEngineId>>();
        let mut sorted = SyncEngineId::iter().collect::<Vec<SyncEngineId>>();
        sorted.sort();

        // iterating should supply identical elements in each.
        assert!(zip(unsorted, sorted).fold(true, |acc, (a, b)| acc && (a == b)))
    }
}
