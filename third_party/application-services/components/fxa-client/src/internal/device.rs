/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::{
    cell::Cell,
    collections::{HashMap, HashSet},
};

pub use super::http_client::{GetDeviceResponse as Device, PushSubscription};
use super::{
    commands::{self, IncomingDeviceCommand, PrivateCommandKeys, PublicCommandKeys},
    http_client::{
        DeviceUpdateRequest, DeviceUpdateRequestBuilder, PendingCommand, UpdateDeviceResponse,
    },
    scopes, telemetry, util, CachedResponse, FirefoxAccount,
};
use crate::{info, warn, DeviceCapability, Error, LocalDevice, Result};
use sync15::DeviceType;

// An devices response is considered fresh for `DEVICES_FRESHNESS_THRESHOLD` ms.
const DEVICES_FRESHNESS_THRESHOLD: u64 = 60_000; // 1 minute

thread_local! {
    /// The maximum size, in bytes, of a command payload. The FxA server may
    /// reject requests to invoke commands with payloads exceeding this size.
    ///
    /// Defaults to 16 KB; overridden in tests.
    pub static COMMAND_MAX_PAYLOAD_SIZE: Cell<usize> = const { Cell::new(16 * 1024) }
}

/// The reason we are fetching commands.
#[derive(Clone, Copy)]
pub enum CommandFetchReason {
    /// We are polling in-case we've missed some.
    Poll,
    /// We got a push notification with the index of the message.
    Push(u64),
}

impl FirefoxAccount {
    /// Fetches the list of devices from the current account including
    /// the current one.
    ///
    /// * `ignore_cache` - If set to true, bypass the in-memory cache
    ///   and fetch devices from the server.
    pub fn get_devices(&mut self, ignore_cache: bool) -> Result<Vec<Device>> {
        if let Some(d) = &self.devices_cache {
            if !ignore_cache && util::now() < d.cached_at + DEVICES_FRESHNESS_THRESHOLD {
                return Ok(d.response.clone());
            }
        }

        let refresh_token = self.get_refresh_token()?;
        let response = self
            .client
            .get_devices(self.state.config(), refresh_token)?;

        self.devices_cache = Some(CachedResponse {
            response: response.clone(),
            cached_at: util::now(),
            etag: "".into(),
        });

        Ok(response)
    }

    pub fn get_current_device(&mut self) -> Result<Option<Device>> {
        Ok(self
            .get_devices(false)?
            .into_iter()
            .find(|d| d.is_current_device))
    }

    /// Replaces the internal set of "tracked" device capabilities by re-registering
    /// new capabilities and returns a set of device commands to register with the
    /// server.
    fn register_capabilities(
        &mut self,
        capabilities: &[DeviceCapability],
    ) -> Result<HashMap<String, String>> {
        let mut commands = HashMap::new();
        for capability in capabilities.iter().collect::<HashSet<_>>() {
            match capability {
                DeviceCapability::SendTab => {
                    let send_tab_command_data =
                        self.generate_command_data(DeviceCapability::SendTab)?;
                    commands.insert(
                        commands::send_tab::COMMAND_NAME.to_owned(),
                        send_tab_command_data,
                    );
                }
                DeviceCapability::CloseTabs => {
                    let close_tabs_command_data =
                        self.generate_command_data(DeviceCapability::CloseTabs)?;
                    commands.insert(
                        commands::close_tabs::COMMAND_NAME.to_owned(),
                        close_tabs_command_data,
                    );
                }
            }
        }
        Ok(commands)
    }

    /// Initializes our own device, most of the time this will be called right after logging-in
    /// for the first time.
    ///
    /// **💾 This method alters the persisted account state.**
    pub fn initialize_device(
        &mut self,
        name: &str,
        device_type: DeviceType,
        capabilities: &[DeviceCapability],
    ) -> Result<LocalDevice> {
        self.with_network_retry("initialize_device", |s| {
            s._initialize_device(name, device_type, capabilities)
        })
    }

    fn _initialize_device(
        &mut self,
        name: &str,
        device_type: DeviceType,
        capabilities: &[DeviceCapability],
    ) -> Result<LocalDevice> {
        self.state
            .set_device_capabilities(capabilities.iter().cloned());
        let commands = self.register_capabilities(capabilities)?;
        let update = DeviceUpdateRequestBuilder::new()
            .display_name(name)
            .device_type(&device_type)
            .available_commands(&commands)
            .build();
        self.update_device(update)
    }

