/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use fluent::FluentValue;
use fluent_fallback::{
    types::{
        L10nAttribute as FluentL10nAttribute, L10nKey as FluentL10nKey,
        L10nMessage as FluentL10nMessage, ResourceId,
    },
    Localization,
};
use fluent_ffi::{convert_args, FluentArgs, FluentArgument, L10nArg};
use l10nregistry_ffi::{
    coverage::l10nregistry_clear_coverage_cache,
    env::GeckoEnvironment,
    registry::{get_l10n_registry, GeckoL10nRegistry, GeckoResourceId},
};
use nsstring::{nsACString, nsCString};
use std::os::raw::c_void;
use std::{borrow::Cow, cell::RefCell};
use thin_vec::ThinVec;
use unic_langid::LanguageIdentifier;
use xpcom::{
    interfaces::{nsIRunnablePriority},
    RefCounted, RefPtr, Refcnt,
};

#[derive(Debug)]
#[repr(C)]
pub struct L10nKey<'s> {
    id: &'s nsACString,
    args: ThinVec<L10nArg<'s>>,
}

impl<'s> From<&'s L10nKey<'s>> for FluentL10nKey<'static> {
    fn from(input: &'s L10nKey<'s>) -> Self {
        FluentL10nKey {
            id: input.id.to_utf8().to_string().into(),
            args: convert_args_to_owned(&input.args),
        }
    }
}

// This is a variant of `convert_args` from `fluent-ffi` with a 'static constrain
// put on the resulting `FluentArgs` to make it acceptable into `spqwn_current_thread`.
pub fn convert_args_to_owned(args: &[L10nArg]) -> Option<FluentArgs<'static>> {
    if args.is_empty() {
        return None;
    }

    let mut result = FluentArgs::with_capacity(args.len());
    for arg in args {
        let val = match arg.value {
            FluentArgument::Double_(d) => FluentValue::from(d),
            // We need this to be owned because we pass the result into `spawn_local`.
            FluentArgument::String(s) => FluentValue::from(Cow::Owned(s.to_utf8().to_string())),
        };
        result.set(arg.id.to_string(), val);
    }
    Some(result)
}

#[derive(Debug)]
#[repr(C)]
pub struct L10nAttribute {
    name: nsCString,
    value: nsCString,
}

impl From<FluentL10nAttribute<'_>> for L10nAttribute {
    fn from(attr: FluentL10nAttribute<'_>) -> Self {
        Self {
            name: nsCString::from(&*attr.name),
            value: nsCString::from(&*attr.value),
        }
    }
}

#[derive(Debug)]
#[repr(C)]
pub struct L10nMessage {
    value: nsCString,
    attributes: ThinVec<L10nAttribute>,
}

impl std::default::Default for L10nMessage {
    fn default() -> Self {
        Self {
            value: nsCString::new(),
            attributes: ThinVec::new(),
        }
    }
}

#[derive(Debug)]
#[repr(C)]
pub struct OptionalL10nMessage {
    is_present: bool,
    message: L10nMessage,
}

impl From<FluentL10nMessage<'_>> for L10nMessage {
    fn from(input: FluentL10nMessage) -> Self {
        let value = if let Some(value) = input.value {
            value.to_string().into()
        } else {
            let mut s = nsCString::new();
            s.set_is_void(true);
            s
        };
        Self {
            value,
            attributes: input.attributes.into_iter().map(Into::into).collect(),
        }
    }
}

pub struct LocalizationRc {
    inner: RefCell<Localization<GeckoL10nRegistry, GeckoEnvironment>>,
    refcnt: Refcnt,
}

// xpcom::RefPtr support
unsafe impl RefCounted for LocalizationRc {
    unsafe fn addref(&self) {
        localization_addref(self);
    }
    unsafe fn release(&self) {
        localization_release(self);
    }
}

