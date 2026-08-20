/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! This module enhances the IncomingBso and OutgoingBso records to deal with
//! arbitrary <T> types, which we call "content"
//! It can:
//! * Parse JSON into some <T> while handling tombstones and invalid json.
//! * Turn arbitrary <T> objects with an `id` field into an OutgoingBso.

use super::{IncomingBso, IncomingContent, IncomingKind, OutgoingBso, OutgoingEnvelope};
use crate::Guid;
use crate::error::{trace, warn};
use error_support::report_error;
use serde::Serialize;
use serde::ser::Error as _;

// The only errors we return here are serde errors.
type Result<T> = std::result::Result<T, serde_json::Error>;

impl<T> IncomingContent<T> {
    /// Returns Some(content) if [self.kind] is [IncomingKind::Content], None otherwise.
    pub fn content(self) -> Option<T> {
        match self.kind {
            IncomingKind::Content(t) => Some(t),
            _ => None,
        }
    }
}

// We don't want to force our T to be Debug, but we can be Debug if T is.
impl<T: std::fmt::Debug> std::fmt::Debug for IncomingKind<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            IncomingKind::Content(r) => {
                write!(f, "IncomingKind::Content<{:?}>", r)
            }
            IncomingKind::Tombstone => write!(f, "IncomingKind::Tombstone"),
            IncomingKind::Malformed => write!(f, "IncomingKind::Malformed"),
        }
    }
}

impl IncomingBso {
    /// Convert an [IncomingBso] to an [IncomingContent] possibly holding a T.
    pub fn into_content<T: for<'de> serde::Deserialize<'de>>(self) -> IncomingContent<T> {
        self.into_content_with_fixup(|_| {})
    }

    /// Like into_content, but adds an additional fixup step where the caller can adjust the
    /// `serde_json::Value'
    pub fn into_content_with_fixup<T: for<'de> serde::Deserialize<'de>>(
        self,
        fixup: impl FnOnce(&mut serde_json::Value),
    ) -> IncomingContent<T> {
        match serde_json::from_str(&self.payload) {
            Ok(mut json) => {
                // We got a good serde_json::Value, run the fixup method
                fixup(&mut json);
                // ...now see if it's a <T>.
                let kind = json_to_kind(json, &self.envelope.id);
                IncomingContent {
                    envelope: self.envelope,
                    kind,
                }
            }
            Err(e) => {
                // payload isn't valid json.
                warn!("Invalid incoming cleartext {}: {}", self.envelope.id, e);
                IncomingContent {
                    envelope: self.envelope,
                    kind: IncomingKind::Malformed,
                }
            }
        }
    }
}

impl OutgoingBso {
    /// Creates a new tombstone record.
    /// Not all collections expect tombstones.
    pub fn new_tombstone(envelope: OutgoingEnvelope) -> Self {
        Self {
            envelope,
            payload: serde_json::json!({"deleted": true}).to_string(),
        }
    }

    /// Creates a outgoing record from some <T>, which can be made into a JSON object
    /// with a valid `id`. This is the most convenient way to create an outgoing
    /// item from a <T> when the default envelope is suitable.
    /// Will panic if there's no good `id` in the json.
    pub fn from_content_with_id<T>(record: T) -> Result<Self>
    where
        T: Serialize,
    {
        let (json, id) = content_with_id_to_json(record)?;
        Ok(Self {
            envelope: id.into(),
            payload: serde_json::to_string(&json)?,
        })
    }

    /// Create an Outgoing record with an explicit envelope. Will panic if the
    /// payload has an ID but it doesn't match the envelope.
    pub fn from_content<T>(envelope: OutgoingEnvelope, record: T) -> Result<Self>
    where
        T: Serialize,
    {
        let json = content_to_json(record, &envelope.id)?;
        Ok(Self {
            envelope,
            payload: serde_json::to_string(&json)?,
        })
    }
}

// Helpers for packing and unpacking serde objects to and from a <T>. In particular:
// * Helping deal complications around raw json payload not having 'id' (the envelope is
//   canonical) but needing it to exist when dealing with serde locally.
//   For example, a record on the server after being decrypted looks like:
//   `{"id": "a-guid", payload: {"field": "value"}}`
//   But the `T` for this typically looks like `struct T { id: Guid, field: String}`
//   So before we try and deserialize this record into a T, we copy the `id` field
//   from the envelope into the payload, and when serializing from a T we do the
//   reverse (ie, ensure the `id` in the payload is removed and placed in the envelope)
// * Tombstones.

