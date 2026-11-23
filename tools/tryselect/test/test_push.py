import json
import shlex
import shutil
import subprocess
import sys
import tempfile
import urllib.parse
from contextlib import ExitStack
from unittest.mock import MagicMock, patch

import mozunit
import pytest
from mozversioncontrol.repo.git import GitRepository
from responses import RequestsMock
from tryselect import push
from tryselect.util.taskcluster import TC_ROOT_URL
from tryselect.util.taskcluster import get_client as real_get_client

real_check_tree_status = push.check_tree_status


@pytest.mark.parametrize(
    "method,labels,params,routes,expected",
    (
        pytest.param(
            "fuzzy",
            ["task-foo", "task-bar"],
            None,
            None,
            {
                "parameters": {
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "tasks": ["task-bar", "task-foo"],
                    },
                },
                "version": 2,
            },
            id="basic",
        ),
        pytest.param(
            "fuzzy",
            ["task-foo"],
            {"existing_tasks": {"task-foo": "123", "task-bar": "abc"}},
            None,
            {
                "parameters": {
                    "existing_tasks": {"task-bar": "abc"},
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "tasks": ["task-foo"],
                    },
                },
                "version": 2,
            },
            id="existing_tasks",
        ),
        pytest.param(
            "fuzzy",
            ["task-" + str(i) for i in range(1001)],  # 1001 tasks, over threshold
            None,
            None,
            {
                "parameters": {
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "priority": "lowest",
                        "tasks": sorted(["task-" + str(i) for i in range(1001)]),
                    },
                },
                "version": 2,
            },
            id="large_push_with_priority",
        ),
        pytest.param(
            "fuzzy",
            ["task-" + str(i) for i in range(500)],  # 500 tasks with rebuild=3
            {"try_task_config": {"rebuild": 3}},
            None,
            {
                "parameters": {
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "priority": "lowest",
                        "rebuild": 3,
                        "tasks": sorted(["task-" + str(i) for i in range(500)]),
                    },
                },
                "version": 2,
            },
            id="large_push_with_rebuild",
        ),
        pytest.param(
            "fuzzy",
            ["task-" + str(i) for i in range(100)],  # Under threshold
            None,
            None,
            {
                "parameters": {
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "tasks": sorted(["task-" + str(i) for i in range(100)]),
                    },
                },
                "version": 2,
            },
            id="small_push_no_priority",
        ),
        pytest.param(
            "fuzzy",
            [
                "task-" + str(i) for i in range(1001)
            ],  # Large push with existing priority
            {"try_task_config": {"priority": "low"}},
            None,
            {
                "parameters": {
                    "optimize_target_tasks": False,
                    "try_task_config": {
                        "env": {"TRY_SELECTOR": "fuzzy"},
                        "priority": "low",  # Should keep existing priority
                        "tasks": sorted(["task-" + str(i) for i in range(1001)]),
                    },
                },
                "version": 2,
            },
            id="large_push_existing_priority",
        ),
    ),
)
def test_generate_try_task_config(method, labels, params, routes, expected):
    # Simulate user responding "yes" to the large push prompt
    with patch("builtins.input", return_value="y"):
        assert (
            push.generate_try_task_config(method, labels, params=params, routes=routes)
            == expected
        )


def test_large_push_user_declines():
    """Test that when user declines large push warning, the system exits."""
    with patch("builtins.input", return_value="n"):
        with pytest.raises(SystemExit) as exc_info:
            push.generate_try_task_config(
                "fuzzy",
                ["task-" + str(i) for i in range(1001)],
            )
        assert exc_info.value.code == 1


def test_large_push_warning_message(capsys):
    """Test that the warning message is displayed for large pushes."""
    with patch("builtins.input", return_value="y"):
        push.generate_try_task_config(
            "fuzzy",
            ["task-" + str(i) for i in range(1001)],
        )
        captured = capsys.readouterr()
        assert "Your push would schedule at least 1001 tasks" in captured.out
        assert "lowest priority" in captured.out


