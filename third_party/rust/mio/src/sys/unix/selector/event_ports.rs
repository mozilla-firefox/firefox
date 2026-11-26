use std::collections::HashMap;
use std::ops::{Deref, DerefMut};
use std::os::fd::{AsFd, AsRawFd, BorrowedFd, FromRawFd, OwnedFd, RawFd};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::{cmp, io};

use crate::{Interest, Token};

/// Unique id for use as `SelectorId`.
#[cfg(debug_assertions)]
static NEXT_ID: AtomicUsize = AtomicUsize::new(1);

type EventMask = libc::c_int;

const READ_EVENTS: EventMask = libc::POLLIN as EventMask;
const WRITE_EVENTS: EventMask = libc::POLLOUT as EventMask;
const ERROR_EVENTS: EventMask = libc::POLLERR as EventMask | libc::POLLHUP as EventMask;

#[derive(Debug)]
pub struct Selector {
    #[cfg(debug_assertions)]
    id: usize,
    port: OwnedFd,
    state: Arc<SelectorState>,
    events: EventBuffer,
}

#[derive(Debug)]
struct EventBuffer {
    port_events: Vec<libc::port_event>,
    poll_fds: Vec<libc::pollfd>,
}

impl EventBuffer {
    fn new() -> EventBuffer {
        EventBuffer {
            port_events: Vec::new(),
            poll_fds: Vec::new(),
        }
    }
}

// SAFETY: `EventBuffer` is only scratch storage for `port_getn` and `poll`
// results and is only accessed through `Selector::select`, which requires
// `&mut self`.
unsafe impl Send for EventBuffer {}
unsafe impl Sync for EventBuffer {}

#[derive(Debug)]
struct SelectorState {
    fds: Mutex<HashMap<RawFd, FdData>>,
}

#[derive(Debug)]
struct FdData {
    token: Token,
    interests: Interest,
    generation: usize,
    associated: bool,
    needs_fallback: bool,
    fallback_writable: bool,
}

impl Selector {
    pub fn new() -> io::Result<Selector> {
        // SAFETY: `port_create(3C)` returns a valid fd on success.
        let port = unsafe { OwnedFd::from_raw_fd(syscall!(port_create())?) };
        syscall!(fcntl(port.as_raw_fd(), libc::F_SETFD, libc::FD_CLOEXEC))?;

        Ok(Selector {
            #[cfg(debug_assertions)]
            id: NEXT_ID.fetch_add(1, Ordering::Relaxed),
            port,
            events: EventBuffer::new(),
            state: Arc::new(SelectorState {
                fds: Mutex::new(HashMap::new()),
            }),
        })
    }

    pub fn try_clone(&self) -> io::Result<Selector> {
        self.port.try_clone().map(|port| Selector {
            // It's the same selector, so we use the same id.
            #[cfg(debug_assertions)]
            id: self.id,
            port,
            events: EventBuffer::new(),
            state: Arc::clone(&self.state),
        })
    }

    pub fn select(&mut self, events: &mut Events, timeout: Option<Duration>) -> io::Result<()> {
        self.state
            .select(self.port.as_raw_fd(), events, timeout, &mut self.events)
    }

    cfg_io_source! {
    #[allow(dead_code)]
    pub fn register(&self, fd: RawFd, token: Token, interests: Interest) -> io::Result<()> {
        self.register_internal(fd, token, interests).map(|_| ())
    }

    pub(crate) fn register_internal(
        &self,
        fd: RawFd,
        token: Token,
        interests: Interest,
    ) -> io::Result<Arc<RegistrationRecord>> {
        self.state
            .register(self.port.as_raw_fd(), fd, token, interests)
    }
    }

    cfg_any_os_ext! {
    pub fn reregister(&self, fd: RawFd, token: Token, interests: Interest) -> io::Result<()> {
        self.state.reregister(self.port.as_raw_fd(), fd, token, interests)
    }

    pub fn deregister(&self, fd: RawFd) -> io::Result<()> {
        self.state.deregister(self.port.as_raw_fd(), fd)
    }
    }

    pub fn wake(&self, token: Token) -> io::Result<()> {
        self.state.wake(self.port.as_raw_fd(), token)
    }
}

