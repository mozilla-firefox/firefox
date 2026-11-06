#!/usr/bin/env python
from unittest import mock

import mozunit

from mozperftest.profiler import ProfilingMediator


class MockProfilerController:
    def __init__(self, name="mock_controller"):
        self.name = name
        self.start_called = False
        self.stop_called = False
        self.options = None
        self.output_path = None
        self.index = None

    def start(self, options=None):
        self.start_called = True
        self.options = options

    def stop(self, output_path, index):
        self.stop_called = True
        self.output_path = output_path
        self.index = index


class MockProfiler:
    name = "mock_profiler"

    @staticmethod
    def is_enabled():
        return True

    @staticmethod
    def get_controller():
        pass


def test_profiling_mediator():
    """Test the ProfilingMediator with custom options and multiple mock profilers, some active and inactive."""

    controller1 = MockProfilerController("controller1")
    controller2 = MockProfilerController("controller2")
    controller3 = MockProfilerController("controller3")

    mock_profiler1 = mock.MagicMock()
    mock_profiler1.is_enabled.return_value = True
    mock_profiler1.get_controller.return_value = controller1

    mock_profiler2 = mock.MagicMock()
    mock_profiler2.is_enabled.return_value = True
    mock_profiler2.get_controller.return_value = controller2

    mock_profiler3 = mock.MagicMock()
    mock_profiler3.is_enabled.return_value = False
    mock_profiler3.get_controller.return_value = controller3

    """Use a dict of mock profilers here.  We expect each profiler
       to test their own controllers directly.
    """
    with mock.patch(
        "mozperftest.profiler.PROFILERS",
        {
            "profiler1": mock_profiler1,
            "profiler2": mock_profiler2,
            "profiler3": mock_profiler3,
        },
    ):
        profiler = ProfilingMediator(["profiler1", "profiler2", "profiler3"])

        assert len(profiler.active_profilers) == 2
        assert controller1 in profiler.active_profilers
        assert controller2 in profiler.active_profilers
        assert controller3 not in profiler.active_profilers

        # Test start
        custom_options = "custom options here"
        profiler.start(custom_options)

        assert controller1.start_called
        assert controller1.options == custom_options
        assert controller2.start_called
        assert controller2.options == custom_options
        assert not controller3.start_called

        # Test stop
        output_path = "/mock/output/path"
        index = 3
        profiler.stop(output_path, index)

        assert controller1.stop_called
        assert controller1.output_path == output_path
        assert controller1.index == index
        assert controller2.stop_called
        assert controller2.output_path == output_path
        assert controller2.index == index
        assert not controller3.stop_called


def test_profiling_mediator_defaults_to_all_profilers():
    """Test that ProfilingMediator defaults to all profilers when profilers=None."""

    controller1 = MockProfilerController("controller1")
    controller2 = MockProfilerController("controller2")

    mock_profiler1 = mock.MagicMock()
    mock_profiler1.is_enabled.return_value = True
    mock_profiler1.get_controller.return_value = controller1

    mock_profiler2 = mock.MagicMock()
    mock_profiler2.is_enabled.return_value = True
    mock_profiler2.get_controller.return_value = controller2

    with mock.patch(
        "mozperftest.profiler.PROFILERS",
        {
            "profiler1": mock_profiler1,
            "profiler2": mock_profiler2,
        },
    ):
        # Pass None to use all profilers
        profiler = ProfilingMediator(None)

        assert len(profiler.active_profilers) == 2
        assert controller1 in profiler.active_profilers
        assert controller2 in profiler.active_profilers


if __name__ == "__main__":
    mozunit.main()
