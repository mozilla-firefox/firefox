/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use glob::MatchOptions;
use std::collections::HashSet;

use super::commands::{
    GenerateExperimenterManifestCmd, GenerateSingleFileManifestCmd, GenerateStructCmd, LintCmd,
    PrintChannelsCmd, PrintInfoCmd, ValidateCmd,
};
use crate::backends::info::ManifestInfo;
use crate::error::FMLError::CliError;
use crate::frontend::ManifestFrontEnd;
use crate::lints::{self, Finding, LintConfig, LintLevel, LintReport};
use crate::{
    backends,
    error::{FMLError, Result},
    intermediate_representation::{FeatureManifest, TargetLanguage},
    parser::Parser,
    util::loaders::{FileLoader, FilePath, LoaderConfig},
};
use std::io::{IsTerminal, Write};
use std::path::Path;
use termcolor::{Color, ColorChoice, ColorSpec, StandardStream, WriteColor};

/// Use this when recursively looking for files.
const MATCHING_FML_EXTENSION: &str = ".fml.yaml";

/// `ColorChoice::Auto` only looks at `TERM`, not at whether stdout is a terminal, so
/// on its own it writes escape codes into redirected output and CI logs.
fn stdout_stream() -> StandardStream {
    let choice = if std::io::stdout().is_terminal() {
        ColorChoice::Auto
    } else {
        ColorChoice::Never
    };
    StandardStream::stdout(choice)
}

pub(crate) fn generate_struct(cmd: &GenerateStructCmd) -> Result<()> {
    let files: FileLoader = TryFrom::try_from(&cmd.loader)?;

    let filename = &cmd.manifest;
    let input = files.file_path(filename)?;

    validate(&ValidateCmd {
        manifest: cmd.manifest.clone(),
        loader: cmd.loader.clone(),
    })?;

    match (&input, &cmd.output.is_dir()) {
        (FilePath::Remote(_), _) => generate_struct_single(&files, input, cmd),
        (FilePath::Local(file), _) if !file.exists() => Err(FMLError::CliError(format!(
            "Input file or directory `{}' does not exist",
            filename
        ))),
        (FilePath::Local(file), _) if file.is_file() => generate_struct_single(&files, input, cmd),
        (FilePath::Local(dir), true) if dir.is_dir() => generate_struct_from_dir(&files, cmd, dir),
        (_, true) => generate_struct_from_glob(&files, cmd, filename),
        _ => Err(FMLError::CliError(
            "Cannot generate a single output file from an input directory".to_string(),
        )),
    }
}

fn generate_struct_from_dir(files: &FileLoader, cmd: &GenerateStructCmd, cwd: &Path) -> Result<()> {
    let entries = cwd.read_dir()?;
    for entry in entries.filter_map(Result::ok) {
        let pb = entry.path();
        if pb.is_dir() {
            generate_struct_from_dir(files, cmd, &pb)?;
        } else if let Some(nm) = pb.file_name().map(|s| s.to_str().unwrap_or_default()) {
            if nm.ends_with(MATCHING_FML_EXTENSION) {
                let path = pb.as_path().into();
                generate_struct_single(files, path, cmd)?;
            }
        }
    }
    Ok(())
}

fn generate_struct_from_glob(
    files: &FileLoader,
    cmd: &GenerateStructCmd,
    pattern: &str,
) -> Result<()> {
    use glob::glob_with;
    let entries = glob_with(pattern, MatchOptions::new()).unwrap();
    for entry in entries.filter_map(Result::ok) {
        let path = entry.as_path().into();
        generate_struct_single(files, path, cmd)?;
    }
    Ok(())
}

fn generate_struct_single(
    files: &FileLoader,
    manifest_path: FilePath,
    cmd: &GenerateStructCmd,
) -> Result<()> {
    let ir = load_feature_manifest(
        files.clone(),
        manifest_path,
        cmd.load_from_ir,
        Some(&cmd.channel),
        false,
    )?;
    generate_struct_from_ir(&ir, cmd)
}

fn generate_struct_from_ir(ir: &FeatureManifest, cmd: &GenerateStructCmd) -> Result<()> {
    let language = &cmd.language;
    ir.validate_manifest_for_lang(language)?;
    match language {
        TargetLanguage::IR => {
            let contents = serde_json::to_string_pretty(&ir)?;
            std::fs::write(&cmd.output, contents)?;
        }
        TargetLanguage::Kotlin => backends::kotlin::generate_struct(ir, cmd)?,
        TargetLanguage::Swift => backends::swift::generate_struct(ir, cmd)?,
        _ => unimplemented!(
            "Unsupported output language for structs: {}",
            language.extension()
        ),
    };
    Ok(())
}

pub(crate) fn generate_experimenter_manifest(cmd: &GenerateExperimenterManifestCmd) -> Result<()> {
    let files: FileLoader = TryFrom::try_from(&cmd.loader)?;
    let path = files.file_path(&cmd.manifest)?;
    let ir = load_feature_manifest(
        files,
        path,
        cmd.load_from_ir,
        None,
        cmd.loader.lax_gecko_pref_validation,
    )?;
    backends::experimenter_manifest::generate_manifest(ir, cmd)?;
    Ok(())
}

pub(crate) fn generate_single_file_manifest(cmd: &GenerateSingleFileManifestCmd) -> Result<()> {
    let files: FileLoader = TryFrom::try_from(&cmd.loader)?;
    let path = files.file_path(&cmd.manifest)?;
    let fm = load_feature_manifest(
        files,
        path,
        false,
        Some(&cmd.channel),
        cmd.loader.lax_gecko_pref_validation,
    )?;
    let frontend: ManifestFrontEnd = fm.into();
    std::fs::write(&cmd.output, serde_yaml::to_string(&frontend)?)?;
    Ok(())
}

fn load_feature_manifest(
    files: FileLoader,
    path: FilePath,
    load_from_ir: bool,
    channel: Option<&str>,
    lax_gecko_pref_validation: bool,
) -> Result<FeatureManifest> {
    let ir = if !load_from_ir {
        let parser: Parser = Parser::new(files, path)?;
        parser.get_intermediate_representation(channel)?
    } else {
        files.read_ir::<FeatureManifest>(&path)?
    };
    ir.validate_manifest_with(lax_gecko_pref_validation)?;
    Ok(ir)
}

pub(crate) fn fetch_file(files: &LoaderConfig, nm: &str) -> Result<()> {
    let files: FileLoader = files.try_into()?;
    let file = files.file_path(nm)?;

    let string = files.read_to_string(&file)?;

    println!("{}", string);
    Ok(())
}

fn output_ok(stream: &mut impl WriteColor, title: &str) -> Result<()> {
    write!(stream, "✅ ")?;
    stream.set_color(ColorSpec::new().set_fg(Some(Color::Green)))?;
    writeln!(stream, "{title}")?;
    stream.reset()?;

    Ok(())
}

fn output_note(stream: &mut impl WriteColor, title: &str) -> Result<()> {
    write!(stream, "ℹ️ ")?;
    stream.set_color(ColorSpec::new().set_fg(Some(Color::Yellow)))?;
    writeln!(stream, "{title}")?;
    stream.reset()?;

    Ok(())
}