impl LocalizationRc {
    pub fn new(
        res_ids: Vec<ResourceId>,
        is_sync: bool,
        registry: Option<&GeckoL10nRegistry>,
        locales: Option<Vec<LanguageIdentifier>>,
    ) -> RefPtr<Self> {
        let env = GeckoEnvironment::new(locales);
        let inner = if let Some(reg) = registry {
            Localization::with_env(res_ids, is_sync, env, reg.clone())
        } else {
            let reg = (*get_l10n_registry()).clone();
            Localization::with_env(res_ids, is_sync, env, reg)
        };

        let loc = Box::new(LocalizationRc {
            inner: RefCell::new(inner),
            refcnt: unsafe { Refcnt::new() },
        });

        unsafe {
            RefPtr::from_raw(Box::into_raw(loc))
                .expect("Failed to create RefPtr<LocalizationRc> from Box<LocalizationRc>")
        }
    }

    pub fn add_resource_id(&self, res_id: ResourceId) {
        self.inner.borrow_mut().add_resource_id(res_id);
    }

    pub fn add_resource_ids(&self, res_ids: Vec<ResourceId>) {
        self.inner.borrow_mut().add_resource_ids(res_ids);
    }

    pub fn remove_resource_id(&self, res_id: ResourceId) -> usize {
        self.inner.borrow_mut().remove_resource_id(res_id)
    }

    pub fn remove_resource_ids(&self, res_ids: Vec<ResourceId>) -> usize {
        self.inner.borrow_mut().remove_resource_ids(res_ids)
    }

    pub fn set_async(&self) {
        if self.is_sync() {
            self.inner.borrow_mut().set_async();
        }
    }

    pub fn is_sync(&self) -> bool {
        self.inner.borrow().is_sync()
    }

    pub fn on_change(&self) {
        self.inner.borrow_mut().on_change();
    }

    pub fn format_value_sync(
        &self,
        id: &nsACString,
        args: &ThinVec<L10nArg>,
        ret_val: &mut nsACString,
        ret_err: &mut ThinVec<nsCString>,
    ) -> bool {
        let mut errors = vec![];
        let args = convert_args(&args);
        if let Ok(value) = self.inner.borrow().bundles().format_value_sync(
            &id.to_utf8(),
            args.as_ref(),
            &mut errors,
        ) {
            if let Some(value) = value {
                ret_val.assign(&value);
            } else {
                ret_val.set_is_void(true);
            }
            #[cfg(debug_assertions)]
            debug_assert_variables_exist(&errors, &[id], |id| id.to_string());
            ret_err.extend(errors.into_iter().map(|err| err.to_string().into()));
            true
        } else {
            false
        }
    }

    pub fn format_values_sync(
        &self,
        keys: &ThinVec<L10nKey>,
        ret_val: &mut ThinVec<nsCString>,
        ret_err: &mut ThinVec<nsCString>,
    ) -> bool {
        ret_val.reserve(keys.len());
        let keys: Vec<FluentL10nKey> = keys.into_iter().map(|k| k.into()).collect();
        let mut errors = vec![];
        if let Ok(values) = self
            .inner
            .borrow()
            .bundles()
            .format_values_sync(&keys, &mut errors)
        {
            for value in values.iter() {
                if let Some(value) = value {
                    ret_val.push(value.as_ref().into());
                } else {
                    let mut void_string = nsCString::new();
                    void_string.set_is_void(true);
                    ret_val.push(void_string);
                }
            }
            #[cfg(debug_assertions)]
            debug_assert_variables_exist(&errors, &keys, |key| key.id.to_string());
            ret_err.extend(errors.into_iter().map(|err| err.to_string().into()));
            true
        } else {
            false
        }
    }

