/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/// Tell the application to report an error
///
/// If configured by the application, this sent to the application, which should report it to a
/// Sentry-like system. This should only be used for errors that we don't expect to see and will
/// work on fixing if we see a non-trivial volume of them.
///
/// type_name identifies the error.  It should be the main text that gets shown to the
/// user and also how the error system groups errors together.  It should be in UpperCamelCase
/// form.
///
/// Good type_names require some trial and error, for example:
///   - Start with the error kind variant name
///   - Add more text to distinguish errors more.  For example an error code, or an extra word
///     based on inspecting the error details
#[macro_export]
macro_rules! report_error {
    ($type_name:expr, $($arg:tt)*) => {
        let message = std::format!($($arg)*);
        $crate::warn!("report {}: {}", $type_name, message);
        $crate::report_error($type_name.to_string(), message.to_string());
    };
}

/// Log a breadcrumb if we see an `Result::Err` value
///
/// Use this macro to wrap a function call that returns a `Result<>`.  If that call returns an
/// error, then we will log a breadcrumb for it.  This can be used to track down the codepath where
/// an error happened.
#[macro_export]
macro_rules! trace_error {
    ($result:expr) => {{
        let result = $result;
        if let Err(e) = &result {
            $crate::breadcrumb!("Saw error: {}", e);
        };
        result
    }};
}

/// Tell the application to log a breadcrumb
///
/// Breadcrumbs are log-like entries that get tracked by the error reporting system.  When we
/// report an error, recent breadcrumbs will be associated with it.
#[macro_export]
macro_rules! breadcrumb {
    ($($arg:tt)*) => {
        {
            let message = std::format!($($arg)*);
            $crate::info!("breadcrumb: {}", message);
            $crate::report_breadcrumb(
                message,
                std::module_path!().to_string(),
                std::line!(),
                std::column!(),
            );
        }
    };
}
