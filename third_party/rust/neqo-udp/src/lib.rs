// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![cfg_attr(all(coverage_nightly, test), feature(coverage_attribute))]
#![expect(
    clippy::missing_errors_doc,
    reason = "Functions simply delegate to tokio and quinn-udp."
)]

use std::{
    array,
    io::{self, IoSliceMut},
    iter,
    net::SocketAddr,
    slice::{self, ChunksMut},
};

use neqo_common::{Datagram, Tos, datagram};
use quinn_udp::{EcnCodepoint, RecvMeta, Transmit, UdpSocketState};
use tracing::{debug, trace};
#[cfg(windows)]
use windows::Win32::Networking::WinSock;

/// Receive buffer size
///
/// Fits a maximum size UDP datagram, or, on platforms with segmentation
/// offloading, multiple smaller datagrams.
const RECV_BUF_SIZE: usize = u16::MAX as usize;

/// The number of buffers to pass to the OS on [`Socket::recv`].
///
/// Platforms without segmentation offloading, i.e. platforms not able to read
/// multiple datagrams into a single buffer, can benefit from using multiple
/// buffers instead.
///
/// Platforms with segmentation offloading have not shown performance
/// improvements when additionally using multiple buffers.
///
/// - Linux/Android: use segmentation offloading via GRO
/// - Windows: use segmentation offloading via URO (caveat see <https://github.com/quinn-rs/quinn/issues/2041>)
/// - Apple: no segmentation offloading available, use multiple buffers
#[cfg(not(apple))]
const NUM_BUFS: usize = 1;
#[cfg(apple)]
// Value approximated based on neqo-bin "Download" benchmark only.
const NUM_BUFS: usize = 16;

/// A UDP receive buffer.
pub struct RecvBuf([Vec<u8>; NUM_BUFS]);

impl Default for RecvBuf {
    fn default() -> Self {
        Self(array::from_fn(|_| vec![0; RECV_BUF_SIZE]))
    }
}

pub fn send_inner(
    state: &UdpSocketState,
    socket: quinn_udp::UdpSockRef<'_>,
    d: &datagram::Batch,
) -> io::Result<()> {
    let transmit = Transmit {
        destination: d.destination(),
        ecn: EcnCodepoint::from_bits(Into::<u8>::into(d.tos())),
        contents: d.data(),
        segment_size: Some(d.datagram_size().get()),
        src_ip: None,
    };

    match state.try_send(socket, &transmit) {
        Ok(()) => {}
        Err(e) if is_emsgsize(&e) => {
            debug!(
                "Failed to send datagram of size {} bytes, in {} segments, each {} bytes, from {} to {}. PMTUD probe? Ignoring error: {e}",
                d.data().len(),
                d.num_datagrams(),
                d.datagram_size().get(),
                d.source(),
                d.destination()
            );
            return Ok(());
        }
        Err(e) if is_enobufs(&e) => {
            // The send queue is momentarily full. Don't map to WouldBlock: the
            // socket IS writable, so edge-triggered epoll/kqueue won't re-signal
            // and the send loop would hang. Drop the packet; QUIC will retransmit.
            debug!("Interface send queue full (ENOBUFS), dropping packet: {e}");
            return Ok(());
        }
        e @ Err(_) => return e,
    }

    trace!(
        "sent {} bytes, in {} segments, each {} bytes, from {} to {} ",
        d.data().len(),
        d.num_datagrams(),
        d.datagram_size().get(),
        d.source(),
        d.destination(),
    );

    Ok(())
}

cfg_select! {
    unix => {
        fn is_emsgsize(e: &io::Error) -> bool {
            e.raw_os_error() == Some(libc::EMSGSIZE)
        }

        fn is_enobufs(e: &io::Error) -> bool {
            e.raw_os_error() == Some(libc::ENOBUFS)
        }
    }
    windows => {
        fn is_emsgsize(e: &io::Error) -> bool {
            // WSAEINVAL is returned when the Windows USO (UDP Segmentation Offload)
            // segment size exceeds the supported limit.
            matches!(e.raw_os_error(), Some(c) if c == WinSock::WSAEMSGSIZE.0 || c == WinSock::WSAEINVAL.0)
        }

        fn is_enobufs(e: &io::Error) -> bool {
            e.raw_os_error() == Some(WinSock::WSAENOBUFS.0)
        }
    }
    _ => {
        fn is_emsgsize(_: &io::Error) -> bool {
            false
        }

        fn is_enobufs(_: &io::Error) -> bool {
            false
        }
    }
}

