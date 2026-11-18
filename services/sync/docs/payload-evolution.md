# Handling the evolution of Sync payloads

(Note that this document has been written in the format of an [application-services ADR](https://github.com/mozilla/application-services/blob/main/docs/adr/0000-use-markdown-architectural-decision-records.md)
but the relevant teams decided that ultimately the best home for this doc is in the Firefox repository)

* Status: Accepted
* Deciders: sync team, credentials management team
* Date: 2023-03-15

Technical Story:
* https://github.com/mozilla/application-services/pull/5434
* https://docs.google.com/document/d/1ToLOERA5HKzEzRVZNv6Ohv_2wZaujW69pVb1Kef2jNY

## Context and Problem Statement

Sync exists on all platforms (Desktop, Android, iOS), all channels (Nightly, Beta, Release, ESR) and is heavily used across all Firefox features.
Whenever there are feature changes or requests that potentially involve schema changes, there are not a lot of good options to ensure sync doesn’t break for any specific client.
Since sync data is synced from all channels, we need to make sure each client can handle the new data and that all channels can support the new schema.
Issues like [credit card failing on android and desktop release channels due to schema change on desktop Nightly](https://bugzilla.mozilla.org/show_bug.cgi?id=1812235)
are examples of such cases we can run into.
This document describes our decision on how we will support payload evolution over time.

Note that even though this document exists in the application-services repository, it should
be considered to apply to all sync implementations, whether in this repository, in the Firefox repository,
or anywhere else.

## Definitions

* A "new" Firefox installation is a version of Firefox which has a change to a Sync payload which
  is not yet understood by "recent" versions. The most common example would be a Nightly version
  of Firefox with a new feature not yet on the release channel.

* A "recent" Firefox installation is a version older than a "new" version, which does not understand
  or have support for new features in "new" versions, but which we still want to support without
  breakage and without the user perceiving data-loss. This is typically accepted to mean the
  current ESR version or later, but taking into account the slow update when new ESRs are released.

* An "old" version is any version before what we consider "recent".


## Decision Drivers

* It must be possible to change what data is carried by Sync to meet future product requirements.
* Both desktop and mobile platforms must be considered.
* We must not break "recent" Firefox installations when a "new" Firefox installation syncs, and vice-versa.
* Round-tripping data from a "new" Firefox installation through a "recent" Firefox installation must not discard any of the new data, and vice-versa.
* Some degree of breakage for "old" Firefox installations when "new" or "recent" firefoxes sync
  might be considered acceptable if absolutely necessary.
* However, breakage of "new" or "recent" Firefoxes when an "old" version syncs is *not* acceptable.
* Because such evolution should be rare, we do not want to set an up-front policy about locking out
  "old" versions just because they might have a problem in the future. That is, we want to avoid
  a policy that dictates versions more than (say) 2 years old will break when syncing "just in case"
* Any solution to this must be achievable in a relatively short timeframe as we know of product
  asks coming down the line which require this capability.

## Considered Options

* A backwards compatible schema policy, consisting of  (a) having engines "round trip" data they
  do not know about and (b) never changing the semantics of existing data.
* A policy which prevents "recent" clients from syncing, or editing data, or other restrictions.
* A formal schema-driven process.
* Consider the sync payloads frozen and never change them.
* Use separate collections for new data

## Decision Outcome

Chosen option: A backwards compatible schema policy because it is very flexible and the only option
meeting the decision drivers.

## Pros and Cons of the Options

### A backwards compatible schema policy

A summary of this option is a policy by which:

* Every sync engine must arrange to persist any fields from the payload which it
  does not understand. The next time that engine needs to upload that record to the storage server,
  it must arrange to add all such "unknown" fields back into the payload.

* Different engines must identify different locations where this might happen. For example, the
 `passwords` engine would identify the "root" of the payload, `addresses` and `creditcards` would
 identify the `entry` sub-object in the payload, while the history engine would probably identify
 *both* the root of the payload and the `visits` array.

* Fields can not change type, nor be removed for a significant amount of time. This might mean
  that "new" clients must support both new fields *and* fields which are considered deprecated
  by these "new" clients because they are still used by "recent" versions.

The pros and cons:

* Good, because it meets the requirements.

* Good, because the initial set of work identified is relatively simple to implement (that work
  specifically is to support the round-tripping of "unknown" fields, in the hope that by the
  time actual schema changes are proposed, this round-trip capability will then be on all "recent"
  versions)

* Bad, because the inability to deprecate or change existing fields means that
  some evolution tasks become complicated. For example, consider a hypothetical change where
  we wanted to change from "street/city/state" fields into a free-form "address" field. New
  Firefox versions would need to populate *both* new and old fields when writing to the server,
  and handle the fact that only the old versions might be updated when it sees an incoming
  record written by a "recent" or "old" versions of Firefox. However, this should be rare.

* Bad, because it's not possible to prove a proposed change meets the requirements - the policy
  is informal and requires good judgement as changes are proposed.

### A policy which prevents "recent" clients from syncing, or editing data

Proposals which fit into this category might have been implemented by (say) adding
a version number to the schema, and if clients did not fully understand the schema it would
either prevent syncing the record, or sync it but not allow editing it, or similar.

This was rejected because:

* The user would certainly perceive data-loss if we ignored the incoming data entirely.
* If we still wanted older versions to "partially" see the record (eg, but disallow editing) we'd
  still need most of the chosen option anyway - specifically, we could still never
  deprecate fields etc.
* The UI/UX of trying to explain to the user why they can't edit a record was deemed impossible
  to do in a satisfactory way.
* This would effectively penalize users who chose to use Nightly Firefoxes in any way. Simply
  allowing a Nightly to sync would effectively break Release/Mobile Firefox versions.

### A formal schema-driven process.

Ideally we could formally describe schemas, but we can't come up with anything here which
works with the constraints of supporting older clients - we simply can't update older released
Firefoxes so they know how to work with the new schemas. We also couldn't come up with a solution
where a schema is downloaded dynamically which also allowed the *semantics* (as opposed to simply
validity) of new fields to be described.

### Consider the sync payloads frozen and never change them.

A process where payloads are frozen was rejected because:

* The most naive approach here would not meet the needs of Firefox in the future.

* A complicated system where we started creating new payload and new collections
  (ie, freezing "old" schemas but then creating "new" schemas only understood by
  newer clients) could not be conceived in a way that still met the requirements,
  particularly around data-loss for older clients. For example, adding a credit-card
  on a Nightly version but having it be completely unavailable on a release firefox
  isn't acceptable.

### Use separate collections for new data

We could store the new data in a separate collection. For example define a
bookmarks2 collection where each record has the same guid as one in bookmarks alongside any new fields.
Newer clients use both collections to sync.

The pros and cons:

* Good, because it allows newer clients to sync new data without affecting recent or older clients
* Bad, because sync writes would lose atomicity without server changes.
  We can currently write to a single collection in an atomic way, but don't have a way to write to multiple collections.
* Bad because this number of collections grows each time we want to add fields.
* Bad because it potentially leaks extra information to an attacker that gets access to the encrypted server records.
  For example if we added a new collection for a single field, then the attacker could guess if that
  field was set or not based on the size of the encrypted record.
* Bad because it's difficult to handle nested data with this approach,
  for example adding a field to a history record visit.
* Bad because it has the same issue with dependent data as the chosen solution.

## Links <!-- optional -->

* This document was originally [brain-stormed in this google docs document](https://docs.google.com/document/d/1ToLOERA5HKzEzRVZNv6Ohv_2wZaujW69pVb1Kef2jNY),
  which may be of interest for historical context, but should not be considered part of this ADR.
