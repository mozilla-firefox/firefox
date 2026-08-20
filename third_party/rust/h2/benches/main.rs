use bytes::Bytes;
use h2::{
    client,
    server::{self, SendResponse},
    RecvStream,
};
use http::Request;

use std::{
    error::Error,
    future::Future,
    pin::Pin,
    task::{Context, Poll},
    time::{Duration, Instant},
};

use tokio::net::{TcpListener, TcpStream};

const NUM_REQUESTS_TO_SEND: usize = 100_000;
const WRITE_CONTENTION_STREAMS: usize = 512;
const WRITE_CONTENTION_CHUNKS_PER_STREAM: usize = 128;
const WRITE_CONTENTION_CHUNK_SIZE: usize = 16 * 1024;
const WRITE_CONTENTION_WINDOW: u32 = 16 * 1024 * 1024;
const WRITE_CONTENTION_MAX_FRAME_SIZE: u32 = 64 * 1024;
const WRITE_CONTENTION_MAX_SEND_BUFFER: usize = 8 * 1024 * 1024;
const WRITE_CONTENTION_WORKER_THREADS: usize = 4;

#[derive(Clone, Copy)]
struct WriteContentionConfig {
    streams: usize,
    chunks_per_stream: usize,
    chunk_size: usize,
}

impl WriteContentionConfig {
    fn from_env() -> Self {
        Self {
            streams: env_usize("H2_WRITE_CONTENTION_STREAMS", WRITE_CONTENTION_STREAMS),
            chunks_per_stream: env_usize(
                "H2_WRITE_CONTENTION_CHUNKS_PER_STREAM",
                WRITE_CONTENTION_CHUNKS_PER_STREAM,
            ),
            chunk_size: env_usize(
                "H2_WRITE_CONTENTION_CHUNK_SIZE",
                WRITE_CONTENTION_CHUNK_SIZE,
            ),
        }
    }

    fn bytes(&self) -> usize {
        self.streams * self.chunks_per_stream * self.chunk_size
    }
}

fn write_contention_worker_threads() -> usize {
    env_usize(
        "H2_WRITE_CONTENTION_WORKER_THREADS",
        WRITE_CONTENTION_WORKER_THREADS,
    )
}

fn env_usize(name: &str, default: usize) -> usize {
    std::env::var(name)
        .ok()
        .and_then(|value| value.parse().ok())
        .unwrap_or(default)
}

// The actual server.
async fn server(addr: &str) -> Result<(), Box<dyn Error + Send + Sync>> {
    let listener = TcpListener::bind(addr).await?;

    loop {
        if let Ok((socket, _peer_addr)) = listener.accept().await {
            tokio::spawn(async move {
                if let Err(e) = serve(socket).await {
                    println!("  -> err={:?}", e);
                }
            });
        }
    }
}

async fn serve(socket: TcpStream) -> Result<(), Box<dyn Error + Send + Sync>> {
    let mut connection = server::handshake(socket).await?;
    while let Some(result) = connection.accept().await {
        let (request, respond) = result?;
        tokio::spawn(async move {
            if let Err(e) = handle_request(request, respond).await {
                println!("error while handling request: {}", e);
            }
        });
    }
    Ok(())
}

async fn handle_request(
    mut request: Request<RecvStream>,
    mut respond: SendResponse<Bytes>,
) -> Result<(), Box<dyn Error + Send + Sync>> {
    let body = request.body_mut();
    while let Some(data) = body.data().await {
        let data = data?;
        let _ = body.flow_control().release_capacity(data.len());
    }
    let response = http::Response::new(());
    let mut send = respond.send_response(response, false)?;
    send.send_data(Bytes::from_static(b"pong"), true)?;

    Ok(())
}

