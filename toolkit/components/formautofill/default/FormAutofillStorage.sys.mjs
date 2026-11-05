/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implements an interface of the storage of Form Autofill.
 */

// We expose a singleton from this module. Some tests may import the
// constructor via the system global.
import {
  AddressesBase,
  CreditCardsBase,
  FormAutofillStorageBase,
} from "resource://autofill/FormAutofillStorageBase.sys.mjs";
import { AppConstants } from "resource://gre/modules/AppConstants.sys.mjs";
import { FormAutofill } from "resource://autofill/FormAutofill.sys.mjs";
import { XPCOMUtils } from "resource://gre/modules/XPCOMUtils.sys.mjs";

const lazy = {};

ChromeUtils.defineESModuleGetters(lazy, {
  AutofillDataTypes: "resource://gre/modules/shared/AutofillDataTypes.sys.mjs",
  CreditCard: "resource://gre/modules/CreditCard.sys.mjs",
  AddressStorageMigrator: "resource://autofill/AutofillStorageMigrator.sys.mjs",
  CreditCardStorageMigrator:
    "resource://autofill/AutofillStorageMigrator.sys.mjs",
  JSONFile: "resource://gre/modules/JSONFile.sys.mjs",
  LoginHelper: "resource://gre/modules/LoginHelper.sys.mjs",
  OSKeyStore: "resource://gre/modules/OSKeyStore.sys.mjs",
  Passports: "resource://autofill/PassportStorage.sys.mjs",
  RustAutofillAddressesAdapter:
    "resource://autofill/RustAutofillAddressStorage.sys.mjs",
  RustAutofillCreditCardsAdapter:
    "resource://autofill/RustAutofillCreditCardStorage.sys.mjs",
  RustAutofillStore: "resource://autofill/RustAutofillStore.sys.mjs",
});

ChromeUtils.defineLazyGetter(lazy, "logger", () =>
  console.createInstance({
    logModule: "FormAutofillStorage",
    maxLogLevelPref: "extensions.formautofill.loglevel",
  })
);

const PROFILE_JSON_FILE_NAME = "autofill-profiles.json";

// Intent: which store should serve addresses. Read at startup and watched
// afterwards, so flipping it copies the addresses over to the other store and
// serves them from there.
const ADDRESS_RUST_ENABLED_PREF =
  "extensions.formautofill.addresses.storage.rust.enabled";

// State, not intent: whether the Rust store is the one serving addresses.
// Managed by Firefox rather than by the user -- the pref above only asks, and a
// profile whose copy has not completed keeps reading from where its addresses
// are. Sync reads it to pick between the two engines.
const ADDRESS_RUST_ACTIVE_PREF =
  "extensions.formautofill.addresses.storage.rust.active";

// Dry-run the migration for telemetry while enabled is still false: the copy is
// verified, reported and then wiped.
const ADDRESS_RUST_MIGRATION_TEST_PREF =
  "extensions.formautofill.addresses.storage.rust.runMigrationTest";

// The credit card counterparts of the three above. Kept separate rather than
// derived from a collection name so that each can be flipped on its own: the
// two migrations are independent and a profile can have moved one and not the
// other.
const CREDIT_CARD_RUST_ENABLED_PREF =
  "extensions.formautofill.creditCards.storage.rust.enabled";

const CREDIT_CARD_RUST_ACTIVE_PREF =
  FormAutofill.CREDITCARDS_RUST_STORAGE_ACTIVE_PREF;

const CREDIT_CARD_RUST_MIGRATION_TEST_PREF =
  "extensions.formautofill.creditCards.storage.rust.runMigrationTest";

// Sent where a credit card action has finished and left nothing pending: its
// writes have landed, and nothing still holds a collection to write to later.
// A copy that waits for the OS key store starts at the first one sent while the
// key store is unlocked; see #maybeRunMigration.
export const MIGRATION_SAFE_POINT_TOPIC = "formautofill-migration-safe-point";