/// The width lint help text wraps to.
const HELP_WIDTH: usize = 88;

/// The column the message starts in.
const LINT_NAME_WIDTH: usize = 26;

fn wrapped(text: &str, initial_indent: &str, subsequent_indent: &str) -> String {
    let options = textwrap::Options::new(HELP_WIDTH)
        .initial_indent(initial_indent)
        .subsequent_indent(subsequent_indent)
        // Lint prose is full of kebab-case names; don't break them at the hyphens.
        .word_splitter(textwrap::WordSplitter::NoHyphenation);
    textwrap::fill(text, options)
}

fn output_finding(stream: &mut impl WriteColor, finding: &Finding) -> Result<()> {
    let (icon, color) = match finding.level {
        LintLevel::Error => ("❎", Color::Red),
        _ => ("⚠️", Color::Yellow),
    };

    write!(stream, "  {icon} ")?;
    stream.set_color(ColorSpec::new().set_fg(Some(color)))?;
    write!(stream, "{:<LINT_NAME_WIDTH$}", finding.lint)?;
    stream.reset()?;
    writeln!(stream, "{}", finding.message)?;

    Ok(())
}

/// Print the findings grouped under the feature, object or enum they're about,
/// followed by the guidance for each lint that fired. The guidance is per lint, not
/// per finding: eighty features missing a `meta-bug` need telling how once.
fn output_findings(stream: &mut impl WriteColor, report: &LintReport) -> Result<()> {
    // The findings arrive sorted by module and subject, so a change of either starts
    // the next group.
    let mut group: Option<(&Option<String>, &String)> = None;

    for finding in &report.findings {
        let this = (&finding.module, &finding.subject);
        if group != Some(this) {
            if group.is_some() {
                writeln!(stream)?;
            }
            group = Some(this);

            stream.set_color(ColorSpec::new().set_bold(true))?;
            write!(stream, "{}", finding.subject)?;
            stream.reset()?;
            match &finding.module {
                Some(module) => writeln!(stream, " (imported from {module})")?,
                None => writeln!(stream)?,
            }
        }
        output_finding(stream, finding)?;
    }

    let lints = report.triggered_lints();
    if !lints.is_empty() {
        writeln!(stream, "\nWhat to do about these:")?;
        for lint in lints {
            stream.set_color(ColorSpec::new().set_bold(true))?;
            writeln!(stream, "  {}", lint.name)?;
            stream.reset()?;
            writeln!(stream, "{}", wrapped(lint.help, "    ", "    "))?;
        }
        writeln!(stream)?;
    }

    Ok(())
}

fn output_err(stream: &mut impl WriteColor, title: &str, detail: &str) -> Result<()> {
    writeln!(stream, "❎ ")?;
    stream.set_color(ColorSpec::new().set_fg(Some(Color::Red)))?;
    writeln!(stream, "{title}")?;
    stream.reset()?;
    writeln!(stream, ": {detail}")?;

    Ok(())
}

pub(crate) fn validate(cmd: &ValidateCmd) -> Result<()> {
    let mut stdout = stdout_stream();

    let files: FileLoader = TryFrom::try_from(&cmd.loader)?;

    let filename = &cmd.manifest;
    let file_path = files.file_path(filename)?;
    let parser: Parser = Parser::new(files, file_path.clone())?;
    let mut loading = HashSet::new();
    let manifest_front_end = parser.load_manifest(&file_path, &mut loading)?;

    let iter_includes = loading.iter().map(|id| id.to_string());

    let channels = manifest_front_end.channels();
    if channels.is_empty() {
        output_note(
            &mut stdout,
            &format!(
                "Loaded modules:\n- {}\n",
                iter_includes.collect::<Vec<String>>().join("\n- ")
            ),
        )?;
        output_ok(&mut stdout, &format!(
            "{}\n{}\n{}",
            "The manifest is valid for including in other files. To be imported, or used as an app manifest, it requires the following:",
            "- A `channels` list",
            "- An `about` block",
        ))?;
        return Ok(());
    }
    let intermediate_representation =
        parser
            .get_intermediate_representation(None)
            .inspect_err(|e| {
                output_err(&mut stdout, "Manifest is invalid", &e.to_string()).unwrap();
            })?;

    output_note(
        &mut stdout,
        &format!(
            "Loaded modules:\n- {}\n",
            iter_includes
                .chain(
                    intermediate_representation
                        .all_imports
                        .keys()
                        .map(|m| m.to_string())
                )
                .collect::<Vec<String>>()
                .join("\n- ")
        ),
    )?;

    writeln!(&mut stdout, "Validating manifest for different channels:")?;

    let results = channels
        .iter()
        .map(|c| {
            let intermediate_representation = parser.get_intermediate_representation(Some(c));
            match intermediate_representation {
                Ok(ir) => (
                    c,
                    ir.validate_manifest_with(cmd.loader.lax_gecko_pref_validation),
                ),
                Err(e) => (c, Err(e)),
            }
        })
        .collect::<Vec<(&String, Result<_>)>>();

    let mut error_count = 0;
    for (channel, result) in results {
        match result {
            Ok(_) => {
                output_ok(&mut stdout, &format!("{channel:.<20}valid"))?;
            }
            Err(e) => {
                error_count += 1;
                output_err(
                    &mut stdout,
                    &format!("{channel:.<20}invalid"),
                    &e.to_string(),
                )?;
            }
        };
    }

    if error_count > 0 {
        return Err(CliError(format!(
            "Manifest contains error(s) in {} channel{}",
            error_count,
            if error_count > 1 { "s" } else { "" }
        )));
    }

    Ok(())
}

fn lint_report(cmd: &LintCmd) -> Result<LintReport> {
    let files: FileLoader = TryFrom::try_from(&cmd.loader)?;
    let file_path = files.file_path(&cmd.manifest)?;

    // One parser for both passes: `load_manifest` walks the whole include tree, and
    // a second one would fetch and parse all of it again.
    let parser: Parser = Parser::new(files, file_path.clone())?;

    // The top level `no-lint` block lives in the file, not the IR.
    let mut loading = HashSet::new();
    let manifest_front_end = parser.load_manifest(&file_path, &mut loading)?;

    let config = LintConfig::new()
        .including_imports(cmd.include_imports)
        .with_file_suppressions(&manifest_front_end.no_lint)
        .allowing(&cmd.allow)?
        .denying(&cmd.deny)?;

    // Linting an invalid manifest would report nonsense.
    let ir = parser
        .get_intermediate_representation(None)
        .and_then(|ir| {
            ir.validate_manifest_with(cmd.loader.lax_gecko_pref_validation)
                .map(|_| ir)
        })
        .map_err(|e| {
            CliError(format!(
                "{e}\nA manifest has to be valid before it can be linted; run `nimbus-fml validate` for details"
            ))
        })?;

    Ok(lints::lint_manifest(&ir, &config))
}

