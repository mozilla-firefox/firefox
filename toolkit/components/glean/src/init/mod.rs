// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::env;
use std::ffi::{c_char, CStr, CString};
use std::ops::DerefMut;
use std::path::PathBuf;

use firefox_on_glean::{metrics, pings};
use nserror::{nsresult, NS_ERROR_FAILURE};
use nsstring::{nsACString, nsCString, nsString};
use xpcom::interfaces::{
    mozILocaleService, nsIFile, nsIPrefService, nsIProperties, nsIXULAppInfo, nsIXULRuntime,
};
use xpcom::{RefPtr, XpCom};

use glean::{ClientInfoMetrics, Configuration, ConfigurationBuilder};

#[cfg(not(target_os = "android"))]
mod upload_pref;
#[cfg(not(target_os = "android"))]
mod user_activity;
mod viaduct_uploader;

#[cfg(not(target_os = "android"))]
use upload_pref::UploadPrefObserver;
#[cfg(not(target_os = "android"))]
use user_activity::UserActivityObserver;
use viaduct_uploader::ViaductUploader;

/// Project FOG's entry point.
///
/// This assembles client information and the Glean configuration and then initializes the global
/// Glean instance.
#[cfg(not(target_os = "android"))]
#[no_mangle]
pub extern "C" fn fog_init(
    data_path_override: &nsACString,
    app_id_override: &nsACString,
    disable_internal_pings: bool,
) -> nsresult {
    let upload_enabled = static_prefs::pref!("datareporting.healthreport.uploadEnabled");
    let recording_enabled = static_prefs::pref!("telemetry.fog.test.localhost_port") < 0;
    let uploader = Some(Box::new(ViaductUploader) as Box<dyn glean::net::PingUploader>);

    fog_init_internal(
        data_path_override,
        app_id_override,
        upload_enabled || recording_enabled,
        uploader,
        // Flipping it around, because no value = defaults to false,
        // so we take in `disable` but pass on `enable`.
        !disable_internal_pings,
    )
    .into()
}

/// Project FOG's entry point on Android.
///
/// This assembles client information and the Glean configuration and then initializes the global
/// Glean instance.
/// It always enables upload and set no uploader.
/// This should only be called in test scenarios.
/// In normal use Glean should be initialized and controlled by the Glean Kotlin SDK.
#[cfg(target_os = "android")]
#[no_mangle]
pub extern "C" fn fog_init(
    data_path_override: &nsACString,
    app_id_override: &nsACString,
    disable_internal_pings: bool,
) -> nsresult {
    // On Android always enable Glean upload.
    let upload_enabled = true;
    // Don't set up an uploader.
    let uploader = None;

    fog_init_internal(
        data_path_override,
        app_id_override,
        upload_enabled,
        uploader,
        !disable_internal_pings,
    )
    .into()
}

fn fog_init_internal(
    data_path_override: &nsACString,
    app_id_override: &nsACString,
    upload_enabled: bool,
    uploader: Option<Box<dyn glean::net::PingUploader>>,
    enable_internal_pings: bool,
) -> Result<(), nsresult> {
    let timer_id = metrics::fog::initializations.start();

    log::debug!("Initializing FOG.");

    setup_observers()?;

    let (mut conf, client_info) = build_configuration(data_path_override, app_id_override)?;

    conf.upload_enabled = upload_enabled;
    conf.uploader = uploader;
    conf.enable_internal_pings = enable_internal_pings;

    // If we're operating in automation without any specific source tags to set,
    // set the tag "automation" so any pings that escape don't clutter the tables.
    // See https://mozilla.github.io/glean/book/user/debugging/index.html#enabling-debugging-features-through-environment-variables
    if env::var("MOZ_AUTOMATION").is_ok() && env::var("GLEAN_SOURCE_TAGS").is_err() {
        log::info!("In automation, setting 'automation' source tag.");
        glean::set_source_tags(vec!["automation".to_string()]);
        log::info!("In automation, disabling MPS to avoid 4am issues.");
        conf.use_core_mps = false;
    }

    log::debug!("Configuration: {:#?}", conf);

    // Register all custom pings before we initialize.
    pings::register_pings(Some(&conf.application_id));

    glean::initialize(conf, client_info);

    metrics::fog::initializations.stop_and_accumulate(timer_id);

    Ok(())
}