    /// Register a set of device capabilities against the current device.
    ///
    /// As the only capability is Send Tab now, its command is registered with the server.
    /// Don't forget to also call this if the Sync Keys change as they
    /// encrypt the Send Tab command data.
    ///
    /// **💾 This method alters the persisted account state.**
    pub fn ensure_capabilities(
        &mut self,
        capabilities: &[DeviceCapability],
    ) -> Result<LocalDevice> {
        self.with_network_retry("ensure_capabilities", |s| {
            s._ensure_capabilities(capabilities)
        })
    }

    fn _ensure_capabilities(&mut self, capabilities: &[DeviceCapability]) -> Result<LocalDevice> {
        self.state
            .set_device_capabilities(capabilities.iter().cloned());
        // Don't re-register if we already have exactly those capabilities.
        if let Some(local_device) = self.state.server_local_device_info() {
            if capabilities == local_device.capabilities {
                return Ok(local_device.clone());
            }
        }
        let commands = self.register_capabilities(capabilities)?;
        let update = DeviceUpdateRequestBuilder::new()
            .available_commands(&commands)
            .build();
        self.update_device(update)
    }

    /// Re-register the device capabilities, this should only be used internally.
    pub(crate) fn reregister_current_capabilities(&mut self) -> Result<()> {
        let capabilities: Vec<_> = self.state.device_capabilities().iter().cloned().collect();
        let commands = self.register_capabilities(&capabilities)?;
        let update = DeviceUpdateRequestBuilder::new()
            .available_commands(&commands)
            .build();
        self.update_device(update)?;
        Ok(())
    }

    pub(crate) fn invoke_command(
        &self,
        command: &str,
        target: &Device,
        payload: &serde_json::Value,
        ttl: Option<u64>,
    ) -> Result<()> {
        let refresh_token = self.get_refresh_token()?;
        self.client.invoke_command(
            self.state.config(),
            refresh_token,
            command,
            &target.id,
            payload,
            ttl,
        )
    }

    /// Poll and parse any pending available command for our device.
    /// This should be called semi-regularly as the main method of
    /// commands delivery (push) can sometimes be unreliable on mobile devices.
    /// Typically called even when a push notification is received, so that
    /// any prior messages for which a push didn't arrive are still handled.
    ///
    /// **💾 This method alters the persisted account state.**
    pub fn poll_device_commands(
        &mut self,
        reason: CommandFetchReason,
    ) -> Result<Vec<IncomingDeviceCommand>> {
        let last_command_index = self.state.last_handled_command_index().unwrap_or(0);
        // We increment last_command_index by 1 because the server response includes the current index.
        self.fetch_and_parse_commands(last_command_index + 1, None, reason)
    }

    pub fn get_command_for_index(&mut self, index: u64) -> Result<IncomingDeviceCommand> {
        let refresh_token = self.get_refresh_token()?;
        let pending_commands =
            self.client
                .get_pending_commands(self.state.config(), refresh_token, index, Some(1))?;
        self.parse_commands_messages(pending_commands.messages, CommandFetchReason::Push(index))?
            .into_iter()
            .next()
            .ok_or_else(|| Error::CommandNotFound)
    }

    fn fetch_and_parse_commands(
        &mut self,
        index: u64,
        limit: Option<u64>,
        reason: CommandFetchReason,
    ) -> Result<Vec<IncomingDeviceCommand>> {
        let refresh_token = self.get_refresh_token()?;
        let pending_commands =
            self.client
                .get_pending_commands(self.state.config(), refresh_token, index, limit)?;
        if pending_commands.messages.is_empty() {
            return Ok(Vec::new());
        }
        info!("Handling {} messages", pending_commands.messages.len());
        let device_commands = self.parse_commands_messages(pending_commands.messages, reason)?;
        self.state
            .set_last_handled_command_index(pending_commands.index);
        Ok(device_commands)
    }

