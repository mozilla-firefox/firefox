// -*- indent-tabs-mode: nil; js-indent-level: 2 -*-
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// The prefs in this file are shipped with the GRE and should apply to all
// embedding situations. Application-specific preferences belong somewhere
// else, such as browser/app/profile/firefox.js or
// mobile/android/app/geckoview-prefs.js.
//
// NOTE: Not all prefs should be defined in this (or any other) data file.
// Static prefs are defined in StaticPrefList.yaml. Those prefs should *not*
// appear in this file.
//
// For the syntax used by this file, consult the comments at the top of
// modules/libpref/parser/src/lib.rs.
//
// Please indent all prefs defined within #ifdef/#ifndef conditions. This
// improves readability, particular for conditional blocks that exceed a single
// screen.

pref("security.tls.insecure_fallback_hosts", "");

pref("security.default_personal_cert",   "Ask Every Time");

// This preference controls what signature algorithms are accepted for signed
// apps (i.e. add-ons). The number is interpreted as a bit mask with the
// following semantic:
// The lowest order bit determines which PKCS#7 algorithms are accepted.
// xxx_0_: SHA-1 and/or SHA-256 PKCS#7 allowed
// xxx_1_: SHA-256 PKCS#7 allowed
// The next two bits determine whether COSE is required and PKCS#7 is allowed
// x_00_x: COSE disabled, ignore files, PKCS#7 must verify
// x_01_x: COSE is verified if present, PKCS#7 must verify
// x_10_x: COSE is required, PKCS#7 must verify if present
// x_11_x: COSE is required, PKCS#7 disabled (fail when present)
pref("security.signed_app_signatures.policy", 2);

pref("security.xfocsp.errorReporting.enabled", true);
pref("security.xfocsp.errorReporting.automatic", false);

// Issuer we use to detect MitM proxies. Set to the issuer of the cert of the
// Firefox update service. The string format is whatever NSS uses to print a DN.
// This value is set and cleared automatically.
pref("security.pki.mitm_canary_issuer", "");
// Pref to disable the MitM proxy checks.
pref("security.pki.mitm_canary_issuer.enabled", true);

// It is set to true when a non-built-in root certificate is detected on a
// Firefox update service's connection.
// This value is set automatically.
// The difference between security.pki.mitm_canary_issuer and this pref is that
// here the root is trusted but not a built-in, whereas for
// security.pki.mitm_canary_issuer.enabled, the root is not trusted.
pref("security.pki.mitm_detected", false);

// Intermediate CA Preloading settings
pref("security.remote_settings.intermediates.enabled", true);
pref("security.remote_settings.intermediates.downloads_per_poll", 5000);
pref("security.remote_settings.intermediates.parallel_downloads", 8);

#if !defined(MOZ_WIDGET_ANDROID)
  pref("security.remote_settings.crlite_filters.enabled", true);
#else
  pref("security.remote_settings.crlite_filters.enabled", false);
#endif

pref("security.osreauthenticator.blank_password", false);
pref("security.osreauthenticator.password_last_changed_lo", 0);
pref("security.osreauthenticator.password_last_changed_hi", 0);

pref("security.crash_tracking.js_load_1.prevCrashes", 0);
pref("security.crash_tracking.js_load_1.maxCrashes", 1);

pref("general.useragent.compatMode.firefox", false);

pref("general.config.obscure_value", 13); // for MCD .cfg files

#ifndef MOZ_BUILD_APP_IS_BROWSER
pref("general.warnOnAboutConfig", true);
#endif

// Whether middle button click with a modifier key starts to autoscroll or
// does nothing.
pref("general.autoscroll.prevent_to_start.shiftKey", true); // Shift
pref("general.autoscroll.prevent_to_start.ctrlKey", false); // Control
pref("general.autoscroll.prevent_to_start.altKey", false);  // Alt
// Command on macOS, Windows key on Windows or Super key on Linux
pref("general.autoscroll.prevent_to_start.metaKey", false);

// When this pref is set to true, middle click on non-editable content keeps
// selected range rather than collapsing selection at the clicked position.
// This behavior is incompatible with Chrome, so enabling this could cause
// breaking some web apps.
// Note that this pref is ignored when "general.autoScroll" is set to false
// or "middlemouse.paste" is set to true.  For the former case, there is no
// reason do be incompatible with Chrome.  For the latter case, the selection
// change is important for "paste" event listeners even if it's non-editable
// content.
pref("general.autoscroll.prevent_to_collapse_selection_by_middle_mouse_down", false);

// maximum number of dated backups to keep at any time
pref("browser.bookmarks.max_backups",       5);

pref("browser.cache.disk_cache_ssl",        true);
// The half life used to re-compute cache entries frecency in hours.
pref("browser.cache.frecency_half_life_hours", 6);

// Don't show "Open with" option on download dialog if true.
pref("browser.download.forbid_open_with", false);

// Enable indexedDB logging.
pref("dom.indexedDB.logging.enabled", true);
// Detailed output in log messages.
pref("dom.indexedDB.logging.details", true);
// Enable profiler marks for indexedDB events.
pref("dom.indexedDB.logging.profiler-marks", false);

// The number of workers per domain allowed to run concurrently.
// We're going for effectively infinite, while preventing abuse.
pref("dom.workers.maxPerDomain", 512);

// The amount of time (milliseconds) service workers keep running after each event.
pref("dom.serviceWorkers.idle_timeout", 30000);

// The amount of time (milliseconds) service workers can be kept running using waitUntil promises
// or executing "long-running" JS after the "idle_timeout" period has expired.
pref("dom.serviceWorkers.idle_extended_timeout", 30000);

// The amount of time (milliseconds) an update request is delayed when triggered
// by a service worker that doesn't control any clients.
pref("dom.serviceWorkers.update_delay", 1000);

// Enable test for 24 hours update, service workers will always treat last update check time is over 24 hours
pref("dom.serviceWorkers.testUpdateOverOneDay", false);

// Blacklist of domains of web apps which are not aware of strict keypress
// dispatching behavior.  This is comma separated list.  If you need to match
// all sub-domains, you can specify it as "*.example.com".  Additionally, you
// can limit the path.  E.g., "example.com/foo" means "example.com/foo*".  So,
// if you need to limit under a directory, the path should end with "/" like
// "example.com/foo/".  Note that this cannot limit port number for now.
pref("dom.keyboardevent.keypress.hack.dispatch_non_printable_keys", "www.icloud.com");
// Pref for end-users and policy to add additional values.
pref("dom.keyboardevent.keypress.hack.dispatch_non_printable_keys.addl", "");

// Blacklist of domains of web apps which handle keyCode and charCode of
// keypress events with a path only for Firefox (i.e., broken if we set
// non-zero keyCode or charCode value to the other).  The format is exactly
// same as "dom.keyboardevent.keypress.hack.dispatch_non_printable_keys". So,
// check its explanation for the detail.
pref("dom.keyboardevent.keypress.hack.use_legacy_keycode_and_charcode", "*.collabserv.com,*.gov.online.office365.us,*.officeapps-df.live.com,*.officeapps.live.com,*.online.office.de,*.partner.officewebapps.cn,*.scniris.com");
// Pref for end-users and policy to add additional values.
pref("dom.keyboardevent.keypress.hack.use_legacy_keycode_and_charcode.addl", "");

// Text recognition is a platform dependent feature, so even if this preference is
// enabled here, the feature may not be visible in all browsers.
pref("dom.text-recognition.enabled", true);

// Fastback caching - if this pref is negative, then we calculate the number
// of content viewers to cache based on the amount of available memory.
pref("browser.sessionhistory.max_total_viewers", -1);

// See http://whatwg.org/specs/web-apps/current-work/#ping
pref("browser.send_pings", false);
pref("browser.send_pings.max_per_link", 1);           // limit the number of pings that are sent per link click
pref("browser.send_pings.require_same_host", false);  // only send pings to the same host if this is true

pref("browser.helperApps.neverAsk.saveToDisk", "");
pref("browser.helperApps.neverAsk.openFile", "");
pref("browser.helperApps.deleteTempFileOnExit", false);

pref("browser.triple_click_selects_paragraph", true);

// Disable support for MathML
pref("mathml.disabled",    false);

// Enable scale transform for stretchy MathML operators. See bug 414277.
pref("mathml.scale_stretchy_operators.enabled", true);

// We'll throttle the download if the download rate is throttle-factor times
// the estimated playback rate, AND we satisfy the cache readahead_limit
// above. The estimated playback rate is time_duration/length_in_bytes.
// This means we'll only throttle the download if there's no concern that
// throttling would cause us to stop and buffer.
pref("media.throttle-factor", 2);

// Master HTML5 media volume scale.
pref("media.volume_scale", "1.0");

// Whether we should play videos opened in a "video document", i.e. videos
// opened as top-level documents, as opposed to inside a media element.
pref("media.play-stand-alone", true);

#ifdef MOZ_WMF
  pref("media.wmf.dxva.enabled", true);
  pref("media.wmf.play-stand-alone", true);
#endif

// GMP storage version number. At startup we check the version against
// media.gmp.storage.version.observed, and if the versions don't match,
// we clear storage and set media.gmp.storage.version.observed=expected.
// This provides a mechanism to clear GMP storage when non-compatible
// changes are made.
pref("media.gmp.storage.version.expected", 1);

// Filter what triggers user notifications.
// See DecoderDoctorDocumentWatcher::ReportAnalysis for details.
#ifdef NIGHTLY_BUILD
  pref("media.decoder-doctor.notifications-allowed", "MediaWMFNeeded,MediaWidevineNoWMF,MediaCannotInitializePulseAudio,MediaCannotPlayNoDecoders,MediaUnsupportedLibavcodec,MediaPlatformDecoderNotFound,MediaDecodeError");
#else
  pref("media.decoder-doctor.notifications-allowed", "MediaWMFNeeded,MediaWidevineNoWMF,MediaCannotInitializePulseAudio,MediaCannotPlayNoDecoders,MediaUnsupportedLibavcodec,MediaPlatformDecoderNotFound");
#endif
pref("media.decoder-doctor.decode-errors-allowed", "");
pref("media.decoder-doctor.decode-warnings-allowed", "");
// Whether we report partial failures.
pref("media.decoder-doctor.verbose", false);
// URL to report decode issues
pref("media.decoder-doctor.new-issue-endpoint", "https://webcompat.com/issues/new");

pref("media.videocontrols.picture-in-picture.enabled", false);
pref("media.videocontrols.picture-in-picture.display-text-tracks.enabled", true);
pref("media.videocontrols.picture-in-picture.video-toggle.enabled", false);
pref("media.videocontrols.picture-in-picture.video-toggle.always-show", false);
pref("media.videocontrols.picture-in-picture.video-toggle.min-video-secs", 45);
pref("media.videocontrols.picture-in-picture.video-toggle.position", "right");
pref("media.videocontrols.picture-in-picture.video-toggle.has-used", false);
pref("media.videocontrols.picture-in-picture.display-text-tracks.toggle.enabled", true);
pref("media.videocontrols.picture-in-picture.display-text-tracks.size", "medium");
pref("media.videocontrols.picture-in-picture.improved-video-controls.enabled", true);
pref("media.videocontrols.picture-in-picture.respect-disablePictureInPicture", true);
pref("media.videocontrols.keyboard-tab-to-all-controls", true);

#ifdef MOZ_WEBRTC
  pref("media.navigator.video.enabled", true);
  pref("media.navigator.video.default_fps",30);
  pref("media.navigator.video.use_remb", true);
  pref("media.navigator.video.use_transport_cc", true);
  pref("media.peerconnection.video.use_rtx", true);
  pref("media.peerconnection.video.use_rtx.blocklist", "doxy.me,*.doxy.me");
  pref("media.peerconnection.sdp.quirk.duplicate_fingerprint.allowlist", "");
  pref("media.navigator.video.use_tmmbr", false);
  pref("media.navigator.audio.use_fec", true);
  pref("media.navigator.video.offer_rtcp_rsize", true);

  #ifdef NIGHTLY_BUILD
    pref("media.peerconnection.sdp.parser", "sipcc");
    pref("media.peerconnection.sdp.alternate_parse_mode", "parallel");
    pref("media.peerconnection.sdp.strict_success", false);
    pref("media.navigator.video.red_ulpfec_enabled", true);
  #else
    pref("media.peerconnection.sdp.parser", "sipcc");
    pref("media.peerconnection.sdp.alternate_parse_mode", "never");
    pref("media.peerconnection.sdp.strict_success", false);
    pref("media.navigator.video.red_ulpfec_enabled", true);
  #endif

  pref("media.peerconnection.sdp.disable_stereo_fmtp", false);
  pref("media.webrtc.debug.log_file", "");

  pref("media.navigator.video.default_width",0);  // adaptive default
  pref("media.navigator.video.default_height",0); // adaptive default
  pref("media.navigator.video.max_fs", 12288); // Enough for 2048x1536
  pref("media.navigator.video.max_fr", 60);
  pref("media.navigator.video.disable_h264_baseline", false);
  pref("media.navigator.video.h264.level", 31); // 0x42E01f - level 3.1
  pref("media.navigator.video.h264.max_br", 0);
  pref("media.navigator.video.h264.max_mbps", 0);
  pref("media.peerconnection.video.vp9_enabled", true);
  pref("media.peerconnection.video.vp9_preferred", false);
  pref("media.getusermedia.audio.max_channels", 0);
  #if defined(ANDROID)
    pref("media.getusermedia.camera.off_while_disabled.enabled", false);
    pref("media.getusermedia.microphone.off_while_disabled.enabled", false);
  #else
    pref("media.getusermedia.camera.off_while_disabled.enabled", true);
    pref("media.getusermedia.microphone.off_while_disabled.enabled", false);
  #endif
  pref("media.getusermedia.camera.off_while_disabled.delay_ms", 3000);
  pref("media.getusermedia.microphone.off_while_disabled.delay_ms", 3000);
  // Desktop is typically VGA capture or more; and qm_select will not drop resolution
  // below 1/2 in each dimension (or so), so QVGA (320x200) is the lowest here usually.
  pref("media.peerconnection.video.min_bitrate", 0);
  pref("media.peerconnection.video.start_bitrate", 0);
  pref("media.peerconnection.video.max_bitrate", 0);
  pref("media.peerconnection.video.min_bitrate_estimate", 0);
  pref("media.peerconnection.video.denoising", false);
  pref("media.navigator.audio.fake_frequency", 1000);
  pref("media.navigator.permission.disabled", false);
  pref("media.navigator.streams.fake", false);
  pref("media.peerconnection.default_iceservers", "[]");
  pref("media.peerconnection.allow_old_setParameters", true);
  pref("media.peerconnection.ice.loopback", false); // Set only for testing in offline environments.
  pref("media.peerconnection.ice.tcp", true);
  pref("media.peerconnection.ice.tcp_so_sock_count", 0); // Disable SO gathering
  pref("media.peerconnection.ice.link_local", false); // Set only for testing IPV6 in networks that don't assign IPV6 addresses
  pref("media.peerconnection.ice.force_interface", ""); // Limit to only a single interface
  pref("media.peerconnection.ice.relay_only", false); // Limit candidates to TURN
  pref("media.peerconnection.use_document_iceservers", true);

  pref("media.peerconnection.identity.timeout", 10000);
  pref("media.peerconnection.ice.stun_client_maximum_transmits", 7);
  pref("media.peerconnection.ice.trickle_grace_period", 5000);
  pref("media.peerconnection.ice.no_host", false);
  pref("media.peerconnection.ice.default_address_only", false);
  // See Bug 1581947 for Android hostname obfuscation
  #if defined(MOZ_WIDGET_ANDROID)
    pref("media.peerconnection.ice.obfuscate_host_addresses", false);
  #else
    pref("media.peerconnection.ice.obfuscate_host_addresses", true);
  #endif
  pref("media.peerconnection.ice.obfuscate_host_addresses.blocklist", "");
  pref("media.peerconnection.ice.proxy_only_if_behind_proxy", false);
  pref("media.peerconnection.ice.proxy_only", false);
  pref("media.peerconnection.ice.proxy_only_if_pbmode", false);
  pref("media.peerconnection.turn.disable", false);
  pref("media.peerconnection.treat_warnings_as_errors", false);
  #ifdef NIGHTLY_BUILD
    pref("media.peerconnection.description.legacy.enabled", false);
  #else
    pref("media.peerconnection.description.legacy.enabled", true);
  #endif

  // 770 = DTLS 1.0, 771 = DTLS 1.2, 772 = DTLS 1.3
  // TODO(bug 1952950) Re-enable this once 1952706 lands everywhere.
  pref("media.peerconnection.dtls.version.min", 771);
  pref("media.peerconnection.dtls.version.max", 771);

#if defined(XP_MACOSX)
  pref("media.getusermedia.audio.processing.platform.enabled", true);
#else
  pref("media.getusermedia.audio.processing.platform.enabled", false);
#endif
  // These values (aec, agc, and noise) are from:
  // third_party/libwebrtc/modules/audio_processing/include/audio_processing.h
  pref("media.getusermedia.audio.processing.aec.enabled", true);
  pref("media.getusermedia.audio.processing.aec", 1); // kModerateSuppression
  pref("media.getusermedia.audio.processing.aec.mobile", false);
  pref("media.getusermedia.audio.processing.noise.enabled", true);
  pref("media.getusermedia.audio.processing.noise", 2); // kHigh
  pref("media.getusermedia.audio.processing.agc.enabled", true);
  pref("media.getusermedia.audio.processing.agc", 1); // kAdaptiveDigital
  pref("media.getusermedia.audio.processing.agc2.forced", true);
  pref("media.getusermedia.audio.processing.hpf.enabled", true);
  pref("media.getusermedia.audio.processing.transient.enabled", true);
#endif // MOZ_WEBRTC

#if !defined(ANDROID)
  pref("media.getusermedia.screensharing.enabled", true);
#endif

pref("media.getusermedia.audio.capture.enabled", false);

// WebVTT debug logging.
pref("media.webvtt.debug.logging", false);

// Whether to allow recording of AudioNodes with MediaRecorder
pref("media.recorder.audio_node.enabled", false);

// Whether MediaRecorder's video encoder should allow dropping frames in order
// to keep up under load. Useful for tests but beware of memory consumption!
pref("media.recorder.video.frame_drops", true);

// The default number of decoded video frames that are enqueued in
// MediaDecoderReader's mVideoQueue.
pref("media.video-queue.default-size", 10);

// The maximum number of queued frames to send to the compositor.
// By default, send all of them.
pref("media.video-queue.send-to-compositor-size", 9999);

pref("media.cubeb.output_voice_routing", true);

// APZ preferences. For documentation/details on what these prefs do, check
// gfx/layers/apz/src/AsyncPanZoomController.cpp.
pref("apz.overscroll.stop_velocity_threshold", "0.01");
pref("apz.overscroll.stretch_factor", "0.35");

pref("apz.zoom-to-focused-input.enabled", true);

pref("formhelper.autozoom.force-disable.test-only", false);

#ifdef XP_MACOSX
  // Whether to run in native HiDPI mode on machines with "Retina"/HiDPI
  // display.
  //   <= 0 : hidpi mode disabled, display will just use pixel-based upscaling.
  //   == 1 : hidpi supported if all screens share the same backingScaleFactor.
  //   >= 2 : hidpi supported even with mixed backingScaleFactors (somewhat
  //          broken).
  pref("gfx.hidpi.enabled", 2);
#endif

pref("gfx.downloadable_fonts.enabled", true);
pref("gfx.downloadable_fonts.fallback_delay", 3000);
pref("gfx.downloadable_fonts.fallback_delay_short", 100);

#ifdef XP_WIN
  pref("gfx.font_rendering.directwrite.use_gdi_table_loading", true);
#endif

#if defined(XP_WIN)
  // comma separated list of backends to use in order of preference
  // e.g., pref("gfx.canvas.azure.backends", "direct2d,skia");
  pref("gfx.canvas.azure.backends", "direct2d1.1,skia");
#elif defined(XP_MACOSX)
  pref("gfx.canvas.azure.backends", "skia");
#else
  pref("gfx.canvas.azure.backends", "skia");
#endif
pref("gfx.content.azure.backends", "skia");

#ifdef XP_WIN
  pref("gfx.webrender.flip-sequential", false);
  pref("gfx.webrender.dcomp-win.enabled", true);
  pref("gfx.webrender.triple-buffering.enabled", true);
#endif

// WebRender debugging utilities.
pref("gfx.webrender.debug.texture-cache", false);
pref("gfx.webrender.debug.texture-cache.clear-evicted", true);
pref("gfx.webrender.debug.render-targets", false);
pref("gfx.webrender.debug.gpu-cache", false);
pref("gfx.webrender.debug.alpha-primitives", false);
pref("gfx.webrender.debug.profiler", false);
pref("gfx.webrender.debug.gpu-time-queries", false);
pref("gfx.webrender.debug.gpu-sample-queries", false);
pref("gfx.webrender.debug.disable-batching", false);
pref("gfx.webrender.debug.epochs", false);
pref("gfx.webrender.debug.echo-driver-messages", false);
pref("gfx.webrender.debug.show-overdraw", false);
pref("gfx.webrender.debug.slow-frame-indicator", false);
pref("gfx.webrender.debug.picture-caching", false);
pref("gfx.webrender.debug.picture-borders", false);
pref("gfx.webrender.debug.force-picture-invalidation", false);
pref("gfx.webrender.debug.primitives", false);
pref("gfx.webrender.debug.small-screen", false);
pref("gfx.webrender.debug.obscure-images", false);
pref("gfx.webrender.debug.glyph-flashing", false);
pref("gfx.webrender.debug.capture-profiler", false);
pref("gfx.webrender.debug.profiler-ui", "Default");
pref("gfx.webrender.debug.window-visibility", false);

pref("gfx.webrender.multithreading", true);
#ifdef XP_WIN
pref("gfx.webrender.pbo-uploads", false);
pref("gfx.webrender.batched-texture-uploads", true);
pref("gfx.webrender.draw-calls-for-texture-copy", true);
#else
pref("gfx.webrender.pbo-uploads", true);
pref("gfx.webrender.batched-texture-uploads", false);
pref("gfx.webrender.draw-calls-for-texture-copy", false);
#endif


pref("accessibility.warn_on_browsewithcaret", true);

pref("accessibility.browsewithcaret_shortcut.enabled", true);

// We follow the "Click in the scrollbar to:" system preference on OS X and
// "gtk-primary-button-warps-slider" property with GTK (since 2.24 / 3.6),
// unless this preference is explicitly set.
#if !defined(XP_MACOSX) && !defined(MOZ_WIDGET_GTK)
  pref("ui.scrollToClick", 0);
#endif

// These are some selection-related colors which have no per platform
// implementation.
#if !defined(XP_MACOSX)
pref("ui.textSelectDisabledBackground", "#b0b0b0");
#endif

// This makes the selection stand out when typeaheadfind is on.
// Used with nsISelectionController::SELECTION_ATTENTION
pref("ui.textSelectAttentionBackground", "#38d878");
pref("ui.textSelectAttentionForeground", "#ffffff");

// This makes the matched text stand out when findbar highlighting is on.
// Used with nsISelectionController::SELECTION_FIND
pref("ui.textHighlightBackground", "#ef0fff");
// The foreground color for the matched text in findbar highlighting
// Used with nsISelectionController::SELECTION_FIND
pref("ui.textHighlightForeground", "#ffffff");

// We want the ability to forcibly disable platform a11y, because
// some non-a11y-related components attempt to bring it up.  See bug
// 538530 for details about Windows; we have a pref here that allows it
// to be disabled for performance and testing resons.
// See bug 761589 for the crossplatform aspect.
//
// This pref is checked only once, and the browser needs a restart to
// pick up any changes.
//
// Values are -1 always on. 1 always off, 0 is auto as some platform perform
// further checks.
pref("accessibility.force_disabled", 0);

pref("focusmanager.testmode", false);

// Type Ahead Find
pref("accessibility.typeaheadfind", true);
// Enable FAYT by pressing / or "
pref("accessibility.typeaheadfind.manual", true);
// casesensitive: controls the find bar's case-sensitivity
//     0 - "never"  (case-insensitive)
//     1 - "always" (case-sensitive)
// other - "auto"   (case-sensitive for mixed-case input, insensitive otherwise)
pref("accessibility.typeaheadfind.casesensitive", 0);
pref("accessibility.typeaheadfind.linksonly", true);
pref("accessibility.typeaheadfind.startlinksonly", false);
// timeout: controls the delay in milliseconds after which the quick-find dialog will close
//          if no further keystrokes are pressed
//              set to a zero or negative value to keep dialog open until it's manually closed
pref("accessibility.typeaheadfind.timeout", 4000);
pref("accessibility.typeaheadfind.soundURL", "beep");
pref("accessibility.typeaheadfind.wrappedSoundURL", "");
pref("accessibility.typeaheadfind.enablesound", true);
#ifdef XP_MACOSX
  pref("accessibility.typeaheadfind.prefillwithselection", false);
#else
  pref("accessibility.typeaheadfind.prefillwithselection", true);
#endif
pref("accessibility.typeaheadfind.matchesCountLimit", 1000);
pref("findbar.highlightAll", false);
pref("findbar.entireword", false);
pref("findbar.iteratorTimeout", 100);
// matchdiacritics: controls the find bar's diacritic matching
//     0 - "never"  (ignore diacritics)
//     1 - "always" (match diacritics)
// other - "auto"   (match diacritics if input has diacritics, ignore otherwise)
pref("findbar.matchdiacritics", 0);
pref("findbar.modalHighlight", false);

// use Mac OS X Appearance panel text smoothing setting when rendering text, disabled by default
pref("gfx.use_text_smoothing_setting", false);

// Number of characters to consider emphasizing for rich autocomplete results
pref("toolkit.autocomplete.richBoundaryCutoff", 200);