cfg_io_source! {
    impl Selector {
        #[cfg(debug_assertions)]
        pub fn id(&self) -> usize {
            self.id
        }
    }
}

impl SelectorState {
    fn select(
        &self,
        port: RawFd,
        events: &mut Events,
        timeout: Option<Duration>,
        event_buffer: &mut EventBuffer,
    ) -> io::Result<()> {
        events.clear();

        if events.capacity() == 0 {
            return Ok(());
        }

        event_buffer.port_events.clear();
        if event_buffer.port_events.capacity() < events.capacity() {
            event_buffer
                .port_events
                .reserve(events.capacity() - event_buffer.port_events.capacity());
        }

        let start = Instant::now();
        let mut attempted = false;

        loop {
            let current_timeout = if attempted {
                match timeout {
                    Some(timeout) => match timeout.checked_sub(start.elapsed()) {
                        Some(timeout) => Some(timeout),
                        None => return Ok(()),
                    },
                    None => None,
                }
            } else {
                timeout
            };
            let poll_fallback_on_timeout = current_timeout.is_none() && self.has_fallback_fds();
            let current_timeout = current_timeout.or_else(|| {
                if poll_fallback_on_timeout {
                    Some(Duration::ZERO)
                } else {
                    None
                }
            });
            let mut timeout = timeout_to_timespec(current_timeout);
            let timeout = timeout
                .as_mut()
                .map(|timeout| timeout as *mut _)
                .unwrap_or(std::ptr::null_mut());
            let mut nget = 1;
            event_buffer.port_events.clear();

            trace!("waiting for Solaris event port events");
            let res = syscall!(port_getn(
                port,
                event_buffer.port_events.as_mut_ptr(),
                event_buffer.port_events.capacity() as libc::c_uint,
                &mut nget,
                timeout,
            ));
            attempted = true;

            if let Err(err) = res {
                match err.raw_os_error() {
                    Some(libc::ETIME) => nget = 0,
                    _ => return Err(err),
                }
            }

            if nget == 0 {
                self.poll_registered_fds(events, poll_fallback_on_timeout)?;
                return Ok(());
            }

            trace!("Solaris event port returned {nget} events");
            // SAFETY: `port_getn` initialized exactly the first `nget` entries.
            unsafe { event_buffer.port_events.set_len(nget as usize) };
            self.process_events(port, event_buffer, events)?;
            if !events.is_empty() {
                return Ok(());
            }
        }
    }

