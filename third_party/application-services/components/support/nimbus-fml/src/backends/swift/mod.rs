/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::error::{FMLError, Result};
use crate::frontend::AboutBlock;
use askama::Template;

use crate::command_line::commands::GenerateStructCmd;
use crate::intermediate_representation::FeatureManifest;

mod gen_structs;

impl AboutBlock {
    fn nimbus_object_name_swift(&self) -> String {
        let swift_about = self.swift_about.as_ref().unwrap();
        swift_about.class.clone()
    }

    fn nimbus_module_name(&self) -> String {
        let swift_about = self.swift_about.as_ref().unwrap();
        swift_about.module.clone()
    }
}

pub(crate) fn generate_struct(manifest: &FeatureManifest, cmd: &GenerateStructCmd) -> Result<()> {
    if manifest.about.swift_about.is_none() {
        return Err(FMLError::ValidationError(
            "about".to_string(),
            format!(
                "The `about` block is missing a valid `ios` or `swift` entry: {}",
                &cmd.manifest
            ),
        ));
    }

    let path = &cmd.output;
    let path = if path.is_dir() {
        path.join(format!(
            "{}.swift",
            manifest.about.nimbus_object_name_swift()
        ))
    } else {
        path.clone()
    };

    let fm = gen_structs::FeatureManifestDeclaration::new(manifest);

    let contents = fm.render()?;

    std::fs::write(path, contents)?;

    Ok(())
}

#[cfg(all(
    test,
    feature = "swift-tests",
    not(feature = "all-features-workaround")
))]
pub mod test {
    use crate::util::{join, pkg_dir, sdk_dir};
    use anyhow::{bail, Context, Result};
    use std::{
        ffi::OsString,
        path::{Path, PathBuf},
        process::Command,
    };

    // The root of the Android kotlin package structure
    fn sdk_ios_dir() -> String {
        join(sdk_dir(), "ios/Nimbus")
    }

    fn mock_nimbus_error_swift() -> String {
        join(pkg_dir(), "fixtures/ios/runtime/NimbusError.swift")
    }

    fn mock_uiimage_swift() -> String {
        join(pkg_dir(), "fixtures/ios/runtime/UIImage.swift")
    }

    // The file with the swift implementation of FeatureVariables
    fn variables_swift() -> String {
        join(sdk_ios_dir(), "FeatureVariables.swift")
    }

    // The file with the swift implementation of FeatureVariables
    fn features_swift() -> String {
        join(sdk_ios_dir(), "FeatureInterface.swift")
    }

    // The file with the swift implementation of FeatureVariables
    fn collections_swift() -> String {
        join(sdk_ios_dir(), "Collections+.swift")
    }

    // The file with the swift implementation of FeatureVariables
    fn dictionaries_swift() -> String {
        join(sdk_ios_dir(), "Dictionary+.swift")
    }

    // The file with the swift implementation of Bundle extensions
    fn bundle_swift() -> String {
        join(sdk_ios_dir(), "Bundle+.swift")
    }

    // The file with the swift implementation of FeatureHolder
    fn feature_holder() -> String {
        join(sdk_ios_dir(), "FeatureHolder.swift")
    }

    fn hardcoded_nimbus_features() -> String {
        join(sdk_ios_dir(), "HardcodedNimbusFeatures.swift")
    }

    // The file with the swift implementation of Feature Manifest protocol file
    fn generated_feature_manifest() -> String {
        join(sdk_ios_dir(), "FeatureManifestInterface.swift")
    }

    fn detect_swiftc() -> Result<bool> {
        let output = Command::new("which").arg("swiftc").output()?;

        Ok(output.status.success())
    }

    pub fn compile_manifest_swift(manifest_files: &[String], out_dir: &Path) -> Result<()> {
        let out_path = PathBuf::from(out_dir);
        let manifest_files = manifest_files.iter().map(PathBuf::from);
        let mut dylib_file = out_path.clone();
        dylib_file.push(format!("lib{}.dylib", "FeatureManifest"));

        // `-emit-library -o <path>` generates a `.dylib`, so that we can use the
        // Swift module from the REPL. Otherwise, we'll get "Couldn't lookup
        // symbols" when we try to import the module.
        // See https://bugs.swift.org/browse/SR-1191.

        let status = Command::new("swiftc")
            .arg("-module-name")
            .arg("FeatureManifest")
            .arg("-emit-library")
            .arg("-o")
            .arg(&dylib_file)
            .arg("-emit-module")
            .arg("-emit-module-path")
            .arg(&out_path)
            .arg("-parse-as-library")
            .arg("-L")
            .arg(&out_path)
            .arg(collections_swift())
            .arg(dictionaries_swift())
            .arg(mock_uiimage_swift())
            .arg(variables_swift())
            .arg(features_swift())
            .arg(feature_holder())
            .arg(hardcoded_nimbus_features())
            .arg(bundle_swift())
            .arg(generated_feature_manifest())
            .arg(mock_nimbus_error_swift())
            .args(manifest_files)
            .spawn()
            .context("Failed to spawn `swiftc` when compiling bindings")?
            .wait()
            .context("Failed to wait for `swiftc` when compiling bindings")?;
        if !status.success() {
            bail!("running `swiftc` failed")
        }
        Ok(())
    }

    pub fn run_script(out_dir: &Path, script_file: &Path) -> Result<()> {
        let mut cmd = Command::new("swift");

        // Find any module maps and/or dylibs in the target directory, and tell swift to use them.
        // Listing the directory like this is a little bit hacky - it would be nicer if we could tell
        // Swift to load only the module(s) for the component under test, but the way we're calling
        // this test function doesn't allow us to pass that name in to the call.

        cmd.arg("-I").arg(out_dir).arg("-L").arg(out_dir);
        for entry in PathBuf::from(out_dir)
            .read_dir()
            .context("Failed to list target directory when running script")?
        {
            let entry = entry.context("Failed to list target directory when running script")?;
            if let Some(ext) = entry.path().extension() {
                if ext == "dylib" || ext == "so" {
                    let mut option = OsString::from("-l");
                    option.push(entry.path());
                    cmd.arg(option);
                }
            }
        }
        cmd.arg(script_file);

        let status = cmd
            .spawn()
            .context("Failed to spawn `swift` when running script")?
            .wait()
            .context("Failed to wait for `swift` when running script")?;
        if !status.success() {
            bail!("running `swift` failed")
        }
        Ok(())
    }

    pub fn run_script_with_generated_code(manifest_files: &[String], script: &Path) -> Result<()> {
        assert!(
            detect_swiftc()?,
            "Bug 2026345 -- Cannot run swift tests without swiftc"
        );

        let temp = tempfile::tempdir()?;
        let build_dir = temp.path();
        compile_manifest_swift(manifest_files, build_dir)?;
        run_script(build_dir, script)
    }
}
