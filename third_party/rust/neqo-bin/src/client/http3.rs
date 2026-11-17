// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

//! An HTTP 3 client implementation.

use std::{
    cell::RefCell,
    collections::VecDeque,
    fmt::Display,
    fs::File,
    io::{BufWriter, Write as _},
    net::SocketAddr,
    num::NonZeroUsize,
    path::PathBuf,
    rc::Rc,
    time::Instant,
};

use http::Uri as Url;
use neqo_common::{Datagram, event::Provider, hex::Hex};
use neqo_http3::{Error, Http3Client, Http3ClientEvent, Http3Parameters, Http3State, Priority};
use neqo_transport::{
    AppError, CloseReason, Connection, EmptyConnectionIdGenerator, OutputBatch,
    RandomConnectionIdGenerator, StreamId,
};
use nss::{AuthenticationStatus, ResumptionToken};
use rustc_hash::FxHashMap as HashMap;
use tracing::{debug, error, info, warn};

use super::{Args, CloseState, Res, get_output_file, qlog_new};
use crate::{
    STREAM_IO_BUFFER_SIZE, now,
    send_data::{SendData, SendResult},
};

pub struct Handler {
    #[expect(clippy::struct_field_names, reason = "This name is more descriptive.")]
    url_handler: UrlHandler,
    token: Option<ResumptionToken>,
    read_buffer: Vec<u8>,
}

impl Handler {
    pub(crate) fn new(url_queue: VecDeque<Url>, args: Args) -> Self {
        let url_handler = UrlHandler {
            url_queue,
            handled_urls: Vec::new(),
            stream_handlers: HashMap::default(),
            all_paths: Vec::new(),
            args,
        };

        Self {
            url_handler,
            token: None,
            read_buffer: vec![0; STREAM_IO_BUFFER_SIZE],
        }
    }
}

pub fn create_client(
    args: &Args,
    local_addr: SocketAddr,
    remote_addr: SocketAddr,
    hostname: &str,
    resumption_token: Option<ResumptionToken>,
) -> Res<Http3Client> {
    let cid_generator: Rc<RefCell<dyn neqo_transport::ConnectionIdGenerator>> = if args.cid_len == 0
    {
        Rc::new(RefCell::new(EmptyConnectionIdGenerator::default()))
    } else {
        Rc::new(RefCell::new(RandomConnectionIdGenerator::new(
            args.cid_len.into(),
        )))
    };
    let mut transport = Connection::new_client(
        hostname,
        &[&args.shared.alpn],
        cid_generator,
        local_addr,
        remote_addr,
        args.shared.quic_parameters.get(args.shared.alpn.as_str()),
        now(),
    )?;
    let ciphers = args.get_ciphers();
    if !ciphers.is_empty() {
        transport.set_ciphers(&ciphers)?;
    }
    let mut client = Http3Client::new_with_conn(
        transport,
        Http3Parameters::default()
            .max_table_size_encoder(args.shared.max_table_size_encoder)
            .max_table_size_decoder(args.shared.max_table_size_decoder)
            .max_blocked_streams(args.shared.max_blocked_streams),
    );

    let qlog = qlog_new(args, hostname, client.connection_id())?;
    client.set_qlog(qlog);
    if let Some(ech) = &args.ech {
        client.enable_ech(&ech.0)?;
    }
    if let Some(token) = resumption_token {
        client.enable_resumption(now(), token)?;
    }

    Ok(client)
}

impl TryFrom<Http3State> for CloseState {
    type Error = CloseReason;

    fn try_from(value: Http3State) -> Result<Self, Self::Error> {
        let (state, error) = match value {
            Http3State::Closing(error) => (Self::Closing, error),
            Http3State::Closed(error) => (Self::Closed, error),
            _ => return Ok(Self::NotClosing),
        };

        if error.is_error() {
            Err(error)
        } else {
            Ok(state)
        }
    }
}

impl super::Client for Http3Client {
    const NO_ERROR: AppError = Error::HttpNone.code();

    fn is_closed(&self) -> Result<CloseState, CloseReason> {
        self.state().try_into()
    }

    fn process_multiple_output(
        &mut self,
        now: Instant,
        max_datagrams: NonZeroUsize,
    ) -> OutputBatch {
        self.process_multiple_output(now, max_datagrams)
    }

    fn process_multiple_input<'a>(
        &mut self,
        dgrams: impl IntoIterator<Item = Datagram<&'a mut [u8]>>,
        now: Instant,
    ) {
        self.process_multiple_input(dgrams, now);
    }

    fn close<S>(&mut self, now: Instant, app_error: AppError, msg: S)
    where
        S: AsRef<str> + Display,
    {
        self.close(now, app_error, msg);
    }

    fn stats(&self) -> neqo_transport::Stats {
        self.transport_stats()
    }

    fn has_events(&self) -> bool {
        Provider::has_events(self)
    }
}

impl super::Handler for Handler {
    type Client = Http3Client;