fn build_configuration(
    data_path_override: &nsACString,
    app_id_override: &nsACString,
) -> Result<(Configuration, ClientInfoMetrics), nsresult> {
    let data_path_str = if data_path_override.is_empty() {
        get_data_path()?
    } else {
        data_path_override.to_utf8().to_string()
    };
    let data_path = PathBuf::from(&data_path_str);

    let (app_build, app_display_version, channel, locale) = get_app_info()?;

    let client_info = ClientInfoMetrics {
        app_build,
        app_display_version,
        channel: Some(channel),
        locale: Some(locale),
        os_version: None, // TODO: bug 2017277
    };
    log::debug!("Client Info: {:#?}", client_info);

    let application_id = if app_id_override.is_empty() {
        let app_id_guard = super::APP_ID.read().unwrap();
        if app_id_guard.is_empty() {
            "firefox.desktop".to_string()
        } else {
            app_id_guard.clone()
        }
    } else {
        app_id_override.to_utf8().to_string()
    };

    let localhost_port = static_prefs::pref!("telemetry.fog.test.localhost_port");
    let server = if localhost_port > 0 {
        format!("http://localhost:{}", localhost_port)
    } else if application_id == "thunderbird.desktop" {
        String::from("https://incoming.thunderbird.net")
    } else {
        String::from("https://incoming.telemetry.mozilla.org")
    };

    extern "C" {
        fn FOG_MaxPingLimit() -> u32;
    }

    // SAFETY NOTE: Safe because it returns a primitive by value.
    let pings_per_interval = unsafe { FOG_MaxPingLimit() };
    metrics::fog::max_pings_per_minute.set(pings_per_interval.into());

    let rate_limit = glean::PingRateLimit {
        seconds_per_interval: 60,
        pings_per_interval,
    };

    let builder = ConfigurationBuilder::new(false, data_path, application_id)
        .with_delay_ping_lifetime_io(true)
        .with_server_endpoint(server)
        .with_use_core_mps(true)
        .with_trim_data_to_registered_pings(true)
        .with_ping_schedule(pings::ping_schedule())
        .with_rate_limit(rate_limit);

    Ok((builder.build(), client_info))
}

#[cfg(not(target_os = "android"))]
fn setup_observers() -> Result<(), nsresult> {
    if let Err(e) = UploadPrefObserver::begin_observing() {
        log::error!(
            "Could not observe data upload pref. Abandoning FOG init due to {:?}",
            e
        );
        return Err(e);
    }

    if let Err(e) = UserActivityObserver::begin_observing() {
        log::error!(
            "Could not observe user activity. Abandoning FOG init due to {:?}",
            e
        );
        return Err(e);
    }

    Ok(())
}

#[cfg(target_os = "android")]
fn setup_observers() -> Result<(), nsresult> {
    // No observers are set up on Android.
    Ok(())
}

/// Construct and return the data_path from the profile dir, or return an error.
fn get_data_path() -> Result<String, nsresult> {
    let dir_svc: RefPtr<nsIProperties> = match xpcom::components::Directory::service() {
        Ok(ds) => ds,
        _ => return Err(NS_ERROR_FAILURE),
    };
    let mut profile_dir = xpcom::GetterAddrefs::<nsIFile>::new();
    unsafe {
        dir_svc
            .Get(
                cstr!("ProfD").as_ptr(),
                &nsIFile::IID,
                profile_dir.void_ptr(),
            )
            .to_result()?;
    }
    let profile_dir = profile_dir.refptr().ok_or(NS_ERROR_FAILURE)?;
    let mut profile_path = nsString::new();
    unsafe {
        (*profile_dir).GetPath(&mut *profile_path).to_result()?;
    }
    let profile_path = String::from_utf16(&profile_path[..]).map_err(|_| NS_ERROR_FAILURE)?;
    let data_path = profile_path + "/datareporting/glean";
    Ok(data_path)
}

