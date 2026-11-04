/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// kinda abusing features here, but features "override" builtin support.
pub use tracing_support::{debug, error, info, trace, warn, Level};

#[cfg(feature = "testing")]
pub use tracing_support::{init_for_tests, init_for_tests_with_level};

mod macros;

#[cfg(feature = "backtrace")]
/// Re-export of the `backtrace` crate for use in macros and
/// to ensure the needed version is kept in sync in dependents.
pub use backtrace;

pub fn initialize() {
    // Reference `rust_component_errors` to ensure it's loaded/initialized
    #[cfg(feature = "glean-sym")]
    let _ = &*glean_metrics::rust_component_errors;
}

#[cfg(not(feature = "backtrace"))]
/// A compatibility shim for `backtrace`.
pub mod backtrace {
    use std::fmt;

    pub struct Backtrace;

    impl Backtrace {
        pub fn new() -> Self {
            Backtrace
        }
    }

    impl Default for Backtrace {
        fn default() -> Self {
            Self::new()
        }
    }

    impl fmt::Debug for Backtrace {
        #[cold]
        fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
            write!(f, "Not available")
        }
    }
}

mod redact;
pub use redact::*;

mod error_tracing;
pub use error_tracing::{report_breadcrumb, report_error_to_app};

pub use error_support_macros::handle_error;

mod handling;
pub use handling::{convert_log_report_error, ErrorHandling, ErrorReporting, GetErrorHandling};

/// XXX - Most of this is now considered deprecated - only FxA uses it, and
/// should be replaced with the facilities in the `handling` module.
///
/// Define a wrapper around the the provided ErrorKind type.
/// See also `define_error` which is more likely to be what you want.
#[macro_export]
macro_rules! define_error_wrapper {
    ($Kind:ty) => {
        pub type Result<T, E = Error> = std::result::Result<T, E>;
        struct ErrorData {
            kind: $Kind,
            backtrace: Option<std::sync::Mutex<$crate::backtrace::Backtrace>>,
        }

        impl ErrorData {
            #[cold]
            fn new(kind: $Kind) -> Self {
                ErrorData {
                    kind,
                    #[cfg(feature = "backtrace")]
                    backtrace: Some(std::sync::Mutex::new(
                        $crate::backtrace::Backtrace::new_unresolved(),
                    )),
                    #[cfg(not(feature = "backtrace"))]
                    backtrace: None,
                }
            }

            #[cfg(feature = "backtrace")]
            #[cold]
            fn get_backtrace(&self) -> Option<&std::sync::Mutex<$crate::backtrace::Backtrace>> {
                self.backtrace.as_ref().map(|mutex| {
                    mutex.lock().unwrap().resolve();
                    mutex
                })
            }

            #[cfg(not(feature = "backtrace"))]
            #[cold]
            fn get_backtrace(&self) -> Option<&std::sync::Mutex<$crate::backtrace::Backtrace>> {
                None
            }
        }

        impl std::fmt::Debug for ErrorData {
            #[cfg(feature = "backtrace")]
            #[cold]
            fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
                let mut bt = self.backtrace.as_ref().unwrap().lock().unwrap();
                bt.resolve();
                write!(f, "{:?}\n\n{}", bt, self.kind)
            }

            #[cfg(not(feature = "backtrace"))]
            #[cold]
            fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
                write!(f, "{}", self.kind)
            }
        }

        #[derive(Debug, thiserror::Error)]
        pub struct Error(Box<ErrorData>);
        impl Error {
            #[cold]
            pub fn kind(&self) -> &$Kind {
                &self.0.kind
            }

            #[cold]
            pub fn backtrace(&self) -> Option<&std::sync::Mutex<$crate::backtrace::Backtrace>> {
                self.0.get_backtrace()
            }
        }

        impl std::fmt::Display for Error {
            #[cold]
            fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
                std::fmt::Display::fmt(self.kind(), f)
            }
        }

        impl From<$Kind> for Error {
            // Cold to optimize in favor of non-error cases.
            #[cold]
            fn from(ctx: $Kind) -> Error {
                Error(Box::new(ErrorData::new(ctx)))
            }
        }
    };
}

/// Define a set of conversions from external error types into the provided
/// error kind. Use `define_error` to do this at the same time as
/// `define_error_wrapper`.
#[macro_export]
macro_rules! define_error_conversions {
    ($Kind:ident { $(($variant:ident, $type:ty)),* $(,)? }) => ($(
        impl From<$type> for Error {
            // Cold to optimize in favor of non-error cases.
            #[cold]
            fn from(e: $type) -> Self {
                Error::from($Kind::$variant(e))
            }
        }
    )*);
}

/// All the error boilerplate (okay, with a couple exceptions in some cases) in
/// one place.
#[macro_export]
macro_rules! define_error {
    ($Kind:ident { $(($variant:ident, $type:ty)),* $(,)? }) => {
        $crate::define_error_wrapper!($Kind);
        $crate::define_error_conversions! {
            $Kind {
                $(($variant, $type)),*
            }
        }
    };
}

#[cfg(feature = "glean-sym")]
pub mod glean_metrics {
    include!(concat!(env!("OUT_DIR"), "/glean_metrics.rs"));
}

uniffi::setup_scaffolding!("errorsupport");