    fn handle(&mut self, client: &mut Http3Client) -> Res<bool> {
        while let Some(event) = client.next_event() {
            match event {
                Http3ClientEvent::AuthenticationNeeded => {
                    client.authenticated(AuthenticationStatus::Ok, now());
                }
                Http3ClientEvent::HeaderReady {
                    stream_id,
                    headers,
                    fin,
                    ..
                } => {
                    if self.url_handler.stream_handler(stream_id).is_some() {
                        debug!("READ HEADERS[{stream_id}]: fin={fin} {headers:?}");
                    } else {
                        warn!("Data on unexpected stream: {stream_id}");
                    }
                    if fin {
                        self.url_handler.on_stream_fin(client, stream_id);
                    }
                }
                Http3ClientEvent::DataReadable { stream_id } => {
                    let mut stream_done = false;
                    match self.url_handler.stream_handler(stream_id) {
                        None => {
                            warn!("Data on unexpected stream: {stream_id}");
                        }
                        Some(handler) => loop {
                            let (sz, fin) =
                                client.read_data(now(), stream_id, &mut self.read_buffer)?;

                            handler.process_data_readable(
                                stream_id,
                                fin,
                                &self.read_buffer[..sz],
                            )?;

                            if fin {
                                stream_done = true;
                                break;
                            }

                            if sz == 0 {
                                break;
                            }
                        },
                    }

                    if stream_done {
                        self.url_handler.on_stream_fin(client, stream_id);
                    }
                }
                Http3ClientEvent::DataWritable { stream_id } => {
                    match self.url_handler.stream_handler(stream_id) {
                        None => {
                            warn!("Data on unexpected stream: {stream_id}");
                        }
                        Some(handler) => {
                            handler.process_data_writable(client, stream_id, now());
                        }
                    }
                }
                Http3ClientEvent::StateChange(Http3State::Connected)
                | Http3ClientEvent::RequestsCreatable => {
                    info!("{event:?}");
                    self.url_handler.process_urls(client);
                }
                Http3ClientEvent::ZeroRttRejected => {
                    info!("{event:?}");
                    // All 0-RTT data was rejected. We need to retransmit it.
                    self.url_handler.reinit();
                    self.url_handler.process_urls(client);
                }
                Http3ClientEvent::ResumptionToken(t) => self.token = Some(t),
                _ => {
                    warn!("Unhandled event {event:?}");
                }
            }
        }

        // Fallback in case the connection closes before NEW_TOKEN arrives.
        let args = &self.url_handler.args;
        if (args.resume || args.save_token.is_some()) && self.token.is_none() {
            self.token = client.take_resumption_token(now());
        }

        Ok(self.url_handler.done())
    }

    fn take_token(&mut self) -> Option<ResumptionToken> {
        self.token.take()
    }
}

trait StreamHandler {
    fn process_data_readable(&mut self, stream_id: StreamId, fin: bool, data: &[u8]) -> Res<()>;
    fn process_data_writable(
        &mut self,
        client: &mut Http3Client,
        stream_id: StreamId,
        now: Instant,
    );
}

struct DownloadStreamHandler {
    out_file: Option<BufWriter<File>>,
    output_read_data: bool,
}

impl StreamHandler for DownloadStreamHandler {
    fn process_data_readable(&mut self, stream_id: StreamId, fin: bool, data: &[u8]) -> Res<()> {
        if let Some(out_file) = &mut self.out_file {
            out_file.write_all(data)?;
        } else if tracing::enabled!(tracing::Level::DEBUG) {
            if !self.output_read_data {
                debug!("READ[{stream_id}]: {} bytes", data.len());
            } else if let Ok(txt) = str::from_utf8(data) {
                debug!("READ[{stream_id}]: {txt}");
            } else {
                debug!("READ[{stream_id}]: 0x{}", Hex::new(data));
            }
        }

        if fin {
            self.out_file.take().map_or_else(
                || {
                    debug!("<FIN[{stream_id}]>");
                    Ok(())
                },
                |mut out_file| out_file.flush(),
            )?;
        }

        Ok(())
    }

    fn process_data_writable(
        &mut self,
        _client: &mut Http3Client,
        _stream_id: StreamId,
        _now: Instant,
    ) {
    }
}

struct UploadStreamHandler {
    data: SendData,
    start: Instant,
}

impl StreamHandler for UploadStreamHandler {
    fn process_data_readable(&mut self, stream_id: StreamId, _fin: bool, data: &[u8]) -> Res<()> {
        if let Ok(txt) = str::from_utf8(data) {
            let trimmed_txt = txt.trim_end_matches(char::from(0));
            let parsed: usize = trimmed_txt.parse().map_err(|_| Error::InvalidInput)?;
            if parsed == self.data.len() {
                info!(
                    "Stream ID: {stream_id:?}, Upload time: {:?}",
                    now().duration_since(self.start)
                );
            }
            Ok(())
        } else {
            error!("Unexpected data [{stream_id}]: 0x{}", Hex::new(data));
            Err(crate::client::Error::Http3(Error::InvalidInput))
        }
    }

