# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os
import socket
import sys
import time
import unittest

from marionette_driver import errors
from marionette_driver.geckoinstance import GeckoInstance
from marionette_driver.marionette import Marionette
from marionette_harness import MarionetteTestCase, run_if_manage_instance


EXIT_CODE_NOT_AVAILABLE = 69


class TestMarionette(MarionetteTestCase):
    def test_correct_test_name(self):
        """Test that the correct test name gets set."""
        expected_test_name = "{module}.py {cls}.{func}".format(
            module=__name__,
            cls=self.__class__.__name__,
            func=self.test_correct_test_name.__name__,
        )

        self.assertIn(expected_test_name, self.marionette.test_name)

    @run_if_manage_instance("Only runnable if Marionette manages the instance")
    def test_raise_for_port_non_existing_process(self):
        """Test that raise_for_port doesn't run into a timeout if instance is not running."""
        self.marionette.quit()
        self.assertIsNotNone(self.marionette.instance.runner.returncode)
        start_time = time.time()
        self.assertRaises(socket.timeout, self.marionette.raise_for_port, timeout=5)
        self.assertLess(time.time() - start_time, 5)

    @run_if_manage_instance("Only runnable if Marionette manages the instance")
    def test_marionette_active_port_file(self):
        active_port_file = os.path.join(
            self.marionette.instance.profile.profile, "MarionetteActivePort"
        )
        self.assertTrue(
            os.path.exists(active_port_file), "MarionetteActivePort file written"
        )
        with open(active_port_file, "r") as fp:
            lines = fp.readlines()
        self.assertEqual(len(lines), 1, "MarionetteActivePort file contains two lines")
        self.assertEqual(
            int(lines[0]),
            self.marionette.port,
            "MarionetteActivePort file contains port",
        )

        self.marionette.quit()
        self.assertFalse(
            os.path.exists(active_port_file), "MarionetteActivePort file removed"
        )

    @unittest.skipIf(sys.platform.startswith("win"), "Bug 2055174")
    @run_if_manage_instance("Only runnable if Marionette manages the instance")
    def test_exit_code_for_marionette_server_failure(self):
        second_marionette = Marionette(host=self.marionette.host, port=self.marionette.port)
        first_gecko_log = self.marionette.instance.gecko_log
        gecko_log = first_gecko_log if first_gecko_log == "-" else os.path.dirname(first_gecko_log)
        second_marionette.instance = GeckoInstance.create(
            None, host=second_marionette.host,
            port=second_marionette.port,
            bin=self.marionette.bin,
            gecko_log=gecko_log)

        self.addCleanup(second_marionette.instance.close, clean=True)

        with self.assertRaises(OSError):
            second_marionette.start_binary(second_marionette.startup_timeout)

        exit_code = second_marionette.instance.runner.wait(second_marionette.DEFAULT_SHUTDOWN_TIMEOUT)
        self.assertEqual(exit_code, EXIT_CODE_NOT_AVAILABLE)

    def test_single_active_session(self):
        self.assertEqual(1, self.marionette.execute_script("return 1"))

        # Use a new Marionette instance for the connection attempt, while there is
        # still an active session present.
        marionette = Marionette(host=self.marionette.host, port=self.marionette.port)
        self.assertRaises(socket.timeout, marionette.raise_for_port, timeout=1.0)

    def test_disable_enable_new_connections(self):
        # Do not re-create socket if it already exists
        self.marionette._send_message("Marionette:AcceptConnections", {"value": True})

        try:
            # Disabling new connections does not affect the existing one.
            self.marionette._send_message(
                "Marionette:AcceptConnections", {"value": False}
            )
            self.assertEqual(1, self.marionette.execute_script("return 1"))

            # Delete the current active session to allow new connection attempts.
            self.marionette.delete_session()

            # Use a new Marionette instance for the connection attempt, that doesn't
            # handle an instance of the application to prevent a connection lost error.
            marionette = Marionette(
                host=self.marionette.host, port=self.marionette.port
            )
            self.assertRaises(socket.timeout, marionette.raise_for_port, timeout=1.0)

        finally:
            self.marionette.quit(in_app=False)

    def test_client_socket_uses_expected_socket_timeout(self):
        current_socket_timeout = self.marionette.socket_timeout

        self.assertEqual(current_socket_timeout, self.marionette.client.socket_timeout)
        self.assertEqual(
            current_socket_timeout,
            self.marionette.client._socket_context._sock.gettimeout(),
        )

    def test_application_update_disabled(self):
        # Updates of the application should always be disabled by default
        with self.marionette.using_context("chrome"):
            update_allowed = self.marionette.execute_script(
                """
              let aus = Cc['@mozilla.org/updates/update-service;1']
                        .getService(Ci.nsIApplicationUpdateService);
              return aus.canCheckForUpdates;
            """
            )

        self.assertFalse(update_allowed)


class TestContext(MarionetteTestCase):
    def setUp(self):
        MarionetteTestCase.setUp(self)
        self.marionette.set_context(self.marionette.CONTEXT_CONTENT)

    def get_context(self):
        return self.marionette._send_message("Marionette:GetContext", key="value")

    def set_context(self, value):
        return self.marionette._send_message("Marionette:SetContext", {"value": value})

    def test_set_context(self):
        self.assertEqual(self.set_context("content"), {"value": None})
        self.assertEqual(self.set_context("chrome"), {"value": None})

        for typ in [True, 42, [], {}, None]:
            with self.assertRaises(errors.InvalidArgumentException):
                self.set_context(typ)

        with self.assertRaises(errors.MarionetteException):
            self.set_context("foo")

    def test_get_context(self):
        self.assertEqual(self.get_context(), "content")
        self.set_context("chrome")
        self.assertEqual(self.get_context(), "chrome")
        self.set_context("content")
        self.assertEqual(self.get_context(), "content")