pref("toolkit.scrollbox.scrollIncrement", 20);
pref("toolkit.scrollbox.clickToScroll.scrollDelay", 150);

pref("toolkit.shopping.ohttpConfigURL", "https://prod.ohttp-gateway.prod.webservices.mozgcp.net/ohttp-configs");
pref("toolkit.shopping.ohttpRelayURL", "https://mozilla-ohttp.fastly-edge.com/");
pref("toolkit.shopping.environment", "prod");

// Determines whether Review Checker is enabled for de and fr domains
pref("toolkit.shopping.experience2023.defr", false);

// Controls logging for Sqlite.sys.mjs.
pref("toolkit.sqlitejsm.loglevel", "Error");

pref("toolkit.tabbox.switchByScrolling", false);

// Telemetry settings.
// Server to submit telemetry pings to.
pref("toolkit.telemetry.server", "https://incoming.telemetry.mozilla.org");
// Telemetry server owner. Please change if you set toolkit.telemetry.server to a different server
pref("toolkit.telemetry.server_owner", "Mozilla");
// Determines whether full SQL strings are returned when they might contain sensitive info
// i.e. dynamically constructed SQL strings or SQL executed by addons against addon DBs
pref("toolkit.telemetry.debugSlowSql", false);
// Whether to use the unified telemetry behavior, requires a restart.
pref("toolkit.telemetry.unified", true);

// DAP related preferences
pref("toolkit.telemetry.dap_enabled", false);
pref("toolkit.telemetry.dap.logLevel", "Warn");
// Verification tasks
pref("toolkit.telemetry.dap_task1_enabled", false);
pref("toolkit.telemetry.dap_task1_taskid", "");
// URL visit counting
pref("toolkit.telemetry.dap_visit_counting_enabled", false);
// Note: format of patterns is "<proto>://<host>/<path>"
// See https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/Match_patterns
pref("toolkit.telemetry.dap_visit_counting_experiment_list", "[]");
// DAP protocol Leader endpoint. Operated by DivviUp/ISRG.
// - HPKE key is base64url-encoded response of the /hpke_config path on server.
pref("toolkit.telemetry.dap.leader.url", "https://dap-09-3.api.divviup.org");
pref("toolkit.telemetry.dap.leader.hpke", "ACkAACAAAQABACDk8wgwe2-TqHyaL74uqjVWMcF1zi9pxiwQhu4aPwncYw");
// DAP protocol Helper endpoint. Operated by Mozilla.
// - HPKE key is base64url-encoded response of the /hpke_config path on server.
pref("toolkit.telemetry.dap.helper.url", "https://dap.services.mozilla.com");
pref("toolkit.telemetry.dap.helper.hpke", "ACkAACAAAQABACAucqWdIQRN6BxumPBRXIlg2JsxcznwWX7vyqzM3cjuQA");

// Controls telemetry logs for the Translations feature throughout Firefox.
pref("toolkit.telemetry.translations.logLevel", "Error");

// pref for mozilla to induce a new ping from users. This value should only ever be increased
// and doing so will induce a new data ping from all users, so be careful. Mozilla may edit
// this pref via our remote update/experimentation system
pref("toolkit.telemetry.user_characteristics_ping.current_version", 0);
// pref containing the value for the user of the last version of the ping we sent
pref("toolkit.telemetry.user_characteristics_ping.last_version_sent", 0);
// if a user wants to disable this type of ping explicitly, set this to true
// firefox/mozilla will not modify this value.
pref("toolkit.telemetry.user_characteristics_ping.opt-out", false);
// if a user wants to send a single ping of this type, they can set it
// to true. It will be set to false again after a successfull ping.
pref("toolkit.telemetry.user_characteristics_ping.send-once", false);
// A unique identifier for the user characteristics ping. This is not the same as
// the telemetry client id (which is not sent in this ping), it is cleared when a
// user opts-out of telemetry, it is set upon first telemetry submission
pref("toolkit.telemetry.user_characteristics_ping.uuid", "");
pref("toolkit.telemetry.user_characteristics_ping.logLevel", "Warn");

// AsyncShutdown delay before crashing in case of shutdown freeze
// ASan, TSan and code coverage builds can be considerably slower. Extend the
// grace period for both the asyncshutdown and the terminator.
#if defined(MOZ_ASAN)
  pref("toolkit.asyncshutdown.crash_timeout", 300000); // 5 minutes
#elif defined(MOZ_TSAN)
  pref("toolkit.asyncshutdown.crash_timeout", 360000); // 6 minutes
#elif defined(MOZ_CODE_COVERAGE)
  pref("toolkit.asyncshutdown.crash_timeout", 180000); // 3 minutes
#else
  pref("toolkit.asyncshutdown.crash_timeout", 60000); // 1 minute
#endif // !defined(MOZ_ASAN) && !defined(MOZ_TSAN)
// Extra logging for AsyncShutdown barriers and phases
pref("toolkit.asyncshutdown.log", false);

// Enable JS dump() function.
// IMPORTANT: These prefs must be here even though they're also defined in
// StaticPrefList.yaml. They are required because MOZILLA_OFFICIAL is false in
// local full builds but true in artifact builds. Without these definitions
// here, dumping is disabled in artifact builds (see Bug 1490412).
#ifdef MOZILLA_OFFICIAL
  pref("browser.dom.window.dump.enabled", false, sticky);
  pref("devtools.console.stdout.chrome", false, sticky);
#else
  pref("browser.dom.window.dump.enabled", true, sticky);
  pref("devtools.console.stdout.chrome", true, sticky);
#endif

pref("devtools.console.stdout.content", false, sticky);

// Controls whether EventEmitter module throws dump message on each emit
pref("toolkit.dump.emit", false);

// Preferences for the new performance panel. Note that some preferences are duplicated
// with a ".remote" postfix. This is because we have one set of preference for local
// profiling, and a second set for remote profiling.

// This pref configures the base URL for the profiler.firefox.com instance to
// use. This is useful so that a developer can change it while working on
// profiler.firefox.com, or in tests. This isn't exposed directly to the user.
pref("devtools.performance.recording.ui-base-url", "https://profiler.firefox.com");
// When gathering profiles from child processes, this is the longest time (in
// seconds) allowed between two responses. 0 = Use internal default.
pref("devtools.performance.recording.child.timeout_s", 15);
// The popup is only enabled by default on Nightly, Dev Edition, and debug buildsd since
// it's a developer focused item. It can still be enabled by going to profiler.firefox.com,
// but by default it is off on Release and Beta. Note that this only adds it to the
// the customization palette, not to the navbar.
#if defined(NIGHTLY_BUILD) || defined(MOZ_DEV_EDITION) || defined(DEBUG)
  pref("devtools.performance.popup.feature-flag", true);
#else
  pref("devtools.performance.popup.feature-flag", false);
#endif
// The preset to use for the recording settings. If set to "custom" then the pref
// values below will be used.
#if defined(NIGHTLY_BUILD) || !defined(MOZILLA_OFFICIAL)
  // Use a more advanced preset on Nightly and local builds.
  pref("devtools.performance.recording.preset", "firefox-platform");
  pref("devtools.performance.recording.preset.remote", "firefox-platform");
#else
  pref("devtools.performance.recording.preset", "web-developer");
  pref("devtools.performance.recording.preset.remote", "web-developer");
#endif
// The profiler's active tab view has a few issues. Disable it until the issues
// are ironed out.
pref("devtools.performance.recording.active-tab-view.enabled", false);
// Profiler buffer size. It is the maximum number of 8-bytes entries in the
// profiler's buffer. 10000000 is ~80mb.
pref("devtools.performance.recording.entries", 10000000);
pref("devtools.performance.recording.entries.remote", 10000000);
// Profiler interval in microseconds. 1000µs is 1ms
pref("devtools.performance.recording.interval", 1000);
pref("devtools.performance.recording.interval.remote", 1000);
// Profiler duration of entries in the profiler's buffer in seconds.
// `0` means no time limit for the markers, they roll off naturally from the
// circular buffer.
pref("devtools.performance.recording.duration", 0);
pref("devtools.performance.recording.duration.remote", 0);
// Profiler feature set. See tools/profiler/core/platform.cpp for features and
// explanations. Remote profiling also includes the java feature by default.
// If the remote debuggee isn't an Android phone, then this feature will
// be ignored.
pref("devtools.performance.recording.features", "[\"js\",\"stackwalk\",\"cpu\",\"screenshots\",\"memory\"]");
pref("devtools.performance.recording.features.remote", "[\"js\",\"stackwalk\",\"cpu\",\"screenshots\",\"memory\",\"java\"]");
// Threads to be captured by the profiler.
pref("devtools.performance.recording.threads", "[\"GeckoMain\",\"Compositor\",\"Renderer\"]");
pref("devtools.performance.recording.threads.remote", "[\"GeckoMain\",\"Compositor\",\"Renderer\"]");
// A JSON array of strings, where each string is a file path to an objdir on
// the host machine. This is used in order to look up symbol information from
// build artifacts of local builds.
pref("devtools.performance.recording.objdirs", "[]");
pref("devtools.performance.recording.power.external-url", "");
pref("devtools.performance.recording.markers.external-url", "");
// The popup will display some introductory text the first time it is displayed.
pref("devtools.performance.popup.intro-displayed", false);
// This preference controls whether the "more actions" menu in about:profiling
// contains entries that help Firefox developers.
#if defined(NIGHTLY_BUILD) || !defined(MOZILLA_OFFICIAL)
  pref("devtools.performance.aboutprofiling.has-developer-options", true);
#else
  pref("devtools.performance.aboutprofiling.has-developer-options", false);
#endif

// Compatibility preferences
// Stringified array of target browsers that users investigate.
pref("devtools.inspector.compatibility.target-browsers", "");

// view source
pref("view_source.editor.path", "");
// allows to add further arguments to the editor; use the %LINE% placeholder
// for jumping to a specific line (e.g. "/line:%LINE%" or "--goto %LINE%")
pref("view_source.editor.args", "");

// whether or not to draw images while dragging
pref("nglayout.enable_drag_images", true);

// URI fixup prefs
pref("browser.fixup.alternate.prefix", "www.");
pref("browser.fixup.alternate.protocol", "https");
pref("browser.fixup.alternate.suffix", ".com");
pref("browser.fixup.fallback-to-https", true);

// NOTE: On most platforms we save print settins to prefs with the name of the
// printer in the pref name (Android being the notable exception, where prefs
// are saved "globally" without a printer name in the pref name).  For those
// platforms, the prefs below simply act as default values for when we
// encounter a printer for the first time, but only a subset of prefs will be
// used in this case.  See nsPrintSettingsService::InitPrintSettingsFromPrefs
// for the restrictions on which prefs can act as defaults.

// Whether we directly use the system print dialog to collect the user's print
// settings rather than using the tab-modal print preview dialog.
// Note: `print.always_print_silent` overrides this.
pref("print.prefer_system_dialog", false);

// Print/Preview Shrink-To-Fit won't shrink below 20% for text-ish documents.
pref("print.shrink-to-fit.scale-limit-percent", 20);

// Whether or not to force the Page Setup submenu of the File menu to shown
pref("print.show_page_setup_menu", false);

// Print header customization
// Use the following codes:
// &T - Title
// &U - Document URL
// &D - Date/Time
// &P - Page Number
// &PT - Page Number "of" Page total
// Set each header to a string containing zero or one of these codes
// and the code will be replaced in that string by the corresponding data
pref("print.print_headerleft", "&T");
pref("print.print_headercenter", "");
pref("print.print_headerright", "&U");
pref("print.print_footerleft", "&PT");
pref("print.print_footercenter", "");
pref("print.print_footerright", "&D");

// A list of comma separated key:value pairs, so:
//
//   key1:value1,key2:value2
//
// Which allows testing extra CUPS-related printer settings for monochrome
// printing.
pref("print.cups.monochrome.extra_settings", "");

// xxxbsmedberg: more toolkit prefs

// Save the Printings after each print job
pref("print.save_print_settings", true);

// Enables the "more settings" in Print Preview to match previous
// configuration.
pref("print.more-settings.open", false);

// Enables you to specify a user unwriteable margin, if a printer's actual
// unwriteable margin is greater than this the printer one will be used.
// This is used by both Printing and Print Preview
// Units are in 1/100ths of an inch.
pref("print.print_edge_top", 0);
pref("print.print_edge_left", 0);
pref("print.print_edge_right", 0);
pref("print.print_edge_bottom", 0);

// Should this just be checking for MOZ_WIDGET_GTK?
#if defined(ANDROID) || defined(XP_UNIX) && !defined(XP_MACOSX)
  pref("print.print_reversed", false);
  // This is the default. Probably just remove this.
  pref("print.print_in_color", true);
#endif

// Scripts & Windows prefs
pref("dom.beforeunload_timeout_ms",         1000);
pref("dom.disable_window_flip",             false);
pref("dom.disable_window_move_resize",      false);

pref("dom.allow_scripts_to_close_windows",          false);

// List of urls for which mutation events are enabled even if mutation events
// in general are disabled. See nsContentUtils::IsURIInPrefList.
pref("dom.mutation_events.forceEnable", "");

pref("dom.popup_allowed_events", "change click dblclick auxclick mousedown mouseup pointerdown pointerup notificationclick reset submit touchend contextmenu");

pref("dom.storage.shadow_writes", false);
pref("dom.storage.snapshot_prefill", 16384);
pref("dom.storage.snapshot_gradual_prefill", 4096);
pref("dom.storage.snapshot_reusing", true);
pref("dom.storage.client_validation", true);

// Enable time picker UI. By default, disabled.
pref("dom.forms.datetime.timepicker", false);

// Enable search in <select> dropdowns (more than 40 options)
pref("dom.forms.selectSearch", false);
// Allow for webpages to provide custom styling for <select>
// popups.
//
// Disabled on GTK (originally due to bug 1338283, but not enabled since, and
// native appearance might be preferred).
// Disabled on macOS because native appearance is preferred, see bug 1703866.
#if defined(MOZ_WIDGET_GTK) || defined(XP_MACOSX)
  pref("dom.forms.select.customstyling", false);
#else
  pref("dom.forms.select.customstyling", true);
#endif

pref("dom.cycle_collector.incremental", true);

// List of domains exempted from RFP. The list is comma separated domain list.
pref("privacy.resistFingerprinting.exemptedDomains", "*.example.invalid");

// If privacy.fingerprintingProtection is enabled, this pref can be used to add
// or remove features from its effects
pref("privacy.fingerprintingProtection.overrides", "");

// If privacy.fingerprintingProtection is enabled, this pref can be used to add
// or remove features on a domain granular level.
pref("privacy.fingerprintingProtection.granularOverrides", "");

// If privacy.baselineFingerprintingProtection is enabled, this pref can be used to add
// or remove features from its effects
pref("privacy.baselineFingerprintingProtection.overrides", "");

// If privacy.baselineFingerprintingProtection is enabled, this pref can be used to add
// or remove features on a domain granular level.
pref("privacy.baselineFingerprintingProtection.granularOverrides", "");

// Fix cookie blocking breakage by providing ephemeral Paritioned LocalStorage
// for a list of hosts when detected as trackers.
// (See nsICookieService::BEHAVIOR_REJECT_TRACKER cookie behavior)
// See: Bug 1505212, Bug 1659394, Bug 1631811, Bug 1665035.
pref("privacy.restrict3rdpartystorage.partitionedHosts", "accounts.google.com/o/oauth2/,d35nw2lg0ahg0v.cloudfront.net/,datastudio.google.com/embed/reporting/,d3qlaywcwingl6.cloudfront.net/");

// If a host is contained in this pref list, user-interaction is required
// before granting the storage access permission.
pref("privacy.restrict3rdpartystorage.userInteractionRequiredForHosts", "");

// The url decoration tokens used to for stripping document referrers based on.
// A list separated by spaces.  This pref isn't meant to be changed by users.
pref("privacy.restrict3rdpartystorage.url_decorations", "");

// Excessive reporting of blocked popups can be a DOS vector,
// by overloading the main process as popups get blocked and when
// users try to restore all popups, which is the most visible
// option in our UI at the time of writing.
// We will invisibly drop any popups from a page that has already
// opened more than this number of popups.
pref("privacy.popups.maxReported", 100);

// Purging first-party tracking cookies.
pref("privacy.purge_trackers.enabled", true);
#ifdef NIGHTLY_BUILD
  pref("privacy.purge_trackers.logging.level", "Warn");
#else
  pref("privacy.purge_trackers.logging.level", "Error");
#endif

// Allowable amount of cookies to purge in a batch.
pref("privacy.purge_trackers.max_purge_count", 100);

// Whether purging should not clear data from domains
// that are associated with other domains which have
// user interaction (even if they don't have user
// interaction directly).
pref("privacy.purge_trackers.consider_entity_list", false);

pref("dom.event.contextmenu.enabled",       true);

pref("javascript.enabled",                  true);
pref("javascript.options.wasm",                   true);
pref("javascript.options.wasm_trustedprincipals", true);
pref("javascript.options.wasm_verbose",           false);
pref("javascript.options.wasm_baselinejit",       true);

pref("javascript.options.asyncstack", true);
// Broadly capturing async stack data adds overhead that is only advisable for
// developers, so we only enable it when the devtools are open, by default.
pref("javascript.options.asyncstack_capture_debuggee_only", true);

// This preference instructs the JS engine to discard the
// source of any privileged JS after compilation. This saves
// memory, but makes things like Function.prototype.toSource()
// fail.
pref("javascript.options.discardSystemSource", false);

// Many of the the following preferences tune the SpiderMonkey GC, if you
// change the defaults here please also consider changing them in
// js/src/jsgc.cpp.  They're documented in js/src/jsapi.h.

// JSGC_MAX_BYTES
// SpiderMonkey defaults to 2^32-1 bytes, but this is measured in MB so that
// cannot be represented directly in order to show it in about:config.
pref("javascript.options.mem.max", -1);

// JSGC_MIN_NURSERY_BYTES / JSGC_MAX_NURSERY_BYTES
pref("javascript.options.mem.nursery.min_kb", 256);
pref("javascript.options.mem.nursery.max_kb", 65536);

// JSGC_MODE
pref("javascript.options.mem.gc_per_zone", true);
pref("javascript.options.mem.gc_incremental", true);

// JSGC_INCREMENTAL_WEAKMAP_ENABLED
pref("javascript.options.mem.incremental_weakmap", true);

// JSGC_SLICE_TIME_BUDGET_MS
// Override the shell's default of unlimited slice time.
pref("javascript.options.mem.gc_incremental_slice_ms", 5);

// JSGC_COMPACTING_ENABLED
pref("javascript.options.mem.gc_compacting", true);

// JSGC_NURSERY_ENABLED
pref("javascript.options.mem.gc_generational", true);

#ifdef NIGHTLY_BUILD
// JSGC_SEMISPACE_NURSERY_ENABLED
pref("javascript.options.mem.gc_experimental_semispace_nursery", false);
#endif

// JSGC_PARALLEL_MARKING_ENABLED
// This only applies to the main runtime and does not affect workers.
#ifndef ANDROID
pref("javascript.options.mem.gc_parallel_marking", true);
#else
pref("javascript.options.mem.gc_parallel_marking", false);
#endif

// JSGC_PARALLEL_MARKING_THRESHOLD_MB
// Minimum heap size at which to use parallel marking, if enabled.
#if defined(XP_WIN)
pref("javascript.options.mem.gc_parallel_marking_threshold_mb", 8);
#elif defined(XP_MACOSX)
pref("javascript.options.mem.gc_parallel_marking_threshold_mb", 4);
#elif defined(ANDROID)
pref("javascript.options.mem.gc_parallel_marking_threshold_mb", 16);
#elif defined(XP_UNIX)
pref("javascript.options.mem.gc_parallel_marking_threshold_mb", 16);
#endif

// JSGC_MAX_MARKING_THREADS
pref("javascript.options.mem.gc_max_parallel_marking_threads", 2);

// JSGC_HIGH_FREQUENCY_TIME_LIMIT
pref("javascript.options.mem.gc_high_frequency_time_limit_ms", 1000);

// JSGC_SMALL_HEAP_SIZE_MAX
pref("javascript.options.mem.gc_small_heap_size_max_mb", 100);

// JSGC_LARGE_HEAP_SIZE_MIN
pref("javascript.options.mem.gc_large_heap_size_min_mb", 500);

// JSGC_HIGH_FREQUENCY_SMALL_HEAP_GROWTH
pref("javascript.options.mem.gc_high_frequency_small_heap_growth", 300);

// JSGC_HIGH_FREQUENCY_LARGE_HEAP_GROWTH
pref("javascript.options.mem.gc_high_frequency_large_heap_growth", 150);

// JSGC_LOW_FREQUENCY_HEAP_GROWTH
pref("javascript.options.mem.gc_low_frequency_heap_growth", 150);

// JSGC_BALANCED_HEAP_LIMITS_ENABLED
pref("javascript.options.mem.gc_balanced_heap_limits", false);

// JSGC_HEAP_GROWTH_FACTOR
pref("javascript.options.mem.gc_heap_growth_factor", 50);

// JSGC_ALLOCATION_THRESHOLD
pref("javascript.options.mem.gc_allocation_threshold_mb", 27);

// JSGC_MALLOC_THRESHOLD_BASE
pref("javascript.options.mem.gc_malloc_threshold_base_mb", 38);

// JSGC_SMALL_HEAP_INCREMENTAL_LIMIT
pref("javascript.options.mem.gc_small_heap_incremental_limit", 150);

// JSGC_LARGE_HEAP_INCREMENTAL_LIMIT
pref("javascript.options.mem.gc_large_heap_incremental_limit", 110);

// JSGC_URGENT_THRESHOLD_MB
pref("javascript.options.mem.gc_urgent_threshold_mb", 16);

// JSGC_MIN_EMPTY_CHUNK_COUNT
pref("javascript.options.mem.gc_min_empty_chunk_count", 1);

// JSGC_HELPER_THREAD_RATIO
pref("javascript.options.mem.gc_helper_thread_ratio", 50);

// JSGC_MAX_HELPER_THREADS
pref("javascript.options.mem.gc_max_helper_threads", 8);

// Eager nursery collection parameters:
// JSGC_NURSERY_EAGER_COLLECTION_THRESHOLD_KB
pref("javascript.options.mem.nursery_eager_collection_threshold_kb", 256);
// JSGC_NURSERY_EAGER_COLLECTION_THRESHOLD_PERCENT
pref("javascript.options.mem.nursery_eager_collection_threshold_percent", 25);
// JSGC_NURSERY_EAGER_COLLECTION_TIMEOUT_MS
pref("javascript.options.mem.nursery_eager_collection_timeout_ms", 5000);

#ifdef JS_GC_ZEAL
pref("javascript.options.mem.gc_zeal.mode", 0);
pref("javascript.options.mem.gc_zeal.frequency", 5000);
#endif

pref("javascript.options.shared_memory", true);

pref("javascript.options.throw_on_debuggee_would_run", false);
pref("javascript.options.dump_stack_on_debuggee_would_run", false);

// advanced prefs
pref("image.animation_mode",                "normal");

// If there is ever a security firedrill that requires
// us to block certian ports global, this is the pref
// to use.  Is is a comma delimited list of port numbers
// for example:
//   pref("network.security.ports.banned", "1,2,3,4,5");
// prevents necko connecting to ports 1-5 unless the protocol
// overrides.

// Transmit UDP busy-work to the LAN when anticipating low latency
// network reads and on wifi to mitigate 802.11 Power Save Polling delays
pref("network.tickle-wifi.enabled", false);
pref("network.tickle-wifi.duration", 400);
pref("network.tickle-wifi.delay", 16);

// Default action for unlisted external protocol handlers
pref("network.protocol-handler.external-default", true);      // OK to load
pref("network.protocol-handler.warn-external-default", true); // warn before load

// Prevent using external protocol handlers for these schemes
pref("network.protocol-handler.external.hcp", false);
pref("network.protocol-handler.external.vbscript", false);
pref("network.protocol-handler.external.javascript", false);
pref("network.protocol-handler.external.data", false);
pref("network.protocol-handler.external.ie.http", false);
pref("network.protocol-handler.external.iehistory", false);
pref("network.protocol-handler.external.ierss", false);
pref("network.protocol-handler.external.mk", false);
pref("network.protocol-handler.external.ms-cxh", false);
pref("network.protocol-handler.external.ms-cxh-full", false);
pref("network.protocol-handler.external.ms-help", false);
pref("network.protocol-handler.external.ms-msdt", false);
pref("network.protocol-handler.external.res", false);
pref("network.protocol-handler.external.search", false);
pref("network.protocol-handler.external.search-ms", false);
pref("network.protocol-handler.external.shell", false);
pref("network.protocol-handler.external.vnd.ms.radio", false);
#ifdef XP_MACOSX
  pref("network.protocol-handler.external.help", false);
#endif
pref("network.protocol-handler.external.disk", false);
pref("network.protocol-handler.external.disks", false);
pref("network.protocol-handler.external.afp", false);
pref("network.protocol-handler.external.moz-icon", false);

// Don't allow  external protocol handlers for common typos
pref("network.protocol-handler.external.ttp", false);  // http
pref("network.protocol-handler.external.htp", false);  // http
pref("network.protocol-handler.external.ttps", false); // https
pref("network.protocol-handler.external.tps", false);  // https
pref("network.protocol-handler.external.ps", false);   // https
pref("network.protocol-handler.external.htps", false); // https
pref("network.protocol-handler.external.ile", false);  // file
pref("network.protocol-handler.external.le", false);   // file

// An exposed protocol handler is one that can be used in all contexts.  A
// non-exposed protocol handler is one that can only be used internally by the
// application.  For example, a non-exposed protocol would not be loaded by the
// application in response to a link click or a X-remote openURL command.
// Instead, it would be deferred to the system's external protocol handler.
// Only internal/built-in protocol handlers can be marked as exposed.

