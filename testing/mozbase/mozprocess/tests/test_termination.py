import os
import signal
import sys
import threading
import time

import mozunit
import pytest
from mozlog.structuredlog import StructuredLogger
from mozprocess.termination import TerminationRequested, handle_termination

pytestmark = pytest.mark.skipif(
    sys.platform == "win32", reason="SIGTERM can't be handled on Windows"
)


@pytest.fixture
def logger(request):
    logger = StructuredLogger(request.node.name)
    logger.entries = []
    logger.add_handler(logger.entries.append)
    yield logger
    logger.remove_handler(logger.entries.append)


def critical_messages(logger):
    return [e["message"] for e in logger.entries if e.get("level") == "CRITICAL"]


def sigterm():
    os.kill(os.getpid(), signal.SIGTERM)
    # make sure the signal handler has run before returning
    time.sleep(1)


def test_sigterm_aborts_context(logger):
    reached = False
    with handle_termination(logger):
        sigterm()
        reached = True
    assert not reached
    assert len(critical_messages(logger)) == 1
    assert "Received SIGTERM" in critical_messages(logger)[0]


def test_message(logger):
    with handle_termination(logger, "stop now"):
        sigterm()
    assert critical_messages(logger) == ["stop now"]


def test_sigterm_handled_as_keyboard_interrupt(logger):
    caught = None
    with handle_termination(logger):
        try:
            sigterm()
        except KeyboardInterrupt as e:
            caught = e
    assert isinstance(caught, TerminationRequested)
    assert len(critical_messages(logger)) == 1


def test_later_sigterms_ignored(logger):
    with handle_termination(logger):
        try:
            sigterm()
        except KeyboardInterrupt:
            pass
        sigterm()
        cleaned_up = True
    assert cleaned_up
    assert len(critical_messages(logger)) == 1


def test_no_sigterm(logger):
    previous = signal.getsignal(signal.SIGTERM)
    with handle_termination(logger):
        assert signal.getsignal(signal.SIGTERM) != previous
    assert signal.getsignal(signal.SIGTERM) == previous
    assert critical_messages(logger) == []


def test_handler_restored_after_sigterm(logger):
    previous = signal.getsignal(signal.SIGTERM)
    with handle_termination(logger):
        sigterm()
    assert signal.getsignal(signal.SIGTERM) == previous


def test_keyboard_interrupt_without_sigterm_propagates(logger):
    with pytest.raises(KeyboardInterrupt):
        with handle_termination(logger):
            raise KeyboardInterrupt()
    assert critical_messages(logger) == []


def test_other_exceptions_propagate(logger):
    with pytest.raises(ValueError):
        with handle_termination(logger):
            try:
                sigterm()
            except KeyboardInterrupt:
                raise ValueError("cleanup failed")
    assert len(critical_messages(logger)) == 1


def test_not_main_thread(logger):
    previous = signal.getsignal(signal.SIGTERM)
    errors = []

    def run():
        try:
            with handle_termination(logger):
                assert signal.getsignal(signal.SIGTERM) == previous
        except Exception as e:
            errors.append(e)

    thread = threading.Thread(target=run)
    thread.start()
    thread.join()
    assert errors == []


if __name__ == "__main__":
    mozunit.main()