    fn process_data_writable(
        &mut self,
        client: &mut Http3Client,
        stream_id: StreamId,
        now: Instant,
    ) {
        match self
            .data
            .send(|chunk| client.send_data(stream_id, chunk, now))
        {
            SendResult::StreamClosed => warn!("Stream {stream_id} is closed"),
            // Stream may be closed; ignore errors.
            SendResult::Done => _ = client.stream_close_send(stream_id, now),
            SendResult::MoreData => {}
        }
    }
}

struct UrlHandler {
    url_queue: VecDeque<Url>,
    handled_urls: Vec<Url>,
    stream_handlers: HashMap<StreamId, Box<dyn StreamHandler>>,
    all_paths: Vec<PathBuf>,
    args: Args,
}

impl UrlHandler {
    fn stream_handler(&mut self, stream_id: StreamId) -> Option<&mut Box<dyn StreamHandler>> {
        self.stream_handlers.get_mut(&stream_id)
    }

    fn process_urls(&mut self, client: &mut Http3Client) {
        loop {
            if self.url_queue.is_empty() {
                break;
            }
            if self.stream_handlers.len() >= self.args.concurrency {
                break;
            }
            if !self.next_url(client) {
                break;
            }
        }
    }

    fn next_url(&mut self, client: &mut Http3Client) -> bool {
        let url = self
            .url_queue
            .pop_front()
            .expect("download_next called with empty queue");
        let now = now();
        match client.fetch(
            now,
            &self.args.method,
            &url,
            &self.args.headers,
            Priority::default(),
        ) {
            Ok(client_stream_id) => {
                debug!("Successfully created stream id {client_stream_id} for {url}");

                let handler: Box<dyn StreamHandler> = match self.args.method.as_str() {
                    "GET" => {
                        let out_file = get_output_file(
                            &url,
                            self.args.output_dir.as_ref(),
                            &mut self.all_paths,
                        );
                        _ = client.stream_close_send(client_stream_id, now); // Stream may be closed; ignore errors.
                        Box::new(DownloadStreamHandler {
                            out_file,
                            output_read_data: self.args.output_read_data,
                        })
                    }
                    "POST" => Box::new(UploadStreamHandler {
                        data: SendData::zeroes(self.args.upload_size),
                        start: now,
                    }),
                    _ => unimplemented!(),
                };

                self.stream_handlers.insert(client_stream_id, handler);
                self.handled_urls.push(url);
                true
            }
            Err(Error::StreamLimit | Error::Unavailable) => {
                self.url_queue.push_front(url);
                false
            }
            Err(e) => {
                panic!("Can't create stream {e}");
            }
        }
    }

    fn done(&self) -> bool {
        self.stream_handlers.is_empty() && self.url_queue.is_empty()
    }

    fn on_stream_fin(&mut self, client: &mut Http3Client, stream_id: StreamId) {
        self.stream_handlers.remove(&stream_id);
        self.process_urls(client);
    }

    fn reinit(&mut self) {
        for url in self.handled_urls.drain(..).rev() {
            self.url_queue.push_front(url);
        }
        self.stream_handlers.clear();
        self.all_paths.clear();
    }
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use std::fs;

    use super::*;
    use crate::{client::Client, temp_dir::TempDir};

    /// RFC 9114, Section 8.1: HTTP/3 closes with `H3_NO_ERROR`; hq-interop keeps 0.
    #[test]
    fn no_error_codes() {
        assert_eq!(<Http3Client as Client>::NO_ERROR, 0x100);
        assert_eq!(<Connection as Client>::NO_ERROR, 0);
    }

    /// A `fin` data event must make the download durable and release the handle immediately.
    #[test]
    fn download_stream_handler_flushes_on_fin() {
        let dir = TempDir::new();
        let path = dir.path().join("download");
        let mut handler = DownloadStreamHandler {
            out_file: Some(BufWriter::new(File::create(&path).unwrap())),
            output_read_data: false,
        };

        handler
            .process_data_readable(StreamId::new(0), false, b"hello")
            .unwrap();
        assert!(fs::read(&path).unwrap().is_empty(), "still buffered");

        handler
            .process_data_readable(StreamId::new(0), true, b"")
            .unwrap();

        assert_eq!(fs::read(&path).unwrap(), b"hello");
        assert!(handler.out_file.is_none(), "fin consumes the file handle");
    }

    #[test]
    fn get_output_file_stays_in_output_dir() {
        let dir = TempDir::new();
        let out_dir = dir.path().join("out");
        let outside = dir.path().join("outside");
        let absolute = outside.to_str().unwrap().replace('\\', "/");
        for url in [
            "http://example.com/../outside".to_string(),
            format!("http://example.com/{absolute}"),
        ] {
            let url = url.parse::<Url>().unwrap();
            let file = get_output_file(&url, Some(&out_dir), &mut Vec::new());
            assert!(file.is_none(), "{url}");
        }
        assert!(!outside.exists());

        let url = "http://example.com/a/b".parse::<Url>().unwrap();
        assert!(get_output_file(&url, Some(&out_dir), &mut Vec::new()).is_some());
        assert!(out_dir.join("a").join("b").exists());
    }
}
