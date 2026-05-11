.. py:currentmodule:: marionette_driver.marionette

Marionette Python Client
========================

The Marionette Python client library allows you to remotely control a
Gecko-based browser or device which is running a Marionette_
server. This includes Firefox Desktop and Firefox for Android.

The Marionette server is built directly into Gecko and can be started by
passing in a command line option to Gecko, or by using a Marionette-enabled
build. The server listens for connections from various clients. Clients can
then control Gecko by sending commands to the server.

This is the official Python client for Marionette.

.. _Marionette: https://developer.mozilla.org/en-US/docs/Marionette

Getting the Client
------------------

The Python client is officially supported. To install it, first make sure you
have `pip installed`_ then run:

.. code-block:: bash

   $ pip install marionette_driver

It's highly recommended to use virtualenv_ when installing Marionette to avoid
package conflicts and other general nastiness.

You should now be ready to start using Marionette. The best way to learn is to
play around with it. Start a `Marionette-enabled instance of Firefox`_, fire up
a python shell and follow along with the
:doc:`interactive tutorial <interactive>`!

.. _pip installed: https://pip.pypa.io/en/latest/installing.html
.. _virtualenv: http://virtualenv.readthedocs.org/en/latest/
.. _Marionette-enabled instance of Firefox: https://developer.mozilla.org/en-US/docs/Mozilla/QA/Marionette/Builds

Using the Client for Testing
----------------------------

Please visit the `Marionette Tests`_ documentation for information regarding
testing with Marionette.

.. _Marionette Tests: https://firefox-source-docs.mozilla.org/testing/marionette/PythonTests.html

Session Management
------------------
A session is a single instance of a Marionette client connected to a Marionette
server. Before you can start executing commands, you need to start a session
with :func:`start_session() <Marionette.start_session>`:

.. code-block:: python

   from marionette_driver.marionette import Marionette

   client = Marionette('127.0.0.1', port=2828)
   client.start_session()

This returns a session id and an object listing the capabilities of the
Marionette server. For example, a server running on Firefox Desktop will
have some features which a server running from Firefox Android won't.
It's also possible to access the capabilities using the
:attr:`~Marionette.session_capabilities` attribute. After finishing with a
session, you can delete it with :func:`~Marionette.delete_session()`. Note that
this will also happen automatically when the Marionette object is garbage
collected.

Context Management
------------------
Commands can only be executed in a single window, frame and scope at a time. In
order to run commands elsewhere, it's necessary to explicitly switch to the
appropriate context.

Use :func:`~Marionette.switch_to_window` to execute commands in the context of a
new window:

.. code-block:: python

   original_window = client.current_window_handle
   for handle in client.window_handles:
       if handle != original_window:
           client.switch_to_window(handle)
           print("Switched to window with '{}' loaded.".format(client.get_url()))
   client.switch_to_window(original_window)

Similarly, use :func:`~Marionette.switch_to_frame` to execute commands in the
context of a new frame (e.g an <iframe> element):

.. code-block:: python

   iframe = client.find_element(By.TAG_NAME, 'iframe')
   client.switch_to_frame(iframe)

Finally Marionette can switch between `chrome` and `content` scope. Chrome is a
privileged scope where you can access things like the Firefox UI itself.
Content scope is where things like webpages live. You can switch between
`chrome` and `content` using the :func:`~Marionette.set_context` and :func:`~Marionette.using_context` functions:

.. code-block:: python

   client.set_context(client.CONTEXT_CONTENT)
   # content scope
   with client.using_context(client.CONTEXT_CHROME):
       #chrome scope
       pass  # ... do stuff ...
   # content scope restored


Navigation
----------

Use :func:`~Marionette.navigate` to open a new website. It's also possible to
move through the back/forward cache using :func:`~Marionette.go_forward` and
:func:`~Marionette.go_back` respectively. To retrieve the currently
open website, use :func:`~Marionette.get_url`:

.. code-block:: python

   url = 'http://mozilla.org'
   client.navigate(url)
   client.go_back()
   client.go_forward()
   assert client.get_url() == url


DOM Elements
------------

In order to inspect or manipulate actual DOM elements, they must first be found
using the :func:`~Marionette.find_element` or :func:`~Marionette.find_elements`
methods:

.. code-block:: python

   from marionette_driver.marionette import WebElement
   element = client.find_element(By.ID, 'my-id')
   assert type(element) == WebElement
   elements = client.find_elements(By.TAG_NAME, 'a')
   assert type(elements) == list

For a full list of valid search strategies, see :doc:`advanced/findelement`.

Now that an element has been found, it's possible to manipulate it:

.. code-block:: python

   element.click()
   element.send_keys('hello!')
   print(element.get_attribute('style'))

For the full list of possible commands, see the :class:`WebElement`
reference.

Be warned that a reference to an element object can become stale if it was
modified or removed from the document. See :doc:`advanced/stale` for tips
on working around this limitation.

Script Execution
----------------

Sometimes Marionette's provided APIs just aren't enough and it is necessary to
run arbitrary javascript. This is accomplished with the
:func:`~Marionette.execute_script` and :func:`~Marionette.execute_async_script`
functions. They accomplish what their names suggest, the former executes some
synchronous JavaScript, while the latter provides a callback mechanism for
running asynchronous JavaScript:

.. code-block:: python

   result = client.execute_script("return arguments[0] + arguments[1];",
                                  script_args=[2, 3])
   assert result == 5

The async method works the same way, except it won't return until the
`resolve()` function is called:

.. code-block:: python

   result = client.execute_async_script("""
       let [resolve] = arguments;
       setTimeout(function() {
         resolve("all done");
       }, arguments[0]);
   """, script_args=[1000])
   assert result == "all done"

Beware that running asynchronous scripts can potentially hang the program
indefinitely if they are not written properly. It is generally a good idea to
set a script timeout using :func:`~Marionette.timeout.script` and handling
`ScriptTimeoutException`.