pub(crate) fn lint(cmd: &LintCmd) -> Result<()> {
    let mut stdout = stdout_stream();

    let report = lint_report(cmd)?;

    if cmd.as_json {
        println!("{}", serde_json::to_string_pretty(&json_report(&report))?);
    } else {
        output_findings(&mut stdout, &report)?;
        output_lint_summary(&mut stdout, &report)?;
    }

    let errors = report.error_count();
    let warnings = report.warning_count();

    if errors > 0 {
        return Err(CliError(format!(
            "Manifest has {} lint error{}",
            errors,
            if errors > 1 { "s" } else { "" }
        )));
    }

    if cmd.error_on_warning && warnings > 0 {
        return Err(CliError(format!(
            "Manifest has {} lint warning{}",
            warnings,
            if warnings > 1 { "s" } else { "" }
        )));
    }

    Ok(())
}

/// The shape `--json` emits: the counts a CI job needs, plus the findings.
fn json_report(report: &LintReport) -> serde_json::Value {
    serde_json::json!({
        "errors": report.error_count(),
        "warnings": report.warning_count(),
        "suppressed": report.suppressed,
        "subjects": report.subject_count(),
        "findings": report.findings,
    })
}

fn output_suppressed(stream: &mut impl WriteColor, report: &LintReport) -> Result<()> {
    if report.suppressed == 0 {
        return Ok(());
    }
    output_note(
        stream,
        &format!(
            "{} finding{} silenced by `no-lint`",
            report.suppressed,
            if report.suppressed > 1 { "s" } else { "" }
        ),
    )
}

fn output_lint_summary(stream: &mut impl WriteColor, report: &LintReport) -> Result<()> {
    if report.is_empty() {
        output_ok(stream, "No lint findings")?;
        return output_suppressed(stream, report);
    }

    let errors = report.error_count();
    let warnings = report.warning_count();
    let mut counts = Vec::new();
    if errors > 0 {
        counts.push(format!(
            "{errors} error{}",
            if errors > 1 { "s" } else { "" }
        ));
    }
    if warnings > 0 {
        counts.push(format!(
            "{warnings} warning{}",
            if warnings > 1 { "s" } else { "" }
        ));
    }

    let subjects = report.subject_count();
    writeln!(
        stream,
        "Found {} in {subjects} place{}.",
        counts.join(" and "),
        if subjects > 1 { "s" } else { "" }
    )?;
    output_suppressed(stream, report)?;
    writeln!(
        stream,
        "{}",
        wrapped(
            "A lint that doesn't apply can be switched off for a single feature with a `no-lint: [LINT_NAME]` list on that feature, for the whole file with a top level `no-lint:` list, or for this run with `--allow LINT_NAME`.",
            "",
            "",
        )
    )?;

    Ok(())
}

pub(crate) fn list_lints() -> Result<()> {
    output_lint_list(&mut stdout_stream())
}

fn output_lint_list(stream: &mut impl WriteColor) -> Result<()> {
    const CATEGORY_WIDTH: usize = 15;
    const LEVEL_WIDTH: usize = 10;

    writeln!(
        stream,
        "{:<LINT_NAME_WIDTH$}{:<CATEGORY_WIDTH$}{:<LEVEL_WIDTH$}DESCRIPTION",
        "LINT", "CATEGORY", "DEFAULT"
    )?;
    for lint in lints::ALL_LINTS.iter() {
        writeln!(
            stream,
            "{:<LINT_NAME_WIDTH$}{:<CATEGORY_WIDTH$}{:<LEVEL_WIDTH$}{}",
            lint.name,
            lint.category.as_str(),
            lint.default_level.as_str(),
            lint.description
        )?;
    }

    Ok(())
}

pub(crate) fn print_channels(cmd: &PrintChannelsCmd) -> Result<()> {
    let files = TryFrom::try_from(&cmd.loader)?;
    let manifest = Parser::load_frontend(files, &cmd.manifest)?;
    let channels = manifest.channels();
    if cmd.as_json {
        let json = serde_json::Value::from(channels);
        println!("{}", json);
    } else {
        println!("{}", channels.join("\n"));
    }
    Ok(())
}