@pytest.mark.parametrize(
    "path,rejected", [("toolkit/components/bar/tests", False), ("browser", True)]
)
def test_test_verify_path_rule(patch_resolver, capsys, path, rejected):
    patch_resolver(
        [],
        [
            {
                "srcdir_relpath": "toolkit/components/bar/tests/xpcshell/test_a.js",
                "manifest_relpath": "toolkit/components/bar/tests/xpcshell/xpcshell.toml",
            }
        ],
    )
    params = {
        "try_task_config": {
            "env": {"MOZHARNESS_TEST_PATHS": json.dumps({"xpcshell": [path]})}
        }
    }
    labels = ["test-linux2404-64/opt-test-verify-path-*"]

    if rejected:
        with pytest.raises(SystemExit):
            push.generate_try_task_config("fuzzy", labels, params=params)
        assert f"error: cannot verify {path}: " in capsys.readouterr().err
    else:
        push.generate_try_task_config("fuzzy", labels, params=params)


def test_test_verify_path_rule_only_applies_to_test_verify_path(patch_resolver):
    patch_resolver([], [])
    params = {
        "try_task_config": {
            "env": {"MOZHARNESS_TEST_PATHS": json.dumps({"xpcshell": ["browser"]})}
        }
    }
    labels = ["test-linux2404-64/opt-xpcshell-*", "test-linux2404-64/opt-test-verify"]
    push.generate_try_task_config("fuzzy", labels, params=params)


def test_write_task_config(tmp_path, capsys):
    """Test that --write-task-config writes try_task_config.json and doesn't push."""
    mock_vcs = MagicMock()
    mock_vcs.name = "git"
    mock_vcs.path = str(tmp_path)
    mock_vcs.base_ref_as_commit.return_value = "def456fakebaserev"
    try_task_config = push.generate_try_task_config("fuzzy", ["task-foo"])

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.vcs", mock_vcs))
        stack.enter_context(
            patch("tryselect.push.MACH_TRY_REMOTE", "https://example.com/fake-try-repo")
        )
        stack.enter_context(patch("tryselect.push.check_working_directory"))
        stack.enter_context(patch("tryselect.push.write_task_config_history"))

        push._is_hg_try.cache_clear()

        assert (
            push.push_to_try(
                "fuzzy",
                "try: test",
                MagicMock(),
                try_task_config=try_task_config,
                write_task_config=True,
            )
            is None
        )

    mock_vcs.push_to_try.assert_not_called()
    mock_vcs.stage_changes.assert_not_called()

    written = tmp_path / "try_task_config.json"
    assert json.loads(written.read_text()) == try_task_config


def push_and_read_try_config(remote, resolved_url, vcs_name="git"):
    """Run a push and return the try_task_config.json it would have committed."""
    push.vcs.name = vcs_name
    push.vcs.get_remote_url.return_value = resolved_url

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.MACH_TRY_REMOTE", remote))
        stack.enter_context(patch("tryselect.push.check_working_directory"))
        stack.enter_context(patch("tryselect.push.write_task_config_history"))
        push._is_hg_try.cache_clear()

        push.push_to_try(
            "fuzzy",
            "try: test",
            MagicMock(),
            try_task_config={
                "version": 2,
                "parameters": {"try_task_config": {"tasks": ["task1"]}},
            },
            push_to_vcs=True,
        )

    call_kwargs = push.vcs.push_to_try.call_args.kwargs
    return json.loads(call_kwargs["changed_files"]["try_task_config.json"])


def test_push_to_try_records_base_rev():
    """push_to_try records the base revision for a Git try remote."""
    config = push_and_read_try_config(
        "try", "https://github.com/mozilla/enterprise-firefox-try"
    )
    assert config["parameters"]["base_rev"] == "def456fakebaserev"


# Recording base_rev here is harmless: only a Git decision task reads it.
xfail_negation = pytest.mark.xfail(
    reason="_is_git_try treats any non-Mercurial-try remote as a Git try one",
    strict=True,
)