    pub fn format_messages_sync(
        &self,
        keys: &ThinVec<L10nKey>,
        ret_val: &mut ThinVec<OptionalL10nMessage>,
        ret_err: &mut ThinVec<nsCString>,
    ) -> bool {
        ret_val.reserve(keys.len());
        let mut errors = vec![];
        let keys: Vec<FluentL10nKey> = keys.into_iter().map(|k| k.into()).collect();
        if let Ok(messages) = self
            .inner
            .borrow()
            .bundles()
            .format_messages_sync(&keys, &mut errors)
        {
            for msg in messages {
                ret_val.push(if let Some(msg) = msg {
                    OptionalL10nMessage {
                        is_present: true,
                        message: msg.into(),
                    }
                } else {
                    OptionalL10nMessage {
                        is_present: false,
                        message: L10nMessage::default(),
                    }
                });
            }
            assert_eq!(keys.len(), ret_val.len());
            #[cfg(debug_assertions)]
            debug_assert_variables_exist(&errors, &keys, |key| key.id.to_string());
            ret_err.extend(errors.into_iter().map(|err| err.to_string().into()));
            true
        } else {
            false
        }
    }

    pub fn format_value(
        &self,
        id: &nsACString,
        args: &ThinVec<L10nArg>,
        promise: &xpcom::Promise,
        callback: extern "C" fn(&xpcom::Promise, &nsACString, &ThinVec<nsCString>),
    ) {
        let bundles = self.inner.borrow().bundles().clone();

        let args = convert_args_to_owned(&args);

        let id = nsCString::from(id);
        let strong_promise = RefPtr::new(promise);

        moz_task::TaskBuilder::new("LocalizationRc::format_value", async move {
            let mut errors = vec![];
            let value = if let Some(value) = bundles
                .format_value(&id.to_utf8(), args.as_ref(), &mut errors)
                .await
            {
                let v: nsCString = value.to_string().into();
                v
            } else {
                let mut v = nsCString::new();
                v.set_is_void(true);
                v
            };
            #[cfg(debug_assertions)]
            debug_assert_variables_exist(&errors, &[id], |id| id.to_string());
            let errors = errors
                .into_iter()
                .map(|err| err.to_string().into())
                .collect();
            callback(&strong_promise, &value, &errors);
        })
        .priority(nsIRunnablePriority::PRIORITY_RENDER_BLOCKING as u32)
        .spawn_local()
        .detach();
    }

    pub fn format_values(
        &self,
        keys: &ThinVec<L10nKey>,
        promise: &xpcom::Promise,
        callback: extern "C" fn(&xpcom::Promise, &ThinVec<nsCString>, &ThinVec<nsCString>),
    ) {
        let bundles = self.inner.borrow().bundles().clone();

        let keys: Vec<FluentL10nKey> = keys.into_iter().map(|k| k.into()).collect();

        let strong_promise = RefPtr::new(promise);

        moz_task::TaskBuilder::new("LocalizationRc::format_values", async move {
            let mut errors = vec![];
            let ret_val = bundles
                .format_values(&keys, &mut errors)
                .await
                .into_iter()
                .map(|value| {
                    if let Some(value) = value {
                        nsCString::from(value.as_ref())
                    } else {
                        let mut v = nsCString::new();
                        v.set_is_void(true);
                        v
                    }
                })
                .collect::<ThinVec<_>>();

            assert_eq!(keys.len(), ret_val.len());

            #[cfg(debug_assertions)]
            debug_assert_variables_exist(&errors, &keys, |key| key.id.to_string());
            let errors = errors
                .into_iter()
                .map(|err| err.to_string().into())
                .collect();

            callback(&strong_promise, &ret_val, &errors);
        })
        .priority(nsIRunnablePriority::PRIORITY_RENDER_BLOCKING as u32)
        .spawn_local()
        .detach();
    }

