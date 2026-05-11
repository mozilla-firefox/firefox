Chrome Registration
-------------------

What is chrome?
---------------

`Chrome` is the set of user interface elements of the
application window that are outside the window's content area. Toolbars,
menu bars, progress bars, and window title bars are all examples of
elements that are typically part of the chrome.

``chrome.manifest`` files are used to register XPCOM components and sources for the chrome protocol.
Every application supplies a root ``chrome.manifest`` file that Mozilla reads on startup.

Chrome providers
----------------

A supplier of chrome for a given window type (e.g., for the browser
window) is called a chrome provider. The providers work together to
supply a complete set of chrome for a particular window, from the images
on the toolbar buttons to the files that describe the text, content, and
appearance of the window itself.

There are three basic types of chrome providers:

Content
   The main source file for a window description comes from the content
   provider, and it can be any file type viewable from within Mozilla.
   It will typically be a XUL file, since XUL is designed for describing
   the contents of windows and dialogs. The JavaScript files that define
   the user interface are also contained within the content packages.

Locale
   Localizable applications keep all their localized information in
   locale providers and Fluent FTL files, which are handled separately.
   This allows translators to plug in a different
   chrome package to translate an application without altering the rest
   of the source code. In a chrome provider, localizable files are mostly
   Java-style properties files.
Skin
   A skin provider is responsible for providing a complete set of files
   that describe the visual appearance of the chrome. Typically a skin
   provider will provide CSS files and
   images.

The chrome registry
-------------------

The Gecko runtime maintains a service known as the chrome registry that
provides mappings from chrome package names to the physical location of
chrome packages on disk.

This chrome registry is configurable and persistent, and thus a user can
install different chrome providers, and select a preferred skin and
locale. This is accomplished through xpinstall and the extension
manager.

In order to inform the chrome registry of the available chrome, a text
manifest is used: this manifest is "chrome.manifest" in the root of an
extension, or theme, or XULRunner application.

The plaintext chrome manifests are in a simple line-based format. Each
line is parsed individually; if the line is parsable the chrome registry
takes the action identified by that line, otherwise the chrome registry
ignores that line (and prints a warning message in the runtime error
console).

.. code:: text

   locale packagename localename path/to/files
   skin packagename skinname path/to/files

.. note::

   The characters @ # ; : ? / are not allowed in the
   packagename.

Manifest instructions
---------------------

comments
~~~~~~~~

.. code:: text

   # this line is a comment - you can put here whatever you want

A line is a comment if it begins with the character '#'. Any following
character in the same line is ignored.

manifest
~~~~~~~~

::

   manifest subdirectory/foo.manifest [flags]

This will load a secondary manifest file. This can be useful for
separating component and chrome registration instructions, or separate
platform-specific registration data.

component
~~~~~~~~~

::

   component {00000000-0000-0000-0000-000000000000} components/mycomponent.js [flags]

Informs Mozilla about a component CID implemented by an XPCOM component
implemented in JavaScript (or another scripting language, if
applicable). The ClassID {0000...} must match the ClassID implemented by
the component. To generate a unique ClassID, use a UUID generator
program or site.

contract
~~~~~~~~

::

   contract @foobar/mycontract;1 {00000000-0000-0000-0000-000000000000} [flags]

Maps a contract ID (a readable string) to the ClassID for a specific
implementation. Typically a contract ID will be paired with a component
entry immediately preceding.

category
~~~~~~~~

::

   category category entry-name value [flags]

Registers an entry in the `category manager`. The
specific format and meaning of category entries depend on the category.

content
~~~~~~~

A content package is registered with the line:

::

   content packagename uri/to/files/ [flags]

This will register a location to use when resolving the URI
``chrome://packagename/content/...``. The URI may be absolute or
relative to the location of the manifest file. Note: it must end with a
'/'.

locale
~~~~~~

A locale package is registered with the line:

.. code:: text

   locale packagename localename uri/to/files/ [flags]

