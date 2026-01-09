/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//  N.B. if you're making a documentation change here, you might also want to make it in:
//
//    * The API docs in ../ios/Logins/LoginRecord.swift
//    * The API docs in ../android/src/main/java/mozilla/appservices/logins/ServerPassword.kt
//    * The android-components docs at
//      https://github.com/mozilla-mobile/android-components/tree/master/components/service/sync-logins
//
//  We'll figure out a more scalable approach to maintaining all those docs at some point...

//! # Login Structs
//!
//! This module defines a number of core structs for Logins. They are:
//! * [`LoginEntry`] A login entry by the user.  This includes the username/password, the site it
//!   was submitted to, etc.  [`LoginEntry`] does not store data specific to a DB record.
//! * [`Login`] - A [`LoginEntry`] plus DB record information.  This includes the GUID and metadata
//!   like time_last_used.
//! * [`EncryptedLogin`] -- A Login above with the username/password data encrypted.
//! * [`LoginFields`], [`SecureLoginFields`], [`LoginMeta`] -- These group the common fields in the
//!   structs above.
//!
//! Why so many structs for similar data?  Consider some common use cases in a hypothetical browser
//! (currently no browsers act exactly like this, although Fenix/android-components comes close):
//!
//! - User visits a page with a login form.
//!   - We inform the user if there are saved logins that can be autofilled.  We use the
//!     `LoginDb.get_by_base_domain()` which returns a `Vec<EncryptedLogin>`.  We don't decrypt the
//!     logins because we want to avoid requiring the encryption key at this point, which would
//!     force the user to authenticate.  Note: this is aspirational at this point, no actual
//!     implementations follow this flow.  Still, we want application-services to support it.
//!   - If the user chooses to autofill, we decrypt the logins into a `Vec<Login>`.  We need to
//!     decrypt at this point to display the username and autofill the password if they select one.
//!   - When the user selects a login, we can use the already decrypted data from `Login` to fill
//!     in the form.
//! - User chooses to save a login for autofilling later.
//!    - We present the user with a dialog that:
//!       - Displays a header that differentiates between different types of save: adding a new
//!         login, updating an existing login, filling in a blank username, etc.
//!       - Allows the user to tweak the username, in case we failed to detect the form field
//!         correctly.  This may affect which header should be shown.
//!    - Here we use `find_login_to_update()` which returns an `Option<Login>`.  Returning a login
//!      that has decrypted data avoids forcing the consumer code to decrypt the username again.
//!
//! # Login
//! This has the complete set of data about a login. Very closely related is the
//! "sync payload", defined in sync/payload.rs, which handles all aspects of the JSON serialization.
//! It contains the following fields:
//! - `meta`: A [`LoginMeta`] struct.
//! - fields: A [`LoginFields`] struct.
//! - sec_fields: A [`SecureLoginFields`] struct.
//!
//! # LoginEntry
//! The struct used to add or update logins. This has the plain-text version of the fields that are
//! stored encrypted, so almost all uses of an LoginEntry struct will also require the
//! encryption key to be known and passed in.    [LoginDB] methods that save data typically input
//! [LoginEntry] instances.  This allows the DB code to handle dupe-checking issues like
//! determining which login record should be updated for a newly submitted [LoginEntry].
//! It contains the following fields:
//! - fields: A [`LoginFields`] struct.
//! - sec_fields: A [`SecureLoginFields`] struct.
//!
//! # EncryptedLogin
//! Encrypted version of [`Login`].  [LoginDB] methods that return data typically return [EncryptedLogin]
//! this allows deferring decryption, and therefore user authentication, until the secure data is needed.
//! It contains the following fields
//! - `meta`: A [`LoginMeta`] struct.
//! - `fields`: A [`LoginFields`] struct.
//! - `sec_fields`: The secure fields as an encrypted string
//!
//! # SecureLoginFields
//! The struct used to hold the fields which are stored encrypted. It contains:
//! - username: A string.
//! - password: A string.
//!
//! # LoginFields
//!
//! The core set of fields, use by both [`Login`] and [`LoginEntry`]
//! It contains the following fields:
//!
//! - `origin`:  The origin at which this login can be used, as a string.
//!
//!   The login should only be used on sites that match this origin (for whatever definition
//!   of "matches" makes sense at the application level, e.g. eTLD+1 matching).
//!   This field is required, must be a valid origin in punycode format, and must not be
//!   set to the empty string.
//!
//!   Examples of valid `origin` values include:
//!   - "https://site.com"
//!   - "http://site.com:1234"
//!   - "ftp://ftp.site.com"
//!   - "moz-proxy://127.0.0.1:8888"
//!   - "chrome://MyLegacyExtension"
//!   - "file://"
//!   - "https://\[::1\]"
//!
//!   If invalid data is received in this field (either from the application, or via sync)
//!   then the logins store will attempt to coerce it into valid data by:
//!   - truncating full URLs to just their origin component, if it is not an opaque origin
//!   - converting values with non-ascii characters into punycode
//!
//!   **XXX TODO:**
//!   - Add a field with the original unicode versions of the URLs instead of punycode?
//!
//! - `sec_fields`: The `username` and `password` for the site, stored as a encrypted JSON
//!   representation of an `SecureLoginFields`.
//!
//!   This field is required and usually encrypted.  There are two different value types:
//!   - Plaintext empty string: Used for deleted records
//!   - Encrypted value: The credentials associated with the login.
//!
//! - `http_realm`:  The challenge string for HTTP Basic authentication, if any.
//!
//!   If present, the login should only be used in response to a HTTP Basic Auth
//!   challenge that specifies a matching realm. For legacy reasons this string may not
//!   contain null bytes, carriage returns or newlines.
//!
//!   If this field is set to the empty string, this indicates a wildcard match on realm.
//!
//!   This field must not be present if `form_action_origin` is set, since they indicate different types
//!   of login (HTTP-Auth based versus form-based). Exactly one of `http_realm` and `form_action_origin`
//!   must be present.
//!
//! - `form_action_origin`:  The target origin of forms in which this login can be used, if any, as a string.
//!
//!   If present, the login should only be used in forms whose target submission URL matches this origin.
//!   This field must be a valid origin or one of the following special cases:
//!   - An empty string, which is a wildcard match for any origin.
//!   - The single character ".", which is equivalent to the empty string
//!   - The string "javascript:", which matches any form with javascript target URL.
//!
//!   This field must not be present if `http_realm` is set, since they indicate different types of login
//!   (HTTP-Auth based versus form-based). Exactly one of `http_realm` and `form_action_origin` must be present.
//!
//!   If invalid data is received in this field (either from the application, or via sync) then the
//!   logins store will attempt to coerce it into valid data by:
//!   - truncating full URLs to just their origin component
//!   - converting origins with non-ascii characters into punycode
//!   - replacing invalid values with null if a valid 'http_realm' field is present
//!
//! - `username_field`:  The name of the form field into which the 'username' should be filled, if any.
//!
//!   This value is stored if provided by the application, but does not imply any restrictions on
//!   how the login may be used in practice. For legacy reasons this string may not contain null
//!   bytes, carriage returns or newlines. This field must be empty unless `form_action_origin` is set.
//!
//!   If invalid data is received in this field (either from the application, or via sync)
//!   then the logins store will attempt to coerce it into valid data by:
//!   - setting to the empty string if 'form_action_origin' is not present
//!
//! - `password_field`:  The name of the form field into which the 'password' should be filled, if any.
//!
//!   This value is stored if provided by the application, but does not imply any restrictions on
//!   how the login may be used in practice. For legacy reasons this string may not contain null
//!   bytes, carriage returns or newlines. This field must be empty unless `form_action_origin` is set.
//!
//!   If invalid data is received in this field (either from the application, or via sync)
//!   then the logins store will attempt to coerce it into valid data by:
//!   - setting to the empty string if 'form_action_origin' is not present
//!
//! # LoginMeta
//!
//! This contains data relating to the login database record -- both on the local instance and
//! synced to other browsers.
//! It contains the following fields:
//! - `id`:  A unique string identifier for this record.
//!
//!   Consumers may assume that `id` contains only "safe" ASCII characters but should otherwise
//!   treat this it as an opaque identifier. These are generated as needed.
//!
//! - `timesUsed`:  A lower bound on the number of times the password from this record has been used, as an integer.
//!
//!   Applications should use the `touch()` method of the logins store to indicate when a password
//!   has been used, and should ensure that they only count uses of the actual `password` field
//!   (so for example, copying the `password` field to the clipboard should count as a "use", but
//!   copying just the `username` field should not).
//!
//!   This number may not record uses that occurred on other devices, since some legacy
//!   sync clients do not record this information. It may be zero for records obtained
//!   via sync that have never been used locally.
//!
//!   When merging duplicate records, the two usage counts are summed.
//!
//!   This field is managed internally by the logins store by default and does not need to
//!   be set explicitly, although any application-provided value will be preserved when creating
//!   a new record.
//!
//!   If invalid data is received in this field (either from the application, or via sync)
//!   then the logins store will attempt to coerce it into valid data by:
//!   - replacing missing or negative values with 0
//!
//!   **XXX TODO:**
//!   - test that we prevent this counter from moving backwards.
//!   - test fixups of missing or negative values
//!   - test that we correctly merge dupes
//!
//! - `time_created`: An upper bound on the time of creation of this login, in integer milliseconds from the unix epoch.
//!
//!   This is an upper bound because some legacy sync clients do not record this information.
//!
//!   Note that this field is typically a timestamp taken from the local machine clock, so it
//!   may be wildly inaccurate if the client does not have an accurate clock.
//!
//!   This field is managed internally by the logins store by default and does not need to
//!   be set explicitly, although any application-provided value will be preserved when creating
//!   a new record.
//!
//!   When merging duplicate records, the smallest non-zero value is taken.
//!
//!   If invalid data is received in this field (either from the application, or via sync)
//!   then the logins store will attempt to coerce it into valid data by:
//!   - replacing missing or negative values with the current time
//!
//!   **XXX TODO:**
//!   - test that we prevent this timestamp from moving backwards.
//!   - test fixups of missing or negative values
//!   - test that we correctly merge dupes
//!
//! - `time_last_used`: A lower bound on the time of last use of this login, in integer milliseconds from the unix epoch.
//!
//!   This is a lower bound because some legacy sync clients do not record this information;
//!   in that case newer clients set `timeLastUsed` when they use the record for the first time.
//!
//!   Note that this field is typically a timestamp taken from the local machine clock, so it
//!   may be wildly inaccurate if the client does not have an accurate clock.
//!
//!   This field is managed internally by the logins store by default and does not need to
//!   be set explicitly, although any application-provided value will be preserved when creating
//!   a new record.
//!
//!   When merging duplicate records, the largest non-zero value is taken.
//!
//!   If invalid data is received in this field (either from the application, or via sync)
//!   then the logins store will attempt to coerce it into valid data by:
//!   - removing negative values
//!
//!   **XXX TODO:**
//!   - test that we prevent this timestamp from moving backwards.
//!   - test fixups of missing or negative values
//!   - test that we correctly merge dupes
//!
//! - `time_password_changed`: A lower bound on the time that the `password` field was last changed, in integer
//!   milliseconds from the unix epoch.
//!
//!   Changes to other fields (such as `username`) are not reflected in this timestamp.
//!   This is a lower bound because some legacy sync clients do not record this information;
//!   in that case newer clients set `time_password_changed` when they change the `password` field.
//!
//!   Note that this field is typically a timestamp taken from the local machine clock, so it
//!   may be wildly inaccurate if the client does not have an accurate clock.
//!
//!   This field is managed internally by the logins store by default and does not need to
//!   be set explicitly, although any application-provided value will be preserved when creating
//!   a new record.
//!
//!   When merging duplicate records, the largest non-zero value is taken.
//!
//!   If invalid data is received in this field (either from the application, or via sync)
//!   then the logins store will attempt to coerce it into valid data by:
//!   - removing negative values
//!
//!   **XXX TODO:**
//!   - test that we prevent this timestamp from moving backwards.
//!   - test that we don't set this for changes to other fields.
//!   - test that we correctly merge dupes
//!
//!
//! In order to deal with data from legacy clients in a robust way, it is necessary to be able to build
//! and manipulate all these `Login` structs that contain invalid data.  The non-encrypted structs
//! implement the `ValidateAndFixup` trait, providing the following methods which can be used by
//! callers to ensure that they're only working with valid records:
//!
//! - `Login::check_valid()`:    Checks validity of a login record, returning `()` if it is valid
//!   or an error if it is not.
//!
//! - `Login::fixup()`:   Returns either the existing login if it is valid, a clone with invalid fields
//!   fixed up if it was safe to do so, or an error if the login is irreparably invalid.

