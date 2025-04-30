use std::{
    borrow::Cow,
    collections::{BTreeMap, BTreeSet},
    env::set_current_dir,
    path::PathBuf,
    process::{ExitCode, Stdio},
};

use clap::Parser;
use ezcmd::EasyCommand;
use itertools::Itertools;
use joinery::JoinableIterator;
use miette::{ensure, miette, Context, Diagnostic, IntoDiagnostic, Report, SourceSpan};
use regex::Regex;

use crate::{
    fs::{create_dir_all, remove_file, FileRoot},
    process::which,
};

mod fs;
mod process;
mod test_split;

/// Vendor WebGPU CTS tests from a local Git checkout of [our `gpuweb/cts` fork].
///
/// WPT tests are generated into `testing/web-platform/mozilla/tests/webgpu/`. If the set of tests
/// changes upstream, make sure that the generated output still matches up with test expectation
/// metadata in `testing/web-platform/mozilla/meta/webgpu/`.
///
/// [our `gpuweb/cts` fork]: https://github.com/mozilla/gpuweb-cts
#[derive(Debug, Parser)]
struct CliArgs {
    /// A path to the top-level directory of your WebGPU CTS checkout.
    cts_checkout_path: PathBuf,
}

fn main() -> ExitCode {
    env_logger::builder()
        .filter_level(log::LevelFilter::Info)
        .parse_default_env()
        .init();

    let args = CliArgs::parse();

    match run(args) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            log::error!("{e:?}");
            ExitCode::FAILURE
        }
    }
}