This will register a locale package when resolving the URI
chrome://*packagename*/locale/... . The *localename* is usually a plain
language identifier "en" or a language-country identifier "en-US". If
more than one locale is registered for a package, the chrome registry
will select the best-fit locale using the user's preferences.

skin
~~~~

A skin package is registered with the line:

.. code:: text

   skin packagename skinname uri/to/files/ [flags]

This will register a skin package when resolving the URI
chrome://packagename/skin/... . The *skinname* is an opaque string
identifying an installed skin. If more than one skin is registered for a
package, the chrome registry will select the best-fit skin using the
user's preferences.

style
~~~~~

Style overlays (custom CSS which will be applied to a chrome page) are
registered with the following syntax:

.. code:: text

   style chrome://URI-to-style chrome://stylesheet-URI [flags]

override
~~~~~~~~

In some cases an extension or embedder may wish to override a chrome
file provided by the application or XULRunner. In order to allow for
this, the chrome registration manifest allows for "override"
instructions:

.. code:: text

   override chrome://package/type/original-uri.whatever new-resolved-URI [flags]

Note: overrides are not recursive (so overriding
chrome://foo/content/bar/ with file:///home/john/blah/ will not usually
do what you want or expect it to do). Also, the path inside overridden
files is relative to the overridden path, not the original one (this can
be annoying and/or useful in CSS files, for example).

.. _chrome_manifest_resource:

resource
~~~~~~~~

Aliases can be created using the ``resource`` instruction:

.. code:: text

   resource aliasname uri/to/files/ [flags]

This will create a mapping for ``resource://<aliasname>/`` URIs to the
path given.

.. note::

   **Note:** There are no security restrictions preventing web content
   from including content at resource: URIs, so take care what you make
   visible there.

Manifest flags
--------------

Manifest lines can have multiple, space-delimited flags added at the end
of the registration line. These flags mark special attributes of chrome
in that package, or limit the conditions under which the line is used.

application
~~~~~~~~~~~

Extensions may install into multiple applications. There may be chrome
registration lines which only apply to one particular application. The
flag

.. code:: text

   application=app-ID

indicates that the instruction should only be applied if the extension
is installed into the application identified by *app-ID*. Multiple
application flags may be included on a single line, in which case the
line is applied if any of the flags match.

This example shows how a different overlay can be used for different
applications:

::

   overlay chrome://browser/content/browser.xul chrome://myaddon/content/ffOverlay.xul application={ec8030f7-c20a-464f-9b0e-13a3a9e97384}
   overlay chrome://messenger/content/mailWindowOverlay.xul chrome://myaddon/content/tbOverlay.xul application={3550f703-e582-4d05-9a08-453d09bdfdc6}
   overlay chrome://songbird/content/xul/layoutBaseOverlay.xul chrome://myaddon/content/sbOverlay.xul application=songbird@songbirdnest.com

appversion
~~~~~~~~~~

Extensions may install into multiple versions of an application. There
may be chrome registration lines which only apply to a particular
application version. The flag

.. code:: text

   appversion=version
   appversion<version
   appversion<=version
   appversion>version
   appversion>=version

indicates that the instruction should only be applied if the extension
is installed into the application version identified. Multiple
``appversion`` flags may be included on a single line, in which case the
line is applied if any of the flags match. The version string must
conform to the `Toolkit version format`.

platformversion
~~~~~~~~~~~~~~~

When supporting more then one application, it is often more convenient
for an extension to specify which Gecko version it is compatible with.
This is particularly true for binary components. If there are chrome
registration lines which only apply to a particular Gecko version, the
flag

.. code:: text

   platformversion=version
   platformversion<version
   platformversion<=version
   platformversion>version
   platformversion>=version

indicates that the instruction should only be applied if the extension
is installed into an application using the Gecko version identified.
Multiple ``platformversion`` flags may be included on a single line, in
which case the line is applied if any of the flags match.

contentaccessible
~~~~~~~~~~~~~~~~~