use crate::{encryption::EncryptorDecryptor, error::*};
use rusqlite::Row;
use serde_derive::*;
use sync_guid::Guid;
use url::Url;

// LoginEntry fields that are stored in cleartext
#[derive(Debug, Clone, Hash, PartialEq, Eq, Default)]
pub struct LoginFields {
    pub origin: String,
    pub form_action_origin: Option<String>,
    pub http_realm: Option<String>,
    pub username_field: String,
    pub password_field: String,
    pub time_of_last_breach: Option<i64>,
    pub time_last_breach_alert_dismissed: Option<i64>,
}

/// LoginEntry fields that are stored encrypted
#[derive(Debug, Clone, Hash, PartialEq, Eq, Serialize, Deserialize, Default)]
pub struct SecureLoginFields {
    // - Username cannot be null, use the empty string instead
    // - Password can't be empty or null (enforced in the ValidateAndFixup code)
    //
    // This matches the desktop behavior:
    // https://searchfox.org/mozilla-central/rev/d3683dbb252506400c71256ef3994cdbdfb71ada/toolkit/components/passwordmgr/LoginManager.jsm#260-267

    // Because we store the json version of this in the DB, and that's the only place the json
    // is used, we rename the fields to short names, just to reduce the overhead in the DB.
    #[serde(rename = "u")]
    pub username: String,
    #[serde(rename = "p")]
    pub password: String,
}