fn run(args: CliArgs) -> miette::Result<()> {
    let CliArgs { cts_checkout_path } = args;

    let cts_ckt = FileRoot::new("cts", cts_checkout_path).unwrap();

    let npm_bin = which("npm", "NPM binary")?;

    let node_bin = which("node", "Node.js binary")?;

    set_current_dir(&*cts_ckt)
        .into_diagnostic()
        .wrap_err("failed to change working directory to CTS checkout")?;
    log::debug!("changed CWD to {cts_ckt}");

    let mut npm_ci_cmd = EasyCommand::simple(&npm_bin, ["ci"]);
    log::info!(
        "ensuring a clean {} directory with {npm_ci_cmd}…",
        cts_ckt.child("node_modules"),
    );
    npm_ci_cmd.run().into_diagnostic()?;

    let out_wpt_dir = cts_ckt.regen_dir("out-wpt", |out_wpt_dir| {
        let mut npm_run_wpt_cmd = EasyCommand::simple(&npm_bin, ["run", "wpt"]);
        log::info!("generating WPT test cases into {out_wpt_dir} with {npm_run_wpt_cmd}…");
        npm_run_wpt_cmd.run().into_diagnostic()
    })?;

    let cts_https_html_path = out_wpt_dir.child("cts-withsomeworkers.https.html");

    {
        for file_name in ["cts-chunked2sec.https.html", "cts.https.html"] {
            let file_name = out_wpt_dir.child(file_name);
            log::info!("removing extraneous {file_name}…");
            remove_file(&*file_name)?;
        }
    }

    log::info!("analyzing {cts_https_html_path}…");
    let cts_https_html_content = fs::read_to_string(&*cts_https_html_path)?;
    let cts_boilerplate_short_timeout;
    let cts_boilerplate_long_timeout;
    let cts_cases;
    #[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
    enum WorkerType {
        Dedicated,
        Service,
        Shared,
    }

    impl WorkerType {
        const DEDICATED: &str = "dedicated";
        const SERVICE: &str = "service";
        const SHARED: &str = "shared";

        pub(crate) fn new(s: &str) -> Option<Self> {
            match s {
                Self::DEDICATED => Some(WorkerType::Dedicated),
                Self::SERVICE => Some(WorkerType::Service),
                Self::SHARED => Some(WorkerType::Shared),
                _ => None,
            }
        }

        pub(crate) fn as_str(&self) -> &'static str {
            match self {
                Self::Dedicated => Self::DEDICATED,
                Self::Service => Self::SERVICE,
                Self::Shared => Self::SHARED,
            }
        }
    }
    {
        {
            let (boilerplate, cases_start) = {
                let cases_start_idx = cts_https_html_content
                    .find("<meta name=variant")
                    .ok_or_else(|| miette!("no test cases found; this is unexpected!"))?;
                cts_https_html_content.split_at(cases_start_idx)
            };

            {
                if !boilerplate.is_empty() {
                    #[derive(Debug, Diagnostic, thiserror::Error)]
                    #[error("last character before test cases was not a newline; bug, or weird?")]
                    #[diagnostic(severity("warning"))]
                    struct Oops {
                        #[label(
                            "this character ({:?}) was expected to be a newline, so that {}",
                            source_code.chars().last().unwrap(),
                            "the test spec. following it is on its own line"
                        )]
                        span: SourceSpan,
                        #[source_code]
                        source_code: String,
                    }
                    ensure!(
                        boilerplate.ends_with('\n'),
                        Oops {
                            span: SourceSpan::from(0..boilerplate.len()),
                            source_code: cts_https_html_content,
                        }
                    );
                }

                // NOTE: Adding `_mozilla` is necessary because [that's how it's mounted][source].
                //
                // [source]: https://searchfox.org/mozilla-central/rev/cd2121e7d83af1b421c95e8c923db70e692dab5f/testing/web-platform/mozilla/README#1-4]
                log::info!(concat!(
                    "  …fixing `script` paths in WPT boilerplate ",
                    "so they work as Mozilla-private WPT tests…"
                ));
                let expected_wpt_script_tag =
                    "<script type=module src=/webgpu/common/runtime/wpt.js></script>";
                ensure!(
                    boilerplate.contains(expected_wpt_script_tag),
                    format!(
                        concat!(
                            "failed to find expected `script` tag for `wpt.js` ",
                            "({:?}); did something change upstream?"
                        ),
                        expected_wpt_script_tag
                    ),
                );
                let mut boilerplate = boilerplate.replacen(
                    expected_wpt_script_tag,
                    "<script type=module src=/_mozilla/webgpu/common/runtime/wpt.js></script>",
                    1,
                );

                cts_boilerplate_short_timeout = boilerplate.clone();

                let timeout_insert_idx = {
                    let meta_charset_utf8 = "\n<meta charset=utf-8>\n";
                    let meta_charset_utf8_idx =
                        boilerplate.find(meta_charset_utf8).ok_or_else(|| {
                            miette!(
                                "could not find {:?} in document; did something change upstream?",
                                meta_charset_utf8
                            )
                        })?;
                    meta_charset_utf8_idx + meta_charset_utf8.len()
                };
                boilerplate.insert_str(
                    timeout_insert_idx,
                    concat!(
                        r#"<meta name="timeout" content="long">"#,
                        " <!-- TODO: narrow to only where it's needed, see ",
                        "https://bugzilla.mozilla.org/show_bug.cgi?id=1850537",
                        " -->\n"
                    ),
                );
                cts_boilerplate_long_timeout = boilerplate
            };

            log::info!("  …parsing test variants in {cts_https_html_path}…");
            let mut parsing_failed = false;
            let meta_variant_regex = Regex::new(concat!(
                "^",
                "<meta name=variant content='",
                r"\?",
                r"(:?worker=(?P<worker_type>\w+)&)?",
                r"q=(?P<test_path>[^']*?:\*)",
                "'>",
                "$"
            ))
            .unwrap();
            cts_cases = cases_start
                .split_terminator('\n')
                .filter_map(|line| {
                    if line.is_empty() {
                        // Empty separator lines exist between groups of different `worker_type`s.
                        return None;
                    }
                    let captures = meta_variant_regex.captures(line);
                    if captures.is_none() {
                        parsing_failed = true;
                        log::error!("line is not a test case: {line:?}");
                    }
                    let captures = captures?;

                    let test_path = captures["test_path"].to_owned();

                    let worker_type =
                        captures
                            .name("worker_type")
                            .map(|wt| wt.as_str())
                            .and_then(|wt| match WorkerType::new(wt) {
                                Some(wt) => Some(wt),
                                None => {
                                    parsing_failed = true;
                                    log::error!("unrecognized `worker` type {wt:?}");
                                    None
                                }
                            });

                    Some((test_path, worker_type, line))
                })
                .collect::<Vec<_>>();
            ensure!(
                !parsing_failed,
                "one or more test case lines failed to parse, fix it and try again"
            );
        };
        log::trace!("\"original\" HTML boilerplate:\n\n{cts_boilerplate_short_timeout}");

        ensure!(
            !cts_cases.is_empty(),
            "no test cases found; this is unexpected!"
        );
        log::info!("  …found {} test cases", cts_cases.len());
    }

    let test_listing_buf;
    let mut tests_to_split = {
        log::info!("generating index of tests to split…");

        let test_split_config = {
            use test_split::*;
            [(
                "webgpu:api,operation,command_buffer,image_copy:mip_levels",
                Config {
                    new_sibling_basename: "image_copy__mip_levels",
                    split_by: SplitBy::first_param(
                        "initMethod",
                        SplitParamsTo::SeparateTestsInSameFile,
                    ),
                },
            )]
        };

        let mut tests_to_split = test_split_config
            .into_iter()
            .map(|(test_path, config)| (test_path, test_split::Entry::from_config(config)))
            .collect::<BTreeMap<_, _>>();

        let mut cmd = EasyCommand::new_with(&node_bin, |cmd| {
            cmd.args(["tools/run_node", "--list", "webgpu:*"])
                .stderr(Stdio::inherit())
        });
        log::info!("  requesting exhaustive list of tests using {cmd}…");
        test_listing_buf = {
            let stdout = cmd.output().into_diagnostic()?.stdout;
            String::from_utf8(stdout)
                .into_diagnostic()
                .context("failed to read output of exhaustive test listing command")?
        };

        log::info!("  building index from list of tests…");
        for full_path in test_listing_buf.lines() {
            let (subtest_path, params) = split_at_nth_colon(2, full_path)
                .wrap_err_with(|| "failed to parse configured split entry")?;
            if let Some(entry) = tests_to_split.get_mut(subtest_path) {
                entry.process_listing_line(params)?;
            }
        }
        test_split::assert_seen(tests_to_split.iter(), |seen| &seen.listing);

        tests_to_split
    };

    cts_ckt.regen_dir(out_wpt_dir.join("cts"), |cts_tests_dir| {
        log::info!("re-distributing tests into single file per test path…");
        let mut failed_writing = false;
        let mut cts_cases_by_spec_file_dir = BTreeMap::<_, BTreeMap<_, BTreeSet<_>>>::new();
        for (path, worker_type, meta) in cts_cases {
            macro_rules! insert {
                ($path:expr, $meta:expr $(,)?) => {{
                    let dir = cts_tests_dir.child($path);
                    if !cts_cases_by_spec_file_dir
                        .entry(dir)
                        .or_default()
                        .entry(worker_type)
                        .or_default()
                        .insert($meta)
                    {
                        log::warn!("duplicate entry {meta:?} detected")
                    }
                }};
            }

            // Context: We want to mirror CTS upstream's `src/webgpu/**/*.spec.ts` paths as
            // entire WPT tests, with each subtest being a WPT variant. Here's a diagram of
            // a CTS path to explain why the logic below is correct:
            //
            // ```sh
            // webgpu:this,is,the,spec.ts,file,path:test_in_file:…
            // \____/ \___________________________/^\__________/
            //  test      `*.spec.ts` file path    |       |
            // \__________________________________/|       |
            //                   |                 |       |
            //              We want this…          | …but not this. CTS upstream generates
            //                                     | this too, but we don't want to divide
            //         second ':' character here---/ here (yet).
            // ```
            let (test_path, _cases) = match split_at_nth_colon(2, &path) {
                Ok(ok) => ok,
                Err(e) => {
                    failed_writing = true;
                    log::error!("{e}");
                    continue;
                }
            };
            let (test_group_path, _test_name) = test_path.rsplit_once(':').unwrap();
            let test_group_path_components = test_group_path.split([':', ',']);

            if let Some(entry) = tests_to_split.get_mut(test_path) {
                let test_split::Entry { seen, ref config } = entry;
                let test_split::Config {
                    new_sibling_basename,
                    split_by,
                } = config;

                let file_path = test_group_path_components
                    .chain([*new_sibling_basename])
                    .join_with("/")
                    .to_string();

                seen.wpt_files = true;

                match split_by {
                    test_split::SplitBy::FirstParam {
                        expected_name,
                        split_to,
                        observed_values,
                    } => match split_to {
                        test_split::SplitParamsTo::SeparateTestsInSameFile => {
                            for value in observed_values {
                                let new_meta = meta.replace(
                                    &*path,
                                    &format!("{test_path}:{expected_name}={value};*"),
                                );
                                assert_ne!(meta, new_meta);
                                insert!(&file_path, new_meta.into());
                            }
                        }
                    },
                }
            } else {
                insert!(
                    &test_group_path_components.join_with("/").to_string(),
                    meta.into()
                )
            };
        }

        test_split::assert_seen(tests_to_split.iter(), |seen| &seen.wpt_files);

        struct WptEntry<'a> {
            cases: BTreeSet<Cow<'a, str>>,
            timeout_length: TimeoutLength,
        }
        #[derive(Clone, Copy, Debug)]
        enum TimeoutLength {
            Short,
            Long,
        }
        let split_cases = {
            let mut split_cases = BTreeMap::new();
            fn insert_with_default_name<'a>(
                split_cases: &mut BTreeMap<fs::Child<'a>, WptEntry<'a>>,
                spec_file_dir: fs::Child<'a>,
                cases: BTreeMap<Option<WorkerType>, BTreeSet<Cow<'a, str>>>,
                timeout_length: TimeoutLength,
            ) {
                for (worker_type, cases) in cases {
                    // TODO: https://bugzilla.mozilla.org/show_bug.cgi?id=1938663
                    if worker_type == Some(WorkerType::Service) {
                        continue;
                    }
                    let file_stem = worker_type.map(|wt| wt.as_str()).unwrap_or("cts");
                    let path = spec_file_dir.child(format!("{file_stem}.https.html"));
                    assert!(split_cases
                        .insert(
                            path,
                            WptEntry {
                                cases,
                                timeout_length
                            }
                        )
                        .is_none());
                }
            }
            {
                let dld_path =
                    &cts_tests_dir.child("webgpu/api/validation/state/device_lost/destroy");
                let (spec_file_dir, cases) = cts_cases_by_spec_file_dir
                    .remove_entry(dld_path)
                    .expect("no `device_lost/destroy` tests found; did they move?");
                insert_with_default_name(
                    &mut split_cases,
                    spec_file_dir,
                    cases,
                    TimeoutLength::Short,
                );
            }
            for (spec_file_dir, cases) in cts_cases_by_spec_file_dir {
                insert_with_default_name(
                    &mut split_cases,
                    spec_file_dir,
                    cases,
                    TimeoutLength::Long,
                );
            }
            split_cases
        };

        for (path, entry) in split_cases {
            let dir = path.parent().expect("no parent found for ");
            match create_dir_all(dir) {
                Ok(()) => log::trace!("made directory {}", dir.display()),
                Err(e) => {
                    failed_writing = true;
                    log::error!("{e:#}");
                    continue;
                }
            }
            let file_contents = {
                let WptEntry {
                    cases,
                    timeout_length,
                } = entry;
                let content = match timeout_length {
                    TimeoutLength::Short => &cts_boilerplate_short_timeout,
                    TimeoutLength::Long => &cts_boilerplate_long_timeout,
                };
                let mut content = content.as_bytes().to_vec();
                for meta in cases {
                    content.extend(meta.as_bytes());
                    content.extend(b"\n");
                }
                content
            };
            match fs::write(&path, &file_contents)
                .wrap_err_with(|| miette!("failed to write output to path {path:?}"))
            {
                Ok(()) => log::debug!("  …wrote {path}"),
                Err(e) => {
                    failed_writing = true;
                    log::error!("{e:#}");
                }
            }
        }
        ensure!(
            !failed_writing,
            "failed to write one or more WPT test files; see above output for more details"
        );
        log::debug!("  …finished writing new WPT test files!");

        log::info!("  …removing {cts_https_html_path}, now that it's been divided up…");
        remove_file(&cts_https_html_path)?;

        log::info!("moving ready-to-go WPT test files into `cts`…");

        let webgpu_dir = out_wpt_dir.child("webgpu");
        let ready_to_go_tests = wax::Glob::new("**/*.{html,{any,sub,worker}.js}")
            .unwrap()
            .walk(&webgpu_dir)
            .map_ok(|entry| webgpu_dir.child(entry.into_path()))
            .collect::<Result<Vec<_>, _>>()
            .map_err(Report::msg)
            .wrap_err_with(|| {
                format!("failed to walk {webgpu_dir} for ready-to-go WPT test files")
            })?;

        log::trace!("  …will move the following: {ready_to_go_tests:#?}");

        for file in ready_to_go_tests {
            let path_relative_to_webgpu_dir = file.strip_prefix(&webgpu_dir).unwrap();
            let dst_path = cts_tests_dir.child(path_relative_to_webgpu_dir);
            log::trace!("…moving {file} to {dst_path}…");
            ensure!(
                !fs::try_exists(&dst_path)?,
                "internal error: duplicate path found while moving ready-to-go test {} to {}",
                file,
                dst_path,
            );
            fs::create_dir_all(dst_path.parent().unwrap()).wrap_err_with(|| {
                format!(
                    concat!(
                        "failed to create destination parent dirs. ",
                        "while recursively moving from {} to {}",
                    ),
                    file, dst_path,
                )
            })?;
            fs::rename(&file, &dst_path)
                .wrap_err_with(|| format!("failed to move {file} to {dst_path}"))?;
        }
        log::debug!("  …finished moving ready-to-go WPT test files");

        Ok(())
    })?;

    log::info!("All done! Now get your CTS _ON_! :)");

    Ok(())
}

fn split_at_nth_colon(nth: usize, path: &str) -> miette::Result<(&str, &str)> {
    path.match_indices(':')
        .nth(nth)
        .map(|(idx, s)| (&path[..idx], &path[idx + s.len()..]))
        .ok_or_else(move || {
            miette::diagnostic!("failed to split at colon {nth} from CTS path `{path}`").into()
        })
}
