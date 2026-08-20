/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::cell::{Cell, RefCell, RefMut};
use std::ffi::c_void;

use dbus::{BusType, Connection, Message, MessageItem};
use log::warn;

use nserror::{
    nsresult, NS_ERROR_FAILURE, NS_ERROR_INVALID_ARG, NS_ERROR_NOT_AVAILABLE,
    NS_ERROR_NOT_IMPLEMENTED, NS_OK,
};
use nsstring::{nsAString, nsString};
use thin_vec::ThinVec;
use xpcom::interfaces::{nsIAlertCallbacks, nsIAlertNotification, nsIObserver};
use xpcom::{xpcom, xpcom_method, RefPtr};

const PORTAL_DESTINATION: &str = "org.freedesktop.portal.Desktop";
const PORTAL_PATH: &str = "/org/freedesktop/portal/desktop";
const PORTAL_INTERFACE: &str = "org.freedesktop.portal.Notification";
const CALL_TIMEOUT_MS: i32 = 3000;

#[xpcom(implement(nsIAlertsService, nsIAlertsDoNotDisturb), atomic)]
struct PortalAlertsService {
    // Connected lazily so creating the service does not block on the
    // session-bus handshake.
    connection: RefCell<Option<Connection>>,
    suppress_for_screen_sharing: Cell<bool>,
}

impl PortalAlertsService {
    fn create() -> RefPtr<Self> {
        PortalAlertsService::allocate(InitPortalAlertsService {
            connection: RefCell::new(None),
            suppress_for_screen_sharing: Cell::new(false),
        })
    }