// This pref controls the default settings.  Per protocol settings can be used
// to override this value.
pref("network.protocol-handler.expose-all", true);

// Example: make IMAP an exposed protocol
// pref("network.protocol-handler.expose.imap", true);

// Whether IOService.connectivity and NS_IsOffline depends on connectivity status
pref("network.manage-offline-status", true);

// <http>
pref("network.http.version", "1.1");      // default
// pref("network.http.version", "1.0");   // uncomment this out in case of problems
// pref("network.http.version", "0.9");   // it'll work too if you're crazy
// keep-alive option is effectively obsolete. Nevertheless it'll work with
// some older 1.0 servers:

pref("network.http.proxy.version", "1.1");    // default
// pref("network.http.proxy.version", "1.0"); // uncomment this out in case of problems
                                              // (required if using junkbuster proxy)

// Whether we should respect the BE_CONSERVATIVE (aka nsIHttpChannelInternal.beConservative)
// flag when connecting to a proxy.  If the configured proxy accepts only TLS 1.3, system
// requests like updates will not pass through.  Setting this pref to false will fix that
// problem.
// Default at true to preserve the behavior we had before for backward compat.
pref("network.http.proxy.respect-be-conservative", true);

// this preference can be set to override the socket type used for normal
// HTTP traffic.  an empty value indicates the normal TCP/IP socket type.
pref("network.http.default-socket-type", "");

// There is a problem with some IIS7 servers that don't close the connection
// properly after it times out (bug #491541). Default timeout on IIS7 is
// 120 seconds. We need to reuse or drop the connection within this time.
// We set the timeout a little shorter to keep a reserve for cases when
// the packet is lost or delayed on the route.
pref("network.http.keep-alive.timeout", 115);

// Timeout connections if an initial response is not received after 5 mins.
pref("network.http.response.timeout", 300);

// Limit the absolute number of http connections.
// Note: the socket transport service will clamp the number below this if the OS
// cannot allocate that many FDs
#ifdef ANDROID
  pref("network.http.max-connections", 128);
#else
  pref("network.http.max-connections", 900);
#endif

// If NOT connecting via a proxy, then
// a new connection will only be attempted if the number of active persistent
// connections to the server is less then max-persistent-connections-per-server.
pref("network.http.max-persistent-connections-per-server", 6);

// Number of connections that we can open beyond the standard parallelism limit defined
// by max-persistent-connections-per-server/-proxy to handle urgent-start marked requests
pref("network.http.max-urgent-start-excessive-connections-per-host", 3);

// If connecting via a proxy, then a
// new connection will only be attempted if the number of active persistent
// connections to the proxy is less then max-persistent-connections-per-proxy.
pref("network.http.max-persistent-connections-per-proxy", 32);

// amount of time (in seconds) to suspend pending requests, before spawning a
// new connection, once the limit on the number of persistent connections per
// host has been reached.  however, a new connection will not be created if
// max-connections or max-connections-per-server has also been reached.
pref("network.http.request.max-start-delay", 10);

// If a connection is reset, we will retry it max-attempts times.
pref("network.http.request.max-attempts", 10);

// Maximum number of consecutive redirects before aborting.
pref("network.http.redirection-limit", 20);

// Enable http compression: comment this out in case of problems with 1.1
// NOTE: support for "compress" has been disabled per bug 196406.
// NOTE: separate values with comma+space (", "): see bug 576033
pref("network.http.accept-encoding", "gzip, deflate");
pref("network.http.accept-encoding.secure", "gzip, deflate, br, zstd");

// Prompt for redirects resulting in unsafe HTTP requests
pref("network.http.prompt-temp-redirect", false);

// If true generate CORRUPTED_CONTENT errors for entities that
// contain an invalid Assoc-Req response header
pref("network.http.assoc-req.enforce", false);

// On networks deploying QoS, it is recommended that these be lockpref()'d,
// since inappropriate marking can easily overwhelm bandwidth reservations
// for certain services (i.e. EF for VoIP, AF4x for interactive video,
// AF3x for broadcast/streaming video, etc)

// default value for HTTP
// in a DSCP environment this should be 40 (0x28, or AF11), per RFC-4594,
// Section 4.8 "High-Throughput Data Service Class"
pref("network.http.qos", 0);

// The number of milliseconds after sending a SYN for an HTTP connection,
// to wait before trying a different connection. 0 means do not use a second
// connection.
pref("network.http.connection-retry-timeout", 250);

// The number of seconds after sending initial SYN for an HTTP connection
// to give up if the OS does not give up first
pref("network.http.connection-timeout", 90);

// Close a connection if tls handshake does not finish in given number of
// seconds.
pref("network.http.tls-handshake-timeout", 30);

// The number of seconds after which we time out a connection of a retry (fallback)
// socket when a certain IP family is already preferred.  This shorter connection
// timeout allows us to find out more quickly that e.g. an IPv6 host is no longer
// available and let us try an IPv4 address, if provided for the host name.
// Set to '0' to use the default connection timeout.
pref("network.http.fallback-connection-timeout", 5);

// The number of seconds to allow active connections to prove that they have
// traffic before considered stalled, after a network change has been detected
// and signalled.
pref("network.http.network-changed.timeout", 5);

// The maximum number of current global half open sockets allowable
// when starting a new speculative connection.
#ifdef ANDROID
  pref("network.http.speculative-parallel-limit", 6);
#else
  pref("network.http.speculative-parallel-limit", 20);
#endif

// Whether or not to block requests for non head js/css items (e.g. media)
// while those elements load.
pref("network.http.rendering-critical-requests-prioritization", true);

// Disable IPv6 for backup connections to workaround problems about broken
// IPv6 connectivity.
pref("network.http.fast-fallback-to-IPv4", true);

// Http3 qpack table size.
pref("network.http.http3.default-qpack-table-size", 65536); // 64k
// Maximal number of streams that can be blocked on waiting for qpack
// instructions.
pref("network.http.http3.default-max-stream-blocked", 20);


// This is only for testing!
// This adds alt-svc mapping and it has a form of <host-name>;<alt-svc-header>
// Example: example1.com;h3=":443",example2.com;h3=":443"
pref("network.http.http3.alt-svc-mapping-for-testing", "");

// alt-svc allows separation of transport routing from
// the origin host without using a proxy.
pref("network.http.altsvc.enabled", true);
pref("network.http.altsvc.oe", false);

pref("network.http.diagnostics", false);

pref("network.http.pacing.requests.enabled", true);
pref("network.http.pacing.requests.min-parallelism", 6);
pref("network.http.pacing.requests.hz", 80);
pref("network.http.pacing.requests.burst", 10);

// TCP Keepalive config for HTTP connections.
pref("network.http.tcp_keepalive.short_lived_connections", true);
// Max time from initial request during which conns are considered short-lived.
pref("network.http.tcp_keepalive.short_lived_time", 60);
// Idle time of TCP connection until first keepalive probe sent.
pref("network.http.tcp_keepalive.short_lived_idle_time", 10);

pref("network.http.tcp_keepalive.long_lived_connections", true);
pref("network.http.tcp_keepalive.long_lived_idle_time", 600);

pref("network.http.enforce-framing.http1", false); // should be named "strict"
pref("network.http.enforce-framing.soft", true);
pref("network.http.enforce-framing.strict_chunked_encoding", true);

// The ratio of the transaction count for the focused window and the count of
// all available active connections.
pref("network.http.focused_window_transaction_ratio", "0.9");

// This is the size of the flow control window (KB) (i.e., the amount of data
// that the parent can send to the child before getting an ack). 0 for disable
// the flow control.
pref("network.http.send_window_size", 1024);

// Whether or not we give more priority to active tab.
// Note that this requires restart for changes to take effect.
#ifdef ANDROID
  // disabled because of bug 1382274
  pref("network.http.active_tab_priority", false);
#else
  pref("network.http.active_tab_priority", true);
#endif

// By default the Accept header sent for documents loaded over HTTP(S) is derived
// by DocumentAcceptHeader() in nsHttpHandler.cpp. If set, this pref overrides it.
// There is also image.http.accept which works in scope of image.
pref("network.http.accept", "");

// The max time to spend on xpcom events between two polls in ms.
pref("network.sts.max_time_for_events_between_two_polls", 100);

// The number of seconds we don't let poll() handing indefinitely after network
// link change has been detected so we can detect breakout of the pollable event.
// Expected in seconds, 0 to disable.
pref("network.sts.poll_busy_wait_period", 50);

// The number of seconds we cap poll() timeout to during the network link change
// detection period.
// Expected in seconds, 0 to disable.
pref("network.sts.poll_busy_wait_period_timeout", 7);

// During shutdown we limit PR_Close calls. If time exceeds this pref (in ms)
// let sockets just leak.
pref("network.sts.max_time_for_pr_close_during_shutdown", 5000);

// When the polling socket pair we use to wake poll() up on demand doesn't
// get signalled (is not readable) within this timeout, we try to repair it.
// This timeout can be disabled by setting this pref to 0.
// The value is expected in seconds.
pref("network.sts.pollable_event_timeout", 6);

// 2147483647 == PR_INT32_MAX == ~2 GB
pref("network.websocket.max-message-size", 2147483647);

// the number of seconds to wait for websocket connection to be opened
pref("network.websocket.timeout.open", 20);

// the number of seconds to wait for a clean close after sending the client
// close message
pref("network.websocket.timeout.close", 20);

// the number of seconds of idle read activity to sustain before sending a
// ping probe. 0 to disable.
pref("network.websocket.timeout.ping.request", 0);

// the deadline, expressed in seconds, for some read activity to occur after
// generating a ping. If no activity happens then an error and unclean close
// event is sent to the javascript websockets application
pref("network.websocket.timeout.ping.response", 10);

// the maximum number of concurrent websocket sessions. By specification there
// is never more than one handshake oustanding to an individual host at
// one time.
pref("network.websocket.max-connections", 200);

// by default scripts loaded from a https:// origin can only open secure
// (i.e. wss://) websockets.
pref("network.websocket.allowInsecureFromHTTPS", false);

// by default we delay websocket reconnects to same host/port if previous
// connection failed, per RFC 6455 section 7.2.3
pref("network.websocket.delay-failed-reconnects", true);

// </ws>

// enables the prefetch service (i.e., prefetching of <link rel="next"> and
// <link rel="prefetch"> URLs).
pref("network.prefetch-next", true);

// The following prefs pertain to the negotiate-auth extension (see bug 17578),
// which provides transparent Kerberos or NTLM authentication using the SPNEGO
// protocol.  Each pref is a comma-separated list of keys, where each key has
// the format:
//   [scheme "://"] [host [":" port]]
// For example, "foo.com" would match "http://www.foo.com/bar", etc.

// This list controls which URIs can use the negotiate-auth protocol.  This
// list should be limited to the servers you know you'll need to login to.
pref("network.negotiate-auth.trusted-uris", "");
// This list controls which URIs can support delegation.
pref("network.negotiate-auth.delegation-uris", "");

// Do not allow SPNEGO by default when challenged by a local server.
pref("network.negotiate-auth.allow-non-fqdn", false);

// Allow SPNEGO by default when challenged by a proxy server.
pref("network.negotiate-auth.allow-proxies", true);

// Path to a specific gssapi library
pref("network.negotiate-auth.gsslib", "");

// Specify if the gss lib comes standard with the OS
pref("network.negotiate-auth.using-native-gsslib", true);

#ifdef XP_WIN
  // Default to using the SSPI intead of GSSAPI on windows
  pref("network.auth.use-sspi", true);
#endif

// Controls which NTLM authentication implementation we default to. True forces
// the use of our generic (internal) NTLM authentication implementation vs. any
// native implementation provided by the os. This pref is for diagnosing issues
// with native NTLM. (See bug 520607 for details.) Using generic NTLM authentication
// can expose the user to reflection attack vulnerabilities. Do not change this
// unless you know what you're doing!
// This pref should be removed 6 months after the release of firefox 3.6.
pref("network.auth.force-generic-ntlm", false);

// The following prefs are used to enable automatic use of the operating
// system's NTLM implementation to silently authenticate the user with their
// Window's domain logon.  The trusted-uris pref follows the format of the
// trusted-uris pref for negotiate authentication.
pref("network.automatic-ntlm-auth.allow-proxies", true);
pref("network.automatic-ntlm-auth.allow-non-fqdn", false);
pref("network.automatic-ntlm-auth.trusted-uris", "");

// The string to return to the server as the 'workstation' that the
// user is using.  Bug 1046421 notes that the previous default, of the
// system hostname, could be used for user fingerprinting.
//
// However, in some network environments where allowedWorkstations is in use
// to provide a level of host-based access control, it must be set to a string
// that is listed in allowedWorkstations for the user's account in their
// AD Domain.
pref("network.generic-ntlm-auth.workstation", "WORKSTATION");

// This preference controls whether to allow sending default credentials (SSO) to
// NTLM/Negotiate servers allowed in the "trusted uri" list when navigating them
// in a Private Browsing window.
// If set to false, Private Browsing windows will not use default credentials and ask
// for credentials from the user explicitly.
// If set to true, and a server URL conforms other conditions for sending default
// credentials, those will be sent automatically in Private Browsing windows.
//
// This preference has no effect when the browser is set to "Never Remember History",
// in that case default credentials will always be used.
pref("network.auth.private-browsing-sso", false);

// Control how throttling of http responses works - number of ms that each
// suspend and resume period lasts (prefs named appropriately)
// This feature is occasionally causing visible regressions (download too slow for
// too long time, jitter in video/audio in background tabs...)
pref("network.http.throttle.enable", false);

// V1 prefs
pref("network.http.throttle.suspend-for", 900);
pref("network.http.throttle.resume-for", 100);

// Common prefs
// Delay we resume throttled background responses after the last unthrottled
// response has finished.  Prevents resuming too soon during an active page load
// at which sub-resource reqeusts quickly come and go.
pref("network.http.throttle.hold-time-ms", 800);
// After the last transaction activation or last data chunk response we only
// throttle for this period of time.  This prevents comet and unresponsive
// http requests to engage long-standing throttling.
pref("network.http.throttle.max-time-ms", 500);

// Give higher priority to requests resulting from a user interaction event
// like click-to-play, image fancy-box zoom, navigation.
pref("network.http.on_click_priority", true);

pref("network.proxy.http",                  "");
pref("network.proxy.http_port",             0);
pref("network.proxy.ssl",                   "");
pref("network.proxy.ssl_port",              0);
pref("network.proxy.socks",                 "");
pref("network.proxy.socks_port",            0);
pref("network.proxy.socks_version",         5);
pref("network.proxy.proxy_over_tls",        true);
pref("network.proxy.no_proxies_on",         "");
pref("network.proxy.failover_timeout",      1800); // 30 minutes
pref("network.online",                      true); //online/offline

// This pref contains the list of hostnames (such as
// "mozilla.org,example.net"). For these hosts, firefox will treat
// SameSite=None if nothing else is specified, even if
// network.cookie.sameSite.laxByDefault if set to true.
// To know the correct syntax, see nsContentUtils::IsURIInList()
pref("network.cookie.sameSite.laxByDefault.disabledHosts", "");

pref("network.cookie.maxNumber", 3000);
pref("network.cookie.maxPerHost", 180);
// Cookies quota for each host. If cookies exceed the limit maxPerHost,
// we evict cookies until we have the quota amount.
pref("network.cookie.quotaPerHost", 150);

// The PAC file to load.  Ignored unless network.proxy.type is 2.
pref("network.proxy.autoconfig_url", "");
// Strip off paths when sending URLs to PAC scripts
pref("network.proxy.autoconfig_url.include_path", false);

// If we cannot load the PAC file, then try again (doubling from interval_min
// until we reach interval_max or the PAC file is successfully loaded).
pref("network.proxy.autoconfig_retry_interval_min", 5);    // 5 seconds
pref("network.proxy.autoconfig_retry_interval_max", 300);  // 5 minutes
pref("network.proxy.enable_wpad_over_dhcp", true);

pref("converter.html2txt.structs",          true); // Output structured phrases (strong, em, code, sub, sup, b, i, u)
pref("converter.html2txt.header_strategy",  1); // 0 = no indention; 1 = indention, increased with header level; 2 = numbering and slight indention

pref("intl.accept_languages",               "chrome://global/locale/intl.properties");
pref("intl.menuitems.alwaysappendaccesskeys","chrome://global/locale/intl.properties");
pref("intl.menuitems.insertseparatorbeforeaccesskeys","chrome://global/locale/intl.properties");
pref("intl.ellipsis",                       "chrome://global-platform/locale/intl.properties");
// this pref allows user to request that all internationalization formatters
// like date/time formatting, unit formatting, calendars etc. should use
// OS locale set instead of the app locale set.
pref("intl.regional_prefs.use_os_locales",  false);
pref("font.language.group",                 "chrome://global/locale/intl.properties");
pref("font.cjk_pref_fallback_order",        "zh-cn,zh-hk,zh-tw,ja,ko");

// This pref controls pseudolocales for testing localization.
// See https://firefox-source-docs.mozilla.org/l10n/fluent/tutorial.html#manually-testing-ui-with-pseudolocalization
pref("intl.l10n.pseudo", "");

// use en-US hyphenation by default for content tagged with plain lang="en"
pref("intl.hyphenation-alias.en", "en-us");
// and for other subtags of en-*, if no specific patterns are available
pref("intl.hyphenation-alias.en-*", "en-us");

pref("intl.hyphenation-alias.af-*", "af");
pref("intl.hyphenation-alias.bg-*", "bg");
pref("intl.hyphenation-alias.bn-*", "bn");
pref("intl.hyphenation-alias.ca-*", "ca");
pref("intl.hyphenation-alias.cs-*", "cs");
pref("intl.hyphenation-alias.cy-*", "cy");
pref("intl.hyphenation-alias.da-*", "da");
pref("intl.hyphenation-alias.eo-*", "eo");
pref("intl.hyphenation-alias.es-*", "es");
pref("intl.hyphenation-alias.et-*", "et");
pref("intl.hyphenation-alias.fi-*", "fi");
pref("intl.hyphenation-alias.fr-*", "fr");
pref("intl.hyphenation-alias.gl-*", "gl");
pref("intl.hyphenation-alias.gu-*", "gu");
pref("intl.hyphenation-alias.hi-*", "hi");
pref("intl.hyphenation-alias.hr-*", "hr");
pref("intl.hyphenation-alias.hsb-*", "hsb");
pref("intl.hyphenation-alias.hu-*", "hu");
pref("intl.hyphenation-alias.ia-*", "ia");
pref("intl.hyphenation-alias.is-*", "is");
pref("intl.hyphenation-alias.it-*", "it");
pref("intl.hyphenation-alias.kmr-*", "kmr");
pref("intl.hyphenation-alias.kn-*", "kn");
pref("intl.hyphenation-alias.la-*", "la");
pref("intl.hyphenation-alias.lt-*", "lt");
pref("intl.hyphenation-alias.ml-*", "ml");
pref("intl.hyphenation-alias.mn-*", "mn");
pref("intl.hyphenation-alias.nl-*", "nl");
pref("intl.hyphenation-alias.or-*", "or");
pref("intl.hyphenation-alias.pa-*", "pa");
pref("intl.hyphenation-alias.pl-*", "pl");
pref("intl.hyphenation-alias.pt-*", "pt");
pref("intl.hyphenation-alias.ru-*", "ru");
pref("intl.hyphenation-alias.sl-*", "sl");
pref("intl.hyphenation-alias.sv-*", "sv");
pref("intl.hyphenation-alias.ta-*", "ta");
pref("intl.hyphenation-alias.te-*", "te");
pref("intl.hyphenation-alias.tr-*", "tr");
pref("intl.hyphenation-alias.uk-*", "uk");

// Assamese and Marathi use the same patterns as Bengali and Hindi respectively
pref("intl.hyphenation-alias.as", "bn");
pref("intl.hyphenation-alias.as-*", "bn");
pref("intl.hyphenation-alias.mr", "hi");
pref("intl.hyphenation-alias.mr-*", "hi");

// Czech patterns are also used for Slovak
pref("intl.hyphenation-alias.sk", "cs");
pref("intl.hyphenation-alias.sk-*", "cs");

// use reformed (1996) German patterns by default unless specifically tagged as de-1901
// (these prefs may soon be obsoleted by better BCP47-based tag matching, but for now...)
pref("intl.hyphenation-alias.de", "de-1996");
pref("intl.hyphenation-alias.de-*", "de-1996");
pref("intl.hyphenation-alias.de-AT-1901", "de-1901");
pref("intl.hyphenation-alias.de-DE-1901", "de-1901");
pref("intl.hyphenation-alias.de-CH-*", "de-CH");

// patterns from TeX are tagged as "sh" (Serbo-Croatian) macrolanguage;
// alias "sr" (Serbian) and "bs" (Bosnian) to those patterns
// (Croatian has its own separate patterns).
pref("intl.hyphenation-alias.sr", "sh");
pref("intl.hyphenation-alias.bs", "sh");
pref("intl.hyphenation-alias.sh-*", "sh");
pref("intl.hyphenation-alias.sr-*", "sh");
pref("intl.hyphenation-alias.bs-*", "sh");

// Norwegian has two forms, Bokmål and Nynorsk, with "no" as a macrolanguage encompassing both.
// For "no", we'll alias to "nb" (Bokmål) as that is the more widely used written form.
pref("intl.hyphenation-alias.no", "nb");
pref("intl.hyphenation-alias.no-*", "nb");
pref("intl.hyphenation-alias.nb-*", "nb");
pref("intl.hyphenation-alias.nn-*", "nn");

// In German, where all proper nouns are capitalized, we allow hyphenation of
// capitalized words.
pref("intl.hyphenate-capitalized.de-1996", true);
pref("intl.hyphenate-capitalized.de-1901", true);
pref("intl.hyphenate-capitalized.de-CH", true);

// Also allow hyphenation of capitalized words in some languages that tend to
// have a a lot of long compound words.
// (Should this be extended to other languages? Should the default be changed?)
pref("intl.hyphenate-capitalized.af", true);
pref("intl.hyphenate-capitalized.fi", true);
pref("intl.hyphenate-capitalized.nl", true);

// All prefs of default font should be "auto".
pref("font.name.serif.ar", "");
pref("font.name.sans-serif.ar", "");
pref("font.name.monospace.ar", "");
pref("font.name.cursive.ar", "");

pref("font.name.serif.el", "");
pref("font.name.sans-serif.el", "");
pref("font.name.monospace.el", "");
pref("font.name.cursive.el", "");

pref("font.name.serif.he", "");
pref("font.name.sans-serif.he", "");
pref("font.name.monospace.he", "");
pref("font.name.cursive.he", "");

pref("font.name.serif.ja", "");
pref("font.name.sans-serif.ja", "");
pref("font.name.monospace.ja", "");
pref("font.name.cursive.ja", "");

pref("font.name.serif.ko", "");
pref("font.name.sans-serif.ko", "");
pref("font.name.monospace.ko", "");
pref("font.name.cursive.ko", "");

pref("font.name.serif.th", "");
pref("font.name.sans-serif.th", "");
pref("font.name.monospace.th", "");
pref("font.name.cursive.th", "");

pref("font.name.serif.x-cyrillic", "");
pref("font.name.sans-serif.x-cyrillic", "");
pref("font.name.monospace.x-cyrillic", "");
pref("font.name.cursive.x-cyrillic", "");

pref("font.name.serif.x-unicode", "");
pref("font.name.sans-serif.x-unicode", "");
pref("font.name.monospace.x-unicode", "");
pref("font.name.cursive.x-unicode", "");

pref("font.name.serif.x-western", "");
pref("font.name.sans-serif.x-western", "");
pref("font.name.monospace.x-western", "");
pref("font.name.cursive.x-western", "");

pref("font.name.serif.zh-CN", "");
pref("font.name.sans-serif.zh-CN", "");
pref("font.name.monospace.zh-CN", "");
pref("font.name.cursive.zh-CN", "");

pref("font.name.serif.zh-TW", "");
pref("font.name.sans-serif.zh-TW", "");
pref("font.name.monospace.zh-TW", "");
pref("font.name.cursive.zh-TW", "");

pref("font.name.serif.zh-HK", "");
pref("font.name.sans-serif.zh-HK", "");
pref("font.name.monospace.zh-HK", "");
pref("font.name.cursive.zh-HK", "");

pref("font.name.serif.x-devanagari", "");
pref("font.name.sans-serif.x-devanagari", "");
pref("font.name.monospace.x-devanagari", "");
pref("font.name.cursive.x-devanagari", "");

pref("font.name.serif.x-tamil", "");
pref("font.name.sans-serif.x-tamil", "");
pref("font.name.monospace.x-tamil", "");
pref("font.name.cursive.x-tamil", "");

pref("font.name.serif.x-armn", "");
pref("font.name.sans-serif.x-armn", "");
pref("font.name.monospace.x-armn", "");
pref("font.name.cursive.x-armn", "");

pref("font.name.serif.x-beng", "");
pref("font.name.sans-serif.x-beng", "");
pref("font.name.monospace.x-beng", "");
pref("font.name.cursive.x-beng", "");

pref("font.name.serif.x-cans", "");
pref("font.name.sans-serif.x-cans", "");
pref("font.name.monospace.x-cans", "");
pref("font.name.cursive.x-cans", "");

pref("font.name.serif.x-ethi", "");
pref("font.name.sans-serif.x-ethi", "");
pref("font.name.monospace.x-ethi", "");
pref("font.name.cursive.x-ethi", "");

pref("font.name.serif.x-geor", "");
pref("font.name.sans-serif.x-geor", "");
pref("font.name.monospace.x-geor", "");
pref("font.name.cursive.x-geor", "");

pref("font.name.serif.x-gujr", "");
pref("font.name.sans-serif.x-gujr", "");
pref("font.name.monospace.x-gujr", "");
pref("font.name.cursive.x-gujr", "");