// What #maybeRunMigration needs of each collection it moves, by data type. The
// migrator is looked up rather than held so that its module still loads only
// once a copy is wanted; its config names the collection's Rust adapter.
ChromeUtils.defineLazyGetter(lazy, "RUST_MIGRATIONS", () => ({
  [lazy.AutofillDataTypes.ADDRESS]: {
    enabledPref: ADDRESS_RUST_ENABLED_PREF,
    activePref: ADDRESS_RUST_ACTIVE_PREF,
    testPref: ADDRESS_RUST_MIGRATION_TEST_PREF,
    migrator: () => lazy.AddressStorageMigrator,
    hasEncryption: false,
  },
  [lazy.AutofillDataTypes.CREDIT_CARD]: {
    enabledPref: CREDIT_CARD_RUST_ENABLED_PREF,
    activePref: CREDIT_CARD_RUST_ACTIVE_PREF,
    testPref: CREDIT_CARD_RUST_MIGRATION_TEST_PREF,
    migrator: () => lazy.CreditCardStorageMigrator,
    // The card number: a copy needs a key at each end.
    hasEncryption: true,
  },
}));

// Observed rather than read once: the migration sets this mid-session, and a
// snapshot taken before that would keep handing back the JSON collection until
// the next restart.
XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "addressRustActive",
  ADDRESS_RUST_ACTIVE_PREF,
  false
);

XPCOMUtils.defineLazyPreferenceGetter(
  lazy,
  "creditCardRustActive",
  CREDIT_CARD_RUST_ACTIVE_PREF,
  false
);

class Addresses extends AddressesBase {}

class CreditCards extends CreditCardsBase {
  constructor(store) {
    super(store);
  }

  async _encryptNumber(creditCard) {
    if (!("cc-number-encrypted" in creditCard)) {
      if ("cc-number" in creditCard) {
        let ccNumber = creditCard["cc-number"];
        if (lazy.CreditCard.isValidNumber(ccNumber)) {
          creditCard["cc-number"] =
            lazy.CreditCard.getLongMaskedNumber(ccNumber);
        } else {
          // Credit card numbers can be entered on versions of Firefox that don't validate
          // the number and then synced to this version of Firefox. Therefore, mask the
          // full number if the number is invalid on this version.
          creditCard["cc-number"] = "*".repeat(ccNumber.length);
        }
        creditCard["cc-number-encrypted"] =
          await lazy.OSKeyStore.encrypt(ccNumber);
      } else {
        creditCard["cc-number-encrypted"] = "";
      }
    }
  }
}

export class FormAutofillStorage extends FormAutofillStorageBase {
  #initPromise = null;
  // The switch in progress, if any. Kept so the next one queues behind it
  // rather than copying over the same pair of stores at the same time.
  #addressSwitch = null;
  #creditCardSwitch = null;
  // Whether MIGRATION_SAFE_POINT_TOPIC is observed, which it is while a credit
  // card copy waits for the OS key store; see #maybeRunMigration.
  #awaitingSafePoint = false;