// The benchmark
async fn send_requests(addr: &str) -> Result<(), Box<dyn Error>> {
    let tcp = loop {
        let Ok(tcp) = TcpStream::connect(addr).await else {
            continue;
        };
        break tcp;
    };
    let (client, h2) = client::handshake(tcp).await?;
    // Spawn a task to run the conn...
    tokio::spawn(async move {
        if let Err(e) = h2.await {
            println!("GOT ERR={:?}", e);
        }
    });

    let mut handles = Vec::with_capacity(NUM_REQUESTS_TO_SEND);
    for _i in 0..NUM_REQUESTS_TO_SEND {
        let mut client = client.clone();
        let task = tokio::spawn(async move {
            let request = Request::builder().body(()).unwrap();

            let instant = Instant::now();
            let (response, _) = client.send_request(request, true).unwrap();
            let response = response.await.unwrap();
            let mut body = response.into_body();
            while let Some(_chunk) = body.data().await {}
            instant.elapsed()
        });
        handles.push(task);
    }

    let instant = Instant::now();
    let mut result = Vec::with_capacity(NUM_REQUESTS_TO_SEND);
    for handle in handles {
        result.push(handle.await.unwrap());
    }
    let mut sum = Duration::new(0, 0);
    for r in result.iter() {
        sum = sum.checked_add(*r).unwrap();
    }

    println!("Overall: {}ms.", instant.elapsed().as_millis());
    println!("Fastest: {}ms", result.iter().min().unwrap().as_millis());
    println!("Slowest: {}ms", result.iter().max().unwrap().as_millis());
    println!(
        "Avg    : {}ms",
        sum.div_f64(NUM_REQUESTS_TO_SEND as f64).as_millis()
    );
    Ok(())
}

async fn write_contention_benchmark() -> Result<(), Box<dyn Error>> {
    let config = WriteContentionConfig::from_env();
    let listener = TcpListener::bind("127.0.0.1:0").await?;
    let addr = listener.local_addr()?;

    println!(
        "H2 write contention: {} streams x {} chunks x {}B = {:.1} MiB",
        config.streams,
        config.chunks_per_stream,
        config.chunk_size,
        config.bytes() as f64 / (1024.0 * 1024.0),
    );

    tokio::spawn(async move {
        let (socket, _peer_addr) = listener.accept().await.unwrap();
        if let Err(e) = serve_write_contention(socket, config).await {
            println!("write contention server error: {e:?}");
        }
    });

    let tcp = TcpStream::connect(addr).await?;
    let mut builder = client::Builder::new();
    builder
        .initial_window_size(WRITE_CONTENTION_WINDOW)
        .initial_connection_window_size(WRITE_CONTENTION_WINDOW)
        .max_frame_size(WRITE_CONTENTION_MAX_FRAME_SIZE)
        .max_concurrent_streams(config.streams as u32)
        .max_send_buffer_size(WRITE_CONTENTION_MAX_SEND_BUFFER);

    let (client, h2) = builder.handshake::<_, Bytes>(tcp).await?;
    tokio::spawn(async move {
        if let Err(e) = h2.await {
            println!("write contention client connection error: {e:?}");
        }
    });

    let mut handles = Vec::with_capacity(config.streams);
    let started = Instant::now();
    for _ in 0..config.streams {
        let client = client.clone();
        let expected = config.chunks_per_stream * config.chunk_size;
        handles.push(tokio::spawn(async move {
            let request = Request::builder().body(()).unwrap();
            let mut client = client.ready().await.unwrap();
            let (response, _) = client.send_request(request, true).unwrap();
            let response = response.await.unwrap();
            let mut body = response.into_body();
            let mut received = 0;

            while let Some(chunk) = body.data().await {
                let chunk = chunk.unwrap();
                received += chunk.len();
                let _ = body.flow_control().release_capacity(chunk.len());
            }

            assert_eq!(received, expected);
        }));
    }

    for handle in handles {
        handle.await.unwrap();
    }

    let elapsed = started.elapsed();
    let mib = config.bytes() as f64 / (1024.0 * 1024.0);
    println!("Overall: {}ms.", elapsed.as_millis());
    println!("Throughput: {:.1} MiB/s", mib / elapsed.as_secs_f64());

    Ok(())
}