pref("font.name.serif.x-guru", "");
pref("font.name.sans-serif.x-guru", "");
pref("font.name.monospace.x-guru", "");
pref("font.name.cursive.x-guru", "");

pref("font.name.serif.x-khmr", "");
pref("font.name.sans-serif.x-khmr", "");
pref("font.name.monospace.x-khmr", "");
pref("font.name.cursive.x-khmr", "");

pref("font.name.serif.x-mlym", "");
pref("font.name.sans-serif.x-mlym", "");
pref("font.name.monospace.x-mlym", "");
pref("font.name.cursive.x-mlym", "");

pref("font.name.serif.x-orya", "");
pref("font.name.sans-serif.x-orya", "");
pref("font.name.monospace.x-orya", "");
pref("font.name.cursive.x-orya", "");

pref("font.name.serif.x-telu", "");
pref("font.name.sans-serif.x-telu", "");
pref("font.name.monospace.x-telu", "");
pref("font.name.cursive.x-telu", "");

pref("font.name.serif.x-knda", "");
pref("font.name.sans-serif.x-knda", "");
pref("font.name.monospace.x-knda", "");
pref("font.name.cursive.x-knda", "");

pref("font.name.serif.x-sinh", "");
pref("font.name.sans-serif.x-sinh", "");
pref("font.name.monospace.x-sinh", "");
pref("font.name.cursive.x-sinh", "");

pref("font.name.serif.x-tibt", "");
pref("font.name.sans-serif.x-tibt", "");
pref("font.name.monospace.x-tibt", "");
pref("font.name.cursive.x-tibt", "");

pref("font.name.serif.x-math", "");
pref("font.name.sans-serif.x-math", "");
pref("font.name.monospace.x-math", "");
pref("font.name.cursive.x-math", "");

pref("font.name-list.serif.x-math", "Latin Modern Math, STIX Two Math, XITS Math, Cambria Math, Libertinus Math, DejaVu Math TeX Gyre, TeX Gyre Bonum Math, TeX Gyre Pagella Math, TeX Gyre Schola, TeX Gyre Termes Math, STIX Math, Asana Math, STIXGeneral, DejaVu Serif, DejaVu Sans, serif");
pref("font.name-list.sans-serif.x-math", "sans-serif");
pref("font.name-list.monospace.x-math", "monospace");

// Some CJK fonts have bad underline offset, their CJK character glyphs are overlapped (or adjoined)  to its underline.
// These fonts are ignored the underline offset, instead of it, the underline is lowered to bottom of its em descent.
pref("font.blacklist.underline_offset", "FangSong,Gulim,GulimChe,MingLiU,MingLiU-ExtB,MingLiU_HKSCS,MingLiU-HKSCS-ExtB,MS Gothic,MS Mincho,MS PGothic,MS PMincho,MS UI Gothic,PMingLiU,PMingLiU-ExtB,SimHei,SimSun,SimSun-ExtB,Hei,Kai,Apple LiGothic,Apple LiSung,Osaka");

// security-sensitive dialogs should delay button enabling. In milliseconds.
pref("security.dialog_enable_delay", 1000);
pref("security.notification_enable_delay", 500);

#ifdef EARLY_BETA_OR_EARLIER
  // Disallow web documents loaded with the SystemPrincipal
  pref("security.disallow_non_local_systemprincipal_in_tests", false);
#endif

// Insecure Form Field Warning
pref("security.insecure_field_warning.ignore_local_ip_address", true);

// Remote settings preferences
pref("services.settings.poll_interval", 86400); // 24H

// The percentage of clients who will report uptake telemetry as
// events instead of just a histogram. This only applies on Release;
// other channels always report events.
pref("services.common.uptake.sampleRate", 1);   // 1%

pref("extensions.abuseReport.enabled", false);
// Whether Firefox integrated abuse reporting feature should be opening the new abuse report form hosted on AMO.
pref("extensions.abuseReport.amoFormURL", "https://addons.mozilla.org/%LOCALE%/firefox/feedback/addon/%addonID%/");
pref("extensions.addonAbuseReport.url", "https://services.addons.mozilla.org/api/v5/abuse/report/addon/");

// Blocklist preferences
pref("extensions.blocklist.enabled", true);
pref("extensions.blocklist.detailsURL", "https://blocked.cdn.mozilla.net/");
pref("extensions.blocklist.itemURL", "https://blocked.cdn.mozilla.net/%blockID%.html");
pref("extensions.blocklist.addonItemURL", "https://addons.mozilla.org/%LOCALE%/firefox/blocked-addon/%addonID%/%addonVersion%/");
// Controls what level the blocklist switches from warning about items to forcibly
// blocking them.
pref("extensions.blocklist.level", 2);
// Whether event pages should be enabled for "manifest_version: 2" extensions.
pref("extensions.eventPages.enabled", true);
// Whether MV3 restrictions for actions popup urls should be extended to MV2 extensions
// (only allowing same extension urls to be used as action popup urls).
pref("extensions.manifestV2.actionsPopupURLRestricted", false);
// Whether "manifest_version: 3" extensions should be allowed to install successfully.
pref("extensions.manifestV3.enabled", true);
#ifndef MOZ_WEBEXT_WEBIDL_ENABLED
  // Defined in StaticPrefList.yaml but overridden here to lock it.
  pref("extensions.backgroundServiceWorker.enabled", false, locked);
#endif
// Whether to enable the updated openPopup API.
#ifdef NIGHTLY_BUILD
  pref("extensions.openPopupWithoutUserGesture.enabled", true);
#else
  pref("extensions.openPopupWithoutUserGesture.enabled", false);
#endif
// Install origins restriction.
pref("extensions.install_origins.enabled", false);

// TODO: bug 1830712: remove prefs related to browser_style deprecation.
pref("extensions.browser_style_mv3.supported", false);
pref("extensions.browser_style_mv3.same_as_mv2", false);

// Experimental Inference API
#ifdef NIGHTLY_BUILD
  pref("extensions.ml.enabled", true);
#else
  pref("extensions.ml.enabled", false);
#endif

// Middle-mouse handling
pref("middlemouse.paste", false);
pref("middlemouse.contentLoadURL", false);
pref("middlemouse.scrollbarPosition", false);

#if defined(XP_WIN) || defined(XP_MACOSX) || defined(MOZ_WIDGET_GTK)
  // Setting false you can disable 4th button and/or 5th button of your mouse.
  // 4th button is typically mapped to "Back" and 5th button is typically mapped
  // to "Forward" button.
  pref("mousebutton.4th.enabled", true);
  pref("mousebutton.5th.enabled", true);
#endif

// mousewheel.*.action can specify the action when you use mosue wheel.
// When no modifier keys are pressed or two or more modifires are pressed,
// .default is used.
// 0: Nothing happens
// 1: Scrolling contents
// 2: Go back or go forward, in your history
// 3: Zoom in or out (reflowing zoom).
// 4: Treat vertical wheel as horizontal scroll
//      This treats vertical wheel operation (i.e., deltaY) as horizontal
//      scroll.  deltaX and deltaZ are always ignored.  So, only
//      "delta_multiplier_y" pref affects the scroll speed.
// 5: Zoom in or out (pinch zoom).
pref("mousewheel.default.action", 1);
pref("mousewheel.with_alt.action", 2);
pref("mousewheel.with_control.action", 3);
// Command key on Mac, Windows log key on Windows and Linux
pref("mousewheel.with_meta.action", 1);
pref("mousewheel.with_shift.action", 4);

// mousewheel.*.action.override_x will override the action
// when the mouse wheel is rotated along the x direction.
// -1: Don't override the action.
// 0 to 3: Override the action with the specified value.
// Note that 4 isn't available because it doesn't make sense to apply the
// default action only for y direction to this pref.
pref("mousewheel.default.action.override_x", -1);
pref("mousewheel.with_alt.action.override_x", -1);
pref("mousewheel.with_control.action.override_x", -1);
// Command key on Mac, Windows log key on Windows and Linux
pref("mousewheel.with_meta.action.override_x", -1);
pref("mousewheel.with_shift.action.override_x", -1);

// mousewheel.*.delta_multiplier_* can specify the value muliplied by the delta
// value.  The values will be used after divided by 100.  I.e., 100 means 1.0,
// -100 means -1.0.  If the values were negative, the direction would be
// reverted.  The absolue value must be 100 or larger.
pref("mousewheel.default.delta_multiplier_x", 100);
pref("mousewheel.default.delta_multiplier_y", 100);
pref("mousewheel.default.delta_multiplier_z", 100);
pref("mousewheel.with_alt.delta_multiplier_x", 100);
pref("mousewheel.with_alt.delta_multiplier_y", 100);
pref("mousewheel.with_alt.delta_multiplier_z", 100);
pref("mousewheel.with_control.delta_multiplier_x", 100);
pref("mousewheel.with_control.delta_multiplier_y", 100);
pref("mousewheel.with_control.delta_multiplier_z", 100);
// Command key on Mac, Windows log key on Windows and Linux
pref("mousewheel.with_meta.delta_multiplier_x", 100);
pref("mousewheel.with_meta.delta_multiplier_y", 100);
pref("mousewheel.with_meta.delta_multiplier_z", 100);
pref("mousewheel.with_shift.delta_multiplier_x", 100);
pref("mousewheel.with_shift.delta_multiplier_y", 100);
pref("mousewheel.with_shift.delta_multiplier_z", 100);

// enable single finger gesture input (win7+ tablets)
pref("gestures.enable_single_finger_input", true);

pref("dom.use_watchdog", true);

// Stop all scripts in a compartment when the "stop script" dialog is used.
pref("dom.global_stop_script", true);

// Enable multi by default.
#if !defined(MOZ_ASAN) && !defined(MOZ_TSAN)
  pref("dom.ipc.processCount", 8);
#elif defined(FUZZING_SNAPSHOT)
  pref("dom.ipc.processCount", 1);
#else
  pref("dom.ipc.processCount", 4);
#endif

// Default to allow only one file:// URL content process.
pref("dom.ipc.processCount.file", 1);

// WebExtensions only support a single extension process.
pref("dom.ipc.processCount.extension", 1);

// The privileged about process only supports a single content process.
pref("dom.ipc.processCount.privilegedabout", 1);

// Limit the privileged mozilla process to a single instance only
// to avoid multiple of these content processes
pref("dom.ipc.processCount.privilegedmozilla", 1);

// Maximum number of isolated content processes per-origin.
#ifdef ANDROID
pref("dom.ipc.processCount.webIsolated", 1);
#else
pref("dom.ipc.processCount.webIsolated", 4);
#endif

// For now we allow a single inference process
pref("dom.ipc.processCount.inference", 1);

// Keep a single privileged about process alive for performance reasons.
// e.g. we do not want to throw content processes out every time we navigate
// away from about:newtab.
pref("dom.ipc.keepProcessesAlive.privilegedabout", 1);

// Disable support for SVG
pref("svg.disabled", false);

// This pref will cause assertions when a remoteType triggers a process switch
// to a new remoteType it should not be able to trigger.
pref("browser.tabs.remote.enforceRemoteTypeRestrictions", false);

// Pref to control whether we use a separate privileged content process
// for about: pages. This pref name did not age well: we will have multiple
// types of privileged content processes, each with different privileges.
pref("browser.tabs.remote.separatePrivilegedContentProcess", false);

// The domains we will isolate into the Mozilla Content Process. Comma-separated
// full domains: any subdomains of the domains listed will also be allowed.
pref("browser.tabs.remote.separatedMozillaDomains", "addons.mozilla.org,accounts.firefox.com");

// Default font types and sizes by locale
pref("font.default.ar", "sans-serif");
pref("font.minimum-size.ar", 0);
pref("font.size.variable.ar", 16);
pref("font.size.monospace.ar", 13);

pref("font.default.el", "serif");
pref("font.minimum-size.el", 0);
pref("font.size.variable.el", 16);
pref("font.size.monospace.el", 13);

pref("font.default.he", "sans-serif");
pref("font.minimum-size.he", 0);
pref("font.size.variable.he", 16);
pref("font.size.monospace.he", 13);

pref("font.default.ja", "sans-serif");
pref("font.minimum-size.ja", 0);
pref("font.size.variable.ja", 16);
pref("font.size.monospace.ja", 16);

pref("font.default.ko", "sans-serif");
pref("font.minimum-size.ko", 0);
pref("font.size.variable.ko", 16);
pref("font.size.monospace.ko", 16);

pref("font.default.th", "sans-serif");
pref("font.minimum-size.th", 0);
pref("font.size.variable.th", 16);
pref("font.size.monospace.th", 13);

pref("font.default.x-cyrillic", "serif");
pref("font.minimum-size.x-cyrillic", 0);
pref("font.size.variable.x-cyrillic", 16);
pref("font.size.monospace.x-cyrillic", 13);

pref("font.default.x-devanagari", "serif");
pref("font.minimum-size.x-devanagari", 0);
pref("font.size.variable.x-devanagari", 16);
pref("font.size.monospace.x-devanagari", 13);

pref("font.default.x-tamil", "serif");
pref("font.minimum-size.x-tamil", 0);
pref("font.size.variable.x-tamil", 16);
pref("font.size.monospace.x-tamil", 13);

pref("font.default.x-armn", "serif");
pref("font.minimum-size.x-armn", 0);
pref("font.size.variable.x-armn", 16);
pref("font.size.monospace.x-armn", 13);

pref("font.default.x-beng", "serif");
pref("font.minimum-size.x-beng", 0);
pref("font.size.variable.x-beng", 16);
pref("font.size.monospace.x-beng", 13);

pref("font.default.x-cans", "serif");
pref("font.minimum-size.x-cans", 0);
pref("font.size.variable.x-cans", 16);
pref("font.size.monospace.x-cans", 13);

pref("font.default.x-ethi", "serif");
pref("font.minimum-size.x-ethi", 0);
pref("font.size.variable.x-ethi", 16);
pref("font.size.monospace.x-ethi", 13);

pref("font.default.x-geor", "serif");
pref("font.minimum-size.x-geor", 0);
pref("font.size.variable.x-geor", 16);
pref("font.size.monospace.x-geor", 13);

pref("font.default.x-gujr", "serif");
pref("font.minimum-size.x-gujr", 0);
pref("font.size.variable.x-gujr", 16);
pref("font.size.monospace.x-gujr", 13);

pref("font.default.x-guru", "serif");
pref("font.minimum-size.x-guru", 0);
pref("font.size.variable.x-guru", 16);
pref("font.size.monospace.x-guru", 13);

pref("font.default.x-khmr", "serif");
pref("font.minimum-size.x-khmr", 0);
pref("font.size.variable.x-khmr", 16);
pref("font.size.monospace.x-khmr", 13);

pref("font.default.x-mlym", "serif");
pref("font.minimum-size.x-mlym", 0);
pref("font.size.variable.x-mlym", 16);
pref("font.size.monospace.x-mlym", 13);

pref("font.default.x-orya", "serif");
pref("font.minimum-size.x-orya", 0);
pref("font.size.variable.x-orya", 16);
pref("font.size.monospace.x-orya", 13);

pref("font.default.x-telu", "serif");
pref("font.minimum-size.x-telu", 0);
pref("font.size.variable.x-telu", 16);
pref("font.size.monospace.x-telu", 13);

pref("font.default.x-knda", "serif");
pref("font.minimum-size.x-knda", 0);
pref("font.size.variable.x-knda", 16);
pref("font.size.monospace.x-knda", 13);

pref("font.default.x-sinh", "serif");
pref("font.minimum-size.x-sinh", 0);
pref("font.size.variable.x-sinh", 16);
pref("font.size.monospace.x-sinh", 13);

pref("font.default.x-tibt", "serif");
pref("font.minimum-size.x-tibt", 0);
pref("font.size.variable.x-tibt", 16);
pref("font.size.monospace.x-tibt", 13);

pref("font.default.x-unicode", "serif");
pref("font.minimum-size.x-unicode", 0);
pref("font.size.variable.x-unicode", 16);
pref("font.size.monospace.x-unicode", 13);

pref("font.default.x-western", "serif");
pref("font.minimum-size.x-western", 0);
pref("font.size.variable.x-western", 16);
pref("font.size.monospace.x-western", 13);

pref("font.default.zh-CN", "sans-serif");
pref("font.minimum-size.zh-CN", 0);
pref("font.size.variable.zh-CN", 16);
pref("font.size.monospace.zh-CN", 16);

pref("font.default.zh-HK", "sans-serif");
pref("font.minimum-size.zh-HK", 0);
pref("font.size.variable.zh-HK", 16);
pref("font.size.monospace.zh-HK", 16);

pref("font.default.zh-TW", "sans-serif");
pref("font.minimum-size.zh-TW", 0);
pref("font.size.variable.zh-TW", 16);
pref("font.size.monospace.zh-TW", 16);

// mathml.css sets font-size to "inherit" and font-family to "serif" so only
// font.name.*.x-math and font.minimum-size.x-math are really relevant.
pref("font.default.x-math", "serif");
pref("font.minimum-size.x-math", 0);
pref("font.size.variable.x-math", 16);
pref("font.size.monospace.x-math", 13);

#ifdef XP_WIN

  pref("font.name-list.emoji", "Segoe UI Emoji, Twemoji Mozilla");

  pref("font.name-list.serif.ar", "Times New Roman");
  pref("font.name-list.sans-serif.ar", "Segoe UI, Tahoma, Arial");
  pref("font.name-list.monospace.ar", "Consolas");
  pref("font.name-list.cursive.ar", "Comic Sans MS");

  pref("font.name-list.serif.el", "Times New Roman");
  pref("font.name-list.sans-serif.el", "Arial");
  pref("font.name-list.monospace.el", "Consolas");
  pref("font.name-list.cursive.el", "Comic Sans MS");

  pref("font.name-list.serif.he", "Narkisim, David");
  pref("font.name-list.sans-serif.he", "Arial");
  pref("font.name-list.monospace.he", "Fixed Miriam Transparent, Miriam Fixed, Rod, Consolas, Courier New");
  pref("font.name-list.cursive.he", "Guttman Yad, Ktav, Arial");

  pref("font.name-list.serif.ja", "Yu Mincho, MS PMincho, MS Mincho, Meiryo, Yu Gothic, MS PGothic, MS Gothic");
  pref("font.name-list.sans-serif.ja", "Meiryo, Yu Gothic, MS PGothic, MS Gothic, Yu Mincho, MS PMincho, MS Mincho");
  pref("font.name-list.monospace.ja", "MS Gothic, MS Mincho, Meiryo, Yu Gothic, Yu Mincho, MS PGothic, MS PMincho");

  pref("font.name-list.serif.ko", "Batang, Gulim");
  pref("font.name-list.sans-serif.ko", "Malgun Gothic, Gulim");
  pref("font.name-list.monospace.ko", "GulimChe");
  pref("font.name-list.cursive.ko", "Gungsuh");

  pref("font.name-list.serif.th", "Tahoma");
  pref("font.name-list.sans-serif.th", "Tahoma");
  pref("font.name-list.monospace.th", "Tahoma");
  pref("font.name-list.cursive.th", "Tahoma");

  pref("font.name-list.serif.x-cyrillic", "Times New Roman");
  pref("font.name-list.sans-serif.x-cyrillic", "Arial");
  pref("font.name-list.monospace.x-cyrillic", "Consolas");
  pref("font.name-list.cursive.x-cyrillic", "Comic Sans MS");

  pref("font.name-list.serif.x-unicode", "Times New Roman");
  pref("font.name-list.sans-serif.x-unicode", "Arial");
  pref("font.name-list.monospace.x-unicode", "Consolas");
  pref("font.name-list.cursive.x-unicode", "Comic Sans MS");

  pref("font.name-list.serif.x-western", "Times New Roman");
  pref("font.name-list.sans-serif.x-western", "Arial");
  pref("font.name-list.monospace.x-western", "Consolas");
  pref("font.name-list.cursive.x-western", "Comic Sans MS");

  pref("font.name-list.serif.zh-CN", "SimSun, MS Song, SimSun-ExtB");
  pref("font.name-list.sans-serif.zh-CN", "Microsoft YaHei, SimHei");
  pref("font.name-list.monospace.zh-CN", "SimSun, MS Song, SimSun-ExtB");
  pref("font.name-list.cursive.zh-CN", "KaiTi, KaiTi_GB2312");

  // Per Taiwanese users' demand. They don't want to use TC fonts for
  // rendering Latin letters. (bug 88579)
  pref("font.name-list.serif.zh-TW", "Times New Roman, PMingLiu, MingLiU, MingLiU-ExtB");
  pref("font.name-list.sans-serif.zh-TW", "Arial, Microsoft JhengHei, PMingLiU, MingLiU, MingLiU-ExtB");
  pref("font.name-list.monospace.zh-TW", "MingLiU, MingLiU-ExtB");
  pref("font.name-list.cursive.zh-TW", "DFKai-SB");

  // hkscsm3u.ttf (HKSCS-2001) :  http://www.microsoft.com/hk/hkscs
  // Hong Kong users have the same demand about glyphs for Latin letters (bug 88579)
  pref("font.name-list.serif.zh-HK", "Times New Roman, MingLiu_HKSCS, Ming(for ISO10646), MingLiU, MingLiU_HKSCS-ExtB, Microsoft JhengHei");
  pref("font.name-list.sans-serif.zh-HK", "Arial, MingLiU_HKSCS, Ming(for ISO10646), MingLiU, MingLiU_HKSCS-ExtB, Microsoft JhengHei");
  pref("font.name-list.monospace.zh-HK", "MingLiU_HKSCS, Ming(for ISO10646), MingLiU, MingLiU_HKSCS-ExtB, Microsoft JhengHei");
  pref("font.name-list.cursive.zh-HK", "DFKai-SB");

  pref("font.name-list.serif.x-devanagari", "Kokila, Raghindi");
  pref("font.name-list.sans-serif.x-devanagari", "Nirmala UI, Mangal");
  pref("font.name-list.monospace.x-devanagari", "Mangal, Nirmala UI");

  pref("font.name-list.serif.x-tamil", "Latha");
  pref("font.name-list.monospace.x-tamil", "Latha");

  // http://www.alanwood.net/unicode/fonts.html

  pref("font.name-list.serif.x-armn", "Sylfaen");
  pref("font.name-list.sans-serif.x-armn", "Arial AMU");
  pref("font.name-list.monospace.x-armn", "Arial AMU");

  pref("font.name-list.serif.x-beng", "Vrinda, Akaash, Likhan, Ekushey Punarbhaba");
  pref("font.name-list.sans-serif.x-beng", "Vrinda, Akaash, Likhan, Ekushey Punarbhaba");
  pref("font.name-list.monospace.x-beng", "Mitra Mono, Likhan, Mukti Narrow");

  pref("font.name-list.serif.x-cans", "Aboriginal Serif, BJCree Uni");
  pref("font.name-list.sans-serif.x-cans", "Aboriginal Sans");
  pref("font.name-list.monospace.x-cans", "Aboriginal Sans, OskiDakelh, Pigiarniq, Uqammaq");

  pref("font.name-list.serif.x-ethi", "Visual Geez Unicode, Visual Geez Unicode Agazian");
  pref("font.name-list.sans-serif.x-ethi", "GF Zemen Unicode");
  pref("font.name-list.monospace.x-ethi", "Ethiopia Jiret");
  pref("font.name-list.cursive.x-ethi", "Visual Geez Unicode Title");

  pref("font.name-list.serif.x-geor", "Sylfaen, BPG Paata Khutsuri U, TITUS Cyberbit Basic");
  pref("font.name-list.sans-serif.x-geor", "BPG Classic 99U");
  pref("font.name-list.monospace.x-geor", "BPG Classic 99U");

  pref("font.name-list.serif.x-gujr", "Shruti");
  pref("font.name-list.sans-serif.x-gujr", "Shruti");
  pref("font.name-list.monospace.x-gujr", "Shruti");

  pref("font.name-list.serif.x-guru", "Raavi, Saab");
  pref("font.name-list.sans-serif.x-guru", "");
  pref("font.name-list.monospace.x-guru", "Raavi, Saab");

  pref("font.name-list.serif.x-khmr", "PhnomPenh OT,.Mondulkiri U GR 1.5, Khmer OS");
  pref("font.name-list.sans-serif.x-khmr", "Khmer OS");
  pref("font.name-list.monospace.x-khmr", "Khmer OS, Khmer OS System");

  pref("font.name-list.serif.x-mlym", "Rachana_w01, AnjaliOldLipi, Kartika, ThoolikaUnicode");
  pref("font.name-list.sans-serif.x-mlym", "Rachana_w01, AnjaliOldLipi, Kartika, ThoolikaUnicode");
  pref("font.name-list.monospace.x-mlym", "Rachana_w01, AnjaliOldLipi, Kartika, ThoolikaUnicode");

  pref("font.name-list.serif.x-orya", "ori1Uni, Kalinga");
  pref("font.name-list.sans-serif.x-orya", "ori1Uni, Kalinga");
  pref("font.name-list.monospace.x-orya", "ori1Uni, Kalinga");

  pref("font.name-list.serif.x-telu", "Gautami, Akshar Unicode");
  pref("font.name-list.sans-serif.x-telu", "Gautami, Akshar Unicode");
  pref("font.name-list.monospace.x-telu", "Gautami, Akshar Unicode");

  pref("font.name-list.serif.x-knda", "Tunga, AksharUnicode");
  pref("font.name-list.sans-serif.x-knda", "Tunga, AksharUnicode");
  pref("font.name-list.monospace.x-knda", "Tunga, AksharUnicode");

  pref("font.name-list.serif.x-sinh", "Iskoola Pota, AksharUnicode");
  pref("font.name-list.sans-serif.x-sinh", "Iskoola Pota, AksharUnicode");
  pref("font.name-list.monospace.x-sinh", "Iskoola Pota, AksharUnicode");

  pref("font.name-list.serif.x-tibt", "Tibetan Machine Uni, Jomolhari, Microsoft Himalaya");
  pref("font.name-list.sans-serif.x-tibt", "Tibetan Machine Uni, Jomolhari, Microsoft Himalaya");
  pref("font.name-list.monospace.x-tibt", "Tibetan Machine Uni, Jomolhari, Microsoft Himalaya");

  pref("font.minimum-size.th", 10);

  pref("font.default.x-devanagari", "sans-serif");

  pref("font.name-list.serif.x-math", "Latin Modern Math, STIX Two Math, XITS Math, Cambria Math, Libertinus Math, DejaVu Math TeX Gyre, TeX Gyre Bonum Math, TeX Gyre Pagella Math, TeX Gyre Schola, TeX Gyre Termes Math, STIX Math, Asana Math, STIXGeneral, DejaVu Serif, DejaVu Sans, Times New Roman");
  pref("font.name-list.sans-serif.x-math", "Arial");
  pref("font.name-list.monospace.x-math", "Consolas");
  pref("font.name-list.cursive.x-math", "Comic Sans MS");

  // ClearType tuning parameters for directwrite/d2d.
  //
  // Allows overriding of underlying registry values in:
  //   HKCU/Software/Microsoft/Avalon.Graphics/<display> (contrast and level)
  //   HKLM/Software/Microsoft/Avalon.Graphics/<display> (gamma, pixel structure)
  // and selection of the ClearType/antialiasing mode.
  //
  // A value of -1 implies use the default value, otherwise value ranges
  // follow registry settings:
  //   gamma [1000, 2200]  default: based on screen, typically 2200 (== 2.2)
  //   enhanced contrast [0, 1000] default: 50
  //   cleartype level [0, 100] default: 100
  //   pixel structure [0, 2] default: 0 (flat/RGB/BGR)
  //   rendering mode [0, 5] default: 0
  //     0 = use default for font & size;
  //     1 = aliased;
  //     2 = GDI Classic;
  //     3 = GDI Natural Widths;
  //     4 = Natural;
  //     5 = Natural Symmetric
  //
  // See:
  //   http://msdn.microsoft.com/en-us/library/aa970267.aspx
  //   http://msdn.microsoft.com/en-us/library/dd368190%28v=VS.85%29.aspx
  // Note: DirectWrite uses the "Enhanced Contrast Level" value rather than the
  // "Text Contrast Level" value

  pref("gfx.font_rendering.cleartype_params.gamma", -1);
  pref("gfx.font_rendering.cleartype_params.enhanced_contrast", -1);
  pref("gfx.font_rendering.cleartype_params.cleartype_level", -1);
  pref("gfx.font_rendering.cleartype_params.pixel_structure", -1);
  pref("gfx.font_rendering.cleartype_params.rendering_mode", -1);

  // We no longer force "GDI Classic" mode on any fonts by default.
  pref("gfx.font_rendering.cleartype_params.force_gdi_classic_for_families", "");
  pref("gfx.font_rendering.cleartype_params.force_gdi_classic_max_size", 0);

  // Switch the keyboard layout per window
  pref("intl.keyboard.per_window_layout", false);

  // If composition_font is set, Gecko sets the font to IME.  IME may use
  // the fonts on their window like candidate window.  If they are empty,
  // Gecko uses the system default font which is set to the IM context.
  // The font name must not start with '@'.  When the writing mode is vertical,
  // Gecko inserts '@' to the start of the font name automatically.
  // FYI: Changing these prefs requires to restart.
  pref("intl.imm.composition_font", "");

  // Japanist 2003's candidate window is broken if the font is "@System" which
  // is default composition font for vertical writing mode.
  // You can specify font to use on candidate window of Japanist 2003.
  // This value must not start with '@'.
  // FYI: Changing this pref requires to restart.
  pref("intl.imm.composition_font.japanist_2003", "MS PGothic");

  // Even if IME claims that they support vertical writing mode but it may not
  // support vertical writing mode for its candidate window.  This pref allows
  // to ignore the claim.
  // FYI: Changing this pref requires to restart.
  pref("intl.imm.vertical_writing.always_assume_not_supported", false);

  // We cannot retrieve active IME name with IMM32 API if a TIP of TSF is
  // active. This pref can specify active IME name when Japanese TIP is active.
  // For example:
  //   Google Japanese Input: "Google 日本語入力 IMM32 モジュール"
  //   ATOK 2011: "ATOK 2011" (similarly, e.g., ATOK 2013 is "ATOK 2013")
  pref("intl.imm.japanese.assume_active_tip_name_as", "");

  // Enable system settings cache for mouse wheel message handling.
  // Note that even if this pref is set to true, Gecko may not cache the system
  // settings if Gecko detects that the cache won't be refreshed properly when
  // the settings are changed.
  pref("mousewheel.system_settings_cache.enabled", true);

  // This is a pref to test system settings cache for mouse wheel message
  // handling.  If this is set to true, Gecko forcibly use the cache.
  pref("mousewheel.system_settings_cache.force_enabled", false);

  // If your mouse drive sends WM_*SCROLL messages when you turn your mouse
  // wheel, set this to true.  Then, gecko processes them as mouse wheel
  // messages.
  pref("mousewheel.emulate_at_wm_scroll", false);

  // Some odd touchpad utils give focus to window under cursor when user tries
  // to scroll.  If this is true, Gecko tries to emulate such odd behavior.
  // Don't make this true unless you want to debug.  Enabling this pref causes
  // making damage to the performance.
  pref("mousewheel.debug.make_window_under_cursor_foreground", false);

  // Enables or disabled the TrackPoint hack, -1 is autodetect, 0 is off,
  // and 1 is on.  Set this to 1 if TrackPoint scrolling is not working.
  pref("ui.trackpoint_hack.enabled", -1);

  // Setting this to a non-empty string overrides the Win32 "window class" used
  // for "normal" windows. Setting this to MozillaUIWindowClass might make
  // some trackpad drivers behave better.
  pref("ui.window_class_override", "");

  // Enables or disables the Elantech gesture hacks.  -1 is autodetect, 0 is
  // off, and 1 is on.  Set this to 1 if three-finger swipe gestures do not
  // cause page back/forward actions, or if pinch-to-zoom does not work.
  pref("ui.elantech_gesture_hacks.enabled", -1);

  // Show the Windows on-screen keyboard (osk.exe) when a text field is focused.
  pref("ui.osk.enabled", true);
  // Only show the on-screen keyboard if there are no physical keyboards
  // attached to the device.
  pref("ui.osk.detect_physical_keyboard", true);
  // Path to TabTip.exe on local machine. Cached for performance reasons.
  pref("ui.osk.on_screen_keyboard_path", "");
  // This pref stores the "reason" that the on-screen keyboard was either
  // shown or not shown when focus is moved to an editable text field. It is
  // used to help debug why the keyboard is either not appearing when expected
  // or appearing when it is not expected.
  pref("ui.osk.debug.keyboardDisplayReason", "");