#[cfg(unix)]
use std::os::fd::AsFd as SocketRef;
#[cfg(windows)]
use std::os::windows::io::AsSocket as SocketRef;

pub fn recv_inner<'a, S: SocketRef>(
    local_address: SocketAddr,
    state: &UdpSocketState,
    socket: S,
    recv_buf: &'a mut RecvBuf,
) -> Result<DatagramIter<'a>, io::Error> {
    let mut metas = [RecvMeta::default(); NUM_BUFS];
    let mut iovs = recv_buf.0.each_mut().map(|b| IoSliceMut::new(b));

    let n = state.recv((&socket).into(), &mut iovs, &mut metas)?;

    if tracing::enabled!(tracing::Level::TRACE) {
        for meta in metas.iter().take(n) {
            trace!(
                "received {} bytes, in {} segments, each {} bytes, from {} to {local_address}",
                meta.len,
                if meta.stride == 0 {
                    0
                } else {
                    meta.len.div_ceil(meta.stride)
                },
                meta.stride,
                meta.addr,
            );
        }
    }

    Ok(DatagramIter {
        current_buffer: None,
        remaining_buffers: metas.into_iter().zip(recv_buf.0.iter_mut()).take(n),
        local_address,
    })
}

pub struct DatagramIter<'a> {
    /// The current buffer, containing zero or more datagrams, each sharing the
    /// same [`RecvMeta`].
    current_buffer: Option<(RecvMeta, ChunksMut<'a, u8>)>,
    /// Remaining buffers, each containing zero or more datagrams, one
    /// [`RecvMeta`] per buffer.
    remaining_buffers:
        iter::Take<iter::Zip<array::IntoIter<RecvMeta, NUM_BUFS>, slice::IterMut<'a, Vec<u8>>>>,
    /// The local address of the UDP socket used to receive the datagrams.
    local_address: SocketAddr,
}

impl<'a> Iterator for DatagramIter<'a> {
    type Item = Datagram<&'a mut [u8]>;

    fn next(&mut self) -> Option<Self::Item> {
        loop {
            // Return the next datagram in the current buffer, if any.
            if let Some((meta, d)) = self
                .current_buffer
                .as_mut()
                .and_then(|(meta, ds)| ds.next().map(|d| (meta, d)))
            {
                return Some(Datagram::from_slice(
                    meta.addr,
                    self.local_address,
                    meta.ecn.map(|n| Tos::from(n as u8)).unwrap_or_default(),
                    d,
                ));
            }

            // There are no more datagrams in the current buffer. Try promoting
            // one of the remaining buffers, if any, to be the current buffer.
            let Some((meta, buf)) = self.remaining_buffers.next() else {
                // Handled all buffers. No more datagrams. Iterator is empty.
                return None;
            };

            // Ignore empty datagrams.
            if meta.len == 0 || meta.stride == 0 {
                debug!(
                    "ignoring empty datagram from {} to {} len {} stride {}",
                    meta.addr, self.local_address, meta.len, meta.stride
                );
                continue;
            }

            // Got another buffer. Let's chunk it into datagrams and return the
            // first datagram in the next loop iteration.
            self.current_buffer = Some((meta, buf[0..meta.len].chunks_mut(meta.stride)));
        }
    }
}

/// A wrapper around a UDP socket, sending and receiving [`Datagram`]s.
pub struct Socket<S> {
    state: UdpSocketState,
    inner: S,
}