    fn parse_commands_messages(
        &mut self,
        messages: Vec<PendingCommand>,
        reason: CommandFetchReason,
    ) -> Result<Vec<IncomingDeviceCommand>> {
        let devices = self.get_devices(false)?;
        let parsed_commands = messages
            .into_iter()
            .filter_map(|msg| match self.parse_command(msg, &devices, reason) {
                Ok(device_command) => Some(device_command),
                Err(e) => {
                    error_support::report_error!(
                        "fxaclient-command",
                        "Error while processing command: {}",
                        e
                    );
                    None
                }
            })
            .collect();
        Ok(parsed_commands)
    }

    fn parse_command(
        &mut self,
        command: PendingCommand,
        devices: &[Device],
        reason: CommandFetchReason,
    ) -> Result<IncomingDeviceCommand> {
        let telem_reason = match reason {
            CommandFetchReason::Poll => telemetry::ReceivedReason::Poll,
            CommandFetchReason::Push(index) if command.index < index => {
                telemetry::ReceivedReason::PushMissed
            }
            _ => telemetry::ReceivedReason::Push,
        };
        let command_data = command.data;
        let sender = command_data
            .sender
            .and_then(|s| devices.iter().find(|i| i.id == s).cloned());
        match command_data.command.as_str() {
            commands::send_tab::COMMAND_NAME => {
                self.handle_send_tab_command(sender, command_data.payload, telem_reason)
            }
            commands::close_tabs::COMMAND_NAME => {
                self.handle_close_tabs_command(sender, command_data.payload, telem_reason)
            }
            _ => Err(Error::UnknownCommand(command_data.command)),
        }
    }

    pub fn set_device_name(&mut self, name: &str) -> Result<LocalDevice> {
        let update = DeviceUpdateRequestBuilder::new().display_name(name).build();
        self.update_device(update)
    }

    pub fn clear_device_name(&mut self) -> Result<()> {
        let update = DeviceUpdateRequestBuilder::new()
            .clear_display_name()
            .build();
        self.update_device(update)?;
        Ok(())
    }

    pub fn set_push_subscription(
        &mut self,
        push_subscription: PushSubscription,
    ) -> Result<LocalDevice> {
        let update = DeviceUpdateRequestBuilder::new()
            .push_subscription(&push_subscription)
            .build();
        self.update_device(update)
    }

    pub(crate) fn replace_device(
        &mut self,
        display_name: &str,
        device_type: &DeviceType,
        push_subscription: &Option<PushSubscription>,
        commands: &HashMap<String, String>,
    ) -> Result<()> {
        self.state.clear_server_local_device_info();
        let mut builder = DeviceUpdateRequestBuilder::new()
            .display_name(display_name)
            .device_type(device_type)
            .available_commands(commands);
        if let Some(push_subscription) = push_subscription {
            builder = builder.push_subscription(push_subscription)
        }
        self.update_device(builder.build())?;
        Ok(())
    }

    fn update_device(&mut self, update: DeviceUpdateRequest<'_>) -> Result<LocalDevice> {
        let refresh_token = self.get_refresh_token()?;
        let res = self
            .client
            .update_device_record(self.state.config(), refresh_token, update);
        match res {
            Ok(resp) => {
                self.state.set_current_device_id(resp.id.clone());
                let local_device = LocalDevice::from(resp);
                self.state
                    .update_server_local_device_info(local_device.clone());
                Ok(local_device)
            }
            Err(err) => {
                // We failed to write an update to the server.
                // Clear local state so that we'll be sure to retry later.
                self.state.clear_server_local_device_info();
                Err(err)
            }
        }
    }

    /// Retrieve the current device id from state
    pub fn get_current_device_id(&mut self) -> Result<String> {
        match self.state.current_device_id() {
            Some(ref device_id) => Ok(device_id.to_string()),
            None => Err(Error::NoCurrentDeviceId),
        }
    }

    /// Generate the command to be registered with the server for
    /// the given capability.
    ///
    /// **💾 This method alters the persisted account state.**
    pub(crate) fn generate_command_data(&mut self, capability: DeviceCapability) -> Result<String> {
        let own_keys = self.load_or_generate_command_keys(capability)?;
        let public_keys: PublicCommandKeys = own_keys.into();
        let oldsync_key = self.get_scoped_key(scopes::OLD_SYNC)?;
        public_keys.as_command_data(oldsync_key)
    }

