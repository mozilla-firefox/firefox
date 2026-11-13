// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use anyhow::Result;
use axum::http::StatusCode;
use serde::{Deserialize, Serialize};
use std::sync::RwLock;
use std::{
    collections::HashMap,
    net::{IpAddr, SocketAddr},
    sync::Arc,
};

use crate::config;

use anyhow::anyhow;
use axum::{
    extract::{Path, State},
    http,
    response::{Html, IntoResponse},
    routing::{get, post},
    Json, Router,
};
use serde_json::Value;
use tokio::net::TcpListener;
use tower_http::set_header::SetResponseHeaderLayer;
use tower_livereload::{LiveReloadLayer, Reloader};

fn create_app(livereload: LiveReloadLayer, state: Db) -> Router {
    Router::new()
        .route("/", get(index))
        .route("/style.css", get(style))
        .route("/script.js", get(script))
        .route("/post", post(post_handler))
        .route(
            "/buckets/{bucket}/collections/{collection}/records",
            get(rs),
        )
        .route(
            "/v2/buckets/{bucket}/collections/{collection}/records",
            get(rs),
        )
        .layer(livereload)
        .layer(SetResponseHeaderLayer::overriding(
            http::header::CACHE_CONTROL,
            http::HeaderValue::from_static("no-cache, no-store, must-revalidate"),
        ))
        .layer(SetResponseHeaderLayer::overriding(
            http::header::PRAGMA,
            http::HeaderValue::from_static("no-cache"),
        ))
        .layer(SetResponseHeaderLayer::overriding(
            http::header::EXPIRES,
            http::HeaderValue::from_static("0"),
        ))
        .with_state(state)
}

fn create_state(livereload: &LiveReloadLayer) -> Db {
    let reloader = livereload.reloader();
    Arc::new(RwLock::new(InMemoryDb::new(reloader)))
}

#[tokio::main]
pub(crate) async fn start_server() -> Result<bool> {
    let livereload = LiveReloadLayer::new();
    let state = create_state(&livereload);
    let app = create_app(livereload, state);

    let addr = get_address()?;
    eprintln!("Copy the address http://{}/ into your mobile browser", addr);

    let listener = TcpListener::bind(addr).await?;
    axum::serve(listener, app).await?;
    Ok(true)
}

pub(crate) fn post_deeplink(
    platform: &str,
    deeplink: &str,
    experiments: Option<&Value>,
) -> Result<bool> {
    let payload = StartAppPostPayload::new(platform, deeplink, experiments);
    let addr = get_address()?;
    let _ret = post_payload(&payload, &addr.to_string())?;
    Ok(true)
}

type Db = Arc<RwLock<InMemoryDb>>;

pub(crate) fn get_address() -> Result<SocketAddr> {
    let host = config::server_host();
    let port = config::server_port();

    let port = port
        .parse::<u16>()
        .map_err(|_| anyhow!("NIMBUS_CLI_SERVER_PORT must be numeric"))?;
    let host = host
        .parse::<IpAddr>()
        .map_err(|_| anyhow!("NIMBUS_CLI_SERVER_HOST must be an IP address"))?;

    Ok((host, port).into())
}

async fn index(State(db): State<Db>) -> Html<String> {
    let mut html =
        include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/index.html")).to_string();
    let li_template = include_str!(concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/assets/li-template.html"
    ));

    let state = db.write().unwrap();
    for p in ["android", "ios", "web"] {
        let ppat = format!("{{{p}}}");
        match state.url(p) {
            Some(url) => {
                let li = li_template.replace("{platform}", p).replace("{url}", url);
                html = html.replace(&ppat, &li);
            }
            _ => {
                html = html.replace(&ppat, "");
            }
        }
    }

    Html(html)
}

async fn style(State(_): State<Db>) -> &'static str {
    include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/style.css"))
}

async fn script(State(_): State<Db>) -> &'static str {
    include_str!(concat!(env!("CARGO_MANIFEST_DIR"), "/assets/script.js"))
}

async fn rs(
    State(db): State<Db>,
    Path((_bucket, _collection)): Path<(String, String)>,
) -> impl IntoResponse {
    let state = db.write().unwrap();

    let latest = state.latest();
    if let Some(latest) = latest {
        if let Some(e) = &latest.experiments {
            (StatusCode::OK, Json(e.clone()))
        } else {
            // The server's latest content has no experiments; e.g.
            // nimbus-cli open --pbpaste
            (StatusCode::NOT_MODIFIED, Json(Value::Null))
        }
    } else {
        // The server is up and running, but the first invocation of a --pbpaste
        // has not come in yet.
        (StatusCode::SERVICE_UNAVAILABLE, Json(Value::Null))
    }
}

async fn post_handler(
    State(db): State<Db>,
    Json(payload): Json<StartAppPostPayload>,
) -> impl IntoResponse {
    eprintln!("Updating {platform} URL", platform = payload.platform);
    let mut state = db.write().unwrap();
    state.update(payload);
    // This will be converted into a JSON response
    // with a status code of `201 Created`
    (StatusCode::CREATED, Json(()))
}

#[derive(Deserialize, Serialize)]
struct StartAppPostPayload {
    platform: String,
    url: String,
    experiments: Option<Value>,
}

impl StartAppPostPayload {
    fn new(platform: &str, url: &str, experiments: Option<&Value>) -> Self {
        Self {
            platform: platform.to_string(),
            url: url.to_string(),
            experiments: experiments.cloned(),
        }
    }
}

fn post_payload<T: Serialize>(payload: &T, addr: &str) -> Result<String> {
    let url = format!("http://{addr}/post");
    let body = serde_json::to_string(payload)?;
    let req = viaduct::Request::post(viaduct::parse_url(&url)?)
        .header("Content-type", "application/json; charset=UTF-8")?
        .header("accept", "application/json")?
        .body(body);
    let resp = req.send()?;

    Ok(resp.text().to_string())
}