impl<S: SocketRef> Socket<S> {
    /// Create a new [`Socket`] given a raw file descriptor managed externally.
    pub fn new(socket: S) -> Result<Self, io::Error> {
        let state = UdpSocketState::new((&socket).into())?;
        Ok(Self {
            state,
            inner: socket,
        })
    }

    /// Enable the Apple fast UDP datapath (`sendmsg_x`/`recvmsg_x`) for this
    /// socket.
    ///
    /// # Safety
    ///
    /// `sendmsg_x` and `recvmsg_x` are private Apple APIs. Quinn-udp resolves
    /// them at runtime via `dlsym` and falls back to standard `sendmsg`/`recvmsg`
    /// if they are unavailable, so this will not crash on unsupported OS versions.
    /// The `unsafe` contract is inherited from [`quinn_udp::UdpSocketState::set_apple_fast_path`].
    #[cfg(apple)]
    pub unsafe fn enable_apple_fast_path(&self) {
        // SAFETY: Caller ensures the APIs are available on this OS version.
        unsafe { self.state.set_apple_fast_path() }
    }

    /// Send a [`datagram::Batch`] on the given [`Socket`].
    pub fn send(&self, d: &datagram::Batch) -> io::Result<()> {
        send_inner(&self.state, (&self.inner).into(), d)
    }

    /// Returns the maximum number of GSO segments supported by this socket.
    pub fn max_gso_segments(&self) -> usize {
        self.state.max_gso_segments()
    }