    fn load_or_generate_command_keys(
        &mut self,
        capability: DeviceCapability,
    ) -> Result<PrivateCommandKeys> {
        match capability {
            DeviceCapability::SendTab => self.load_or_generate_send_tab_keys(),
            DeviceCapability::CloseTabs => self.load_or_generate_close_tabs_keys(),
        }
    }
}

impl TryFrom<String> for DeviceCapability {
    type Error = Error;

    fn try_from(command: String) -> Result<Self> {
        match command.as_str() {
            commands::send_tab::COMMAND_NAME => Ok(DeviceCapability::SendTab),
            commands::close_tabs::COMMAND_NAME => Ok(DeviceCapability::CloseTabs),
            _ => Err(Error::UnknownCommand(command)),
        }
    }
}

impl From<UpdateDeviceResponse> for LocalDevice {
    fn from(resp: UpdateDeviceResponse) -> Self {
        Self {
            id: resp.id,
            display_name: resp.display_name,
            device_type: resp.device_type,
            capabilities: resp
                .available_commands
                .into_keys()
                .filter_map(|command| match command.try_into() {
                    Ok(capability) => Some(capability),
                    Err(e) => {
                        warn!("While parsing UpdateDeviceResponse: {e}");
                        None
                    }
                })
                .collect(),
            push_subscription: resp.push_subscription.map(Into::into),
            push_endpoint_expired: resp.push_endpoint_expired,
        }
    }
}