    fn process_events(
        &self,
        port: RawFd,
        event_buffer: &mut EventBuffer,
        events: &mut Events,
    ) -> io::Result<()> {
        let mut fds = self.fds.lock().unwrap();
        let raw_events = event_buffer.port_events.as_slice();
        let poll_fds = &mut event_buffer.poll_fds;

        events.reserve(raw_events.len());
        poll_fds.clear();
        poll_fds.reserve(raw_events.len());
        for event in raw_events {
            if event.portev_source != libc::PORT_SOURCE_FD as libc::c_ushort {
                continue;
            }

            let fd = event.portev_object as RawFd;
            let Some(data) = fds.get(&fd) else {
                continue;
            };
            poll_fds.push(libc::pollfd {
                fd,
                events: interests_to_port(data.interests) as libc::c_short,
                revents: 0,
            });
        }

        if !poll_fds.is_empty() {
            syscall!(poll(
                poll_fds.as_mut_ptr(),
                poll_fds.len() as libc::nfds_t,
                0
            ))?;
        }

        let mut poll_fds = poll_fds.iter();
        for event in raw_events {
            if event.portev_source == libc::PORT_SOURCE_USER as libc::c_ushort {
                push_event(
                    events,
                    libc::PORT_SOURCE_USER as libc::c_ushort,
                    0,
                    Token(event.portev_user as usize),
                    event.portev_events,
                );
            } else if event.portev_source == libc::PORT_SOURCE_FD as libc::c_ushort {
                let fd = event.portev_object as RawFd;
                let generation = event.portev_user as usize;

                let Some(data) = fds.get_mut(&fd) else {
                    continue;
                };
                let current_interest_events = interests_to_port(data.interests);
                let poll_events = poll_fds.next().and_then(|poll_fd| {
                    debug_assert_eq!(poll_fd.fd, fd);
                    if poll_fd.revents == 0 {
                        None
                    } else {
                        Some(EventMask::from(poll_fd.revents))
                    }
                });
                let Some(poll_events) = poll_events else {
                    if event.portev_events & ERROR_EVENTS == 0 {
                        let generation = data.generation.wrapping_add(1).max(1);
                        associate(port, fd, data.interests, generation)?;
                        data.generation = generation;
                        data.associated = true;
                        data.needs_fallback = false;
                        data.fallback_writable = false;
                        continue;
                    }

                    let poll_events = event.portev_events;
                    if data.generation != generation && poll_events & current_interest_events == 0 {
                        let generation = data.generation.wrapping_add(1).max(1);
                        associate(port, fd, data.interests, generation)?;
                        data.generation = generation;
                        data.associated = true;
                        data.needs_fallback = false;
                        data.fallback_writable = false;
                        continue;
                    }

                    let generation = data.generation.wrapping_add(1).max(1);
                    associate(port, fd, data.interests, generation)?;
                    data.generation = generation;
                    data.associated = true;
                    data.needs_fallback = false;
                    data.fallback_writable = false;
                    push_event(
                        events,
                        libc::PORT_SOURCE_FD as libc::c_ushort,
                        fd as libc::uintptr_t,
                        data.token,
                        poll_events,
                    );
                    continue;
                };
                if data.generation != generation && poll_events & current_interest_events == 0 {
                    let generation = data.generation.wrapping_add(1).max(1);
                    associate(port, fd, data.interests, generation)?;
                    data.generation = generation;
                    data.associated = true;
                    data.needs_fallback = false;
                    data.fallback_writable = false;
                    continue;
                }

                // Event ports disassociate an fd after delivery. Re-associate
                // only interests that were not delivered, and use fallback
                // polling for delivered interests so sources that clear
                // readiness without reregistering can observe future events.
                let generation = data.generation.wrapping_add(1).max(1);
                let remaining_interests = data
                    .interests
                    .remove(interests_from_event(poll_events).unwrap_or(data.interests));
                if let Some(remaining_interests) = remaining_interests {
                    associate(port, fd, remaining_interests, generation)?;
                    data.associated = true;
                } else {
                    data.associated = false;
                }
                data.generation = generation;
                data.needs_fallback = true;
                data.fallback_writable = false;

                push_event(
                    events,
                    libc::PORT_SOURCE_FD as libc::c_ushort,
                    fd as libc::uintptr_t,
                    data.token,
                    poll_events,
                );
            }
        }

        Ok(())
    }

    fn poll_registered_fds(
        &self,
        events: &mut Events,
        include_writable_only: bool,
    ) -> io::Result<()> {
        let mut fds = self.fds.lock().unwrap();
        if fds.is_empty() {
            return Ok(());
        }

        let mut poll_fds = Vec::new();
        let mut tokens = Vec::new();
        let mut fallback_writable = Vec::new();
        for (&fd, data) in fds.iter() {
            if !data.needs_fallback {
                continue;
            }

            poll_fds.push(libc::pollfd {
                fd,
                events: interests_to_port(data.interests) as libc::c_short,
                revents: 0,
            });
            tokens.push(data.token);
            fallback_writable.push(data.fallback_writable);
        }

        if poll_fds.is_empty() {
            return Ok(());
        }

        syscall!(poll(
            poll_fds.as_mut_ptr(),
            poll_fds.len() as libc::nfds_t,
            0
        ))?;

        events.reserve(poll_fds.len());
        for ((poll_fd, token), fallback_writable) in
            poll_fds.iter().zip(tokens).zip(fallback_writable)
        {
            if poll_fd.revents != 0 {
                let revents = EventMask::from(poll_fd.revents);
                let writable_only = revents & !WRITE_EVENTS == 0;
                if include_writable_only || fallback_writable || !writable_only {
                    push_event(
                        events,
                        libc::PORT_SOURCE_FD as libc::c_ushort,
                        poll_fd.fd as libc::uintptr_t,
                        token,
                        revents,
                    );
                    if writable_only {
                        if let Some(data) = fds.get_mut(&poll_fd.fd) {
                            data.fallback_writable = false;
                        }
                    }
                }
            }
        }

        Ok(())
    }

