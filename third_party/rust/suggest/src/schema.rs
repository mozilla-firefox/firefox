/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

use crate::db::Sqlite3Extension;
use rusqlite::{Connection, Transaction};
use sql_support::{
    open_database::{self, ConnectionInitializer},
    ConnExt,
};

/// The current database schema version.
///
/// For any changes to the schema [`SQL`], please make sure to:
///
///  1. Bump this version.
///  2. Add a migration from the old version to the new version in
///     [`SuggestConnectionInitializer::upgrade_from`].
///     a. If suggestions should be re-ingested after the migration, call `clear_database()` inside
///        the migration.
///  3. If the migration adds any tables, delete their their rows in
///     `clear_database()` by adding their names to `conditional_tables`, unless
///     they are cleared via a deletion trigger or there's some other good
///     reason not to do so.
pub const VERSION: u32 = 37;

/// The current Suggest database schema.
pub const SQL: &str = "
CREATE TABLE meta(
    key TEXT PRIMARY KEY,
    value NOT NULL
) WITHOUT ROWID;

CREATE TABLE ingested_records(
    id TEXT,
    collection TEXT,
    type TEXT NOT NULL,
    last_modified INTEGER NOT NULL,
    PRIMARY KEY (id, collection)
) WITHOUT ROWID;

CREATE TABLE keywords(
    keyword TEXT NOT NULL,
    suggestion_id INTEGER NOT NULL,
    full_keyword_id INTEGER NULL,
    rank INTEGER NOT NULL,
    PRIMARY KEY (keyword, suggestion_id)
) WITHOUT ROWID;

-- Metrics for the `keywords` table per provider. Not all providers use or
-- update it. If you modify an existing provider to use this, you will need to
-- populate this table somehow with metrics for the provider's existing
-- keywords, for example as part of a schema migration.
CREATE TABLE keywords_metrics(
    record_id TEXT NOT NULL PRIMARY KEY,
    provider INTEGER NOT NULL,
    max_length INTEGER NOT NULL,
    max_word_count INTEGER NOT NULL
) WITHOUT ROWID;

-- full keywords are what we display to the user when a (partial) keyword matches
CREATE TABLE full_keywords(
    id INTEGER PRIMARY KEY,
    suggestion_id INTEGER NOT NULL,
    full_keyword TEXT NOT NULL
);

CREATE TABLE prefix_keywords(
    keyword_prefix TEXT NOT NULL,
    keyword_suffix TEXT NOT NULL DEFAULT '',
    confidence INTEGER NOT NULL DEFAULT 0,
    rank INTEGER NOT NULL,
    suggestion_id INTEGER NOT NULL,
    PRIMARY KEY (keyword_prefix, keyword_suffix, suggestion_id)
) WITHOUT ROWID;

CREATE UNIQUE INDEX keywords_suggestion_id_rank ON keywords(suggestion_id, rank);

CREATE TABLE suggestions(
    id INTEGER PRIMARY KEY,
    record_id TEXT NOT NULL,
    provider INTEGER NOT NULL,
    title TEXT NOT NULL,
    url TEXT NOT NULL,
    score REAL NOT NULL
);

CREATE TABLE amp_custom_details(
    suggestion_id INTEGER PRIMARY KEY,
    advertiser TEXT NOT NULL,
    block_id INTEGER NOT NULL,
    iab_category TEXT NOT NULL,
    impression_url TEXT NOT NULL,
    click_url TEXT NOT NULL,
    icon_id TEXT NOT NULL,
    FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
);

CREATE TABLE wikipedia_custom_details(
    suggestion_id INTEGER PRIMARY KEY REFERENCES suggestions(id) ON DELETE CASCADE,
    icon_id TEXT NOT NULL
);

CREATE TABLE amo_custom_details(
    suggestion_id INTEGER PRIMARY KEY,
    description TEXT NOT NULL,
    guid TEXT NOT NULL,
    icon_url TEXT NOT NULL,
    rating TEXT,
    number_of_ratings INTEGER NOT NULL,
    FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
);

CREATE TABLE fakespot_custom_details(
    suggestion_id INTEGER PRIMARY KEY,
    fakespot_grade TEXT NOT NULL,
    product_id TEXT NOT NULL,
    keywords TEXT NOT NULL,
    product_type TEXT NOT NULL,
    rating REAL NOT NULL,
    total_reviews INTEGER NOT NULL,
    icon_id TEXT,
    FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
);