    pub fn format_messages(
        &self,
        keys: &ThinVec<L10nKey>,
        promise: &xpcom::Promise,
        callback: extern "C" fn(
            &xpcom::Promise,
            &ThinVec<OptionalL10nMessage>,
            &ThinVec<nsCString>,
        ),
    ) {
        let bundles = self.inner.borrow().bundles().clone();

        let keys: Vec<FluentL10nKey> = keys.into_iter().map(|k| k.into()).collect();

        let strong_promise = RefPtr::new(promise);

        moz_task::TaskBuilder::new("LocalizationRc::format_messages", async move {
            let mut errors = vec![];
            let ret_val = bundles
                .format_messages(&keys, &mut errors)
                .await
                .into_iter()
                .map(|msg| {
                    if let Some(msg) = msg {
                        OptionalL10nMessage {
                            is_present: true,
                            message: msg.into(),
                        }
                    } else {
                        OptionalL10nMessage {
                            is_present: false,
                            message: L10nMessage::default(),
                        }
                    }
                })
                .collect::<ThinVec<_>>();

            assert_eq!(keys.len(), ret_val.len());

            #[cfg(debug_assertions)]
            debug_assert_variables_exist(&errors, &keys, |key| key.id.to_string());

            let errors = errors
                .into_iter()
                .map(|err| err.to_string().into())
                .collect();

            callback(&strong_promise, &ret_val, &errors);
        })
        .priority(nsIRunnablePriority::PRIORITY_RENDER_BLOCKING as u32)
        .spawn_local()
        .detach();
    }
}

#[no_mangle]
pub extern "C" fn localization_parse_locale(input: &nsCString) -> *const c_void {
    let l: LanguageIdentifier = input.to_utf8().parse().unwrap();
    Box::into_raw(Box::new(l)) as *const c_void
}

#[no_mangle]
pub extern "C" fn localization_new(
    res_ids: &ThinVec<GeckoResourceId>,
    is_sync: bool,
    reg: Option<&GeckoL10nRegistry>,
    result: &mut *const LocalizationRc,
) {
    *result = std::ptr::null();
    let res_ids: Vec<ResourceId> = res_ids.iter().map(ResourceId::from).collect();
    *result = RefPtr::forget_into_raw(LocalizationRc::new(res_ids, is_sync, reg, None));
}

#[no_mangle]
pub extern "C" fn localization_new_with_locales(
    res_ids: &ThinVec<GeckoResourceId>,
    is_sync: bool,
    reg: Option<&GeckoL10nRegistry>,
    locales: Option<&ThinVec<nsCString>>,
    result: &mut *const LocalizationRc,
) -> bool {
    *result = std::ptr::null();
    let res_ids: Vec<ResourceId> = res_ids.iter().map(ResourceId::from).collect();
    let locales: Result<Option<Vec<LanguageIdentifier>>, _> = locales
        .map(|locales| {
            locales
                .iter()
                .map(|s| LanguageIdentifier::from_bytes(&s))
                .collect()
        })
        .transpose();

    if let Ok(locales) = locales {
        *result = RefPtr::forget_into_raw(LocalizationRc::new(res_ids, is_sync, reg, locales));
        true
    } else {
        false
    }
}

#[no_mangle]
pub unsafe extern "C" fn localization_addref(loc: &LocalizationRc) {
    loc.refcnt.inc();
}

#[no_mangle]
pub unsafe extern "C" fn localization_release(loc: *const LocalizationRc) {
    let rc = (*loc).refcnt.dec();
    if rc == 0 {
        std::mem::drop(Box::from_raw(loc as *const _ as *mut LocalizationRc));
    }
}

#[no_mangle]
pub extern "C" fn localization_add_res_id(loc: &LocalizationRc, res_id: &GeckoResourceId) {
    loc.add_resource_id(res_id.into());
}

#[no_mangle]
pub extern "C" fn localization_add_res_ids(loc: &LocalizationRc, res_ids: &ThinVec<GeckoResourceId>) {
    let res_ids = res_ids.iter().map(ResourceId::from).collect();
    loc.add_resource_ids(res_ids);
}

#[no_mangle]
pub extern "C" fn localization_remove_res_id(loc: &LocalizationRc, res_id: &GeckoResourceId) -> usize {
    loc.remove_resource_id(res_id.into())
}