async fn serve_write_contention(
    socket: TcpStream,
    config: WriteContentionConfig,
) -> Result<(), Box<dyn Error + Send + Sync>> {
    let mut builder = server::Builder::new();
    builder
        .initial_window_size(WRITE_CONTENTION_WINDOW)
        .initial_connection_window_size(WRITE_CONTENTION_WINDOW)
        .max_frame_size(WRITE_CONTENTION_MAX_FRAME_SIZE)
        .max_concurrent_streams(config.streams as u32)
        .max_send_buffer_size(WRITE_CONTENTION_MAX_SEND_BUFFER);

    let mut connection = builder.handshake(socket).await?;
    while let Some(result) = connection.accept().await {
        let (request, respond) = result?;
        tokio::spawn(async move {
            if let Err(e) = handle_write_contention_request(request, respond, config).await {
                println!("write contention request error: {e}");
            }
        });
    }

    Ok(())
}

async fn handle_write_contention_request(
    mut request: Request<RecvStream>,
    mut respond: SendResponse<Bytes>,
    config: WriteContentionConfig,
) -> Result<(), Box<dyn Error + Send + Sync>> {
    let body = request.body_mut();
    while let Some(data) = body.data().await {
        let data = data?;
        let _ = body.flow_control().release_capacity(data.len());
    }

    let response = http::Response::new(());
    let mut send = respond.send_response(response, false)?;
    let chunk = Bytes::from(vec![b'x'; config.chunk_size]);

    for idx in 0..config.chunks_per_stream {
        let end_of_stream = idx + 1 == config.chunks_per_stream;
        send_chunk(&mut send, chunk.clone(), end_of_stream).await?;
    }

    Ok(())
}

async fn send_chunk(
    send: &mut h2::SendStream<Bytes>,
    chunk: Bytes,
    end_of_stream: bool,
) -> Result<(), h2::Error> {
    let len = chunk.len();
    send.reserve_capacity(len);
    loop {
        if send.capacity() >= len {
            send.send_data(chunk, end_of_stream)?;
            return Ok(());
        }

        match (Capacity { send }).await {
            Some(Ok(_)) => {}
            Some(Err(err)) => return Err(err),
            None => return Err(h2::Reason::INTERNAL_ERROR.into()),
        }
    }
}

struct Capacity<'a> {
    send: &'a mut h2::SendStream<Bytes>,
}

impl Future for Capacity<'_> {
    type Output = Option<Result<usize, h2::Error>>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.send.poll_capacity(cx)
    }
}

fn main() {
    let _ = env_logger::try_init();
    let bench = std::env::var("H2_BENCH").unwrap_or_else(|_| "all".to_string());

    if bench == "write-contention" {
        let worker_threads = write_contention_worker_threads();
        println!("H2 write contention worker threads: {worker_threads}");
        let rt = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(worker_threads)
            .enable_all()
            .build()
            .unwrap();
        rt.block_on(write_contention_benchmark()).unwrap();
        return;
    }

    let addr = "127.0.0.1:5928";
    println!("H2 running in current-thread runtime at {addr}:");
    std::thread::spawn(|| {
        let rt = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .unwrap();
        rt.block_on(server(addr)).unwrap();
    });

    let rt = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .unwrap();
    rt.block_on(send_requests(addr)).unwrap();

    let addr = "127.0.0.1:5929";
    println!("H2 running in multi-thread runtime at {addr}:");
    std::thread::spawn(|| {
        let rt = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(4)
            .enable_all()
            .build()
            .unwrap();
        rt.block_on(server(addr)).unwrap();
    });

    let rt = tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .unwrap();
    rt.block_on(send_requests(addr)).unwrap();

    if bench == "all" {
        let worker_threads = write_contention_worker_threads();
        println!("H2 write contention worker threads: {worker_threads}");
        let rt = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(worker_threads)
            .enable_all()
            .build()
            .unwrap();
        rt.block_on(write_contention_benchmark()).unwrap();
    }
}
