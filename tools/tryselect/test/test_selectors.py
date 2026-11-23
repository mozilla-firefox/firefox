import os
from copy import deepcopy
from textwrap import dedent

import mozunit
import pytest
from mach.util import get_state_dir
from taskgraph.taskgraph import TaskGraph
from tryselect.tasks import add_chunk_patterns


@pytest.fixture(scope="module")
def monkeymodule():
    with pytest.MonkeyPatch.context() as mp:
        yield mp


@pytest.fixture(scope="module", autouse=True)
def setup_state_dir(monkeymodule, tmp_path_factory, run_mach):
    state_dir = tmp_path_factory.mktemp("mozbuild")
    machrc = state_dir / "machrc"
    machrc.write_text(
        dedent(
            """
          [try]
          default=fuzzy
          """
        ).lstrip()
    )
    monkeymodule.setenv("EDITOR", "cat")
    monkeymodule.setenv("MOZBUILD_STATE_PATH", str(state_dir))
    monkeymodule.setenv("MACH_TRY_PRESET_PATHS", str(state_dir / "try_presets.yml"))
    monkeymodule.setenv("MACHRC", machrc)

    get_state_dir(specific_to_topsrcdir=True)

    try:
        run_mach(["try", "--help"])  # create the virtualenv
    except SystemExit:
        pass


@pytest.fixture
def target_task_set():
    return {
        "test/foo-opt": {
            "kind": "test",
            "label": "test/foo-opt",
            "attributes": {},
            "task": {},
            "optimization": {},
            "dependencies": {},
        },
        "test/foo-debug": {
            "kind": "test",
            "label": "test/foo-debug",
            "attributes": {},
            "task": {},
            "optimization": {},
            "dependencies": {},
        },
        "build-baz": {
            "kind": "build",
            "label": "build-baz",
            "attributes": {},
            "task": {},
            "optimization": {},
            "dependencies": {},
        },
    }


@pytest.fixture
def full_task_set(target_task_set):
    full_task_set = deepcopy(target_task_set)
    full_task_set.update({
        "test/bar-opt": {
            "kind": "test",
            "label": "test/bar-opt",
            "attributes": {},
            "task": {},
            "optimization": {},
            "dependencies": {},
        },
        "test/bar-debug": {
            "kind": "test",
            "label": "test/bar-debug",
            "attributes": {},
            "task": {},
            "optimization": {},
            "dependencies": {},
        },
    })
    return full_task_set