@pytest.mark.parametrize(
    "remote,resolved_url,vcs_name",
    (
        # Mercurial try: the decision task gets the changed files from
        # hg.mozilla.org rather than from a diff, so there is nothing to record.
        pytest.param(push.HG_TRY_URL, None, "git", id="hg_try"),
        pytest.param(push.HG_TRY_URL + "-comm-central", None, "git", id="hg_try_comm"),
        # A Mercurial checkout resolves `try` to a path just like Git does, so the
        # name of the remote alone can't tell the two apart.
        pytest.param("try", "ssh://hg.mozilla.org/try", "hg", id="hg_checkout"),
        # git-cinnabar pushes to Mercurial try from a Git checkout, through a
        # remote that is configured in git and does end with `try`.
        pytest.param(
            "try",
            "hg::https://hg.mozilla.org/try",
            "git",
            id="cinnabar",
            marks=xfail_negation,
        ),
        # A Git remote that isn't a try repository.
        pytest.param(
            "upstream",
            "https://github.com/mozilla-firefox/firefox",
            "git",
            id="not_try",
            marks=xfail_negation,
        ),
    ),
)
def test_push_to_try_skips_base_rev(remote, resolved_url, vcs_name):
    """push_to_try only records a base revision for a Git try remote."""
    config = push_and_read_try_config(remote, resolved_url, vcs_name)
    assert "base_rev" not in config["parameters"]


def test_get_sys_argv():
    input_argv = [
        "./mach",
        "try",
        "fuzzy",
        "--full",
        "--artifact",
        "--push-to-vcs",
        "--query",
        "'android-hw !shippable !nofis",
        "--no-push",
    ]
    expected_string = './mach try fuzzy --full --artifact --push-to-vcs --query "\'android-hw !shippable !nofis" --no-push'
    assert push.get_sys_argv(input_argv) == expected_string


def test_get_sys_argv_2():
    input_argv = [
        "./mach",
        "try",
        "fuzzy",
        "--query",
        "'test-linux1804-64-qr/opt-mochitest-plain-",
        "--worker-override=t-linux-large=gecko-t/t-linux-2204-wayland-experimental",
        "--no-push",
    ]
    expected_string = './mach try fuzzy --query "\'test-linux1804-64-qr/opt-mochitest-plain-" --worker-override=t-linux-large=gecko-t/t-linux-2204-wayland-experimental --no-push'
    assert push.get_sys_argv(input_argv) == expected_string


@pytest.mark.parametrize(
    "url,push_to_vcs,expect_direct_push",
    [
        pytest.param(
            "https://example.com/fake-try-repo",
            False,
            True,
            id="non_hg_remote_https",
        ),
        pytest.param(
            "git@github.com:mozilla/fake-try.git",
            False,
            True,
            id="non_hg_remote_git",
        ),
        pytest.param(
            "https://hg.mozilla.org/other-repo",
            False,
            True,
            id="non_hg_remote_partial_match",
        ),
        pytest.param(
            "ssh://hg.mozilla.org/try",
            False,
            False,
            id="hg_remote_uses_lando",
        ),
        pytest.param(
            "ssh://hg.mozilla.org/try",
            True,
            True,
            id="push_to_vcs",
        ),
    ],
)
def test_push_to_try_routing(
    mock_push_to_lando_try,
    url,
    push_to_vcs,
    expect_direct_push,
):
    mock_vcs = MagicMock()
    mock_vcs.get_remote_url.return_value = url
    mock_vcs.branch = "feature-branch"

    mock_metrics = MagicMock()
    mock_metrics.mach_try.commit_prep.start = MagicMock()
    mock_metrics.mach_try.commit_prep.stop = MagicMock()

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.vcs", mock_vcs))
        stack.enter_context(patch("tryselect.push.MACH_TRY_REMOTE", url))
        mock_lando = stack.enter_context(mock_push_to_lando_try)
        stack.enter_context(patch("tryselect.push.check_working_directory"))
        stack.enter_context(
            patch(
                "tryselect.push.generate_try_task_config",
                return_value={"tasks": ["task1"]},
            )
        )
        stack.enter_context(
            patch("tryselect.push.push_to_git_backing", return_value="deadbeef")
        )
        stack.enter_context(patch("tryselect.push.write_task_config_history"))

        push._is_hg_try.cache_clear()

        push.push_to_try(
            "fuzzy",
            "try: test",
            mock_metrics,
            push_to_vcs=push_to_vcs,
            dry_run=False,
        )

        if expect_direct_push:
            mock_lando.assert_not_called()
            mock_vcs.push_to_try.assert_called_once()
        else:
            mock_lando.assert_called_once()
            mock_vcs.push_to_try.assert_not_called()