pub(crate) fn print_info(cmd: &PrintInfoCmd) -> Result<()> {
    let files: FileLoader = TryFrom::try_from(&cmd.loader)?;
    let path = files.file_path(&cmd.manifest)?;
    let fm = load_feature_manifest(files, path.clone(), false, cmd.channel.as_deref(), false)?;
    let info = if let Some(feature_id) = &cmd.feature {
        ManifestInfo::from_feature(&path, &fm, feature_id)?
    } else {
        ManifestInfo::from(&path, &fm)
    };
    if cmd.as_json {
        println!("{}", info.to_json()?);
    } else {
        println!("{}", info.to_yaml()?);
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use std::path::PathBuf;

    use anyhow::anyhow;
    use tempfile::NamedTempFile;

    use super::*;
    use crate::frontend::AboutBlock;
    use crate::util::{join, pkg_dir};

    pub(crate) const MANIFEST_PATHS: &[&str] = &[
        "fixtures/ir/simple_nimbus_validation.ir.json",
        "fixtures/ir/simple_nimbus_validation.ir.json",
        "fixtures/ir/with_objects.ir.json",
        "fixtures/ir/full_homescreen.ir.json",
        "fixtures/fe/importing/simple/app.yaml",
        "fixtures/fe/importing/diamond/00-app.yaml",
        "fixtures/fe/gecko-pref.yaml",
    ];

    #[allow(dead_code)]
    pub(crate) fn generate_and_assert(
        test_script: &str,
        manifest: &str,
        channel: &str,
        is_ir: bool,
    ) -> Result<()> {
        let output = NamedTempFile::new()?;
        let cmd = create_command_from_test(test_script, manifest, channel, is_ir, output.path())?;
        generate_struct(&cmd)?;
        run_script_with_generated_code(
            &cmd.language,
            &[cmd.output.as_path().display().to_string()],
            test_script,
        )?;
        Ok(())
    }

    fn generate_struct_cli_overrides(from_cli: AboutBlock, cmd: &GenerateStructCmd) -> Result<()> {
        let files: FileLoader = TryFrom::try_from(&cmd.loader)?;
        let path = files.file_path(&cmd.manifest)?;
        let mut ir =
            load_feature_manifest(files, path, cmd.load_from_ir, Some(&cmd.channel), false)?;

        // We do a dance here to make sure that we can override class names and package names during tests,
        // and while we still have to support setting those options from the command line.
        // We will deprecate setting classnames, package names etc, then we can simplify.
        let from_file = ir.about;
        let kotlin_about = from_cli.kotlin_about.or(from_file.kotlin_about);
        let swift_about = from_cli.swift_about.or(from_file.swift_about);
        let about = AboutBlock {
            kotlin_about,
            swift_about,
            ..Default::default()
        };
        ir.about = about;

        generate_struct_from_ir(&ir, cmd)
    }

    // Given a manifest.fml and script.kts in the tests directory generate
    // a manifest.kt and run the script against it.
    #[allow(dead_code)]
    pub(crate) fn generate_and_assert_with_config(
        test_script: &str,
        manifest: &str,
        channel: &str,
        is_ir: bool,
        config_about: AboutBlock,
    ) -> Result<()> {
        let output = NamedTempFile::new()?;
        let cmd = create_command_from_test(test_script, manifest, channel, is_ir, output.path())?;
        generate_struct_cli_overrides(config_about, &cmd)?;
        run_script_with_generated_code(
            &cmd.language,
            &[cmd.output.as_path().display().to_string()],
            test_script,
        )?;
        Ok(())
    }

    #[allow(dead_code)]
    pub(crate) fn create_command_from_test(
        test_script: &str,
        manifest: &str,
        channel: &str,
        is_ir: bool,
        output: &Path,
    ) -> Result<GenerateStructCmd, crate::error::FMLError> {
        let test_script = join(pkg_dir(), test_script);
        let pbuf = PathBuf::from(&test_script);
        let ext = pbuf
            .extension()
            .ok_or_else(|| anyhow!("Require a test_script with an extension: {}", test_script))?;
        let language: TargetLanguage = ext.try_into()?;
        let manifest_fml = join(pkg_dir(), manifest);
        let loader = Default::default();
        Ok(GenerateStructCmd {
            manifest: manifest_fml,
            output: output.into(),
            load_from_ir: is_ir,
            language,
            channel: channel.into(),
            loader,
        })
    }

    #[allow(dead_code)]
    pub(crate) fn generate_multiple_and_assert(
        test_script: &str,
        manifests: &[(&str, &str)],
    ) -> Result<()> {
        let cmds = manifests
            .iter()
            .map(|(manifest, channel)| {
                let output = NamedTempFile::new()?;
                let cmd =
                    create_command_from_test(test_script, manifest, channel, false, output.path())?;
                generate_struct(&cmd)?;
                Ok(cmd)
            })
            .collect::<Result<Vec<_>>>()?;

        let first = cmds
            .first()
            .expect("At least one manifests are always used");
        let language = &first.language;

        let manifests_out = cmds
            .iter()
            .map(|cmd| cmd.output.display().to_string())
            .collect::<Vec<_>>();

        run_script_with_generated_code(language, &manifests_out, test_script)?;
        Ok(())
    }

    fn run_script_with_generated_code(
        language: &TargetLanguage,
        #[allow(unused_variables)] manifests_out: &[String],
        #[allow(unused_variables)] test_script: &str,
    ) -> Result<()> {
        match language {
            #[cfg(all(feature = "kotlin-tests", not(feature = "all-features-workaround")))]
            TargetLanguage::Kotlin => {
                backends::kotlin::test::run_script_with_generated_code(manifests_out, test_script)?
            }

            #[cfg(all(feature = "swift-tests", not(feature = "all-features-workaround")))]
            TargetLanguage::Swift => backends::swift::test::run_script_with_generated_code(
                manifests_out,
                test_script.as_ref(),
            )?,

            _ => unimplemented!(),
        }

        #[allow(unreachable_code)]
        Ok(())
    }

    #[test]
    fn test_importing_simple_experimenter_manifest() -> Result<()> {
        // Both the app and lib files declare features, so we should have an experimenter manifest file with two features.
        let tmpfile = NamedTempFile::new()?;
        let cmd = create_experimenter_manifest_cmd(
            "fixtures/fe/importing/simple/app.yaml",
            tmpfile.path(),
        )?;
        generate_experimenter_manifest(&cmd)?;

        let manifest: serde_yaml::Value = serde_yaml::from_reader(&tmpfile)?;
        println!("{:?}", manifest);

        assert!(manifest.is_mapping());
        let manifest = manifest.as_mapping().unwrap();

        assert!(manifest.contains_key("homescreen"));
        assert!(manifest.contains_key("search"));

        Ok(())
    }

    #[test]
    fn test_generate_experimenter_gecko_prefs() -> Result<()> {
        let tmpfile = NamedTempFile::new()?;
        let cmd = create_experimenter_manifest_cmd("fixtures/fe/gecko-pref.yaml", tmpfile.path())?;
        generate_experimenter_manifest(&cmd)?;

        let manifest: serde_yaml::Value = serde_yaml::from_reader(&tmpfile)?;
        println!("{:?}", manifest);

        let variables = manifest
            .get("gecko-nimbus-validation")
            .unwrap()
            .get("variables")
            .unwrap();

        fn assert_pref_var(
            variable: &serde_yaml::Value,
            expected_pref: &str,
            expected_branch: &str,
        ) {
            let pref_annotation = variable.get("setPref").unwrap();
            let pref = pref_annotation.get("pref").unwrap().as_str().unwrap();
            assert_eq!(pref, expected_pref);
            let branch = pref_annotation.get("branch").unwrap().as_str().unwrap();
            assert_eq!(branch, expected_branch);
        }

        assert_pref_var(
            variables.get("test-preference-bool").unwrap(),
            "gecko.nimbus.test.bool",
            "default",
        );

        assert_pref_var(
            variables.get("test-preference-int").unwrap(),
            "gecko.nimbus.test.int",
            "default",
        );

        assert_pref_var(
            variables.get("test-preference-string").unwrap(),
            "gecko.nimbus.test.string",
            "default",
        );

        Ok(())
    }

    #[test]
    fn test_generate_catches_invalid_feature() -> Result<(), FMLError> {
        let manifest = join(
            pkg_dir(),
            "fixtures/fe/invalid/invalid_default_value_for_one_channel.fml.yaml",
        );
        let output = NamedTempFile::new()?;

        let cmd: GenerateStructCmd = GenerateStructCmd {
            manifest,
            output: output.path().into(),
            language: TargetLanguage::ExperimenterYAML,
            load_from_ir: false,
            channel: "app-debug".to_string(),
            loader: Default::default(),
        };

        let result = generate_struct(&cmd);
        assert!(result.is_err());
        Ok(())
    }

    #[test]
    fn test_validate_command() -> Result<()> {
        let paths = MANIFEST_PATHS
            .iter()
            .filter(|p| p.ends_with(".yaml"))
            .chain([&"fixtures/fe/no_about_no_channels.yaml"])
            .collect::<Vec<&&str>>();
        for path in paths {
            let manifest = join(pkg_dir(), path);
            let cmd = ValidateCmd {
                loader: Default::default(),
                manifest,
            };
            validate(&cmd)?;
        }
        Ok(())
    }

    fn lint_cmd(path: &str) -> LintCmd {
        LintCmd {
            manifest: join(pkg_dir(), path),
            loader: Default::default(),
            allow: Default::default(),
            deny: Default::default(),
            error_on_warning: false,
            include_imports: false,
            as_json: false,
        }
    }

    /// The lints a fixture trips, sorted and deduplicated.
    fn lints_for(path: &str) -> Result<Vec<&'static str>> {
        let report = lint_report(&lint_cmd(path))?;
        let mut lints: Vec<_> = report.findings.iter().map(|f| f.lint).collect();
        lints.sort_unstable();
        lints.dedup();
        Ok(lints)
    }

    #[test]
    fn test_lint_command_says_nothing_about_a_well_formed_manifest() -> Result<()> {
        let path = "fixtures/fe/lints/well-formed.fml.yaml";
        assert_eq!(lints_for(path)?, Vec::<&str>::new());

        // Warnings are all the lints produce by default, so this succeeds.
        lint(&lint_cmd(path))?;
        Ok(())
    }

    #[test]
    fn test_lint_command_finds_the_problems_in_a_manifest() -> Result<()> {
        assert_eq!(
            lints_for("fixtures/fe/lints/needs-work.fml.yaml")?,
            vec![
                "COMMON_PREFIX",
                "DEEP_NESTING",
                "ENUM_VARIANT_CASING",
                "FEATURE_NAME_CASING",
                "MISSING_CONTACTS",
                "MISSING_DOCUMENTATION",
                "MISSING_ENABLED_VARIABLE",
                "MISSING_META_BUG",
                "NEGATED_BOOLEAN",
                "STRINGLY_TYPED",
                "TERSE_DESCRIPTION",
                "TODO_IN_DESCRIPTION",
                "TRIVIAL_ENUM",
                "TYPE_IN_NAME",
                "TYPE_NAME_CASING",
                "UNUSED_TYPE",
                "VARIABLE_NAME_CASING",
            ]
        );
        Ok(())
    }

    #[test]
    fn test_lint_command_honours_no_lint() -> Result<()> {
        // The file excuses itself from MISSING_META_BUG, its feature from
        // MISSING_ENABLED_VARIABLE.
        assert_eq!(
            lints_for("fixtures/fe/lints/suppressions.fml.yaml")?,
            // ... but both lists also name a lint that doesn't exist.
            vec!["UNKNOWN_LINT"]
        );
        Ok(())
    }

    #[test]
    fn test_lint_command_reports_unknown_names_at_both_levels() -> Result<()> {
        let report = lint_report(&lint_cmd("fixtures/fe/lints/suppressions.fml.yaml"))?;
        let unknown: Vec<_> = report
            .findings
            .iter()
            .filter(|f| f.lint == "UNKNOWN_LINT")
            .map(|f| (f.subject.as_str(), f.message.as_str()))
            .collect();

        assert_eq!(
            unknown,
            vec![
                (
                    "feature `legacy-feature`",
                    "`no-lint` names `NOT_A_REAL_LINT`, which isn't a lint"
                ),
                (
                    "this manifest",
                    "`no-lint` names `NOT_A_REAL_FILE_LINT`, which isn't a lint"
                ),
            ]
        );
        Ok(())
    }

    #[test]
    fn test_lint_says_when_no_lint_silenced_a_finding() -> Result<()> {
        // The included file silences MISSING_META_BUG, so lint would otherwise call
        // the manifest clean without saying why.
        let report = lint_report(&lint_cmd("fixtures/fe/lints/including.fml.yaml"))?;
        assert!(report.is_empty());
        assert_eq!(report.suppressed, 1);

        let mut buffer = termcolor::Buffer::no_color();
        output_suppressed(&mut buffer, &report)?;
        let output = String::from_utf8(buffer.into_inner()).expect("output is UTF-8");
        assert!(
            output.contains("1 finding silenced by `no-lint`"),
            "{output}"
        );

        Ok(())
    }

    #[test]
    fn test_lint_command_honours_no_lint_in_an_included_file() -> Result<()> {
        // The included file excuses what it defines from MISSING_META_BUG; the
        // including file provides its own.
        let path = "fixtures/fe/lints/including.fml.yaml";
        assert_eq!(lints_for(path)?, Vec::<&str>::new());
        assert_eq!(lint_report(&lint_cmd(path))?.suppressed, 1);
        Ok(())
    }

    #[test]
    fn test_lint_command_allow_and_deny() -> Result<()> {
        let path = "fixtures/fe/lints/needs-work.fml.yaml";

        let mut cmd = lint_cmd(path);
        cmd.allow = vec!["TRIVIAL_ENUM".to_string()];
        assert!(!lint_report(&cmd)?
            .findings
            .iter()
            .any(|f| f.lint == "TRIVIAL_ENUM"));

        // Warnings on their own don't fail the run.
        let mut cmd = lint_cmd(path);
        lint(&cmd)?;

        cmd.deny = vec!["TRIVIAL_ENUM".to_string()];
        assert!(lint(&cmd).is_err());

        let mut cmd = lint_cmd(path);
        cmd.error_on_warning = true;
        assert!(lint(&cmd).is_err());

        Ok(())
    }

    /// Render a report the way `lint` does, minus the colour.
    fn rendered(report: &LintReport) -> Result<String> {
        let mut buffer = termcolor::Buffer::no_color();
        output_findings(&mut buffer, report)?;
        output_lint_summary(&mut buffer, report)?;
        Ok(String::from_utf8(buffer.into_inner()).expect("output is UTF-8"))
    }

    #[test]
    fn test_every_finding_says_what_it_is_about() -> Result<()> {
        // Findings are grouped by feature, so two of the same lint in one feature
        // are only distinguishable by their messages.
        let cmd = lint_cmd("fixtures/fe/lints/needs-work.fml.yaml");
        let report = lint_report(&cmd)?;
        let output = rendered(&report)?;

        let findings: Vec<_> = output
            .lines()
            .filter(|l| l.trim_start().starts_with(['⚠', '❎']))
            .collect();
        assert_eq!(findings.len(), report.findings.len());

        let distinct: HashSet<_> = findings.iter().collect();
        assert_eq!(
            distinct.len(),
            findings.len(),
            "two findings render identically:\n{output}"
        );

        Ok(())
    }

    #[test]
    fn test_help_is_printed_once_per_lint() -> Result<()> {
        let cmd = lint_cmd("fixtures/fe/lints/needs-work.fml.yaml");
        let report = lint_report(&cmd)?;
        // The help is wrapped, so match against it unwrapped.
        let output = rendered(&report)?
            .split_whitespace()
            .collect::<Vec<_>>()
            .join(" ");

        // The fixture trips MISSING_META_BUG once and TERSE_DESCRIPTION twice.
        assert_eq!(output.matches("Add a `meta-bug` URL").count(), 1);
        assert_eq!(
            output
                .matches("use the description to say what changes when the value changes")
                .count(),
            1
        );

        Ok(())
    }

    #[test]
    fn test_grouping_names_each_subject_once() -> Result<()> {
        let cmd = lint_cmd("fixtures/fe/lints/needs-work.fml.yaml");
        let report = lint_report(&cmd)?;
        let output = rendered(&report)?;

        for subject in ["feature `myBadFeature`", "object `unusedObject`"] {
            assert_eq!(
                output.matches(&format!("\n{subject}\n")).count()
                    + usize::from(output.starts_with(&format!("{subject}\n"))),
                1,
                "{subject} should head exactly one group:\n{output}"
            );
        }

        Ok(())
    }

    #[test]
    fn test_summary_counts_what_was_found() -> Result<()> {
        let mut cmd = lint_cmd("fixtures/fe/lints/needs-work.fml.yaml");
        cmd.deny = vec!["TRIVIAL_ENUM".to_string()];
        let report = lint_report(&cmd)?;

        let expected = format!(
            "Found 1 error and {} warnings in 3 places.",
            report.warning_count()
        );
        assert!(rendered(&report)?.contains(&expected), "{expected}");

        let clean = lint_report(&lint_cmd("fixtures/fe/lints/well-formed.fml.yaml"))?;
        assert!(rendered(&clean)?.contains("No lint findings"));

        Ok(())
    }

    #[test]
    fn test_lint_command_counts_suppressed_findings() -> Result<()> {
        // The fixture silences one lint for the file and one for its feature.
        let report = lint_report(&lint_cmd("fixtures/fe/lints/suppressions.fml.yaml"))?;
        assert_eq!(report.suppressed, 2);
        assert!(rendered(&report)?.contains("2 findings silenced by `no-lint`"));

        Ok(())
    }

    #[test]
    fn test_lint_command_json_carries_the_counts() -> Result<()> {
        let mut cmd = lint_cmd("fixtures/fe/lints/needs-work.fml.yaml");
        cmd.deny = vec!["TRIVIAL_ENUM".to_string()];
        let report = lint_report(&cmd)?;

        let json = json_report(&report);
        assert_eq!(json["errors"], 1);
        assert_eq!(json["warnings"], report.warning_count());
        assert_eq!(json["suppressed"], 0);
        assert_eq!(json["subjects"], 3);
        assert_eq!(
            json["findings"].as_array().unwrap().len(),
            report.findings.len()
        );

        Ok(())
    }

    #[test]
    fn test_lint_command_rejects_unknown_lint_names() {
        let mut cmd = lint_cmd("fixtures/fe/lints/well-formed.fml.yaml");
        cmd.allow = vec!["NOT_A_LINT".to_string()];

        let error = lint(&cmd).expect_err("An unknown lint name should be an error");
        assert!(error.to_string().contains("NOT_A_LINT"));
    }

    #[test]
    fn test_lint_command_ignores_imported_features_by_default() -> Result<()> {
        let path = "fixtures/fe/importing/simple/app.yaml";

        let cmd = lint_cmd(path);
        let report = lint_report(&cmd)?;
        assert!(report.findings.iter().all(|f| f.module.is_none()));

        let mut cmd = lint_cmd(path);
        cmd.include_imports = true;
        let report = lint_report(&cmd)?;
        assert!(report.findings.iter().any(|f| f.module.is_some()));

        Ok(())
    }

    #[test]
    fn test_lint_list_names_every_lint() -> Result<()> {
        let mut buffer = termcolor::Buffer::no_color();
        output_lint_list(&mut buffer)?;
        let output = String::from_utf8(buffer.into_inner()).expect("output is UTF-8");

        // A header, then one row per lint and nothing else.
        assert_eq!(output.lines().count(), lints::ALL_LINTS.len() + 1);

        for lint in lints::ALL_LINTS.iter() {
            let row = output
                .lines()
                .find(|l| l.starts_with(lint.name))
                .unwrap_or_else(|| panic!("{} should be listed:\n{output}", lint.name));
            assert!(row.contains(lint.category.as_str()), "{row}");
            assert!(row.contains(lint.default_level.as_str()), "{row}");
            assert!(row.contains(lint.description), "{row}");
        }

        Ok(())
    }

    /// A lint no fixture trips is either raising nothing at all, or is only ever
    /// tested through the module that declares it.
    #[test]
    fn test_every_lint_is_tripped_by_a_fixture() -> Result<()> {
        let mut fired: HashSet<&str> = HashSet::new();

        for entry in std::fs::read_dir(join(pkg_dir(), "fixtures/fe/lints"))? {
            let name = entry?.file_name().to_string_lossy().to_string();
            // `invalid.fml.yaml` is there to be rejected before any lint runs.
            if !name.ends_with(MATCHING_FML_EXTENSION) || name == "invalid.fml.yaml" {
                continue;
            }

            let mut cmd = lint_cmd(&format!("fixtures/fe/lints/{name}"));
            cmd.include_imports = true;
            fired.extend(lint_report(&cmd)?.findings.iter().map(|f| f.lint));
        }

        let missing: Vec<_> = lints::ALL_LINTS
            .iter()
            .map(|l| l.name)
            .filter(|name| !fired.contains(name))
            .collect();
        assert_eq!(
            missing,
            Vec::<&str>::new(),
            "no fixture in fixtures/fe/lints trips these"
        );

        Ok(())
    }

    #[test]
    fn test_lint_command_needs_a_valid_manifest() {
        let cmd = lint_cmd("fixtures/fe/lints/invalid.fml.yaml");

        let error = lint(&cmd).expect_err("An invalid manifest should be an error");
        assert!(
            error.to_string().contains("run `nimbus-fml validate`"),
            "{error}"
        );
    }

    #[test]
    fn test_validate_command_fails_on_bad_default_value_for_one_channel() -> Result<()> {
        let path = "fixtures/fe/invalid/invalid_default_value_for_one_channel.fml.yaml";
        let manifest = join(pkg_dir(), path);
        let cmd = ValidateCmd {
            loader: Default::default(),
            manifest,
        };
        let result = validate(&cmd);

        assert!(result.is_err());

        match result.err().unwrap() {
            CliError(error) => {
                assert_eq!(error, "Manifest contains error(s) in 1 channel");
            }
            _ => panic!("Error is not a ValidationError"),
        };

        Ok(())
    }

    pub(crate) fn create_experimenter_manifest_cmd(
        path: &str,
        output: &Path,
    ) -> Result<GenerateExperimenterManifestCmd> {
        let manifest = join(pkg_dir(), path);
        let load_from_ir = manifest.ends_with(".ir.json");
        let loader = Default::default();
        Ok(GenerateExperimenterManifestCmd {
            manifest,
            output: output.into(),
            language: TargetLanguage::ExperimenterYAML,
            load_from_ir,
            loader,
        })
    }

    fn test_single_merged_manifest_file(path: &str, channel: &str) -> Result<()> {
        let manifest = join(pkg_dir(), path);
        let loader = Default::default();

        // Load the source file, and get the default_json()
        let files: FileLoader = TryFrom::try_from(&loader)?;
        let src = files.file_path(&manifest)?;
        let fm = load_feature_manifest(files, src, false, Some(channel), false)?;
        let expected = fm.default_json();

        let output = NamedTempFile::new()?;

        // Generate the merged file
        let cmd = GenerateSingleFileManifestCmd {
            loader: Default::default(),
            manifest,
            output: output.path().into(),
            channel: channel.to_string(),
        };
        generate_single_file_manifest(&cmd)?;

        // Reload the generated file, and get the default_json()
        let dest = FilePath::Local(output.path().into());
        let files: FileLoader = TryFrom::try_from(&loader)?;
        let fm = load_feature_manifest(files, dest, false, Some(channel), false)?;
        let observed = fm.default_json();

        // They should be the same.
        assert_eq!(expected, observed);

        Ok(())
    }

    #[test]
    fn test_single_file_command() -> Result<()> {
        test_single_merged_manifest_file("fixtures/fe/browser.yaml", "release")?;
        test_single_merged_manifest_file(
            "fixtures/fe/importing/including-imports/ui.fml.yaml",
            "none",
        )?;
        test_single_merged_manifest_file(
            "fixtures/fe/importing/including-imports/app.fml.yaml",
            "release",
        )?;
        test_single_merged_manifest_file("fixtures/fe/importing/overrides/app.fml.yaml", "debug")?;
        test_single_merged_manifest_file("fixtures/fe/importing/overrides/lib.fml.yaml", "debug")?;
        test_single_merged_manifest_file("fixtures/fe/importing/diamond/00-app.yaml", "debug")?;
        test_single_merged_manifest_file("fixtures/fe/importing/diamond/01-lib.yaml", "debug")?;
        test_single_merged_manifest_file("fixtures/fe/importing/diamond/02-sublib.yaml", "debug")?;
        test_single_merged_manifest_file("fixtures/fe/misc-features.yaml", "debug")?;
        Ok(())
    }
}

