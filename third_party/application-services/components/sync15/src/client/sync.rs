/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::{CollectionUpdate, GlobalState, LocalCollStateMachine, Sync15StorageClient};
use crate::KeyBundle;
use crate::clients_engine;
use crate::engine::SyncEngine;
use crate::error::{Error, info, warn};
use crate::telemetry;
use interrupt_support::Interruptee;

#[allow(clippy::too_many_arguments)]
pub fn synchronize_with_clients_engine(
    client: &Sync15StorageClient,
    global_state: &GlobalState,
    root_sync_key: &KeyBundle,
    clients: Option<&clients_engine::Engine<'_>>,
    engine: &dyn SyncEngine,
    fully_atomic: bool,
    telem_engine: &mut telemetry::Engine,
    interruptee: &dyn Interruptee,
) -> Result<(), Error> {
    let collection = engine.collection_name();
    info!("Syncing collection {}", collection);

    // our global state machine is ready - get the collection machine going.
    let coll_state = match LocalCollStateMachine::get_state(engine, global_state, root_sync_key)? {
        Some(coll_state) => coll_state,
        None => {
            // XXX - this is either "error" or "declined".
            warn!(
                "can't setup for the {} collection - hopefully it works later",
                collection
            );
            return Ok(());
        }
    };

    engine.sync_started()?;
    if let Some(clients) = clients {
        engine.set_clients(&|| clients.get_client_data())?;
    }
    interruptee.err_if_interrupted()?;
    // We assume an "engine" manages exactly one "collection" with the engine's name.
    match engine.get_collection_request(coll_state.last_modified)? {
        None => {
            info!("skipping incoming for {} - not needed.", collection);
        }
        Some(collection_request) => {
            // Ideally we would "batch" incoming records (eg, fetch just 1000 at a time)
            // and ask the engine to "stage" them as they come in - but currently we just read
            // them all in one request.

            // Doing this batching will involve specifying a "limit=" param and
            // "x-if-unmodified-since" for each request, looking for an
            // "X-Weave-Next-Offset header in the response and using that in subsequent
            // requests.
            // See https://mozilla-services.readthedocs.io/en/latest/storage/apis-1.5.html#syncstorage-paging
            //
            // But even if we had that, we need to deal with a 412 response on a subsequent batch,
            // so we can't know if we've staged *every* record for that timestamp; the next
            // sync must use an earlier one.
            //
            // For this reason, an engine can't really trust a server timestamp until the
            // very end when we know we've staged them all.
            let incoming = super::fetch_incoming(client, &coll_state, collection_request)?;
            info!("Downloaded {} remote changes", incoming.len());
            engine.stage_incoming(incoming, telem_engine)?;
            interruptee.err_if_interrupted()?;
        }
    };

    // Should consider adding a new `fetch_outgoing()` and having `apply()` only apply.
    // It *might* even make sense to only call `apply()` when something was staged,
    // but that's not clear - see the discussion at
    // https://github.com/mozilla/application-services/pull/5441/files/f36274f455a6299f10e7ce56b167882c369aa806#r1189267540
    info!("Applying changes");
    let outgoing = engine.apply(coll_state.last_modified, telem_engine)?;
    interruptee.err_if_interrupted()?;

    // XXX - this upload strategy is buggy due to batching. With enough records, we will commit
    // 2 batches on the server. If the second fails, we get an Err<> here, so can't tell the
    // engine about the successful server batch commit.
    // Most stuff below should be called per-batch rather than at the successful end of all
    // batches, but that's not trivial.
    info!("Uploading {} outgoing changes", outgoing.len());
    let upload_info = CollectionUpdate::new_from_changeset(
        client,
        &coll_state,
        collection,
        outgoing,
        fully_atomic,
    )?
    .upload()?;
    info!(
        "Upload success ({} records success, {} records failed)",
        upload_info.successful_ids.len(),
        upload_info.failed_ids.len()
    );

    let mut telem_outgoing = telemetry::EngineOutgoing::new();
    telem_outgoing.sent(upload_info.successful_ids.len() + upload_info.failed_ids.len());
    telem_outgoing.failed(upload_info.failed_ids.len());
    telem_engine.outgoing(telem_outgoing);

    engine.set_uploaded(upload_info.modified_timestamp, upload_info.successful_ids)?;

    // The above should all be per-batch :(

    engine.sync_finished()?;

    info!("Sync finished!");
    Ok(())
}