@pytest.mark.skipif(os.name == "nt", reason="site validation fails in CI")
@pytest.mark.parametrize(
    "selector,commands,expected",
    (
        pytest.param(
            "fuzzy",
            ["try", "fuzzy", "-q", "'foo"],
            dedent(
                """
              Commit message:
              Fuzzy query='foo

              mach try command: `./mach try fuzzy -q 'foo`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "tasks": [
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="fuzzy",
        ),
        pytest.param(
            "fuzzy",
            ["try", "fuzzy", "-q", "'bar"],
            "no tasks selected\n",
            id="fuzzy",
        ),
        pytest.param(
            "fuzzy",
            ["try", "fuzzy", "--full", "-q", "'bar"],
            dedent(
                """
              Commit message:
              Fuzzy query='bar

              mach try command: `./mach try fuzzy --full -q 'bar`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "tasks": [
                              "test/bar-debug",
                              "test/bar-opt"
                          ]
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="fuzzy",
        ),
        pytest.param(
            "fuzzy",
            ["try", "fuzzy", "--full", "-q", "'foo", "-q", "'bar"],
            dedent(
                """
              Commit message:
              Fuzzy query='foo&query='bar

              mach try command: `./mach try fuzzy --full -q 'foo -q 'bar`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "tasks": [
                              "test/bar-debug",
                              "test/bar-opt",
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="fuzzy multiple selectors",
        ),
        pytest.param(
            "fuzzy",
            ["try", "fuzzy", "--and", "-q", "'foo", "-q", "'opt"],
            dedent(
                """
              Commit message:
              Fuzzy query='foo&query='opt

              mach try command: `./mach try fuzzy --and -q 'foo -q 'opt`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "tasks": [
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="fuzzy query intersection",
        ),
        pytest.param(
            "fuzzy",
            [
                ["try", "fuzzy", "--save", "foo", "-q", "'test", "-q", "'opt"],
                ["try", "fuzzy", "--preset", "foo", "-xq", "'test"],
            ],
            dedent(
                """
              preset saved, run with: --preset=foo
              Commit message:
              Fuzzy (preset: foo) query='test&query='opt&query='test

              mach try command: `./mach try fuzzy --preset foo -xq 'test`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "tasks": [
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="fuzzy intersection with preset containing multiple queries",
        ),
        pytest.param(
            "fuzzy",
            ["try", "fuzzy", "--full", "-q", "testfoo | 'testbar"],
            dedent(
                """
              Commit message:
              Fuzzy query=testfoo | 'testbar

              mach try command: `./mach try fuzzy --full -q testfoo | 'testbar`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "tasks": [
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="fuzzy exact match",
        ),
        pytest.param(
            "fuzzy",
            ["try", "fuzzy", "--full", "--exact", "-q", "testfoo | 'testbar"],
            dedent(
                """
              Commit message:
              Fuzzy query=testfoo | 'testbar

              mach try command: `./mach try fuzzy --full --exact -q testfoo | 'testbar`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "tasks": [
                              "test/bar-debug",
                              "test/bar-opt"
                          ]
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="fuzzy exact match",
        ),
        pytest.param(
            "fuzzy",
            ["try", "fuzzy", "--env", "FOO=1", "--env", "BAR=baz", "-q", "'foo"],
            dedent(
                """
              Commit message:
              Fuzzy query='foo

              mach try command: `./mach try fuzzy --env FOO=1 --env BAR=baz -q 'foo`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "BAR": "baz",
                              "FOO": "1",
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "tasks": [
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="fuzzy task config",
        ),
        pytest.param(
            "auto",
            ["try", "auto"],
            dedent(
                """
              Commit message:
              Tasks automatically selected.

              mach try command: `./mach try auto`

              Pushed via `mach try auto`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "filters": [
                          "try_auto"
                      ],
                      "optimize_strategies": "gecko_taskgraph.optimize:tryselect.bugbug_reduced_manifests_config_selection_medium",
                      "optimize_target_tasks": true,
                      "test_manifest_loader": "bugbug",
                      "try_mode": "try_auto",
                      "try_task_config": {}
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="auto selector",
        ),
        pytest.param(
            "auto",
            ["try", "auto", "--closed-tree"],
            dedent(
                """
              Commit message:
              Tasks automatically selected. ON A CLOSED TREE

              mach try command: `./mach try auto --closed-tree`

              Pushed via `mach try auto`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "filters": [
                          "try_auto"
                      ],
                      "optimize_strategies": "gecko_taskgraph.optimize:tryselect.bugbug_reduced_manifests_config_selection_medium",
                      "optimize_target_tasks": true,
                      "test_manifest_loader": "bugbug",
                      "try_mode": "try_auto",
                      "try_task_config": {}
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="auto",
        ),
        pytest.param(
            "auto",
            ["try", "auto", "--closed-tree", "-m", "foo {msg} bar"],
            dedent(
                """
              Commit message:
              foo Tasks automatically selected. bar ON A CLOSED TREE

              mach try command: `./mach try auto --closed-tree -m foo {msg} bar`

              Pushed via `mach try auto`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "filters": [
                          "try_auto"
                      ],
                      "optimize_strategies": "gecko_taskgraph.optimize:tryselect.bugbug_reduced_manifests_config_selection_medium",
                      "optimize_target_tasks": true,
                      "test_manifest_loader": "bugbug",
                      "try_mode": "try_auto",
                      "try_task_config": {}
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="auto",
        ),
        pytest.param(
            "empty",
            ["try", "empty"],
            dedent(
                """
              Commit message:
              No try selector specified, use "Add New Jobs" to select tasks.

              mach try command: `./mach try empty`

              Pushed via `mach try empty`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "empty"
                          },
                          "tasks": []
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="empty",
        ),
        pytest.param(
            "empty",
            ["try", "empty", "--closed-tree"],
            dedent(
                """
              Commit message:
              No try selector specified, use "Add New Jobs" to select tasks. ON A CLOSED TREE

              mach try command: `./mach try empty --closed-tree`

              Pushed via `mach try empty`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "empty"
                          },
                          "tasks": []
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="empty",
        ),
        pytest.param(
            "empty",
            ["try", "empty", "--closed-tree", "-m", "foo {msg} bar"],
            dedent(
                """
              Commit message:
              foo No try selector specified, use "Add New Jobs" to select tasks. bar ON A CLOSED TREE

              mach try command: `./mach try empty --closed-tree -m foo {msg} bar`

              Pushed via `mach try empty`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "empty"
                          },
                          "tasks": []
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="empty",
        ),
        pytest.param(
            "fuzzy",
            ["try", "fuzzy", "-q", "foo", "--message", "Foobar"],
            dedent(
                """
              Commit message:
              Foobar

              Fuzzy query=foo

              mach try command: `./mach try fuzzy -q foo --message Foobar`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "tasks": [
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="message fuzzy",
        ),
        pytest.param(
            "fuzzy",
            ["try", "fuzzy", "-q", "foo", "-m", "Foobar: {msg}"],
            dedent(
                """
              Commit message:
              Foobar: Fuzzy query=foo

              mach try command: `./mach try fuzzy -q foo -m Foobar: {msg}`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "tasks": [
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

            """
            ).lstrip(),
            id="message fuzzy",
        ),
        pytest.param(
            "fuzzy",
            [
                ["try", "fuzzy", "--save", "foo", "-q", "'foo", "--rebuild", "5"],
                ["try", "fuzzy", "--preset", "foo"],
                ["try", "--preset", "foo"],
                ["try", "--edit-presets"],
            ],
            dedent(
                """
              preset saved, run with: --preset=foo
              Commit message:
              Fuzzy (preset: foo) query='foo

              mach try command: `./mach try fuzzy --preset foo`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "rebuild": 5,
                          "tasks": [
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

              Commit message:
              Fuzzy (preset: foo) query='foo

              mach try command: `./mach try --preset foo`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "rebuild": 5,
                          "tasks": [
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

              foo:
                dry_run: true
                no_artifact: true
                query:
                - "'foo"
                rebuild: 5
                selector: fuzzy
                """
            ).lstrip(),
            id="preset with fuzzy subcommand",
        ),
        pytest.param(
            "fuzzy",
            [
                ["try", "fuzzy", "--save", "foo", "-q", "'foo", "--rebuild", "5"],
                ["try", "fuzzy", "--preset", "foo", "-q'build"],
                ["try", "fuzzy", "--preset", "foo", "-xq'opt"],
            ],
            dedent(
                """
              preset saved, run with: --preset=foo
              Commit message:
              Fuzzy (preset: foo) query='foo&query='build

              mach try command: `./mach try fuzzy --preset foo -q'build`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "rebuild": 5,
                          "tasks": [
                              "build-baz",
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

              Commit message:
              Fuzzy (preset: foo) query='foo&query='opt

              mach try command: `./mach try fuzzy --preset foo -xq'opt`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "rebuild": 5,
                          "tasks": [
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

                """
            ).lstrip(),
            id="preset queries can be appended",
        ),
        pytest.param(
            "fuzzy",
            [
                ["try", "fuzzy", "--save", "foo", "-q", "'foo", "--rebuild", "5"],
                [
                    "try",
                    "fuzzy",
                    "--preset",
                    "foo",
                    "--gecko-profile-features=nostacksampling,cpu",
                ],
            ],
            dedent(
                """
              preset saved, run with: --preset=foo
              Commit message:
              Fuzzy (preset: foo) query='foo

              mach try command: `./mach try fuzzy --preset foo --gecko-profile-features=nostacksampling,cpu`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "gecko-profile": true,
                          "gecko-profile-features": "nostacksampling,cpu",
                          "rebuild": 5,
                          "tasks": [
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

              """
            ).lstrip(),
            id="gecko-profile handling",
        ),
        pytest.param(
            "fuzzy",
            [
                [
                    "try",
                    "fuzzy",
                    "--save",
                    "foo",
                    "-q",
                    "'foo",
                    "--rebuild",
                    "5",
                    "--gecko-profile-features=nostacksampling,cpu",
                ],
                ["try", "fuzzy", "--preset", "foo"],
                ["try", "fuzzy", "--edit-presets", "foo"],
            ],
            dedent(
                """
              preset saved, run with: --preset=foo
              Commit message:
              Fuzzy (preset: foo) query='foo

              mach try command: `./mach try fuzzy --preset foo`

              Pushed via `mach try fuzzy`
              Calculated try_task_config.json:
              {
                  "parameters": {
                      "optimize_target_tasks": false,
                      "try_task_config": {
                          "env": {
                              "TRY_SELECTOR": "fuzzy"
                          },
                          "gecko-profile": true,
                          "gecko-profile-features": "nostacksampling,cpu",
                          "rebuild": 5,
                          "tasks": [
                              "test/foo-debug",
                              "test/foo-opt"
                          ]
                      }
                  },
                  "version": 2
              }

              foo:
                dry_run: true
                gecko_profile_features: nostacksampling,cpu
                no_artifact: true
                query:
                - "'foo"
                rebuild: 5
                selector: fuzzy
              """
            ).lstrip(),
            id="gecko-profile handling",
        ),
    ),
)
def test_run_mach(
    capfd,
    mocker,
    run_mach,
    target_task_set,
    full_task_set,
    selector,
    commands,
    expected,
):
    """These tests were initially converted from the `cramtest` framework. It's
    likely there is duplication of test coverage between here and the specific
    selector tests."""
    capfd.readouterr()
    mocker.patch("tryselect.push._is_hg_try", return_value=True)
    mocker.patch("tryselect.push.MACH_TRY_PUSH_TO_VCS", True)
    if isinstance(commands[0], str):
        commands = [commands]

    for cmd in commands:
        m = mocker.patch("tryselect.push.get_sys_argv")
        m.return_value = f"./mach {' '.join(cmd)}"

        cmd.append("--no-push")

        if selector in cmd:
            cmd.append("--no-artifact")

            if selector == "fuzzy":

                def fake_generate_tasks(*args, **kwargs):
                    task_set = target_task_set
                    if "--full" in cmd:
                        task_set = full_task_set
                    return add_chunk_patterns(TaskGraph.from_json(task_set)[1])

                m = mocker.patch(f"tryselect.selectors.{selector}.generate_tasks")
                m.side_effect = fake_generate_tasks

        try:
            cmd.insert(0, f"--settings={os.environ['MACHRC']}")
            run_mach(cmd)
        except SystemExit:
            pass

    out, _ = capfd.readouterr()
    assert out == expected


if __name__ == "__main__":
    mozunit.main()
