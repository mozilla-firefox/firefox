# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import sys
from unittest import mock

import mozunit
import pytest
from mozdevice import ADBError

here = os.path.abspath(os.path.dirname(__file__))
raptor_dir = os.path.join(os.path.dirname(here), "raptor")
sys.path.insert(0, raptor_dir)

from performance_tuning import PerformanceTuner, tune_performance

PHASE_PREFIXES = {
    "scheduler": "/sys/block/",
    "vm": "/proc/sys/vm/",
    "cpu": "/sys/devices/system/cpu/",
    "gpu": "/sys/class/kgsl/",
    "kernel": "/sys/kernel/debug/",
}
SCHEDULER_WRITE = "echo noop > /sys/block/sda/queue/scheduler"
STOP_SERVICES = ["stop mpdecision", "stop thermal-engine", "stop thermald"]


def make_device(model="Pixel 6", rooted=True, service_list="", fail_on=None):
    device = mock.Mock()
    device.is_rooted = rooted
    fail_on = fail_on or set()

    def shell_output(cmd, timeout=None):
        if cmd in fail_on or any(cmd.startswith(f) for f in fail_on):
            raise ADBError(f"failed: {cmd}")
        if cmd == "getprop ro.product.model":
            return model
        if cmd == "service list":
            return service_list
        return ""

    def shell_bool(cmd, timeout=None):
        if cmd in fail_on:
            raise ADBError(f"failed: {cmd}")
        return True

    device.shell_output.side_effect = shell_output
    device.shell_bool.side_effect = shell_bool
    return device


def output_cmds(device):
    return [c.args[0] for c in device.shell_output.call_args_list]


def bool_cmds(device):
    return [c.args[0] for c in device.shell_bool.call_args_list]


def writes(device):
    return [c for c in output_cmds(device) if c.startswith("echo ")]


def write_target(cmd):
    return cmd.split(" > ", 1)[1]


def phase_of(cmd):
    target = write_target(cmd)
    for phase, prefix in PHASE_PREFIXES.items():
        if target.startswith(prefix):
            return phase
    return None


def phase_order(device):
    order = []
    for cmd in writes(device):
        phase = phase_of(cmd)
        if not order or order[-1] != phase:
            order.append(phase)
    return order


def all_timeouts(device):
    calls = (
        device.shell_output.call_args_list
        + device.shell_bool.call_args_list
        + device.clear_logcat.call_args_list
    )
    return {c.kwargs.get("timeout") for c in calls}


def test_init_defaults_to_device_logger():
    """Check that the tuner falls back to the device logger and empty test names."""
    device = make_device()
    tuner = PerformanceTuner(device)
    assert tuner.log is device._logger
    assert tuner.timeout is None
    assert tuner.test_names == []


def test_init_uses_given_log_and_test_names():
    """Check that an explicit log, timeout and test names are stored on the tuner."""
    device = make_device()
    log = mock.Mock()
    tuner = PerformanceTuner(device, log=log, timeout=7, test_names=["a"])
    assert tuner.log is log
    assert tuner.timeout == 7
    assert tuner.test_names == ["a"]


def test_tune_performance_not_rooted_only_stays_awake():
    """Check that an unrooted device only gets the stay-awake setting and a logcat clear."""
    device = make_device(rooted=False)
    log = mock.Mock()
    tune_performance(device, log=log, timeout=30)

    assert bool_cmds(device) == ["svc power stayon usb"]
    device.shell_output.assert_not_called()
    device.clear_logcat.assert_called_once_with(timeout=30)
    assert all_timeouts(device) == {30}
    log.info.assert_any_call("android device performance tuning complete")


def test_tune_performance_rooted_unknown_device():
    """Check that a rooted unknown model gets every tuning phase except CPU, in order."""
    device = make_device(model="Pixel 6")
    tune_performance(device, log=mock.Mock(), timeout=12)

    assert bool_cmds(device) == ["svc power stayon usb"] + STOP_SERVICES
    cmds = output_cmds(device)
    assert cmds[0] == "getprop ro.product.model"
    assert cmds.count("getprop ro.product.model") == 1
    assert phase_order(device) == ["scheduler", "vm", "gpu", "kernel"]
    assert "echo 0 > /proc/sys/vm/swappiness" in cmds
    assert cmds.index("service list") < cmds.index(
        "echo 0 > /sys/class/kgsl/kgsl-3d0/bus_split"
    )
    device.clear_logcat.assert_called_once_with(timeout=12)
    assert all_timeouts(device) == {12}