impl SecureLoginFields {
    pub fn encrypt(&self, encdec: &dyn EncryptorDecryptor, login_id: &str) -> Result<String> {
        let string = serde_json::to_string(&self)?;
        let cipherbytes = encdec
            .encrypt(string.as_bytes().into())
            .map_err(|e| Error::EncryptionFailed(format!("{e} (encrypting {login_id})")))?;
        let ciphertext = std::str::from_utf8(&cipherbytes).map_err(|e| {
            Error::EncryptionFailed(format!("{e} (encrypting {login_id}: data not utf8)"))
        })?;
        Ok(ciphertext.to_owned())
    }

    pub fn decrypt(
        ciphertext: &str,
        encdec: &dyn EncryptorDecryptor,
        login_id: &str,
    ) -> Result<Self> {
        let jsonbytes = encdec
            .decrypt(ciphertext.as_bytes().into())
            .map_err(|e| Error::DecryptionFailed(format!("{e} (decrypting {login_id})")))?;
        let json =
            std::str::from_utf8(&jsonbytes).map_err(|e| Error::DecryptionFailed(e.to_string()))?;
        Ok(serde_json::from_str(json)?)
    }
}

/// Login data specific to database records
#[derive(Debug, Clone, Hash, PartialEq, Eq, Default)]
pub struct LoginMeta {
    pub id: String,
    pub time_created: i64,
    pub time_password_changed: i64,
    pub time_last_used: i64,
    pub times_used: i64,
}