#[cfg(all(
    test,
    feature = "jsonschema-tests",
    not(feature = "all-features-workaround")
))]
mod test_jsonschema {
    use std::fs;

    use jsonschema::JSONSchema;
    use tempfile::NamedTempFile;

    use super::test::{create_experimenter_manifest_cmd, MANIFEST_PATHS};
    use super::*;
    use crate::backends::experimenter_manifest::ExperimenterManifest;
    use crate::util::{join, pkg_dir};

    fn validate_against_experimenter_schema<P: AsRef<Path>>(
        schema_path: P,
        generated_yaml: &serde_yaml::Value,
    ) -> Result<()> {
        let generated_manifest: ExperimenterManifest =
            serde_yaml::from_value(generated_yaml.to_owned())?;
        let generated_json = serde_json::to_value(generated_manifest)?;

        let schema = fs::read_to_string(&schema_path)?;
        let schema: serde_json::Value = serde_json::from_str(&schema)?;
        let compiled = JSONSchema::compile(&schema).expect("The schema is invalid");
        let res = compiled.validate(&generated_json);
        if let Err(e) = res {
            panic!(
                "Validation errors: \n{}",
                e.map(|e| e.to_string()).collect::<Vec<String>>().join("\n")
            );
        }
        Ok(())
    }

