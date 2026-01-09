/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Login entry from a server payload
//
// This struct is used for fetching/sending login records to the server.  There are a number
// of differences between this and the top-level Login struct; some fields are renamed, some are
// locally encrypted, etc.
use crate::encryption::EncryptorDecryptor;
use crate::error::*;
use crate::login::ValidateAndFixup;
use crate::SecureLoginFields;
use crate::{EncryptedLogin, LoginEntry, LoginFields, LoginMeta};
use serde_derive::*;
use sync15::bso::OutgoingBso;
use sync_guid::Guid;

type UnknownFields = serde_json::Map<String, serde_json::Value>;

trait UnknownFieldsExt {
    fn encrypt(&self, encdec: &dyn EncryptorDecryptor) -> Result<String>;
    fn decrypt(ciphertext: &str, encdec: &dyn EncryptorDecryptor) -> Result<Self>
    where
        Self: Sized;
}

impl UnknownFieldsExt for UnknownFields {
    fn encrypt(&self, encdec: &dyn EncryptorDecryptor) -> Result<String> {
        let string = serde_json::to_string(&self)?;
        let cipherbytes = encdec
            .encrypt(string.as_bytes().into())
            .map_err(|e| Error::EncryptionFailed(e.to_string()))?;
        let ciphertext = std::str::from_utf8(&cipherbytes)
            .map_err(|e| Error::EncryptionFailed(e.to_string()))?;
        Ok(ciphertext.to_owned())
    }

    fn decrypt(ciphertext: &str, encdec: &dyn EncryptorDecryptor) -> Result<Self> {
        let jsonbytes = encdec
            .decrypt(ciphertext.as_bytes().into())
            .map_err(|e| Error::DecryptionFailed(e.to_string()))?;
        let json =
            std::str::from_utf8(&jsonbytes).map_err(|e| Error::DecryptionFailed(e.to_string()))?;
        Ok(serde_json::from_str(json)?)
    }
}

/// What we get from the server after parsing the payload. We need to round-trip "unknown"
/// fields, but don't want to carry them around in `EncryptedLogin`.
#[derive(Debug)]
pub(super) struct IncomingLogin {
    pub login: EncryptedLogin,
    // An encrypted UnknownFields, or None if there are none.
    pub unknown: Option<String>,
}

impl IncomingLogin {
    pub fn guid(&self) -> Guid {
        self.login.guid()
    }

    pub(super) fn from_incoming_payload(
        p: LoginPayload,
        encdec: &dyn EncryptorDecryptor,
    ) -> Result<Self> {
        let original_fields = LoginFields {
            origin: p.hostname,
            form_action_origin: p.form_submit_url,
            http_realm: p.http_realm,
            username_field: p.username_field,
            password_field: p.password_field,
            time_of_last_breach: p.time_of_last_breach,
            time_last_breach_alert_dismissed: p.time_last_breach_alert_dismissed,
        };
        let original_sec_fields = SecureLoginFields {
            username: p.username,
            password: p.password,
        };
        // we do a bit of a dance here to maybe_fixup() the fields via LoginEntry
        let original_login_entry = LoginEntry::new(original_fields, original_sec_fields);
        let login_entry = original_login_entry
            .maybe_fixup()?
            .unwrap_or(original_login_entry);
        let fields = LoginFields {
            origin: login_entry.origin,
            form_action_origin: login_entry.form_action_origin,
            http_realm: login_entry.http_realm,
            username_field: login_entry.username_field,
            password_field: login_entry.password_field,
            time_of_last_breach: None,
            time_last_breach_alert_dismissed: None,
        };
        let id = String::from(p.guid);
        let sec_fields = SecureLoginFields {
            username: login_entry.username,
            password: login_entry.password,
        }
        .encrypt(encdec, &id)?;

        // We handle NULL in the DB for migrated databases and it's wasteful
        // to encrypt the common case of an empty map, so...
        let unknown = if p.unknown_fields.is_empty() {
            None
        } else {
            Some(p.unknown_fields.encrypt(encdec)?)
        };

        // If we can't fix the parts we keep the invalid bits.
        Ok(Self {
            login: EncryptedLogin {
                meta: LoginMeta {
                    id,
                    time_created: p.time_created,
                    time_password_changed: p.time_password_changed,
                    time_last_used: p.time_last_used,
                    times_used: p.times_used,
                },
                fields,
                sec_fields,
            },
            unknown,
        })
    }
}

