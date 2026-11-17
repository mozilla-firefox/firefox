/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

use super::TestDevice;
use crate::consts::{Capability, HIDCmd, CID_BROADCAST};
use crate::ctap2::commands::{CommandError, RequestCtap1, RequestCtap2, Retryable, StatusCode};
use crate::status_update::{send_status, MessageDirection};
use crate::transport::errors::{ApduErrorStatus, HIDError};
use crate::transport::{CtapVersionSupport, FidoDevice, FidoDeviceIO, FidoProtocol};
use crate::u2ftypes::{U2FDeviceInfo, U2FHIDCont, U2FHIDInit, U2FHIDInitResp};
use crate::util::io_err;
use crate::StatusUpdate;
use rand::{thread_rng, RngCore};
use std::cmp::Eq;
use std::fmt;
use std::hash::Hash;
use std::io;
use std::io::{Read, Write};
use std::sync::mpsc::Sender;
use std::thread;
use std::time::Duration;

pub trait HIDDevice: FidoDevice + Read + Write {
    type BuildParameters: Sized;
    type Id: fmt::Debug + PartialEq + Eq + Hash + Sized;

    // Open device, verify that it is indeed a CTAP device and potentially read initial values
    fn new(parameters: Self::BuildParameters) -> Result<Self, (HIDError, Self::Id)>;
    fn id(&self) -> Self::Id;

    fn get_device_info(&self) -> Option<U2FDeviceInfo>;
    fn set_device_info(&mut self, dev_info: U2FDeviceInfo);

    // Channel ID management
    fn get_cid(&self) -> &[u8; 4];
    fn set_cid(&mut self, cid: [u8; 4]);

    // HID report sizes
    fn in_rpt_size(&self) -> usize;
    fn out_rpt_size(&self) -> usize;

    fn get_property(&self, prop_name: &str) -> io::Result<String>;

    // Initialize on a protocol-level
    fn pre_init(&mut self) -> Result<(), HIDError> {
        if self.initialized() {
            return Ok(());
        }

        let mut nonce = [0u8; 8];
        thread_rng().fill_bytes(&mut nonce);

        // Send Init to broadcast address to create a new channel
        self.set_cid(CID_BROADCAST);
        let (cmd, raw) = HIDDevice::sendrecv(self, HIDCmd::Init, &nonce, &|| true)?;
        if cmd != HIDCmd::Init {
            return Err(HIDError::DeviceError);
        }

        let rsp = U2FHIDInitResp::read(&raw, &nonce)?;
        // Set the new Channel ID
        self.set_cid(rsp.cid);

        let vendor = self
            .get_property("Manufacturer")
            .unwrap_or_else(|_| String::from("Unknown Vendor"));
        let product = self
            .get_property("Product")
            .unwrap_or_else(|_| String::from("Unknown Device"));

        let info = U2FDeviceInfo {
            vendor_name: vendor.as_bytes().to_vec(),
            device_name: product.as_bytes().to_vec(),
            version_interface: rsp.version_interface,
            version_major: rsp.version_major,
            version_minor: rsp.version_minor,
            version_build: rsp.version_build,
            cap_flags: rsp.cap_flags,
        };
        debug!("{:?}: {:?}", self.id(), info);
        self.set_device_info(info);

        // A CTAPHID host SHALL accept a response size that is longer than the
        // anticipated size to allow for future extensions of the protocol, yet
        // maintaining backwards compatibility. Future versions will maintain
        // the response structure of the current version, but additional fields
        // may be added.

        Ok(())
    }

    fn sendrecv(
        &mut self,
        cmd: HIDCmd,
        send: &[u8],
        keep_alive: &dyn Fn() -> bool,
    ) -> io::Result<(HIDCmd, Vec<u8>)> {
        self.u2f_write(cmd.into(), send)?;
        debug!("sent to Device {:?} cmd={:?}: {:?}", self.id(), cmd, send);
        loop {
            let (cmd, data) = self.u2f_read()?;
            if cmd != HIDCmd::Keepalive {
                debug!(
                    "got from Device {:?} status={:?}: {:?}",
                    self.id(),
                    cmd,
                    data
                );
                return Ok((cmd, data));
            }
            // The authenticator might send us HIDCmd::Keepalive messages indefinitely, e.g. if
            // it's waiting for user presence. The keep_alive function is used to cancel the
            // transaction.
            if !keep_alive() {
                break;
            }
        }

        // If this is a CTAP2 device we can tell the authenticator to cancel the transaction on its
        // side as well. There's nothing to do for U2F/CTAP1 devices.
        if self.get_protocol() == FidoProtocol::CTAP2 {
            self.u2f_write(u8::from(HIDCmd::Cancel), &[])?;
        }
        // For CTAP2 devices we expect to read
        //  (HIDCmd::Cbor, [CTAP2_ERR_KEEPALIVE_CANCEL])
        // for U2F/CTAP1 we expect to read
        //  (HIDCmd::Keepalive, [status]).
        self.u2f_read()
    }