def test_tune_performance_rooted_a51_sets_cpu_governor():
    """Check that the Samsung A51 also gets CPU writes between the VM and GPU phases."""
    device = make_device(model="SM-A515F")
    tune_performance(device, log=mock.Mock())

    assert phase_order(device) == ["scheduler", "vm", "cpu", "gpu", "kernel"]
    cmds = output_cmds(device)
    assert (
        "echo performance > /sys/devices/system/cpu/cpufreq/policy0/scaling_governor"
        in cmds
    )
    assert (
        "echo 2314000 > /sys/devices/system/cpu/cpufreq/policy4/scaling_min_freq"
        in cmds
    )


@pytest.mark.parametrize(
    "model,test_names,expect_extra",
    [
        ("SM-A556E", ["speedometer3"], True),
        ("SM-A556B", ["browsertime-speedometer3-fenix"], True),
        ("SM-A556E", ["tp6m-amazon"], False),
        ("SM-A556E", None, False),
        ("SM-S921U", ["speedometer3"], False),
    ],
)
def test_tune_performance_a55_speedometer3_extras(model, test_names, expect_extra):
    """Check that only A55 speedometer3 runs disable accessibility and LambdaTest apps."""
    device = make_device(model=model)
    tune_performance(device, log=mock.Mock(), test_names=test_names)

    extra = bool_cmds(device)[1 + len(STOP_SERVICES) :]
    if expect_extra:
        assert "settings put secure accessibility_enabled 0" in extra
        assert "pm disable com.lambdatest.androidwebrtc" in extra
    else:
        assert extra == []


def test_tune_performance_uses_device_logger_by_default():
    """Check that the module-level helper logs to the device logger when none is given."""
    device = make_device(rooted=False)
    tune_performance(device)
    device._logger.info.assert_any_call("tuning android device performance")


def test_set_value_logs_success():
    """Check that a successful write echoes the value into the file and logs success."""
    device = make_device()
    log = mock.Mock()
    tuner = PerformanceTuner(device, log=log, timeout=5)
    tuner._set_value_and_check_exitcode("/some/file", 42)

    device.shell_output.assert_called_once_with("echo 42 > /some/file", timeout=5)
    log.info.assert_any_call("successfully set /some/file to 42")


def test_set_value_ignores_adb_error():
    """Check that an ADBError during a write is logged and not raised."""
    device = make_device(fail_on={"echo 42 > /some/file"})
    log = mock.Mock()
    tuner = PerformanceTuner(device, log=log)
    tuner._set_value_and_check_exitcode("/some/file", 42)

    messages = [c.args[0] for c in log.info.call_args_list]
    assert "successfully set /some/file to 42" not in messages
    assert any(
        m.startswith("Ignoring failure to set value /some/file to 42.")
        and "failed: echo 42 > /some/file" in m
        for m in messages
    )


def test_set_value_does_not_swallow_other_errors():
    """Check that errors other than ADBError propagate out of a write."""
    device = make_device()
    device.shell_output.side_effect = RuntimeError("boom")
    tuner = PerformanceTuner(device, log=mock.Mock())
    with pytest.raises(RuntimeError, match="boom"):
        tuner._set_value_and_check_exitcode("/some/file", 1)


def test_failed_sysfs_writes_do_not_abort_tuning():
    """Check that failing scheduler, VM and CPU writes still let later phases run."""
    device = make_device(
        model="SM-A515F",
        fail_on={
            SCHEDULER_WRITE,
            "echo 0 > /proc/sys/vm/",
            "echo performance > /sys/devices/system/cpu/",
        },
    )
    tune_performance(device, log=mock.Mock())
    assert phase_order(device) == ["scheduler", "vm", "cpu", "gpu", "kernel"]
    device.clear_logcat.assert_called_once()


def test_turn_off_services_reports_success():
    """Check that services absent from the service list are reported as terminated."""
    device = make_device(service_list="1 activity: [android.app.IActivityManager]")
    log = mock.Mock()
    PerformanceTuner(device, log=log).turn_off_services()

    assert bool_cmds(device) == STOP_SERVICES
    for service in ("mpdecision", "thermal-engine", "thermald"):
        log.info.assert_any_call(f"successfully terminated: {service}")
    log.warning.assert_not_called()


def test_turn_off_services_warns_when_still_listed():
    """Check that a service still in the service list produces a warning."""
    device = make_device(service_list="5 thermal-engine: []\n")
    log = mock.Mock()
    PerformanceTuner(device, log=log).turn_off_services()

    log.warning.assert_called_once_with("failed to terminate: thermal-engine")
    log.info.assert_any_call("successfully terminated: mpdecision")


def test_turn_off_services_ignores_stop_errors():
    """Check that a failing stop command is logged and the remaining services still stop."""
    device = make_device(fail_on={"stop mpdecision", "stop thermald"})
    log = mock.Mock()
    PerformanceTuner(device, log=log).turn_off_services()

    assert bool_cmds(device) == STOP_SERVICES
    log.info.assert_any_call(
        "Ignoring failure to stop service mpdecision. Error: ADBError: failed: stop mpdecision"
    )
    assert "service list" in output_cmds(device)