@pytest.mark.parametrize(
    "url,expected",
    [
        pytest.param("git@github.com:mozilla-firefox/firefox-dev.git", True, id="ssh"),
        pytest.param(
            "https://github.com/mozilla-firefox/firefox-dev", True, id="https"
        ),
        pytest.param(
            "https://github.com/Mozilla-Firefox/Firefox-Dev", True, id="mixed-case"
        ),
        pytest.param(
            "https://github.com/mozilla-firefox/firefox-dev-secure",
            False,
            id="secure",
        ),
        pytest.param("https://github.com/someone/firefox-dev", False, id="fork"),
    ],
)
def test_push_to_try_prints_treeherder_url(capsys, url, expected):
    mock_vcs = MagicMock()
    mock_vcs.get_remote_url.return_value = url
    mock_vcs.push_to_try.return_value = "abc123"

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.vcs", mock_vcs))
        stack.enter_context(patch("tryselect.push.MACH_TRY_REMOTE", "origin"))
        stack.enter_context(patch("tryselect.push.check_working_directory"))
        stack.enter_context(patch("tryselect.push.check_tree_status"))
        stack.enter_context(patch("tryselect.push.write_task_config_history"))
        push._is_hg_try.cache_clear()

        push.push_to_try("fuzzy", "try: test", MagicMock(), push_to_vcs=True)

    message = (
        "\nView push in Treeherder:\n"
        " https://treeherder.mozilla.org/jobs?repo=firefox-dev&revision=abc123\n"
    )
    out = capsys.readouterr().out
    assert out.endswith(message) if expected else "Treeherder" not in out


@pytest.fixture
def mock_tc_secret(monkeypatch):
    monkeypatch.setattr(push, "get_client", real_get_client)
    monkeypatch.setenv("MOZ_AUTOMATION", "1")
    monkeypatch.setenv("TASKCLUSTER_ROOT_URL", TC_ROOT_URL)
    monkeypatch.setenv("TASKCLUSTER_CLIENT_ID", "test-client")
    monkeypatch.setenv("TASKCLUSTER_ACCESS_TOKEN", "test-token")
    secret_url = f"{TC_ROOT_URL}/api/secrets/v1/secret/{urllib.parse.quote(push.GIT_BACKING_SECRET, '')}"
    with RequestsMock() as rsps:
        rsps.add(rsps.GET, secret_url, json={"secret": {"ssh_privkey": "fake-key\n"}})
        yield


def test_push_to_git_backing_returns_git_push_sha(
    tmp_path, monkeypatch, mock_tc_secret
):
    """push_to_git_backing pushes to git-backing with SSH and returns the git SHA."""
    git_repo = GitRepository(tmp_path)
    monkeypatch.setattr(push, "vcs", git_repo)

    def mock_run(*args, **kwargs):
        if args[0] == "rev-parse":
            return "gitsha456\n"
        return None

    with patch.object(git_repo, "_run", side_effect=mock_run), patch.object(
        git_repo, "push"
    ) as mock_push:
        result = push.push_to_git_backing("try")

    assert result == "gitsha456"
    mock_push.assert_called_once()
    env = mock_push.call_args.kwargs.get("env", {})
    assert "-o IdentitiesOnly=yes" in env.get("GIT_SSH_COMMAND", "")
    assert "-o StrictHostKeyChecking=accept-new" in env.get("GIT_SSH_COMMAND", "")


