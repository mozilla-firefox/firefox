/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * Cached metadata about an active Nimbus experiment or rollout.
 *
 * This metadata is stored inside prefs of the form `nimbus.syncdatastore.*` and
 * `nimbus.syncdefaultsstore.*` by ExperimentStore.sys.mjs for a subset of
 * features that require either:
 * - being accessible from platform code, or
 * - being read very early in startup, before Nimbus has fully initialized.
 */
[GenerateInitFromJSON]
dictionary CachedNimbusExperimentMetadata {
  /** The experiment slug. */
  required UTF8String slug;

  /** The active branch. */
  required CachedNimbusExperimentBranch branch;
};

/**
 * Cached metadata about the active branch of an experiment or rollout.
 */
[GenerateInitFromJSON]
dictionary CachedNimbusExperimentBranch {
  /** The slug of the active branch. */
  required UTF8String slug;
};
