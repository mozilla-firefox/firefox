# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import platform
import sys

# OS Specifics
ABS_WORK_DIR = os.path.join(os.getcwd(), "build")
BINARY_PATH = os.path.join(ABS_WORK_DIR, "firefox", "firefox.exe")
INSTALLER_PATH = os.path.join(ABS_WORK_DIR, "installer.zip")
NODEJS_PATH = None
if "MOZ_FETCHES_DIR" in os.environ:
    NODEJS_PATH = os.path.join(os.environ["MOZ_FETCHES_DIR"], "node/node.exe")

REQUIRE_GPU = False
if "REQUIRE_GPU" in os.environ:
    REQUIRE_GPU = os.environ["REQUIRE_GPU"] == "1"

USE_HARDWARE = False
if "USE_HARDWARE" in os.environ:
    USE_HARDWARE = os.environ["USE_HARDWARE"] == "1"

PYWIN32 = "pywin32==306"

XPCSHELL_NAME = "xpcshell.exe"
EXE_SUFFIX = ".exe"
DISABLE_SCREEN_SAVER = False
ADJUST_MOUSE_AND_SCREEN = True
DESKTOP_VISUALFX_THEME = {
    "Let Windows choose": 0,
    "Best appearance": 1,
    "Best performance": 2,
    "Custom": 3,
}.get("Best appearance")
TASKBAR_AUTOHIDE_REG_PATH = {
    "Windows 7": r"HKCU:SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\StuckRects2",
    "Windows 10": r"HKCU:SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\StuckRects3",
}.get(f"{platform.system()} {platform.release()}")
#####
config = {
    "exes": {
        "python": sys.executable,
        "hg": os.path.join(os.environ.get("PROGRAMFILES", ""), "Mercurial", "hg"),
    },
    ###
    "installer_path": INSTALLER_PATH,
    "binary_path": BINARY_PATH,
    "xpcshell_name": XPCSHELL_NAME,
    "virtualenv_modules": [PYWIN32, "six==1.16.0", "vcversioner==2.16.0.0"],
    "virtualenv_path": "venv",
    "exe_suffix": EXE_SUFFIX,
    "run_file_names": {
        "mochitest": "runtests.py",
        "reftest": "runreftest.py",
        "xpcshell": "runxpcshelltests.py",
        "cppunittest": "runcppunittests.py",
        "gtest": "rungtests.py",
        "jittest": "jit_test.py",
    },
    "minimum_tests_zip_dirs": [
        "bin/*",
        "certs/*",
        "config/*",
        "mach",
        "marionette/*",
        "modules/*",
        "mozbase/*",
        "tools/*",
        "mozpack/*",
        "mozbuild/*",
    ],
    "suite_definitions": {
        "cppunittest": {
            "options": [
                "--symbols-path=%(symbols_path)s",
                "--utility-path=tests/bin",
                "--xre-path=%(abs_app_dir)s",
            ],
            "run_filename": "runcppunittests.py",
            "testsdir": "cppunittest",
        },
        "jittest": {
            "options": [
                "tests/bin/js",
                "--no-slow",
                "--no-progress",
                "--format=automation",
                "--jitflags=all",
                "--timeout=970",  # Keep in sync with run_timeout below.
            ],
            "run_filename": "jit_test.py",
            "testsdir": "jit-test/jit-test",
            "run_timeout": 1000,  # Keep in sync with --timeout above.
        },
        "mochitest": {
            "options": [
                "--appname=%(binary_path)s",
                "--utility-path=tests/bin",
                "--extra-profile-file=tests/bin/plugins",
                "--symbols-path=%(symbols_path)s",
                "--certificate-path=tests/certs",
                "--quiet",
                "--log-errorsummary=%(error_summary_file)s",
                "--screenshot-on-fail",
                "--cleanup-crashes",
                "--marionette-startup-timeout=180",
            ],
            "run_filename": "runtests.py",
            "testsdir": "mochitest",
        },
        "reftest": {
            "options": [
                "--appname=%(binary_path)s",
                "--utility-path=tests/bin",
                "--extra-profile-file=tests/bin/plugins",
                "--symbols-path=%(symbols_path)s",
                "--log-errorsummary=%(error_summary_file)s",
                "--cleanup-crashes",
                "--marionette-startup-timeout=180",
                "--sandbox-read-whitelist=%(abs_work_dir)s",
            ],
            "run_filename": "runreftest.py",
            "testsdir": "reftest",
        },
        "xpcshell": {
            "options": [
                "--self-test",
                "--symbols-path=%(symbols_path)s",
                "--log-errorsummary=%(error_summary_file)s",
                "--utility-path=tests/bin",
                "--manifest=tests/xpcshell/tests/xpcshell.toml",
            ],
            "run_filename": "runxpcshelltests.py",
            "testsdir": "xpcshell",
        },
        "gtest": {
            "options": [
                "--xre-path=%(abs_res_dir)s",
                "--cwd=%(gtest_dir)s",
                "--symbols-path=%(symbols_path)s",
                "--utility-path=tests/bin",
                "%(binary_path)s",
            ],
            "run_filename": "rungtests.py",
        },
    },
    # local mochi suites
    "all_mochitest_suites": {
        "mochitest-plain": [],
        "mochitest-plain-gpu": ["--subsuite=gpu"],
        "mochitest-media": ["--subsuite=media"],
        "mochitest-chrome": ["--flavor=chrome", "--disable-e10s"],
        "mochitest-chrome-gpu": ["--flavor=chrome", "--subsuite=gpu", "--disable-e10s"],
        "mochitest-browser-chrome": ["--flavor=browser"],
        "mochitest-browser-screenshots": [
            "--flavor=browser",
            "--subsuite=screenshots",
        ],
        "mochitest-webgl1-core": ["--subsuite=webgl1-core"],
        "mochitest-webgl1-ext": ["--subsuite=webgl1-ext"],
        "mochitest-webgl2-core": ["--subsuite=webgl2-core"],
        "mochitest-webgl2-ext": ["--subsuite=webgl2-ext"],
        "mochitest-webgl2-deqp": ["--subsuite=webgl2-deqp"],
        "mochitest-webgpu": ["--subsuite=webgpu"],
        "mochitest-devtools-chrome": [
            "--flavor=browser",
            "--subsuite=devtools",
        ],
        "mochitest-browser-a11y": ["--flavor=browser", "--subsuite=a11y"],
        "mochitest-browser-media": ["--flavor=browser", "--subsuite=media-bc"],
        "mochitest-browser-translations": [
            "--flavor=browser",
            "--subsuite=translations",
        ],
        "mochitest-a11y": ["--flavor=a11y", "--disable-e10s"],
        "mochitest-remote": ["--flavor=browser", "--subsuite=remote"],
    },
    # local reftest suites
    "all_reftest_suites": {
        "crashtest": {
            "options": ["--suite=crashtest", "--topsrcdir=tests/reftest/tests"],
            "tests": ["tests/reftest/tests/testing/crashtest/crashtests.list"],
        },
        "jsreftest": {
            "options": [
                "--extra-profile-file=tests/jsreftest/tests/js/src/tests/user.js",
                "--suite=jstestbrowser",
                "--topsrcdir=tests/jsreftest/tests",
            ],
            "tests": ["tests/jsreftest/tests/js/src/tests/jstests.list"],
        },
        "reftest": {
            "options": ["--suite=reftest", "--topsrcdir=tests/reftest/tests"],
            "tests": ["tests/reftest/tests/layout/reftests/reftest.list"],
        },
    },
    "all_xpcshell_suites": {
        "xpcshell": {
            "options": [
                "--xpcshell=%(abs_app_dir)s/" + XPCSHELL_NAME,
                "--msix-app-binary=%(binary_path)s",
                "--msix-app-path=%(install_dir)s",
                "--msix-xre-path=%(install_dir)s",
            ],
            "tests": [],
        },
    },
    "all_cppunittest_suites": {"cppunittest": ["tests/cppunittest"]},
    "all_gtest_suites": {"gtest": []},
    "all_jittest_suites": {
        "jittest": [],
        "jittest-chunked": [],
    },
    "run_cmd_checks_enabled": True,
    "preflight_run_cmd_suites": [
        {
            "name": "disable_screen_saver",
            "cmd": ["xset", "s", "off", "s", "reset"],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": False,
            "enabled": DISABLE_SCREEN_SAVER,
        },
        {
            "name": "run mouse & screen adjustment script",
            "cmd": [
                sys.executable,
                os.path.join(
                    os.getcwd(),
                    "mozharness",
                    "external_tools",
                    "mouse_and_screen_resolution.py",
                ),
                "--configuration-file",
                os.path.join(
                    os.getcwd(),
                    "mozharness",
                    "external_tools",
                    "machine-configuration.json",
                ),
                (
                    "--platform=win10-vm"
                    if REQUIRE_GPU and (platform.uname().version == "10.0.19045")
                    else (
                        "--platform=win11-hw"
                        if REQUIRE_GPU
                        and platform.uname().version in ("10.0.26100", "10.0.26200")
                        else "--platform=win7"
                    )
                ),
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": ADJUST_MOUSE_AND_SCREEN,
        },
        {
            "name": "enable microphone access for msix",
            "cmd": [
                "powershell",
                "-command",
                r'New-Item -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\ -Name "Mozilla.MozillaFirefoxNightly_5x4grbbqzn2q4" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "enable microphone access for msix, add allow key",
            "cmd": [
                "powershell",
                "-command",
                r'New-ItemProperty -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\Mozilla.MozillaFirefoxNightly_5x4grbbqzn2q4 -Name "Value" -Value "Allow" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "enable microphone access for msix (central)",
            "cmd": [
                "powershell",
                "-command",
                r'New-Item -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\ -Name "Mozilla.MozillaFirefoxNightly_jag0gd4e3s9p2" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "enable microphone access for msix, add allow key (central)",
            "cmd": [
                "powershell",
                "-command",
                r'New-ItemProperty -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\Mozilla.MozillaFirefoxNightly_jag0gd4e3s9p2 -Name "Value" -Value "Allow" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "enable microphone access for msix (beta)",
            "cmd": [
                "powershell",
                "-command",
                r'New-Item -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\ -Name "Mozilla.MozillaFirefoxBeta_5x4grbbqzn2q4" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "enable microphone access for msix, add allow key (beta)",
            "cmd": [
                "powershell",
                "-command",
                r'New-ItemProperty -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\Mozilla.MozillaFirefoxBeta_5x4grbbqzn2q4 -Name "Value" -Value "Allow" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "enable microphone access for msix (beta)",
            "cmd": [
                "powershell",
                "-command",
                r'New-Item -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\ -Name "Mozilla.MozillaFirefoxBeta_jag0gd4e3s9p2" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "enable microphone access for msix, add allow key (beta)",
            "cmd": [
                "powershell",
                "-command",
                r'New-ItemProperty -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\Mozilla.MozillaFirefoxBeta_jag0gd4e3s9p2 -Name "Value" -Value "Allow" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "enable microphone access for msix (release)",
            "cmd": [
                "powershell",
                "-command",
                r'New-Item -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\ -Name "Mozilla.MozillaFirefox_jag0gd4e3s9p2" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "enable microphone access for msix, add allow key (release)",
            "cmd": [
                "powershell",
                "-command",
                r'New-ItemProperty -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\Mozilla.MozillaFirefox_jag0gd4e3s9p2 -Name "Value" -Value "Allow" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "enable microphone access for msix (esr)",
            "cmd": [
                "powershell",
                "-command",
                r'New-Item -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\ -Name "Mozilla.MozillaFirefox_5x4grbbqzn2q4" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "enable microphone access for msix, add allow key (esr)",
            "cmd": [
                "powershell",
                "-command",
                r'New-ItemProperty -Path HKCU:\Software\Microsoft\Windows\CurrentVersion\CapabilityAccessManager\ConsentStore\microphone\Mozilla.MozillaFirefox_5x4grbbqzn2q4 -Name "Value" -Value "Allow" -Force',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "disable windows security and maintenance notifications",
            "cmd": [
                "powershell",
                "-command",
                "\"&{$p='HKCU:SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings\\Windows.SystemToast.SecurityAndMaintenance';if(!(Test-Path -Path $p)){&New-Item -Path $p -Force}&Set-ItemProperty -Path $p -Name Enabled -Value 0}\"",  # noqa
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": (platform.release() == "10"),
        },
        {
            "name": "set windows VisualFX",
            "cmd": [
                "powershell",
                "-command",
                f"\"&{{&Set-ItemProperty -Path 'HKCU:Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VisualEffects' -Name VisualFXSetting -Value {DESKTOP_VISUALFX_THEME}}}\"",
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "create scrollbars always show key",
            "cmd": [
                "powershell",
                "-command",
                "New-ItemProperty -Path 'HKCU:\\Control Panel\\Accessibility' -Name 'DynamicScrollbars' -Value 0",
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": False,
            "enabled": True,
        },
        {
            "name": "hide windows taskbar",
            "cmd": [
                "powershell",
                "-command",
                f"\"&{{$p='{TASKBAR_AUTOHIDE_REG_PATH}';$v=(Get-ItemProperty -Path $p).Settings;$v[8]=3;&Set-ItemProperty -Path $p -Name Settings -Value $v}}\"",
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "restart windows explorer",
            "cmd": [
                "powershell",
                "-command",
                '"&{&Stop-Process -ProcessName explorer}"',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True,
        },
        {
            "name": "prepare chrome profile",
            "cmd": [
                "powershell",
                "-command",
                "if (test-path ${env:ProgramFiles(x86)}\\Google\\Chrome\\Application\\chrome.exe) {start chrome; Start-Sleep -s 30; taskkill /F /IM chrome.exe /T}",
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": False,
        },
        {
            "name": "ensure proper graphics driver",
            "cmd": [
                "powershell",
                "-command",
                'if (-Not ((Get-CimInstance win32_VideoController).InstalledDisplayDrivers | Out-String).contains("nvgrid")) { echo "Missing nvgrid driver: " + (Get-CimInstance win32_VideoController).InstalledDisplayDrivers; exit 4; }',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True if REQUIRE_GPU and not USE_HARDWARE else False,
            "fatal_exit_code": 4,
        },
        {
            "name": "ensure display refresh rate == 60",
            "cmd": [
                "powershell",
                "-command",
                'if (-Not ((Get-WmiObject win32_videocontroller).CurrentRefreshRate | Out-String).contains("60")) { echo "Screen refresh rate != 60: " + ((Get-WmiObject win32_videocontroller).CurrentRefreshRate | Out-String); exit 4; }',
            ],
            "architectures": ["32bit", "64bit"],
            "halt_on_failure": True,
            "enabled": True if REQUIRE_GPU and USE_HARDWARE else False,
            "fatal_exit_code": 4,
        },
    ],
    "vcs_output_timeout": 1000,
    "minidump_save_path": "%(abs_work_dir)s/../minidumps",
    "unstructured_flavors": {
        "jittest": [],
    },
    "nodejs_path": NODEJS_PATH,
}