/// A login together with meta fields, handed over to the store API; ie a login persisted
/// elsewhere, useful for migrations
pub struct LoginEntryWithMeta {
    pub entry: LoginEntry,
    pub meta: LoginMeta,
}

/// A bulk insert result entry, returned by `add_many` and `add_many_with_records`
/// Please note that although the success case is much larger than the error case, this is
/// negligible in real life, as we expect a very small success/error ratio.
#[allow(clippy::large_enum_variant)]
pub enum BulkResultEntry {
    Success { login: Login },
    Error { message: String },
}

/// A login handed over to the store API; ie a login not yet persisted
#[derive(Debug, Clone, Hash, PartialEq, Eq, Default)]
pub struct LoginEntry {
    // login fields
    pub origin: String,
    pub form_action_origin: Option<String>,
    pub http_realm: Option<String>,
    pub username_field: String,
    pub password_field: String,

    // secure fields
    pub username: String,
    pub password: String,
}

impl LoginEntry {
    pub fn new(fields: LoginFields, sec_fields: SecureLoginFields) -> Self {
        Self {
            origin: fields.origin,
            form_action_origin: fields.form_action_origin,
            http_realm: fields.http_realm,
            username_field: fields.username_field,
            password_field: fields.password_field,

            username: sec_fields.username,
            password: sec_fields.password,
        }
    }

    /// Helper for validation and fixups of an "origin" provided as a string.
    pub fn validate_and_fixup_origin(origin: &str) -> Result<Option<String>> {
        // Check we can parse the origin, then use the normalized version of it.
        match Url::parse(origin) {
            Ok(mut u) => {
                // Presumably this is a faster path than always setting?
                if u.path() != "/"
                    || u.fragment().is_some()
                    || u.query().is_some()
                    || u.username() != "/"
                    || u.password().is_some()
                {
                    // Not identical - we only want the origin part, so kill
                    // any other parts which may exist.
                    // But first special case `file://` URLs which always
                    // resolve to `file://`
                    if u.scheme() == "file" {
                        return Ok(if origin == "file://" {
                            None
                        } else {
                            Some("file://".into())
                        });
                    }
                    u.set_path("");
                    u.set_fragment(None);
                    u.set_query(None);
                    let _ = u.set_username("");
                    let _ = u.set_password(None);
                    let mut href = String::from(u);
                    // We always store without the trailing "/" which Urls have.
                    if href.ends_with('/') {
                        href.pop().expect("url must have a length");
                    }
                    if origin != href {
                        // Needs to be fixed up.
                        return Ok(Some(href));
                    }
                }
                Ok(None)
            }
            Err(e) => {
                breadcrumb!(
                    "Error parsing login origin: {e:?} ({})",
                    error_support::redact_url(origin)
                );
                // We can't fixup completely invalid records, so always throw.
                Err(InvalidLogin::IllegalOrigin {
                    reason: e.to_string(),
                }
                .into())
            }
        }
    }
}

/// A login handed over from the store API, which has been persisted and contains persistence
/// information such as id and time stamps
#[derive(Debug, Clone, Hash, PartialEq, Eq, Default)]
pub struct Login {
    // meta fields
    pub id: String,
    pub time_created: i64,
    pub time_password_changed: i64,
    pub time_last_used: i64,
    pub times_used: i64,

    // login fields
    pub origin: String,
    pub form_action_origin: Option<String>,
    pub http_realm: Option<String>,
    pub username_field: String,
    pub password_field: String,

    // secure fields
    pub username: String,
    pub password: String,

    // breach alerts
    pub time_of_last_breach: Option<i64>,
    pub time_last_breach_alert_dismissed: Option<i64>,
}

impl Login {
    pub fn new(meta: LoginMeta, fields: LoginFields, sec_fields: SecureLoginFields) -> Self {
        Self {
            id: meta.id,
            time_created: meta.time_created,
            time_password_changed: meta.time_password_changed,
            time_last_used: meta.time_last_used,
            times_used: meta.times_used,

            origin: fields.origin,
            form_action_origin: fields.form_action_origin,
            http_realm: fields.http_realm,
            username_field: fields.username_field,
            password_field: fields.password_field,

            username: sec_fields.username,
            password: sec_fields.password,

            time_of_last_breach: fields.time_last_breach_alert_dismissed,
            time_last_breach_alert_dismissed: fields.time_last_breach_alert_dismissed,
        }
    }

    #[inline]
    pub fn guid(&self) -> Guid {
        Guid::from_string(self.id.clone())
    }

    pub fn entry(&self) -> LoginEntry {
        LoginEntry {
            origin: self.origin.clone(),
            form_action_origin: self.form_action_origin.clone(),
            http_realm: self.http_realm.clone(),
            username_field: self.username_field.clone(),
            password_field: self.password_field.clone(),

            username: self.username.clone(),
            password: self.password.clone(),
        }
    }

