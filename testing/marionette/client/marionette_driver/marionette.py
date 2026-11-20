# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import base64
import datetime
import json
import os
import socket
import sys
import time
import traceback
from contextlib import contextmanager

from . import errors, transport
from .decorators import do_process_check
from .geckoinstance import GeckoInstance
from .keys import Keys
from .timeout import Timeouts

WEB_ELEMENT_KEY = "element-6066-11e4-a52e-4f735466cecf"
WEB_FRAME_KEY = "frame-075b-4da1-b6ba-e579c2d3230a"
WEB_SHADOW_ROOT_KEY = "shadow-6066-11e4-a52e-4f735466cecf"
WEB_WINDOW_KEY = "window-fcc6-11e5-b4f8-330a88ab9d7f"


class MouseButton:
    """Enum-like class for mouse button constants."""

    LEFT = 0
    MIDDLE = 1
    RIGHT = 2


class ActionSequence:
    r"""API for creating and performing action sequences.

    Each action method adds one or more actions to a queue. When perform()
    is called, the queued actions fire in order.

    May be chained together as in::

         ActionSequence(self.marionette, "key", id) \
            .key_down("a") \
            .key_up("a") \
            .perform()
    """

    def __init__(self, marionette, action_type, input_id, pointer_params=None):
        self.marionette = marionette
        self._actions = []
        self._id = input_id
        self._pointer_params = pointer_params
        self._type = action_type

    @property
    def dict(self):
        d = {
            "type": self._type,
            "id": self._id,
            "actions": self._actions,
        }
        if self._pointer_params is not None:
            d["parameters"] = self._pointer_params
        return d

    def perform(self):
        """Perform all queued actions."""
        self.marionette.actions.perform([self.dict])

    def _key_action(self, subtype, value):
        self._actions.append({"type": subtype, "value": value})

    def _pointer_action(self, subtype, button):
        self._actions.append({"type": subtype, "button": button})

    def pause(self, duration):
        self._actions.append({"type": "pause", "duration": duration})
        return self

    def pointer_move(self, x, y, duration=None, origin=None):
        """Queue a pointerMove action.

        :param x: Destination x-axis coordinate of pointer in CSS pixels.
        :param y: Destination y-axis coordinate of pointer in CSS pixels.
        :param duration: Number of milliseconds over which to distribute the
                         move. If None, remote end defaults to 0.
        :param origin: Origin of coordinates, either "viewport", "pointer" or
                       an Element. If None, remote end defaults to "viewport".
        """
        action = {"type": "pointerMove", "x": x, "y": y}
        if duration is not None:
            action["duration"] = duration
        if origin is not None:
            if isinstance(origin, WebElement):
                action["origin"] = {origin.kind: origin.id}
            else:
                action["origin"] = origin
        self._actions.append(action)
        return self

    def pointer_up(self, button=MouseButton.LEFT):
        """Queue a pointerUp action for `button`.

        :param button: Pointer button to perform action with.
                       Default: 0, which represents main device button.
        """
        self._pointer_action("pointerUp", button)
        return self

    def pointer_down(self, button=MouseButton.LEFT):
        """Queue a pointerDown action for `button`.

        :param button: Pointer button to perform action with.
                       Default: 0, which represents main device button.
        """
        self._pointer_action("pointerDown", button)
        return self

    def click(self, element=None, button=MouseButton.LEFT):
        """Queue a click with the specified button.

        If an element is given, move the pointer to that element first,
        otherwise click current pointer coordinates.

        :param element: Optional element to click.
        :param button: Integer representing pointer button to perform action
                       with. Default: 0, which represents main device button.
        """
        if element:
            self.pointer_move(0, 0, origin=element)
        return self.pointer_down(button).pointer_up(button)

    def key_down(self, value):
        """Queue a keyDown action for `value`.

        :param value: Single character to perform key action with.
        """
        self._key_action("keyDown", value)
        return self

    def key_up(self, value):
        """Queue a keyUp action for `value`.

        :param value: Single character to perform key action with.
        """
        self._key_action("keyUp", value)
        return self

    def scroll(self, x, y, delta_x, delta_y, duration=None, origin=None):
        """Queue a scroll action.

        :param x: Destination x-axis coordinate of pointer in CSS pixels.
        :param y: Destination y-axis coordinate of pointer in CSS pixels.
        :param delta_x: Scroll delta for x-axis in CSS pixels.
        :param delta_y: Scroll delta for y-axis in CSS pixels.
        :param duration: Number of milliseconds over which to distribute the
                         scroll. If None, remote end defaults to 0.
        :param origin: Origin of coordinates, either "viewport", "pointer" or
                       an Element. If None, remote end defaults to "viewport".
        """
        action = {
            "type": "scroll",
            "x": x,
            "y": y,
            "deltaX": delta_x,
            "deltaY": delta_y,
        }

        if duration is not None:
            action["duration"] = duration
        if origin is not None:
            if isinstance(origin, WebElement):
                action["origin"] = {origin.kind: origin.id}
            else:
                action["origin"] = origin
        self._actions.append(action)
        return self

    def send_keys(self, keys):
        """Queue a keyDown and keyUp action for each character in `keys`.

        :param keys: String of keys to perform key actions with.
        """
        for c in keys:
            self.key_down(c)
            self.key_up(c)
        return self


class Actions:
    def __init__(self, marionette):
        self.marionette = marionette

    def perform(self, actions=None):
        """Perform actions by tick from each action sequence in `actions`.

        :param actions: List of input source action sequences. A single action
                        sequence may be created with the help of
                        ``ActionSequence.dict``.
        """
        body = {"actions": [] if actions is None else actions}
        return self.marionette._send_message("WebDriver:PerformActions", body)

    def release(self):
        return self.marionette._send_message("WebDriver:ReleaseActions")

    def sequence(self, *args, **kwargs):
        """Return an empty ActionSequence of the designated type.

        See ActionSequence for parameter list.
        """
        return ActionSequence(self.marionette, *args, **kwargs)