  /**
   * Settles which store serves addresses at startup, once the JSON store has
   * loaded, and returns a promise covering both so the "await initialize()"
   * contract still holds. Watching the pref for later changes is part of the
   * same chain, so nothing observes a flip before the profile has settled.
   *
   * Memoized, like the base's own initialize(): a repeat call hands back the
   * same promise rather than running the setup again.
   */
  initialize() {
    this.#initPromise ??= super
      .initialize()
      .then(() => this.#maybeRunMigration(lazy.AutofillDataTypes.ADDRESS))
      .then(() => this.#maybeRunMigration(lazy.AutofillDataTypes.CREDIT_CARD))
      .then(() => {
        Services.prefs.addObserver(ADDRESS_RUST_ENABLED_PREF, this);
        Services.prefs.addObserver(CREDIT_CARD_RUST_ENABLED_PREF, this);
      });
    return this.#initPromise;
  }

  observe(subject, topic, data) {
    if (topic == MIGRATION_SAFE_POINT_TOPIC) {
      // Observed only while a copy waits for the key store, and one safe point
      // with it unlocked is enough.
      if (!lazy.OSKeyStore.isLoggedIn) {
        return;
      }
      Services.obs.removeObserver(this, topic);
      this.#awaitingSafePoint = false;
      this.#queueCreditCardMigration();
      return;
    }
    if (topic != "nsPref:changed") {
      return;
    }
    if (data == ADDRESS_RUST_ENABLED_PREF) {
      this.#queueAddressMigration();
    } else if (data == CREDIT_CARD_RUST_ENABLED_PREF) {
      this.#queueCreditCardMigration();
    }
  }

  _finalize() {
    if (this.#initPromise) {
      Services.prefs.removeObserver(ADDRESS_RUST_ENABLED_PREF, this);
      Services.prefs.removeObserver(CREDIT_CARD_RUST_ENABLED_PREF, this);
    }
    if (this.#awaitingSafePoint) {
      this.#awaitingSafePoint = false;
      Services.obs.removeObserver(this, MIGRATION_SAFE_POINT_TOPIC);
    }
    return super._finalize();
  }

  /**
   * Copy the addresses from the store serving them to the one the pref now asks
   * for, and serve them from there.
   *
   * Queued behind initialize() and behind a switch already running: the two
   * would otherwise copy over the same pair of stores at once, and the second
   * would overwrite what the first had just read.
   *
   * A write that lands while the copy is running goes to the store that is
   * still serving, so it is in the source after the source has been read and
   * may not reach the target. A copy into Rust hears it and fails rather than
   * switching.
   */
  #queueAddressMigration() {
    this.#addressSwitch = (this.#addressSwitch ?? this.initialize())
      .then(() => this.#maybeRunMigration(lazy.AutofillDataTypes.ADDRESS))
      // Caught rather than left to reject: the next flip chains onto this
      // promise, and a rejected one would swallow every switch after it.
      .catch(e => lazy.logger.error("Could not switch the address store", e));
    return this.#addressSwitch;
  }

  // For test only: the switch the last pref flip started, so a test can wait
  // for one that is not going to change anything observable.
  get _addressSwitch() {
    return this.#addressSwitch;
  }

  /**
   * Bring a collection to the store its prefs ask for. Run at startup, on a
   * flip of the collection's enabled pref, and for credit cards when the OS
   * key store unlocks after a deferred copy:
   *
   *  - rust.enabled set, Rust not serving: copy JSON into Rust and switch.
   *  - rust.enabled cleared, Rust serving: copy Rust back to JSON and switch.
   *  - Neither, with runMigrationTest set and a dry run pending: copy JSON into
   *    Rust to measure it, then empty Rust again.
   *
   * A profile whose copy into Rust has spent its attempts stays on JSON:
   * rust.enabled is read as false while the attempts stay spent, and is never
   * written here, since Nimbus unenrols a profile whose setPref pref changes
   * under it. Such a profile is not dry run either.
   *
   * The serving store is the source, and the target ends up matching it.
   * Moving into Rust changes rust.active only after a complete copy, so a
   * failed one leaves the profile on the store that still holds everything.
   * Moving back to JSON always changes it once the copy has been tried, even
   * if the copy failed or the Rust records could not be read: JSON is the
   * store a profile falls back to, so it must not be kept in Rust. Whatever
   * did not make it back is served as JSON last held it.
   *
   * A copy into Rust empties Rust first, as a leftover tombstone would block
   * its guid from being inserted again. A copy back keeps what JSON holds and
   * does not carry Rust's pending deletions, so one not yet uploaded can come
   * back on the next sync.
   *
   * Nothing here catches: a throw at startup would reject initialize() and
   * leave autofill offering nothing. The migrator reports its own failures
   * and returns false instead.
   *
   * @param {string} dataType AutofillDataTypes.ADDRESS or
   *   AutofillDataTypes.CREDIT_CARD.
   */
  async #maybeRunMigration(dataType) {
    const { enabledPref, activePref, testPref, migrator, hasEncryption } =
      lazy.RUST_MIGRATIONS[dataType];
    const { collectionName } = lazy.AutofillDataTypes.get(dataType);
    const Migrator = migrator();
    const active = Services.prefs.getBoolPref(activePref, false);
    const enabledByPref = Services.prefs.getBoolPref(enabledPref, false);
    const testing =
      !enabledByPref && !active && Services.prefs.getBoolPref(testPref, false);
    // Asked only when a copy into Rust is wanted: reading the migrator's config
    // loads the Rust adapter's module.
    const gaveUp =
      !active && (enabledByPref || testing) && Migrator.attemptsExhausted;
    const enabled = enabledByPref && !gaveUp;
    const dryRun = testing && !gaveUp && Migrator.dryRunPending;
    if (gaveUp && enabledByPref) {
      lazy.logger.log(
        `Not migrating ${collectionName}: every attempt already failed.`
      );
    }
    if (enabled == active && !dryRun) {
      return;
    }

    // The JSON store is the source of every copy but the one back, so for a
    // copy into Rust, the Rust store -- building its adapter starts opening
    // autofill.db -- is reached only once this profile is known to want a copy
    // and able to make it.
    const intoRust = !active;
    const json =
      dataType == lazy.AutofillDataTypes.CREDIT_CARD
        ? this.#jsonCreditCards()
        : this.#jsonAddresses();
    const source = intoRust ? json : Migrator.config.rustAdapter.getInstance();

    // A copy of encrypted records needs a key at each end, and must not ask
    // the user for either. The Rust store's key is NSS's, so with a primary
    // password set nothing is copied into Rust, whether or not the token is
    // unlocked. A copy back is still allowed, so turning the enabled pref off
    // can return such a profile to JSON, but only while the token is unlocked:
    // a locked one would read every record back without its values.
    if (hasEncryption && lazy.LoginHelper.isPrimaryPasswordSet()) {
      if (intoRust) {
        lazy.logger.log(
          `Not migrating ${collectionName}: a primary password is set.`
        );
        if (json._data.some(record => !record.deleted)) {
          this.#reportBlocked(Migrator, "primary_password", dryRun);
        }
        return;
      }
      if (lazy.RustAutofillStore.primaryPasswordLocked) {
        lazy.logger.log(
          `Deferring the ${collectionName} copy back: the token is locked.`
        );
        return;
      }
    }

    // Only an encrypted collection needs to know whether there is anything to
    // copy. The Rust store decrypts every record it reads, so for a copy back
    // this read fails when its key cannot decrypt them -- the token was reset,
    // or key4.db replaced. A copy back goes ahead regardless, to fail and
    // report in the migrator and still hand the profile back to JSON. Nothing
    // here throws, which at startup would take autofill down with it.
    let anyRecord;
    if (hasEncryption) {
      try {
        [anyRecord] = await source.getAll({ prompt: false });
      } catch (e) {
        lazy.logger.error(
          `Could not read the ${collectionName} to migrate.`,
          e
        );
        if (intoRust) {
          return;
        }
      }
    }
    // A profile with no records has nothing to decrypt and goes ahead.
    if (anyRecord) {
      // Wait until something else has unlocked the OS key store, so a copy
      // into Rust never surfaces a keyring prompt of its own. It is retried at
      // the next MIGRATION_SAFE_POINT_TOPIC with the key store unlocked rather
      // than at the unlock itself, which lands in the middle of a fill or a
      // save whose writes would then fall inside the copy, or after it into the
      // store it leaves. Windows does not wait: its key store reads the key
      // through CredReadA, which shows no UI. A copy back does not wait either:
      // while Rust serves, reads use NSS's key and nothing may unlock the OS
      // key store, so the copy back would never run.
      if (
        intoRust &&
        AppConstants.platform != "win" &&
        !lazy.OSKeyStore.isLoggedIn
      ) {
        lazy.logger.log(
          `Deferring the ${collectionName} migration to a safe point.`
        );
        this.#reportBlocked(Migrator, "keystore_locked", dryRun);
        if (!this.#awaitingSafePoint) {
          Services.obs.addObserver(this, MIGRATION_SAFE_POINT_TOPIC);
          this.#awaitingSafePoint = true;
        }
        return;
      }
      // Another key store operation is in flight, possibly a dialog of the
      // user's that the copy would otherwise share. Asking now hands back that
      // operation rather than starting one, so retry once it settles.
      if (lazy.OSKeyStore.isUIBusy) {
        lazy.logger.log(
          `Deferring the ${collectionName} migration behind the key store.`
        );
        if (intoRust) {
          this.#reportBlocked(Migrator, "keystore_busy", dryRun);
        }
        lazy.OSKeyStore.ensureLoggedIn(false).then(() =>
          this.#queueCreditCardMigration()
        );
        return;
      }
    }

    const copy = new Migrator(
      source,
      intoRust ? Migrator.config.rustAdapter.getInstance() : json
    );
    // A dry run always reports false, so only a completed real copy hands the
    // profile over to Rust. A copy back hands it over either way.
    const ok = await copy.maybeRun({ dryRun, wipe: intoRust });
    if (!ok) {
      if (intoRust) {
        if (!dryRun && Migrator.attemptsExhausted) {
          lazy.logger.warn(
            `Giving up on migrating ${collectionName}: every attempt failed.`
          );
        }
        return;
      }
      lazy.logger.warn(
        `Serving ${collectionName} from JSON after an incomplete copy back.`
      );
    }
    Services.prefs.setBoolPref(activePref, enabled);

    // The copy is silent, but the store answering a read has changed, and
    // what observers hold was computed from the other one -- the field names
    // FormAutofillStatus shares with the content processes, and the saved
    // profile count. The two stores hold the same records at this point, so
    // this is a recompute rather than news.
    Services.obs.notifyObservers(
      {
        wrappedJSObject: {
          sourceSync: false,
          guid: null,
          collectionName,
        },
      },
      "formautofill-storage-changed",
      "migrate"
    );
  }

  /**
   * Record that a copy into Rust this profile owes was held back.
   *
   * @param {Function} Migrator The collection's migrator class.
   * @param {string} reason Which check held the copy back.
   * @param {boolean} dryRun Whether the copy owed is a dry run.
   */
  #reportBlocked(Migrator, reason, dryRun) {
    try {
      Migrator.config.metrics.migrateToRustBlocked.record({
        reason,
        dry_run: dryRun,
      });
    } catch (e) {
      lazy.logger.error("Could not report the blocked migration", e);
    }
  }

