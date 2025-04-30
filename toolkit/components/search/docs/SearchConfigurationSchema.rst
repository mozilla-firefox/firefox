===========================
Search Configuration Schema
===========================

This document outlines the details of the `search-config-v2`_ schema and how
the various sub-parts interact. For the full fields and descriptions, please see
the `schema itself`_.

.. note::
    In the examples, only relevant properties are displayed.

Overview
========

The configuration is a JSON blob, an object with a ``data`` property and value
as an array containing ``engine``, ``defaultEngines``, and ``engineOrders``
record types:

.. code-block:: js

    {
      data: [
        {
          // defaultEngines details
        },
        {
          // engine 1 details
        },
        {
          // engine 2 details
        },
        {
          // engineOrders details
        },
      ]
    }

Environment
===========
An ``environment`` property is contained within each of the record types and is
used to determine where some, or all, of that record is applied to according to
the user's detected environment. There are many different properties in ``environment``.
These may be used individually or together to build up a match. Here is the list
of those properties:

``allRegionsAndLocales``
  - ``"environment": { "allRegionsAndLocales": true }``
  - Indicates that this section applies to all regions and locales. May be
    modified by excludedRegions/excludedLocales.

``applications``
  - ``"environment": { "applications": ["firefox"] }``
  - This property specifies the applications the record would be applied to.
  - Some applications we support are ``firefox``, ``firefox-android``, ``firefox-ios``,
    ``focus-android``, and ``focus-ios``.

``channels``
  - ``"environment": { "channels": ["default"] }``
  - One or more channels may be specified in an array to restrict a configuration
    to just those channels. The following is a list of the channels:

    - default: Self-builds of Firefox, or possibly some self-distributed versions.
    - nightly: Firefox Nightly builds.
    - aurora: Firefox Developer Edition.
    - beta: Firefox Beta.
    - release: The main Firefox release channel.
    - esr: The ESR Channel. This will also match versions of Firefox where the
      displayed version number includes ``esr``. We do this to include Linux
      distributions and other manual builds of ESR.

``distributions``
  - ``"environment": { "distributions": ["distribution-name"] }``
  - An array of distribution identifiers that the record object applies to.
  - The identifiers match those supplied by the ``distribution.id`` preference.

``excludedDistributions``
  - ``"environment": { "distributions": ["distribution-excluded"] }``
  - An array of distribution identifiers that the record object does not apply to.

``excludedLocales``
  - ``"environment": { "excludedLocales": ["de"] }``
  - An array of locales that this section should be excluded from.

``excludedRegions``
  - ``"environment": { "excludedRegions": ["ca"] }``
  - An array of regions that this section should be excluded from.

``experiment``
  - ``"environment": { "experiment": "experimental" }``
  - Experiments can be run by giving a value to the ``experiment`` property.
  - The experiment value should also be supplied to the ``experiment`` property
    of the ``searchConfiguration`` feature on Nimbus.

``locales``
  - ``"environment": { "locales": ["en-US"] }``
  - An array of locales that this section applies to.

``regions``
  - ``"environment": { "regions": ["ca"] }``
  - An array of regions that this section applies to.
  - ``"unknown"`` may be used to apply to situations where we have not been able
    to detect the user's region. However, this is not currently expected to be used.

``maxVersion`` and ``minVersion``
  - Minimum and Maximum versions may be specified to restrict a configuration to
    specific ranges. These may be open-ended. Version comparison is performed
    using `the version comparator`_.
  - Note: comparison against ``maxVersion`` is a less-than comparison. The
    ``maxVersion`` won't be matched directly.
  - ``"environment": { "minVersion": "72.0a1" }`` Indicates the record object
    will be included for any version after 72.0a1.
  - ``"environment": { "minVersion": "68.0a1", "maxVersion": "72.0a1" }``
    Indicates the record object would be included only between 68.0a1 and 71.x
    version.

Engine Objects
==============

Each engine object represents a search engine that may be presented to a user.
Each engine object is treated as a separate engine within the application, with
independent settings.

.. code-block:: js

    {
      "base": {
        "classification": "general"
        "name": "engine1 name",
        "partnerCode": "bar",
        "urls": {
          "search": {
            "base": "https://www.example.com",
            "params": [
              {
                "name": "code",
                "value": "{partnerCode}"
              }
            ],
            "searchTermParamName": "q"
          },
        },
      },
      "identifier": "engine1",
      "recordType": "engine",
      "variants": [
        {
          "environment": { "allRegionsAndLocales": true }
        }
      ]
    }

The required **top-level** properties are:

- ``base`` Defines the base details for the engine. The required base properties
  are ``classification``, ``name``, and ``urls``.

    - The ``urls`` may be different types of  urls. These various types include
      ``search``, ``suggestions``, and ``trending`` urls. The url property contains
      the base url and any query string params to build the complete url. If
      ``engine1`` is used to search ``kitten``, the search url will be constructed
      as ``https://www.example.com/?code=bar&q=kitten``. Notice the ``partnerCode``
      from the base is inserted into parameters ``{partnerCode}`` value of the search url.
- ``identifier`` Identifies the search engine and is used internally to set the telemetry id.
- ``recordType`` The type of record the object is. Always ``engine`` for engine
  objects.
- ``variants`` Specifies the environment the engine is included in, which details
  the region, locale, and other environment properties.

Engine Variants
===============
A engine may be available only to users located in specific regions or with
certain locales. For example, when the ``environment`` section of variants specify
locales and regions:

.. code-block:: js

    "variants": [
      {
        "environment": { "locales": ["en-US"], "regions": ["US"] }
      }
    ]

In this case users identified as being in the ``en-US`` locale and ``US`` region
would be able to have the engine available.

**Multiple Variants**

