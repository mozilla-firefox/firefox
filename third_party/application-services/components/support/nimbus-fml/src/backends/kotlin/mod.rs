/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::command_line::commands::GenerateStructCmd;
use crate::error::{FMLError, Result};
use crate::frontend::AboutBlock;
use crate::intermediate_representation::FeatureManifest;
use askama::Template;

mod gen_structs;

impl AboutBlock {
    fn nimbus_fully_qualified_name(&self) -> String {
        let kt_about = self.kotlin_about.as_ref().unwrap();

        let class = &kt_about.class;
        if class.starts_with('.') {
            format!("{}{}", kt_about.package, class)
        } else {
            class.clone()
        }
    }

    fn nimbus_object_name_kt(&self) -> String {
        let fqe = self.nimbus_fully_qualified_name();
        let last = fqe.split('.').next_back().unwrap_or(&fqe);
        last.to_string()
    }

    fn nimbus_package_name(&self) -> Option<String> {
        let fqe = self.nimbus_fully_qualified_name();
        if !fqe.contains('.') {
            return None;
        }
        let mut it = fqe.split('.');
        it.next_back()?;
        Some(it.collect::<Vec<&str>>().join("."))
    }

    fn resource_package_name(&self) -> String {
        let kt_about = self.kotlin_about.as_ref().unwrap();
        kt_about.package.clone()
    }
}

pub(crate) fn generate_struct(manifest: &FeatureManifest, cmd: &GenerateStructCmd) -> Result<()> {
    if manifest.about.kotlin_about.is_none() {
        return Err(FMLError::ValidationError(
            "about".to_string(),
            format!(
                "The `about` block is missing a valid `android` or `kotlin` entry: {}",
                &cmd.manifest
            ),
        ));
    }

    let path = &cmd.output;
    let path = if path.is_dir() {
        path.join(format!("{}.kt", manifest.about.nimbus_object_name_kt()))
    } else {
        path.clone()
    };

    let kt = gen_structs::FeatureManifestDeclaration::new(manifest);

    let contents = kt.render()?;

    std::fs::write(path, contents)?;

    Ok(())
}

#[cfg(all(
    test,
    feature = "kotlin-tests",
    not(feature = "all-features-workaround")
))]
pub mod test {
    use crate::util::{join, pkg_dir, sdk_dir};
    use anyhow::{bail, Result};
    use std::path::Path;
    use std::process::Command;

    // The root of the Android kotlin package structure
    fn sdk_android_dir() -> String {
        join(sdk_dir(), "android/src/main/java")
    }

    // The directory with the mock implementations of Android
    // used for testing.
    fn runtime_dir() -> String {
        join(pkg_dir(), "fixtures/android/runtime")
    }

    // We'll put our test scripts in here.
    fn tests_dir() -> String {
        join(pkg_dir(), "fixtures/android/tests")
    }

    // The jar archive we need to do JSON with in Kotlin/Java.
    // This is the same library as bundled in Android.
    fn json_jar() -> String {
        join(runtime_dir(), "json.jar")
    }

    // The file with the kt implementation of FeatureVariables
    fn variables_kt() -> String {
        join(
            sdk_android_dir(),
            "org/mozilla/experiments/nimbus/FeatureVariables.kt",
        )
    }

    fn nimbus_internals_kt() -> String {
        join(sdk_android_dir(), "org/mozilla/experiments/nimbus/internal")
    }

    // The file with the kt implementation of FeatureVariables
    fn features_kt() -> String {
        join(
            sdk_android_dir(),
            "org/mozilla/experiments/nimbus/FeaturesInterface.kt",
        )
    }

    fn hardcoded_features_kt() -> String {
        join(
            sdk_android_dir(),
            "org/mozilla/experiments/nimbus/HardcodedNimbusFeatures.kt",
        )
    }

    fn classpath(classes: &Path) -> Result<String> {
        Ok(format!("{}:{}", json_jar(), classes.to_str().unwrap()))
    }

    fn detect_kotlinc() -> Result<bool> {
        let output = Command::new("which").arg("kotlinc").output()?;

        Ok(output.status.success())
    }

    // Compile a genertaed manifest file against the mocked out Android runtime.
    pub fn compile_manifest_kt(manifest_paths: &[String]) -> Result<tempfile::TempDir> {
        let temp = tempfile::tempdir()?;
        let build_dir = temp.path();

        let status = Command::new("kotlinc")
            // Our generated bindings should not produce any warnings; fail tests if they do.
            .arg("-Werror")
            .arg("-J-ea")
            // Reflect $CLASSPATH from the environment, to help find `json.jar`.
            .arg("-classpath")
            .arg(json_jar())
            .arg("-d")
            .arg(build_dir)
            .arg(variables_kt())
            .arg(features_kt())
            .arg(hardcoded_features_kt())
            .arg(runtime_dir())
            .arg(nimbus_internals_kt())
            .args(manifest_paths)
            .spawn()?
            .wait()?;
        if status.success() {
            Ok(temp)
        } else {
            bail!("running `kotlinc` failed compiling a generated manifest")
        }
    }

    // Given a generated manifest, run a kts script against it.
    pub fn run_script_with_generated_code(manifests_kt: &[String], script: &str) -> Result<()> {
        assert!(
            detect_kotlinc()?,
            "Bug 2026340 -- Cannot run kotlin tests without kotlinc"
        );

        let temp_dir = compile_manifest_kt(manifests_kt)?;
        let build_dir = temp_dir.path();

        let status = Command::new("kotlinc")
            // Our generated bindings should not produce any warnings; fail tests if they do.
            .arg("-Werror")
            .arg("-J-ea")
            // Reflect $CLASSPATH from the environment, to help find `json.jar`.
            .arg("-classpath")
            .arg(&classpath(build_dir)?)
            .arg("-script")
            .arg(script)
            .spawn()?
            .wait()?;

        drop(temp_dir);
        if status.success() {
            Ok(())
        } else {
            bail!("running `kotlinc` failed running a script")
        }
    }

    #[test]
    fn smoke_test_runtime_dir() -> Result<()> {
        run_script_with_generated_code(
            &[join(tests_dir(), "SmokeTestFeature.kt")],
            "fixtures/android/tests/smoke_test.kts",
        )?;
        Ok(())
    }
}
