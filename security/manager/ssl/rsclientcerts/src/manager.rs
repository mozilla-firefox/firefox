/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use pkcs11_bindings::*;
use rsclientcerts_util::error::{Error, ErrorType};
use rsclientcerts_util::error_here;
use std::collections::BTreeMap;
use std::convert::TryInto;
use std::ffi::c_void;

use crate::cryptoki::{CryptokiCert, CryptokiTrust};

/// Helper type for DoFindObjects-type functions where a rust `ClientCertsBackend` implementation
/// calls a C function to search for certificates and keys.
pub type FindObjectsCallback = Option<
    unsafe extern "C" fn(
        typ: u8,
        data_len: usize,
        data: *const u8,
        extra_len: usize,
        extra: *const u8,
        ctx: *mut c_void,
    ),
>;

/// Helper type for DoSign-type functions where a rust `ClientCertsBackend` implementation calls a C
/// function to sign some data.
pub type SignCallback =
    Option<unsafe extern "C" fn(data_len: usize, data: *const u8, ctx: *mut c_void)>;

pub trait CryptokiObject {
    fn matches(&self, attrs: &[(CK_ATTRIBUTE_TYPE, Vec<u8>)]) -> bool;
    fn get_attribute(&self, attribute: CK_ATTRIBUTE_TYPE) -> Option<&[u8]>;
}