    xpcom_method!(show_alert => ShowAlert(aAlert: *const nsIAlertNotification, aAlertListener: *const nsIObserver));
    fn show_alert(
        &self,
        _alert: &nsIAlertNotification,
        _listener: Option<&nsIObserver>,
    ) -> Result<(), nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }

    xpcom_method!(show_alert_with_callbacks => ShowAlertWithCallbacks(aAlert: *const nsIAlertNotification, aAlertCallbacks: *const nsIAlertCallbacks));
    fn show_alert_with_callbacks(
        &self,
        alert: &nsIAlertNotification,
        callbacks: Option<&nsIAlertCallbacks>,
    ) -> Result<(), nsresult> {
        if self.suppress_for_screen_sharing.get() {
            if let Some(callbacks) = callbacks {
                unsafe { callbacks.OnAlertFinished() };
            }
            return Ok(());
        }
        self.add_notification(alert).map_err(|error| {
            warn!("XDG Desktop Portal notification failed: {error}");
            NS_ERROR_FAILURE
        })?;
        if let Some(callbacks) = callbacks {
            // Alert interaction is not implemented yet, so the callback state
            // is released right after showing.
            unsafe {
                callbacks.OnAlertShow();
                callbacks.OnAlertFinished();
            }
        }
        Ok(())
    }

    // TODO: Use the proxy support in newer dbus-rs instead once the vendored
    // crate is upgraded (bug 2061689).
    fn session_connection(&self) -> Result<RefMut<'_, Connection>, String> {
        let mut guard = self.connection.borrow_mut();
        // Drop a dead connection so a session-bus restart does not disable
        // the backend for the rest of the session.
        if guard
            .as_ref()
            .is_some_and(|connection| !connection.is_connected())
        {
            *guard = None;
        }
        if guard.is_none() {
            let connection = Connection::get_private(BusType::Session)
                .map_err(|error| format!("could not connect to the session bus: {error:?}"))?;
            *guard = Some(connection);
        }
        Ok(RefMut::map(guard, |connection| {
            connection.as_mut().unwrap()
        }))
    }

    fn add_notification(&self, alert: &nsIAlertNotification) -> Result<(), String> {
        // The id is unique per profile, unlike the name for chrome callers.
        let mut portal_id = nsString::new();
        unsafe { alert.GetId(&mut *portal_id) }
            .to_result()
            .map_err(|error| format!("could not read the alert id: {error}"))?;
        let mut title = nsString::new();
        unsafe { alert.GetTitle(&mut *title) }
            .to_result()
            .map_err(|error| format!("could not read the alert title: {error}"))?;
        let mut body = nsString::new();
        unsafe { alert.GetText(&mut *body) }
            .to_result()
            .map_err(|error| format!("could not read the alert text: {error}"))?;

        // All XPCOM calls must stay above this borrow: a JS-implemented alert
        // runs script in its getters, which could reenter this service and
        // panic the RefCell.
        let connection = self.session_connection()?;

        let portal_id = dbus_string(&portal_id);
        let entries = vec![
            ("title".to_string(), dbus_string(&title).into()),
            ("body".to_string(), dbus_string(&body).into()),
        ];
        let mut message = Message::new_method_call(
            PORTAL_DESTINATION,
            PORTAL_PATH,
            PORTAL_INTERFACE,
            "AddNotification",
        )?;
        message.append_items(&[
            portal_id.into(),
            MessageItem::from_dict(entries.into_iter().map(Ok::<_, ()>)).unwrap(),
        ]);
        connection
            .send_with_reply_and_block(message, CALL_TIMEOUT_MS)
            .map_err(|error| format!("{error:?}"))?;
        Ok(())
    }

    xpcom_method!(close_alert => CloseAlert(aName: *const nsAString, aContextClosed: bool));
    fn close_alert(&self, _name: &nsAString, _context_closed: bool) -> Result<(), nsresult> {
        Ok(())
    }

    xpcom_method!(get_history => GetHistory() -> ThinVec<nsString>);
    fn get_history(&self) -> Result<ThinVec<nsString>, nsresult> {
        // Neither org.freedesktop.Notifications nor
        // org.freedesktop.portal.Notification supports getting the previous
        // notifications.
        Err(NS_ERROR_NOT_AVAILABLE)
    }

    xpcom_method!(teardown => Teardown());
    fn teardown(&self) -> Result<(), nsresult> {
        Ok(())
    }

    xpcom_method!(pbm_teardown => PbmTeardown());
    fn pbm_teardown(&self) -> Result<(), nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }

    xpcom_method!(is_fullscreen => IsFullscreen() -> bool);
    fn is_fullscreen(&self) -> Result<bool, nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }

    xpcom_method!(get_manual_do_not_disturb => GetManualDoNotDisturb() -> bool);
    fn get_manual_do_not_disturb(&self) -> Result<bool, nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }

    xpcom_method!(set_manual_do_not_disturb => SetManualDoNotDisturb(aDoNotDisturb: bool));
    fn set_manual_do_not_disturb(&self, _do_not_disturb: bool) -> Result<(), nsresult> {
        Err(NS_ERROR_NOT_IMPLEMENTED)
    }

    xpcom_method!(get_suppress_for_screen_sharing => GetSuppressForScreenSharing() -> bool);
    fn get_suppress_for_screen_sharing(&self) -> Result<bool, nsresult> {
        Ok(self.suppress_for_screen_sharing.get())
    }

    xpcom_method!(set_suppress_for_screen_sharing => SetSuppressForScreenSharing(aSuppress: bool));
    fn set_suppress_for_screen_sharing(&self, suppress: bool) -> Result<(), nsresult> {
        self.suppress_for_screen_sharing.set(suppress);
        Ok(())
    }
}

// The dbus crate builds CStrings, which abort on interior nuls, so truncate
// there like the C-string based backends effectively do.
fn dbus_string(value: &nsAString) -> String {
    let mut value = value.to_string();
    if let Some(position) = value.find('\0') {
        value.truncate(position);
    }
    value
}

#[unsafe(no_mangle)]
pub extern "C" fn new_portal_alerts_service(
    iid: *const xpcom::nsIID,
    result: *mut *mut c_void,
) -> nsresult {
    if iid.is_null() || result.is_null() {
        return NS_ERROR_INVALID_ARG;
    }
    let service = PortalAlertsService::create();
    unsafe { service.QueryInterface(iid, result) }
}