/// The JSON payload that lives on the storage servers.
#[derive(Debug, Clone, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct LoginPayload {
    #[serde(rename = "id")]
    pub guid: Guid,

    // This is 'origin' in our Login struct.
    pub hostname: String,

    // This is 'form_action_origin' in our Login struct.
    // rename_all = "camelCase" by default will do formSubmitUrl, but we can just
    // override this one field.
    #[serde(rename = "formSubmitURL")]
    #[serde(skip_serializing_if = "Option::is_none")]
    pub form_submit_url: Option<String>,

    #[serde(skip_serializing_if = "Option::is_none")]
    pub http_realm: Option<String>,

    #[serde(default)]
    pub username: String,

    pub password: String,

    #[serde(default)]
    pub username_field: String,

    #[serde(default)]
    pub password_field: String,

    #[serde(default)]
    #[serde(deserialize_with = "deserialize_timestamp")]
    pub time_created: i64,

    #[serde(default)]
    #[serde(deserialize_with = "deserialize_timestamp")]
    pub time_password_changed: i64,

    #[serde(default)]
    #[serde(deserialize_with = "deserialize_timestamp")]
    pub time_last_used: i64,

    #[serde(default)]
    pub times_used: i64,

    // Additional "unknown" round-tripped fields.
    #[serde(flatten)]
    unknown_fields: UnknownFields,

    #[serde(default)]
    #[serde(deserialize_with = "deserialize_optional_timestamp")]
    pub time_of_last_breach: Option<i64>,

    #[serde(default)]
    #[serde(deserialize_with = "deserialize_optional_timestamp")]
    pub time_last_breach_alert_dismissed: Option<i64>,
}

// These probably should be on the payload itself, but one refactor at a time!
impl EncryptedLogin {
    pub fn into_bso(
        self,
        encdec: &dyn EncryptorDecryptor,
        enc_unknown_fields: Option<String>,
    ) -> Result<OutgoingBso> {
        let unknown_fields = match enc_unknown_fields {
            Some(s) => UnknownFields::decrypt(&s, encdec)?,
            None => Default::default(),
        };
        let sec_fields = SecureLoginFields::decrypt(&self.sec_fields, encdec, &self.meta.id)?;
        Ok(OutgoingBso::from_content_with_id(
            crate::sync::LoginPayload {
                guid: self.guid(),
                hostname: self.fields.origin,
                form_submit_url: self.fields.form_action_origin,
                http_realm: self.fields.http_realm,
                username_field: self.fields.username_field,
                password_field: self.fields.password_field,
                username: sec_fields.username,
                password: sec_fields.password,
                time_created: self.meta.time_created,
                time_password_changed: self.meta.time_password_changed,
                time_last_used: self.meta.time_last_used,
                times_used: self.meta.times_used,
                time_of_last_breach: self.fields.time_of_last_breach,
                time_last_breach_alert_dismissed: self.fields.time_last_breach_alert_dismissed,
                unknown_fields,
            },
        )?)
    }
}

// Quiet clippy, since this function is passed to deserialiaze_with...
#[allow(clippy::unnecessary_wraps)]
fn deserialize_timestamp<'de, D>(deserializer: D) -> std::result::Result<i64, D::Error>
where
    D: serde::de::Deserializer<'de>,
{
    use serde::de::Deserialize;
    // Invalid and negative timestamps are all replaced with 0. Eventually we
    // should investigate replacing values that are unreasonable but still fit
    // in an i64 (a date 1000 years in the future, for example), but
    // appropriately handling that is complex.
    Ok(i64::deserialize(deserializer).unwrap_or_default().max(0))
}

// Quiet clippy, since this function is passed to deserialiaze_with...
#[allow(clippy::unnecessary_wraps)]
fn deserialize_optional_timestamp<'de, D>(
    deserializer: D,
) -> std::result::Result<Option<i64>, D::Error>
where
    D: serde::de::Deserializer<'de>,
{
    use serde::de::Deserialize;
    Ok(i64::deserialize(deserializer).ok())
}