    pub fn encrypt(self, encdec: &dyn EncryptorDecryptor) -> Result<EncryptedLogin> {
        let sec_fields = SecureLoginFields {
            username: self.username,
            password: self.password,
        }
        .encrypt(encdec, &self.id)?;
        Ok(EncryptedLogin {
            meta: LoginMeta {
                id: self.id,
                time_created: self.time_created,
                time_password_changed: self.time_password_changed,
                time_last_used: self.time_last_used,
                times_used: self.times_used,
            },
            fields: LoginFields {
                origin: self.origin,
                form_action_origin: self.form_action_origin,
                http_realm: self.http_realm,
                username_field: self.username_field,
                password_field: self.password_field,
                time_of_last_breach: self.time_last_breach_alert_dismissed,
                time_last_breach_alert_dismissed: self.time_last_breach_alert_dismissed,
            },
            sec_fields,
        })
    }
}

/// A login stored in the database
#[derive(Debug, Clone, Hash, PartialEq, Eq, Default)]
pub struct EncryptedLogin {
    pub meta: LoginMeta,
    pub fields: LoginFields,
    pub sec_fields: String,
}

impl EncryptedLogin {
    #[inline]
    pub fn guid(&self) -> Guid {
        Guid::from_string(self.meta.id.clone())
    }

    // TODO: Remove this: https://github.com/mozilla/application-services/issues/4185
    #[inline]
    pub fn guid_str(&self) -> &str {
        &self.meta.id
    }

    pub fn decrypt(self, encdec: &dyn EncryptorDecryptor) -> Result<Login> {
        let sec_fields = self.decrypt_fields(encdec)?;
        Ok(Login::new(self.meta, self.fields, sec_fields))
    }

    pub fn decrypt_fields(&self, encdec: &dyn EncryptorDecryptor) -> Result<SecureLoginFields> {
        SecureLoginFields::decrypt(&self.sec_fields, encdec, &self.meta.id)
    }

    pub(crate) fn from_row(row: &Row<'_>) -> Result<EncryptedLogin> {
        let login = EncryptedLogin {
            meta: LoginMeta {
                id: row.get("guid")?,
                time_created: row.get("timeCreated")?,
                // Might be null
                time_last_used: row
                    .get::<_, Option<i64>>("timeLastUsed")?
                    .unwrap_or_default(),

                time_password_changed: row.get("timePasswordChanged")?,
                times_used: row.get("timesUsed")?,
            },
            fields: LoginFields {
                origin: row.get("origin")?,
                http_realm: row.get("httpRealm")?,

                form_action_origin: row.get("formActionOrigin")?,

                username_field: string_or_default(row, "usernameField")?,
                password_field: string_or_default(row, "passwordField")?,

                time_of_last_breach: row.get::<_, Option<i64>>("timeOfLastBreach")?,
                time_last_breach_alert_dismissed: row
                    .get::<_, Option<i64>>("timeLastBreachAlertDismissed")?,
            },
            sec_fields: row.get("secFields")?,
        };
        // XXX - we used to perform a fixup here, but that seems heavy-handed
        // and difficult - we now only do that on add/insert when we have the
        // encryption key.
        Ok(login)
    }
}

fn string_or_default(row: &Row<'_>, col: &str) -> Result<String> {
    Ok(row.get::<_, Option<String>>(col)?.unwrap_or_default())
}

pub trait ValidateAndFixup {
    // Our validate and fixup functions.
    fn check_valid(&self) -> Result<()>
    where
        Self: Sized,
    {
        self.validate_and_fixup(false)?;
        Ok(())
    }

    fn fixup(self) -> Result<Self>
    where
        Self: Sized,
    {
        match self.maybe_fixup()? {
            None => Ok(self),
            Some(login) => Ok(login),
        }
    }

    fn maybe_fixup(&self) -> Result<Option<Self>>
    where
        Self: Sized,
    {
        self.validate_and_fixup(true)
    }

    // validates, and optionally fixes, a struct. If fixup is false and there is a validation
    // issue, an `Err` is returned. If fixup is true and a problem was fixed, and `Ok(Some<Self>)`
    // is returned with the fixed version. If there was no validation problem, `Ok(None)` is
    // returned.
    fn validate_and_fixup(&self, fixup: bool) -> Result<Option<Self>>
    where
        Self: Sized;
}