#[no_mangle]
pub extern "C" fn localization_remove_res_ids(
    loc: &LocalizationRc,
    res_ids: &ThinVec<GeckoResourceId>,
) -> usize {
    let res_ids = res_ids.iter().map(ResourceId::from).collect();
    loc.remove_resource_ids(res_ids)
}

#[no_mangle]
pub extern "C" fn localization_format_value_sync(
    loc: &LocalizationRc,
    id: &nsACString,
    args: &ThinVec<L10nArg>,
    ret_val: &mut nsACString,
    ret_err: &mut ThinVec<nsCString>,
) -> bool {
    loc.format_value_sync(id, args, ret_val, ret_err)
}

#[no_mangle]
pub extern "C" fn localization_format_values_sync(
    loc: &LocalizationRc,
    keys: &ThinVec<L10nKey>,
    ret_val: &mut ThinVec<nsCString>,
    ret_err: &mut ThinVec<nsCString>,
) -> bool {
    loc.format_values_sync(keys, ret_val, ret_err)
}

#[no_mangle]
pub extern "C" fn localization_format_messages_sync(
    loc: &LocalizationRc,
    keys: &ThinVec<L10nKey>,
    ret_val: &mut ThinVec<OptionalL10nMessage>,
    ret_err: &mut ThinVec<nsCString>,
) -> bool {
    loc.format_messages_sync(keys, ret_val, ret_err)
}

#[no_mangle]
pub extern "C" fn localization_format_value(
    loc: &LocalizationRc,
    id: &nsACString,
    args: &ThinVec<L10nArg>,
    promise: &xpcom::Promise,
    callback: extern "C" fn(&xpcom::Promise, &nsACString, &ThinVec<nsCString>),
) {
    loc.format_value(id, args, promise, callback);
}

#[no_mangle]
pub extern "C" fn localization_format_values(
    loc: &LocalizationRc,
    keys: &ThinVec<L10nKey>,
    promise: &xpcom::Promise,
    callback: extern "C" fn(&xpcom::Promise, &ThinVec<nsCString>, &ThinVec<nsCString>),
) {
    loc.format_values(keys, promise, callback);
}

#[no_mangle]
pub extern "C" fn localization_format_messages(
    loc: &LocalizationRc,
    keys: &ThinVec<L10nKey>,
    promise: &xpcom::Promise,
    callback: extern "C" fn(&xpcom::Promise, &ThinVec<OptionalL10nMessage>, &ThinVec<nsCString>),
) {
    loc.format_messages(keys, promise, callback);
}

#[no_mangle]
pub extern "C" fn localization_set_async(loc: &LocalizationRc) {
    loc.set_async();
}

#[no_mangle]
pub extern "C" fn localization_is_sync(loc: &LocalizationRc) -> bool {
    loc.is_sync()
}

#[no_mangle]
pub extern "C" fn localization_on_change(loc: &LocalizationRc) {
    l10nregistry_clear_coverage_cache();
    loc.on_change();
}

#[cfg(debug_assertions)]
fn debug_assert_variables_exist<K, F>(
    errors: &[fluent_fallback::LocalizationError],
    keys: &[K],
    to_string: F,
) where
    F: Fn(&K) -> String,
{
    for error in errors {
        if let fluent_fallback::LocalizationError::Resolver { errors, .. } = error {
            use fluent::{
                resolver::{errors::ReferenceKind, ResolverError},
                FluentError,
            };
            for error in errors {
                if let FluentError::ResolverError(ResolverError::Reference(
                    ReferenceKind::Variable { id },
                )) = error
                {
                    // This error needs to be actionable for Firefox engineers to fix
                    // their Fluent issues. It might be nicer to share the specific
                    // message, but at this point we don't have that information.
                    eprintln!(
                        "Fluent error, the argument \"${}\" was not provided a value.",
                        id
                    );
                    eprintln!("This error happened while formatting the following messages:");
                    for key in keys {
                        eprintln!("  {:?}", to_string(key))
                    }

                    // Panic with the slightly more cryptic ResolverError.
                    panic!("{}", error.to_string());
                }
            }
        }
    }
}