/// Return a tuple of the build_id, app version, build channel, and locale.
/// If the XUL Runtime isn't a XULAppInfo (e.g. in xpcshell),
/// build_id will be "unknown".
/// Other problems result in an error being returned instead.
fn get_app_info() -> Result<(String, String, String, String), nsresult> {
    let xul: RefPtr<nsIXULRuntime> =
        xpcom::components::XULRuntime::service().map_err(|_| NS_ERROR_FAILURE)?;

    let pref_service: RefPtr<nsIPrefService> =
        xpcom::components::Preferences::service().map_err(|_| NS_ERROR_FAILURE)?;
    let locale_service: RefPtr<mozILocaleService> =
        xpcom::components::Locale::service().map_err(|_| NS_ERROR_FAILURE)?;
    let branch = xpcom::getter_addrefs(|p| {
        // Safe because:
        //  * `null` is explicitly allowed per documentation
        //  * `p` is a valid outparam guaranteed by `getter_addrefs`
        unsafe { pref_service.GetDefaultBranch(std::ptr::null(), p) }
    })?;
    let pref_name = CString::new("app.update.channel").map_err(|_| NS_ERROR_FAILURE)?;
    let mut channel = nsCString::new();
    // Safe because:
    //  * `branch` is non-null (otherwise `getter_addrefs` would've been `Err`
    //  * `pref_name` exists so a pointer to it is valid for the life of the function
    //  * `channel` exists so a pointer to it is valid, and it can be written to
    unsafe {
        if (*branch)
            .GetCharPref(pref_name.as_ptr(), channel.deref_mut() as *mut nsACString)
            .to_result()
            .is_err()
        {
            channel = "unknown".into();
        }
    }

    extern "C" {
        fn FOG_MozAppVersionDisplay() -> *const c_char;
    }
    // SAFETY: It's literally a quoted literal.
    let version = unsafe { CStr::from_ptr(FOG_MozAppVersionDisplay()) }
        .to_str()
        .map_err(|_| NS_ERROR_FAILURE)?;

    let app_info = match xul.query_interface::<nsIXULAppInfo>() {
        Some(ai) => ai,
        // In e.g. xpcshell the XULRuntime isn't XULAppInfo.
        // We still want to return sensible values so tests don't explode.
        _ => {
            return Ok((
                "unknown".to_owned(),
                version.to_string(),
                channel.to_string(),
                "unknown".to_owned(),
            ))
        }
    };

    let mut build_id = nsCString::new();
    unsafe {
        app_info.GetAppBuildID(&mut *build_id).to_result()?;
    }

    let mut locale = nsCString::new();
    unsafe {
        locale_service
            .GetAppLocaleAsBCP47(&mut *locale)
            .to_result()?;
    }

    Ok((
        build_id.to_string(),
        version.to_string(),
        channel.to_string(),
        locale.to_string(),
    ))
}

/// **TEST-ONLY METHOD**
/// Resets FOG and the underlying Glean SDK, clearing stores.
#[no_mangle]
pub extern "C" fn fog_test_reset(
    data_path_override: &nsACString,
    app_id_override: &nsACString,
) -> nsresult {
    fog_test_reset_internal(data_path_override, app_id_override).into()
}

/// **TEST-ONLY METHOD**
/// Shutdown FOG and the Glean SDK.
#[no_mangle]
pub extern "C" fn fog_test_shutdown() {
    glean::shutdown();
}

// Split out into its own function so I could use `?`
#[cfg(not(target_os = "android"))]
fn fog_test_reset_internal(
    data_path_override: &nsACString,
    app_id_override: &nsACString,
) -> Result<(), nsresult> {
    let (mut conf, client_info) = build_configuration(data_path_override, app_id_override)?;

    let upload_enabled = static_prefs::pref!("datareporting.healthreport.uploadEnabled");
    let recording_enabled = static_prefs::pref!("telemetry.fog.test.localhost_port") < 0;
    conf.upload_enabled = upload_enabled || recording_enabled;

    // Don't accidentally send "main" pings during tests.
    conf.use_core_mps = false;

    // I'd prefer to reuse the uploader, but it gets moved into Glean so we build anew.
    conf.uploader = Some(Box::new(ViaductUploader) as Box<dyn glean::net::PingUploader>);

    glean::test_reset_glean(conf, client_info, true);

    // Register all custom pings after we initialize.
    pings::register_pings(None);

    Ok(())
}

#[cfg(target_os = "android")]
fn fog_test_reset_internal(
    data_path_override: &nsACString,
    app_id_override: &nsACString,
) -> Result<(), nsresult> {
    let (mut conf, client_info) = build_configuration(data_path_override, app_id_override)?;

    // On Android always enable Glean upload.
    conf.upload_enabled = true;

    // Don't accidentally send "main" pings during tests.
    conf.use_core_mps = false;

    // Same as before, would prefer to reuse, but it gets moved into Glean so we build anew.
    conf.uploader = Some(Box::new(ViaductUploader) as Box<dyn glean::net::PingUploader>);

    glean::test_reset_glean(conf, client_info, true);
    Ok(())
}