impl ValidateAndFixup for LoginEntry {
    fn validate_and_fixup(&self, fixup: bool) -> Result<Option<Self>> {
        // XXX TODO: we've definitely got more validation and fixups to add here!

        let mut maybe_fixed = None;

        /// A little helper to magic a Some(self.clone()) into existence when needed.
        macro_rules! get_fixed_or_throw {
            ($err:expr) => {
                // This is a block expression returning a local variable,
                // entirely so we can give it an explicit type declaration.
                {
                    if !fixup {
                        return Err($err.into());
                    }
                    warn!("Fixing login record {:?}", $err);
                    let fixed: Result<&mut Self> =
                        Ok(maybe_fixed.get_or_insert_with(|| self.clone()));
                    fixed
                }
            };
        }

        if self.origin.is_empty() {
            return Err(InvalidLogin::EmptyOrigin.into());
        }

        if self.form_action_origin.is_some() && self.http_realm.is_some() {
            get_fixed_or_throw!(InvalidLogin::BothTargets)?.http_realm = None;
        }

        if self.form_action_origin.is_none() && self.http_realm.is_none() {
            return Err(InvalidLogin::NoTarget.into());
        }

        let form_action_origin = self.form_action_origin.clone().unwrap_or_default();
        let http_realm = maybe_fixed
            .as_ref()
            .unwrap_or(self)
            .http_realm
            .clone()
            .unwrap_or_default();

        let field_data = [
            ("form_action_origin", &form_action_origin),
            ("http_realm", &http_realm),
            ("origin", &self.origin),
            ("username_field", &self.username_field),
            ("password_field", &self.password_field),
        ];

        for (field_name, field_value) in &field_data {
            // Nuls are invalid.
            if field_value.contains('\0') {
                return Err(InvalidLogin::IllegalFieldValue {
                    field_info: format!("`{}` contains Nul", field_name),
                }
                .into());
            }

            // Newlines are invalid in Desktop for all the fields here.
            if field_value.contains('\n') || field_value.contains('\r') {
                return Err(InvalidLogin::IllegalFieldValue {
                    field_info: format!("`{}` contains newline", field_name),
                }
                .into());
            }
        }

        // Desktop doesn't like fields with the below patterns
        if self.username_field == "." {
            return Err(InvalidLogin::IllegalFieldValue {
                field_info: "`username_field` is a period".into(),
            }
            .into());
        }

        // Check we can parse the origin, then use the normalized version of it.
        if let Some(fixed) = Self::validate_and_fixup_origin(&self.origin)? {
            get_fixed_or_throw!(InvalidLogin::IllegalFieldValue {
                field_info: "Origin is not normalized".into()
            })?
            .origin = fixed;
        }

        match &maybe_fixed.as_ref().unwrap_or(self).form_action_origin {
            None => {
                if !self.username_field.is_empty() {
                    get_fixed_or_throw!(InvalidLogin::IllegalFieldValue {
                        field_info: "username_field must be empty when form_action_origin is null"
                            .into()
                    })?
                    .username_field
                    .clear();
                }
                if !self.password_field.is_empty() {
                    get_fixed_or_throw!(InvalidLogin::IllegalFieldValue {
                        field_info: "password_field must be empty when form_action_origin is null"
                            .into()
                    })?
                    .password_field
                    .clear();
                }
            }
            Some(href) => {
                // "", ".", and "javascript:" are special cases documented at the top of this file.
                if href == "." {
                    // A bit of a special case - if we are being asked to fixup, we replace
                    // "." with an empty string - but if not fixing up we don't complain.
                    if fixup {
                        maybe_fixed
                            .get_or_insert_with(|| self.clone())
                            .form_action_origin = Some("".into());
                    }
                } else if !href.is_empty() && href != "javascript:" {
                    if let Some(fixed) = Self::validate_and_fixup_origin(href)? {
                        get_fixed_or_throw!(InvalidLogin::IllegalFieldValue {
                            field_info: "form_action_origin is not normalized".into()
                        })?
                        .form_action_origin = Some(fixed);
                    }
                }
            }
        }

        // secure fields
        //
        // \r\n chars are valid in desktop for some reason, so we allow them here too.
        if self.username.contains('\0') {
            return Err(InvalidLogin::IllegalFieldValue {
                field_info: "`username` contains Nul".into(),
            }
            .into());
        }
        if self.password.is_empty() {
            return Err(InvalidLogin::EmptyPassword.into());
        }
        if self.password.contains('\0') {
            return Err(InvalidLogin::IllegalFieldValue {
                field_info: "`password` contains Nul".into(),
            }
            .into());
        }

        Ok(maybe_fixed)
    }
}

#[cfg(test)]
pub mod test_utils {
    use super::*;
    use crate::encryption::test_utils::encrypt_struct;