#[cfg(not(feature = "keydb"))]
#[cfg(test)]
mod tests {
    use super::*;
    use crate::encryption::test_utils::{encrypt_struct, TEST_ENCDEC};
    use crate::sync::merge::SyncLoginData;
    use crate::{EncryptedLogin, LoginFields, LoginMeta, SecureLoginFields};
    use sync15::bso::IncomingBso;

    #[test]
    fn test_payload_to_login() {
        let bso = IncomingBso::from_test_content(serde_json::json!({
            "id": "123412341234",
            "httpRealm": "test",
            "hostname": "https://www.example.com",
            "username": "user",
            "password": "password",
        }));
        let login = IncomingLogin::from_incoming_payload(
            bso.into_content::<LoginPayload>().content().unwrap(),
            &*TEST_ENCDEC,
        )
        .unwrap()
        .login;
        assert_eq!(login.meta.id, "123412341234");
        assert_eq!(login.fields.http_realm, Some("test".to_string()));
        assert_eq!(login.fields.origin, "https://www.example.com");
        assert_eq!(login.fields.form_action_origin, None);
        let sec_fields = login.decrypt_fields(&*TEST_ENCDEC).unwrap();
        assert_eq!(sec_fields.username, "user");
        assert_eq!(sec_fields.password, "password");
    }

    // formSubmitURL (now formActionOrigin) being an empty string is a valid
    // legacy case that is supported on desktop, we should ensure we are as well
    // https://searchfox.org/mozilla-central/rev/32c74afbb24dce4b5dd6b33be71197e615631d71/toolkit/components/passwordmgr/test/unit/test_logins_change.js#183-184
    #[test]
    fn test_payload_empty_form_action_to_login() {
        let bso = IncomingBso::from_test_content(serde_json::json!({
            "id": "123412341234",
            "formSubmitURL": "",
            "hostname": "https://www.example.com",
            "username": "user",
            "password": "password",
        }));
        let login = IncomingLogin::from_incoming_payload(
            bso.into_content::<LoginPayload>().content().unwrap(),
            &*TEST_ENCDEC,
        )
        .unwrap()
        .login;
        assert_eq!(login.meta.id, "123412341234");
        assert_eq!(login.fields.form_action_origin, Some("".to_string()));
        assert_eq!(login.fields.http_realm, None);
        assert_eq!(login.fields.origin, "https://www.example.com");
        let sec_fields = login.decrypt_fields(&*TEST_ENCDEC).unwrap();
        assert_eq!(sec_fields.username, "user");
        assert_eq!(sec_fields.password, "password");

        let bso = login.into_bso(&*TEST_ENCDEC, None).unwrap();
        assert_eq!(bso.envelope.id, "123412341234");
        let payload_data: serde_json::Value = serde_json::from_str(&bso.payload).unwrap();
        assert_eq!(payload_data["httpRealm"], serde_json::Value::Null);
        assert_eq!(payload_data["formSubmitURL"], "".to_string());
    }

    #[test]
    fn test_payload_unknown_fields() {
        // No "unknown" fields.
        let bso = IncomingBso::from_test_content(serde_json::json!({
            "id": "123412341234",
            "httpRealm": "test",
            "hostname": "https://www.example.com",
            "username": "user",
            "password": "password",
        }));
        let payload = bso.into_content::<LoginPayload>().content().unwrap();
        assert!(payload.unknown_fields.is_empty());

        // An unknown "foo"
        let bso = IncomingBso::from_test_content(serde_json::json!({
            "id": "123412341234",
            "httpRealm": "test",
            "hostname": "https://www.example.com",
            "username": "user",
            "password": "password",
            "foo": "bar",
        }));
        let payload = bso.into_content::<LoginPayload>().content().unwrap();
        assert_eq!(payload.unknown_fields.len(), 1);
        assert_eq!(
            payload.unknown_fields.get("foo").unwrap().as_str().unwrap(),
            "bar"
        );
        // re-serialize it.
        let unknown = Some(encrypt_struct::<UnknownFields>(&payload.unknown_fields));
        let login = IncomingLogin::from_incoming_payload(payload, &*TEST_ENCDEC)
            .unwrap()
            .login;
        // The raw outgoing payload should have it back.
        let outgoing = login.into_bso(&*TEST_ENCDEC, unknown).unwrap();
        let json =
            serde_json::from_str::<serde_json::Map<String, serde_json::Value>>(&outgoing.payload)
                .unwrap();
        assert_eq!(json.get("foo").unwrap().as_str().unwrap(), "bar");
    }