  /**
   * The credit card counterpart of #queueAddressMigration. Kept on its own
   * promise so a flip of one pref does not queue behind a copy of the other
   * collection.
   */
  #queueCreditCardMigration() {
    this.#creditCardSwitch = (this.#creditCardSwitch ?? this.initialize())
      .then(() => this.#maybeRunMigration(lazy.AutofillDataTypes.CREDIT_CARD))
      // Caught rather than left to reject: the next flip chains onto this
      // promise, and a rejected one would swallow every switch after it.
      .catch(e =>
        lazy.logger.error("Could not switch the credit card store", e)
      );
    return this.#creditCardSwitch;
  }

  // For test only: the switch the last pref flip started, so a test can wait
  // for one that is not going to change anything observable.
  get _creditCardSwitch() {
    return this.#creditCardSwitch;
  }

  // The JSON collection, built on first use. Not the same question as
  // getCreditCards(), which answers with whichever store is serving.
  #jsonCreditCards() {
    if (!this._creditCards) {
      this._store.ensureDataReady();
      this._creditCards = new CreditCards(this._store);
    }
    return this._creditCards;
  }

  // The JSON collection, built on first use. Not the same question as
  // getAddresses(), which answers with whichever store is serving.
  #jsonAddresses() {
    if (!this._addresses) {
      this._store.ensureDataReady();
      this._addresses = new Addresses(this._store);
    }
    return this._addresses;
  }

  getAddresses() {
    return lazy.addressRustActive
      ? lazy.RustAutofillAddressesAdapter.getInstance()
      : this.#jsonAddresses();
  }

  getCreditCards() {
    return lazy.creditCardRustActive
      ? lazy.RustAutofillCreditCardsAdapter.getInstance()
      : this.#jsonCreditCards();
  }

  getPassports() {
    if (!this._passports) {
      const rustStore = new lazy.RustAutofillStore();
      this._passports = new lazy.Passports(rustStore.ensureOpen());
    }
    return this._passports;
  }

  /**
   * Loads the profile data from file to memory.
   *
   * @returns {JSONFile}
   *          The JSONFile store.
   */
  _initializeStore() {
    return new lazy.JSONFile({
      path: this._path,
      dataPostProcessor: this._dataPostProcessor.bind(this),
    });
  }

  _dataPostProcessor(data) {
    data.version = this.version;
    if (!data.addresses) {
      data.addresses = [];
    }
    if (!data.creditCards) {
      data.creditCards = [];
    }
    return data;
  }
}

// The singleton exposed by this module.
export const formAutofillStorage = new FormAutofillStorage(
  PathUtils.join(PathUtils.profileDir, PROFILE_JSON_FILE_NAME)
);