// Deserializing json into a T
fn json_to_kind<T>(mut json: serde_json::Value, id: &Guid) -> IncomingKind<T>
where
    T: for<'de> serde::Deserialize<'de>,
{
    // It's possible that the payload does not carry 'id', but <T> always does - so grab it from the
    // envelope and put it into the json before deserializing the record.
    if let serde_json::Value::Object(ref mut map) = json {
        if map.contains_key("deleted") {
            return IncomingKind::Tombstone;
        }
        match map.get("id") {
            Some(serde_json::Value::String(content_id)) => {
                // It exists in the payload! We treat a mismatch as malformed.
                if content_id != id {
                    trace!(
                        "malformed incoming record: envelope id: {} payload id: {}",
                        content_id, id
                    );
                    report_error!(
                        "incoming-invalid-mismatched-ids",
                        "Envelope and payload don't agree on the ID"
                    );
                    return IncomingKind::Malformed;
                }
                if !id.is_valid_for_sync_server() {
                    trace!("malformed incoming record: id is not valid: {}", id);
                    report_error!(
                        "incoming-invalid-bad-payload-id",
                        "ID in the payload is invalid"
                    );
                    return IncomingKind::Malformed;
                }
            }
            Some(v) => {
                // It exists in the payload but is not a string - they can't possibly be
                // the same as the envelope uses a String, so must be malformed.
                trace!("malformed incoming record: id is not a string: {}", v);
                report_error!("incoming-invalid-wrong_type", "ID is not a string");
                return IncomingKind::Malformed;
            }
            None => {
                // Doesn't exist in the payload - add it before trying to deser a T.
                if !id.is_valid_for_sync_server() {
                    trace!("malformed incoming record: id is not valid: {}", id);
                    report_error!(
                        "incoming-invalid-bad-envelope-id",
                        "ID in envelope is not valid"
                    );
                    return IncomingKind::Malformed;
                }
                map.insert("id".to_string(), id.to_string().into());
            }
        }
    };
    match serde_path_to_error::deserialize(json) {
        Ok(v) => IncomingKind::Content(v),
        Err(e) => {
            report_error!(
                "invalid-incoming-content",
                "{}.{}: {}",
                std::any::type_name::<T>(),
                e.path(),
                e.inner()
            );
            IncomingKind::Malformed
        }
    }
}

// Serializing <T> into json with special handling of `id` (the `id` from the payload
// is used as the envelope ID)
fn content_with_id_to_json<T>(record: T) -> Result<(serde_json::Value, Guid)>
where
    T: Serialize,
{
    let mut json = serde_json::to_value(record)?;
    let id = match json.as_object_mut() {
        Some(ref mut map) => {
            match map.get("id").as_ref().and_then(|v| v.as_str()) {
                Some(id) => {
                    let id: Guid = id.into();
                    if !id.is_valid_for_sync_server() {
                        // This is a sanity check on our own IDs, not something the
                        // server enforces, so a violation is an error for this one
                        // record rather than a reason to panic the process, which
                        // took down the whole parent process (bug 2056116).
                        return Err(serde_json::Error::custom("record's ID is invalid"));
                    }
                    id
                }
                // In practice, this is a "static" error and not influenced by runtime behavior
                None => panic!("record does not have an ID in the payload"),
            }
        }
        None => panic!("record is not a json object"),
    };
    Ok((json, id))
}