Chrome resources can no longer be referenced from within <img>,
<script>, or other elements contained in, or added to, content that was
loaded from an untrusted source. This restriction applies to both
elements defined by the untrusted source and to elements added by
trusted extensions. If such references need to be explicitly allowed,
set the ``contentaccessible`` flag to ``yes`` to obtain the behavior
found in older versions of Firefox. See
`bug 436989 <https://bugzilla.mozilla.org/show_bug.cgi?id=436989>`__.

The ``contentaccessible`` flag applies only to content packages: it is
not recognized for locale or skin registration. However, the matching
locale and skin packages will also be exposed to content.

**n.b.:** Because older versions of Firefox do not understand the
``contentaccessible`` flag, any extension designed to work with both
Firefox 3 and older versions of Firefox will need to provide a fallback.
For example:

::

   content packagename chrome/path/
   content packagename chrome/path/ contentaccessible=yes

os
~~

Extensions (or themes) may offer different features depending on the
operating system on which Firefox is running. The value is compared to
the value of `OS_TARGET` for the platform.

.. code:: text

   os=WINNT
   os=Darwin

osversion
~~~~~~~~~

An extension or theme may need to operate differently depending on which
version of an operating system is running. For example, a theme may wish
to adopt a different look on Mac OS X 10.5 than 10.4:

.. code:: text

   osversion>=10.5

abi
~~~

If a component is only compatible with a particular ABI, it can specify
which ABI/OS by using this directive. The value is taken from the
`nsIXULRuntime` OS and
XPCOMABI values (concatenated with an underscore). For example:

::

   binary-component component/myLib.dll abi=WINNT_x86-MSVC
   binary-component component/myLib.so abi=Linux_x86-gcc3

platform (Platform-specific packages)
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Some packages are marked with a special flag indicating that they are
platform specific. Some parts of content, skin, and locales may be
different based on the platform being run. These packages contain three
different sets of files, for Windows and OS/2, Macintosh, and Unix-like
platforms. For example, the order of the "OK" and "Cancel" buttons in a
dialog is different, as well as the names of some items.

The "platform" modifier is only parsed for content registration; it is
not recognized for locale or skin registration. However, it applies to
content, locale, and skin parts of the package, when specified.

process
~~~~~~~

In electrolysis registrations can be set to only apply in either the
main process or any content processes. The "process" flag selects
between these two. This can allow you to register different components
for the same contract ID or ensure a component can only be loaded in the
main process.

::

   component {09543782-22b1-4a0b-ba07-9134365776ee} maincomponent.js process=main
   component {98309951-ac89-4642-afea-7b2b6216bcef} contentcomponent.js process=content

remoteenabled
~~~~~~~~~~~~~

In `multiprocess Firefox`, the
default is that a given chrome: URI will always be loaded into the
chrome process. If you set the "remoteenabled" flag, then the page will
be loaded in the same process as the ``browser`` that loaded it:

::

   content packagename chrome/path/ remoteenabled=yes

remoterequired
~~~~~~~~~~~~~~

In `multiprocess Firefox`, the
default is that a given chrome: URI will always be loaded into the
chrome process. If you set the "remoterequired" flag, then the page will
always be loaded into a child process:

::

   content packagename chrome/path/ remoterequired=yes

Example chrome manifest
-----------------------

.. list-table::
   :widths: 20 20 20 20


   *  - type
      - engine
      - language
      - url
   *  - content
      - branding
      - browser/content/branding/
      - contentaccessible=yes
   *  - content
      - browser
      - browser/content/browser/
      - contentaccessible=yes
   *  - override
      -
      - chrome://global/content/license.html
      - chrome://browser/content/license.html
   *  - resource
      - payments
      - browser/res/payments/
      -
   *  - skin
      - browser
      - classic/1.0 browser/skin/classic/browser/
      -
   *  - locale
      - branding
      - en-US
      - en-US/locale/branding/
   *  - locale
      - browser
      - en-US
      - en-US/locale/browser/
   *  - locale
      - browser-region
      - en-US
      - en-US/locale/browser-region/