struct InMemoryDb {
    reloader: Reloader,
    payloads: HashMap<String, StartAppPostPayload>,
    latest: Option<String>,
}

impl InMemoryDb {
    fn new(reloader: Reloader) -> Self {
        Self {
            reloader,
            payloads: Default::default(),
            latest: None,
        }
    }

    fn url(&self, platform: &str) -> Option<&str> {
        Some(self.payloads.get(platform)?.url.as_str())
    }

    fn update(&mut self, payload: StartAppPostPayload) {
        self.latest = Some(payload.platform.clone());
        self.payloads.insert(payload.platform.clone(), payload);
        self.reloader.reload();
    }

    fn latest(&self) -> Option<&StartAppPostPayload> {
        let key = self.latest.as_ref()?;
        self.payloads.get(key)
    }
}

#[cfg(test)]
mod tests {
    use serde_json::json;
    use tokio::sync::oneshot::Sender;

    use super::*;

    async fn start_test_server(port: u32) -> Result<(Db, Sender<()>)> {
        let livereload = LiveReloadLayer::new();
        let state = create_state(&livereload);

        let app = create_app(livereload, state.clone());
        let addr = format!("127.0.0.1:{port}");
        let listener = TcpListener::bind(addr).await?;
        let (tx, rx) = tokio::sync::oneshot::channel::<()>();
        tokio::spawn(async move {
            axum::serve(listener, app)
                .with_graceful_shutdown(async {
                    rx.await.ok();
                })
                .await
                .unwrap();
        });

        Ok((state, tx))
    }

    async fn get(port: u32, endpoint: &str) -> Result<String> {
        let url = format!("http://127.0.0.1:{port}{endpoint}");

        let response = reqwest::Client::new().get(url).send().await?;

        let body = response.bytes().await?;
        let s = std::str::from_utf8(&body)?;

        Ok(s.to_string())
    }

    async fn post_payload<T: Serialize>(payload: &T, addr: &str) -> Result<reqwest::Response> {
        let url = format!("http://{addr}/post");
        let body = serde_json::to_string(payload)?;
        Ok(reqwest::Client::new()
            .post(url)
            .header("accept", "application/json")
            .header("Content-type", "application/json; charset=UTF-8")
            .body(body)
            .send()
            .await?)
    }

    #[tokio::test]
    async fn test_smoke_test() -> Result<()> {
        let port = 1234;
        let (_db, tx) = start_test_server(port).await?;

        let s = get(port, "/").await?;
        assert!(s.contains("<html>"));

        let _ = tx.send(());
        Ok(())
    }

    #[tokio::test]
    async fn test_posting_platform_url() -> Result<()> {
        let port = 1235;
        let (db, tx) = start_test_server(port).await?;

        let platform = "android";
        let deeplink = "fenix-dev-test://open-now";

        let payload = StartAppPostPayload::new(platform, deeplink, None);
        let _ = post_payload(&payload, &format!("127.0.0.1:{port}")).await?;

        // Check the internal state
        let state = db.write().unwrap();
        let url = state.url(platform);
        assert_eq!(url, Some(deeplink));

        let _ = tx.send(());
        Ok(())
    }

    #[tokio::test]
    async fn test_posting_platform_url_from_index_page() -> Result<()> {
        let port = 1236;
        let (_, tx) = start_test_server(port).await?;

        let platform = "android";
        let deeplink = "fenix-dev-test://open-now";

        let payload = StartAppPostPayload::new(platform, deeplink, None);
        let _ = post_payload(&payload, &format!("127.0.0.1:{port}")).await?;

        // Check the index.html page
        let s = get(port, "/").await?;
        assert!(s.contains(deeplink));

        let _ = tx.send(());
        Ok(())
    }

    #[tokio::test]
    async fn test_posting_value_to_fake_remote_settings() -> Result<()> {
        let port = 1237;
        let (_, tx) = start_test_server(port).await?;

        let platform = "android";
        let deeplink = "fenix-dev-test://open-now";
        let value = json!({
            "int": 1,
            "boolean": true,
            "object": {},
            "array": [],
            "null": null,
        });
        let payload = StartAppPostPayload::new(platform, deeplink, Some(&value));
        let _ = post_payload(&payload, &format!("127.0.0.1:{port}")).await?;

        // Check the fake Remote Settings page
        let s = get(port, "/v2/buckets/BUCKET/collections/COLLECTION/records").await?;
        assert_eq!(s, serde_json::to_string(&value)?);

        let s = get(port, "/buckets/BUCKET/collections/COLLECTION/records").await?;
        assert_eq!(s, serde_json::to_string(&value)?);

        let _ = tx.send(());
        Ok(())
    }

    #[tokio::test]
    async fn test_getting_null_values_from_fake_remote_settings() -> Result<()> {
        let port = 1238;
        let (_, tx) = start_test_server(port).await?;

        // Part 1: get from remote settings page before anything has been posted yet.
        let s = get(port, "/v2/buckets/BUCKET/collections/COLLECTION/records").await?;
        assert_eq!(s, "null".to_string());

        // Part 2: Post a payload, but not with any experiments.
        let platform = "android";
        let deeplink = "fenix-dev-test://open-now";

        let payload = StartAppPostPayload::new(platform, deeplink, None);
        let _ = post_payload(&payload, &format!("127.0.0.1:{port}")).await?;

        // Check the fake Remote Settings page, should be empty, since an experiments payload
        // wasn't posted
        let s = get(port, "/v2/buckets/BUCKET/collections/COLLECTION/records").await?;
        assert_eq!(s, "".to_string());

        let _ = tx.send(());
        Ok(())
    }
}
