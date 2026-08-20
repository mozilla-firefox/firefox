/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Viaduct hyper backend
//!
//! This implements a backend using `hyper`.
//! Unlike the `dev` backend it also supports HTTPS.
//!
//! In order to support HTTPS it depends on `hyper-tls`, which we don't want to vendor into moz-central at this time.
//! Because of that, only use this for crates that aren't members of the moz-central workspace like CLIs and the ios megazord.

use std::{sync::Arc, time::Duration};

use error_support::info;
use tokio::time::timeout;
use url::Url;

use viaduct::{
    error::MapBackendError, init_backend, Backend, ClientSettings, Header, Method, Request,
    Response, Result, ViaductError,
};

type Connector = hyper_tls::HttpsConnector<hyper::client::connect::HttpConnector>;
type Client = hyper::client::Client<Connector, hyper::Body>;

struct HyperBackend {
    runtime: tokio::runtime::Runtime,
    client: Client,
}

/// Set the viaduct backend to the `hyper`-based one with HTTPS support.
///
/// Named `viaduct_init_backend_hyper` since that reads better on iOS/Swift where there aren't any
/// namespaces.  Once we move to UniFII 0.31 we can use the renaming feature to do this instead.
#[uniffi::export]
pub fn viaduct_init_backend_hyper() {
    info!("initializing hyper backend");
    // Create a multi-threaded runtime, with 1 worker thread.
    //
    // This creates and manages a single worker thread.
    let runtime = tokio::runtime::Builder::new_multi_thread()
        .worker_threads(1)
        .enable_all()
        .build()
        .unwrap();
    let https_connector = hyper_tls::HttpsConnector::new();
    let client = hyper::Client::<()>::builder().build(https_connector);
    let backend = Arc::new(HyperBackend { runtime, client });
    init_backend(backend)
}

#[async_trait::async_trait]
impl Backend for HyperBackend {
    async fn send_request(&self, request: Request, settings: ClientSettings) -> Result<Response> {
        let handle = self.runtime.handle().clone();
        let client = self.client.clone();
        match handle
            .spawn(async move {
                let req_timeout = settings.timeout;
                let req = make_request_inner(client, request, settings);
                if req_timeout == 0 {
                    req.await
                } else {
                    let duration = Duration::from_millis(req_timeout.into());
                    timeout(duration, req)
                        .await
                        .unwrap_or_else(|_| Err(ViaductError::new_backend_error("Request timeout")))
                }
            })
            .await
        {
            Ok(result) => result,
            Err(e) => Err(ViaductError::new_backend_error(format!(
                "error spawning tokio task: {e}"
            ))),
        }
    }
}

/// Inner portion of `make_request()`
///
/// This expects to be run in a `tokio::spawn` closure
async fn make_request_inner(
    client: Client,
    mut request: Request,
    settings: ClientSettings,
) -> Result<Response> {
    let mut url = request.url.clone();
    // If the user-agent isn't set, try to get it from `GLOBAL_SETTINGS`.
    // This only affects consumers who aren't yet using `Client` to make their requests, since
    // `Client` already does this.  Once we move all consumers over, we can remove this code.
    if request.headers.get("User-Agent").is_none() {
        let user_agent = settings.user_agent.or_else(|| {
            viaduct::settings::GLOBAL_SETTINGS
                .read()
                .default_user_agent
                .clone()
        });
        if let Some(ua) = user_agent {
            request = request.header("User-Agent", ua)?;
        }
    }
    let mut resp = make_single_request(&client, request.clone()).await?;
    let mut redirect_count = 0;
    while resp.status().is_redirection() {
        redirect_count += 1;
        if settings.redirect_limit != 0 && redirect_count > settings.redirect_limit {
            return Err(ViaductError::new_backend_error("Too many redirections"));
        }
        let Some(location) = resp.headers().get("location") else {
            return Err(ViaductError::new_backend_error("location header missing"));
        };
        url = url.join(location.to_str().map_backend_error()?)?;
        let new_request = Request {
            method: request.method,
            url: url.clone(),
            headers: request.headers.clone(),
            body: None,
        };
        resp = make_single_request(&client, new_request).await?;
    }

    let status = resp.status().as_u16();

    let mut headers = Vec::new();
    for (name, value) in resp.headers() {
        let name = name.as_str().to_string();
        let value = String::from_utf8_lossy(value.as_bytes()).to_string();
        headers.push(Header::new(name, value)?);
    }
    Ok(Response {
        request_method: request.method,
        url,
        status,
        headers: headers.into(),
        body: resp.into_body(),
    })
}

async fn make_single_request(
    client: &Client,
    request: Request,
) -> Result<hyper::Response<Vec<u8>>> {
    let mut builder = hyper::Request::builder()
        .uri(convert_url(request.url)?)
        .method(match request.method {
            Method::Get => hyper::Method::GET,
            Method::Head => hyper::Method::HEAD,
            Method::Post => hyper::Method::POST,
            Method::Put => hyper::Method::PUT,
            Method::Delete => hyper::Method::DELETE,
            Method::Connect => hyper::Method::CONNECT,
            Method::Options => hyper::Method::OPTIONS,
            Method::Trace => hyper::Method::TRACE,
            Method::Patch => hyper::Method::PATCH,
        });
    for h in request.headers.into_vec() {
        let name = hyper::http::HeaderName::from_bytes(h.name.as_bytes()).map_backend_error()?;
        let value = hyper::http::HeaderValue::from_str(&h.value).map_backend_error()?;
        builder.headers_mut().unwrap().insert(name, value);
    }
    let req = builder
        .body(hyper::Body::from(request.body.unwrap_or_default()))
        .map_backend_error()?;
    let (parts, body) = client.request(req).await.map_backend_error()?.into_parts();
    let body = hyper::body::to_bytes(body).await.map_backend_error()?;
    Ok(hyper::Response::from_parts(parts, body.to_vec()))
}

fn convert_url(url: Url) -> Result<hyper::Uri> {
    hyper::Uri::builder()
        .scheme(url.scheme())
        .authority(url.authority())
        .path_and_query(match url.query() {
            None => url.path().to_string(),
            Some(query) => format!("{}?{}", url.path(), query),
        })
        .build()
        .map_backend_error()
}

uniffi::setup_scaffolding!();