#endif // XP_WIN

#ifdef XP_MACOSX
  // Mac specific preference defaults
  pref("browser.drag_out_of_frame_style", 1);

  // default fonts (in UTF8 and using canonical names)
  // to determine canonical font names, use a debug build and
  // enable NSPR logging for module fontInfoLog:5
  // canonical names immediately follow '(fontinit) family:' in the log

  // For some scripts there is no commonly-installed monospace font, so we just use
  // the same as serif/sans-serif, but we prefix the list with Menlo so that at least
  // Latin text will be monospaced if it occurs when that lang code is in effect.

  pref("font.name-list.emoji", "Apple Color Emoji");

  pref("font.name-list.serif.ar", "Al Bayan");
  pref("font.name-list.sans-serif.ar", "Geeza Pro");
  pref("font.name-list.monospace.ar", "Menlo, Geeza Pro");
  pref("font.name-list.cursive.ar", "DecoType Naskh");
  pref("font.name-list.fantasy.ar", "KufiStandardGK");

  pref("font.name-list.serif.el", "Times, Times New Roman");
  pref("font.name-list.sans-serif.el", "Helvetica, Lucida Grande");
  pref("font.name-list.monospace.el", "Menlo");
  pref("font.name-list.cursive.el", "Lucida Grande, Times");
  pref("font.name-list.fantasy.el", "Lucida Grande, Times");

  pref("font.name-list.serif.he", "Times New Roman");
  pref("font.name-list.sans-serif.he", "Arial");
  pref("font.name-list.monospace.he", "Menlo, Courier New");
  pref("font.name-list.cursive.he", "Times New Roman");
  pref("font.name-list.fantasy.he", "Times New Roman");

  pref("font.name-list.serif.ja", "Hiragino Mincho ProN, Hiragino Mincho Pro");
  pref("font.name-list.sans-serif.ja", "Hiragino Kaku Gothic ProN, Hiragino Kaku Gothic Pro, Hiragino Sans");
  pref("font.name-list.monospace.ja", "Osaka-Mono, Menlo, Hiragino Kaku Gothic ProN, Hiragino Sans");

  pref("font.name-list.serif.ko", "AppleMyungjo");
  pref("font.name-list.sans-serif.ko", "Apple SD Gothic Neo, AppleGothic");
  pref("font.name-list.monospace.ko", "Menlo, Apple SD Gothic Neo, AppleGothic");

  pref("font.name-list.serif.th", "Thonburi");
  pref("font.name-list.sans-serif.th", "Thonburi");
  pref("font.name-list.monospace.th", "Menlo, Ayuthaya");

  pref("font.name-list.serif.x-armn", "Noto Sans Armenian");
  pref("font.name-list.sans-serif.x-armn", "Noto Sans Armenian");
  pref("font.name-list.monospace.x-armn", "Menlo, Noto Sans Armenian");

  // SolaimanLipi, Rupali http://ekushey.org/?page/mac_download
  pref("font.name-list.serif.x-beng", "Bangla MN");
  pref("font.name-list.sans-serif.x-beng", "Bangla Sangam MN");
  pref("font.name-list.monospace.x-beng", "Menlo, Bangla Sangam MN");

  pref("font.name-list.serif.x-cans", "Euphemia UCAS");
  pref("font.name-list.sans-serif.x-cans", "Euphemia UCAS");
  pref("font.name-list.monospace.x-cans", "Menlo, Euphemia UCAS");

  pref("font.name-list.serif.x-cyrillic", "Times, Times New Roman");
  pref("font.name-list.sans-serif.x-cyrillic", "Helvetica, Arial");
  pref("font.name-list.monospace.x-cyrillic", "Menlo");
  pref("font.name-list.cursive.x-cyrillic", "Geneva");
  pref("font.name-list.fantasy.x-cyrillic", "Charcoal CY");

  pref("font.name-list.serif.x-devanagari", "ITF Devanagari, Devanagari MT");
  pref("font.name-list.sans-serif.x-devanagari", "Kohinoor Devanagari, Devanagari Sangam MN, Devanagari MT");
  pref("font.name-list.monospace.x-devanagari", "Menlo, Kohinoor Devanagari, Devanagari Sangam MN, Devanagari MT");

  // Abyssinica SIL http://scripts.sil.org/AbyssinicaSIL_Download
  pref("font.name-list.serif.x-ethi", "Kefa, Abyssinica SIL");
  pref("font.name-list.sans-serif.x-ethi", "Kefa, Abyssinica SIL");
  pref("font.name-list.monospace.x-ethi", "Menlo, Kefa, Abyssinica SIL");

  // no suitable fonts for georgian ship with mac os x
  // however some can be freely downloaded
  // TITUS Cyberbit Basic http://titus.fkidg1.uni-frankfurt.de/unicode/tituut.asp
  // Zuzumbo http://homepage.mac.com/rsiradze/FileSharing91.html
  pref("font.name-list.serif.x-geor", "TITUS Cyberbit Basic");
  pref("font.name-list.sans-serif.x-geor", "Zuzumbo");
  pref("font.name-list.monospace.x-geor", "Menlo, Zuzumbo");

  pref("font.name-list.serif.x-gujr", "Gujarati MT");
  pref("font.name-list.sans-serif.x-gujr", "Gujarati Sangam MN, Gujarati MT");
  pref("font.name-list.monospace.x-gujr", "Menlo, Gujarati Sangam MN, Gujarati MT");

  pref("font.name-list.serif.x-guru", "Gurmukhi MT");
  pref("font.name-list.sans-serif.x-guru", "Gurmukhi MT");
  pref("font.name-list.monospace.x-guru", "Menlo, Gurmukhi MT");

  pref("font.name-list.serif.x-khmr", "Khmer MN");
  pref("font.name-list.sans-serif.x-khmr", "Khmer Sangam MN");
  pref("font.name-list.monospace.x-khmr", "Menlo, Khmer Sangam MN");

  pref("font.name-list.serif.x-mlym", "Malayalam MN");
  pref("font.name-list.sans-serif.x-mlym", "Malayalam Sangam MN");
  pref("font.name-list.monospace.x-mlym", "Menlo, Malayalam Sangam MN");

  pref("font.name-list.serif.x-orya", "Oriya MN");
  pref("font.name-list.sans-serif.x-orya", "Oriya Sangam MN");
  pref("font.name-list.monospace.x-orya", "Menlo, Oriya Sangam MN");

  // Pothana http://web.nickshanks.com/typography/telugu/
  pref("font.name-list.serif.x-telu", "Telugu MN, Pothana");
  pref("font.name-list.sans-serif.x-telu", "Telugu Sangam MN, Pothana");
  pref("font.name-list.monospace.x-telu", "Menlo, Telugu Sangam MN, Pothana");

  // Kedage http://web.nickshanks.com/typography/kannada/
  pref("font.name-list.serif.x-knda", "Kannada MN, Kedage");
  pref("font.name-list.sans-serif.x-knda", "Kannada Sangam MN, Kedage");
  pref("font.name-list.monospace.x-knda", "Menlo, Kannada Sangam MN, Kedage");

  pref("font.name-list.serif.x-sinh", "Sinhala MN");
  pref("font.name-list.sans-serif.x-sinh", "Sinhala Sangam MN");
  pref("font.name-list.monospace.x-sinh", "Menlo, Sinhala Sangam MN");

  pref("font.name-list.serif.x-tamil", "InaiMathi");
  pref("font.name-list.sans-serif.x-tamil", "InaiMathi");
  pref("font.name-list.monospace.x-tamil", "Menlo, InaiMathi");

  // Kailasa ships with mac os x >= 10.5
  pref("font.name-list.serif.x-tibt", "Kailasa");
  pref("font.name-list.sans-serif.x-tibt", "Kailasa");
  pref("font.name-list.monospace.x-tibt", "Menlo, Kailasa");

  pref("font.name-list.serif.x-unicode", "Times");
  pref("font.name-list.sans-serif.x-unicode", "Helvetica");
  pref("font.name-list.monospace.x-unicode", "Menlo");
  pref("font.name-list.cursive.x-unicode", "Apple Chancery");
  pref("font.name-list.fantasy.x-unicode", "Papyrus");

  pref("font.name-list.serif.x-western", "Times, Times New Roman");
  pref("font.name-list.sans-serif.x-western", "Helvetica, Arial");
  pref("font.name-list.monospace.x-western", "Menlo");
  pref("font.name-list.cursive.x-western", "Apple Chancery");
  pref("font.name-list.fantasy.x-western", "Papyrus");

  pref("font.name-list.serif.zh-CN", "Times New Roman, Songti SC, STSong, Heiti SC");
  pref("font.name-list.sans-serif.zh-CN", "Arial, PingFang SC, STHeiti, Heiti SC");
  pref("font.name-list.monospace.zh-CN", "Menlo, PingFang SC, STHeiti, Heiti SC");
  pref("font.name-list.cursive.zh-CN", "Kaiti SC");

  pref("font.name-list.serif.zh-TW", "Times New Roman, Songti TC, LiSong Pro, Heiti TC");
  pref("font.name-list.sans-serif.zh-TW", "Arial, PingFang TC, Heiti TC, LiHei Pro");
  pref("font.name-list.monospace.zh-TW", "Menlo, PingFang TC, Heiti TC, LiHei Pro");
  pref("font.name-list.cursive.zh-TW", "Kaiti TC");

  pref("font.name-list.serif.zh-HK", "Times New Roman, Songti TC, LiSong Pro, Heiti TC");
  pref("font.name-list.sans-serif.zh-HK", "Arial, PingFang TC, Heiti TC, LiHei Pro");
  pref("font.name-list.monospace.zh-HK", "Menlo, PingFang TC, Heiti TC, LiHei Pro");
  pref("font.name-list.cursive.zh-HK", "Kaiti TC");

  // XP_MACOSX changes to default font sizes
  pref("font.minimum-size.th", 10);

  // Apple's Symbol is Unicode so use it
  pref("font.name-list.serif.x-math", "Latin Modern Math, STIX Two Math, XITS Math, Cambria Math, Libertinus Math, DejaVu Math TeX Gyre, TeX Gyre Bonum Math, TeX Gyre Pagella Math, TeX Gyre Schola, TeX Gyre Termes Math, STIX Math, Asana Math, STIXGeneral, DejaVu Serif, DejaVu Sans, Symbol, Times");
  pref("font.name-list.sans-serif.x-math", "Helvetica");
  pref("font.name-list.monospace.x-math", "Menlo");
  pref("font.name-list.cursive.x-math", "Apple Chancery");
  pref("font.name-list.fantasy.x-math", "Papyrus");

  // Individual font faces to be treated as independent families,
  // listed as <Postscript name of face:Owning family name>
  pref("font.single-face-list", "Osaka-Mono:Osaka");

  // optimization hint for fonts with localized names to be read in at startup, otherwise read in at lookup miss
  // names are canonical family names (typically English names)
  pref("font.preload-names-list", "Hiragino Kaku Gothic ProN,Hiragino Mincho ProN,STSong");

  // Override font-weight values for some problematic families Apple ships
  // (see bug 931426).
  // The name here is the font's PostScript name, which can be checked in
  // the Font Book utility or other tools.
  pref("font.weight-override.AppleSDGothicNeo-Thin", 100); // Ensure Thin < UltraLight < Light
  pref("font.weight-override.AppleSDGothicNeo-UltraLight", 200);
  pref("font.weight-override.AppleSDGothicNeo-Light", 300);
  pref("font.weight-override.AppleSDGothicNeo-Heavy", 900); // Ensure Heavy > ExtraBold (800)

  pref("font.weight-override.Avenir-Book", 300); // Ensure Book < Roman (400)
  pref("font.weight-override.Avenir-BookOblique", 300);
  pref("font.weight-override.Avenir-MediumOblique", 500); // Harmonize MediumOblique with Medium
  pref("font.weight-override.Avenir-Black", 900); // Ensure Black > Heavy (800)
  pref("font.weight-override.Avenir-BlackOblique", 900);

  pref("font.weight-override.AvenirNext-MediumItalic", 500); // Harmonize MediumItalic with Medium
  pref("font.weight-override.AvenirNextCondensed-MediumItalic", 500);

  pref("font.weight-override.HelveticaNeue-Light", 300); // Ensure Light > Thin (200)
  pref("font.weight-override.HelveticaNeue-LightItalic", 300);

  // Macbook touchpad two finger pixel scrolling
  pref("mousewheel.enable_pixel_scrolling", true);

#endif // XP_MACOSX

#ifdef XP_IOS
  // For many scripts there is no standard "monospaced" font, so we just use
  // the same as serif/sans-serif, but we prefix the list with Menlo so that at
  // least any runs of Latin text found when that lang code is in effect will be
  // monospaced, even if the "native" script can't be.

  // For non-Latin/Greek/Cyrillic scripts, there may not be any serif/sans-serif
  // distinction available, so both generics resolve to the same font.

  pref("font.name-list.emoji", "Apple Color Emoji");

  pref("font.name-list.serif.ar", "Al Nile");
  pref("font.name-list.sans-serif.ar", "Geeza Pro");
  pref("font.name-list.monospace.ar", "Menlo, Geeza Pro");

  pref("font.name-list.serif.el", "Times New Roman");
  pref("font.name-list.sans-serif.el", "Arial");
  pref("font.name-list.monospace.el", "Menlo");

  pref("font.name-list.serif.he", "Times New Roman");
  pref("font.name-list.sans-serif.he", "Arial");
  pref("font.name-list.monospace.he", "Menlo");

  pref("font.name-list.serif.ja", "Hiragino Mincho ProN");
  pref("font.name-list.sans-serif.ja", "Hiragino Sans");
  pref("font.name-list.monospace.ja", "Menlo, Hiragino Sans");

  pref("font.name-list.serif.ko", "Apple SD Gothic Neo");
  pref("font.name-list.sans-serif.ko", "Apple SD Gothic Neo");
  pref("font.name-list.monospace.ko", "Menlo, Apple SD Gothic Neo");

  pref("font.name-list.serif.th", "Thonburi");
  pref("font.name-list.sans-serif.th", "Thonburi");
  pref("font.name-list.monospace.th", "Menlo, Thonburi");

  // XXX Unsure if the "SF Armenian" font is present/visible as standard?
  pref("font.name-list.serif.x-armn", "SF Armenian");
  pref("font.name-list.sans-serif.x-armn", "SF Armenian");
  pref("font.name-list.monospace.x-armn", "Menlo, SF Armenian");

  pref("font.name-list.serif.x-beng", "Kohinoor Bangla");
  pref("font.name-list.sans-serif.x-beng", "Kohinoor Bangla");
  pref("font.name-list.monospace.x-beng", "Menlo, Kohinoor Bangla");

  pref("font.name-list.serif.x-cans", "Euphemia UCAS");
  pref("font.name-list.sans-serif.x-cans", "Euphemia UCAS");
  pref("font.name-list.monospace.x-cans", "Menlo, Euphemia UCAS");

  pref("font.name-list.serif.x-cyrillic", "Times New Roman");
  pref("font.name-list.sans-serif.x-cyrillic", "Arial");
  pref("font.name-list.monospace.x-cyrillic", "Menlo");

  pref("font.name-list.serif.x-devanagari", "Kohinoor Devanagari, Devanagari Sangam MN");
  pref("font.name-list.sans-serif.x-devanagari", "Kohinoor Devanagari, Devanagari Sangam MN");
  pref("font.name-list.monospace.x-devanagari", "Menlo, Kohinoor Devanagari, Devanagari Sangam MN");

  pref("font.name-list.serif.x-ethi", "Kefa");
  pref("font.name-list.sans-serif.x-ethi", "Kefa");
  pref("font.name-list.monospace.x-ethi", "Menlo, Kefa");

  // XXX Is "SF Georgian" present/visible as standard?
  pref("font.name-list.serif.x-geor", "SF Georgian");
  pref("font.name-list.sans-serif.x-geor", "SF Georgian");
  pref("font.name-list.monospace.x-geor", "Menlo, SF Georgian");

  pref("font.name-list.serif.x-gujr", "Kohinoor Gujarati");
  pref("font.name-list.sans-serif.x-gujr", "Kohinoor Gujarati");
  pref("font.name-list.monospace.x-gujr", "Menlo, Kohinoor Gujarati");

  // XXX Check spelling: "Mukta Mahee" or "MuktaMahee"?
  pref("font.name-list.serif.x-guru", "Mukta Mahee");
  pref("font.name-list.sans-serif.x-guru", "Mukta Mahee");
  pref("font.name-list.monospace.x-guru", "Menlo, Mukta Mahee");

  pref("font.name-list.serif.x-khmr", "Khmer Sangam MN");
  pref("font.name-list.sans-serif.x-khmr", "Khmer Sangam MN");
  pref("font.name-list.monospace.x-khmr", "Menlo, Khmer Sangam MN");

  pref("font.name-list.serif.x-mlym", "Malayalam Sangam MN");
  pref("font.name-list.sans-serif.x-mlym", "Malayalam Sangam MN");
  pref("font.name-list.monospace.x-mlym", "Menlo, Malayalam Sangam MN");

  pref("font.name-list.serif.x-orya", "Noto Sans Oriya");
  pref("font.name-list.sans-serif.x-orya", "Noto Sans Oriya");
  pref("font.name-list.monospace.x-orya", "Menlo, Noto Sans Oriya");

  pref("font.name-list.serif.x-telu", "Kohinoor Telugu");
  pref("font.name-list.sans-serif.x-telu", "Kohinoor Telugu");
  pref("font.name-list.monospace.x-telu", "Menlo, Kohinoor Telugu");

  pref("font.name-list.serif.x-knda", "Noto Sans Kannada");
  pref("font.name-list.sans-serif.x-knda", "Noto Sans Kannada");
  pref("font.name-list.monospace.x-knda", "Menlo, Noto Sans Kannada");

  pref("font.name-list.serif.x-sinh", "Sinhala Sangam MN");
  pref("font.name-list.sans-serif.x-sinh", "Sinhala Sangam MN");
  pref("font.name-list.monospace.x-sinh", "Menlo, Sinhala Sangam MN");

  pref("font.name-list.serif.x-tamil", "Tamil Sangam MN");
  pref("font.name-list.sans-serif.x-tamil", "Tamil Sangam MN");
  pref("font.name-list.monospace.x-tamil", "Menlo, Tamil Sangam MN");

  pref("font.name-list.serif.x-tibt", "Kailasa");
  pref("font.name-list.sans-serif.x-tibt", "Kailasa");
  pref("font.name-list.monospace.x-tibt", "Menlo, Kailasa");

  pref("font.name-list.serif.x-unicode", "Times New Roman");
  pref("font.name-list.sans-serif.x-unicode", "Arial");
  pref("font.name-list.monospace.x-unicode", "Menlo");

  pref("font.name-list.serif.x-western", "Times New Roman");
  pref("font.name-list.sans-serif.x-western", "Arial");
  pref("font.name-list.monospace.x-western", "Menlo");

  // XXX Is there an alternative that would be better for 'serif'?
  pref("font.name-list.serif.zh-CN", "Times New Roman, PingFang SC");
  pref("font.name-list.sans-serif.zh-CN", "Arial, PingFang SC");
  pref("font.name-list.monospace.zh-CN", "Menlo, PingFang SC");

  pref("font.name-list.serif.zh-TW", "Times New Roman, PingFang TC");
  pref("font.name-list.sans-serif.zh-TW", "Arial, PingFang TC");
  pref("font.name-list.monospace.zh-TW", "Menlo, PingFang TC");

  pref("font.name-list.serif.zh-HK", "Times New Roman, PingFang HK");
  pref("font.name-list.sans-serif.zh-HK", "Arial, PingFang HK");
  pref("font.name-list.monospace.zh-HK", "Menlo, PingFang HK");

  pref("font.name-list.serif.x-math", "STIX Two Math, Symbol, Apple Symbols");
  pref("font.name-list.sans-serif.x-math", "Arial");
  pref("font.name-list.monospace.x-math", "Menlo");