    fn u2f_write(&mut self, cmd: u8, send: &[u8]) -> io::Result<()> {
        let mut count = U2FHIDInit::write(self, cmd, send)?;

        // Send continuation packets.
        let mut sequence = 0u8;
        while count < send.len() {
            count += U2FHIDCont::write(self, sequence, &send[count..])?;
            sequence += 1;
        }

        Ok(())
    }

    fn u2f_read(&mut self) -> io::Result<(HIDCmd, Vec<u8>)> {
        // Now we read. This happens in 2 chunks: The initial packet, which has
        // the size we expect overall, then continuation packets, which will
        // fill in data until we have everything.
        let (cmd, data) = {
            let (cmd, mut data) = U2FHIDInit::read(self)?;

            trace!("init frame data read: {:04X?}", &data);
            let mut sequence = 0u8;
            while data.len() < data.capacity() {
                let max = data.capacity() - data.len();
                data.extend_from_slice(&U2FHIDCont::read(self, sequence, max)?);
                sequence += 1;
            }
            (cmd, data)
        };
        trace!("u2f_read({:?}) cmd={:?}: {:04X?}", self.id(), cmd, &data);
        Ok((cmd, data))
    }
}

impl<T: HIDDevice> CtapVersionSupport for T {
    fn supports_ctap1(&self) -> bool {
        self.get_device_info()
            .is_some_and(|i| {
                if i.cap_flags.contains(Capability::NMSG) {
                    let r = !i.cap_flags.contains(Capability::CBOR);
                    if r {
                        // https://bugzilla.mozilla.org/show_bug.cgi?id=2093140
                        warn!("Authenticator set capability 'NMSG' but not 'CBOR', this is not CTAP2 compliant! Using as CTAP1-only.");
                    }
                    r
                } else {
                    true
                }
            })
    }

    fn supports_ctap2(&self) -> bool {
        self.get_device_info()
            .is_some_and(|i| i.cap_flags.contains(Capability::CBOR))
    }
}

#[cfg(not(test))]
impl<T: HIDDevice> TestDevice for T {}

impl<T: HIDDevice + TestDevice> FidoDeviceIO for T {
    fn send_msg_cancellable<Out, Req: RequestCtap1<Output = Out> + RequestCtap2<Output = Out>>(
        &mut self,
        msg: &Req,
        keep_alive: &dyn Fn() -> bool,
        status: Option<&Sender<StatusUpdate>>,
    ) -> Result<Out, HIDError> {
        if !self.initialized() {
            return Err(HIDError::DeviceNotInitialized);
        }
        match self.get_protocol() {
            FidoProtocol::CTAP1 => self.send_ctap1_cancellable(msg, keep_alive, status),
            FidoProtocol::CTAP2 => self.send_cbor_cancellable(msg, keep_alive, status),
        }
    }

    fn send_cbor_cancellable<Req: RequestCtap2>(
        &mut self,
        msg: &Req,
        keep_alive: &dyn Fn() -> bool,
        status: Option<&Sender<StatusUpdate>>,
    ) -> Result<Req::Output, HIDError> {
        debug!("sending {:?} to {:?}", msg, self);
        if !self.supports_ctap2() {
            return Err(HIDError::UnexpectedVersion);
        }

        #[cfg(test)]
        {
            if self.skip_serialization() {
                return self.send_ctap2_unserialized(msg);
            }
        }

        if let Some(status) = status {
            send_status(
                status,
                StatusUpdate::RequestLogging(MessageDirection::Ctap2Request, format!("{msg:?}")),
            );
        }

        let mut data = msg.wire_format()?;
        let mut buf: Vec<u8> = Vec::with_capacity(data.len() + 1);
        // CTAP2 command
        buf.push(msg.command() as u8);
        // payload
        buf.append(&mut data);
        let buf = buf;

        let (cmd, resp) = self.sendrecv(HIDCmd::Cbor, &buf, keep_alive)?;
        let response = if cmd == HIDCmd::Cbor {
            let response = msg.handle_response_ctap2(self, &resp)?;
            Ok(response)
        } else {
            Err(HIDError::UnexpectedCmd(cmd.into()))
        };
        if let Some(status) = status {
            send_status(
                status,
                StatusUpdate::RequestLogging(
                    MessageDirection::Ctap2Response,
                    format!("{response:?}"),
                ),
            );
        }
        response
    }