// Serializing <T> into json with special handling of `id` (if `id` in serialized
// JSON already exists, we panic if it doesn't match the envelope. If the serialized
// content does not have an `id`, it is added from the envelope)
// is used as the envelope ID)
fn content_to_json<T>(record: T, id: &Guid) -> Result<serde_json::Value>
where
    T: Serialize,
{
    let mut payload = serde_json::to_value(record)?;
    if let Some(ref mut map) = payload.as_object_mut() {
        if let Some(content_id) = map.get("id").as_ref().and_then(|v| v.as_str()) {
            assert_eq!(content_id, id);
            if !id.is_valid_for_sync_server() {
                // See content_with_id_to_json: don't panic on an invalid ID.
                return Err(serde_json::Error::custom("record's ID is invalid"));
            }
        } else {
            map.insert("id".to_string(), serde_json::Value::String(id.to_string()));
        }
    };
    Ok(payload)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::bso::IncomingBso;
    use serde::{Deserialize, Serialize};
    use serde_json::json;

    #[derive(Default, Debug, PartialEq, Serialize, Deserialize)]
    struct TestStruct {
        id: Guid,
        data: u32,
    }
    #[test]
    fn test_content_deser() {
        error_support::init_for_tests();
        let json = json!({
            "id": "test",
            "payload": json!({"data": 1}).to_string(),
        });
        let incoming: IncomingBso = serde_json::from_value(json).unwrap();
        assert_eq!(incoming.envelope.id, "test");
        let record = incoming.into_content::<TestStruct>().content().unwrap();
        let expected = TestStruct {
            id: Guid::new("test"),
            data: 1,
        };
        assert_eq!(record, expected);
    }

    #[test]
    fn test_content_deser_empty_id() {
        error_support::init_for_tests();
        let json = json!({
            "id": "",
            "payload": json!({"data": 1}).to_string(),
        });
        let incoming: IncomingBso = serde_json::from_value(json).unwrap();
        // The envelope has an invalid ID, but it's not handled until we try and deserialize
        // it into a T
        assert_eq!(incoming.envelope.id, "");
        let content = incoming.into_content::<TestStruct>();
        assert!(matches!(content.kind, IncomingKind::Malformed));
    }

    #[test]
    fn test_content_deser_invalid() {
        error_support::init_for_tests();
        // And a non-empty but still invalid guid.
        let json = json!({
            "id": "X".repeat(65),
            "payload": json!({"data": 1}).to_string(),
        });
        let incoming: IncomingBso = serde_json::from_value(json).unwrap();
        let content = incoming.into_content::<TestStruct>();
        assert!(matches!(content.kind, IncomingKind::Malformed));
    }

    #[test]
    fn test_content_deser_not_string() {
        error_support::init_for_tests();
        // A non-string id.
        let json = json!({
            "id": "0",
            "payload": json!({"id": 0, "data": 1}).to_string(),
        });
        let incoming: IncomingBso = serde_json::from_value(json).unwrap();
        let content = incoming.into_content::<serde_json::Value>();
        assert!(matches!(content.kind, IncomingKind::Malformed));
    }

    #[test]
    fn test_content_ser_with_id() {
        error_support::init_for_tests();
        // When serializing, expect the ID to be in the top-level payload (ie,
        // in the envelope) but should not appear in the cleartext `payload` part of
        // the payload.
        let val = TestStruct {
            id: Guid::new("test"),
            data: 1,
        };
        let outgoing = OutgoingBso::from_content_with_id(val).unwrap();

        // The envelope should have our ID.
        assert_eq!(outgoing.envelope.id, Guid::new("test"));

        // and make sure `cleartext` part of the payload the data and the id.
        let ct_value = serde_json::from_str::<serde_json::Value>(&outgoing.payload).unwrap();
        assert_eq!(ct_value, json!({"data": 1, "id": "test"}));
    }

    #[test]
    fn test_content_ser_with_envelope() {
        error_support::init_for_tests();
        // When serializing, expect the ID to be in the top-level payload (ie,
        // in the envelope) but should not appear in the cleartext `payload`
        let val = TestStruct {
            id: Guid::new("test"),
            data: 1,
        };
        let envelope: OutgoingEnvelope = Guid::new("test").into();
        let outgoing = OutgoingBso::from_content(envelope, val).unwrap();

        // The envelope should have our ID.
        assert_eq!(outgoing.envelope.id, Guid::new("test"));

        // and make sure `cleartext` part of the payload has data and the id.
        let ct_value = serde_json::from_str::<serde_json::Value>(&outgoing.payload).unwrap();
        assert_eq!(ct_value, json!({"data": 1, "id": "test"}));
    }

    #[test]
    #[should_panic]
    fn test_content_ser_no_ids() {
        error_support::init_for_tests();
        #[derive(Serialize)]
        struct StructWithNoId {
            data: u32,
        }
        let val = StructWithNoId { data: 1 };
        let _ = OutgoingBso::from_content_with_id(val);
    }

    #[test]
    #[should_panic]
    fn test_content_ser_not_object() {
        error_support::init_for_tests();
        let _ = OutgoingBso::from_content_with_id(json!("string"));
    }

    #[test]
    #[should_panic]
    fn test_content_ser_mismatched_ids() {
        error_support::init_for_tests();
        let val = TestStruct {
            id: Guid::new("test"),
            data: 1,
        };
        let envelope: OutgoingEnvelope = Guid::new("different").into();
        let _ = OutgoingBso::from_content(envelope, val);
    }

    #[test]
    fn test_content_empty_id() {
        error_support::init_for_tests();
        let val = TestStruct {
            id: Guid::new(""),
            data: 1,
        };
        // An invalid ID is a recoverable error, not a panic (bug 2056116).
        assert!(OutgoingBso::from_content_with_id(val).is_err());
    }

    #[test]
    fn test_content_invalid_id() {
        error_support::init_for_tests();
        let val = TestStruct {
            id: Guid::new(&"X".repeat(65)),
            data: 1,
        };
        assert!(OutgoingBso::from_content_with_id(val).is_err());
    }
}