#endif // XP_IOS

#ifdef ANDROID
  // Handled differently under Mac/Windows
  pref("network.protocol-handler.warn-external.file", false);
  pref("browser.drag_out_of_frame_style", 1);

  // Middle-mouse handling
  pref("middlemouse.paste", true);
  pref("middlemouse.scrollbarPosition", true);

  // Tab focus model bit field:
  // 1 focuses text controls, 2 focuses other form elements, 4 adds links.
  // Leave this at the default, 7, to match mozilla1.0-era user expectations.
  // pref("accessibility.tabfocus", 1);

  pref("helpers.global_mime_types_file", "/etc/mime.types");
  pref("helpers.global_mailcap_file", "/etc/mailcap");
  pref("helpers.private_mime_types_file", "~/.mime.types");
  pref("helpers.private_mailcap_file", "~/.mailcap");
#endif // ANDROID

#if !defined(ANDROID) && !defined(XP_MACOSX) && defined(XP_UNIX)
  // Handled differently under Mac/Windows
  pref("network.protocol-handler.warn-external.file", false);
  pref("browser.drag_out_of_frame_style", 1);

  // Middle-mouse handling
  pref("middlemouse.paste", true);
  pref("middlemouse.scrollbarPosition", true);

  // Tab focus model bit field:
  // 1 focuses text controls, 2 focuses other form elements, 4 adds links.
  // Leave this at the default, 7, to match mozilla1.0-era user expectations.
  // pref("accessibility.tabfocus", 1);

  pref("helpers.global_mime_types_file", "/etc/mime.types");
  pref("helpers.global_mailcap_file", "/etc/mailcap");
  pref("helpers.private_mime_types_file", "~/.mime.types");
  pref("helpers.private_mailcap_file", "~/.mailcap");

  // font names

  // fontconfig doesn't support emoji yet
  // https://lists.freedesktop.org/archives/fontconfig/2016-October/005842.html
  pref("font.name-list.emoji", "Noto Color Emoji, Twemoji Mozilla");

  pref("font.name-list.serif.ar", "serif");
  pref("font.name-list.sans-serif.ar", "sans-serif");
  pref("font.name-list.monospace.ar", "monospace");
  pref("font.name-list.cursive.ar", "cursive");
  pref("font.size.monospace.ar", 12);

  pref("font.name-list.serif.el", "serif");
  pref("font.name-list.sans-serif.el", "sans-serif");
  pref("font.name-list.monospace.el", "monospace");
  pref("font.name-list.cursive.el", "cursive");
  pref("font.size.monospace.el", 12);

  pref("font.name-list.serif.he", "serif");
  pref("font.name-list.sans-serif.he", "sans-serif");
  pref("font.name-list.monospace.he", "monospace");
  pref("font.name-list.cursive.he", "cursive");
  pref("font.size.monospace.he", 12);

  pref("font.name-list.serif.ja", "serif");
  pref("font.name-list.sans-serif.ja", "sans-serif");
  pref("font.name-list.monospace.ja", "monospace");
  pref("font.name-list.cursive.ja", "cursive");

  pref("font.name-list.serif.ko", "serif");
  pref("font.name-list.sans-serif.ko", "sans-serif");
  pref("font.name-list.monospace.ko", "monospace");
  pref("font.name-list.cursive.ko", "cursive");

  pref("font.name-list.serif.th", "serif");
  pref("font.name-list.sans-serif.th", "sans-serif");
  pref("font.name-list.monospace.th", "monospace");
  pref("font.name-list.cursive.th", "cursive");
  pref("font.minimum-size.th", 13);

  pref("font.name-list.serif.x-armn", "serif");
  pref("font.name-list.sans-serif.x-armn", "sans-serif");
  pref("font.name-list.monospace.x-armn", "monospace");
  pref("font.name-list.cursive.x-armn", "cursive");

  pref("font.name-list.serif.x-beng", "serif");
  pref("font.name-list.sans-serif.x-beng", "sans-serif");
  pref("font.name-list.monospace.x-beng", "monospace");
  pref("font.name-list.cursive.x-beng", "cursive");

  pref("font.name-list.serif.x-cans", "serif");
  pref("font.name-list.sans-serif.x-cans", "sans-serif");
  pref("font.name-list.monospace.x-cans", "monospace");
  pref("font.name-list.cursive.x-cans", "cursive");

  pref("font.name-list.serif.x-cyrillic", "serif");
  pref("font.name-list.sans-serif.x-cyrillic", "sans-serif");
  pref("font.name-list.monospace.x-cyrillic", "monospace");
  pref("font.name-list.cursive.x-cyrillic", "cursive");
  pref("font.size.monospace.x-cyrillic", 12);

  pref("font.name-list.serif.x-devanagari", "serif");
  pref("font.name-list.sans-serif.x-devanagari", "sans-serif");
  pref("font.name-list.monospace.x-devanagari", "monospace");
  pref("font.name-list.cursive.x-devanagari", "cursive");

  pref("font.name-list.serif.x-ethi", "serif");
  pref("font.name-list.sans-serif.x-ethi", "sans-serif");
  pref("font.name-list.monospace.x-ethi", "monospace");
  pref("font.name-list.cursive.x-ethi", "cursive");

  pref("font.name-list.serif.x-geor", "serif");
  pref("font.name-list.sans-serif.x-geor", "sans-serif");
  pref("font.name-list.monospace.x-geor", "monospace");
  pref("font.name-list.cursive.x-geor", "cursive");

  pref("font.name-list.serif.x-gujr", "serif");
  pref("font.name-list.sans-serif.x-gujr", "sans-serif");
  pref("font.name-list.monospace.x-gujr", "monospace");
  pref("font.name-list.cursive.x-gujr", "cursive");

  pref("font.name-list.serif.x-guru", "serif");
  pref("font.name-list.sans-serif.x-guru", "sans-serif");
  pref("font.name-list.monospace.x-guru", "monospace");
  pref("font.name-list.cursive.x-guru", "cursive");

  pref("font.name-list.serif.x-khmr", "serif");
  pref("font.name-list.sans-serif.x-khmr", "sans-serif");
  pref("font.name-list.monospace.x-khmr", "monospace");
  pref("font.name-list.cursive.x-khmr", "cursive");

  pref("font.name-list.serif.x-knda", "serif");
  pref("font.name-list.sans-serif.x-knda", "sans-serif");
  pref("font.name-list.monospace.x-knda", "monospace");
  pref("font.name-list.cursive.x-knda", "cursive");

  pref("font.name-list.serif.x-mlym", "serif");
  pref("font.name-list.sans-serif.x-mlym", "sans-serif");
  pref("font.name-list.monospace.x-mlym", "monospace");
  pref("font.name-list.cursive.x-mlym", "cursive");

  pref("font.name-list.serif.x-orya", "serif");
  pref("font.name-list.sans-serif.x-orya", "sans-serif");
  pref("font.name-list.monospace.x-orya", "monospace");
  pref("font.name-list.cursive.x-orya", "cursive");

  pref("font.name-list.serif.x-sinh", "serif");
  pref("font.name-list.sans-serif.x-sinh", "sans-serif");
  pref("font.name-list.monospace.x-sinh", "monospace");
  pref("font.name-list.cursive.x-sinh", "cursive");

  pref("font.name-list.serif.x-tamil", "serif");
  pref("font.name-list.sans-serif.x-tamil", "sans-serif");
  pref("font.name-list.monospace.x-tamil", "monospace");
  pref("font.name-list.cursive.x-tamil", "cursive");

  pref("font.name-list.serif.x-telu", "serif");
  pref("font.name-list.sans-serif.x-telu", "sans-serif");
  pref("font.name-list.monospace.x-telu", "monospace");
  pref("font.name-list.cursive.x-telu", "cursive");

  pref("font.name-list.serif.x-tibt", "serif");
  pref("font.name-list.sans-serif.x-tibt", "sans-serif");
  pref("font.name-list.monospace.x-tibt", "monospace");
  pref("font.name-list.cursive.x-tibt", "cursive");

  pref("font.name-list.serif.x-unicode", "serif");
  pref("font.name-list.sans-serif.x-unicode", "sans-serif");
  pref("font.name-list.monospace.x-unicode", "monospace");
  pref("font.name-list.cursive.x-unicode", "cursive");
  pref("font.size.monospace.x-unicode", 12);

  pref("font.name-list.serif.x-western", "serif");
  pref("font.name-list.sans-serif.x-western", "sans-serif");
  pref("font.name-list.monospace.x-western", "monospace");
  pref("font.name-list.cursive.x-western", "cursive");
  pref("font.size.monospace.x-western", 12);

  pref("font.name-list.serif.zh-CN", "serif");
  pref("font.name-list.sans-serif.zh-CN", "sans-serif");
  pref("font.name-list.monospace.zh-CN", "monospace");
  pref("font.name-list.cursive.zh-CN", "cursive");

  pref("font.name-list.serif.zh-HK", "serif");
  pref("font.name-list.sans-serif.zh-HK", "sans-serif");
  pref("font.name-list.monospace.zh-HK", "monospace");
  pref("font.name-list.cursive.zh-HK", "cursive");

  pref("font.name-list.serif.zh-TW", "serif");
  pref("font.name-list.sans-serif.zh-TW", "sans-serif");
  pref("font.name-list.monospace.zh-TW", "monospace");
  pref("font.name-list.cursive.zh-TW", "cursive");

  pref("intl.ime.use_simple_context_on_password_field", false);

  // uim may use key snooper to listen to key events.  Unfortunately, we cannot
  // know whether it uses or not since it's a build option.  So, we need to make
  // distribution switchable whether we think uim uses key snooper or not with
  // this pref.  Debian 9.x still uses uim as their default IM and it uses key
  // snooper.  So, let's use true for its default value.
  pref("intl.ime.hack.uim.using_key_snooper", true);

#endif // !ANDROID && !XP_MACOSX && XP_UNIX

#if defined(ANDROID)

  pref("font.size.monospace.ar", 12);

  pref("font.default.el", "sans-serif");
  pref("font.size.monospace.el", 12);

  pref("font.size.monospace.he", 12);

  pref("font.default.x-cyrillic", "sans-serif");
  pref("font.size.monospace.x-cyrillic", 12);

  pref("font.default.x-unicode", "sans-serif");
  pref("font.size.monospace.x-unicode", 12);

  pref("font.default.x-western", "sans-serif");
  pref("font.size.monospace.x-western", 12);

#endif // ANDROID

#if defined(ANDROID)
  // We use the bundled Charis SIL Compact as serif font for Firefox for Android

  pref("font.name-list.emoji", "SamsungColorEmoji, Noto Color Emoji");

  pref("font.name-list.serif.ar", "Noto Naskh Arabic, Noto Serif, Droid Serif");
  pref("font.name-list.sans-serif.ar", "Noto Naskh Arabic, Roboto, Google Sans, Droid Sans");
  pref("font.name-list.monospace.ar", "Noto Naskh Arabic");

  pref("font.name-list.serif.el", "Droid Serif, Noto Serif"); // not Charis SIL Compact, only has a few Greek chars
  pref("font.name-list.sans-serif.el", "Roboto, Google Sans, Droid Sans");
  pref("font.name-list.monospace.el", "Droid Sans Mono");

  pref("font.name-list.serif.he", "Droid Serif, Noto Serif, Noto Serif Hebrew");
  pref("font.name-list.sans-serif.he", "Roboto, Google Sans, Noto Sans Hebrew, Droid Sans Hebrew, Droid Sans, Arial");
  pref("font.name-list.monospace.he", "Droid Sans Mono");

  pref("font.name-list.serif.ja", "Charis SIL Compact, Noto Serif CJK JP, Noto Serif, Droid Serif");
  pref("font.name-list.sans-serif.ja", "Roboto, Google Sans, Droid Sans, MotoyaLMaru, MotoyaLCedar, Noto Sans JP, Noto Sans CJK JP, SEC CJK JP, Droid Sans Japanese");
  pref("font.name-list.monospace.ja", "MotoyaLMaru, MotoyaLCedar, Noto Sans Mono CJK JP, SEC Mono CJK JP, Droid Sans Mono");

  pref("font.name-list.serif.ko", "Charis SIL Compact, Noto Serif CJK KR, Noto Serif, Droid Serif, HYSerif");
  pref("font.name-list.sans-serif.ko", "Roboto, Google Sans, SmartGothic, NanumGothic, Noto Sans KR, Noto Sans CJK KR, One UI Sans KR VF, SamsungKorean_v2.0, SEC CJK KR, DroidSansFallback, Droid Sans Fallback");
  pref("font.name-list.monospace.ko", "Droid Sans Mono, Noto Sans Mono CJK KR, SEC Mono CJK KR");

  pref("font.name-list.serif.th", "Charis SIL Compact, Noto Serif, Noto Serif Thai, Droid Serif");
  pref("font.name-list.sans-serif.th", "Roboto, Google Sans, Noto Sans Thai, Droid Sans Thai, Droid Sans");
  pref("font.name-list.monospace.th", "Droid Sans Mono");

  pref("font.name-list.serif.x-armn", "Noto Serif Armenian");
  pref("font.name-list.sans-serif.x-armn", "Noto Sans Armenian");

  pref("font.name-list.serif.x-beng", "Noto Serif Bengali");
  pref("font.name-list.sans-serif.x-beng", "Noto Sans Bengali");

  pref("font.name-list.serif.x-cyrillic", "Charis SIL Compact, Noto Serif, Droid Serif");
  pref("font.name-list.sans-serif.x-cyrillic", "Roboto, Google Sans, Droid Sans");
  pref("font.name-list.monospace.x-cyrillic", "Droid Sans Mono");

  pref("font.name-list.serif.x-devanagari", "Noto Serif Devanagari");
  pref("font.name-list.sans-serif.x-devanagari", "Noto Sans Devanagari");

  pref("font.name-list.serif.x-ethi", "Noto Serif Ethiopic");
  pref("font.name-list.sans-serif.x-ethi", "Noto Sans Ethiopic");

  pref("font.name-list.serif.x-geor", "Noto Serif Georgian");
  pref("font.name-list.sans-serif.x-geor", "Noto Sans Georgian");

  pref("font.name-list.serif.x-gujr", "Noto Serif Gujarati");
  pref("font.name-list.sans-serif.x-gujr", "Noto Sans Gujarati");

  pref("font.name-list.serif.x-guru", "Noto Serif Gurmukhi");
  pref("font.name-list.sans-serif.x-guru", "Noto Sans Gurmukhi");

  pref("font.name-list.serif.x-khmr", "Noto Serif Khmer");
  pref("font.name-list.sans-serif.x-khmr", "Noto Sans Khmer");

  pref("font.name-list.serif.x-knda", "Noto Serif Kannada");
  pref("font.name-list.sans-serif.x-knda", "Noto Sans Kannada");

  pref("font.name-list.serif.x-mlym", "Noto Serif Malayalam");
  pref("font.name-list.sans-serif.x-mlym", "Noto Sans Malayalam");

  pref("font.name-list.sans-serif.x-orya", "Noto Sans Oriya");

  pref("font.name-list.serif.x-sinh", "Noto Serif Sinhala");
  pref("font.name-list.sans-serif.x-sinh", "Noto Sans Sinhala");

  pref("font.name-list.serif.x-tamil", "Noto Serif Tamil");
  pref("font.name-list.sans-serif.x-tamil", "Noto Sans Tamil");

  pref("font.name-list.serif.x-telu", "Noto Serif Telugu");
  pref("font.name-list.sans-serif.x-telu", "Noto Sans Telugu");

  pref("font.name-list.serif.x-tibt", "Noto Serif Tibetan");
  pref("font.name-list.sans-serif.x-tibt", "Noto Sans Tibetan");

  pref("font.name-list.serif.x-unicode", "Charis SIL Compact, Noto Serif, Droid Serif");
  pref("font.name-list.sans-serif.x-unicode", "Roboto, Google Sans, Droid Sans");
  pref("font.name-list.monospace.x-unicode", "Droid Sans Mono");

  pref("font.name-list.serif.x-western", "Charis SIL Compact, Noto Serif, Droid Serif");
  pref("font.name-list.sans-serif.x-western", "Roboto, Google Sans, Droid Sans");
  pref("font.name-list.monospace.x-western", "Droid Sans Mono");

  pref("font.name-list.serif.zh-CN", "Charis SIL Compact, Noto Serif CJK SC, Noto Serif, Droid Serif, Droid Sans Fallback");
  pref("font.name-list.sans-serif.zh-CN", "Roboto, Google Sans, Droid Sans, Noto Sans SC, Noto Sans CJK SC, SEC CJK SC, Droid Sans Fallback");
  pref("font.name-list.monospace.zh-CN", "Droid Sans Mono, Noto Sans Mono CJK SC, SEC Mono CJK SC, Noto Sans CJK SC, MiSans VF, MiSans, Droid Sans Fallback");

  pref("font.name-list.serif.zh-HK", "Charis SIL Compact, Noto Serif CJK TC, Noto Serif, Droid Serif, Droid Sans Fallback");
  pref("font.name-list.sans-serif.zh-HK", "Roboto, Google Sans, Droid Sans, Noto Sans TC, Noto Sans SC, Noto Sans CJK TC, SEC CJK TC, Droid Sans Fallback");
  pref("font.name-list.monospace.zh-HK", "Droid Sans Mono, Noto Sans Mono CJK TC, SEC Mono CJK TC, Noto Sans CJK TC, MiSans TC VF, MiSans TC, Droid Sans Fallback");

  pref("font.name-list.serif.zh-TW", "Charis SIL Compact, Noto Serif CJK TC, Noto Serif, Droid Serif, Droid Sans Fallback");
  pref("font.name-list.sans-serif.zh-TW", "Roboto, Google Sans, Droid Sans, Noto Sans TC, Noto Sans SC, Noto Sans CJK TC, SEC CJK TC, Droid Sans Fallback");
  pref("font.name-list.monospace.zh-TW", "Droid Sans Mono, Noto Sans Mono CJK TC, SEC Mono CJK TC, Noto Sans CJK TC, MiSans TC VF, MiSans TC, Droid Sans Fallback");

  pref("font.name-list.serif.x-math", "Latin Modern Math, STIX Two Math, XITS Math, Cambria Math, Libertinus Math, DejaVu Math TeX Gyre, TeX Gyre Bonum Math, TeX Gyre Pagella Math, TeX Gyre Schola, TeX Gyre Termes Math, STIX Math, Asana Math, STIXGeneral, DejaVu Serif, DejaVu Sans, Charis SIL Compact");
  pref("font.name-list.sans-serif.x-math", "Roboto, Google Sans");
  pref("font.name-list.monospace.x-math", "Droid Sans Mono");

#endif

// Login Manager prefs
pref("signon.rememberSignons",              true);
pref("signon.autofillForms",                true);
pref("signon.autofillForms.autocompleteOff", true);
pref("signon.autofillForms.http",           false);
pref("signon.autologin.proxy",              false);
pref("signon.capture.inputChanges.enabled", true);
pref("signon.formlessCapture.enabled",      true);
pref("signon.formRemovalCapture.enabled",   true);
pref("signon.generation.available",               true);
pref("signon.improvedPasswordRules.enabled", true);
pref("signon.backup.enabled",               true);
pref("signon.generation.confidenceThreshold",     "0.75");
pref("signon.generation.enabled",                 true);
pref("signon.passwordEditCapture.enabled",        false);
pref("signon.privateBrowsingCapture.enabled",     true);
pref("signon.storeWhenAutocompleteOff",     true);
pref("signon.userInputRequiredToCapture.enabled", true);
pref("signon.usernameOnlyForm.lookupThreshold",  5);
pref("signon.debug",                        false);
pref("signon.recipes.path", "resource://app/defaults/settings/main/password-recipes.json");
pref("signon.recipes.remoteRecipes.enabled", true);
pref("signon.relatedRealms.enabled", false);
pref("signon.schemeUpgrades",                     true);
pref("signon.includeOtherSubdomainsInLookup",     true);
// This temporarily prevents the primary password to reprompt for autocomplete.
pref("signon.masterPasswordReprompt.timeout_ms", 900000); // 15 Minutes
pref("signon.showAutoCompleteFooter",             false);
pref("signon.firefoxRelay.base_url", "https://relay.firefox.com/api/v1/");
pref("signon.firefoxRelay.learn_more_url", "https://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/firefox-relay-integration");
pref("signon.firefoxRelay.manage_url", "https://relay.firefox.com/accounts/profile/?utm_medium=firefox-desktop&utm_source=modal&utm_campaign=limit&utm_content=manage-masks-global");
pref("signon.firefoxRelay.terms_of_service_url", "https://www.mozilla.org/%LOCALE%/about/legal/terms/subscription-services/");
pref("signon.firefoxRelay.privacy_policy_url", "https://www.mozilla.org/%LOCALE%/privacy/subscription-services/");
pref("signon.signupDetection.confidenceThreshold",     "0.75");

// Satchel (Form Manager) prefs
pref("browser.formfill.debug",            false);
pref("browser.formfill.enable",           true);
pref("browser.formfill.expire_days",      180);
pref("browser.formfill.agedWeight",       2);
pref("browser.formfill.bucketSize",       1);
pref("browser.formfill.maxTimeGroupings", 25);
pref("browser.formfill.timeGroupingSize", 604800);
pref("browser.formfill.boundaryWeight",   25);
pref("browser.formfill.prefixWeight",     5);

// Zoom prefs
pref("browser.zoom.full", false);
pref("toolkit.zoomManager.zoomValues", ".3,.5,.67,.8,.9,1,1.1,1.2,1.33,1.5,1.7,2,2.4,3,4,5");

//
// Image-related prefs
//

// By default the Accept header sent for images loaded over HTTP(S) is derived
// by ImageAcceptHeader() in nsHttpHandler.cpp. If set, this pref overrides it.
// There is also network.http.accept which works in scope of document.
pref("image.http.accept", "");

// sendbuffer of 0 means use OS default, sendbuffer unset means use
// gecko default which varies depending on windows version and is OS
// default on non windows
// pref("network.tcp.sendbuffer", 0);

// TCP Keepalive
pref("network.tcp.keepalive.enabled", true);
// Default idle time before first TCP keepalive probe; same time for interval
// between successful probes. Can be overridden in socket transport API.
// Win, Linux and Mac.
pref("network.tcp.keepalive.idle_time", 600); // seconds; 10 mins
// Default timeout for retransmission of unack'd keepalive probes.
// Win and Linux only; not configurable on Mac.
#if defined(XP_UNIX) && !defined(XP_MACOSX) || defined(XP_WIN)
  pref("network.tcp.keepalive.retry_interval", 1); // seconds
#endif
// Default maximum probe retransmissions.
// Linux only; not configurable on Win and Mac; fixed at 10 and 8 respectively.
#if defined(XP_UNIX) && !defined(XP_MACOSX)
  pref("network.tcp.keepalive.probe_count", 4);
#endif

#ifdef MOZ_WIDGET_GTK
  pref("widget.disable-workspace-management", false);
#endif

// All the Geolocation preferences are here.
//
pref("geo.provider.network.url", "https://www.googleapis.com/geolocation/v1/geolocate?key=%GOOGLE_LOCATION_SERVICE_API_KEY%");

// Timeout to wait before sending the location request.
pref("geo.provider.network.timeToWaitBeforeSending", 5000);
// Timeout for outbound network geolocation provider.
pref("geo.provider.network.timeout", 60000);

#ifdef XP_MACOSX
  pref("geo.provider.use_corelocation", true);
#endif

// Set to false if things are really broken.
#ifdef XP_WIN
  pref("geo.provider.ms-windows-location", true);
#endif

// Region
pref("browser.region.log", false);
pref("browser.region.network.url", "https://location.services.mozilla.com/v1/country?key=%MOZILLA_API_KEY%");
// Include wifi data in region request.
pref("browser.region.network.scan", false);
// Timeout for whole region request.
pref("browser.region.timeout", 5000);
pref("browser.region.update.enabled", true);

pref("browser.meta_refresh_when_inactive.disabled", false);

// XPInstall prefs
pref("xpinstall.whitelist.required", true);
// Only Firefox requires add-on signatures
pref("xpinstall.signatures.required", false);
pref("extensions.langpacks.signatures.required", false);
pref("extensions.webExtensionsMinPlatformVersion", "42.0a1");
pref("extensions.experiments.enabled", true);

// Other webextensions prefs
pref("extensions.webextensions.keepStorageOnUninstall", false);
pref("extensions.webextensions.keepUuidOnUninstall", false);
// Redirect basedomain used by identity api
pref("extensions.webextensions.identity.redirectDomain", "extensions.allizom.org");
pref("extensions.webextensions.restrictedDomains", "accounts-static.cdn.mozilla.net,accounts.firefox.com,addons.cdn.mozilla.net,addons.mozilla.org,api.accounts.firefox.com,content.cdn.mozilla.net,discovery.addons.mozilla.org,install.mozilla.org,oauth.accounts.firefox.com,profile.accounts.firefox.com,support.mozilla.org,sync.services.mozilla.com");