    #[test]
    fn test_schema_validation() -> Result<()> {
        for path in MANIFEST_PATHS {
            let output = NamedTempFile::new()?;
            let cmd = create_experimenter_manifest_cmd(path, output.path())?;
            generate_experimenter_manifest(&cmd)?;

            let generated = fs::read_to_string(&cmd.output)?;
            let generated_yaml = serde_yaml::from_str(&generated)?;
            validate_against_experimenter_schema(
                join(pkg_dir(), "ExperimentFeatureManifest.schema.json"),
                &generated_yaml,
            )?;
        }
        Ok(())
    }
}

#[cfg(all(
    test,
    feature = "kotlin-tests",
    not(feature = "all-features-workaround")
))]
mod kts_tests {
    use crate::frontend::{AboutBlock, KotlinAboutBlock};

    use super::test::{
        generate_and_assert, generate_and_assert_with_config, generate_multiple_and_assert,
    };
    use super::*;

    #[test]
    fn test_simple_validation_code_from_ir() -> Result<()> {
        generate_and_assert(
            "test/simple_nimbus_validation.kts",
            "fixtures/ir/simple_nimbus_validation.ir.json",
            "release",
            true,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_objects_code_from_ir() -> Result<()> {
        generate_and_assert(
            "test/with_objects.kts",
            "fixtures/ir/with_objects.ir.json",
            "release",
            true,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_full_homescreen_from_ir() -> Result<()> {
        generate_and_assert(
            "test/full_homescreen.kts",
            "fixtures/ir/full_homescreen.ir.json",
            "release",
            true,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_full_fenix_release() -> Result<()> {
        generate_and_assert_with_config(
            "test/fenix_release.kts",
            "fixtures/fe/browser.yaml",
            "release",
            false,
            AboutBlock {
                kotlin_about: Some(KotlinAboutBlock {
                    package: "com.example.app".to_string(),
                    class: "com.example.release.FxNimbus".to_string(),
                }),
                swift_about: None,
                ..Default::default()
            },
        )?;
        Ok(())
    }

    #[test]
    fn test_with_full_fenix_nightly() -> Result<()> {
        generate_and_assert_with_config(
            "test/fenix_nightly.kts",
            "fixtures/fe/browser.yaml",
            "nightly",
            false,
            AboutBlock {
                kotlin_about: Some(KotlinAboutBlock {
                    package: "com.example.app".to_string(),
                    class: "com.example.nightly.FxNimbus".to_string(),
                }),
                swift_about: None,
                ..Default::default()
            },
        )?;
        Ok(())
    }

    #[test]
    fn test_with_full_fenix_nightly_with_prefs() -> Result<()> {
        generate_and_assert_with_config(
            "test/fenix_nightly.kts",
            "fixtures/fe/browser-with-prefs.yaml",
            "nightly",
            false,
            AboutBlock {
                kotlin_about: Some(KotlinAboutBlock {
                    package: "com.example.app".to_string(),
                    class: "com.example.nightly.FxNimbus".to_string(),
                }),
                swift_about: None,
                ..Default::default()
            },
        )?;
        Ok(())
    }

    #[test]
    fn test_with_dx_improvements() -> Result<()> {
        generate_and_assert(
            "test/dx_improvements_testing.kts",
            "fixtures/fe/dx_improvements.yaml",
            "testing",
            false,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_app_menu_from_ir() -> Result<()> {
        generate_and_assert(
            "test/app_menu.kts",
            "fixtures/ir/app_menu.ir.json",
            "release",
            true,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_bundled_resources_kts() -> Result<()> {
        generate_and_assert(
            "test/bundled_resources.kts",
            "fixtures/fe/bundled_resouces.yaml",
            "testing",
            false,
        )?;
        Ok(())
    }

    #[test]
    fn test_importing_simple_kts() -> Result<()> {
        generate_multiple_and_assert(
            "test/importing/simple/app_debug.kts",
            &[
                ("fixtures/fe/importing/simple/lib.yaml", "debug"),
                ("fixtures/fe/importing/simple/app.yaml", "debug"),
            ],
        )?;
        Ok(())
    }

    #[test]
    fn test_importing_channel_mismatching_kts() -> Result<()> {
        generate_multiple_and_assert(
            "test/importing/channels/app_debug.kts",
            &[
                ("fixtures/fe/importing/channels/app.fml.yaml", "app-debug"),
                ("fixtures/fe/importing/channels/lib.fml.yaml", "debug"),
            ],
        )?;
        Ok(())
    }

    #[test]
    fn test_importing_override_defaults_kts() -> Result<()> {
        generate_multiple_and_assert(
            "test/importing/overrides/app_debug.kts",
            &[
                ("fixtures/fe/importing/overrides/app.fml.yaml", "debug"),
                ("fixtures/fe/importing/overrides/lib.fml.yaml", "debug"),
            ],
        )?;
        Ok(())
    }

    #[test]
    fn test_importing_override_defaults_coverall_kts() -> Result<()> {
        generate_multiple_and_assert(
            "test/importing/overrides-coverall/app_debug.kts",
            &[
                (
                    "fixtures/fe/importing/overrides-coverall/app.fml.yaml",
                    "debug",
                ),
                (
                    "fixtures/fe/importing/overrides-coverall/lib.fml.yaml",
                    "debug",
                ),
            ],
        )?;
        Ok(())
    }

    #[test]
    fn test_importing_diamond_overrides_kts() -> Result<()> {
        // In this test, sublib implements a feature.
        // Both lib and app offer some configuration, and both app and lib
        // need to import sublib.
        generate_multiple_and_assert(
            "test/importing/diamond/00-app.kts",
            &[
                ("fixtures/fe/importing/diamond/00-app.yaml", "debug"),
                ("fixtures/fe/importing/diamond/01-lib.yaml", "debug"),
                ("fixtures/fe/importing/diamond/02-sublib.yaml", "debug"),
            ],
        )?;
        Ok(())
    }

    #[test]
    #[ignore]
    fn test_importing_reexporting_features() -> Result<()> {
        // In this test, sublib implements a feature.
        // Both lib and app offer some configuration, but app doesn't need to know
        // that the feature is provided by sublib– where the feature lives
        // is an implementation detail, and should be encapsulated by lib.
        // This is currently not possible, but filed as EXP-2540.
        generate_multiple_and_assert(
            "test/importing/reexporting/00-app.kts",
            &[
                ("fixtures/fe/importing/reexporting/00-app.yaml", "debug"),
                ("fixtures/fe/importing/reexporting/01-lib.yaml", "debug"),
                ("fixtures/fe/importing/reexporting/02-sublib.yaml", "debug"),
            ],
        )?;
        Ok(())
    }

    #[test]
    fn test_importing_including_imports_kts() -> Result<()> {
        generate_multiple_and_assert(
            "test/importing/including-imports/app_release.kts",
            &[
                (
                    "fixtures/fe/importing/including-imports/ui.fml.yaml",
                    "none",
                ),
                (
                    "fixtures/fe/importing/including-imports/app.fml.yaml",
                    "release",
                ),
            ],
        )?;
        Ok(())
    }

    #[test]
    fn regression_test_concurrent_access_of_feature_holder_kts() -> Result<()> {
        generate_and_assert(
            "test/threadsafe_feature_holder.kts",
            "fixtures/fe/browser.yaml",
            "release",
            false,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_coenrolled_features_and_imports_kts() -> Result<()> {
        generate_multiple_and_assert(
            "test/allow_coenrolling.kts",
            &[
                ("fixtures/fe/importing/coenrolling/app.fml.yaml", "release"),
                ("fixtures/fe/importing/coenrolling/ui.fml.yaml", "release"),
            ],
        )?;
        Ok(())
    }

    #[test]
    fn test_with_preference_overrides_kt() -> Result<()> {
        generate_multiple_and_assert(
            "test/pref_overrides.kts",
            &[("fixtures/fe/pref_overrides.fml.yaml", "debug")],
        )?;
        Ok(())
    }
}

#[cfg(all(
    test,
    feature = "swift-tests",
    not(feature = "all-features-workaround")
))]
mod swift_tests {
    use super::test::{generate_and_assert, generate_multiple_and_assert};
    use super::*;

    #[test]
    fn test_with_app_menu_swift_from_ir() -> Result<()> {
        generate_and_assert(
            "test/app_menu.swift",
            "fixtures/ir/app_menu.ir.json",
            "release",
            true,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_objects_swift_from_ir() -> Result<()> {
        generate_and_assert(
            "test/with_objects.swift",
            "fixtures/ir/with_objects.ir.json",
            "release",
            true,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_bundled_resources_swift() -> Result<()> {
        generate_and_assert(
            "test/bundled_resources.swift",
            "fixtures/fe/bundled_resouces.yaml",
            "testing",
            false,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_full_fenix_release_swift() -> Result<()> {
        generate_and_assert(
            "test/fenix_release.swift",
            "fixtures/fe/browser.yaml",
            "release",
            false,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_full_fenix_nightly_swift() -> Result<()> {
        generate_and_assert(
            "test/fenix_nightly.swift",
            "fixtures/fe/browser.yaml",
            "nightly",
            false,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_full_firefox_swift() -> Result<()> {
        generate_and_assert(
            "test/firefox_ios_release.swift",
            "fixtures/fe/including/ios.yaml",
            "release",
            false,
        )?;
        Ok(())
    }

    #[test]
    fn test_importing_simple_swift() -> Result<()> {
        generate_multiple_and_assert(
            "test/importing/simple/app_debug.swift",
            &[
                ("fixtures/fe/importing/simple/app.yaml", "debug"),
                ("fixtures/fe/importing/simple/lib.yaml", "debug"),
            ],
        )?;
        Ok(())
    }

    #[test]
    fn test_importing_override_defaults_swift() -> Result<()> {
        generate_multiple_and_assert(
            "test/importing/overrides/app_debug.swift",
            &[
                ("fixtures/fe/importing/overrides/app.fml.yaml", "debug"),
                ("fixtures/fe/importing/overrides/lib.fml.yaml", "debug"),
            ],
        )?;
        Ok(())
    }
    #[test]
    fn test_importing_diamond_overrides_swift() -> Result<()> {
        // In this test, sublib implements a feature.
        // Both lib and app offer some configuration, and both app and lib
        // need to import sublib.
        generate_multiple_and_assert(
            "test/importing/diamond/00-app.swift",
            &[
                ("fixtures/fe/importing/diamond/00-app.yaml", "debug"),
                ("fixtures/fe/importing/diamond/01-lib.yaml", "debug"),
                ("fixtures/fe/importing/diamond/02-sublib.yaml", "debug"),
            ],
        )?;
        Ok(())
    }

    #[test]
    fn test_importing_including_imports_swift() -> Result<()> {
        generate_multiple_and_assert(
            "test/importing/including-imports/app_release.swift",
            &[
                (
                    "fixtures/fe/importing/including-imports/ui.fml.yaml",
                    "none",
                ),
                (
                    "fixtures/fe/importing/including-imports/app.fml.yaml",
                    "release",
                ),
            ],
        )?;
        Ok(())
    }
    #[test]
    fn regression_test_concurrent_access_of_feature_holder_swift() -> Result<()> {
        generate_and_assert(
            "test/threadsafe_feature_holder.swift",
            "fixtures/fe/browser.yaml",
            "release",
            false,
        )?;
        Ok(())
    }

    #[test]
    fn test_with_coenrolled_features_and_imports_swift() -> Result<()> {
        generate_multiple_and_assert(
            "test/allow_coenrolling.swift",
            &[
                ("fixtures/fe/importing/coenrolling/app.fml.yaml", "release"),
                ("fixtures/fe/importing/coenrolling/ui.fml.yaml", "release"),
            ],
        )?;
        Ok(())
    }

    #[test]
    fn test_with_preference_overrides_swift() -> Result<()> {
        generate_multiple_and_assert(
            "test/pref_overrides.swift",
            &[("fixtures/fe/pref_overrides.fml.yaml", "debug")],
        )?;
        Ok(())
    }
}