@pytest.mark.skipif(
    shutil.which("ssh-keygen") is None, reason="ssh-keygen not available"
)
def test_push_to_git_backing_key_usable(tmp_path, monkeypatch, mock_tc_secret):
    """git-backing ssh deploy key is readable, with permissions ssh will accept"""
    ssh_keygen = shutil.which("ssh-keygen")
    assert ssh_keygen

    if sys.platform == "win32":
        # The tempdir might already have a narrow default ACL and therefore never
        # reproduce the overly-open ACL the fix this is testing guards against. Point
        # the keyfile at a dedicated directory instead, with an inheritable broad grant
        # added ahead of time, so the test does something useful.
        fake_temp = tmp_path / "faketemp"
        fake_temp.mkdir()
        subprocess.run(
            ["icacls", str(fake_temp), "/grant", "Users:(OI)(CI)(RX)"],
            check=True,
            capture_output=True,
        )
        monkeypatch.setattr(tempfile, "tempdir", str(fake_temp))

    git_repo = GitRepository(tmp_path)
    monkeypatch.setattr(push, "vcs", git_repo)

    def mock_run(*args, **kwargs):
        if args[0] == "rev-parse":
            return "gitsha456\n"
        return None

    unexpected_failures = []

    def check_keyfile(*args, **kwargs):
        ssh_command = kwargs.get("env", {}).get("GIT_SSH_COMMAND", "")
        parts = shlex.split(ssh_command)
        keyfile_path = parts[parts.index("-i") + 1]

        proc = subprocess.run(
            [ssh_keygen, "-y", "-f", keyfile_path],
            capture_output=True,
            text=True,
            check=False,
        )
        stderr = proc.stderr.lower()
        # The only acceptable failure is one caused by our fake key's content
        # being garbage, i.e. ssh-keygen must have gotten as far as reading and
        # parsing the file. Different ssh-keygen builds word this differently.
        content_error_markers = (
            "invalid format",
            "error in libcrypto",
        )
        if proc.returncode != 0 and not any(
            marker in stderr for marker in content_error_markers
        ):
            unexpected_failures.append(proc.stderr)

        if sys.platform == "win32":
            # ssh's own strict permission check only fires once it actually
            # authenticates over a live connection, which needs a real
            # server. Inspect the ACL directly instead: the keyfile was
            # created under a directory with an inheritable Users grant, so
            # the fix must strip that inherited ACE (icacls marks inherited
            # entries with "(I)") or ssh would reject the key as too open.
            acl = subprocess.run(
                ["icacls", keyfile_path],
                capture_output=True,
                text=True,
                check=True,
            ).stdout
            if "(I)" in acl or "users:" in acl.lower():
                unexpected_failures.append(acl)

    with patch.object(git_repo, "_run", side_effect=mock_run), patch.object(
        git_repo, "push", side_effect=check_keyfile
    ):
        push.push_to_git_backing("try")

    assert not unexpected_failures, (
        f"ssh would reject the key file: {unexpected_failures}"
    )


def test_push_to_try_skips_git_backing_for_hg_repos():
    """push_to_try skips git-backing when the local vcs is hg."""
    url = "ssh://hg.mozilla.org/try"
    mock_metrics = MagicMock()
    mock_git_backing = MagicMock()

    push.vcs.name = "hg"
    push.vcs.get_remote_url.return_value = url

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.MACH_TRY_REMOTE", url))
        stack.enter_context(patch("tryselect.push.GIT_BACKING_ENABLED", True))
        stack.enter_context(patch("tryselect.push.check_working_directory"))
        stack.enter_context(patch("tryselect.push.write_task_config_history"))
        stack.enter_context(
            patch("tryselect.push.push_to_git_backing", mock_git_backing)
        )
        push._is_hg_try.cache_clear()

        push.push_to_try(
            "fuzzy",
            "try: test",
            mock_metrics,
            try_task_config={
                "version": 2,
                "parameters": {"try_task_config": {"tasks": ["task1"]}},
            },
            push_to_vcs=True,
            dry_run=False,
        )

    mock_git_backing.assert_not_called()


def test_push_to_try_injects_git_backing_params():
    """push_to_try injects head_git_repository and head_git_rev into try_task_config."""
    url = "ssh://hg.mozilla.org/try"
    mock_metrics = MagicMock()

    push.vcs.get_remote_url.return_value = url

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.MACH_TRY_REMOTE", url))
        stack.enter_context(patch("tryselect.push.GIT_BACKING_ENABLED", True))
        stack.enter_context(patch("tryselect.push.check_working_directory"))
        stack.enter_context(patch("tryselect.push.write_task_config_history"))
        stack.enter_context(
            patch("tryselect.push.push_to_git_backing", return_value="deadbeef123")
        )
        push._is_hg_try.cache_clear()

        push.push_to_try(
            "fuzzy",
            "try: test",
            mock_metrics,
            try_task_config={
                "version": 2,
                "parameters": {"try_task_config": {"tasks": ["task1"]}},
            },
            push_to_vcs=True,
            dry_run=False,
        )

    call_kwargs = push.vcs.push_to_try.call_args.kwargs
    config = json.loads(call_kwargs["changed_files"]["try_task_config.json"])
    assert config["parameters"]["head_git_repository"] == push.GIT_BACKING_REPO
    assert config["parameters"]["head_git_rev"] == "deadbeef123"