// Extensions are prevented from accessing Quarantined Domains by default.
pref("extensions.quarantinedDomains.enabled", true);
pref("extensions.quarantinedDomains.list", "");

// Include origin permissions in the install prompt for MV3 extensions.
pref("extensions.originControls.grantByDefault", true);

// Whether or not the moz-extension resource loads are remoted. For debugging
// purposes only. Setting this to false will break moz-extension URI loading
// unless other process sandboxing and extension remoting prefs are changed.
pref("extensions.webextensions.protocol.remote", true);

// Enable userScripts API by default.
pref("extensions.webextensions.userScripts.enabled", true);
pref("extensions.userScripts.mv3.enabled", true);

// Whether or not the installed extensions should be migrated to the storage.local IndexedDB backend.
pref("extensions.webextensions.ExtensionStorageIDB.enabled", true);

// Whether to allow the inline options browser in HTML about:addons page.
pref("extensions.htmlaboutaddons.inline-options.enabled", true);
// Show recommendations on the extension and theme list views.
pref("extensions.htmlaboutaddons.recommendations.enabled", true);

// The URL for the privacy policy related to recommended add-ons.
pref("extensions.recommendations.privacyPolicyUrl", "");
// The URL for a recommended theme, shown on the theme page in about:addons.
pref("extensions.recommendations.themeRecommendationUrl", "");

// Report Site Issue button
// Note that on enabling the button in other release channels, make sure to
// disable it in problematic tests, see disableNonReleaseActions() inside
// browser/modules/test/browser/head.js
pref("extensions.webcompat-reporter.newIssueEndpoint", "https://webcompat.com/issues/new");
#if MOZ_UPDATE_CHANNEL != release && MOZ_UPDATE_CHANNEL != esr
  pref("extensions.webcompat-reporter.enabled", true);
#else
  pref("extensions.webcompat-reporter.enabled", false);
#endif

// Add-on content security policies.
pref("extensions.webextensions.base-content-security-policy", "script-src 'self' https://* http://localhost:* http://127.0.0.1:* moz-extension: blob: filesystem: 'unsafe-eval' 'wasm-unsafe-eval' 'unsafe-inline';");
pref("extensions.webextensions.base-content-security-policy.v3", "script-src 'self' 'wasm-unsafe-eval';");
pref("extensions.webextensions.default-content-security-policy", "script-src 'self' 'wasm-unsafe-eval';");
pref("extensions.webextensions.default-content-security-policy.v3", "script-src 'self'; upgrade-insecure-requests;");


pref("network.buffer.cache.count", 24);
pref("network.buffer.cache.size",  32768);

// Web Notification
pref("dom.webnotifications.requireinteraction.count", 3);

// DOM full-screen API.
#ifdef XP_MACOSX
  // Whether to use macOS native full screen for Fullscreen API
  pref("full-screen-api.macos-native-full-screen", true);
#endif
// transition duration of fade-to-black and fade-from-black, unit: ms
#ifndef MOZ_WIDGET_GTK
  pref("full-screen-api.transition-duration.enter", "200 200");
  pref("full-screen-api.transition-duration.leave", "200 200");
#else
  pref("full-screen-api.transition-duration.enter", "0 0");
  pref("full-screen-api.transition-duration.leave", "0 0");
#endif
// timeout for black screen in fullscreen transition, unit: ms
pref("full-screen-api.transition.timeout", 1000);
// time for the warning box stays on the screen before sliding out, unit: ms
pref("full-screen-api.warning.timeout", 3000);
// delay for the warning box to show when pointer stays on the top, unit: ms
pref("full-screen-api.warning.delay", 500);

// DOM pointerlock API
// time for the warning box stays on the screen before sliding out, unit: ms
pref("pointer-lock-api.warning.timeout", 3000);

// Push

pref("dom.push.loglevel", "Error");

pref("dom.push.serverURL", "wss://push.services.mozilla.com/");
pref("dom.push.userAgentID", "");

// The maximum number of push messages that a service worker can receive
// without user interaction.
pref("dom.push.maxQuotaPerSubscription", 16);

// The maximum number of recent message IDs to store for each push
// subscription, to avoid duplicates for unacknowledged messages.
pref("dom.push.maxRecentMessageIDsPerSubscription", 100);

// The delay between receiving a push message and updating the quota for a
// subscription.
pref("dom.push.quotaUpdateDelay", 3000); // 3 seconds

// Is the network connection allowed to be up?
// This preference should be used in UX to enable/disable push.
pref("dom.push.connection.enabled", true);

// Exponential back-off start is 5 seconds like in HTTP/1.1.
// Maximum back-off is pingInterval.
pref("dom.push.retryBaseInterval", 5000);

// Interval at which to ping PushServer to check connection status. In
// milliseconds. If no reply is received within requestTimeout, the connection
// is considered closed.
pref("dom.push.pingInterval", 1800000); // 30 minutes

// How long before we timeout
pref("dom.push.requestTimeout", 10000);

// How long must we wait before declaring that a window is a "ghost" (i.e., a
// likely leak)?  This should be longer than it usually takes for an eligible
// window to be collected via the GC/CC.
pref("memory.ghost_window_timeout_seconds", 60);

// Don't dump memory reports on OOM, by default.
pref("memory.dump_reports_on_oom", false);

// Number of stack frames to capture in createObjectURL for about:memory.
pref("memory.blob_report.stack_frames", 0);

// path to OSVR DLLs
pref("gfx.vr.osvr.utilLibPath", "");
pref("gfx.vr.osvr.commonLibPath", "");
pref("gfx.vr.osvr.clientLibPath", "");
pref("gfx.vr.osvr.clientKitLibPath", "");

// nsMemoryInfoDumper can watch a fifo in the temp directory and take various
// actions when the fifo is written to.  Disable this in general.
pref("memory_info_dumper.watch_fifo.enabled", false);

// If minInterval is 0, the check will only happen
// when the service has a strong suspicion we are in a captive portal
pref("network.captive-portal-service.minInterval", 60000); // 60 seconds
pref("network.captive-portal-service.maxInterval", 1500000); // 25 minutes
// Every 10 checks, the delay is increased by a factor of 5
pref("network.captive-portal-service.backoffFactor", "5.0");
pref("network.captive-portal-service.enabled", false);

pref("network.connectivity-service.enabled", true);
pref("network.connectivity-service.DNSv4.domain", "example.org");
pref("network.connectivity-service.DNSv6.domain", "example.org");
pref("network.connectivity-service.DNS_HTTPS.domain", "cloudflare-dns.com");
pref("network.connectivity-service.IPv4.url", "http://detectportal.firefox.com/success.txt?ipv4");
pref("network.connectivity-service.IPv6.url", "http://detectportal.firefox.com/success.txt?ipv6");

pref("network.trr.uri", "");
// credentials to pass to DOH end-point
pref("network.trr.credentials", "");
pref("network.trr.custom_uri", "");
// Before TRR is widely used the NS record for this host is fetched
// from the DOH end point to ensure proper configuration
pref("network.trr.confirmationNS", "example.com");
// Comma separated list of domains that we should not use TRR for
pref("network.trr.excluded-domains", "");
pref("network.trr.builtin-excluded-domains", "localhost,local");

pref("captivedetect.canonicalURL", "http://detectportal.firefox.com/canonical.html");
pref("captivedetect.canonicalContent", "<meta http-equiv=\"refresh\" content=\"0;url=https://support.mozilla.org/kb/captive-portal\"/>");
pref("captivedetect.maxWaitingTime", 5000);
pref("captivedetect.pollingTime", 3000);
pref("captivedetect.maxRetryCount", 5);

// The tables used for Safebrowsing phishing and malware checks
pref("urlclassifier.malwareTable", "goog-malware-proto,goog-unwanted-proto,moztest-harmful-simple,moztest-malware-simple,moztest-unwanted-simple");
#ifdef MOZILLA_OFFICIAL
  // In official builds, we are allowed to use Google's private phishing
  // list (see bug 1288840).
  pref("urlclassifier.phishTable", "goog-phish-proto,moztest-phish-simple");
#else
  pref("urlclassifier.phishTable", "googpub-phish-proto,moztest-phish-simple");
#endif

// Tables for application reputation
pref("urlclassifier.downloadAllowTable", "goog-downloadwhite-proto");
pref("urlclassifier.downloadBlockTable", "goog-badbinurl-proto");

// Tables for anti-tracking features
pref("urlclassifier.trackingAnnotationTable", "moztest-track-simple,ads-track-digest256,social-track-digest256,analytics-track-digest256,content-track-digest256");
pref("urlclassifier.trackingAnnotationWhitelistTable", "moztest-trackwhite-simple,mozstd-trackwhite-digest256,google-trackwhite-digest256");
pref("urlclassifier.trackingTable", "moztest-track-simple,ads-track-digest256,social-track-digest256,analytics-track-digest256");
pref("urlclassifier.trackingWhitelistTable", "moztest-trackwhite-simple,mozstd-trackwhite-digest256,google-trackwhite-digest256");

pref("urlclassifier.features.fingerprinting.blacklistTables", "base-fingerprinting-track-digest256");
pref("urlclassifier.features.fingerprinting.whitelistTables", "mozstd-trackwhite-digest256,google-trackwhite-digest256");
pref("urlclassifier.features.fingerprinting.annotate.blacklistTables", "base-fingerprinting-track-digest256");
pref("urlclassifier.features.fingerprinting.annotate.whitelistTables", "mozstd-trackwhite-digest256,google-trackwhite-digest256");
pref("urlclassifier.features.cryptomining.blacklistTables", "base-cryptomining-track-digest256");
pref("urlclassifier.features.cryptomining.whitelistTables", "mozstd-trackwhite-digest256");
pref("urlclassifier.features.cryptomining.annotate.blacklistTables", "base-cryptomining-track-digest256");
pref("urlclassifier.features.cryptomining.annotate.whitelistTables", "mozstd-trackwhite-digest256");
pref("urlclassifier.features.socialtracking.blacklistTables", "social-tracking-protection-facebook-digest256,social-tracking-protection-linkedin-digest256,social-tracking-protection-twitter-digest256");
pref("urlclassifier.features.socialtracking.whitelistTables", "mozstd-trackwhite-digest256,google-trackwhite-digest256");
pref("urlclassifier.features.socialtracking.annotate.blacklistTables", "social-tracking-protection-facebook-digest256,social-tracking-protection-linkedin-digest256,social-tracking-protection-twitter-digest256");
pref("urlclassifier.features.socialtracking.annotate.whitelistTables", "mozstd-trackwhite-digest256,google-trackwhite-digest256");
pref("urlclassifier.features.emailtracking.blocklistTables", "base-email-track-digest256");
pref("urlclassifier.features.emailtracking.allowlistTables", "mozstd-trackwhite-digest256");
pref("urlclassifier.features.emailtracking.datacollection.blocklistTables", "base-email-track-digest256,content-email-track-digest256");
pref("urlclassifier.features.emailtracking.datacollection.allowlistTables", "mozstd-trackwhite-digest256");
pref("urlclassifier.features.consentmanager.annotate.blocklistTables", "consent-manager-track-digest256");
pref("urlclassifier.features.consentmanager.annotate.allowlistTables", "mozstd-trackwhite-digest256");

// These tables will never trigger a gethash call.
pref("urlclassifier.disallow_completions", "goog-downloadwhite-digest256,base-track-digest256,mozstd-trackwhite-digest256,content-track-digest256,mozplugin-block-digest256,mozplugin2-block-digest256,ads-track-digest256,social-track-digest256,analytics-track-digest256,base-fingerprinting-track-digest256,content-fingerprinting-track-digest256,base-cryptomining-track-digest256,content-cryptomining-track-digest256,fanboyannoyance-ads-digest256,fanboysocial-ads-digest256,easylist-ads-digest256,easyprivacy-ads-digest256,adguard-ads-digest256,social-tracking-protection-digest256,social-tracking-protection-facebook-digest256,social-tracking-protection-linkedin-digest256,social-tracking-protection-twitter-digest256,base-email-track-digest256,content-email-track-digest256,consent-manager-track-digest256");

// Workaround for Google Recaptcha
pref("urlclassifier.trackingAnnotationSkipURLs", "");
pref("privacy.rejectForeign.allowList", "");

// The list of email webapp sites
pref("privacy.trackingprotection.emailtracking.webapp.domains", "mail.163.com,mail.aol.com,fastmail.com,webmail.gandi.net,mail.google.com,navigator-bs.gmx.com,app.hey.com,horde.org/apps/webmail,hushmail.com,icloud.com/mail,kolabnow.com,laposte.net/accueil,mail.lycos.com,mail.com/mail/,mail.ru,mailfence.com,outlook.live.com,email-postaci.com/,posteo.de,mail.protonmail.com,app.rackspace.com,mail.rediff.com,emailmg.ipage.com,runbox.com,mail.sina.com.cn,tutanota.com,mail.yahoo.com,mail.yandex.com,mail.zimbra.com,zoho.com/mail/");

// Number of random entries to send with a gethash request
pref("urlclassifier.gethashnoise", 4);

// Gethash timeout for Safe Browsing
pref("urlclassifier.gethash.timeout_ms", 5000);

// Name of the about: page to display Safe Browsing warnings (bug 399233)
pref("urlclassifier.alternate_error_page", "blocked");

// Enable safe-browsing debugging
pref("browser.safebrowsing.debug", false);

// Allow users to ignore Safe Browsing warnings.
pref("browser.safebrowsing.allowOverride", true);

// These names are approved by the Google Safe Browsing team.
// Any changes must be coordinated with them.
#ifdef MOZILLA_OFFICIAL
  pref("browser.safebrowsing.id", "navclient-auto-ffox");
#else
  pref("browser.safebrowsing.id", "Firefox");
#endif

// Download protection
pref("browser.safebrowsing.downloads.enabled", true);
pref("browser.safebrowsing.downloads.remote.enabled", true);
pref("browser.safebrowsing.downloads.remote.timeout_ms", 15000);
pref("browser.safebrowsing.downloads.remote.url", "https://sb-ssl.google.com/safebrowsing/clientreport/download?key=%GOOGLE_SAFEBROWSING_API_KEY%");
pref("browser.safebrowsing.downloads.remote.block_dangerous",            true);
pref("browser.safebrowsing.downloads.remote.block_dangerous_host",       true);
pref("browser.safebrowsing.downloads.remote.block_potentially_unwanted", true);
pref("browser.safebrowsing.downloads.remote.block_uncommon",             true);

// Android SafeBrowsing's configuration is in ContentBlocking.java, keep in sync.
#ifndef MOZ_WIDGET_ANDROID

// Google Safe Browsing provider (legacy)
pref("browser.safebrowsing.provider.google.pver", "2.2");
pref("browser.safebrowsing.provider.google.lists", "goog-badbinurl-shavar,goog-downloadwhite-digest256,goog-phish-shavar,googpub-phish-shavar,goog-malware-shavar,goog-unwanted-shavar");
pref("browser.safebrowsing.provider.google.updateURL", "https://safebrowsing.google.com/safebrowsing/downloads?client=SAFEBROWSING_ID&appver=%MAJOR_VERSION%&pver=2.2&key=%GOOGLE_SAFEBROWSING_API_KEY%");
pref("browser.safebrowsing.provider.google.gethashURL", "https://safebrowsing.google.com/safebrowsing/gethash?client=SAFEBROWSING_ID&appver=%MAJOR_VERSION%&pver=2.2");
pref("browser.safebrowsing.provider.google.reportURL", "https://safebrowsing.google.com/safebrowsing/diagnostic?site=");
pref("browser.safebrowsing.provider.google.reportPhishMistakeURL", "https://%LOCALE%.phish-error.mozilla.com/?url=");
pref("browser.safebrowsing.provider.google.reportMalwareMistakeURL", "https://%LOCALE%.malware-error.mozilla.com/?url=");
pref("browser.safebrowsing.provider.google.advisoryURL", "https://developers.google.com/safe-browsing/v4/advisory");
pref("browser.safebrowsing.provider.google.advisoryName", "Google Safe Browsing");

// Google Safe Browsing provider
pref("browser.safebrowsing.provider.google4.pver", "4");
pref("browser.safebrowsing.provider.google4.lists", "goog-badbinurl-proto,goog-downloadwhite-proto,goog-phish-proto,googpub-phish-proto,goog-malware-proto,goog-unwanted-proto,goog-harmful-proto");
pref("browser.safebrowsing.provider.google4.updateURL", "https://safebrowsing.googleapis.com/v4/threatListUpdates:fetch?$ct=application/x-protobuf&key=%GOOGLE_SAFEBROWSING_API_KEY%&$httpMethod=POST");
pref("browser.safebrowsing.provider.google4.gethashURL", "https://safebrowsing.googleapis.com/v4/fullHashes:find?$ct=application/x-protobuf&key=%GOOGLE_SAFEBROWSING_API_KEY%&$httpMethod=POST");
pref("browser.safebrowsing.provider.google4.reportURL", "https://safebrowsing.google.com/safebrowsing/diagnostic?site=");
pref("browser.safebrowsing.provider.google4.reportPhishMistakeURL", "https://%LOCALE%.phish-error.mozilla.com/?url=");
pref("browser.safebrowsing.provider.google4.reportMalwareMistakeURL", "https://%LOCALE%.malware-error.mozilla.com/?url=");
pref("browser.safebrowsing.provider.google4.advisoryURL", "https://developers.google.com/safe-browsing/v4/advisory");
pref("browser.safebrowsing.provider.google4.advisoryName", "Google Safe Browsing");
pref("browser.safebrowsing.provider.google4.dataSharingURL", "https://safebrowsing.googleapis.com/v4/threatHits?$ct=application/x-protobuf&key=%GOOGLE_SAFEBROWSING_API_KEY%&$httpMethod=POST");
pref("browser.safebrowsing.provider.google4.dataSharing.enabled", false);

#endif // ifndef MOZ_WIDGET_ANDROID

pref("browser.safebrowsing.reportPhishURL", "https://%LOCALE%.phish-report.mozilla.com/?url=");

// Mozilla Safe Browsing provider (for tracking protection and plugin blocking)
pref("browser.safebrowsing.provider.mozilla.pver", "2.2");
pref("browser.safebrowsing.provider.mozilla.lists", "base-track-digest256,mozstd-trackwhite-digest256,google-trackwhite-digest256,content-track-digest256,mozplugin-block-digest256,mozplugin2-block-digest256,ads-track-digest256,social-track-digest256,analytics-track-digest256,base-fingerprinting-track-digest256,content-fingerprinting-track-digest256,base-cryptomining-track-digest256,content-cryptomining-track-digest256,fanboyannoyance-ads-digest256,fanboysocial-ads-digest256,easylist-ads-digest256,easyprivacy-ads-digest256,adguard-ads-digest256,social-tracking-protection-digest256,social-tracking-protection-facebook-digest256,social-tracking-protection-linkedin-digest256,social-tracking-protection-twitter-digest256,base-email-track-digest256,content-email-track-digest256,consent-manager-track-digest256");
pref("browser.safebrowsing.provider.mozilla.updateURL", "moz-sbrs:://antitracking");
pref("browser.safebrowsing.provider.mozilla.gethashURL", "https://shavar.services.mozilla.com/gethash?client=SAFEBROWSING_ID&appver=%MAJOR_VERSION%&pver=2.2");
// Set to a date in the past to force immediate download in new profiles.
pref("browser.safebrowsing.provider.mozilla.nextupdatetime", "1");
// Block lists for tracking protection. The name values will be used as the keys
// to lookup the localized name in preferences.properties.
pref("browser.safebrowsing.provider.mozilla.lists.base", "moz-std");
pref("browser.safebrowsing.provider.mozilla.lists.content", "moz-full");

// The table and global pref for blocking plugin content
#ifdef NIGHTLY_BUILD
  pref("urlclassifier.blockedTable", "moztest-block-simple,mozplugin-block-digest256");
#else
  pref("urlclassifier.blockedTable", "moztest-block-simple");
#endif

#ifdef XP_MACOSX
  #if !defined(RELEASE_OR_BETA) || defined(DEBUG)
    // In non-release builds we crash by default on insecure text input (when a
    // password editor has focus but secure event input isn't enabled).  The
    // following pref, when turned on, disables this behavior.  See bug 1188425.
    pref("intl.allow-insecure-text-input", false);
  #endif
#endif // XP_MACOSX

// Search service settings
pref("browser.search.log", false);
pref("browser.search.update", true);
pref("browser.search.suggest.enabled", true);
pref("browser.search.suggest.enabled.private", false);
pref("browser.search.separatePrivateDefault", true);
pref("browser.search.separatePrivateDefault.ui.enabled", false);
pref("browser.search.removeEngineInfobar.enabled", true);
// Temporary preference to allow switching between the Rust based search engine
// selector and the JavaScript one (bug 1914143).
#ifdef EARLY_BETA_OR_EARLIER
  pref("browser.search.rustSelector.featureGate", true);
#else
  pref("browser.search.rustSelector.featureGate", false);
#endif

// GMPInstallManager prefs

// User-settable override to media.gmp-manager.url for testing purposes.
//pref("media.gmp-manager.url.override", "");

// When |media.gmp-manager.allowLocalSources| is true, we will allow falling
// back to using the plugin configurations distributed with Firefox to update
// or install plugins. This fallback is only used when we fail to get an
// acceptable configuration via |media.gmp-manager.url|.
pref("media.gmp-manager.allowLocalSources", true);

// Update service URL for GMP install/updates:
pref("media.gmp-manager.url", "https://aus5.mozilla.org/update/3/GMP/%VERSION%/%BUILD_ID%/%BUILD_TARGET%/%LOCALE%/%CHANNEL%/%OS_VERSION%/%DISTRIBUTION%/%DISTRIBUTION_VERSION%/update.xml");

// When |media.gmp-manager.checkContentSignature| is true, then the reply
// containing the update xml file is expected to provide a content signature
// header. Information from this header will be used to validate the response.
// If this header is not present, is malformed, or cannot be determined as
// valid then the update will fail.
// We should eventually remove this pref and any cert pinning code and make
// the content signature path the sole path. We retain this for now in case
// we need to debug content sig vs cert pin.
pref("media.gmp-manager.checkContentSignature", true);

// When |media.gmp-manager.cert.requireBuiltIn| is true or not specified the
// final certificate and all certificates the connection is redirected to before
// the final certificate for the url specified in the |media.gmp-manager.url|
// preference must be built-in. The check related to this pref is not done if
// |media.gmp-manager.checkContentSignature| is set to true (the content
// signature check provides protection that supersedes the built in
// requirement).
pref("media.gmp-manager.cert.requireBuiltIn", true);

// The |media.gmp-manager.certs.| preference branch contains branches that are
// sequentially numbered starting at 1 that contain attribute name / value
// pairs for the certificate used by the server that hosts the update xml file
// as specified in the |media.gmp-manager.url| preference. When these preferences are
// present the following conditions apply for a successful update check:
// 1. the uri scheme must be https
// 2. the preference name must exist as an attribute name on the certificate and
//    the value for the name must be the same as the value for the attribute name
//    on the certificate.
// If these conditions aren't met it will be treated the same as when there is
// no update available. This validation will not be performed when the
// |media.gmp-manager.url.override| user preference has been set for testing updates or
// when the |media.gmp-manager.cert.checkAttributes| preference is set to false.
// This check will also not be done if the |media.gmp-manager.checkContentSignature|
// pref is set to true. Also, the |media.gmp-manager.url.override| preference should
// ONLY be used for testing.
// IMPORTANT! app.update.certs.* prefs should also be updated if these
// are updated.
pref("media.gmp-manager.cert.checkAttributes", true);
pref("media.gmp-manager.certs.1.issuerName", "CN=DigiCert SHA2 Secure Server CA,O=DigiCert Inc,C=US");
pref("media.gmp-manager.certs.1.commonName", "aus5.mozilla.org");
pref("media.gmp-manager.certs.2.issuerName", "CN=thawte SSL CA - G2,O=\"thawte, Inc.\",C=US");
pref("media.gmp-manager.certs.2.commonName", "aus5.mozilla.org");

// Whether or not to perform reader mode article parsing on page load.
// If this pref is disabled, we will never show a reader mode icon in the toolbar.
pref("reader.parse-on-load.enabled", true);

// After what size document we don't bother running Readability on it
// because it'd slow things down too much
pref("reader.parse-node-limit", 3000);

// Whether or not debug mode is enabled in reader mode. If enabled, reader mode
// will log what it's doing.
pref("reader.debug", false);

// Whether we include full URLs in browser console errors. This is disabled
// by default because some platforms will persist these, leading to privacy issues.
pref("reader.errors.includeURLs", false);

// The default relative font size in reader mode (1-9)
pref("reader.font_size", 5);

// The font type in reader (sans-serif, serif, monospace)
pref("reader.font_type", "sans-serif");

// Default font types available in reader mode.
pref("reader.font_type.values", "[\"sans-serif\",\"serif\",\"monospace\"]");

// The default font weight in reader mode (regular, light, bold)
pref("reader.font_weight", "regular");

// Font weight values available in reader mode.
pref("reader.font_weight.values", "[\"regular\",\"light\",\"bold\"]");

// The default relative content width in reader mode (1-9)
pref("reader.content_width", 3);

// The default relative line height in reader mode (1-9)
pref("reader.line_height", 4);

// The default character spacing in reader mode (1-9)
pref("reader.character_spacing", 0);

// The default word spacing in reader mode (1-9)
pref("reader.word_spacing", 0);

// The default text alignment direction in reader mode
pref("reader.text_alignment", "start");

// The default color scheme in reader mode (light, dark, sepia, auto, contrast, gray)
// auto = color automatically adjusts according to ambient light level
// (auto only works on platforms where the 'devicelight' event is enabled)
pref("reader.color_scheme", "auto");

// Color scheme values available in reader mode UI.
pref("reader.color_scheme.values", "[\"auto\",\"light\",\"dark\",\"sepia\",\"contrast\",\"gray\"]");

