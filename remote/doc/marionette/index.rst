==========
Marionette
==========

Marionette is a remote `protocol`_ that lets out-of-process programs
communicate with, instrument, and control Gecko-based browsers.

It provides interfaces for interacting with both the internal JavaScript
runtime and UI elements of Gecko-based browsers, such as Firefox
and Fennec.  It can control both the chrome- and content documents,
giving a high level of control and ability to emulate user interaction.

Within the central tree, Marionette is used in most TaskCluster
test jobs to instrument Gecko.  It can additionally be used to
write different kinds of functional tests:

  * The `Marionette Python client`_ is used in the `Mn` job, which
    is generally what you want to use for interacting with web documents

Outside the tree, Marionette is used by `geckodriver`_ to implement
`WebDriver`_.

Marionette supports to various degrees all the Gecko based applications,
including Firefox, Thunderbird, Fennec, and Fenix.

.. _protocol: Protocol.html
.. _Marionette Python client: /python/marionette_driver.html
.. _geckodriver: /testing/geckodriver/
.. _WebDriver: https://w3c.github.io/webdriver/

Some further documentation can be found here:

.. toctree::
   :maxdepth: 1

   Intro.md
   Building.md
   PythonTests.md
   Protocol.md
   Contributing.md
   NewContributors.md
   Patches.md
   Debugging.md
   Testing.md
   Taskcluster.md
   CodeStyle.md
   SeleniumAtoms.md
   Prefs.md


Bugs
====

Bugs for the Marionette server are tracked in the
`Remote Protocol :: Marionette <https://bugzilla.mozilla.org/buglist.cgi?product=Remote%20Protocol&component=Marionette&resolution=--->`__
component. Bugs for the Python client and test harness are tracked in the
`Testing :: Marionette Client and Harness <https://bugzilla.mozilla.org/buglist.cgi?product=Testing&component=Marionette%20Client%20and%20Harness&resolution=--->`__
component.


Communication
=============

The mailing list for Marionette discussion is
https://groups.google.com/a/mozilla.org/g/dev-webdriver.

If you prefer real-time chat, ask your questions
on `#webdriver:mozilla.org <https://chat.mozilla.org/#/room/#webdriver:mozilla.org>`__.