class WebElement:
    """Represents a DOM Element."""

    identifiers = (WEB_ELEMENT_KEY,)

    def __init__(self, marionette, id, kind=WEB_ELEMENT_KEY):
        self.marionette = marionette
        assert id is not None
        self.id = id
        self.kind = kind

    def __str__(self):
        return self.id

    def __eq__(self, other_element):
        return self.id == other_element.id

    def __hash__(self):
        # pylint --py3k: W1641
        return hash(self.id)

    def find_element(self, method, target):
        """Returns an ``WebElement`` instance that matches the specified
        method and target, relative to the current element.

        For more details on this function, see the
        :func:`~marionette_driver.marionette.Marionette.find_element` method
        in the Marionette class.
        """
        return self.marionette.find_element(method, target, self.id)

    def find_elements(self, method, target):
        """Returns a list of all ``WebElement`` instances that match the
        specified method and target in the current context.

        For more details on this function, see the
        :func:`~marionette_driver.marionette.Marionette.find_elements` method
        in the Marionette class.
        """
        return self.marionette.find_elements(method, target, self.id)

    def get_attribute(self, name):
        """Returns the requested attribute, or None if no attribute
        is set.
        """
        body = {"id": self.id, "name": name}
        return self.marionette._send_message(
            "WebDriver:GetElementAttribute", body, key="value"
        )

    def get_property(self, name):
        """Returns the requested property, or None if the property is
        not set.
        """
        try:
            body = {"id": self.id, "name": name}
            return self.marionette._send_message(
                "WebDriver:GetElementProperty", body, key="value"
            )
        except errors.UnknownCommandException:
            # Keep backward compatibility for code which uses get_attribute() to
            # also retrieve element properties.
            # Remove when Firefox 55 is stable.
            return self.get_attribute(name)

    def click(self):
        """Simulates a click on the element."""
        self.marionette._send_message("WebDriver:ElementClick", {"id": self.id})

    @property
    def text(self):
        """Returns the visible text of the element, and its child elements."""
        body = {"id": self.id}
        return self.marionette._send_message(
            "WebDriver:GetElementText", body, key="value"
        )

    def send_keys(self, *strings):
        """Sends the string via synthesized keypresses to the element.
        If an array is passed in like `marionette.send_keys(Keys.SHIFT, "a")` it
        will be joined into a string.
        If an integer is passed in like `marionette.send_keys(1234)` it will be
        coerced into a string.
        """
        keys = Marionette.convert_keys(*strings)
        self.marionette._send_message(
            "WebDriver:ElementSendKeys", {"id": self.id, "text": keys}
        )

    def clear(self):
        """Clears the input of the element."""
        self.marionette._send_message("WebDriver:ElementClear", {"id": self.id})

    def is_selected(self):
        """Returns True if the element is selected."""
        body = {"id": self.id}
        return self.marionette._send_message(
            "WebDriver:IsElementSelected", body, key="value"
        )

    def is_enabled(self):
        """This command will return False if all the following criteria
        are met otherwise return True:

        * A form control is disabled.
        * A ``WebElement`` has a disabled boolean attribute.
        """
        body = {"id": self.id}
        return self.marionette._send_message(
            "WebDriver:IsElementEnabled", body, key="value"
        )

    def is_displayed(self):
        """Returns True if the element is displayed, False otherwise."""
        body = {"id": self.id}
        return self.marionette._send_message(
            "WebDriver:IsElementDisplayed", body, key="value"
        )

    @property
    def tag_name(self):
        """The tag name of the element."""
        body = {"id": self.id}
        return self.marionette._send_message(
            "WebDriver:GetElementTagName", body, key="value"
        )

    @property
    def rect(self):
        """Gets the element's bounding rectangle.

        This will return a dictionary with the following:

          * x and y represent the top left coordinates of the ``WebElement``
            relative to top left corner of the document.
          * height and the width will contain the height and the width
            of the DOMRect of the ``WebElement``.
        """
        return self.marionette._send_message(
            "WebDriver:GetElementRect", {"id": self.id}
        )

    def value_of_css_property(self, property_name):
        """Gets the value of the specified CSS property name.

        :param property_name: Property name to get the value of.
        """
        body = {"id": self.id, "propertyName": property_name}
        return self.marionette._send_message(
            "WebDriver:GetElementCSSValue", body, key="value"
        )

    @property
    def shadow_root(self):
        """Gets the shadow root of the current element"""
        return self.marionette._send_message(
            "WebDriver:GetShadowRoot", {"id": self.id}, key="value"
        )

    @property
    def computed_label(self):
        """Gets the computed accessibility label of the current element"""
        return self.marionette._send_message(
            "WebDriver:GetComputedLabel", {"id": self.id}, key="value"
        )

    @property
    def computed_role(self):
        """Gets the computed accessibility role of the current element"""
        return self.marionette._send_message(
            "WebDriver:GetComputedRole", {"id": self.id}, key="value"
        )

    @property
    def accessibility_properties(self):
        """Gets the accessibility properties for the current element"""
        return self.marionette._send_message(
            "Marionette:GetAccessibilityPropertiesForElement",
            {"id": self.id},
            key="value",
        )

    @classmethod
    def _from_json(cls, json, marionette):
        if isinstance(json, dict):
            if WEB_ELEMENT_KEY in json:
                return cls(marionette, json[WEB_ELEMENT_KEY], WEB_ELEMENT_KEY)
        raise ValueError("Unrecognised web element")


class ShadowRoot:
    """A Class to handling Shadow Roots"""

    identifiers = (WEB_SHADOW_ROOT_KEY,)

    def __init__(self, marionette, id, kind=WEB_SHADOW_ROOT_KEY):
        self.marionette = marionette
        assert id is not None
        self.id = id
        self.kind = kind

    def __str__(self):
        return self.id

    def __eq__(self, other_element):
        return self.id == other_element.id

    def __hash__(self):
        # pylint --py3k: W1641
        return hash(self.id)

    def find_element(self, method, target):
        """Returns a ``WebElement`` instance that matches the specified
        method and target, relative to the current shadow root.

        For more details on this function, see the
        :func:`~marionette_driver.marionette.Marionette.find_element` method
        in the Marionette class.
        """
        body = {"shadowRoot": self.id, "value": target, "using": method}
        return self.marionette._send_message(
            "WebDriver:FindElementFromShadowRoot", body, key="value"
        )

    def find_elements(self, method, target):
        """Returns a list of all ``WebElement`` instances that match the
         specified method and target in the current shadow root.

        For more details on this function, see the
        :func:`~marionette_driver.marionette.Marionette.find_elements` method
        in the Marionette class.
        """
        body = {"shadowRoot": self.id, "value": target, "using": method}
        return self.marionette._send_message(
            "WebDriver:FindElementsFromShadowRoot", body
        )

    @classmethod
    def _from_json(cls, json, marionette):
        if isinstance(json, dict):
            if WEB_SHADOW_ROOT_KEY in json:
                return cls(marionette, json[WEB_SHADOW_ROOT_KEY])
        raise ValueError("Unrecognised shadow root")


class WebFrame:
    """A Class to handle frame windows"""

    identifiers = (WEB_FRAME_KEY,)

    def __init__(self, marionette, id, kind=WEB_FRAME_KEY):
        self.marionette = marionette
        assert id is not None
        self.id = id
        self.kind = kind

    def __str__(self):
        return self.id

    def __eq__(self, other_element):
        return self.id == other_element.id

    def __hash__(self):
        # pylint --py3k: W1641
        return hash(self.id)

    @classmethod
    def _from_json(cls, json, marionette):
        if isinstance(json, dict):
            if WEB_FRAME_KEY in json:
                return cls(marionette, json[WEB_FRAME_KEY])
        raise ValueError("Unrecognised web frame")


class WebWindow:
    """A Class to handle top-level windows"""

    identifiers = (WEB_WINDOW_KEY,)

    def __init__(self, marionette, id, kind=WEB_WINDOW_KEY):
        self.marionette = marionette
        assert id is not None
        self.id = id
        self.kind = kind

    def __str__(self):
        return self.id

    def __eq__(self, other_element):
        return self.id == other_element.id

    def __hash__(self):
        # pylint --py3k: W1641
        return hash(self.id)

    @classmethod
    def _from_json(cls, json, marionette):
        if isinstance(json, dict):
            if WEB_WINDOW_KEY in json:
                return cls(marionette, json[WEB_WINDOW_KEY])
        raise ValueError("Unrecognised web window")


class Alert:
    """A class for interacting with alerts.

    ::

        Alert(marionette).accept()
        Alert(marionette).dismiss()
    """

    def __init__(self, marionette):
        self.marionette = marionette

    def accept(self):
        """Accept a currently displayed modal dialog."""
        self.marionette._send_message("WebDriver:AcceptAlert")

    def dismiss(self):
        """Dismiss a currently displayed modal dialog."""
        self.marionette._send_message("WebDriver:DismissAlert")

    @property
    def text(self):
        """Return the currently displayed text in a tab modal."""
        return self.marionette._send_message("WebDriver:GetAlertText", key="value")

    def send_keys(self, *string):
        """Send keys to the currently displayed text input area in an open
        tab modal dialog."""
        self.marionette._send_message(
            "WebDriver:SendAlertText", {"text": Marionette.convert_keys(*string)}
        )


