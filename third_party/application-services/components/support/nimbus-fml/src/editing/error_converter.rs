/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use super::{values_finder::ValuesFinder, ErrorKind, FeatureValidationError};
#[cfg(feature = "client-lib")]
use super::{CorrectionCandidate, FmlEditorError};
use crate::{
    error::FMLError,
    intermediate_representation::{EnumDef, FeatureDef, ObjectDef},
};
use serde_json::Value;
use std::collections::{BTreeMap, BTreeSet};

#[allow(dead_code)]
pub(crate) struct ErrorConverter<'a> {
    pub(crate) enum_defs: &'a BTreeMap<String, EnumDef>,
    pub(crate) object_defs: &'a BTreeMap<String, ObjectDef>,
}

impl<'a> ErrorConverter<'a> {
    pub(crate) fn new(
        enum_defs: &'a BTreeMap<String, EnumDef>,
        object_defs: &'a BTreeMap<String, ObjectDef>,
    ) -> Self {
        Self {
            enum_defs,
            object_defs,
        }
    }

    pub(crate) fn convert_feature_error(
        &self,
        feature_def: &FeatureDef,
        feature_value: &Value,
        error: FeatureValidationError,
    ) -> FMLError {
        let values = ValuesFinder::new(self.enum_defs, feature_def, feature_value);
        let long_message = self.long_message(&values, &error);
        FMLError::ValidationError(error.path.path, long_message)
    }

    #[allow(dead_code)]
    #[cfg(feature = "client-lib")]
    pub(crate) fn convert_into_editor_errors(
        &self,
        feature_def: &FeatureDef,
        feature_value: &Value,
        src: &str,
        errors: &Vec<FeatureValidationError>,
    ) -> Vec<FmlEditorError> {
        let mut editor_errors: Vec<_> = Default::default();
        let values = ValuesFinder::new(self.enum_defs, feature_def, feature_value);
        for error in errors {
            // While experimenter is not known to be using the corrections, we should continue to use
            // the long message which includes the did_you_mean and corrections.
            let message = self.long_message(&values, error);
            // After experimenter is using the corrections, we can switch to
            // let message = self.message(error);

            let highlight = error.path.first_error_token().map(String::from);
            // TODO: derive the highlighted token from the error span.
            let error_span = error.path.error_span(src);

            let corrections = self.correction_candidates(&values, src, error);

            let error = FmlEditorError {
                message,

                highlight,
                corrections,

                // deprecated, can be removed once it's removed in experimenter.
                line: error_span.from.line,
                col: error_span.from.col,

                error_span,
            };
            editor_errors.push(error);
        }
        editor_errors
    }

    pub(crate) fn convert_object_error(&self, error: FeatureValidationError) -> FMLError {
        FMLError::ValidationError(error.path.path.to_owned(), self.message(&error))
    }
}

impl ErrorConverter<'_> {
    fn long_message(&self, values: &ValuesFinder, error: &FeatureValidationError) -> String {
        let message = self.message(error);
        let mut suggestions = self.string_replacements(error, values);
        let dym = did_you_mean(&mut suggestions);
        format!("{message}{dym}")
    }

    fn message(&self, error: &FeatureValidationError) -> String {
        let token = error.path.error_token_abbr();
        error.kind.message(&token)
    }

    #[allow(dead_code)]
    #[cfg(feature = "client-lib")]
    fn correction_candidates(
        &self,
        values: &ValuesFinder,
        _src: &str,
        error: &FeatureValidationError,
    ) -> Vec<CorrectionCandidate> {
        let strings = self.string_replacements(error, values);
        let placeholders = self.placeholder_replacements(error, values);

        let mut candidates = Vec::with_capacity(strings.len() + placeholders.len());
        for s in &strings {
            candidates.push(CorrectionCandidate::string_replacement(s));
        }
        for s in &placeholders {
            candidates.push(CorrectionCandidate::literal_replacement(s));
        }
        candidates
    }
}

/// The following methods are for unpacking errors coming out of the DefaultsValidator, to be used
/// for correction candidates (like Quick Fix in VSCode) and autocomplete.
impl ErrorConverter<'_> {
    #[allow(dead_code)]
    #[cfg(feature = "client-lib")]
    fn placeholder_replacements(
        &self,
        error: &FeatureValidationError,
        values: &ValuesFinder,
    ) -> BTreeSet<String> {
        match &error.kind {
            ErrorKind::InvalidValue { value_type: t, .. }
            | ErrorKind::TypeMismatch { value_type: t }
            | ErrorKind::InvalidNestedValue { prop_type: t, .. } => values.all_placeholders(t),
            _ => Default::default(),
        }
    }

    fn string_replacements(
        &self,
        error: &FeatureValidationError,
        values: &ValuesFinder,
    ) -> BTreeSet<String> {
        let complete = match &error.kind {
            ErrorKind::InvalidKey { key_type: t, .. }
            | ErrorKind::InvalidValue { value_type: t, .. }
            | ErrorKind::TypeMismatch { value_type: t } => values.all_specific_strings(t),
            // For property keys that we don't want to suggest to the user, but we _do_ want them involved in
            // validation or code generation, we make them never/difficult to be overridden by an experiment,
            // by filtering them here.
            ErrorKind::InvalidPropKey { valid, .. } => valid
                .iter()
                .filter(|s| s.starts_with(char::is_alphanumeric))
                .map(ToOwned::to_owned)
                .collect(),
            ErrorKind::InvalidNestedValue { .. } => Default::default(),
        };

        // We don't want to suggest any tokens that the user has already used correctly, so
        // we can filter out the ones in use.
        match &error.kind {
            ErrorKind::InvalidKey { in_use, .. } | ErrorKind::InvalidPropKey { in_use, .. }
                // This last check is an optimization:
                // if none of the in_use are valid,
                // then we can skip cloning.
                if !complete.is_disjoint(in_use) =>
            {
                complete.difference(in_use).cloned().collect()
            }
            _ => complete,
        }
    }
}

fn did_you_mean(words: &mut BTreeSet<String>) -> String {
    let mut words = words.iter();
    match words.len() {
        0 => String::from(""),
        1 => format!("; did you mean \"{}\"?", words.next().unwrap()),
        2 => format!(
            "; did you mean \"{}\" or \"{}\"?",
            words.next().unwrap(),
            words.next().unwrap(),
        ),
        _ => {
            let last = words.next_back().unwrap();
            format!(
                "; did you mean one of \"{}\" or \"{last}\"?",
                itertools::join(words, "\", \"")
            )
        }
    }
}
