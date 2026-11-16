# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import json
import unittest
from pathlib import Path
from shutil import rmtree
from tempfile import mkdtemp
from unittest import mock

import mozunit
from mach.registrar import Registrar

from mozbuild import rust_commands


def _spec(config=None, **overrides):
    edge = {
        "kind": "library",
        "manifest_path": "/src/toolkit/library/rust/Cargo.toml",
        "working_directory": "/obj/toolkit/library/rust",
        "names": ["gkrust"],
        "features": ["gkrust-feature"],
    }
    edge.update(overrides)
    return {
        "config": {"CARGO": "cargo", "RUST_TARGET": "x86_64-unknown-linux-gnu"}
        if config is None
        else config,
        "edge": edge,
        "topobjdir": "/obj",
        "topsrcdir": "/src",
    }


class TestRunCargoCommand(unittest.TestCase):
    def setUp(self):
        self._categories = []
        for cat in ("build", "post-build", "misc", "testing", "devenv", "build-dev"):
            if cat in Registrar.categories:
                continue
            Registrar.register_category(cat, cat, cat)
            self._categories.append(cat)
        self.tmpdir = Path(mkdtemp())

    def tearDown(self):
        rmtree(self.tmpdir)
        for cat in self._categories:
            del Registrar.categories[cat]
            del Registrar.commands_by_category[cat]

    def _run(
        self,
        kind="library",
        cargo_command="check",
        package_args=(),
        subcommand_args=None,
        cargo_build_flags=None,
        cargo_extra_flags=None,
        jobs=0,
        continue_on_error=False,
        returncode=0,
        write_spec=True,
        spec_overrides=None,
        spec_config=None,
        verbose=False,
        message_format_json=False,
        composed_argv=None,
        environ=None,
        live_substs=None,
    ):
        from mozbuild.mach_commands import _run_cargo_command

        directory = "toolkit/library/rust"
        if write_spec:
            spec_dir = self.tmpdir / directory
            spec_dir.mkdir(parents=True, exist_ok=True)
            (spec_dir / rust_commands.CARGO_SPEC_FILES[kind]).write_text(
                json.dumps(
                    _spec(config=spec_config, kind=kind, **(spec_overrides or {}))
                ),
                encoding="utf-8",
            )

        command_context = mock.Mock(
            substs=live_substs
            or {"CARGO": "cargo", "RUST_TARGET": "x86_64-unknown-linux-gnu"},
            topsrcdir="/src",
            topobjdir=str(self.tmpdir),
        )

        seen = {}

        def fake_argv(command, substs, invocation, subcommand, **kwargs):
            seen["command"] = command
            seen["invocation"] = invocation
            seen["subcommand"] = subcommand
            seen.update(kwargs)
            if composed_argv is not None:
                return composed_argv
            return ["cargo", subcommand]

        def fake_env(
            command,
            substs,
            environ,
            invocation,
            topsrcdir,
            topobjdir,
            ltoable=None,
            subcommand="build",
        ):
            seen["ltoable"] = ltoable
            seen["environ"] = dict(environ)
            seen["env_invocation"] = invocation
            seen["substs"] = substs
            seen["topsrcdir"] = topsrcdir
            seen["topobjdir"] = topobjdir
            return {"COMPOSED_ENV": "1"}

        def fake_run(argv, env=None, cwd=None, check=None):
            seen["run_argv"] = argv
            seen["run_env"] = env
            seen["run_cwd"] = cwd
            return mock.Mock(returncode=returncode)

        with mock.patch.object(
            rust_commands, "compose_env", side_effect=fake_env
        ), mock.patch.object(
            rust_commands, "compose_mach_cargo_argv", side_effect=fake_argv
        ), mock.patch("subprocess.run", side_effect=fake_run), mock.patch.dict(
            "os.environ", environ or {}, clear=True
        ):
            rc = _run_cargo_command(
                command_context,
                "gkrust",
                {"directory": directory, "kind": kind},
                cargo_command,
                package_args,
                subcommand_args,
                cargo_build_flags,
                cargo_extra_flags,
                message_format_json,
                continue_on_error,
                jobs,
                verbose,
            )
        return rc, seen

    def test_composes_from_the_serialized_configuration(self):
        # The live substs deliberately disagree with the spec: mach cargo and the
        # run_cargo action must use the same snapshot.
        rc, seen = self._run(
            spec_config={"CARGO": "spec-cargo", "RUST_TARGET": "aarch64-linux-android"}
        )
        self.assertEqual(rc, 0)
        self.assertEqual(seen["substs"]["CARGO"], "spec-cargo")
        self.assertEqual(seen["substs"]["RUST_TARGET"], "aarch64-linux-android")
        self.assertEqual(seen["topsrcdir"], "/src")
        self.assertEqual(seen["topobjdir"], "/obj")

    def test_reads_library_spec(self):
        rc, seen = self._run(kind="library")
        self.assertEqual(rc, 0)
        self.assertEqual(seen["command"].kind, "library")
        self.assertEqual(seen["subcommand"], "check")

    def test_reads_program_spec(self):
        rc, seen = self._run(
            kind="program",
            cargo_command="clippy",
            spec_overrides={"names": ["geckodriver"]},
        )
        self.assertEqual(rc, 0)
        self.assertEqual(seen["command"].kind, "program")

    def test_reads_host_program_spec(self):
        rc, seen = self._run(
            kind="host-program",
            spec_overrides={"names": ["geckodriver"]},
        )
        self.assertEqual(rc, 0)
        self.assertEqual(seen["command"].kind, "host-program")

    def test_missing_spec_returns_error(self):
        rc, _ = self._run(write_spec=False)
        self.assertEqual(rc, 1)

    def test_jobs_forwarded(self):
        _, seen = self._run(jobs=3)
        self.assertEqual(seen["jobs"], 3)

    def test_failure_propagates(self):
        rc, _ = self._run(returncode=17)
        self.assertEqual(rc, 17)

    def test_continue_on_error_swallows_failure(self):
        rc, _ = self._run(returncode=17, continue_on_error=True)
        self.assertEqual(rc, 0)

    def test_missing_plugin_suggestion(self):
        with mock.patch("builtins.print") as printed:
            rc, _ = self._run(cargo_command="clippy", returncode=101)
        self.assertEqual(rc, 101)
        joined = " ".join(str(c.args[0]) for c in printed.call_args_list if c.args)
        self.assertIn("cargo install cargo-clippy", joined)

    def test_subcommand_args_substituted(self):
        _, seen = self._run(subcommand_args=["--", "{crate}", "{arch}"])
        self.assertEqual(
            seen["extra_cli_flags"],
            ("--", "gkrust", "x86_64-unknown-linux-gnu"),
        )

    def test_package_args_precede_the_subcommand_args(self):
        _, seen = self._run(
            package_args=["-p", "gkrust-shared"], subcommand_args=["--all-targets"]
        )
        self.assertEqual(
            seen["extra_cli_flags"], ("-p", "gkrust-shared", "--all-targets")
        )

    def test_package_args_are_substituted_and_drop_the_auto_args(self):
        _, seen = self._run(package_args=["-p", "gkrust-shared", "--target={arch}"])
        self.assertEqual(
            seen["extra_cli_flags"],
            ("-p", "gkrust-shared", "--target=x86_64-unknown-linux-gnu"),
        )
        self.assertFalse(seen["auto_args"])

    def test_auto_args_kept_without_package_args(self):
        _, seen = self._run()
        self.assertTrue(seen["auto_args"])

    def test_target_substitution_stays_one_argument(self):
        _, seen = self._run(
            kind="host-program", subcommand_args=["{target}"], cargo_command="clippy"
        )
        self.assertEqual(seen["extra_cli_flags"], ("--bin=gkrust",))

    def test_build_flags_override_replaces_computed_flags(self):
        _, seen = self._run(
            cargo_build_flags=["check", "--manifest-path", "{manifest}"]
        )
        self.assertIn("check", seen["build_flags_override"])
        self.assertIn(
            "/src/toolkit/library/rust/Cargo.toml", seen["build_flags_override"]
        )
        # Replacing the build flags drops the LTO capable Rust flags too.
        self.assertFalse(seen["ltoable"])

    def test_ltoable_forced_without_build_flags(self):
        _, seen = self._run()
        self.assertTrue(seen["ltoable"])
        self.assertEqual(seen["build_flags_override"], ())

    def test_cargo_extra_flags_reach_the_composer(self):
        _, seen = self._run(cargo_extra_flags=["--offline", "{crate}"])
        self.assertEqual(seen["invocation"].cargo_extra_flags, ("--offline", "gkrust"))

    def test_cargo_extra_flags_take_precedence_over_the_environment(self):
        _, seen = self._run(
            cargo_extra_flags=["--offline"], environ={"CARGO_EXTRA_FLAGS": "--ignored"}
        )
        self.assertEqual(seen["invocation"].cargo_extra_flags, ("--offline",))
        _, seen = self._run(environ={"CARGO_EXTRA_FLAGS": "--from-env"})
        self.assertEqual(seen["invocation"].cargo_extra_flags, ("--from-env",))

    def test_arguments_keep_their_boundaries(self):
        _, seen = self._run(
            cargo_build_flags=["-f", "{topsrcdir}/my checkout/Cargo.lock"],
            subcommand_args=["--target-dir", "{topsrcdir}/obj dir"],
        )
        self.assertEqual(
            seen["build_flags_override"], ("-f", "/src/my checkout/Cargo.lock")
        )
        self.assertEqual(seen["extra_cli_flags"], ("--target-dir", "/src/obj dir"))

    def test_verbose_prints_quoted_command(self):
        with mock.patch("builtins.print") as printed:
            self._run(verbose=True, composed_argv=["cargo", "check", "a b"])
        joined = " ".join(str(c.args[0]) for c in printed.call_args_list if c.args)
        self.assertIn("cargo check 'a b'", joined)

    def test_verbose_and_json_output_reach_the_invocation(self):
        _, seen = self._run(verbose=True, message_format_json=True)
        self.assertTrue(seen["invocation"].verbose)
        self.assertTrue(seen["invocation"].json_output)
        self.assertIs(seen["env_invocation"], seen["invocation"])

    def test_quiet_invocation_by_default(self):
        _, seen = self._run()
        self.assertFalse(seen["invocation"].verbose)
        self.assertFalse(seen["invocation"].json_output)

    def test_invocation_inherits_the_environment(self):
        environ = {
            "MACH_STDOUT_ISATTY": "1",
            "extra_rustflags": "-Wclippy::all",
            "RUSTC_BOOTSTRAP": "1",
        }
        _, seen = self._run(environ=environ)
        self.assertEqual(seen["invocation"].color, "always")
        self.assertEqual(seen["invocation"].extra_rustflags, ("-Wclippy::all",))
        self.assertEqual(seen["invocation"].rustc_bootstrap, "1")
        self.assertEqual(seen["environ"]["MACH_STDOUT_ISATTY"], "1")
        self.assertEqual(seen["environ"]["RUSTC_BOOTSTRAP"], "1")

    def test_passes_composed_argv_env_and_cwd_to_subprocess(self):
        _, seen = self._run()
        self.assertEqual(seen["run_argv"], ["cargo", "check"])
        self.assertEqual(seen["run_env"], {"COMPOSED_ENV": "1"})
        self.assertEqual(seen["run_cwd"], "/obj/toolkit/library/rust")

    def test_configured_path_replaces_the_inherited_one(self):
        _, seen = self._run(
            live_substs={"CARGO": "cargo", "PATH": "/toolchain/bin:/usr/bin"},
            environ={"PATH": "/usr/bin"},
        )
        self.assertEqual(seen["run_env"]["PATH"], "/toolchain/bin:/usr/bin")
        _, seen = self._run(environ={"PATH": "/usr/bin"})
        self.assertNotIn("PATH", seen["run_env"])

    def test_configured_compiler_cache_substs_reach_the_environment(self):
        _, seen = self._run(
            live_substs={
                "CARGO": "cargo",
                "SCCACHE_BASEDIRS": "/src/",
                "SCCACHE_DIR": "/state/cache/sccache",
                "CCACHE_DIR": "/state/cache/ccache",
                "BUILDCACHE_DIR": "/state/cache/buildcache",
            }
        )
        self.assertEqual(
            seen["run_env"],
            {
                "COMPOSED_ENV": "1",
                "SCCACHE_BASEDIRS": "/src/",
                "SCCACHE_DIR": "/state/cache/sccache",
                "CCACHE_DIR": "/state/cache/ccache",
                "BUILDCACHE_DIR": "/state/cache/buildcache",
            },
        )

    def _run_cargo(self, substs, cargo_command="check", package=None):
        from buildconfig import topsrcdir

        from mozbuild import mach_commands

        command_context = mock.Mock(
            substs=substs,
            topsrcdir=topsrcdir,
            topobjdir=str(self.tmpdir),
        )
        command_context.resolve_num_jobs.return_value = 1
        command_context._spawn.return_value.build.return_value = 0
        command_context._spawn.return_value.configure.return_value = 0
        with mock.patch.object(
            mach_commands, "_run_cargo_command", return_value=0
        ) as executor:
            rc = mach_commands.cargo(command_context, cargo_command, package=package)
        return rc, command_context, executor

    def test_dispatches_to_executor(self):
        rc, cc, executor = self._run_cargo({})
        self.assertEqual(rc, 0)
        executor.assert_called()
        cc.ensure_backend_current.assert_called_once()

    def test_package_dispatch_passes_the_target_placeholder(self):
        _, _, executor = self._run_cargo({}, package="gkrust-shared")
        self.assertEqual(
            executor.call_args.args[4], ["-p", "gkrust-shared", "--target={arch}"]
        )


if __name__ == "__main__":
    mozunit.main()