pub trait Sign {
    fn get_signature_length(
        &mut self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<usize, Error>;
    fn sign(
        &mut self,
        data: &[u8],
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<Vec<u8>, Error>;
}

pub trait ClientCertsBackend {
    type Key: CryptokiObject + Sign;

    // Potentially search for CryptokiObjects and return them. If no search was performed,
    // the backend should return `Ok(None)`.
    #[allow(clippy::type_complexity)]
    fn find_objects(
        &mut self,
        slot_id: CK_SLOT_ID,
    ) -> Result<Option<(Vec<CryptokiCert>, Vec<Self::Key>, Vec<CryptokiTrust>)>, Error>;
    fn get_slot_info(&self) -> CK_SLOT_INFO;
    fn get_token_info(&self) -> CK_TOKEN_INFO;
    fn get_mechanism_list(&self) -> Vec<CK_MECHANISM_TYPE>;
    fn change_password(&mut self, _from: &[u8], _to: &[u8]) -> Result<(), Error> {
        Err(error_here!(ErrorType::LibraryFailure))
    }
    fn login(&mut self, _password: &[u8]) -> Result<(), Error> {
        Err(error_here!(ErrorType::LibraryFailure))
    }
    fn logout(&mut self) {}
    fn is_logged_in(&self) -> bool {
        false
    }
}

const SUPPORTED_ATTRIBUTES: &[CK_ATTRIBUTE_TYPE] = &[
    CKA_CERTIFICATE_TYPE,
    CKA_CLASS,
    CKA_EC_PARAMS,
    CKA_HASH_OF_CERTIFICATE,
    CKA_ID,
    CKA_ISSUER,
    CKA_KEY_TYPE,
    CKA_LABEL,
    CKA_MODULUS,
    CKA_NAME_HASH_ALGORITHM,
    CKA_PRIVATE,
    CKA_SERIAL_NUMBER,
    CKA_SUBJECT,
    CKA_TOKEN,
    CKA_VALUE,
    nss::CKA_PKCS_TRUST_CLIENT_AUTH,
    nss::CKA_PKCS_TRUST_CODE_SIGNING,
    nss::CKA_PKCS_TRUST_EMAIL_PROTECTION,
    nss::CKA_PKCS_TRUST_SERVER_AUTH,
];

enum Cryptoki<B: ClientCertsBackend> {
    Cert(CryptokiCert),
    Key(B::Key),
    Trust(CryptokiTrust),
}

impl<B: ClientCertsBackend> Cryptoki<B> {
    fn matches(&self, attrs: &[(CK_ATTRIBUTE_TYPE, Vec<u8>)]) -> bool {
        match self {
            Cryptoki::Cert(cert) => cert.matches(attrs),
            Cryptoki::Key(key) => key.matches(attrs),
            Cryptoki::Trust(key) => key.matches(attrs),
        }
    }

    fn get_attribute(&self, attribute: CK_ATTRIBUTE_TYPE) -> Option<&[u8]> {
        match self {
            Cryptoki::Cert(cert) => cert.get_attribute(attribute),
            Cryptoki::Key(key) => key.get_attribute(attribute),
            Cryptoki::Trust(trust) => trust.get_attribute(attribute),
        }
    }

    fn id(&self) -> Result<&[u8], Error> {
        let attribute = match self {
            Cryptoki::Cert(_) | Cryptoki::Key(_) => CKA_ID,
            Cryptoki::Trust(_) => CKA_HASH_OF_CERTIFICATE,
        };
        self.get_attribute(attribute)
            .ok_or_else(|| error_here!(ErrorType::LibraryFailure))
    }

    fn get_signature_length(
        &mut self,
        data: Vec<u8>,
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<usize, Error> {
        match self {
            Cryptoki::Cert(_) => Err(error_here!(ErrorType::InvalidArgument)),
            Cryptoki::Key(key) => key.get_signature_length(&data, params),
            Cryptoki::Trust(_) => Err(error_here!(ErrorType::InvalidArgument)),
        }
    }

    fn sign(
        &mut self,
        data: Vec<u8>,
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<Vec<u8>, Error> {
        match self {
            Cryptoki::Cert(_) => Err(error_here!(ErrorType::InvalidArgument)),
            Cryptoki::Key(key) => key.sign(&data, params),
            Cryptoki::Trust(_) => Err(error_here!(ErrorType::InvalidArgument)),
        }
    }
}

struct Object<B: ClientCertsBackend> {
    cryptoki_object: Cryptoki<B>,
    active: bool,
}

impl<B: ClientCertsBackend> Object<B> {
    fn new(cryptoki_object: Cryptoki<B>) -> Object<B> {
        Object {
            cryptoki_object,
            active: true,
        }
    }

    fn matches(&self, attrs: &[(CK_ATTRIBUTE_TYPE, Vec<u8>)]) -> bool {
        if !self.active {
            return false;
        }
        self.cryptoki_object.matches(attrs)
    }

    fn get_attribute(&self, attribute: CK_ATTRIBUTE_TYPE) -> Option<&[u8]> {
        self.cryptoki_object.get_attribute(attribute)
    }

    fn get_signature_length(
        &mut self,
        data: Vec<u8>,
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<usize, Error> {
        self.cryptoki_object.get_signature_length(data, params)
    }

    fn sign(
        &mut self,
        data: Vec<u8>,
        params: &Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<Vec<u8>, Error> {
        self.cryptoki_object.sign(data, params)
    }

    fn mark_active(&mut self, active: bool) {
        self.active = active;
    }
}

/// Helper struct to manage the state of a single slot, backed by a given `ClientCertsBackend`.
struct Slot<B: ClientCertsBackend> {
    /// A map of object handles to the underlying objects.
    objects: BTreeMap<CK_OBJECT_HANDLE, Object<B>>,
    /// A map of certificate identifiers to object handles.
    cert_ids: BTreeMap<Vec<u8>, CK_OBJECT_HANDLE>,
    /// A map of key identifiers to object handles. For each id in this map, there should be a
    /// corresponding identical id in the `cert_ids` map.
    key_ids: BTreeMap<Vec<u8>, CK_OBJECT_HANDLE>,
    /// A map of trust identifiers to object handles. For each id in this map, there should be a
    /// corresponding identical id in the `cert_ids` map.
    trust_ids: BTreeMap<Vec<u8>, CK_OBJECT_HANDLE>,
    /// The next object handle to hand out.
    next_handle: CK_OBJECT_HANDLE,
    /// The backend that provides objects, signing, etc.
    backend: B,
}

impl<B: ClientCertsBackend> Slot<B> {
    fn new(backend: B) -> Slot<B> {
        Slot {
            objects: BTreeMap::new(),
            cert_ids: BTreeMap::new(),
            key_ids: BTreeMap::new(),
            trust_ids: BTreeMap::new(),
            next_handle: 1,
            backend,
        }
    }

    fn get_next_handle(&mut self) -> CK_OBJECT_HANDLE {
        let next_handle = self.next_handle;
        self.next_handle += 1;
        next_handle
    }

    // When a new search session is opened, potentially search for certificates and keys to expose.
    // De-duplicate previously-found certificates and keys by keeping track of their IDs.
    fn maybe_find_new_objects(&mut self, slot_id: CK_SLOT_ID) -> Result<(), Error> {
        let Some((certs, keys, trusts)) = self.backend.find_objects(slot_id)? else {
            // If no search was performed, return early and use the already-known objects.
            return Ok(());
        };
        // Otherwise, a search was performed. Mark all already-known objects as inactive, and then
        // go through the returned objects, marking any already-known objects as active, as well as
        // adding new objects.
        self.objects
            .values_mut()
            .for_each(|object| object.mark_active(false));
        for cert in certs {
            let cryptoki_object = Cryptoki::Cert(cert);
            if let Some(handle) = self.cert_ids.get(cryptoki_object.id()?) {
                let known_object = self
                    .objects
                    .get_mut(handle)
                    .ok_or(error_here!(ErrorType::LibraryFailure))?;
                known_object.mark_active(true);
                continue;
            }
            let handle = self.get_next_handle();
            self.cert_ids.insert(cryptoki_object.id()?.to_vec(), handle);
            self.objects.insert(handle, Object::new(cryptoki_object));
        }
        for key in keys {
            let cryptoki_object = Cryptoki::Key(key);
            if let Some(handle) = self.key_ids.get(cryptoki_object.id()?) {
                let known_object = self
                    .objects
                    .get_mut(handle)
                    .ok_or(error_here!(ErrorType::LibraryFailure))?;
                known_object.mark_active(true);
                continue;
            }
            let handle = self.get_next_handle();
            self.key_ids.insert(cryptoki_object.id()?.to_vec(), handle);
            self.objects.insert(handle, Object::new(cryptoki_object));
        }
        for trust in trusts {
            let cryptoki_object = Cryptoki::Trust(trust);
            if let Some(handle) = self.trust_ids.get(cryptoki_object.id()?) {
                let known_object = self
                    .objects
                    .get_mut(handle)
                    .ok_or(error_here!(ErrorType::LibraryFailure))?;
                known_object.mark_active(true);
                continue;
            }
            let handle = self.get_next_handle();
            self.trust_ids
                .insert(cryptoki_object.id()?.to_vec(), handle);
            self.objects.insert(handle, Object::new(cryptoki_object));
        }
        Ok(())
    }
}

/// The `Manager` keeps track of the state of this module with respect to the PKCS #11
/// specification. This includes what sessions are open, which search and sign operations are
/// ongoing, and what objects are known and by what handle.
pub struct Manager<B: ClientCertsBackend> {
    /// A map of open session handle to slot ID. Sessions can be created (opened) on a particular
    /// slot and later closed.
    sessions: BTreeMap<CK_SESSION_HANDLE, CK_SLOT_ID>,
    /// A map of searches to PKCS #11 object handles that match those searches.
    searches: BTreeMap<CK_SESSION_HANDLE, Vec<CK_OBJECT_HANDLE>>,
    /// A map of sign operations to a pair of the object handle and optionally some params being
    /// used by each one.
    signs: BTreeMap<CK_SESSION_HANDLE, (CK_OBJECT_HANDLE, Option<CK_RSA_PKCS_PSS_PARAMS>)>,
    /// The next session handle to hand out.
    next_session: CK_SESSION_HANDLE,
    /// The list of slots managed by this Manager. The slot at index n has slot ID n + 1.
    slots: Vec<Slot<B>>,
}

impl<B: ClientCertsBackend> Manager<B> {
    pub fn new(slots: Vec<B>) -> Manager<B> {
        Manager {
            sessions: BTreeMap::new(),
            searches: BTreeMap::new(),
            signs: BTreeMap::new(),
            next_session: 1,
            slots: slots.into_iter().map(Slot::new).collect(),
        }
    }

    /// Get a list of slot IDs. If `token_present` is `true`, returns only the IDs of present
    /// slots. Otherwise, returns all slot IDs.
    pub fn get_slot_ids(&self, token_present: bool) -> Vec<CK_SLOT_ID> {
        let mut slot_ids = Vec::with_capacity(self.slots.len());
        for (index, slot) in self.slots.iter().enumerate() {
            if slot.backend.get_slot_info().flags & CKF_TOKEN_PRESENT == CKF_TOKEN_PRESENT
                || !token_present
            {
                slot_ids.push((index + 1).try_into().unwrap());
            }
        }
        slot_ids
    }

    pub fn get_slot_info(&self, slot_id: CK_SLOT_ID) -> Result<CK_SLOT_INFO, Error> {
        let slot = self.slot_id_to_slot(slot_id)?;
        Ok(slot.backend.get_slot_info())
    }

    pub fn get_token_info(&self, slot_id: CK_SLOT_ID) -> Result<CK_TOKEN_INFO, Error> {
        let slot = self.slot_id_to_slot(slot_id)?;
        Ok(slot.backend.get_token_info())
    }

    pub fn get_mechanism_list(&self, slot_id: CK_SLOT_ID) -> Result<Vec<CK_MECHANISM_TYPE>, Error> {
        let slot = self.slot_id_to_slot(slot_id)?;
        Ok(slot.backend.get_mechanism_list())
    }

    pub fn open_session(&mut self, slot_id: CK_SLOT_ID) -> Result<CK_SESSION_HANDLE, Error> {
        let next_session = self.next_session;
        self.next_session += 1;
        self.sessions.insert(next_session, slot_id);
        Ok(next_session)
    }

    pub fn close_session(&mut self, session: CK_SESSION_HANDLE) -> Result<(), Error> {
        self.sessions
            .remove(&session)
            .map(|_| ())
            .ok_or(error_here!(ErrorType::InvalidInput))
    }

    pub fn close_all_sessions(&mut self, slot_id: CK_SLOT_ID) -> Result<(), Error> {
        self.sessions
            .retain(|_, existing_slot_id| *existing_slot_id != slot_id);
        Ok(())
    }

    pub fn change_password(
        &mut self,
        session: CK_SESSION_HANDLE,
        from: &[u8],
        to: &[u8],
    ) -> Result<(), Error> {
        let Some(slot_id) = self.sessions.get(&session) else {
            return Err(error_here!(ErrorType::InvalidArgument));
        };
        let slot = self.slot_id_to_slot_mut(*slot_id)?;
        slot.backend.change_password(from, to)
    }

    pub fn login(&mut self, session: CK_SESSION_HANDLE, password: &[u8]) -> Result<(), Error> {
        let Some(slot_id) = self.sessions.get(&session) else {
            return Err(error_here!(ErrorType::InvalidArgument));
        };
        let slot = self.slot_id_to_slot_mut(*slot_id)?;
        slot.backend.login(password)
    }

    pub fn logout(&mut self, session: CK_SESSION_HANDLE) -> Result<(), Error> {
        let Some(slot_id) = self.sessions.get(&session) else {
            return Err(error_here!(ErrorType::InvalidArgument));
        };
        let slot = self.slot_id_to_slot_mut(*slot_id)?;
        slot.backend.logout();
        Ok(())
    }

    pub fn get_session_info(&self, session: CK_SESSION_HANDLE) -> Result<CK_SESSION_INFO, Error> {
        let Some(slot_id) = self.sessions.get(&session) else {
            return Err(error_here!(ErrorType::InvalidArgument));
        };
        let slot = self.slot_id_to_slot(*slot_id)?;
        Ok(CK_SESSION_INFO {
            slotID: *slot_id,
            state: if slot.backend.is_logged_in() {
                CKS_RO_USER_FUNCTIONS
            } else {
                CKS_RO_PUBLIC_SESSION
            },
            flags: CKF_SERIAL_SESSION,
            ulDeviceError: 0,
        })
    }

    fn slot_id_to_slot(&self, slot_id: CK_SLOT_ID) -> Result<&Slot<B>, Error> {
        let slot_id: usize = slot_id
            .try_into()
            .map_err(|_| error_here!(ErrorType::InvalidInput))?;
        if slot_id < 1 {
            return Err(error_here!(ErrorType::InvalidInput));
        }
        self.slots
            .get(slot_id - 1)
            .ok_or(error_here!(ErrorType::InvalidInput))
    }

    fn slot_id_to_slot_mut(&mut self, slot_id: CK_SLOT_ID) -> Result<&mut Slot<B>, Error> {
        let slot_id: usize = slot_id
            .try_into()
            .map_err(|_| error_here!(ErrorType::InvalidInput))?;
        if slot_id < 1 {
            return Err(error_here!(ErrorType::InvalidInput));
        }
        self.slots
            .get_mut(slot_id - 1)
            .ok_or(error_here!(ErrorType::InvalidInput))
    }

    /// PKCS #11 specifies that search operations happen in three phases: setup, get any matches
    /// (this part may be repeated if the caller uses a small buffer), and end. This implementation
    /// does all of the work up front and gathers all matching objects during setup and retains them
    /// until they are retrieved and consumed via `search`.
    pub fn start_search(
        &mut self,
        session: CK_SESSION_HANDLE,
        attrs: Vec<(CK_ATTRIBUTE_TYPE, Vec<u8>)>,
    ) -> Result<(), Error> {
        let Some(slot_id) = self.sessions.get(&session) else {
            return Err(error_here!(ErrorType::InvalidArgument));
        };
        let slot_id = *slot_id;
        let slot = self.slot_id_to_slot_mut(slot_id)?;
        // If the search is for an attribute we don't support, no objects will match. This check
        // saves us having to look through all of our objects.
        for (attr, _) in &attrs {
            if !SUPPORTED_ATTRIBUTES.contains(attr) {
                self.searches.insert(session, Vec::new());
                return Ok(());
            }
        }
        slot.maybe_find_new_objects(slot_id)?;
        let mut handles = Vec::new();
        for (handle, object) in &slot.objects {
            if object.matches(&attrs) {
                handles.push(*handle);
            }
        }
        self.searches.insert(session, handles);
        Ok(())
    }

    /// Given a session and a maximum number of object handles to return, attempts to retrieve up to
    /// that many objects from the corresponding search. Updates the search so those objects are not
    /// returned repeatedly. `max_objects` must be non-zero.
    pub fn search(
        &mut self,
        session: CK_SESSION_HANDLE,
        max_objects: usize,
    ) -> Result<Vec<CK_OBJECT_HANDLE>, Error> {
        if max_objects == 0 {
            return Err(error_here!(ErrorType::InvalidArgument));
        }
        match self.searches.get_mut(&session) {
            Some(search) => {
                let split_at = if max_objects >= search.len() {
                    0
                } else {
                    search.len() - max_objects
                };
                let to_return = search.split_off(split_at);
                if to_return.len() > max_objects {
                    return Err(error_here!(ErrorType::LibraryFailure));
                }
                Ok(to_return)
            }
            None => Err(error_here!(ErrorType::InvalidArgument)),
        }
    }

    pub fn clear_search(&mut self, session: CK_SESSION_HANDLE) -> Result<(), Error> {
        self.searches.remove(&session);
        Ok(())
    }

    pub fn get_attributes(
        &self,
        session: CK_SESSION_HANDLE,
        object_handle: CK_OBJECT_HANDLE,
        attr_types: Vec<CK_ATTRIBUTE_TYPE>,
    ) -> Result<Vec<Option<Vec<u8>>>, Error> {
        let Some(slot_id) = self.sessions.get(&session) else {
            return Err(error_here!(ErrorType::InvalidArgument));
        };
        let slot = self.slot_id_to_slot(*slot_id)?;
        let object = match slot.objects.get(&object_handle) {
            Some(object) => object,
            None => return Err(error_here!(ErrorType::InvalidArgument)),
        };
        let mut results = Vec::with_capacity(attr_types.len());
        for attr_type in attr_types {
            let result = object
                .get_attribute(attr_type)
                .map(|value| value.to_owned());
            results.push(result);
        }
        Ok(results)
    }

    /// The way NSS uses PKCS #11 to sign data happens in two phases: setup and sign. This
    /// implementation makes a note of which key is to be used (if it exists) during setup. When the
    /// caller finishes with the sign operation, this implementation retrieves the key handle and
    /// performs the signature.
    pub fn start_sign(
        &mut self,
        session: CK_SESSION_HANDLE,
        key_handle: CK_OBJECT_HANDLE,
        params: Option<CK_RSA_PKCS_PSS_PARAMS>,
    ) -> Result<(), Error> {
        if self.signs.contains_key(&session) {
            return Err(error_here!(ErrorType::InvalidArgument));
        }
        self.signs.insert(session, (key_handle, params));
        Ok(())
    }

    pub fn get_signature_length(
        &mut self,
        session: CK_SESSION_HANDLE,
        data: Vec<u8>,
    ) -> Result<usize, Error> {
        // Take ownership of the key handle and params of the sign (this has the added benefit of
        // removing this data in case of an error).
        let (key_handle, params) = match self.signs.remove(&session) {
            Some((key_handle, params)) => (key_handle, params),
            None => return Err(error_here!(ErrorType::InvalidArgument)),
        };
        let Some(slot_id) = self.sessions.get(&session) else {
            return Err(error_here!(ErrorType::InvalidArgument));
        };
        let slot = self.slot_id_to_slot_mut(*slot_id)?;
        let key = match slot.objects.get_mut(&key_handle) {
            Some(key) => key,
            None => return Err(error_here!(ErrorType::InvalidArgument)),
        };
        let signature_length = key.get_signature_length(data, &params)?;
        // Re-add the key handle and params if getting the signature length succeeded.
        self.signs.insert(session, (key_handle, params));
        Ok(signature_length)
    }

    pub fn sign(&mut self, session: CK_SESSION_HANDLE, data: Vec<u8>) -> Result<Vec<u8>, Error> {
        // Performing the signature (via C_Sign, which is the only way we support) finishes the sign
        // operation, so it needs to be removed here.
        let (key_handle, params) = match self.signs.remove(&session) {
            Some((key_handle, params)) => (key_handle, params),
            None => return Err(error_here!(ErrorType::InvalidArgument)),
        };
        let Some(slot_id) = self.sessions.get(&session) else {
            return Err(error_here!(ErrorType::InvalidArgument));
        };
        let slot = self.slot_id_to_slot_mut(*slot_id)?;
        let key = match slot.objects.get_mut(&key_handle) {
            Some(key) => key,
            None => return Err(error_here!(ErrorType::InvalidArgument)),
        };
        key.sign(data, &params)
    }
}