// The custom color scheme options in reader colors menu.
pref("reader.custom_colors.foreground", "");
pref("reader.custom_colors.background", "");

pref("reader.custom_colors.unvisited-links", "");
pref("reader.custom_colors.visited-links", "");

pref("reader.custom_colors.selection-highlight", "");

// Whether to use a vertical or horizontal toolbar.
pref("reader.toolbar.vertical", true);

#if !defined(ANDROID)
  pref("narrate.enabled", true);
#else
  pref("narrate.enabled", false);
#endif

pref("narrate.test", false);
pref("narrate.rate", 0);
pref("narrate.voice", " { \"default\": \"automatic\" }");
// Only make voices that match content language available.
pref("narrate.filter-voices", true);

pref("memory.report_concurrency", 10);

pref("toolkit.pageThumbs.screenSizeDivisor", 7);
pref("toolkit.pageThumbs.minWidth", 0);
pref("toolkit.pageThumbs.minHeight", 0);

// 16MB default non-parseable upload limit for requestBody.raw.bytes
pref("webextensions.webRequest.requestBodyMaxRawBytes", 16777216);

// Enforce a 10MB quota in the storage.session extension API.
pref("webextensions.storage.session.enforceQuota", true);

// Should we use the old kinto-based implementation of storage.sync? To be removed in bug 1637465.
pref("webextensions.storage.sync.kinto", false);
// Server used by the old kinto-based implementation of storage.sync.
pref("webextensions.storage.sync.serverURL", "https://webextensions.settings.services.mozilla.com/v1");

// Allow customization of the fallback directory for file uploads
pref("dom.input.fallbackUploadDir", "");

// Turn rewriting of youtube embeds on/off
pref("plugins.rewrite_youtube_embeds", true);

// Default media volume
pref("media.default_volume", "1.0");

pref("dom.storageManager.prompt.testing", false);
pref("dom.storageManager.prompt.testing.allow", false);


pref("browser.storageManager.pressureNotification.minIntervalMS", 1200000);
pref("browser.storageManager.pressureNotification.usageThresholdGB", 5);

pref("browser.sanitizer.loglevel", "Warn");

// Enable Firefox translations powered by the Bergamot translations
// engine https://browser.mt/. See Bug 971044. Note that this pref can be turned
// on in different apps like Firefox Desktop, even if it's disabled by default here.
pref("browser.translations.enable", false);
// Enable Firefox Select translations powered by the Bergamot translations
// engine https://browser.mt/. Note that this pref can be turned on in different apps
// like Firefox Desktop, even if it's disabled by default here.
pref("browser.translations.select.enable", false);
// Set to "All" to see all logs, which are useful for debugging. Set to "Info" to see
// the application logic logs, and not all of the translated messages, which can be
// slow and overwhelming.
pref("browser.translations.logLevel", "Error");
// The BCP-47 language tags of the most recently translated-into target language.
// This preference is considered when offering a specific language to translate into,
// but is not considered as a "known" language when deciding whether to offer Translations at all.
pref("browser.translations.mostRecentTargetLanguages", "");
// A comma-separated list of BCP-47 language tags that affect the behavior of translations.
// Languages listed in the alwaysTranslateLanguages list will trigger auto-translate on page load.
pref("browser.translations.alwaysTranslateLanguages", "");
// A comma-separated list of BCP-47 language tags that affect the behavior of translations.
// Languages listed in the neverTranslateLanguages list will signal that the translations button
// and menus should not be displayed automatically when visiting pages in those languages.
pref("browser.translations.neverTranslateLanguages", "");
// By default the translations engine on about:translations uses text for translation,
// and the full page translations uses HTML. Set this pref to true to use the HTML
// translation behavior on about:translations. Requires a page refresh.
pref("browser.translations.useHTML", false);
// Automatically popup an offer to translate on sites.
pref("browser.translations.automaticallyPopup", true);
// Simulate the behavior of using a device that does not support the translations engine.
// Requires restart.
// Enables or disables the usage of lexical shortlisting for the translation models.
// Using a lexical shortlist will increase translation speed, but may reduce quality.
pref("browser.translations.useLexicalShortlist", false);
pref("browser.translations.simulateUnsupportedEngine", false);
// The translations code relies on asynchronous network request. Chaos mode simulates
// flaky and slow network connections, so that the UI may be manually tested. The
// "chaos.errors" makes network requests fail, while "timeoutMS" randomly times out
// between 0ms and the timeoutMS provided.
pref("browser.translations.chaos.errors", false);
pref("browser.translations.chaos.timeoutMS", 0);

// Enable the experimental machine learning inference engine.
pref("browser.ml.enable", true);
// Set to "All" to see all logs, which are useful for debugging.
pref("browser.ml.logLevel", "Error");
// Model hub root URL used to download models.
pref("browser.ml.modelHubRootUrl", "https://model-hub.mozilla.org/");
// Model URL template
pref("browser.ml.modelHubUrlTemplate", "{model}/{revision}");
// Maximum disk size for ML model cache (in GiB)
pref("browser.ml.modelCacheMaxSize", 4);
// Model cache timeout in ms
pref("browser.ml.modelCacheTimeout", 120000);
// Minimal Physical RAM required in GiB
pref("browser.ml.minimumPhysicalMemory", 4);
// Check for memory before running
pref("browser.ml.checkForMemory", true);
// Allowed overrides for various ml features
pref("browser.ml.overridePipelineOptions", "{}");

// When a user cancels this number of authentication dialogs coming from
// a single web page in a row, all following authentication dialogs will
// be blocked (automatically canceled) for that page. The counter resets
// when the page is reloaded.
// To disable all auth prompting, set the limit to 0.
// To disable blocking of auth prompts, set the limit to -1.
pref("prompts.authentication_dialog_abuse_limit", 2);

pref("dom.payments.request.supportedRegions", "US,CA");

#ifdef MOZ_ASAN_REPORTER
  pref("asanreporter.apiurl", "https://anf1.fuzzing.mozilla.org/crashproxy/submit/");
  pref("asanreporter.clientid", "unknown");
  pref("toolkit.telemetry.overrideUpdateChannel", "nightly-asan");
#endif

// If `true`, about:processes shows in-process subframes.
pref("toolkit.aboutProcesses.showAllSubframes", false);
// If `true`, about:processes shows thread information.
#ifdef NIGHTLY_BUILD
  pref("toolkit.aboutProcesses.showThreads", true);
#else
  pref("toolkit.aboutProcesses.showThreads", false);
#endif
// If `true`, about:processes will offer to profile processes.
pref("toolkit.aboutProcesses.showProfilerIcons", true);
// Time in seconds between when the profiler is started and when the
// profile is captured.
pref("toolkit.aboutProcesses.profileDuration", 5);

// This preference controls how about:logging handles profiles when stopping:
// either it opens the profile in a new tab on profiler.firefox.com, or it
// uploads it directly to the cloud storage, providing the URL.
// On Android, it's not currently possible to capture a profile this way,
// therefore the profile is uploaded by default.
#if !defined(MOZ_WIDGET_ANDROID)
  pref("toolkit.aboutLogging.uploadProfileToCloud", false);
#else
  pref("toolkit.aboutLogging.uploadProfileToCloud", true);
#endif
// The pref "toolkit.aboutlogging.uploadProfileUrl" can also be set to change
// the upload endpoint. The comment below shows the default value. It's not
// defined usually because we don't expect our users, even advanced, to change
// it, and therefore this will likely only be used in our tests.
// pref("toolkit.aboutlogging.uploadProfileUrl", "https://api.profiler.firefox.com/compressed-store");


// When a crash happens, whether to include heap regions of the crash context
// in the minidump. Enabled by default on nightly and aurora.
#ifdef RELEASE_OR_BETA
  pref("toolkit.crashreporter.include_context_heap", false);
#else
  pref("toolkit.crashreporter.include_context_heap", true);
#endif

// Support for legacy customizations that rely on checking the
// user profile directory for these stylesheets:
//  * userContent.css
//  * userChrome.css
pref("toolkit.legacyUserProfileCustomizations.stylesheets", false);

#ifdef MOZ_DATA_REPORTING
  pref("datareporting.policy.dataSubmissionEnabled", true);
  pref("datareporting.policy.dataSubmissionPolicyNotifiedTime", "0");
  pref("datareporting.policy.dataSubmissionPolicyAcceptedVersion", 0);
  pref("datareporting.policy.dataSubmissionPolicyBypassNotification", false);
  pref("datareporting.policy.currentPolicyVersion", 2);
  pref("datareporting.policy.minimumPolicyVersion", 1);
  pref("datareporting.policy.minimumPolicyVersion.channel-beta", 2);
  pref("datareporting.policy.firstRunURL", "https://www.mozilla.org/privacy/firefox/");
#endif

#ifdef MOZ_SERVICES_HEALTHREPORT
  #if !defined(ANDROID)
    pref("datareporting.healthreport.infoURL", "https://www.mozilla.org/legal/privacy/firefox.html#health-report");

    // Health Report is enabled by default on all channels.
    // Do note that the toggle on Fenix and Focus does NOT reflect to this pref.
    pref("datareporting.healthreport.uploadEnabled", true);
    pref("datareporting.usage.uploadEnabled", true);
  #endif
#endif

pref("services.common.log.logger.rest.request", "Debug");
pref("services.common.log.logger.rest.response", "Debug");
pref("services.common.log.logger.tokenserverclient", "Debug");

#ifdef MOZ_SERVICES_SYNC
  pref("services.sync.lastversion", "firstrun");
  pref("services.sync.sendVersionInfo", true);

  pref("services.sync.scheduler.idleInterval", 3600);  // 1 hour
  pref("services.sync.scheduler.activeInterval", 600);   // 10 minutes
  pref("services.sync.scheduler.immediateInterval", 90);    // 1.5 minutes
  pref("services.sync.scheduler.idleTime", 300);   // 5 minutes

  pref("services.sync.scheduler.fxa.singleDeviceInterval", 3600); // 1 hour

  // Note that new engines are typically added with a default of disabled, so
  // when an existing sync user gets the Firefox upgrade that supports the engine
  // it starts as disabled until the user has explicitly opted in.
  // The sync "create account" process typically *will* offer these engines, so
  // they may be flipped to enabled at that time.
  pref("services.sync.engine.addons", true);
  pref("services.sync.engine.addresses", false);
  pref("services.sync.engine.bookmarks", true);
  pref("services.sync.engine.creditcards", false);
  pref("services.sync.engine.history", true);
  pref("services.sync.engine.passwords", true);
  pref("services.sync.engine.prefs", true);
  pref("services.sync.engine.tabs", true);
  pref("services.sync.engine.tabs.filteredSchemes", "about|resource|chrome|file|blob|moz-extension|data");

  // The addresses and CC engines might not actually be available at all.
  pref("services.sync.engine.addresses.available", false);
  pref("services.sync.engine.creditcards.available", false);

  // If true, add-on sync ignores changes to the user-enabled flag. This
  // allows people to have the same set of add-ons installed across all
  // profiles while maintaining different enabled states.
  pref("services.sync.addons.ignoreUserEnabledChanges", false);

  // Comma-delimited list of hostnames to trust for add-on install.
  pref("services.sync.addons.trustedSourceHostnames", "addons.mozilla.org");

  pref("services.sync.log.appender.console", "Fatal");
  pref("services.sync.log.appender.dump", "Error");
  pref("services.sync.log.appender.file.level", "Trace");
  pref("services.sync.log.appender.file.logOnError", true);
  #if defined(NIGHTLY_BUILD)
    pref("services.sync.log.appender.file.logOnSuccess", true);
  #else
    pref("services.sync.log.appender.file.logOnSuccess", false);
  #endif
  pref("services.sync.log.appender.file.maxErrorAge", 864000); // 10 days

  // The default log level for all "Sync.*" logs. Adjusting this pref will
  // adjust the level for *all* Sync logs (except engines, and that's only
  // because we supply a default for the engines below.)
  pref("services.sync.log.logger", "Debug");

  // Prefs for Sync engines can be controlled globally or per-engine.
  // We only define the global level here, but manually creating prefs
  // like "services.sync.log.logger.engine.bookmarks" will control just
  // that engine.
  pref("services.sync.log.logger.engine", "Debug");
  pref("services.sync.log.cryptoDebug", false);

  pref("services.sync.telemetry.submissionInterval", 43200); // 12 hours in seconds
  pref("services.sync.telemetry.maxPayloadCount", 500);

  #ifdef EARLY_BETA_OR_EARLIER
    // Enable the (fairly costly) client/server validation through early Beta, but
    // not release candidates or Release.
    pref("services.sync.engine.bookmarks.validation.enabled", true);
    pref("services.sync.engine.passwords.validation.enabled", true);
  #endif

  // We consider validation this frequently. After considering validation, even
  // if we don't end up validating, we won't try again unless this much time has passed.
  pref("services.sync.engine.bookmarks.validation.interval", 86400); // 24 hours in seconds
  pref("services.sync.engine.passwords.validation.interval", 86400); // 24 hours in seconds

  // We only run validation `services.sync.validation.percentageChance` percent of
  // the time, even if it's been the right amount of time since the last validation,
  // and you meet the maxRecord checks.
  pref("services.sync.engine.bookmarks.validation.percentageChance", 10);
  pref("services.sync.engine.passwords.validation.percentageChance", 10);

  // We won't validate an engine if it has more than this many records on the server.
  pref("services.sync.engine.bookmarks.validation.maxRecords", 1000);
  pref("services.sync.engine.passwords.validation.maxRecords", 1000);

  // The maximum number of immediate resyncs to trigger for changes made during
  // a sync.
  pref("services.sync.maxResyncs", 1);

  // The URL of the Firefox Accounts auth server backend
  pref("identity.fxaccounts.auth.uri", "https://api.accounts.firefox.com/v1");

  // Percentage chance we skip an extension storage sync (kinto life support).
  pref("services.sync.extension-storage.skipPercentageChance", 50);
#endif // MOZ_SERVICES_SYNC

#if defined(ENABLE_WEBDRIVER)
  // WebDriver is a remote control interface that enables introspection and
  // control of user agents. It provides a platform- and language-neutral wire
  // protocol as a way for out-of-process programs to remotely instruct the
  // behavior of web browsers.
  //
  // Gecko's implementation is backed by Marionette (WebDriver HTTP) and the
  // Remote Agent (WebDriver BiDi).

  // Delay server startup until a modal dialogue has been clicked to allow time
  // for user to set breakpoints in the Browser Toolbox.
  pref("marionette.debugging.clicktostart", false);

  // Port to start Marionette server on.
  pref("marionette.port", 2828);

  // Defines the protocols that will be active for the Remote Agent.
  // 1: WebDriver BiDi
  // 2: CDP (Chrome DevTools Protocol)
  // 3: WebDriver BiDi + CDP
  pref("remote.active-protocols", 1);

  // Enable WebDriver BiDi experimental commands and events.
  #if defined(NIGHTLY_BUILD)
    pref("remote.experimental.enabled", true);
  #else
    pref("remote.experimental.enabled", false);
  #endif

  // Defines the verbosity of the internal logger.
  //
  // Available levels are, in descending order of severity, "Trace", "Debug",
  // "Config", "Info", "Warn", "Error", and "Fatal". The value is treated
  // case-sensitively.
  pref("remote.log.level", "Info");

  // Certain log messages that are known to be long are truncated. This
  // preference causes them to not be truncated.
  pref("remote.log.truncate", true);

  // Sets recommended automation preferences when Remote Agent or Marionette is
  // started.
  pref("remote.prefs.recommended", true);

  // Enable retrying to execute commands in the child process in case the
  // JSWindowActor gets destroyed.
  pref("remote.retry-on-abort", true);
#endif

// Enable the JSON View tool (an inspector for application/json documents).
pref("devtools.jsonview.enabled", true);

// Default theme ("auto", "dark" or "light").
pref("devtools.theme", "auto", sticky);

// Completely disable DevTools entry points, as well as all DevTools command
// line arguments.
pref("devtools.policy.disabled", false);

// Enable deprecation warnings.
pref("devtools.errorconsole.deprecation_warnings", true);

#ifdef NIGHTLY_BUILD
  // Don't show the Browser Toolbox prompt on local builds / nightly.
  pref("devtools.debugger.prompt-connection", false, sticky);
#else
  pref("devtools.debugger.prompt-connection", true, sticky);
#endif

#ifdef MOZILLA_OFFICIAL
  // Disable debugging chrome.
  pref("devtools.chrome.enabled", false, sticky);
  // Disable remote debugging connections.
  pref("devtools.debugger.remote-enabled", false, sticky);
#else
  // In local builds, enable the browser toolbox by default.
  pref("devtools.chrome.enabled", true, sticky);
  pref("devtools.debugger.remote-enabled", true, sticky);
#endif

// Enable service worker debugging on all channels.
pref("devtools.debugger.features.windowless-service-workers", true);

// Disable remote debugging protocol logging.
pref("devtools.debugger.log", false);
pref("devtools.debugger.log.verbose", false);

pref("devtools.debugger.remote-port", 6000);
pref("devtools.debugger.remote-websocket", false);
// Force debugger server binding on the loopback interface.
pref("devtools.debugger.force-local", true);

// Limit for intercepted request and response bodies (1 MB).
// Possible values:
// 0 => the response body has no limit
// n => represents max number of bytes stored
pref("devtools.netmonitor.responseBodyLimit", 1048576);
pref("devtools.netmonitor.requestBodyLimit", 1048576);

// Limit for WebSocket/EventSource messages (100 KB).
pref("devtools.netmonitor.msg.messageDataLimit", 100000);

// DevTools default color unit.
pref("devtools.defaultColorUnit", "authored");

// Used for devtools debugging.
pref("devtools.dump.emit", false);

// Disable device discovery logging.
pref("devtools.discovery.log", false);

// The extension ID for devtools-adb-extension.
pref("devtools.remote.adb.extensionID", "adb@mozilla.org");
// The URL for for devtools-adb-extension (overridden in tests to a local
// path).
pref("devtools.remote.adb.extensionURL", "https://ftp.mozilla.org/pub/labs/devtools/adb-extension/#OS#/adb-extension-latest-#OS#.xpi");

// Enable Inactive CSS detection; used both by the client and the server.
pref("devtools.inspector.inactive.css.enabled", true);

// Should F12 open the Developer Tools toolbox.
pref("devtools.f12_enabled", true);

#if defined(NIGHTLY_BUILD) || defined(MOZ_DEV_EDITION)
// Define in StaticPrefList.yaml and override here since StaticPrefList.yaml
// doesn't provide a way to lock the pref
pref("dom.postMessage.sharedArrayBuffer.bypassCOOP_COEP.insecure.enabled", false);
#else
pref("dom.postMessage.sharedArrayBuffer.bypassCOOP_COEP.insecure.enabled", false, locked);
#endif

// Preferences for the form autofill toolkit component.
// The truthy values of "extensions.formautofill.addresses.available"
// is "on" and "detect",
// any other value means autofill isn't available.
// "detect" means it's enabled if conditions defined in the extension are met.
// Note: "extensions.formautofill.available"
// is not being used in form autofill, but need to exist for migration purposes.
pref("extensions.formautofill.available", "detect");
pref("extensions.formautofill.addresses.supported", "detect");
pref("extensions.formautofill.addresses.enabled", true);
pref("extensions.formautofill.addresses.capture.enabled", true);
#if defined(ANDROID)
  // On android we have custom logic to control this. Ideally we should use nimbus there as well.
  // https://github.com/mozilla-mobile/firefox-android/blob/d566743ea0f041ce27c1204da903de380f96b46e/fenix/app/src/main/java/org/mozilla/fenix/utils/Settings.kt#L1502-L1510
  pref("extensions.formautofill.addresses.experiments.enabled", true);
#else
  // Whether address autofill is enabled or not ( this is set via Nimbus )
  pref("extensions.formautofill.addresses.experiments.enabled", false);
#endif
pref("extensions.formautofill.addresses.ignoreAutocompleteOff", true);
// Supported countries need to follow ISO 3166-1 to align with "browser.search.region"
pref("extensions.formautofill.addresses.supportedCountries", "US,CA,FR,DE");
pref("extensions.formautofill.creditCards.supported", "detect");
pref("extensions.formautofill.creditCards.enabled", true);
pref("extensions.formautofill.creditCards.ignoreAutocompleteOff", true);

// Supported countries need to follow ISO 3166-1 to align with "browser.search.region"
pref("extensions.formautofill.creditCards.supportedCountries", "US,CA,GB,FR,DE,IT,ES,AT,BE,PL");

// Algorithm used by formautofill while determine whether a field is a credit card field
// 0:Heurstics based on regular expression string matching
// 1:Fathom in js implementation
// 2:Fathom in c++ implementation
pref("extensions.formautofill.creditCards.heuristics.mode", 2);
pref("extensions.formautofill.creditCards.heuristics.fathom.types", "cc-number,cc-name");
// Defines the threshold to identify whether a field is a cc field
pref("extensions.formautofill.creditCards.heuristics.fathom.confidenceThreshold", "0.5");
// Defineis the threshold to mark fields that are "high-confidence", see `isValidSection` for details
pref("extensions.formautofill.creditCards.heuristics.fathom.highConfidenceThreshold", "0.95");
// This is Only for testing! Set the confidence value (> 0 && <= 1) after a field is identified by fathom
pref("extensions.formautofill.creditCards.heuristics.fathom.testConfidence", "0");

pref("extensions.formautofill.loglevel", "Warn");

// Temporary prefs that we will be removed if the telemetry data (added in Fx123) does not show any problems with the new heuristics.
pref("extensions.formautofill.heuristics.captureOnFormRemoval", true);
pref("extensions.formautofill.heuristics.captureOnPageNavigation", true);

pref("extensions.formautofill.heuristics.detectDynamicFormChanges", true);
pref("extensions.formautofill.heuristics.fillOnDynamicFormChanges", true);
// Note: The greater the timeout value the higher the risk of automatically filling fields after a non-script/user action.
pref("extensions.formautofill.heuristics.fillOnDynamicFormChanges.timeout", 1000);
pref("extensions.formautofill.heuristics.refillOnSiteClearingFields", true);
pref("extensions.formautofill.heuristics.refillOnSiteClearingFields.timeout", 500);

pref("extensions.formautofill.heuristics.autofillSameOriginWithTop", true);

#ifdef NIGHTLY_BUILD
  pref("extensions.formautofill.ml.experiment.enabled", true);
  pref("extensions.formautofill.ml.experiment.modelRevision", "v0.1.3");
#endif

pref("toolkit.osKeyStore.loglevel", "Warn");

pref("extensions.formautofill.supportRTL", false);

// Controls the log level for CookieBannerListService.sys.mjs.
pref("cookiebanners.listService.logLevel", "Error");

// Controls the log level for Cookie Banner Auto Clicking.
pref("cookiebanners.bannerClicking.logLevel", "Error");

// Enables the cookie banner auto clicking. The cookie banner auto clicking
// depends on the `cookiebanners.service.mode` pref.
pref("cookiebanners.bannerClicking.enabled", true);

// Whether or not banner auto clicking test mode is enabled.
pref("cookiebanners.bannerClicking.testing", false);

// The maximum time (ms) after load for detecting banner and button elements for
// cookie banner auto clicking.
pref("cookiebanners.bannerClicking.timeoutAfterLoad", 5000);

// Maximum time (ms) after DOMContentLoaded for detecting banners. This is a
// catchall for cases where a load even never occurs.
pref("cookiebanners.bannerClicking.timeoutAfterDOMContentLoaded", 20000);

// How often (milliseconds) to run the banner detection query selectors to detect
// the banner element and/or buttons.
pref("cookiebanners.bannerClicking.pollingInterval", 500);

// Array of test rules for cookie banner handling as a JSON string. They will be
// inserted in addition to regular rules and may override them when setting the
// same domain. Every array item should be a valid CookieBannerRule. See
// CookieBannerRule.schema.json.
pref("cookiebanners.listService.testRules", "[]");

// Still fetches rules from RemoteSettings, but discards them. Used in tests.
pref("cookiebanners.listService.testSkipRemoteSettings", false);

// The domains we will block from installing SitePermsAddons. Comma-separated
// full domains: any subdomains of the domains listed will also be allowed.
pref("dom.sitepermsaddon-provider.separatedBlocklistedDomains", "shopee.co.th,shopee.tw,shopee.co.id,shopee.com.my,shopee.vn,shopee.ph,shopee.sg,shopee.com.br,shopee.com,shopee.cn,shopee.io,shopee.pl,shopee.com.mx,shopee.com.co,shopee.cl,shopee.kr,shopee.es,shopee.in,alipay.com,miravia.es");

// Log level for logger in URLQueryStrippingListService
pref("privacy.query_stripping.listService.logLevel", "Error");

// Signal to the webcompat site intervention add-on to use the MV3
// scripting.registerContentScripts API instead of the older MV2
// contentScripts.register API.
pref("extensions.webcompat.useScriptingAPI", true);

// Controls the log level for Fingerprinting Remote Overrides.
pref("privacy.fingerprintingProtection.WebCompatService.logLevel", "Error");
// To test strip on share site specific parameters by enabling a different list to be used
pref("privacy.query_stripping.strip_on_share.enableTestMode", false);

#if defined(MOZ_BACKGROUNDTASKS) && defined(ENABLE_TESTS)
  // Test prefs to verify background tasks inheret and override gecko prefs
  // correctly.
  pref("toolkit.backgroundtasks.tests.geckoPrefsInherited", 17);
  pref("toolkit.backgroundtasks.tests.geckoPrefsOverriden", 18);
#endif

// To disable the Strip on Share context menu option if nothing can be stripped
pref("privacy.query_stripping.strip_on_share.canDisable", true);

// Captcha Detection
pref("captchadetection.loglevel", "Warn");
pref("captchadetection.actor.enabled", true);