    // Factory function to make a new login
    //
    // It uses the guid to create a unique origin/form_action_origin
    pub fn enc_login(id: &str, password: &str) -> EncryptedLogin {
        let sec_fields = SecureLoginFields {
            username: "user".to_string(),
            password: password.to_string(),
        };
        EncryptedLogin {
            meta: LoginMeta {
                id: id.to_string(),
                ..Default::default()
            },
            fields: LoginFields {
                form_action_origin: Some(format!("https://{}.example.com", id)),
                origin: format!("https://{}.example.com", id),
                ..Default::default()
            },
            // TODO: fixme
            sec_fields: encrypt_struct(&sec_fields),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_url_fixups() -> Result<()> {
        // Start with URLs which are all valid and already normalized.
        for input in &[
            // The list of valid origins documented at the top of this file.
            "https://site.com",
            "http://site.com:1234",
            "ftp://ftp.site.com",
            "moz-proxy://127.0.0.1:8888",
            "chrome://MyLegacyExtension",
            "file://",
            "https://[::1]",
        ] {
            assert_eq!(LoginEntry::validate_and_fixup_origin(input)?, None);
        }

        // And URLs which get normalized.
        for (input, output) in &[
            ("https://site.com/", "https://site.com"),
            ("http://site.com:1234/", "http://site.com:1234"),
            ("http://example.com/foo?query=wtf#bar", "http://example.com"),
            ("http://example.com/foo#bar", "http://example.com"),
            (
                "http://username:password@example.com/",
                "http://example.com",
            ),
            ("http://😍.com/", "http://xn--r28h.com"),
            ("https://[0:0:0:0:0:0:0:1]", "https://[::1]"),
            // All `file://` URLs normalize to exactly `file://`. See #2384 for
            // why we might consider changing that later.
            ("file:///", "file://"),
            ("file://foo/bar", "file://"),
            ("file://foo/bar/", "file://"),
            ("moz-proxy://127.0.0.1:8888/", "moz-proxy://127.0.0.1:8888"),
            (
                "moz-proxy://127.0.0.1:8888/foo",
                "moz-proxy://127.0.0.1:8888",
            ),
            ("chrome://MyLegacyExtension/", "chrome://MyLegacyExtension"),
            (
                "chrome://MyLegacyExtension/foo",
                "chrome://MyLegacyExtension",
            ),
        ] {
            assert_eq!(
                LoginEntry::validate_and_fixup_origin(input)?,
                Some((*output).into())
            );
        }

        // Finally, look at some invalid logins
        for input in &[".", "example", "example.com"] {
            assert!(LoginEntry::validate_and_fixup_origin(input).is_err());
        }

        Ok(())
    }

    #[test]
    fn test_check_valid() {
        #[derive(Debug, Clone)]
        struct TestCase {
            login: LoginEntry,
            should_err: bool,
            expected_err: &'static str,
        }

        let valid_login = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_empty_origin = LoginEntry {
            origin: "".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_empty_password = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "".into(),
            ..Default::default()
        };

        let login_with_form_submit_and_http_realm = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            form_action_origin: Some("https://www.example.com".into()),
            username: "".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_without_form_submit_or_http_realm = LoginEntry {
            origin: "https://www.example.com".into(),
            username: "".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_legacy_form_submit_and_http_realm = LoginEntry {
            origin: "https://www.example.com".into(),
            form_action_origin: Some("".into()),
            username: "".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_null_http_realm = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.\0com".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_null_username = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "\0".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_null_password = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "username".into(),
            password: "test\0".into(),
            ..Default::default()
        };

        let login_with_newline_origin = LoginEntry {
            origin: "\rhttps://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_newline_username_field = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username_field: "\n".into(),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_newline_realm = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("foo\nbar".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_newline_password = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "test\n".into(),
            ..Default::default()
        };

        let login_with_period_username_field = LoginEntry {
            origin: "https://www.example.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username_field: ".".into(),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_period_form_action_origin = LoginEntry {
            form_action_origin: Some(".".into()),
            origin: "https://www.example.com".into(),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_javascript_form_action_origin = LoginEntry {
            form_action_origin: Some("javascript:".into()),
            origin: "https://www.example.com".into(),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_malformed_origin_parens = LoginEntry {
            origin: " (".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_host_unicode = LoginEntry {
            origin: "http://💖.com".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_origin_trailing_slash = LoginEntry {
            origin: "https://www.example.com/".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_origin_expanded_ipv6 = LoginEntry {
            origin: "https://[0:0:0:0:0:0:1:1]".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_unknown_protocol = LoginEntry {
            origin: "moz-proxy://127.0.0.1:8888".into(),
            http_realm: Some("https://www.example.com".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let test_cases = [
            TestCase {
                login: valid_login,
                should_err: false,
                expected_err: "",
            },
            TestCase {
                login: login_with_empty_origin,
                should_err: true,
                expected_err: "Invalid login: Origin is empty",
            },
            TestCase {
                login: login_with_empty_password,
                should_err: true,
                expected_err: "Invalid login: Password is empty",
            },
            TestCase {
                login: login_with_form_submit_and_http_realm,
                should_err: true,
                expected_err: "Invalid login: Both `formActionOrigin` and `httpRealm` are present",
            },
            TestCase {
                login: login_without_form_submit_or_http_realm,
                should_err: true,
                expected_err:
                    "Invalid login: Neither `formActionOrigin` or `httpRealm` are present",
            },
            TestCase {
                login: login_with_null_http_realm,
                should_err: true,
                expected_err: "Invalid login: Login has illegal field: `http_realm` contains Nul",
            },
            TestCase {
                login: login_with_null_username,
                should_err: true,
                expected_err: "Invalid login: Login has illegal field: `username` contains Nul",
            },
            TestCase {
                login: login_with_null_password,
                should_err: true,
                expected_err: "Invalid login: Login has illegal field: `password` contains Nul",
            },
            TestCase {
                login: login_with_newline_origin,
                should_err: true,
                expected_err: "Invalid login: Login has illegal field: `origin` contains newline",
            },
            TestCase {
                login: login_with_newline_realm,
                should_err: true,
                expected_err:
                    "Invalid login: Login has illegal field: `http_realm` contains newline",
            },
            TestCase {
                login: login_with_newline_username_field,
                should_err: true,
                expected_err:
                    "Invalid login: Login has illegal field: `username_field` contains newline",
            },
            TestCase {
                login: login_with_newline_password,
                should_err: false,
                expected_err: "",
            },
            TestCase {
                login: login_with_period_username_field,
                should_err: true,
                expected_err:
                    "Invalid login: Login has illegal field: `username_field` is a period",
            },
            TestCase {
                login: login_with_period_form_action_origin,
                should_err: false,
                expected_err: "",
            },
            TestCase {
                login: login_with_javascript_form_action_origin,
                should_err: false,
                expected_err: "",
            },
            TestCase {
                login: login_with_malformed_origin_parens,
                should_err: true,
                expected_err:
                    "Invalid login: Login has illegal origin: relative URL without a base",
            },
            TestCase {
                login: login_with_host_unicode,
                should_err: true,
                expected_err: "Invalid login: Login has illegal field: Origin is not normalized",
            },
            TestCase {
                login: login_with_origin_trailing_slash,
                should_err: true,
                expected_err: "Invalid login: Login has illegal field: Origin is not normalized",
            },
            TestCase {
                login: login_with_origin_expanded_ipv6,
                should_err: true,
                expected_err: "Invalid login: Login has illegal field: Origin is not normalized",
            },
            TestCase {
                login: login_with_unknown_protocol,
                should_err: false,
                expected_err: "",
            },
            TestCase {
                login: login_with_legacy_form_submit_and_http_realm,
                should_err: false,
                expected_err: "",
            },
        ];

        for tc in &test_cases {
            let actual = tc.login.check_valid();

            if tc.should_err {
                assert!(actual.is_err(), "{:#?}", tc);
                assert_eq!(
                    tc.expected_err,
                    actual.unwrap_err().to_string(),
                    "{:#?}",
                    tc,
                );
            } else {
                assert!(actual.is_ok(), "{:#?}", tc);
                assert!(
                    tc.login.clone().fixup().is_ok(),
                    "Fixup failed after check_valid passed: {:#?}",
                    &tc,
                );
            }
        }
    }

    #[test]
    fn test_fixup() {
        #[derive(Debug, Default)]
        struct TestCase {
            login: LoginEntry,
            fixedup_host: Option<&'static str>,
            fixedup_form_action_origin: Option<String>,
        }

        // Note that most URL fixups are tested above, but we have one or 2 here.
        let login_with_full_url = LoginEntry {
            origin: "http://example.com/foo?query=wtf#bar".into(),
            form_action_origin: Some("http://example.com/foo?query=wtf#bar".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_host_unicode = LoginEntry {
            origin: "http://😍.com".into(),
            form_action_origin: Some("http://😍.com".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_period_fsu = LoginEntry {
            origin: "https://example.com".into(),
            form_action_origin: Some(".".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };
        let login_with_empty_fsu = LoginEntry {
            origin: "https://example.com".into(),
            form_action_origin: Some("".into()),
            username: "test".into(),
            password: "test".into(),
            ..Default::default()
        };

        let login_with_form_submit_and_http_realm = LoginEntry {
            origin: "https://www.example.com".into(),
            form_action_origin: Some("https://www.example.com".into()),
            // If both http_realm and form_action_origin are specified, we drop
            // the former when fixing up. So for this test we must have an
            // invalid value in http_realm to ensure we don't validate a value
            // we end up dropping.
            http_realm: Some("\n".into()),
            username: "".into(),
            password: "test".into(),
            ..Default::default()
        };

        let test_cases = [
            TestCase {
                login: login_with_full_url,
                fixedup_host: "http://example.com".into(),
                fixedup_form_action_origin: Some("http://example.com".into()),
            },
            TestCase {
                login: login_with_host_unicode,
                fixedup_host: "http://xn--r28h.com".into(),
                fixedup_form_action_origin: Some("http://xn--r28h.com".into()),
            },
            TestCase {
                login: login_with_period_fsu,
                fixedup_form_action_origin: Some("".into()),
                ..TestCase::default()
            },
            TestCase {
                login: login_with_form_submit_and_http_realm,
                fixedup_form_action_origin: Some("https://www.example.com".into()),
                ..TestCase::default()
            },
            TestCase {
                login: login_with_empty_fsu,
                // Should still be empty.
                fixedup_form_action_origin: Some("".into()),
                ..TestCase::default()
            },
        ];

        for tc in &test_cases {
            let login = tc.login.clone().fixup().expect("should work");
            if let Some(expected) = tc.fixedup_host {
                assert_eq!(login.origin, expected, "origin not fixed in {:#?}", tc);
            }
            assert_eq!(
                login.form_action_origin, tc.fixedup_form_action_origin,
                "form_action_origin not fixed in {:#?}",
                tc,
            );
            login.check_valid().unwrap_or_else(|e| {
                panic!("Fixup produces invalid record: {:#?}", (e, &tc, &login));
            });
            assert_eq!(
                login.clone().fixup().unwrap(),
                login,
                "fixup did not reach fixed point for testcase: {:#?}",
                tc,
            );
        }
    }

    #[test]
    fn test_secure_fields_serde() {
        let sf = SecureLoginFields {
            username: "foo".into(),
            password: "pwd".into(),
        };
        assert_eq!(
            serde_json::to_string(&sf).unwrap(),
            r#"{"u":"foo","p":"pwd"}"#
        );
        let got: SecureLoginFields = serde_json::from_str(r#"{"u": "user", "p": "p"}"#).unwrap();
        let expected = SecureLoginFields {
            username: "user".into(),
            password: "p".into(),
        };
        assert_eq!(got, expected);
    }
}