def test_set_scheduler():
    """Check that the block scheduler is set to noop with the configured timeout."""
    device = make_device()
    PerformanceTuner(device, log=mock.Mock(), timeout=3).set_scheduler()
    device.shell_output.assert_called_once_with(SCHEDULER_WRITE, timeout=3)


def test_set_svc_power_stayon():
    """Check that the device is told to stay awake while on USB."""
    device = make_device()
    PerformanceTuner(device, log=mock.Mock(), timeout=3).set_svc_power_stayon()
    device.shell_bool.assert_called_once_with("svc power stayon usb", timeout=3)


def test_disable_accessibility():
    """Check that accessibility services are cleared and accessibility is turned off."""
    device = make_device()
    PerformanceTuner(device, log=mock.Mock()).disable_accessibility()
    assert bool_cmds(device) == [
        'settings put secure enabled_accessibility_services ""',
        "settings put secure accessibility_enabled 0",
    ]


def test_disable_lt_services():
    """Check that each LambdaTest package is force-stopped and then disabled."""
    device = make_device()
    PerformanceTuner(device, log=mock.Mock()).disable_lt_services()
    cmds = bool_cmds(device)
    for pkg in ("com.genymobile.scrcpy", "com.lambdatest.androidwebrtc"):
        assert cmds.index(f"am force-stop {pkg}") < cmds.index(f"pm disable {pkg}")
        assert f"pm disable-user {pkg}" in cmds


@pytest.mark.parametrize(
    "method,value", [("disable_animations", "0.0"), ("restore_animations", "1.0")]
)
def test_animation_scales(method, value):
    """Check that all three global animation scales are set to the expected value."""
    device = make_device()
    getattr(PerformanceTuner(device, log=mock.Mock(), timeout=4), method)()
    cmds = bool_cmds(device)
    assert len(cmds) == 3
    assert all(
        c.startswith("settings put global ") and c.endswith(f"_scale {value}")
        for c in cmds
    )
    assert f"settings put global window_animation_scale {value}" in cmds
    assert all_timeouts(device) == {4}


def test_set_virtual_memory_parameters():
    """Check that only VM sysctls are written, with swappiness disabled."""
    device = make_device()
    PerformanceTuner(device, log=mock.Mock()).set_virtual_memory_parameters()
    assert {phase_of(c) for c in writes(device)} == {"vm"}
    assert "echo 0 > /proc/sys/vm/swappiness" in output_cmds(device)


@pytest.mark.parametrize(
    "model,expect_cpu", [("SM-A515F", True), ("Pixel 6", False), ("SM-A515FN", False)]
)
def test_set_cpu_performance_parameters_queries_model(model, expect_cpu):
    """Check that CPU writes happen only for an exact A51 model match when none is given."""
    device = make_device(model=model)
    PerformanceTuner(device, log=mock.Mock()).set_cpu_performance_parameters()
    assert output_cmds(device)[0] == "getprop ro.product.model"
    assert bool(writes(device)) == expect_cpu
    assert {phase_of(c) for c in writes(device)} <= {"cpu"}


def test_set_cpu_performance_parameters_unknown_device_logs():
    """Check that a given unknown model skips CPU writes without querying the device."""
    device = make_device()
    log = mock.Mock()
    PerformanceTuner(device, log=log).set_cpu_performance_parameters("Pixel 6")
    device.shell_output.assert_not_called()
    log.info.assert_any_call(
        "CPU for device with ro.product.model 'Pixel 6' unknown, not scaling_governor"
    )


@pytest.mark.parametrize("device_name", [None, "SM-A515F"])
def test_set_gpu_performance_parameters(device_name):
    """Check that kgsl GPU writes happen and the model is queried only when not given."""
    device = make_device(model="SM-A515F")
    PerformanceTuner(device, log=mock.Mock()).set_gpu_performance_parameters(
        device_name
    )

    cmds = output_cmds(device)
    assert ("getprop ro.product.model" in cmds) == (device_name is None)
    assert {phase_of(c) for c in writes(device)} == {"gpu"}
    assert "echo 1000000 > /sys/class/kgsl/kgsl-3d0/idle_timer" in cmds


def test_set_kernel_performance_parameters():
    """Check that only msm-bus debugfs entries are written."""
    device = make_device()
    PerformanceTuner(device, log=mock.Mock()).set_kernel_performance_parameters()
    assert writes(device)
    assert {phase_of(c) for c in writes(device)} == {"kernel"}


if __name__ == "__main__":
    mozunit.main()
