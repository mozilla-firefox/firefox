Signing Local macOS Builds
==========================

Background
----------
Firefox for macOS is mainly signed in one of two different ways: one for
production releases, and one for builds that run on try. Typically, developers
testing local builds don’t need to be concerned with signing unless they are
working on an area specifically affected by macOS entitlements such as passkeys,
loading third party libraries, or adding a new process type. However, it is
good practice to test builds that are as close as possible to the production
configuration or the try server configuration. Local builds are not signed
automatically and mach doesn’t include support for running tests on signed
builds. However, the mach command ``macos-sign`` can be used to sign local
packaged builds for manual testing. ``macos-sign`` supports signing builds to
match try or production builds.

    Note: On Apple Silicon Macs, where all executables are required to be
    signed, Firefox binaries will be “ad-hoc” self-signed automatically by the
    linker during compilation, but this is a per-binary sign with no
    Firefox-specific runtime settings or entitlements. This document ignores
    automatic signing.

To sign your own local build so that it has the same set of entitlements as a
production release requires a signing certificate and provisioning profile
issued from Mozilla’s Apple Developer account. Entitlements are used to grant
Firefox certain permissions as well as impose security restrictions when it is
run on macOS. Some entitlements are considered restricted and can only be
enabled when signed by a certificate from an account that has been granted
permission to use the entitlement, such as the passkey macOS entitlement. As an
example of the production restrictions, production builds use an entitlement
that disallows debugger attachment. Disallowing debuggers is required for
distribution because it is required by the Notarization system. When signing
locally, developers can modify entitlements as needed.

Summary
-------

**Production build:** requires Mozilla's official Apple Developer ID
certificate, private key, and provisioning profile. Only Mozilla Release
Engineering has access to the official Developer ID certificate and private key.
Mozilla developers can request a limited-use Apple *Developer* certificate which
can be used to generated production-like builds for local testing. See
:ref:`like-prod` below.

**Developer build:** requires generating a self-signed certificate (to sign
like a try push is signed) or using ad-hoc signing (no setup required and only
usable locally), will have no passkey support (or any other restricted
entitlement), has fewer restrictions with respect to module loading, is
debuggable.

Signing Your Build Like try
---------------------------
To sign your own local build with entitlements that match what is used for try
push automated testing, generate a self-signed code signing certificate using
the macOS Keychain Access application. During the process, supply a unique name
not used by other Keychain entries making sure to not use any spaces. For
example, ``my-firefox-selfsign-cert-2024``.  This string will be used as
the signing identity and passed to ``macos-sign`` with the ``-s`` option.
``./mach`` passes this to the codesign command which looks up the entry in the
keychain. When running the signing command, you'll be prompted to allow
``codesign`` to access the keychain entry. Select ``Always Allow`` when
prompted.

.. code-block:: shell

    $ ./mach build package
    $ open <path-to-dmg>
    <drag Browser to the Desktop>
    $ ./mach macos-sign -s my-firefox-selfsign-cert-2024 -a ~/Desktop/Nightly.app

The entitlements in the tree used for this configuration are labeled as
developer and we call this the developer build. Developer signed builds differ
from production signed in the following ways:

* They allow debugger attachment
* They allow loading of third party libraries in all processes
* They respect dyld environment variables
* They don’t include restricted entitlements such as the passkey entitlement
  (and therefore passkeys can’t be used)

Ad-hoc Signing Your Build - Like try Signing, but Requires no Configuration and is For Local Use Only
-----------------------------------------------------------------------------------------------------
Omitting the ``-s`` option will use ad-hoc signing which requires no setup. The
build will be more limited than builds signed with a self-signed cert. Ad-hoc
signed builds are not verifiable or runnable on any other system. There is
little documentation available about the limitations.

.. code-block:: shell

    $ ./mach build package
    $ open <path-to-dmg>
    <drag Browser to the Desktop>
    $ ./mach macos-sign -a ~/Desktop/Nightly.app

.. _like-prod:

Signing Your Build Like Production
----------------------------------
To sign your local build like a production Firefox build, you’ll need an Apple
Developer signing certificate and provisioning profile issued from Mozilla's
Apple Developer account. Developers will be given a development-role login
allowing a signing certificate and provisioning profile to be generated. The
provisioning profile used with Development certificates limits the signed
application to Mozilla developer machines via a hardware ID. Employees can file
a bug `here <https://bugzilla.mozilla.org/enter_bug.cgi?product=App%20Stores&component=App%20Store%20Access>`__
to request an account. Once the developer's Apple account is setup as a member
of Mozilla's Apple account, Xcode can be used to download a Developer signing
certificate and provisioning profile for development use. Use Keychain Access to
get the codesigning identifier for your development cert which should be passed
as the ``-s`` codesigning identity to ``mach macos-sign``:

.. code-block:: shell

    $ ./mach build package
    $ open <path-to-dmg>
    <drag Browser to the Desktop>
    $ ./mach macos-sign -a ~/Desktop/Nightly.app -s <MOZILLA_DEVELOPER_CERT_ID> -e production

Example: Re-Signing Official Nightly
------------------------------------

.. code-block:: shell

    $ ditto /Applications/Firefox\ Nightly.app ~/Desktop/FirefoxNightly.app
    $ ./mach macos-sign -a ~/Desktop/FirefoxNightly.app
    0:00.20 Using ad-hoc signing identity
    0:00.20 Using nightly channel signing configuration
    0:00.20 Using developer entitlements
    0:00.20 Reading build config file /Users/me/r/mc/taskcluster/config.yml
    0:00.23 Stripping existing xattrs and signatures
    0:01.91 Signing with codesign
    0:02.72 Verification of signed app /Users/me/Desktop/FirefoxNightly.app OK

Example: Re-Signing Official Developer Edition With `rcodesign <https://crates.io/crates/apple-codesign>`__ Using a pkcs12 Certificate Key Pair
-----------------------------------------------------------------------------------------------------------------------------------------------

More information about rcodesign can be found on the
`rust crate page <https://crates.io/crates/apple-codesign>`__ or
`github repo <https://github.com/indygreg/apple-platform-rs>`__. Certificates
can be exported from Keychain Access in .p12 format.

.. code-block:: shell

    $ ditto /Applications/Firefox\ Developer\ Edition.app/ ~/Desktop/DevEdition.app
    $ ./mach macos-sign -r -a ~/Desktop/DevEdition.app \
      --rcodesign-p12-file ./myDevId.p12 \
      --rcodesign-p12-password-file ./myDevId.p12.passwd
    0:00.26 Using pkcs12 signing identity
    0:00.26 Using devedition channel signing configuration
    0:00.26 Using developer entitlements
    0:00.26 Reading build config file /Users/me/r/mc/taskcluster/config.yml
    0:00.29 Stripping existing xattrs and signatures
    0:02.09 Signing with rcodesign
    0:11.16 Verification of signed app /Users/me/Desktop/DevEdition.app OK