    fn has_fallback_fds(&self) -> bool {
        self.fds
            .lock()
            .unwrap()
            .values()
            .any(|data| data.needs_fallback)
    }

    cfg_io_source! {
    fn register(
        &self,
        port: RawFd,
        fd: RawFd,
        token: Token,
        interests: Interest,
    ) -> io::Result<Arc<RegistrationRecord>> {
        let mut fds = self.fds.lock().unwrap();
        let record = Arc::new(RegistrationRecord::new());

        associate(port, fd, interests, 1)?;

        fds.insert(
            fd,
            FdData {
                token,
                interests,
                generation: 1,
                associated: true,
                needs_fallback: true,
                fallback_writable: true,
            },
        );
        Ok(record)
    }
    }

    cfg_any_os_ext! {
    fn reregister(
        &self,
        port: RawFd,
        fd: RawFd,
        token: Token,
        interests: Interest,
    ) -> io::Result<()> {
        let mut fds = self.fds.lock().unwrap();
        let data = fds.get_mut(&fd).ok_or(io::ErrorKind::NotFound)?;
        let generation = data.generation.wrapping_add(1).max(1);

        associate(port, fd, interests, generation)?;

        data.token = token;
        data.interests = interests;
        data.generation = generation;
        data.associated = true;
        data.needs_fallback = true;
        data.fallback_writable = true;
        Ok(())
    }

    fn deregister(&self, port: RawFd, fd: RawFd) -> io::Result<()> {
        let mut fds = self.fds.lock().unwrap();
        let data = fds.remove(&fd).ok_or(io::ErrorKind::NotFound)?;

        if data.associated {
            dissociate(port, fd)?;
        }
        Ok(())
    }
    }

    fn wake(&self, port: RawFd, token: Token) -> io::Result<()> {
        // `events` is controlled by Mio for user events. Solaris returns it
        // as `portev_events`, so use `POLLIN` to make waker events readable
        // like the other selector implementations.
        syscall!(port_send(
            port,
            libc::POLLIN as libc::c_int,
            usize::from(token) as *mut libc::c_void,
        ))
        .map(|_| ())
    }
}

fn push_event(
    events: &mut Events,
    source: libc::c_ushort,
    object: libc::uintptr_t,
    token: Token,
    event_mask: EventMask,
) {
    if let Some(event) = events
        .iter_mut()
        .find(|event| event.0.portev_user as usize == usize::from(token))
    {
        event.0.portev_events |= event_mask;
    } else {
        events.push(Event(libc::port_event {
            portev_events: event_mask,
            portev_source: source,
            portev_pad: 0,
            portev_object: object,
            portev_user: usize::from(token) as *mut libc::c_void,
        }));
    }
}

fn associate(port: RawFd, fd: RawFd, interests: Interest, generation: usize) -> io::Result<()> {
    syscall!(port_associate(
        port,
        libc::PORT_SOURCE_FD,
        fd as libc::uintptr_t,
        interests_to_port(interests),
        generation as *mut libc::c_void,
    ))
    .map(|_| ())
}

cfg_any_os_ext! {
fn dissociate(port: RawFd, fd: RawFd) -> io::Result<()> {
    syscall!(port_dissociate(port, libc::PORT_SOURCE_FD, fd as libc::uintptr_t))
        .map(|_| ())
        .or_else(|err| {
            if err.raw_os_error() == Some(libc::ENOENT) {
                Ok(())
            } else {
                Err(err)
            }
        })
}
}

fn timeout_to_timespec(timeout: Option<Duration>) -> Option<libc::timespec> {
    timeout.map(|timeout| libc::timespec {
        tv_sec: cmp::min(timeout.as_secs(), libc::time_t::MAX as u64) as libc::time_t,
        tv_nsec: libc::c_long::from(timeout.subsec_nanos() as i32),
    })
}

fn interests_to_port(interests: Interest) -> EventMask {
    let mut events = 0;

    if interests.is_readable() {
        events |= READ_EVENTS;
    }

    if interests.is_writable() {
        events |= WRITE_EVENTS;
    }

    events
}