class Marionette:
    """Represents a Marionette connection to a browser or device."""

    CONTEXT_CHROME = "chrome"  # non-browser content: windows, dialogs, etc.
    CONTEXT_CONTENT = "content"  # browser content: iframes, divs, etc.
    DEFAULT_STARTUP_TIMEOUT = 120
    DEFAULT_SHUTDOWN_TIMEOUT = (
        70  # By default Firefox will kill hanging threads after 60s
    )

    # Bug 1336953 - Until we can remove the socket timeout parameter it has to be
    # set a default value which is larger than the longest timeout as defined by the
    # WebDriver spec. In that case its 300s for page load. Also add another minute
    # so that slow builds have enough time to send the timeout error to the client.
    DEFAULT_SOCKET_TIMEOUT = 360

    def __init__(
        self,
        host="127.0.0.1",
        port=2828,
        app=None,
        bin=None,
        baseurl=None,
        socket_timeout=None,
        startup_timeout=None,
        **instance_args,
    ):
        """Construct a holder for the Marionette connection.

        Remember to call ``start_session`` in order to initiate the
        connection and start a Marionette session.

        :param host: Host where the Marionette server listens.
            Defaults to 127.0.0.1.
        :param port: Port where the Marionette server listens.
            Defaults to port 2828.
        :param baseurl: Where to look for files served from Marionette's
            www directory.
        :param socket_timeout: Timeout for Marionette socket operations.
        :param startup_timeout: Seconds to wait for a connection with
            binary.
        :param bin: Path to browser binary.  If any truthy value is given
            this will attempt to start a Gecko instance with the specified
            `app`.
        :param app: Type of ``instance_class`` to use for managing app
            instance. See ``marionette_driver.geckoinstance``.
        :param instance_args: Arguments to pass to ``instance_class``.
        """
        self.host = "127.0.0.1"  # host
        if int(port) == 0:
            port = Marionette.check_port_available(port)
        self.port = self.local_port = int(port)
        self.bin = bin
        self.client = None
        self.instance = None
        self.requested_capabilities = None
        self.session = None
        self.session_id = None
        self.process_id = None
        self.profile = None
        self.window = None
        self.chrome_window = None
        self.baseurl = baseurl
        self._test_name = None
        self._test_group = None
        self.crashed = 0
        self.is_shutting_down = False
        self.cleanup_ran = False

        if socket_timeout is None:
            self.socket_timeout = self.DEFAULT_SOCKET_TIMEOUT
        else:
            self.socket_timeout = float(socket_timeout)

        if startup_timeout is None:
            self.startup_timeout = self.DEFAULT_STARTUP_TIMEOUT
        else:
            self.startup_timeout = int(startup_timeout)

        self.shutdown_timeout = self.DEFAULT_SHUTDOWN_TIMEOUT

        if self.bin:
            self.instance = GeckoInstance.create(
                app, host=self.host, port=self.port, bin=self.bin, **instance_args
            )
            self.start_binary(self.startup_timeout)

    @property
    def actions(self):
        return Actions(self)

    @property
    def timeout(self):
        return Timeouts(self)

    @property
    def profile_path(self):
        if self.instance and self.instance.profile:
            return self.instance.profile.profile

    def start_binary(self, timeout):
        try:
            self.check_port_available(self.port, host=self.host)
        except OSError:
            _, value, tb = sys.exc_info()
            msg = f"Port {self.host}:{self.port} is unavailable ({value})"
            raise OSError(msg).with_traceback(tb)

        try:
            self.instance.start()
            self.raise_for_port(timeout=timeout)
        except socket.timeout:
            # Something went wrong with starting up Marionette server. Given
            # that the process will not quit itself, force a shutdown immediately.
            self.cleanup()

            msg = (
                "Process killed after {}s because no connection to Marionette "
                "server could be established. Check gecko.log for errors"
            )
            raise OSError(msg.format(timeout)).with_traceback(sys.exc_info()[2])

    def cleanup(self):
        if self.session is not None:
            try:
                self.delete_session()
            except (OSError, errors.MarionetteException):
                # These exceptions get thrown if the Marionette server
                # hit an exception/died or the connection died. We can
                # do no further server-side cleanup in this case.
                pass

        if self.instance:
            # stop application and, if applicable, stop emulator
            self.instance.close(clean=True)
            if self.instance.unresponsive_count >= 3:
                raise errors.UnresponsiveInstanceException(
                    "Application clean-up has failed >2 consecutive times."
                )

        self.cleanup_ran = True

    def __del__(self):
        if not self.cleanup_ran:
            self.cleanup()

    @staticmethod
    def check_port_available(port, host=""):
        """Check if "host:port" is available.

        Raise socket.error if port is not available.
        """
        port = int(port)
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            s.bind((host, port))
            port = s.getsockname()[1]
        finally:
            s.close()
            return port

    def raise_for_port(self, timeout=None, check_process_status=True):
        """Raise socket.timeout if no connection can be established.

        :param timeout: Optional timeout in seconds for the server to be ready.
        :param check_process_status: Optional, if `True` the process will be
            continuously checked if it has exited, and the connection
            attempt will be aborted.
        """
        if timeout is None:
            timeout = self.startup_timeout

        runner = None
        if self.instance is not None:
            runner = self.instance.runner

        poll_interval = 0.1
        starttime = datetime.datetime.now()
        timeout_time = starttime + datetime.timedelta(seconds=timeout)

        client = transport.TcpTransport(self.host, self.port, 0.5)

        connected = False
        while datetime.datetime.now() < timeout_time:
            # If the instance we want to connect to is not running return immediately
            if check_process_status and runner is not None and not runner.is_running():
                break

            try:
                client.connect()
                return True
            except OSError:
                pass
            finally:
                client.close()

            time.sleep(poll_interval)

        if not connected:
            # There might have been a startup crash of the application
            if runner is not None and self.check_for_crash() > 0:
                raise OSError(f"Process crashed (Exit code: {runner.wait(0)})")

            raise socket.timeout(
                f"Timed out waiting for connection on {self.host}:{self.port}!"
            )

    @do_process_check
    def _send_message(self, name, params=None, key=None):
        """Send a blocking message to the server.

        Marionette provides an asynchronous, non-blocking interface and
        this attempts to paper over this by providing a synchronous API
        to the user.

        :param name: Requested command key.
        :param params: Optional dictionary of key/value arguments.
        :param key: Optional key to extract from response.

        :returns: Full response from the server, or if `key` is given,
            the value of said key in the response.
        """
        if not self.session_id and name != "WebDriver:NewSession":
            raise errors.InvalidSessionIdException("Please start a session")

        try:
            msg = self.client.request(name, params)
        except OSError:
            self.delete_session(send_request=False)
            raise

        res, err = msg.result, msg.error
        if err:
            self._handle_error(err)

        if key is not None:
            return self._from_json(res.get(key))
        else:
            return self._from_json(res)

    def _handle_error(self, obj):
        error = obj["error"]
        message = obj["message"]
        stacktrace = obj["stacktrace"]

        raise errors.lookup(error)(message, stacktrace=stacktrace)

    def check_for_crash(self):
        """Check if the process crashed.

        :returns: True, if a crash happened since the method has been called the last time.
        """
        crash_count = 0

        if self.instance:
            crash_count = self.instance.runner.check_for_crashes(
                test_name=self.test_name or "marionette.py", group=self.test_group
            )
            self.crashed = self.crashed + crash_count

        return crash_count > 0

    def _handle_socket_failure(self):
        """Handle socket failures for the currently connected application.

        If the application crashed then clean-up internal states, or in case of a content
        crash also kill the process. If there are other reasons for a socket failure,
        wait for the process to shutdown itself, or force kill it.

        Please note that the method expects an exception to be handled on the current stack
        frame, and is only called via the `@do_process_check` decorator.

        """
        exc_cls, exc, tb = sys.exc_info()

        # If the application hasn't been launched by Marionette no further action can be done.
        # In such cases we simply re-throw the exception.
        if not self.instance:
            raise exc.with_traceback(tb)

        else:
            # Somehow the socket disconnected. Give the application some time to shutdown
            # itself before killing the process.
            returncode = self.instance.runner.wait(timeout=self.shutdown_timeout)

            if returncode is None:
                message = (
                    "Process killed because the connection to Marionette server is "
                    "lost. Check gecko.log for errors"
                )
                # This will force-close the application without sending any other message.
                self.cleanup()
            else:
                # If Firefox quit itself check if there was a crash
                crash_count = self.check_for_crash()

                if crash_count > 0:
                    # SIGUSR1 indicates a forced shutdown due to a content process crash
                    if returncode == 245:
                        message = "Content process crashed"
                    else:
                        message = "Process crashed (Exit code: {returncode})"
                else:
                    message = (
                        "Process has been unexpectedly closed (Exit code: {returncode})"
                    )

                self.delete_session(send_request=False)

            message += " (Reason: {reason})"

            raise OSError(
                message.format(returncode=returncode, reason=exc)
            ).with_traceback(tb)

    @staticmethod
    def convert_keys(*string):
        typing = []
        for val in string:
            if isinstance(val, Keys):
                typing.append(val)
            elif isinstance(val, int):
                val = str(val)
                for i in range(len(val)):
                    typing.append(val[i])
            else:
                for i in range(len(val)):
                    typing.append(val[i])
        return "".join(typing)

    def clear_pref(self, pref):
        """Clear the user-defined value from the specified preference.

        :param pref: Name of the preference.
        """
        with self.using_context(self.CONTEXT_CHROME):
            self.execute_script(
                """
               const { Preferences } = ChromeUtils.importESModule(
                 "resource://gre/modules/Preferences.sys.mjs"
               );
               Preferences.reset(arguments[0]);
               """,
                script_args=(pref,),
            )

    def get_pref(self, pref, default_branch=False, value_type="unspecified"):
        """Get the value of the specified preference.

        :param pref: Name of the preference.
        :param default_branch: Optional, if `True` the preference value will be read
                               from the default branch. Otherwise the user-defined
                               value if set is returned. Defaults to `False`.
        :param value_type: Optional, XPCOM interface of the pref's complex value.
                           Possible values are: `nsIFile` and
                           `nsIPrefLocalizedString`.

        Usage example::

            marionette.get_pref("browser.tabs.warnOnClose")

        """
        with self.using_context(self.CONTEXT_CHROME):
            pref_value = self.execute_script(
                """
                const { Preferences } = ChromeUtils.importESModule(
                  "resource://gre/modules/Preferences.sys.mjs"
                );

                let pref = arguments[0];
                let defaultBranch = arguments[1];
                let valueType = arguments[2];

                prefs = new Preferences({defaultBranch: defaultBranch});
                return prefs.get(pref, null, Components.interfaces[valueType]);
                """,
                script_args=(pref, default_branch, value_type),
            )
            return pref_value

    def set_pref(self, pref, value, default_branch=False):
        """Set the value of the specified preference.

        :param pref: Name of the preference.
        :param value: The value to set the preference to. If the value is None,
                      reset the preference to its default value. If no default
                      value exists, the preference will cease to exist.
        :param default_branch: Optional, if `True` the preference value will
                       be written to the default branch, and will remain until
                       the application gets restarted. Otherwise a user-defined
                       value is set. Defaults to `False`.

        Usage example::

            marionette.set_pref("browser.tabs.warnOnClose", True)

        """
        with self.using_context(self.CONTEXT_CHROME):
            if value is None:
                self.clear_pref(pref)
                return

            self.execute_script(
                """
                const { Preferences } = ChromeUtils.importESModule(
                  "resource://gre/modules/Preferences.sys.mjs"
                );

                let pref = arguments[0];
                let value = arguments[1];
                let defaultBranch = arguments[2];

                prefs = new Preferences({defaultBranch: defaultBranch});
                prefs.set(pref, value);
                """,
                script_args=(pref, value, default_branch),
            )

    def set_prefs(self, prefs, default_branch=False):
        """Set the value of a list of preferences.

        :param prefs: A dict containing one or more preferences and their values
                      to be set. See :func:`set_pref` for further details.
        :param default_branch: Optional, if `True` the preference value will
                       be written to the default branch, and will remain until
                       the application gets restarted. Otherwise a user-defined
                       value is set. Defaults to `False`.

        Usage example::

            marionette.set_prefs({"browser.tabs.warnOnClose": True})

        """
        for pref, value in prefs.items():
            self.set_pref(pref, value, default_branch=default_branch)

    @contextmanager
    def using_prefs(self, prefs, default_branch=False):
        """Set preferences for code executed in a `with` block, and restores them on exit.

        :param prefs: A dict containing one or more preferences and their values
                      to be set. See :func:`set_prefs` for further details.
        :param default_branch: Optional, if `True` the preference value will
                       be written to the default branch, and will remain until
                       the application gets restarted. Otherwise a user-defined
                       value is set. Defaults to `False`.

        Usage example::

            with marionette.using_prefs({"browser.tabs.warnOnClose": True}):
                # ... do stuff ...

        """
        original_prefs = {p: self.get_pref(p) for p in prefs}
        self.set_prefs(prefs, default_branch=default_branch)

        try:
            yield
        finally:
            self.set_prefs(original_prefs, default_branch=default_branch)

    @do_process_check
    def enforce_gecko_prefs(self, prefs):
        """Checks if the running instance has the given prefs. If not,
        it will kill the currently running instance, and spawn a new
        instance with the requested preferences.

        :param prefs: A dictionary whose keys are preference names.
        """
        if not self.instance:
            raise errors.MarionetteException(
                "enforce_gecko_prefs() can only be called "
                "on Gecko instances launched by Marionette"
            )
        pref_exists = True
        with self.using_context(self.CONTEXT_CHROME):
            for pref, value in prefs.items():
                if type(value) is not str:
                    value = json.dumps(value)
                pref_exists = self.execute_script(
                    f"""
                let prefInterface = Components.classes["@mozilla.org/preferences-service;1"]
                                              .getService(Components.interfaces.nsIPrefBranch);
                let pref = '{pref}';
                let value = '{value}';
                let type = prefInterface.getPrefType(pref);
                switch(type) {{
                    case prefInterface.PREF_STRING:
                        return value == prefInterface.getCharPref(pref).toString();
                    case prefInterface.PREF_BOOL:
                        return value == prefInterface.getBoolPref(pref).toString();
                    case prefInterface.PREF_INT:
                        return value == prefInterface.getIntPref(pref).toString();
                    case prefInterface.PREF_INVALID:
                        return false;
                }}
                """
                )
                if not pref_exists:
                    break

        if not pref_exists:
            context = self._send_message("Marionette:GetContext", key="value")
            self.delete_session()
            self.instance.restart(prefs)
            self.raise_for_port()
            self.start_session(self.requested_capabilities)

            # Restore the context as used before the restart
            self.set_context(context)

    def _request_in_app_shutdown(self, flags=None, safe_mode=False):
        """Attempt to quit the currently running instance from inside the
        application. If shutdown is prevented by some component the quit
        will be forced.

        This method effectively calls `Services.startup.quit` in Gecko.
        Possible flag values are listed at https://bit.ly/3IYcjYi.

        :param flags: Optional additional quit masks to include.

        :param safe_mode: Optional flag to indicate that the application has to
            be restarted in safe mode.

        :returns: A dictionary containing details of the application shutdown.
                  The `cause` property reflects the reason, and `forced` indicates
                  that something prevented the shutdown and the application had
                  to be forced to shutdown.

        :throws InvalidArgumentException: If there are multiple
            `shutdown_flags` ending with `"Quit"`.
        """
        body = {}
        if flags is not None:
            body["flags"] = list(
                flags,
            )
        if safe_mode:
            body["safeMode"] = safe_mode

        return self._send_message("Marionette:Quit", body)

    @do_process_check
    def quit(self, clean=False, in_app=True, callback=None):
        """
        By default this method will trigger a normal shutdown of the currently running instance.
        But it can also be used to force terminate the process.

        This command will delete the active marionette session. It also allows
        manipulation of eg. the profile data while the application is not running.
        To start the application again, :func:`start_session` has to be called.

        :param clean: If True a new profile will be used after the next start of
                      the application. Note that the in_app initiated quit always
                      maintains the same profile.

        :param in_app: If True, marionette will cause a quit from within the
                       application. Otherwise the application will be restarted
                       immediately by killing the process.

        :param callback: If provided and `in_app` is True, the callback will
                         be used to trigger the shutdown.

        :returns: A dictionary containing details of the application shutdown.
                  The `cause` property reflects the reason, and `forced` indicates
                  that something prevented the shutdown and the application had
                  to be forced to shutdown.
        """
        if not self.instance:
            raise errors.MarionetteException(
                "quit() can only be called on Gecko instances launched by Marionette"
            )

        quit_details = {"cause": "shutdown", "forced": False}

        if in_app:
            if clean:
                raise ValueError(
                    "An in_app restart cannot be triggered with the clean flag set"
                )

            if callback is not None and not callable(callback):
                raise ValueError(f"Specified callback '{callback}' is not callable")

            # Block Marionette from accepting new connections
            self._send_message("Marionette:AcceptConnections", {"value": False})

            try:
                self.is_shutting_down = True
                if callback is not None:
                    callback()
                    quit_details["in_app"] = True
                else:
                    quit_details = self._request_in_app_shutdown()

            except OSError:
                # A possible IOError should be ignored at this point, given that
                # quit() could have been called inside of `using_context`,
                # which wants to reset the context but fails sending the message.
                pass

            except Exception:
                # For any other error assume the application is not going to shutdown.
                # As such allow Marionette to accept new connections again.
                self.is_shutting_down = False
                self._send_message("Marionette:AcceptConnections", {"value": True})
                raise

            try:
                self.delete_session(send_request=False)

                # Try to wait for the process to end itself before force-closing it.
                returncode = self.instance.runner.wait(timeout=self.shutdown_timeout)
                if returncode is None:
                    self.cleanup()

                    message = "Process still running {}s after quit request"
                    raise OSError(message.format(self.shutdown_timeout))

            finally:
                self.is_shutting_down = False

        else:
            self.delete_session(send_request=False)
            self.instance.close(clean=clean)

            quit_details.update({"in_app": False, "forced": True})

        if quit_details.get("cause") not in (None, "shutdown"):
            raise errors.MarionetteException(
                "Unexpected shutdown reason '{}' for quitting the process.".format(
                    quit_details["cause"]
                )
            )

        return quit_details

    @do_process_check
    def restart(
        self, callback=None, clean=False, in_app=True, safe_mode=False, silent=False
    ):
        """
        By default this method will restart the currently running instance by using the same
        profile. But it can also be forced to terminate the currently running instance, and
        to spawn a new instance with the same or different profile.

        :param callback: If provided and `in_app` is True, the callback will be
                         used to trigger the restart.

        :param clean: If True a new profile will be used after the restart. Note
                      that the in_app initiated restart always maintains the same
                      profile.

        :param in_app: If True, marionette will cause a restart from within the
                       application. Otherwise the application will be restarted
                       immediately by killing the process.

        :param safe_mode: Optional flag to indicate that the application has to
            be restarted in safe mode.

        :param silent: Optional flag to indicate that the application should
            not open any window after a restart. Note that this flag is only
            supported on MacOS and requires "in_app" to be True.

        :returns: A dictionary containing details of the application restart.
                  The `cause` property reflects the reason, and `forced` indicates
                  that something prevented the shutdown and the application had
                  to be forced to shutdown.
        """
        if not self.instance:
            raise errors.MarionetteException(
                "restart() can only be called on Gecko instances launched by Marionette"
            )

        context = self._send_message("Marionette:GetContext", key="value")
        restart_details = {"cause": "restart", "forced": False}

        # Safe mode and the silent flag require an in_app restart.
        if (safe_mode or silent) and not in_app:
            raise ValueError("An in_app restart is required for safe or silent mode")

        if in_app:
            if clean:
                raise ValueError(
                    "An in_app restart cannot be triggered with the clean flag set"
                )

            if callback is not None and not callable(callback):
                raise ValueError(f"Specified callback '{callback}' is not callable")

            # Block Marionette from accepting new connections
            self._send_message("Marionette:AcceptConnections", {"value": False})

            try:
                self.is_shutting_down = True
                if callback is not None:
                    callback()
                    restart_details["in_app"] = True
                else:
                    flags = ["eRestart"]
                    if silent:
                        flags.append("eSilently")

                    try:
                        restart_details = self._request_in_app_shutdown(
                            flags=flags, safe_mode=safe_mode
                        )
                    except Exception as e:
                        self._send_message(
                            "Marionette:AcceptConnections", {"value": True}
                        )
                        raise e

            except OSError:
                # A possible IOError should be ignored at this point, given that
                # restart() could have been called inside of `using_context`,
                # which wants to reset the context but fails sending the message.
                pass

            timeout_restart = self.shutdown_timeout + self.startup_timeout
            try:
                # Wait for a new Marionette connection to appear while the
                # process restarts itself.
                self.raise_for_port(timeout=timeout_restart, check_process_status=False)
            except socket.timeout:
                exc_cls, _, tb = sys.exc_info()

                if self.instance.runner.returncode is None:
                    self.is_shutting_down = False

                    # The process is still running, which means the shutdown
                    # request was not correct or the application ignored it.
                    # Allow Marionette to accept connections again.
                    self._send_message("Marionette:AcceptConnections", {"value": True})

                    message = "Process still running {}s after restart request"
                    raise exc_cls(message.format(timeout_restart)).with_traceback(tb)

                else:
                    # The process shutdown but didn't start again.
                    self.cleanup()
                    msg = "Process unexpectedly quit without restarting (exit code: {})"
                    raise exc_cls(
                        msg.format(self.instance.runner.returncode)
                    ).with_traceback(tb)

            self.is_shutting_down = False

            # Create a new session to retrieve the new process id of the application
            self.delete_session(send_request=False)

        else:
            self.delete_session()
            self.instance.restart(clean=clean)

            self.raise_for_port(timeout=self.DEFAULT_STARTUP_TIMEOUT)

            restart_details.update({"in_app": False, "forced": True})

        self.start_session(self.requested_capabilities, process_forked=in_app)
        # Restore the context as used before the restart
        self.set_context(context)

        if restart_details.get("cause") not in (None, "restart"):
            raise errors.MarionetteException(
                "Unexpected shutdown reason '{}' for restarting the process".format(
                    restart_details["cause"]
                )
            )

        return restart_details

    def absolute_url(self, relative_url):
        """
        Returns an absolute url for files served from Marionette's www directory.

        :param relative_url: The url of a static file, relative to Marionette's www directory.
        """
        return f"{self.baseurl}{relative_url}"

    @do_process_check
    def start_session(self, capabilities=None, process_forked=False, timeout=None):
        """Create a new WebDriver session.
        This method must be called before performing any other action.

        :param capabilities: An optional dictionary of
            Marionette-recognised capabilities.  It does not
            accept a WebDriver conforming capabilities dictionary
            (including alwaysMatch, firstMatch, desiredCapabilities,
            or requriedCapabilities), and only recognises extension
            capabilities that are specific to Marionette.
        :param process_forked: If True, the existing process forked itself due
            to an internal restart.
        :param timeout: Optional timeout in seconds for the server to be ready.

        :returns: A dictionary of the capabilities offered.
        """
        if capabilities is None:
            capabilities = {"strictFileInteractability": True}
        self.requested_capabilities = capabilities

        if timeout is None:
            timeout = self.startup_timeout

        self.crashed = 0

        if not process_forked:
            # Only handle the binary if there was no process before which also
            # might have forked itself due to a restart
            if self.instance:
                returncode = self.instance.runner.returncode
                # We're managing a binary which has terminated. Start it again
                # and implicitely wait for the Marionette server to be ready.
                if returncode is not None:
                    self.start_binary(timeout)

            else:
                # In the case when Marionette doesn't manage the binary wait until
                # its server component has been started.
                self.raise_for_port(timeout=timeout)

        self.client = transport.TcpTransport(self.host, self.port, self.socket_timeout)
        self.protocol, _ = self.client.connect()

        try:
            resp = self._send_message("WebDriver:NewSession", capabilities)
        except errors.UnknownException:
            # Force closing the managed process when the session cannot be
            # created due to global JavaScript errors.
            exc_type, value, tb = sys.exc_info()
            if self.instance and self.instance.runner.is_running():
                self.instance.close()
            raise exc_type(value.message).with_traceback(tb)

        self.session_id = resp["sessionId"]
        self.session = resp["capabilities"]
        self.cleanup_ran = False

        self.process_id = self.session.get("moz:processID")
        if process_forked:
            self.instance.update_process(self.process_id, self.shutdown_timeout)

        self.profile = self.session.get("moz:profile")

        timeout = self.session.get("moz:shutdownTimeout")
        if timeout is not None:
            # pylint --py3k W1619
            self.shutdown_timeout = timeout / 1000 + 10

        return self.session

    @property
    def test_name(self):
        return self._test_name

    @test_name.setter
    def test_name(self, test_name):
        self._test_name = test_name

    @property
    def test_group(self):
        """Group (e.g. manifest) of the tests being run, crashes are attributed to it."""
        return self._test_group

    @test_group.setter
    def test_group(self, test_group):
        self._test_group = test_group

    def delete_session(self, send_request=True):
        """Close the current session and disconnect from the server.

        :param send_request: Optional, if `True` a request to close the session on
            the server side will be sent. Use `False` in case of eg. in_app restart()
            or quit(), which trigger a deletion themselves. Defaults to `True`.
        """
        try:
            if send_request:
                try:
                    self._send_message("WebDriver:DeleteSession")
                except errors.InvalidSessionIdException:
                    pass
        finally:
            self.process_id = None
            self.profile = None
            self.session = None
            self.session_id = None
            self.window = None

            if self.client is not None:
                self.client.close()

    @property
    def session_capabilities(self):
        """A JSON dictionary representing the capabilities of the
        current session.

        """
        return self.session

    @property
    def current_window_handle(self):
        """Get the current window's handle.

        Returns an opaque server-assigned identifier to this window
        that uniquely identifies it within this Marionette instance.
        This can be used to switch to this window at a later point.

        :returns: unique window handle
        :rtype: string
        """
        with self.using_context("content"):
            self.window = self._send_message("WebDriver:GetWindowHandle", key="value")

        return self.window

    @property
    def current_chrome_window_handle(self):
        """Get the current chrome window's handle. Corresponds to
        a chrome window that may itself contain tabs identified by
        window_handles.

        Returns an opaque server-assigned identifier to this window
        that uniquely identifies it within this Marionette instance.
        This can be used to switch to this window at a later point.

        :returns: unique window handle
        :rtype: string
        """
        with self.using_context("chrome"):
            self.chrome_window = self._send_message(
                "WebDriver:GetWindowHandle", key="value"
            )

        return self.chrome_window

    def set_window_rect(self, x=None, y=None, height=None, width=None):
        """Set the position and size of the current window.

        The supplied width and height values refer to the window outerWidth
        and outerHeight values, which include scroll bars, title bars, etc.

        An error will be returned if the requested window size would result
        in the window being in the maximised state.

        :param x: x coordinate for the top left of the window
        :param y: y coordinate for the top left of the window
        :param width: The width to resize the window to.
        :param height: The height to resize the window to.
        """
        body = {"x": x, "y": y, "height": height, "width": width}
        return self._send_message("WebDriver:SetWindowRect", body)

    @property
    def window_rect(self):
        return self._send_message("WebDriver:GetWindowRect")

    @property
    def title(self):
        """Current title of the active window."""
        return self._send_message("WebDriver:GetTitle", key="value")

    @property
    def window_handles(self):
        """Get list of windows in the current context.

        If called in the content context it will return a list of
        references to all available browser windows.

        Each window handle is assigned by the server, and the list of
        strings returned does not have a guaranteed ordering.

        :returns: Unordered list of unique window handles as strings
        """
        with self.using_context("content"):
            return self._send_message("WebDriver:GetWindowHandles")

    @property
    def chrome_window_handles(self):
        """Get a list of currently open chrome windows.

        Each window handle is assigned by the server, and the list of
        strings returned does not have a guaranteed ordering.

        :returns: Unordered list of unique chrome window handles as strings
        """
        with self.using_context("chrome"):
            return self._send_message("WebDriver:GetWindowHandles")

    @property
    def page_source(self):
        """A string representation of the DOM."""
        return self._send_message("WebDriver:GetPageSource", key="value")

    def open(self, type=None, focus=False, private=False):
        """Open a new window, or tab based on the specified context type.

        If no context type is given the application will choose the best
        option based on tab and window support.

        :param type: Type of window to be opened. Can be one of "tab" or "window"
        :param focus: If true, the opened window will be focused
        :param private: If true, open a private window

        :returns: Dict with new window handle, and type of opened window
        """
        body = {"type": type, "focus": focus, "private": private}
        return self._send_message("WebDriver:NewWindow", body)

    def close(self):
        """Close the current window, ending the session if it's the last
        window currently open.

        :returns: Unordered list of remaining unique window handles as strings
        """
        return self._send_message("WebDriver:CloseWindow")

    def close_chrome_window(self):
        """Close the currently selected chrome window, ending the session
        if it's the last window open.

        :returns: Unordered list of remaining unique chrome window handles as strings
        """
        return self._send_message("WebDriver:CloseChromeWindow")

    def register_chrome_handler(self, manifestPath, entries):
        """Register a chrome protocol handler.

        :param manifestPath: Path to the chrome manifest file
        :param entries:  Chrome entries to register.

        `entries` is an array of arrays, each containing a registry entry
        (type, namespace, path, options) as it would appar in a chrome.manifest
        file. Only the following entry types are currently accepted:

            - "content" A URL entry. Must be a 3-element array.
            - "override" A URL override entry. Must be a 3-element array.
            - "locale" A locale package entry. Must be a 4-element array.

        :returns: id of the registered chrome handler
        """
        return self._send_message(
            "Marionette:RegisterChromeHandler",
            {
                "manifestPath": manifestPath,
                "entries": entries,
            },
            key="value",
        )

    def unregister_chrome_handler(self, id):
        """Unregister a previous registered chrome protocol handler."""
        self._send_message("Marionette:UnregisterChromeHandler", {"id": id})

    def set_context(self, context):
        """Sets the context that Marionette commands are running in.

        :param context: Context, may be one of the class properties
            `CONTEXT_CHROME` or `CONTEXT_CONTENT`.

        Usage example::

            marionette.set_context(marionette.CONTEXT_CHROME)
        """
        if context not in [self.CONTEXT_CHROME, self.CONTEXT_CONTENT]:
            raise ValueError(f"Unknown context: {context}")

        self._send_message("Marionette:SetContext", {"value": context})

    @contextmanager
    def using_context(self, context):
        """Sets the context that Marionette commands are running in using
        a `with` statement. The state of the context on the server is
        saved before entering the block, and restored upon exiting it.

        :param context: Context, may be one of the class properties
            `CONTEXT_CHROME` or `CONTEXT_CONTENT`.

        Usage example::

            with marionette.using_context(marionette.CONTEXT_CHROME):
                # chrome scope
                ... do stuff ...
        """
        scope = self._send_message("Marionette:GetContext", key="value")
        self.set_context(context)
        try:
            yield
        finally:
            self.set_context(scope)

    def switch_to_alert(self):
        """Returns an :class:`~marionette_driver.marionette.Alert` object for
        interacting with a currently displayed alert.

        ::

            alert = self.marionette.switch_to_alert()
            text = alert.text
            alert.accept()
        """
        return Alert(self)

    def switch_to_window(self, handle, focus=True):
        """Switch to the specified window; subsequent commands will be
        directed at the new window.

        :param handle: The id of the window to switch to.

        :param focus: A boolean value which determins whether to focus
            the window that we just switched to.
        """
        self._send_message(
            "WebDriver:SwitchToWindow", {"handle": handle, "focus": focus}
        )
        self.window = handle

    def switch_to_default_content(self):
        """Switch the current context to page's default content."""
        return self.switch_to_frame()

    def switch_to_parent_frame(self):
        """
        Switch to the Parent Frame
        """
        self._send_message("WebDriver:SwitchToParentFrame")

    def switch_to_frame(self, frame=None):
        """Switch the current context to the specified frame. Subsequent
        commands will operate in the context of the specified frame,
        if applicable.

        :param frame: A reference to the frame to switch to.  This can
            be an :class:`~marionette_driver.marionette.WebElement`,
            or an integer index. If you call ``switch_to_frame`` without an
            argument, it will switch to the top-level frame.
        """
        body = {}
        if isinstance(frame, WebElement):
            body["element"] = frame.id
        elif frame is not None:
            body["id"] = frame

        self._send_message("WebDriver:SwitchToFrame", body)

    def get_url(self):
        """Get a string representing the current URL.

        On Desktop this returns a string representation of the URL of
        the current top level browsing context.  This is equivalent to
        document.location.href.

        When in the context of the chrome, this returns the canonical
        URL of the current resource.

        :returns: string representation of URL
        """
        return self._send_message("WebDriver:GetCurrentURL", key="value")

    def get_window_type(self):
        """Gets the windowtype attribute of the window Marionette is
        currently acting on.

        This command only makes sense in a chrome context. You might use this
        method to distinguish a browser window from an editor window.
        """
        try:
            return self._send_message("Marionette:GetWindowType", key="value")
        except errors.UnknownCommandException:
            return self._send_message("getWindowType", key="value")

    def navigate(self, url):
        """Navigate to given `url`.

        Navigates the current top-level browsing context's content
        frame to the given URL and waits for the document to load or
        the session's page timeout duration to elapse before returning.

        The command will return with a failure if there is an error
        loading the document or the URL is blocked.  This can occur if
        it fails to reach the host, the URL is malformed, the page is
        restricted (about:* pages), or if there is a certificate issue
        to name some examples.

        The document is considered successfully loaded when the
        `DOMContentLoaded` event on the frame element associated with the
        `window` triggers and `document.readyState` is "complete".

        In chrome context it will change the current `window`'s location
        to the supplied URL and wait until `document.readyState` equals
        "complete" or the page timeout duration has elapsed.

        :param url: The URL to navigate to.
        """
        self._send_message("WebDriver:Navigate", {"url": url})

    def go_back(self):
        """Causes the browser to perform a back navigation."""
        self._send_message("WebDriver:Back")

    def go_forward(self):
        """Causes the browser to perform a forward navigation."""
        self._send_message("WebDriver:Forward")

    def refresh(self):
        """Causes the browser to perform to refresh the current page."""
        self._send_message("WebDriver:Refresh")

    def _to_json(self, args):
        if isinstance(args, (list, tuple)):
            wrapped = []
            for arg in args:
                wrapped.append(self._to_json(arg))
        elif isinstance(args, dict):
            wrapped = {}
            for arg in args:
                wrapped[arg] = self._to_json(args[arg])
        elif type(args) is WebElement:
            wrapped = {WEB_ELEMENT_KEY: args.id}
        elif type(args) is ShadowRoot:
            wrapped = {WEB_SHADOW_ROOT_KEY: args.id}
        elif type(args) is WebFrame:
            wrapped = {WEB_FRAME_KEY: args.id}
        elif type(args) is WebWindow:
            wrapped = {WEB_WINDOW_KEY: args.id}
        elif isinstance(args, (bool, int, float, str)) or args is None:
            wrapped = args
        return wrapped

    def _from_json(self, value):
        if isinstance(value, dict) and any(
            k in value.keys() for k in WebElement.identifiers
        ):
            return WebElement._from_json(value, self)
        elif isinstance(value, dict) and any(
            k in value.keys() for k in ShadowRoot.identifiers
        ):
            return ShadowRoot._from_json(value, self)
        elif isinstance(value, dict) and any(
            k in value.keys() for k in WebFrame.identifiers
        ):
            return WebFrame._from_json(value, self)
        elif isinstance(value, dict) and any(
            k in value.keys() for k in WebWindow.identifiers
        ):
            return WebWindow._from_json(value, self)
        elif isinstance(value, dict):
            return {key: self._from_json(val) for key, val in value.items()}
        elif isinstance(value, list):
            return list(self._from_json(item) for item in value)
        else:
            return value

    def execute_script(
        self,
        script,
        script_args=(),
        new_sandbox=True,
        sandbox="default",
        script_timeout=None,
    ):
        """Executes a synchronous JavaScript script, and returns the
        result (or None if the script does return a value).

        The script is executed in the context set by the most recent
        :func:`set_context` call, or to the CONTEXT_CONTENT context if
        :func:`set_context` has not been called.

        :param script: A string containing the JavaScript to execute.
        :param script_args: An interable of arguments to pass to the script.
        :param new_sandbox: If False, preserve global variables from
            the last execute_*script call. This is True by default, in which
            case no globals are preserved.
        :param sandbox: A tag referring to the sandbox you wish to use;
            if you specify a new tag, a new sandbox will be created.
            If you use the special tag `system`, the sandbox will
            be created using the system principal which has elevated
            privileges.
        :param script_timeout: Timeout in milliseconds, overriding
            the session's default script timeout.

        Simple usage example:

        ::

            result = marionette.execute_script("return 1;")
            assert result == 1

        You can use the `script_args` parameter to pass arguments to the
        script:

        ::

            result = marionette.execute_script(
                "return arguments[0] + arguments[1];",
                script_args=(
                    2,
                    3,
                ),
            )
            assert result == 5
            some_element = marionette.find_element(By.ID, "someElement")
            sid = marionette.execute_script(
                "return arguments[0].id;", script_args=(some_element,)
            )
            assert some_element.get_attribute("id") == sid

        Scripts wishing to access non-standard properties of the window
        object must use window.wrappedJSObject:

        ::

            result = marionette.execute_script('''
              window.wrappedJSObject.test1 = "foo";
              window.wrappedJSObject.test2 = "bar";
              return window.wrappedJSObject.test1 + window.wrappedJSObject.test2;
              ''')
            assert result == "foobar"

        Global variables set by individual scripts do not persist between
        script calls by default.  If you wish to persist data between
        script calls, you can set `new_sandbox` to False on your next call,
        and add any new variables to a new 'global' object like this:

        ::

            marionette.execute_script("global.test1 = 'foo';")
            result = self.marionette.execute_script(
                "return global.test1;", new_sandbox=False
            )
            assert result == "foo"

        """
        original_timeout = None
        if script_timeout is not None:
            original_timeout = self.timeout.script
            self.timeout.script = script_timeout / 1000.0

        try:
            args = self._to_json(script_args)
            stack = traceback.extract_stack()
            frame = stack[-2:-1][0]  # grab the second-to-last frame
            filename = (
                frame[0] if sys.platform == "win32" else os.path.relpath(frame[0])
            )
            body = {
                "script": script.strip(),
                "args": args,
                "newSandbox": new_sandbox,
                "sandbox": sandbox,
                "line": int(frame[1]),
                "filename": filename,
            }
            rv = self._send_message("WebDriver:ExecuteScript", body, key="value")

        finally:
            if script_timeout is not None:
                self.timeout.script = original_timeout

        return rv

    def execute_async_script(
        self,
        script,
        script_args=(),
        new_sandbox=True,
        sandbox="default",
        script_timeout=None,
    ):
        """Executes an asynchronous JavaScript script, and returns the
        result (or None if the script does return a value).

        The script is executed in the context set by the most recent
        :func:`set_context` call, or to the CONTEXT_CONTENT context if
        :func:`set_context` has not been called.

        :param script: A string containing the JavaScript to execute.
        :param script_args: An interable of arguments to pass to the script.
        :param new_sandbox: If False, preserve global variables from
            the last execute_*script call. This is True by default,
            in which case no globals are preserved.
        :param sandbox: A tag referring to the sandbox you wish to use; if
            you specify a new tag, a new sandbox will be created.  If you
            use the special tag `system`, the sandbox will be created
            using the system principal which has elevated privileges.
        :param script_timeout: Timeout in milliseconds, overriding
            the session's default script timeout.

        Usage example:

        ::

            marionette.timeout.script = 10
            result = self.marionette.execute_async_script('''
              // this script waits 5 seconds, and then returns the number 1
              let [resolve] = arguments;
              setTimeout(function() {
                resolve(1);
              }, 5000);
            ''')
            assert result == 1
        """
        original_timeout = None
        if script_timeout is not None:
            original_timeout = self.timeout.script
            self.timeout.script = script_timeout / 1000.0

        try:
            args = self._to_json(script_args)
            stack = traceback.extract_stack()
            frame = stack[-2:-1][0]  # grab the second-to-last frame
            filename = (
                frame[0] if sys.platform == "win32" else os.path.relpath(frame[0])
            )
            body = {
                "script": script.strip(),
                "args": args,
                "newSandbox": new_sandbox,
                "sandbox": sandbox,
                "scriptTimeout": script_timeout,
                "line": int(frame[1]),
                "filename": filename,
            }
            rv = self._send_message("WebDriver:ExecuteAsyncScript", body, key="value")

        finally:
            if script_timeout is not None:
                self.timeout.script = original_timeout

        return rv

    def find_element(self, method, target, id=None):
        """Returns an :class:`~marionette_driver.marionette.WebElement`
        instance that matches the specified method and target in the current
        context.

        An :class:`~marionette_driver.marionette.WebElement` instance may be
        used to call other methods on the element, such as
        :func:`~marionette_driver.marionette.WebElement.click`.  If no element
        is immediately found, the attempt to locate an element will be repeated
        for up to the amount of time set by
        :attr:`marionette_driver.timeout.Timeouts.implicit`. If multiple
        elements match the given criteria, only the first is returned. If no
        element matches, a ``NoSuchElementException`` will be raised.

        :param method: The method to use to locate the element; one of:
            "id", "name", "class name", "tag name", "css selector",
            "link text", "partial link text" and "xpath".
            Note that the "name", "link text" and "partial link test"
            methods are not supported in the chrome DOM.
        :param target: The target of the search.  For example, if method =
            "tag", target might equal "div".  If method = "id", target would
            be an element id.
        :param id: If specified, search for elements only inside the element
            with the specified id.
        """
        body = {"value": target, "using": method}
        if id:
            body["element"] = id

        return self._send_message("WebDriver:FindElement", body, key="value")

    def find_elements(self, method, target, id=None):
        """Returns a list of all
        :class:`~marionette_driver.marionette.WebElement` instances that match
        the specified method and target in the current context.

        An :class:`~marionette_driver.marionette.WebElement` instance may be
        used to call other methods on the element, such as
        :func:`~marionette_driver.marionette.WebElement.click`.  If no element
        is immediately found, the attempt to locate an element will be repeated
        for up to the amount of time set by
        :attr:`marionette_driver.timeout.Timeouts.implicit`.

        :param method: The method to use to locate the elements; one
            of: "id", "name", "class name", "tag name", "css selector",
            "link text", "partial link text" and "xpath".
            Note that the "name", "link text" and "partial link test"
            methods are not supported in the chrome DOM.
        :param target: The target of the search.  For example, if method =
            "tag", target might equal "div".  If method = "id", target would be
            an element id.
        :param id: If specified, search for elements only inside the element
            with the specified id.
        """
        body = {"value": target, "using": method}
        if id:
            body["element"] = id

        return self._send_message("WebDriver:FindElements", body)

    def generate_test_report(self, message, group=None):
        """Generates a test report to be observed by registered reporting observers

        :param message: The message string to be used as the body of the generated report
        :param group: The name of the endpoint that will receive the report
        """
        body = {"message": message}
        if group is not None:
            body["group"] = group

        self._send_message("Reporting:GenerateTestReport", body)

    def get_active_element(self):
        el_or_ref = self._send_message("WebDriver:GetActiveElement", key="value")
        return el_or_ref

    def add_cookie(self, cookie):
        """Adds a cookie to your current session.

        :param cookie: A dictionary object, with required keys - "name"
            and "value"; optional keys - "path", "domain", "secure",
            "expiry".

        Usage example:

        ::

            driver.add_cookie({"name": "foo", "value": "bar"})
            driver.add_cookie({"name": "foo", "value": "bar", "path": "/"})
            driver.add_cookie({
                "name": "foo",
                "value": "bar",
                "path": "/",
                "secure": True,
            })
        """
        self._send_message("WebDriver:AddCookie", {"cookie": cookie})

    def delete_all_cookies(self):
        """Delete all cookies in the scope of the current session.

        Usage example:

        ::

            driver.delete_all_cookies()
        """
        self._send_message("WebDriver:DeleteAllCookies")

    def delete_cookie(self, name):
        """Delete a cookie by its name.

        :param name: Name of cookie to delete.

        Usage example:

        ::

            driver.delete_cookie("foo")
        """
        self._send_message("WebDriver:DeleteCookie", {"name": name})

    def get_cookie(self, name):
        """Get a single cookie by name. Returns the cookie if found,
        None if not.

        :param name: Name of cookie to get.
        """
        cookies = self.get_cookies()
        for cookie in cookies:
            if cookie["name"] == name:
                return cookie
        return None

    def get_cookies(self):
        """Get all the cookies for the current domain.

        This is the equivalent of calling `document.cookie` and
        parsing the result.

        :returns: A list of cookies for the current domain.
        """
        return self._send_message("WebDriver:GetCookies")

    def save_screenshot(self, fh, element=None, full=True, scroll=True, readback=False):
        """Takes a screenhot of a web element or the current frame and
        saves it in the filehandle.

        It is a wrapper around screenshot()
        :param fh: The filehandle to save the screenshot at.

        The rest of the parameters are defined like in screenshot()
        """
        data = self.screenshot(element, "binary", full, scroll, readback)
        fh.write(data)

    def screenshot(
        self, element=None, format="base64", full=True, scroll=True, readback=False
    ):
        """Takes a screenshot of a web element or the current frame.

        The screen capture is returned as a lossless PNG image encoded
        as a base 64 string by default. If the `element` argument is defined the
        capture area will be limited to the bounding box of that
        element.  Otherwise, the capture area will be the bounding box
        of the current frame.

        :param element: The element to take a screenshot of.  If None, will
            take a screenshot of the current frame.

        :param format: if "base64" (the default), returns the screenshot
            as a base64-string. If "binary", the data is decoded and
            returned as raw binary. If "hash", the data is hashed using
            the SHA-256 algorithm and the result is returned as a hex digest.

        :param full: If True (the default), the capture area will be the
            complete frame. Else only the viewport is captured. Only applies
            when `element` is None.

        :param scroll: When `element` is provided, scroll to it before
            taking the screenshot (default).  Otherwise, avoid scrolling
            `element` into view.

        :param readback: If True, capture the composited framebuffer via
            drawWindow instead of re-rendering the document with drawSnapshot.
            This matches what is actually rendered on screen (for example an
            in-progress view transition), but only returns what is currently
            composited (the foreground tab of the content area), so it is
            ignored for element, full-document, and chrome-scope captures and
            returns stale pixels for a background tab or an occluded window.
            Defaults to False.
        """

        if element:
            element = element.id

        body = {
            "id": element,
            "full": full,
            "hash": False,
            "scroll": scroll,
            "readback": readback,
        }
        if format == "hash":
            body["hash"] = True

        data = self._send_message("WebDriver:TakeScreenshot", body, key="value")

        if format in {"base64", "hash"}:
            return data
        elif format == "binary":
            return base64.b64decode(data.encode("ascii"))
        else:
            raise ValueError(
                "format parameter must be either 'base64'"
                f" or 'binary', not {repr(format)}"
            )

    @property
    def orientation(self):
        """Get the current browser orientation.

        Will return one of the valid primary orientation values
        portrait-primary, landscape-primary, portrait-secondary, or
        landscape-secondary.
        """
        try:
            return self._send_message("Marionette:GetScreenOrientation", key="value")
        except errors.UnknownCommandException:
            return self._send_message("getScreenOrientation", key="value")

    def set_orientation(self, orientation):
        """Set the current browser orientation.

        The supplied orientation should be given as one of the valid
        orientation values.  If the orientation is unknown, an error
        will be raised.

        Valid orientations are "portrait" and "landscape", which fall
        back to "portrait-primary" and "landscape-primary"
        respectively, and "portrait-secondary" as well as
        "landscape-secondary".

        :param orientation: The orientation to lock the screen in.
        """
        body = {"orientation": orientation}
        try:
            self._send_message("Marionette:SetScreenOrientation", body)
        except errors.UnknownCommandException:
            self._send_message("setScreenOrientation", body)

    def minimize_window(self):
        """Iconify the browser window currently receiving commands.
        The action should be equivalent to the user pressing the minimize
        button in the OS window.

        Note that this command is not available on Fennec.  It may also
        not be available in certain window managers.

        :returns Window rect.
        """
        return self._send_message("WebDriver:MinimizeWindow")

    def maximize_window(self):
        """Resize the browser window currently receiving commands.
        The action should be equivalent to the user pressing the maximize
        button in the OS window.


        Note that this command is not available on Fennec.  It may also
        not be available in certain window managers.

        :returns: Window rect.
        """
        return self._send_message("WebDriver:MaximizeWindow")

    def fullscreen(self):
        """Synchronously sets the user agent window to full screen as
        if the user had done "View > Enter Full Screen",  or restores
        it if it is already in full screen.

        :returns: Window rect.
        """
        return self._send_message("WebDriver:FullscreenWindow")

    def set_permission(self, descriptor, state):
        """Set the permission for the origin of the current page."""
        body = {
            "descriptor": descriptor,
            "state": state,
        }
        return self._send_message("WebDriver:SetPermission", body)

    def get_accessibility_properties_for_accessibility_node(self, id):
        """Gets the properties for the accessibility node with the given id."""
        return self._send_message(
            "Marionette:GetAccessibilityPropertiesForAccessibilityNode",
            {"id": id},
            key="value",
        )