impl TryFrom<Device> for crate::Device {
    type Error = Error;
    fn try_from(d: Device) -> Result<Self> {
        let capabilities: Vec<_> = d
            .available_commands
            .keys()
            .filter_map(|k| match k.as_str() {
                commands::send_tab::COMMAND_NAME => Some(DeviceCapability::SendTab),
                commands::close_tabs::COMMAND_NAME => Some(DeviceCapability::CloseTabs),
                _ => None,
            })
            .collect();
        Ok(crate::Device {
            id: d.common.id,
            display_name: d.common.display_name,
            device_type: d.common.device_type,
            capabilities,
            push_subscription: d.common.push_subscription.map(Into::into),
            push_endpoint_expired: d.common.push_endpoint_expired,
            is_current_device: d.is_current_device,
            last_access_time: d.last_access_time.map(TryFrom::try_from).transpose()?,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::internal::http_client::*;
    use crate::internal::oauth::RefreshToken;
    use crate::internal::Config;
    use crate::ScopedKey;
    use mockall::predicate::always;
    use mockall::predicate::eq;
    use nss_as::ensure_initialized;
    use std::collections::HashSet;
    use std::sync::Arc;

    fn setup() -> FirefoxAccount {
        ensure_initialized();

        // I'd love to be able to configure a single mocked client here,
        // but can't work out how to do that within the typesystem.
        let config = Config::stable_dev("12345678", "https://foo.bar");
        let mut fxa = FirefoxAccount::with_config(config);
        fxa.state.force_refresh_token(RefreshToken {
            token: "refreshtok".to_string(),
            scopes: HashSet::default(),
        });
        fxa.state.insert_scoped_key("https://identity.mozilla.com/apps/oldsync", ScopedKey {
            kty: "oct".to_string(),
            scope: "https://identity.mozilla.com/apps/oldsync".to_string(),
            k: "kMtwpVC0ZaYFJymPza8rXK_0CgCp3KMwRStwGfBRBDtL6hXRDVJgQFaoOQ2dimw0Bko5WVv2gNTy7RX5zFYZHg".to_string(),
            kid: "1542236016429-Ox1FbJfFfwTe5t-xq4v2hQ".to_string(),
        });
        fxa
    }

    #[test]
    fn test_ensure_capabilities_does_not_hit_the_server_if_nothing_has_changed() {
        let mut fxa = setup();

        // Do an initial call to ensure_capabilities().
        let mut client = MockFxAClient::new();
        client
            .expect_update_device_record()
            .with(always(), eq("refreshtok"), always())
            .times(1)
            .returning(|_, _, _| {
                Ok(UpdateDeviceResponse {
                    id: "device1".to_string(),
                    display_name: "".to_string(),
                    device_type: DeviceType::Desktop,
                    push_subscription: None,
                    available_commands: HashMap::from([(
                        commands::send_tab::COMMAND_NAME.to_owned(),
                        "fake-command-data".to_owned(),
                    )]),
                    push_endpoint_expired: false,
                })
            });
        fxa.set_client(Arc::new(client));
        fxa.ensure_capabilities(&[DeviceCapability::SendTab])
            .unwrap();
        let saved = fxa.to_json().unwrap();

        // Do another call with the same capabilities.
        // The MockFxAClient will panic if it tries to hit the network again, which it shouldn't.
        fxa.ensure_capabilities(&[DeviceCapability::SendTab])
            .unwrap();

        // Do another call with the same capabilities , after restoring from disk.
        // The MockFxAClient will panic if it tries to hit the network, which it shouldn't.
        let mut restored = FirefoxAccount::from_json(&saved).unwrap();
        restored.set_client(Arc::new(MockFxAClient::new()));
        restored
            .ensure_capabilities(&[DeviceCapability::SendTab])
            .unwrap();
    }

    #[test]
    fn test_ensure_capabilities_updates_the_server_if_capabilities_increase() {
        let mut fxa = setup();

        // Do an initial call to ensure_capabilities().
        let mut client = MockFxAClient::new();
        client
            .expect_update_device_record()
            .with(always(), eq("refreshtok"), always())
            .times(1)
            .returning(|_, _, _| {
                Ok(UpdateDeviceResponse {
                    id: "device1".to_string(),
                    display_name: "".to_string(),
                    device_type: DeviceType::Desktop,
                    push_subscription: None,
                    available_commands: HashMap::default(),
                    push_endpoint_expired: false,
                })
            });
        fxa.set_client(Arc::new(client));

        fxa.ensure_capabilities(&[]).unwrap();
        let saved = fxa.to_json().unwrap();

        // Do another call with reduced capabilities.
        let mut client = MockFxAClient::new();
        client
            .expect_update_device_record()
            .with(always(), eq("refreshtok"), always())
            .times(1)
            .returning(|_, _, _| {
                Ok(UpdateDeviceResponse {
                    id: "device1".to_string(),
                    display_name: "".to_string(),
                    device_type: DeviceType::Desktop,
                    push_subscription: None,
                    available_commands: HashMap::from([(
                        commands::send_tab::COMMAND_NAME.to_owned(),
                        "fake-command-data".to_owned(),
                    )]),
                    push_endpoint_expired: false,
                })
            });
        fxa.set_client(Arc::new(client));

        fxa.ensure_capabilities(&[DeviceCapability::SendTab])
            .unwrap();

        // Do another call with the same capabilities , after restoring from disk.
        // The MockFxAClient will panic if it tries to hit the network, which it shouldn't.
        let mut restored = FirefoxAccount::from_json(&saved).unwrap();
        let mut client = MockFxAClient::new();
        client
            .expect_update_device_record()
            .with(always(), eq("refreshtok"), always())
            .returning(|_, _, _| {
                Ok(UpdateDeviceResponse {
                    id: "device1".to_string(),
                    display_name: "".to_string(),
                    device_type: DeviceType::Desktop,
                    push_subscription: None,
                    available_commands: HashMap::from([(
                        commands::send_tab::COMMAND_NAME.to_owned(),
                        "fake-command-data".to_owned(),
                    )]),
                    push_endpoint_expired: false,
                })
            });
        restored.set_client(Arc::new(client));

        restored
            .ensure_capabilities(&[DeviceCapability::SendTab])
            .unwrap();
    }

    #[test]
    fn test_ensure_capabilities_updates_the_server_if_capabilities_reduce() {
        let mut fxa = setup();

        // Do an initial call to ensure_capabilities().
        let mut client = MockFxAClient::new();
        client
            .expect_update_device_record()
            .with(always(), eq("refreshtok"), always())
            .times(1)
            .returning(|_, _, _| {
                Ok(UpdateDeviceResponse {
                    id: "device1".to_string(),
                    display_name: "".to_string(),
                    device_type: DeviceType::Desktop,
                    push_subscription: None,
                    available_commands: HashMap::from([(
                        commands::send_tab::COMMAND_NAME.to_owned(),
                        "fake-command-data".to_owned(),
                    )]),
                    push_endpoint_expired: false,
                })
            });
        fxa.set_client(Arc::new(client));

        fxa.ensure_capabilities(&[DeviceCapability::SendTab])
            .unwrap();
        let saved = fxa.to_json().unwrap();

        // Do another call with reduced capabilities.
        let mut client = MockFxAClient::new();
        client
            .expect_update_device_record()
            .with(always(), eq("refreshtok"), always())
            .times(1)
            .returning(|_, _, _| {
                Ok(UpdateDeviceResponse {
                    id: "device1".to_string(),
                    display_name: "".to_string(),
                    device_type: DeviceType::Desktop,
                    push_subscription: None,
                    available_commands: HashMap::default(),
                    push_endpoint_expired: false,
                })
            });
        fxa.set_client(Arc::new(client));

        fxa.ensure_capabilities(&[]).unwrap();

        // Do another call with the same capabilities , after restoring from disk.
        // The MockFxAClient will panic if it tries to hit the network, which it shouldn't.
        let mut restored = FirefoxAccount::from_json(&saved).unwrap();
        let mut client = MockFxAClient::new();
        client
            .expect_update_device_record()
            .with(always(), eq("refreshtok"), always())
            .times(1)
            .returning(|_, _, _| {
                Ok(UpdateDeviceResponse {
                    id: "device1".to_string(),
                    display_name: "".to_string(),
                    device_type: DeviceType::Desktop,
                    push_subscription: None,
                    available_commands: HashMap::default(),
                    push_endpoint_expired: false,
                })
            });
        restored.set_client(Arc::new(client));

        restored.ensure_capabilities(&[]).unwrap();
    }

    #[test]
    fn test_ensure_capabilities_will_reregister_after_new_login_flow() {
        let mut fxa = setup();

        // Do an initial call to ensure_capabilities().
        let mut client = MockFxAClient::new();
        client
            .expect_update_device_record()
            .with(always(), eq("refreshtok"), always())
            .times(1)
            .returning(|_, _, _| {
                Ok(UpdateDeviceResponse {
                    id: "device1".to_string(),
                    display_name: "".to_string(),
                    device_type: DeviceType::Desktop,
                    push_subscription: None,
                    available_commands: HashMap::from([(
                        commands::send_tab::COMMAND_NAME.to_owned(),
                        "fake-command-data".to_owned(),
                    )]),
                    push_endpoint_expired: false,
                })
            });
        fxa.set_client(Arc::new(client));
        fxa.ensure_capabilities(&[DeviceCapability::SendTab])
            .unwrap();

        // Fake that we've completed a new login flow.
        // (which annoyingly makes a bunch of network requests)
        let mut client = MockFxAClient::new();
        client
            .expect_destroy_access_token()
            .with(always(), always())
            .times(1)
            .returning(|_, _| {
                Err(Error::RemoteError {
                    code: 500,
                    errno: 999,
                    error: "server error".to_string(),
                    message: "this will be ignored anyway".to_string(),
                    info: "".to_string(),
                })
            });
        client
            .expect_get_devices()
            .with(always(), always())
            .times(1)
            .returning(|_, _| {
                Err(Error::RemoteError {
                    code: 500,
                    errno: 999,
                    error: "server error".to_string(),
                    message: "this will be ignored anyway".to_string(),
                    info: "".to_string(),
                })
            });
        client
            .expect_destroy_refresh_token()
            .with(always(), always())
            .times(1)
            .returning(|_, _| {
                Err(Error::RemoteError {
                    code: 500,
                    errno: 999,
                    error: "server error".to_string(),
                    message: "this will be ignored anyway".to_string(),
                    info: "".to_string(),
                })
            });
        fxa.set_client(Arc::new(client));

        fxa.handle_oauth_response(
            OAuthTokenResponse {
                keys_jwe: None,
                refresh_token: Some("newRefreshTok".to_string()),
                session_token: None,
                expires_in: 12345,
                scope: "profile".to_string(),
                access_token: "accesstok".to_string(),
            },
            None,
        )
        .unwrap();

        assert!(fxa.state.server_local_device_info().is_none());

        // Do another call with the same capabilities.
        // It should re-register, as server-side state may have changed.
        let mut client = MockFxAClient::new();
        client
            .expect_update_device_record()
            .with(always(), eq("newRefreshTok"), always())
            .times(1)
            .returning(|_, _, _| {
                Ok(UpdateDeviceResponse {
                    id: "device1".to_string(),
                    display_name: "".to_string(),
                    device_type: DeviceType::Desktop,
                    push_subscription: None,
                    available_commands: HashMap::from([(
                        commands::send_tab::COMMAND_NAME.to_owned(),
                        "fake-command-data".to_owned(),
                    )]),
                    push_endpoint_expired: false,
                })
            });
        fxa.set_client(Arc::new(client));
        fxa.ensure_capabilities(&[DeviceCapability::SendTab])
            .unwrap();
    }

    #[test]
    fn test_ensure_capabilities_updates_the_server_if_previous_attempt_failed() {
        let mut fxa = setup();

        // Do an initial call to ensure_capabilities(), that fails.
        let mut client = MockFxAClient::new();
        client
            .expect_update_device_record()
            .with(always(), eq("refreshtok"), always())
            .times(1)
            .returning(|_, _, _| {
                Err(Error::RemoteError {
                    code: 500,
                    errno: 999,
                    error: "server error".to_string(),
                    message: "this will be ignored anyway".to_string(),
                    info: "".to_string(),
                })
            });
        fxa.set_client(Arc::new(client));

        fxa.ensure_capabilities(&[DeviceCapability::SendTab])
            .unwrap_err();

        // Do another call, which should re-attempt the update.
        let mut client = MockFxAClient::new();
        client
            .expect_update_device_record()
            .with(always(), eq("refreshtok"), always())
            .times(1)
            .returning(|_, _, _| {
                Ok(UpdateDeviceResponse {
                    id: "device1".to_string(),
                    display_name: "".to_string(),
                    device_type: DeviceType::Desktop,
                    push_subscription: None,
                    available_commands: HashMap::from([(
                        commands::send_tab::COMMAND_NAME.to_owned(),
                        "fake-command-data".to_owned(),
                    )]),
                    push_endpoint_expired: false,
                })
            });
        fxa.set_client(Arc::new(client));

        fxa.ensure_capabilities(&[DeviceCapability::SendTab])
            .unwrap();
    }

    #[test]
    fn test_get_devices() {
        let mut fxa = setup();
        let mut client = MockFxAClient::new();
        client
            .expect_get_devices()
            .with(always(), always())
            .times(1)
            .returning(|_, _| {
                Ok(vec![Device {
                    common: DeviceResponseCommon {
                        id: "device1".into(),
                        display_name: "".to_string(),
                        device_type: DeviceType::Desktop,
                        push_subscription: None,
                        available_commands: HashMap::new(),
                        push_endpoint_expired: true,
                    },
                    is_current_device: true,
                    location: DeviceLocation {
                        city: None,
                        country: None,
                        state: None,
                        state_code: None,
                    },
                    last_access_time: None,
                }])
            });

        fxa.set_client(Arc::new(client));
        assert!(fxa.devices_cache.is_none());

        assert!(fxa.get_devices(false).is_ok());
        assert!(fxa.devices_cache.is_some());

        let cache = fxa.devices_cache.clone().unwrap();
        assert!(!cache.response.is_empty());
        assert!(cache.cached_at > 0);

        let cached_devices = cache.response;
        assert_eq!(cached_devices[0].id, "device1".to_string());

        // Check that a second call to get_devices doesn't hit the server
        assert!(fxa.get_devices(false).is_ok());
        assert!(fxa.devices_cache.is_some());

        let cache2 = fxa.devices_cache.unwrap();
        let cached_devices2 = cache2.response;

        assert_eq!(cache.cached_at, cache2.cached_at);
        assert_eq!(cached_devices.len(), cached_devices2.len());
        assert_eq!(cached_devices[0].id, cached_devices2[0].id);
    }

    #[test]
    fn test_get_devices_network_errors() {
        let mut fxa = setup();
        let mut client = MockFxAClient::new();
        client
            .expect_get_devices()
            .with(always(), always())
            .times(1)
            .returning(|_, _| {
                Err(Error::RemoteError {
                    code: 500,
                    errno: 101,
                    error: "Did not work!".to_owned(),
                    message: "Did not work!".to_owned(),
                    info: "Did not work!".to_owned(),
                })
            });

        fxa.set_client(Arc::new(client));
        assert!(fxa.devices_cache.is_none());

        let res = fxa.get_devices(false);

        assert!(res.is_err());
        assert!(fxa.devices_cache.is_none());
    }
}
