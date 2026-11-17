// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![expect(clippy::missing_errors_doc, reason = "Passing up tokio errors.")]

use std::{io, iter, net::SocketAddr};

use neqo_common::datagram;
use neqo_udp::{DatagramIter, RecvBuf};
use tracing::{debug, warn};

/// Ideally this would live in [`neqo_udp`]. [`neqo_udp`] is used in Firefox.
///
/// Firefox uses `cargo vet`. [`tokio`] the dependency of [`neqo_udp`] is not
/// audited as `safe-to-deploy`. `cargo vet` will require `safe-to-deploy` for
/// [`tokio`] even when behind a feature flag.
///
/// See <https://github.com/mozilla/cargo-vet/issues/626>.
pub struct Socket {
    state: quinn_udp::UdpSocketState,
    inner: tokio::net::UdpSocket,
    /// Cached at bind time to avoid a `getsockname` syscall on every `recv()`.
    local_addr: SocketAddr,
}

impl Socket {
    /// Create a new [`Socket`] bound to the provided address, not managed externally.
    pub fn bind<A: std::net::ToSocketAddrs>(addr: A) -> Result<Self, io::Error> {
        const ONE_MB: usize = 1 << 20;

        let socket = std::net::UdpSocket::bind(addr)?;
        let state = quinn_udp::UdpSocketState::new((&socket).into())?;
        #[cfg(apple)]
        // SAFETY: Quinn-udp resolves `sendmsg_x`/`recvmsg_x` via `dlsym` at
        // runtime and falls back to standard `sendmsg`/`recvmsg` if unavailable,
        // so this is safe on all supported Apple OS versions.
        // neqo-bin always enables the Apple fast datapath as a canary.
        unsafe {
            state.set_apple_fast_path();
        }

        // FIXME: We need to experiment if increasing this actually improves performance.
        // Also, on BSD and Apple targets, this seems to increase the `net.inet.udp.maxdgram`
        // sysctl, which is not the same as the socket buffer.
        // if send_buf_before < ONE_MB {
        //     state.set_send_buffer_size((&socket).into(), ONE_MB)?;
        //     let send_buf_after = state.send_buffer_size((&socket).into())?;
        //     debug!("Increasing socket send buffer size from {send_buf_before} to {ONE_MB}, now:
        // {send_buf_after}"); } else {
        //     debug!("Default socket send buffer size is {send_buf_before}, not changing");
        // }
        debug!(
            "Default socket send buffer size is {:?}",
            state.send_buffer_size((&socket).into())
        );

        let recv_buf_before = state.recv_buffer_size((&socket).into())?;
        if recv_buf_before < ONE_MB {
            // Same as Firefox.
            // <https://searchfox.org/mozilla-central/rev/fa5b44a4ea5c98b6a15f39638ea4cd04dc271f3d/modules/libpref/init/StaticPrefList.yaml#13474-13477>
            // Halve the request until the OS accepts it, e.g., NetBSD caps it at `kern.sbmax`.
            for size in iter::successors(Some(ONE_MB), |size| Some(size / 2))
                .take_while(|&s| s > recv_buf_before)
            {
                match state.set_recv_buffer_size((&socket).into(), size) {
                    Ok(()) => break,
                    Err(e) => warn!("Failed to set socket recv buffer size to {size}: {e}"),
                }
            }
            debug!(
                "Increasing socket recv buffer size from {recv_buf_before}, now: {:?}",
                state.recv_buffer_size((&socket).into())
            );
        } else {
            debug!("Default socket receive buffer size is {recv_buf_before}, not changing");
        }

        let inner = tokio::net::UdpSocket::from_std(socket)?;
        let local_addr = inner.local_addr()?;
        Ok(Self {
            state,
            inner,
            local_addr,
        })
    }

    /// The local address this socket was bound to; cannot change after [`Self::bind`].
    #[must_use]
    pub const fn local_addr(&self) -> SocketAddr {
        self.local_addr
    }

    /// See [`tokio::net::UdpSocket::writable`].
    pub async fn writable(&self) -> Result<(), io::Error> {
        self.inner.writable().await
    }

    /// See [`tokio::net::UdpSocket::readable`].
    pub async fn readable(&self) -> Result<(), io::Error> {
        self.inner.readable().await
    }

    /// Send a [`datagram::Batch`] on the given [`Socket`].
    pub fn send(&self, d: &datagram::Batch) -> io::Result<()> {
        self.inner.try_io(tokio::io::Interest::WRITABLE, || {
            neqo_udp::send_inner(&self.state, (&self.inner).into(), d)
        })
    }

    /// Receive a batch of [`neqo_common::Datagram`]s on the given [`Socket`], each set with
    /// this socket's local address.
    pub fn recv<'a>(
        &self,
        recv_buf: &'a mut RecvBuf,
    ) -> Result<Option<DatagramIter<'a>>, io::Error> {
        self.inner
            .try_io(tokio::io::Interest::READABLE, || {
                neqo_udp::recv_inner(self.local_addr, &self.state, &self.inner, recv_buf)
            })
            .map(Some)
            .or_else(|e| {
                if e.kind() == io::ErrorKind::WouldBlock {
                    Ok(None)
                } else {
                    Err(e)
                }
            })
    }

    pub fn max_gso_segments(&self) -> usize {
        self.state.max_gso_segments()
    }

    /// Whether transmitted datagrams might get fragmented by the IP layer
    ///
    /// Returns `false` on targets which employ e.g. the `IPV6_DONTFRAG` socket option.
    pub fn may_fragment(&self) -> bool {
        self.state.may_fragment()
    }
}