When there are more than one variant the last matching variant will be applied.

.. code-block:: js

  "variants": [
    {
      "environment": { "locales": ["en-US"] }
      "partnerCode": "bar"
    },
    {
      "environment": { "locales": ["en-US"], "regions": ["US"]}
      "partnerCode": "foo"
    },
  ]

In this case users identified in ``en-US`` locale and ``US`` region  matched
both the variants. Users in ``en-US`` locale and ``US`` region matched the
first variant because it has ``en-US`` locales and when regions is not specified,
it means all regions are included. Then it matched the second variant because it matched
``en-US`` locale and ``US`` region. The result will be that for this user the
partner code will have value ``foo``.

Engine Subvariants
==================
Nested within ``variants`` may be an array of ``subVariants``. Subvariants
contain the same properties as variants except the ``subVariants`` property. The
purpose of subvariants is for the combination of environment properties from the
last matched subvariant and the top level variant.

.. code-block:: js

    "variants": [
      {
        "environment": { "regions": ["US", "CA", "GB"] }
        "subVariants": [
          {
            "environment": { "channels": [ "esr"] },
            "partnerCode": "bar",
          },
        ]
      }
    ]

In this case users identified as being in ``US`` region and ``esr`` channel
would match the subvariant and would be able to have the engine available with
partner code ``bar`` applied.

**Multiple Subvariants**

When there are more than one subvariant the last matching subvariant will be
applied.

.. code-block:: js

  "variants": [
    {
      "environment": { "regions": ["US", "CA", "GB"] }
      "subVariants": [
        {
          "environment": { "channels": [ "esr"] },
          "partnerCode": "bar",
        },
        {
          "environment": { "channels": [ "esr"], "locales": ["fr"] },
          "partnerCode": "foo",
        }
      ]
    }
  ]

In this case users identified in ``US`` region, ``fr`` locale, and ``esr`` channel
matched both the subvariants. It matched the first subvariant because the first
environment has ``US`` region from the top-level variant, ``esr`` channel, and
all locales. Then it matched the second variant because the second environment
has ``US`` region from top-level variant, ``fr`` locale, and ``esr`` channel.
The user will receive the last matched subvariant with partner code ``foo``.

Engine Defaults
===============

An engine may be specified as the default for one of two purposes:

#. normal browsing mode,
#. private browsing mode.

If there is no engine specified for private browsing mode for a particular region/locale
pair, then the normal mode engine is used.

If the instance of the application does not support a separate private browsing mode engine,
then it will only use the normal mode engine.

An engine may or may not be default for particular regions/locales. The
``defaultEngines`` record is structured to provide ``globalDefault`` and
``globalDefaultPrivate`` properties, these properties define the user's engine
if there are no ``specificDefaults`` sections that match the user's environment.
The ``specificDefaults`` sections can define different engines that match with
specific user environments.

.. code-block:: js

    {
      "globalDefault": "engine1",
      "globalDefaultPrivate": "engine1",
      "recordType": "defaultEngines",
      "specificDefaults": [
        {
          "default": "engine2",
          "defaultPrivate": "engine3"
          "environment": {
            "locales": [
              "en-CA"
            ],
            "regions": [
              "CA"
            ]
          },
        }
      ]
    }


In normal mode:

    - engine1 is default for all regions except for region CA locale en-CA
    - engine2 is default in only the CA region and locale en-CA

In private browsing mode:

    - engine1 is private default for all regions except for region CA locale en-CA
    - engine3 is private default in only the CA region and locale en-CA

Engine Ordering
===============

The ``engineOrders`` record type indicates the suggested ordering of an engine
relative to other engines when displayed to the user, unless the user has
customized their ordering. The ordering is listed in the ``order`` property, an
ordered array with the first engine being at the lowest index.

If there is no matching order for the user's environment, then this order applies:

#. Default Engine
#. Default Private Engine (if any)
#. Other engines in alphabetical order.

Example:

.. code-block:: js

    {
      "orders": [
        {
          "environment": {
            "distributions": [
              "distro"
            ]
          },
          "order": [
            "c-engine",
            "b-engine",
            "a-engine",
          ]
        }
      ]
      "recordType": "engineOrders",
    }

This would result in the order: ``c-engine``, ``b-engine``, ``a-engine`` for the
distribution ``distro``.

Locale Fallbacks
================

On desktop, the locale provided for the user's environment matches the locales that
are shipped with Firefox. The current list of possibilities may be found in the
:searchfox:`all-locales file <browser/locales/all-locales>`. These are typically
two-letter locale names, with some variants.

On mobile platforms, the locales are typically language and region, e.g. ``de-DE``
and ``de-AT``. To match with the locales on desktop, we need to implement a fallback
routine to map the locales to those that desktop uses. For example, ``de-DE``
needs to map to ``de``, but ``en-CA`` would stay as it is.

Whilst this could be managed by looking through the full search configuration for
all the locales referenced, this would incur a runtime cost on every startup and
parsing of the configuration. Hence we have introduced the ``availableLocales``
record which is a list of all the locales currently referenced in the search
configuration.

When determining the user's locale to compare with the environments within the
configuration, the following algorithm is used by the search engine selector:

#. If the user's locale matches one in the ``availableLocales``, then the user's
   locale is used unchanged.

#. If the user's locale is of the form ``ab-CD`` and the ``ab`` part matches one
   in ``availableLocales``, then the two-letter form is used.

#. Otherwise, the locale used doesn't matter, and either form may be used.

.. _schema itself: https://searchfox.org/mozilla-central/source/toolkit/components/search/schema/
.. _the version comparator: https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/version/format
.. _search-config-v2: https://searchfox.org/mozilla-central/source/services/settings/dumps/main/search-config-v2.json