CREATE VIRTUAL TABLE IF NOT EXISTS fakespot_fts USING FTS5(
  title,
  content='',
  contentless_delete=1,
  tokenize=\"porter unicode61 remove_diacritics 2 tokenchars '''-'\"
);

CREATE TRIGGER fakespot_ai AFTER INSERT ON fakespot_custom_details BEGIN
  INSERT INTO fakespot_fts(rowid, title)
    SELECT id, title
    FROM suggestions
    WHERE id = new.suggestion_id;
END;

CREATE VIRTUAL TABLE IF NOT EXISTS amp_fts USING FTS5(
  full_keywords,
  title,
  content='',
  contentless_delete=1,
  tokenize=\"porter unicode61 remove_diacritics 2 tokenchars '''-'\"
);

-- DELETE/UPDATE triggers are difficult to implement, since the FTS contents are split between the fakespot_custom_details and suggestions tables.
-- If you use an AFTER trigger, then the data from the other table has already been deleted.
-- BEFORE triggers are discouraged by the SQLite docs.
-- Instead, the drop_suggestions function handles updating the FTS data.

CREATE INDEX suggestions_record_id ON suggestions(record_id);

CREATE TABLE icons(
    id TEXT PRIMARY KEY,
    data BLOB NOT NULL,
    mimetype TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE yelp_subjects(
    keyword TEXT PRIMARY KEY,
    record_id TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE yelp_modifiers(
    type INTEGER NOT NULL,
    keyword TEXT NOT NULL,
    record_id TEXT NOT NULL,
    PRIMARY KEY (type, keyword)
) WITHOUT ROWID;

CREATE TABLE yelp_custom_details(
    icon_id TEXT PRIMARY KEY,
    score REAL NOT NULL,
    record_id TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE mdn_custom_details(
    suggestion_id INTEGER PRIMARY KEY,
    description TEXT NOT NULL,
    FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
);

CREATE TABLE dynamic_custom_details(
    suggestion_id INTEGER PRIMARY KEY,
    suggestion_type TEXT NOT NULL,
    json_data TEXT,
    FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
);
CREATE INDEX dynamic_custom_details_suggestion_type ON dynamic_custom_details(suggestion_type);

CREATE TABLE geonames(
    id INTEGER PRIMARY KEY,
    record_id TEXT NOT NULL,
    name TEXT NOT NULL,
    latitude REAL NOT NULL,
    longitude REAL NOT NULL,
    feature_class TEXT NOT NULL,
    feature_code TEXT NOT NULL,
    country_code TEXT NOT NULL,
    admin1_code TEXT NOT NULL,
    population INTEGER NOT NULL
);
CREATE INDEX geonames_feature_class ON geonames(feature_class);
CREATE INDEX geonames_feature_code ON geonames(feature_code);

CREATE TABLE geonames_alternates(
    name TEXT NOT NULL,
    geoname_id INTEGER NOT NULL,
    -- The value of the `iso_language` field for the alternate. This will be
    -- null for the alternate we artificially create for the `name` in the
    -- corresponding geoname record.
    iso_language TEXT,
    PRIMARY KEY (name, geoname_id),
    FOREIGN KEY(geoname_id) REFERENCES geonames(id) ON DELETE CASCADE
) WITHOUT ROWID;
CREATE INDEX geonames_alternates_geoname_id ON geonames_alternates(geoname_id);

CREATE TABLE geonames_metrics(
    record_id TEXT NOT NULL PRIMARY KEY,
    max_name_length INTEGER NOT NULL,
    max_name_word_count INTEGER NOT NULL
) WITHOUT ROWID;

-- `url` may be an opaque dismissal key rather than a URL depending on the
-- suggestion type.
CREATE TABLE dismissed_suggestions (
    url TEXT PRIMARY KEY
) WITHOUT ROWID;
";

/// Initializes an SQLite connection to the Suggest database, performing
/// migrations as needed.
#[derive(Default)]
pub struct SuggestConnectionInitializer<'a> {
    extensions_to_load: &'a [Sqlite3Extension],
}

impl<'a> SuggestConnectionInitializer<'a> {
    pub fn new(extensions_to_load: &'a [Sqlite3Extension]) -> Self {
        Self { extensions_to_load }
    }

    pub fn load_extensions(&self, conn: &Connection) -> open_database::Result<()> {
        // Safety: this relies on the extensions we're loading to operate correctly, for the
        // entry point to be correct, etc.
        unsafe {
            let _guard = rusqlite::LoadExtensionGuard::new(conn)?;
            for ext in self.extensions_to_load {
                conn.load_extension(&ext.library, ext.entry_point.as_deref())?;
            }
        }
        Ok(())
    }
}

impl ConnectionInitializer for SuggestConnectionInitializer<'_> {
    const NAME: &'static str = "suggest db";
    const END_VERSION: u32 = VERSION;

    fn prepare(&self, conn: &Connection, _db_empty: bool) -> open_database::Result<()> {
        self.load_extensions(conn)?;
        sql_support::setup_sqlite_defaults(conn)?;
        conn.execute("PRAGMA foreign_keys = ON", ())?;
        sql_support::debug_tools::define_debug_functions(conn)?;

        Ok(())
    }

    fn init(&self, db: &Transaction<'_>) -> open_database::Result<()> {
        db.execute_batch(SQL)?;
        Ok(())
    }

    fn upgrade_from(&self, tx: &Transaction<'_>, version: u32) -> open_database::Result<()> {
        match version {
            1..=15 => {
                // Treat databases with these older schema versions as corrupt,
                // so that they'll be replaced by a fresh, empty database with
                // the current schema.
                Err(open_database::Error::Corrupt)
            }
            16 => {
                tx.execute(
                    "
                    CREATE TABLE dismissed_suggestions (
                        url_hash INTEGER PRIMARY KEY
                    ) WITHOUT ROWID;",
                    (),
                )?;
                Ok(())
            }
            17 => {
                tx.execute(
                    "
                    DROP TABLE dismissed_suggestions;
                    CREATE TABLE dismissed_suggestions (
                        url TEXT PRIMARY KEY
                    ) WITHOUT ROWID;",
                    (),
                )?;
                Ok(())
            }
            18 => {
                tx.execute_batch(
                    "
                    CREATE TABLE IF NOT EXISTS dismissed_suggestions (
                        url TEXT PRIMARY KEY
                    ) WITHOUT ROWID;",
                )?;
                Ok(())
            }
            19 => {
                // Clear the database since we're going to be dropping the keywords table and
                // re-creating it
                clear_database(tx)?;
                tx.execute_batch(
                    "
                    -- Recreate the various keywords table to drop the foreign keys.
                    DROP TABLE keywords;
                    DROP TABLE full_keywords;
                    DROP TABLE prefix_keywords;
                    CREATE TABLE keywords(
                        keyword TEXT NOT NULL,
                        suggestion_id INTEGER NOT NULL,
                        full_keyword_id INTEGER NULL,
                        rank INTEGER NOT NULL,
                        PRIMARY KEY (keyword, suggestion_id)
                    ) WITHOUT ROWID;
                    CREATE TABLE full_keywords(
                        id INTEGER PRIMARY KEY,
                        suggestion_id INTEGER NOT NULL,
                        full_keyword TEXT NOT NULL
                    );
                    CREATE TABLE prefix_keywords(
                        keyword_prefix TEXT NOT NULL,
                        keyword_suffix TEXT NOT NULL DEFAULT '',
                        confidence INTEGER NOT NULL DEFAULT 0,
                        rank INTEGER NOT NULL,
                        suggestion_id INTEGER NOT NULL,
                        PRIMARY KEY (keyword_prefix, keyword_suffix, suggestion_id)
                    ) WITHOUT ROWID;
                    CREATE UNIQUE INDEX keywords_suggestion_id_rank ON keywords(suggestion_id, rank);
                    ",
                )?;
                Ok(())
            }

            // Migration for the fakespot data.  This is not currently active for any users, it's
            // only used for the tests.  It's safe to alter the fakespot_custom_detail schema and
            // update this migration as the project moves forward.
            //
            // Note: if we want to add a regular migration while the fakespot code is still behind
            // a feature flag, insert it before this one and make fakespot the last migration.
            20 => {
                tx.execute_batch(
                    "
                CREATE TABLE fakespot_custom_details(
                    suggestion_id INTEGER PRIMARY KEY,
                    fakespot_grade TEXT NOT NULL,
                    product_id TEXT NOT NULL,
                    rating REAL NOT NULL,
                    total_reviews INTEGER NOT NULL,
                    FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
                );
                -- Create the Fakespot FTS table.
                -- The `tokenize` param is hard to read.  The effect is that dashes and apostrophes are
                -- considered valid tokens in a word, rather than separators.
                CREATE VIRTUAL TABLE IF NOT EXISTS fakespot_fts USING FTS5(
                  title,
                  prefix='4 5 6 7 8 9 10 11',
                  content='',
                  contentless_delete=1,
                  tokenize=\"porter unicode61 remove_diacritics 2 tokenchars '''-'\"
                );
                CREATE TRIGGER fakespot_ai AFTER INSERT ON fakespot_custom_details BEGIN
                  INSERT INTO fakespot_fts(rowid, title)
                    SELECT id, title
                    FROM suggestions
                    WHERE id = new.suggestion_id;
                END;
                ",
                )?;
                Ok(())
            }
            21 => {
                // Drop and re-create the fakespot_custom_details to add the icon_id column.
                tx.execute_batch(
                    "
                    DROP TABLE fakespot_custom_details;
                    CREATE TABLE fakespot_custom_details(
                        suggestion_id INTEGER PRIMARY KEY,
                        fakespot_grade TEXT NOT NULL,
                        product_id TEXT NOT NULL,
                        rating REAL NOT NULL,
                        total_reviews INTEGER NOT NULL,
                        icon_id TEXT,
                        FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
                    );
                    CREATE TRIGGER fakespot_ai AFTER INSERT ON fakespot_custom_details BEGIN
                      INSERT INTO fakespot_fts(rowid, title)
                        SELECT id, title
                        FROM suggestions
                        WHERE id = new.suggestion_id;
                    END;
                    ",
                )?;
                Ok(())
            }
            22 => {
                // Drop and re-create the fakespot_fts table to remove the prefix index param
                tx.execute_batch(
                    "
                    DROP TABLE fakespot_fts;
                    CREATE VIRTUAL TABLE fakespot_fts USING FTS5(
                      title,
                      content='',
                      contentless_delete=1,
                      tokenize=\"porter unicode61 remove_diacritics 2 tokenchars '''-'\"
                    );
                    ",
                )?;
                Ok(())
            }
            23 => {
                // Drop all suggestions, then recreate the fakespot_custom_details table to add the
                // `keywords` and `product_type` fields.
                clear_database(tx)?;
                tx.execute_batch(
                    "
                    DROP TABLE fakespot_custom_details;
                    CREATE TABLE fakespot_custom_details(
                        suggestion_id INTEGER PRIMARY KEY,
                        fakespot_grade TEXT NOT NULL,
                        product_id TEXT NOT NULL,
                        keywords TEXT NOT NULL,
                        product_type TEXT NOT NULL,
                        rating REAL NOT NULL,
                        total_reviews INTEGER NOT NULL,
                        icon_id TEXT,
                        FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
                    );
                    CREATE TRIGGER fakespot_ai AFTER INSERT ON fakespot_custom_details BEGIN
                      INSERT INTO fakespot_fts(rowid, title)
                        SELECT id, title
                        FROM suggestions
                        WHERE id = new.suggestion_id;
                    END;
                    ",
                )?;
                Ok(())
            }
            24 => {
                // Clear the database so that we re-ingest and populate the ingested_records table.
                clear_database(tx)?;
                tx.execute_batch(
                    "
                    CREATE TABLE rs_cache(
                        collection TEXT PRIMARY KEY,
                        data TEXT NOT NULL
                    ) WITHOUT ROWID;
                    CREATE TABLE ingested_records(
                        id TEXT,
                        collection TEXT,
                        type TEXT NOT NULL,
                        last_modified INTEGER NOT NULL,
                        PRIMARY KEY (id, collection)
                    ) WITHOUT ROWID;
                    ",
                )?;
                Ok(())
            }
            25 => {
                // Create the exposure suggestions table and index.
                tx.execute_batch(
                    "
                    CREATE TABLE exposure_custom_details(
                        suggestion_id INTEGER PRIMARY KEY,
                        type TEXT NOT NULL,
                        FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
                    );
                    CREATE INDEX exposure_custom_details_type ON exposure_custom_details(type);
                    ",
                )?;
                Ok(())
            }
            26 => {
                // Create tables related to city-based weather.
                tx.execute_batch(
                    "
                    CREATE TABLE keywords_metrics(
                        record_id TEXT NOT NULL PRIMARY KEY,
                        provider INTEGER NOT NULL,
                        max_length INTEGER NOT NULL,
                        max_word_count INTEGER NOT NULL
                    ) WITHOUT ROWID;

                    CREATE TABLE geonames(
                        id INTEGER PRIMARY KEY,
                        record_id TEXT NOT NULL,
                        name TEXT NOT NULL,
                        feature_class TEXT NOT NULL,
                        feature_code TEXT NOT NULL,
                        country_code TEXT NOT NULL,
                        admin1_code TEXT NOT NULL,
                        population INTEGER
                    );
                    CREATE INDEX geonames_feature_class ON geonames(feature_class);
                    CREATE INDEX geonames_feature_code ON geonames(feature_code);

                    CREATE TABLE geonames_alternates(
                        name TEXT NOT NULL,
                        geoname_id INTEGER NOT NULL,
                        PRIMARY KEY (name, geoname_id),
                        FOREIGN KEY(geoname_id) REFERENCES geonames(id) ON DELETE CASCADE
                    ) WITHOUT ROWID;

                    CREATE TABLE geonames_metrics(
                        record_id TEXT NOT NULL PRIMARY KEY,
                        max_name_length INTEGER NOT NULL,
                        max_name_word_count INTEGER NOT NULL
                    ) WITHOUT ROWID;
                    ",
                )?;
                Ok(())
            }
            27 => {
                // Add latitude and longitude to the geonames table. Clear the
                // database so geonames are reingested.
                clear_database(tx)?;
                tx.execute_batch(
                    "
                    DROP INDEX geonames_feature_class;
                    DROP INDEX geonames_feature_code;
                    DROP TABLE geonames;
                    CREATE TABLE geonames(
                        id INTEGER PRIMARY KEY,
                        record_id TEXT NOT NULL,
                        name TEXT NOT NULL,
                        latitude REAL NOT NULL,
                        longitude REAL NOT NULL,
                        feature_class TEXT NOT NULL,
                        feature_code TEXT NOT NULL,
                        country_code TEXT NOT NULL,
                        admin1_code TEXT NOT NULL,
                        population INTEGER NOT NULL
                    );
                    CREATE INDEX geonames_feature_class ON geonames(feature_class);
                    CREATE INDEX geonames_feature_code ON geonames(feature_code);
                    ",
                )?;
                Ok(())
            }
            28 => {
                // Add `iso_language` column to `geonames_alternates`. Clear the
                // database so geonames are reingested.
                clear_database(tx)?;
                tx.execute_batch(
                    "
                    DROP TABLE geonames_alternates;
                    CREATE TABLE geonames_alternates(
                        name TEXT NOT NULL,
                        geoname_id INTEGER NOT NULL,
                        -- The value of the `iso_language` field for the alternate. This will be
                        -- null for the alternate we artificially create for the `name` in the
                        -- corresponding geoname record.
                        iso_language TEXT,
                        PRIMARY KEY (name, geoname_id),
                        FOREIGN KEY(geoname_id) REFERENCES geonames(id) ON DELETE CASCADE
                    ) WITHOUT ROWID;
                    ",
                )?;
                Ok(())
            }
            29 => {
                // This migration only clears the database because some tables
                // that should have been cleared in previous migrations were
                // not.
                clear_database(tx)?;
                Ok(())
            }
            30 => {
                // Add the `geonames_alternates_geoname_id` index.
                clear_database(tx)?;
                tx.execute_batch(
                    "
                    CREATE INDEX geonames_alternates_geoname_id ON geonames_alternates(geoname_id);
                    ",
                )?;
                Ok(())
            }
            31 => {
                // Need to clear the database so that the FTS index will get filled.
                clear_database(tx)?;
                tx.execute_batch(
                    "
                    CREATE VIRTUAL TABLE IF NOT EXISTS amp_fts USING FTS5(
                      full_keywords,
                      title,
                      content='',
                      contentless_delete=1,
                      tokenize=\"porter unicode61 remove_diacritics 2 tokenchars '''-'\"
                    );

                    ",
                )?;
                Ok(())
            }
            32 => {
                // Drop rs_cache since it's no longer needed.
                tx.execute_batch("DROP TABLE rs_cache;")?;
                Ok(())
            }
            33 => {
                // This migration is due to the replacement of the
                // `quicksuggest` collection with `quicksuggest-amp` and
                // `quicksuggest-other`. Clear the DB so that records from the
                // old collection don't stick around. See bug 1953945.
                clear_database(tx)?;
                Ok(())
            }
            34 => {
                // Replace the exposure suggestions table and index with the
                // dynamic suggestions table and index.
                tx.execute_batch(
                    r#"
                    DROP INDEX exposure_custom_details_type;
                    DROP TABLE exposure_custom_details;
                    CREATE TABLE dynamic_custom_details(
                        suggestion_id INTEGER PRIMARY KEY,
                        suggestion_type TEXT NOT NULL,
                        json_data TEXT,
                        FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
                    );
                    CREATE INDEX dynamic_custom_details_suggestion_type ON dynamic_custom_details(suggestion_type);
                    "#,
                )?;
                Ok(())
            }
            35 => {
                // The commit that added this migration was reverted.
                Ok(())
            }
            36 => {
                tx.execute_batch("DROP TABLE IF EXISTS yelp_location_signs;")?;
                Ok(())
            }
            _ => Err(open_database::Error::IncompatibleVersion(version)),
        }
    }
}

/// Clears the database, removing all suggestions, icons, and metadata.
pub fn clear_database(db: &Connection) -> rusqlite::Result<()> {
    db.execute_batch(
        "
        DELETE FROM meta;
        DELETE FROM keywords;
        DELETE FROM full_keywords;
        DELETE FROM prefix_keywords;
        DELETE FROM suggestions;
        DELETE FROM icons;
        DELETE FROM yelp_subjects;
        DELETE FROM yelp_modifiers;
        DELETE FROM yelp_custom_details;
        ",
    )?;
    let conditional_tables = [
        "fakespot_fts",
        "geonames",
        "geonames_metrics",
        "ingested_records",
        "keywords_metrics",
    ];
    for t in conditional_tables {
        let table_exists = db.exists("SELECT 1 FROM sqlite_master WHERE name = ?", [t])?;
        if table_exists {
            db.execute(&format!("DELETE FROM {t}"), ())?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use sql_support::open_database::test_utils::MigratedDatabaseFile;

    // Snapshot of the v16 schema.  We use this to test that we can migrate from there to the
    // current schema.
    const V16_SCHEMA: &str = r#"
CREATE TABLE meta(
    key TEXT PRIMARY KEY,
    value NOT NULL
) WITHOUT ROWID;

CREATE TABLE keywords(
    keyword TEXT NOT NULL,
    suggestion_id INTEGER NOT NULL REFERENCES suggestions(id) ON DELETE CASCADE,
    full_keyword_id INTEGER NULL REFERENCES full_keywords(id) ON DELETE SET NULL,
    rank INTEGER NOT NULL,
    PRIMARY KEY (keyword, suggestion_id)
) WITHOUT ROWID;

-- full keywords are what we display to the user when a (partial) keyword matches
-- The FK to suggestion_id makes it so full keywords get deleted when the parent suggestion is deleted.
CREATE TABLE full_keywords(
    id INTEGER PRIMARY KEY,
    suggestion_id INTEGER NOT NULL REFERENCES suggestions(id) ON DELETE CASCADE,
    full_keyword TEXT NOT NULL
);

CREATE TABLE prefix_keywords(
    keyword_prefix TEXT NOT NULL,
    keyword_suffix TEXT NOT NULL DEFAULT '',
    confidence INTEGER NOT NULL DEFAULT 0,
    rank INTEGER NOT NULL,
    suggestion_id INTEGER NOT NULL REFERENCES suggestions(id) ON DELETE CASCADE,
    PRIMARY KEY (keyword_prefix, keyword_suffix, suggestion_id)
) WITHOUT ROWID;

CREATE UNIQUE INDEX keywords_suggestion_id_rank ON keywords(suggestion_id, rank);

CREATE TABLE suggestions(
    id INTEGER PRIMARY KEY,
    record_id TEXT NOT NULL,
    provider INTEGER NOT NULL,
    title TEXT NOT NULL,
    url TEXT NOT NULL,
    score REAL NOT NULL
);

CREATE TABLE amp_custom_details(
    suggestion_id INTEGER PRIMARY KEY,
    advertiser TEXT NOT NULL,
    block_id INTEGER NOT NULL,
    iab_category TEXT NOT NULL,
    impression_url TEXT NOT NULL,
    click_url TEXT NOT NULL,
    icon_id TEXT NOT NULL,
    FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
);

CREATE TABLE wikipedia_custom_details(
    suggestion_id INTEGER PRIMARY KEY REFERENCES suggestions(id) ON DELETE CASCADE,
    icon_id TEXT NOT NULL
);

CREATE TABLE amo_custom_details(
    suggestion_id INTEGER PRIMARY KEY,
    description TEXT NOT NULL,
    guid TEXT NOT NULL,
    icon_url TEXT NOT NULL,
    rating TEXT,
    number_of_ratings INTEGER NOT NULL,
    FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
);

CREATE INDEX suggestions_record_id ON suggestions(record_id);

CREATE TABLE icons(
    id TEXT PRIMARY KEY,
    data BLOB NOT NULL,
    mimetype TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE yelp_subjects(
    keyword TEXT PRIMARY KEY,
    record_id TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE yelp_modifiers(
    type INTEGER NOT NULL,
    keyword TEXT NOT NULL,
    record_id TEXT NOT NULL,
    PRIMARY KEY (type, keyword)
) WITHOUT ROWID;

CREATE TABLE yelp_location_signs(
    keyword TEXT PRIMARY KEY,
    need_location INTEGER NOT NULL,
    record_id TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE yelp_custom_details(
    icon_id TEXT PRIMARY KEY,
    score REAL NOT NULL,
    record_id TEXT NOT NULL
) WITHOUT ROWID;

CREATE TABLE mdn_custom_details(
    suggestion_id INTEGER PRIMARY KEY,
    description TEXT NOT NULL,
    FOREIGN KEY(suggestion_id) REFERENCES suggestions(id) ON DELETE CASCADE
);

PRAGMA user_version=16;
"#;

    /// Test running all schema upgrades from V16, which was the first schema with a "real"
    /// migration.
    ///
    /// If an upgrade fails, then this test will fail with a panic.
    #[test]
    fn test_all_upgrades() {
        let db_file =
            MigratedDatabaseFile::new(SuggestConnectionInitializer::default(), V16_SCHEMA);
        db_file.run_all_upgrades();
        db_file.assert_schema_matches_new_database();
    }

    /// Test that `clear_database()` works correctly during migrations.
    ///
    /// TODO: This only checks `ingested_records` and `rs_cache` for now since
    /// they're very important, but ideally this would test all tables.
    #[test]
    fn test_clear_database() -> anyhow::Result<()> {
        // Start with the v16 schema.
        let db_file =
            MigratedDatabaseFile::new(SuggestConnectionInitializer::default(), V16_SCHEMA);

        // Upgrade to v25, the first version with with `ingested_records` tables.
        db_file.upgrade_to(25);

        // Insert some ingested records.
        let conn = db_file.open();
        conn.execute(
            "INSERT INTO ingested_records(id, collection, type, last_modified) VALUES(?, ?, ?, ?)",
            ("record-id", "quicksuggest", "record-type", 1),
        )?;
        conn.close().expect("Connection should be closed");

        // Finish upgrading to the current version.
        db_file.upgrade_to(VERSION);
        db_file.assert_schema_matches_new_database();

        // `ingested_records` should be empty.
        let conn = db_file.open();
        assert_eq!(
            conn.query_one::<i32>("SELECT count(*) FROM ingested_records")?,
            0,
            "ingested_records should be empty"
        );
        conn.close().expect("Connection should be closed");

        Ok(())
    }

    /// Test that yelp_location_signs table could be removed correctly.
    #[test]
    fn test_remove_yelp_location_signs_table() -> anyhow::Result<()> {
        // Start with the v16 schema.
        let db_file =
            MigratedDatabaseFile::new(SuggestConnectionInitializer::default(), V16_SCHEMA);

        // Upgrade to v36.
        db_file.upgrade_to(36);

        // Drop the table to simulate old 35 > 36 migration.
        let conn = db_file.open();
        conn.execute("DROP table yelp_location_signs", ())?;
        conn.close().expect("Connection should be closed");

        // Finish upgrading to the current version.
        db_file.upgrade_to(VERSION);
        db_file.assert_schema_matches_new_database();

        Ok(())
    }
}