    fn send_ctap1_cancellable<Req: RequestCtap1>(
        &mut self,
        msg: &Req,
        keep_alive: &dyn Fn() -> bool,
        status: Option<&Sender<StatusUpdate>>,
    ) -> Result<Req::Output, HIDError> {
        debug!("sending {:?} to {:?}", msg, self);
        if !self.supports_ctap1() {
            return Err(HIDError::UnexpectedVersion);
        }

        #[cfg(test)]
        {
            if self.skip_serialization() {
                return self.send_ctap1_unserialized(msg);
            }
        }

        if let Some(status) = status {
            send_status(
                status,
                StatusUpdate::RequestLogging(MessageDirection::Ctap1Request, format!("{msg:?}")),
            );
        }

        let (data, add_info) = msg.ctap1_format()?;

        while keep_alive() {
            // sendrecv will not block with a CTAP1 device
            let (cmd, mut data) = self.sendrecv(HIDCmd::Msg, &data, &|| true)?;
            let response = if cmd == HIDCmd::Msg {
                if data.len() < 2 {
                    return Err(io_err("Unexpected Response: shorter than expected").into());
                }
                let split_at = data.len() - 2;
                let msg_status = data.split_off(split_at);
                // This will bubble up error if status != no error
                let msg_status = ApduErrorStatus::from([msg_status[0], msg_status[1]]);

                match msg.handle_response_ctap1(self, msg_status, &data, &add_info) {
                    Ok(out) => Ok(out),
                    Err(Retryable::Retry) => {
                        // sleep 100ms then loop again
                        // TODO(baloo): meh, use tokio instead?
                        thread::sleep(Duration::from_millis(100));
                        continue;
                    }
                    Err(Retryable::Error(e)) => Err(e),
                }
            } else {
                Err(HIDError::UnexpectedCmd(cmd.into()))
            };

            if let Some(status) = status {
                send_status(
                    status,
                    StatusUpdate::RequestLogging(
                        MessageDirection::Ctap1Response,
                        format!("{response:?}"),
                    ),
                );
            }
            return response;
        }

        Err(HIDError::Command(CommandError::StatusCode(
            StatusCode::KeepaliveCancel,
            None,
        )))
    }
}

#[cfg(test)]
mod tests {
    use crate::{transport::device_selector::Device, CtapVersionSupport};

    /// Test all possible capabilities flags.
    #[test]
    fn test_capabilities() {
        // (name, capabilities, supports_ctap1, supports_ctap2)
        let tests = [
            ("capabilities/ctap1_only", 0x0, true, false),
            ("capabilities/ctap1_wink", 0x1, true, false),
            // CBOR (CTAP1 + CTAP2)
            ("capabilities/both", 0x4, true, true),
            ("capabilities/both_wink", 0x5, true, true),
            ("capabilities/both_6", 0x6, true, true),
            // NMSG (CTAP2 only)
            ("capabilities/ctap2_only", 0xc, false, true),
            ("capabilities/ctap2_wink", 0xd, false, true),
            // Many Feitian authenticators set bit 0x2, which we should ignore.
            ("capabilities/feitian", 0x7, true, true),
            ("capabilities/feitian_multipass", 0xf, false, true),
            // Don't have examples of these.
            ("capabilities/ctap1_only_2", 0x2, true, false),
            ("capabilities/ctap1_only_3", 0x3, true, false),
            ("capabilities/ctap2_only_e", 0xe, false, true),
            // "MitID chip" is a modified Feitian MultiPass FIDO issued by the Danish government.
            // These are known to accept CTAP1 commands, but have invalid capability flags:
            // https://bugzilla.mozilla.org/show_bug.cgi?id=2093140
            ("capabilities/mitid_chip", 0xb, true, false),
            // Invalid flag combinations, where we should fall back to CTAP1.
            ("capabilities/invalid_8", 0x8, true, false),
            ("capabilities/invalid_9", 0x9, true, false),
            ("capabilities/invalid_a", 0xa, true, false),
        ];

        for (name, capabilities, supports_ctap1, supports_ctap2) in tests {
            for extra_bits in 0u8..=0xf {
                let capabilities = capabilities | (extra_bits << 4);
                let device = Device::new_pre_inited_capabilities(name, capabilities);
                assert_eq!(
                    supports_ctap1,
                    device.supports_ctap1(),
                    "supports_ctap1: {name}, {capabilities:02x}"
                );
                assert_eq!(
                    supports_ctap2,
                    device.supports_ctap2(),
                    "supports_ctap2: {name}, {capabilities:02x}"
                );
            }
        }
    }
}
