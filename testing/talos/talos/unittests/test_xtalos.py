import json
import os
import re

import mozunit

from talos.xtalos import xtalos
from talos.xtalos.etlparser import NAME_SUBSTITUTIONS


def test_NAME_SUBSTITUTIONS():
    filepaths_map = {
        # tp5n files
        r"{talos}\talos\tests\tp5n\alibaba.com\i03.i.aliimg.com\images\eng\style\css_images": r"{talos}\talos\tests\{tp5n_files}",  # NOQA: E501
        r"{talos}\talos\tests\tp5n\cnet.com\i.i.com.com\cnwk.1d\i\tron\fd": r"{talos}\talos\tests\{tp5n_files}",  # NOQA: E501
        r"{talos}\talos\tests\tp5n\tp5n.manifest": r"{talos}\talos\tests\{tp5n_files}",
        r"{talos}\talos\tests\tp5n\tp5n.manifest.develop": r"{talos}\talos\tests\{tp5n_files}",
        r"{talos}\talos\tests\tp5n\yelp.com\media1.ct.yelpcdn.com\photo": r"{talos}\talos\tests\{tp5n_files}",  # NOQA: E501
        # cltbld for Windows 7 32bit
        r"c:\users\cltbld.t-w732-ix-015.000\appdata\locallow\mozilla": r"c:\users\{cltbld}\appdata\locallow\mozilla",  # NOQA: E501
        r"c:\users\cltbld.t-w732-ix-035.000\appdata\locallow\mozilla": r"c:\users\{cltbld}\appdata\locallow\mozilla",  # NOQA: E501
        r"c:\users\cltbld.t-w732-ix-058.000\appdata\locallow\mozilla": r"c:\users\{cltbld}\appdata\locallow\mozilla",  # NOQA: E501
        r"c:\users\cltbld.t-w732-ix-112.001\appdata\local\temp": r"c:\users\{cltbld}\appdata\local\temp",  # NOQA: E501
        # nvidia's 3D Vision
        r"c:\program files\nvidia corporation\3d vision\npnv3dv.dll": r"c:\program files\{nvidia_3d_vision}",  # NOQA: E501
        r"c:\program files\nvidia corporation\3d vision\npnv3dvstreaming.dll": r"c:\program files\{nvidia_3d_vision}",  # NOQA: E501
        r"c:\program files\nvidia corporation\3d vision\nvstereoapii.dll": r"c:\program files\{nvidia_3d_vision}",  # NOQA: E501
        r"{firefox}\browser\extensions\{45b6d270-f6ec-4930-a6ad-14bac5ea2204}.xpi": r"{firefox}\browser\extensions\{uuid}.xpi",  # NOQA: E501
        r"c:\slave\test\build\venv\lib\site-packages\pip\_vendor\html5lib\treebuilders": r"c:\slave\test\build\venv\lib\site-packages\{pip_vendor}",  # NOQA: E501
        r"c:\slave\test\build\venv\lib\site-packages\pip\_vendor\colorama": r"c:\slave\test\build\venv\lib\site-packages\{pip_vendor}",  # NOQA: E501
        r"c:\slave\test\build\venv\lib\site-packages\pip\_vendor\cachecontrol\caches": r"c:\slave\test\build\venv\lib\site-packages\{pip_vendor}",  # NOQA: E501
        r"c:\slave\test\build\venv\lib\site-packages\pip\_vendor\requests\packages\urllib3\packages\ssl_match_hostname": r"c:\slave\test\build\venv\lib\site-packages\{pip_vendor}",  # NOQA: E501
    }

    for given_raw_path, exp_normal_path in filepaths_map.items():
        normal_path = given_raw_path
        for pattern, substitution in NAME_SUBSTITUTIONS:
            normal_path = re.sub(pattern, substitution, normal_path)
        assert exp_normal_path == normal_path


def test_options_from_config_overrides_known_options_only(tmp_path):
    config = tmp_path / "config.json"
    config.write_text(json.dumps({"a": 10, "extra": "ignored"}))
    options = {"a": 1, "b": 2}
    result = xtalos.options_from_config(options, str(config))
    assert result is options
    assert options == {"a": 10, "b": 2}


def test_xtalos_options_defaults():
    args = xtalos.XtalosOptions().parse_args([])
    assert vars(args) == {
        "processID": None,
        "xperf_path": "xperf.exe",
        "etl_filename": "test.etl",
        "debug_level": 1,
        "outputFile": "etl_output.csv",
        "xperf_providers": [],
        "xperf_user_providers": [],
        "xperf_stackwalk": [],
        "configFile": None,
        "allowlist_file": None,
        "all_stages": False,
        "all_threads": False,
        "approot": None,
        "error_filename": None,
    }


def test_xtalos_options_parse_reads_all_flags():
    args = xtalos.XtalosOptions().parse_args([
        "--pid",
        "12",
        "-r",
        "A",
        "-r",
        "B",
        "--user-providers",
        "U",
        "-s",
        "S",
        "-w",
        "allow.txt",
        "-a",
        "app",
        "--error-filename",
        "err.txt",
        "-i",
        "-t",
    ])
    assert args.processID == "12"
    assert args.xperf_providers == ["A", "B"]
    assert args.xperf_user_providers == ["U"]
    assert args.xperf_stackwalk == ["S"]
    assert args.allowlist_file == "allow.txt"
    assert args.approot == "app"
    assert args.error_filename == "err.txt"
    assert args.all_stages and args.all_threads


def test_verify_options_resolves_xperf_path_and_rejects_missing_file(tmp_path, capsys):
    xperf = tmp_path / "xperf.exe"
    xperf.write_text("")
    config = tmp_path / "config.json"
    config.write_text(json.dumps({"xperf_path": str(xperf), "processID": 7}))
    parser = xtalos.XtalosOptions()

    options = parser.verifyOptions(parser.parse_args(["-c", str(config)]))
    assert options.xperf_path == os.path.abspath(str(xperf))
    assert options.processID == 7

    missing = str(tmp_path / "missing.exe")
    assert parser.verifyOptions(parser.parse_args(["-x", missing])) is None
    assert "does not exist" in capsys.readouterr().out


if __name__ == "__main__":
    mozunit.main()