fn interests_from_event(events: EventMask) -> Option<Interest> {
    let mut interests = None;

    if (events & READ_EVENTS) != 0 {
        interests = Some(Interest::READABLE);
    }

    if (events & WRITE_EVENTS) != 0 {
        interests = Some(match interests {
            Some(interests) => interests.add(Interest::WRITABLE),
            None => Interest::WRITABLE,
        });
    }

    interests
}

#[repr(transparent)]
#[derive(Clone)]
pub struct Event(libc::port_event);

unsafe impl Send for Event {}
unsafe impl Sync for Event {}

impl Deref for Event {
    type Target = libc::port_event;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for Event {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

pub struct Events(Vec<Event>);

impl Deref for Events {
    type Target = Vec<Event>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for Events {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl Events {
    pub fn with_capacity(capacity: usize) -> Events {
        Events(Vec::with_capacity(capacity))
    }
}

// `Events` cannot derive `Send` or `Sync` because of the
// `portev_user: *mut c_void` field in `libc::port_event`. Mio stores tokens in
// `portev_user`, and its event API only exposes those tokens and readiness
// flags without allowing shared mutation of the raw pointer field.
unsafe impl Send for Events {}
unsafe impl Sync for Events {}

pub mod event {
    use std::fmt;

    use crate::sys::Event;
    use crate::Token;

    use super::EventMask;

    pub fn token(event: &Event) -> Token {
        Token(event.0.portev_user as usize)
    }

    pub fn is_readable(event: &Event) -> bool {
        (event.0.portev_events & libc::POLLIN as EventMask) != 0
            || (event.0.portev_events & libc::POLLPRI as EventMask) != 0
    }

    pub fn is_writable(event: &Event) -> bool {
        (event.0.portev_events & libc::POLLOUT as EventMask) != 0
    }

    pub fn is_error(event: &Event) -> bool {
        (event.0.portev_events & libc::POLLERR as EventMask) != 0
    }

    pub fn is_read_closed(event: &Event) -> bool {
        (event.0.portev_events & libc::POLLHUP as EventMask) != 0
    }

    pub fn is_write_closed(event: &Event) -> bool {
        (event.0.portev_events & libc::POLLHUP as EventMask) != 0
            || ((event.0.portev_events & libc::POLLOUT as EventMask) != 0
                && (event.0.portev_events & libc::POLLERR as EventMask) != 0)
            || event.0.portev_events == libc::POLLERR as EventMask
    }

    pub fn is_priority(event: &Event) -> bool {
        (event.0.portev_events & libc::POLLPRI as EventMask) != 0
    }

    pub fn is_aio(_: &Event) -> bool {
        false
    }

    pub fn is_lio(_: &Event) -> bool {
        false
    }

    pub fn debug_details(f: &mut fmt::Formatter<'_>, event: &Event) -> fmt::Result {
        #[allow(clippy::trivially_copy_pass_by_ref)]
        fn check_events(got: &EventMask, want: &libc::c_short) -> bool {
            (*got & EventMask::from(*want)) != 0
        }
        debug_detail!(
            EventsDetails(EventMask),
            check_events,
            libc::POLLIN,
            libc::POLLPRI,
            libc::POLLOUT,
            libc::POLLRDNORM,
            libc::POLLRDBAND,
            libc::POLLWRNORM,
            libc::POLLWRBAND,
            libc::POLLERR,
            libc::POLLHUP,
        );

        f.debug_struct("port_event")
            .field("events", &EventsDetails(event.0.portev_events))
            .field("source", &event.0.portev_source)
            .field("object", &event.0.portev_object)
            .field("user", &event.0.portev_user)
            .finish()
    }
}

impl AsFd for Selector {
    fn as_fd(&self) -> BorrowedFd<'_> {
        self.port.as_fd()
    }
}

impl AsRawFd for Selector {
    fn as_raw_fd(&self) -> RawFd {
        self.port.as_raw_fd()
    }
}

cfg_io_source! {
    mod registered_io_source;
    pub(crate) use registered_io_source::IoSourceState;
    use registered_io_source::RegistrationRecord;
}