    #[test]
    fn test_form_submit_payload_to_login() {
        let bso = IncomingBso::from_test_content(serde_json::json!({
            "id": "123412341234",
            "hostname": "https://www.example.com",
            "formSubmitURL": "https://www.example.com",
            "usernameField": "username-field",
            "username": "user",
            "password": "password",
        }));
        let login = IncomingLogin::from_incoming_payload(
            bso.into_content::<LoginPayload>().content().unwrap(),
            &*TEST_ENCDEC,
        )
        .unwrap()
        .login;
        assert_eq!(login.meta.id, "123412341234");
        assert_eq!(login.fields.http_realm, None);
        assert_eq!(login.fields.origin, "https://www.example.com");
        assert_eq!(
            login.fields.form_action_origin,
            Some("https://www.example.com".to_string())
        );
        assert_eq!(login.fields.username_field, "username-field");
        let sec_fields = login.decrypt_fields(&*TEST_ENCDEC).unwrap();
        assert_eq!(sec_fields.username, "user");
        assert_eq!(sec_fields.password, "password");
    }

    #[test]
    fn test_login_into_payload() {
        let login = EncryptedLogin {
            meta: LoginMeta {
                id: "123412341234".into(),
                ..Default::default()
            },
            fields: LoginFields {
                http_realm: Some("test".into()),
                origin: "https://www.example.com".into(),
                ..Default::default()
            },
            sec_fields: encrypt_struct(&SecureLoginFields {
                username: "user".into(),
                password: "password".into(),
            }),
        };
        let bso = login.into_bso(&*TEST_ENCDEC, None).unwrap();
        assert_eq!(bso.envelope.id, "123412341234");
        let payload_data: serde_json::Value = serde_json::from_str(&bso.payload).unwrap();
        assert_eq!(payload_data["httpRealm"], "test".to_string());
        assert_eq!(payload_data["hostname"], "https://www.example.com");
        assert_eq!(payload_data["username"], "user");
        assert_eq!(payload_data["password"], "password");
        assert!(matches!(
            payload_data["formActionOrigin"],
            serde_json::Value::Null
        ));
    }

    #[test]
    fn test_username_field_requires_a_form_target() {
        let bad_json = serde_json::json!({
            "id": "123412341234",
            "httpRealm": "test",
            "hostname": "https://www.example.com",
            "username": "test",
            "password": "test",
            "usernameField": "invalid"
        });
        let bad_bso = IncomingBso::from_test_content(bad_json.clone());

        // Incoming sync data gets fixed automatically.
        let login = IncomingLogin::from_incoming_payload(
            bad_bso.into_content::<LoginPayload>().content().unwrap(),
            &*TEST_ENCDEC,
        )
        .unwrap()
        .login;
        assert_eq!(login.fields.username_field, "");

        // SyncLoginData::from_payload also fixes up.
        let bad_bso = IncomingBso::from_test_content(bad_json);
        let login = SyncLoginData::from_bso(bad_bso, &*TEST_ENCDEC)
            .unwrap()
            .inbound
            .unwrap()
            .login;
        assert_eq!(login.fields.username_field, "");
    }

    #[test]
    fn test_password_field_requires_a_form_target() {
        let bad_bso = IncomingBso::from_test_content(serde_json::json!({
            "id": "123412341234",
            "httpRealm": "test",
            "hostname": "https://www.example.com",
            "username": "test",
            "password": "test",
            "passwordField": "invalid"
        }));

        let login = IncomingLogin::from_incoming_payload(
            bad_bso.into_content::<LoginPayload>().content().unwrap(),
            &*TEST_ENCDEC,
        )
        .unwrap()
        .login;
        assert_eq!(login.fields.password_field, "");
    }
}