    /// Receive a batch of [`Datagram`]s on the given [`Socket`], each
    /// set with the provided local address.
    pub fn recv<'a>(
        &self,
        local_address: SocketAddr,
        recv_buf: &'a mut RecvBuf,
    ) -> Result<DatagramIter<'a>, io::Error> {
        recv_inner(local_address, &self.state, &self.inner, recv_buf)
    }

    /// Whether transmitted datagrams might get fragmented by the IP layer
    ///
    /// Returns `false` on targets which employ e.g. the `IPV6_DONTFRAG` socket option.
    pub fn may_fragment(&self) -> bool {
        self.state.may_fragment()
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    #![allow(
        clippy::allow_attributes,
        clippy::unwrap_in_result,
        reason = "OK in tests."
    )]
    use std::{env, num::NonZeroUsize};

    use neqo_common::{Dscp, Ecn};

    use super::*;

    fn socket() -> Result<Socket<std::net::UdpSocket>, io::Error> {
        let socket = Socket::new(std::net::UdpSocket::bind("127.0.0.1:0")?)?;
        // Reverse non-blocking flag set by `UdpSocketState` to make the test non-racy.
        socket.inner.set_nonblocking(false)?;
        Ok(socket)
    }

    #[test]
    fn handle_empty_datagram() -> Result<(), io::Error> {
        // quinn-udp doesn't support sending emtpy datagrams across all
        // platforms. Use `std` socket instead.  See also
        // <https://github.com/quinn-rs/quinn/pull/2123>.
        let sender = std::net::UdpSocket::bind("127.0.0.1:0")?;
        let receiver = socket()?;
        let receiver_addr: SocketAddr = "127.0.0.1:0".parse().unwrap();

        sender.send_to(&[], receiver.inner.local_addr()?)?;
        let mut recv_buf = RecvBuf::default();
        let mut datagrams = receiver.recv(receiver_addr, &mut recv_buf)?;

        assert_eq!(datagrams.next(), None);

        Ok(())
    }

    #[test]
    fn datagram_tos() -> Result<(), io::Error> {
        let sender = socket()?;
        let receiver = socket()?;
        let receiver_addr: SocketAddr = "127.0.0.1:0".parse().unwrap();

        let datagram: datagram::Batch = Datagram::new(
            sender.inner.local_addr()?,
            receiver.inner.local_addr()?,
            Tos::from((Dscp::Le, Ecn::Ect1)),
            b"Hello, world!".to_vec(),
        )
        .into();

        sender.send(&datagram)?;

        let mut recv_buf = RecvBuf::default();
        let mut received_datagrams = receiver
            .recv(receiver_addr, &mut recv_buf)
            .expect("receive to succeed");

        // Assert that the ECN is correct.
        // On Android API level <= 25 the IPv4 `IP_TOS` control message is
        // not supported and thus ECN bits can not be received.
        // On NetBSD and OpenBSD, this also fails, but the cause has not been looked into.
        if cfg!(target_os = "android")
            && env::var("API_LEVEL")
                .ok()
                .and_then(|v| v.parse::<u32>().ok())
                .expect("API_LEVEL environment variable to be set on Android")
                <= 25
            || cfg!(any(target_os = "netbsd", target_os = "openbsd"))
        {
            assert_eq!(
                Ecn::default(),
                Ecn::from(received_datagrams.next().unwrap().tos())
            );
        } else {
            assert_eq!(
                Ecn::from(datagram.tos()),
                Ecn::from(received_datagrams.next().unwrap().tos())
            );
        }
        Ok(())
    }

    #[test]
    #[cfg(unix)]
    fn is_emsgsize_and_is_enobufs_are_disjoint() {
        let emsgsize = io::Error::from_raw_os_error(libc::EMSGSIZE);
        assert!(is_emsgsize(&emsgsize));
        assert!(!is_enobufs(&emsgsize));

        let enobufs = io::Error::from_raw_os_error(libc::ENOBUFS);
        assert!(!is_emsgsize(&enobufs));
        assert!(is_enobufs(&enobufs));

        let eagain = io::Error::from_raw_os_error(libc::EAGAIN);
        assert!(!is_emsgsize(&eagain));
        assert!(!is_enobufs(&eagain));
    }

    #[test]
    #[cfg(windows)]
    fn is_emsgsize_true_for_wsaemsgsize() {
        let err = io::Error::from_raw_os_error(WinSock::WSAEMSGSIZE.0);
        assert!(is_emsgsize(&err));
    }

    #[test]
    #[cfg(windows)]
    fn is_emsgsize_true_for_wsaeinval() {
        let err = io::Error::from_raw_os_error(WinSock::WSAEINVAL.0);
        assert!(is_emsgsize(&err));
    }

    #[test]
    #[cfg(windows)]
    fn is_emsgsize_false_for_other_windows_errors() {
        let err = io::Error::from_raw_os_error(WinSock::WSAEWOULDBLOCK.0);
        assert!(!is_emsgsize(&err));
    }

    #[test]
    #[cfg(windows)]
    fn is_enobufs_true_for_wsaenobufs() {
        let err = io::Error::from_raw_os_error(WinSock::WSAENOBUFS.0);
        assert!(is_enobufs(&err));
        assert!(!is_emsgsize(&err));
    }

    #[test]
    fn is_emsgsize_and_is_enobufs_false_for_non_os_error() {
        let err = io::Error::other("test error");
        assert!(!is_emsgsize(&err));
        assert!(!is_enobufs(&err));
    }

    #[test]
    fn max_gso_segments_returns_at_least_one() -> Result<(), io::Error> {
        let s = socket()?;
        assert!(s.max_gso_segments() >= 1);
        Ok(())
    }

    /// Expect [`Socket::recv`] to handle multiple [`Datagram`]s on GRO read.
    #[test]
    #[cfg_attr(
        not(any(target_os = "linux", target_os = "windows")),
        ignore = "GRO not available"
    )]
    fn many_datagrams_through_gso_gro() -> Result<(), io::Error> {
        const SEGMENT_SIZE: usize = 128;

        let sender = socket()?;
        let receiver = socket()?;
        let receiver_addr: SocketAddr = "127.0.0.1:0".parse().unwrap();

        let max_gso_segments = sender.max_gso_segments();
        let msg = vec![0xAB; SEGMENT_SIZE * max_gso_segments];
        let batch = datagram::Batch::new(
            sender.inner.local_addr()?,
            receiver.inner.local_addr()?,
            Tos::from((Dscp::Le, Ecn::Ect0)),
            NonZeroUsize::new(SEGMENT_SIZE).expect("SEGMENT_SIZE cannot be zero"),
            msg,
        );

        sender.send(&batch)?;

        // Allow for one GSO sendmsg to result in multiple GRO recvmmsg.
        let mut num_received = 0;
        let mut recv_buf = RecvBuf::default();
        while num_received < max_gso_segments {
            receiver
                .recv(receiver_addr, &mut recv_buf)
                .expect("receive to succeed")
                .for_each(|d| {
                    assert_eq!(
                        SEGMENT_SIZE,
                        d.len(),
                        "Expect received datagrams to have same length as sent datagrams"
                    );
                    num_received += 1;
                });
        }

        Ok(())
    }

    #[test]
    fn send_ignore_emsgsize() -> Result<(), io::Error> {
        let sender = socket()?;
        // Use non-blocking socket to test for `WouldBlock` error.
        let receiver = Socket::new(std::net::UdpSocket::bind("127.0.0.1:0")?)?;
        let receiver_addr: SocketAddr = "127.0.0.1:0".parse().unwrap();

        // Send oversized batch and expect `EMSGSIZE` error to be ignored.
        //
        // Use two segments to ensure quinn-udp's `effective_segment_size()` returns `Some`,
        // which sets `UDP_SEND_MSG_SIZE` on Windows. With a single segment,
        // `effective_segment_size()` returns `None`, and Windows silently truncates
        // the oversized datagram instead of returning `EMSGSIZE`.
        let segment_size = usize::from(u16::MAX) + 1;
        let oversized_batch = datagram::Batch::new(
            sender.inner.local_addr()?,
            receiver.inner.local_addr()?,
            Tos::from((Dscp::Le, Ecn::Ect1)),
            NonZeroUsize::new(segment_size).unwrap(),
            vec![0; segment_size * 2],
        );
        sender.send(&oversized_batch)?;

        let mut recv_buf = RecvBuf::default();
        match receiver.recv(receiver_addr, &mut recv_buf) {
            Ok(_) => panic!("Expected an error, but received datagrams"),
            Err(e) => assert_eq!(e.kind(), io::ErrorKind::WouldBlock),
        }

        // Now send a normal datagram to ensure that the socket is still usable.
        let normal_datagram = Datagram::new(
            sender.inner.local_addr()?,
            receiver.inner.local_addr()?,
            Tos::from((Dscp::Le, Ecn::Ect1)),
            b"Hello World!".to_vec(),
        )
        .into();
        sender.send(&normal_datagram)?;

        let mut recv_buf = RecvBuf::default();
        // Block until "Hello World!" is received.
        receiver.inner.set_nonblocking(false)?;
        let mut received_datagram = receiver.recv(receiver_addr, &mut recv_buf)?;
        assert_eq!(
            received_datagram.next().unwrap().as_ref(),
            normal_datagram.data()
        );

        Ok(())
    }

    #[test]
    #[cfg(apple)]
    fn apple_fast_path() -> Result<(), io::Error> {
        let socket = socket()?;
        // SAFETY: Tests run on Apple OS versions that support sendmsg_x/recvmsg_x.
        unsafe {
            socket.enable_apple_fast_path();
        }
        assert!(socket.max_gso_segments() > 1);
        Ok(())
    }

    #[test]
    fn may_fragment_returns_bool() -> Result<(), io::Error> {
        let s = socket()?;
        // On platforms that set DONTFRAG (Linux, macOS), this should be false.
        // On other platforms it may be true. Either way it must not panic.
        let frag = s.may_fragment();
        // On Linux and macOS, fragmentation is disabled via socket options.
        #[cfg(apple)]
        assert!(!frag, "may_fragment should be false on this platform");
        #[cfg(target_os = "linux")]
        assert!(!frag, "may_fragment should be false on Linux");
        #[cfg(not(any(apple, target_os = "linux")))]
        let _: bool = frag;
        Ok(())
    }

    #[test]
    fn max_gso_segments_is_consistent() -> Result<(), io::Error> {
        let s = socket()?;
        let a = s.max_gso_segments();
        let b = s.max_gso_segments();
        assert_eq!(a, b, "max_gso_segments should be deterministic");
        assert!(a >= 1);
        Ok(())
    }
}