def test_push_to_try_skips_git_backing_when_disabled():
    """When GIT_BACKING_ENABLED is False, push_to_git_backing is not called and
    head_git_repository/head_git_rev are not injected."""
    url = "ssh://hg.mozilla.org/try"
    mock_metrics = MagicMock()
    mock_git_backing = MagicMock()

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.MACH_TRY_REMOTE", url))
        stack.enter_context(patch("tryselect.push.GIT_BACKING_ENABLED", False))
        stack.enter_context(patch("tryselect.push.check_working_directory"))
        stack.enter_context(patch("tryselect.push.write_task_config_history"))
        stack.enter_context(
            patch("tryselect.push.push_to_git_backing", mock_git_backing)
        )
        push._is_hg_try.cache_clear()

        push.push_to_try(
            "fuzzy",
            "try: test",
            mock_metrics,
            try_task_config={
                "version": 2,
                "parameters": {"try_task_config": {"tasks": ["task1"]}},
            },
            push_to_vcs=True,
            dry_run=False,
        )

    mock_git_backing.assert_not_called()
    call_kwargs = push.vcs.push_to_try.call_args.kwargs
    config = json.loads(call_kwargs["changed_files"]["try_task_config.json"])
    assert "head_git_repository" not in config.get("parameters", {})
    assert "head_git_rev" not in config.get("parameters", {})


@pytest.mark.parametrize(
    "status,body,closed",
    (
        pytest.param(200, {"result": {"status": "open"}}, False, id="open"),
        pytest.param(200, {"result": {"status": "closed"}}, True, id="closed"),
        pytest.param(404, {"detail": "No tree try found."}, False, id="unknown"),
    ),
)
def test_check_tree_status(responses, status, body, closed):
    responses.add(
        responses.GET,
        push.TREESTATUS_URL.format(domain="lando.moz.tools", tree="try"),
        json=body,
        status=status,
    )
    push.vcs.get_remote_url.return_value = None
    if closed:
        with pytest.raises(SystemExit):
            real_check_tree_status("ssh://hg.mozilla.org/try")
    else:
        real_check_tree_status("ssh://hg.mozilla.org/try")


@pytest.mark.parametrize(
    "push_to_vcs,closed_tree,suffix",
    (
        pytest.param(True, False, False, id="open"),
        pytest.param(True, True, True, id="hg_closed"),
        pytest.param(False, False, False, id="lando"),
        pytest.param(False, True, False, id="lando_closed"),
    ),
)
def test_push_to_try_closed_tree(
    mock_push_to_lando_try, push_to_vcs, closed_tree, suffix
):
    url = "ssh://hg.mozilla.org/try"
    push.vcs.get_remote_url.return_value = url

    with ExitStack() as stack:
        stack.enter_context(patch("tryselect.push.MACH_TRY_REMOTE", url))
        stack.enter_context(patch("tryselect.push.check_working_directory"))
        lando = stack.enter_context(mock_push_to_lando_try)
        push._is_hg_try.cache_clear()

        push.push_to_try(
            "fuzzy",
            "try: test",
            MagicMock(),
            closed_tree=closed_tree,
            push_to_vcs=push_to_vcs,
        )

    assert push.check_tree_status.called == (push_to_vcs and not closed_tree)
    if push_to_vcs:
        commit_message = push.vcs.push_to_try.call_args.args[0]
    else:
        commit_message = lando.call_args.args[1]
    assert ("ON A CLOSED TREE" in commit_message) == suffix


if __name__ == "__main__":
    mozunit.main()
