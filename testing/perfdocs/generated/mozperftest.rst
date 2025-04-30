===========
Mozperftest
===========

**Mozperftest** can be used to run performance tests.


.. toctree::

   running
   tools
   writing
   developing
   vision

The following documents all testing we have for mozperftest.
If the owner does not specify the Usage and Description, it's marked N/A.

browser/base/content/test
-------------------------
Performance tests from the 'browser/base/content/test' folder.

perftest_browser_xhtml_dom.js
=============================

:owner: Browser Front-end team
:name: Dom-size

**Measures the size of the DOM**


browser/components/translations/tests/browser
---------------------------------------------
Performance tests for Translations models on Firefox Desktop

browser_translations_perf_es_en.js
==================================

:owner: Translations Team
:name: Full-Page Translation (Spanish to English)
:Default options:

::

 --perfherder
 --perfherder-metrics name:engine-init-time,unit:ms,shouldAlert:True,lowerIsBetter:True, name:words-per-second,unit:WPS,shouldAlert:True,lowerIsBetter:False, name:tokens-per-second,unit:TPS,shouldAlert:True,lowerIsBetter:False, name:peak-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:stabilized-memory-usage,unit:MiB,shouldAlert:True,lowerIsBetter:True, name:total-translation-time,unit:s,shouldAlert:True,lowerIsBetter:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Tests the speed of Full Page Translations using the Spanish-to-English model.**


dom/serviceworkers/test/performance
-----------------------------------
Performance tests running through Mochitest for Service Workers

test_caching.html
=================

:owner: DOM LWS
:name: Service Worker Caching
:Default options:

::

 --perfherder
 --perfherder-metrics name:No cache,unit:ms,shouldAlert:True, name:Cached,unit:ms,shouldAlert:True, name:No cache again,unit:ms,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor plain

**Test service worker caching.**

test_fetch.html
===============

:owner: DOM LWS
:name: Service Worker Fetch
:Default options:

::

 --perfherder
 --perfherder-metrics name:Cold fetch,unit:ms,shouldAlert:True, name:Undisturbed fetch,unit:ms,shouldAlert:True, name:Intercepted fetch,unit:ms,shouldAlert:True, name:Liberated fetch,unit:ms,shouldAlert:True, name:Undisturbed XHR,unit:ms,shouldAlert:True, name:Intercepted XHR,unit:ms,shouldAlert:True, name:Liberated XHR,unit:ms,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor plain

**Test cold and warm fetches.**

test_registration.html
======================

:owner: DOM LWS
:name: Service Worker Registration
:Default options:

::

 --perfherder
 --perfherder-metrics name:Registration,unit:ms,shouldAlert:True, name:Registration Internals,unit:ms,shouldAlert:True, name:Activation,unit:ms,shouldAlert:True, name:Unregistration,unit:ms,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor plain

**Test registration, activation, and unregistration.**

test_update.html
================

:owner: DOM LWS
:name: Service Worker Update
:Default options:

::

 --perfherder
 --perfherder-metrics name:Vacuous update,unit:ms,shouldAlert:True, name:Server update,unit:ms,shouldAlert:True, name:Main callback,unit:ms,shouldAlert:True, name:SW callback,unit:ms,shouldAlert:True, name:Update internals,unit:ms,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor plain

**Test updating.**


dom/webgpu/tests/mochitest
--------------------------
Performance tests from the 'dom/webgpu/tests/mochitest' folder.

test_queue_write_perf.html
==========================

:owner: Graphics Team
:name: Queue Write
:Default options:

::

 --perfherder
 --perfherder-metrics name:writeBuffer Time,unit:ms, name:writeTexture Time,unit:ms
 --manifest perftest.toml
 --manifest-flavor plain

**Test the performance of Queue.writeBuffer and Queue.writeTexture**


intl/benchmarks/test/xpcshell
-----------------------------
Performance tests running through XPCShell for Intl code

perftest_dateTimeFormat.js
==========================

:owner: Internationalization Team
:name: Intl.DateTimeFormat
:tags: intl,ecma402
:Default options:

::

 --perfherder
 --perfherder-metrics name:Intl.DateTimeFormat constructor iterations,unit:iterations, name:Intl.DateTimeFormat constructor accumulatedTime,unit:ms, name:Intl.DateTimeFormat constructor perCallTime,unit:ms, name:Intl.DateTimeFormat.prototype.format iterations,unit:iterations, name:Intl.DateTimeFormat.prototype.format accumulatedTime,unit:ms, name:Intl.DateTimeFormat.prototype.format perCallTime,unit:ms
 --verbose

**Test the speed of the Intl.DateTimeFormat implementation.**

perftest_locale.js
==================

:owner: Internationalization Team
:name: Intl.Locale
:tags: intl,ecma402
:Default options:

::

 --perfherder
 --perfherder-metrics name:Intl.Locale constructor iterations,unit:iterations, name:Intl.Locale constructor accumulatedTime,unit:ms, name:Intl.Locale constructor perCallTime,unit:ms, name:Intl.Locale.prototype accessors iterations,unit:iterations, name:Intl.Locale.prototype accessors accumulatedTime,unit:ms, name:Intl.Locale.prototype accessors perCallTime,unit:ms, name:Intl.Locale.maximize operation iterations,unit:iterations, name:Intl.Locale.maximize operation accumulatedTime,unit:ms, name:Intl.Locale.maximize operation perCallTime,unit:ms
 --verbose

**Test the speed of the Intl.Locale implementation.**

perftest_numberFormat.js
========================

:owner: Internationalization Team
:name: Intl.NumberFormat
:tags: intl,ecma402
:Default options:

::

 --perfherder
 --perfherder-metrics name:Intl.NumberFormat constructor iterations,unit:iterations, name:Intl.NumberFormat constructor accumulatedTime,unit:ms, name:Intl.NumberFormat constructor perCallTime,unit:ms, name:Intl.NumberFormat.prototype.format iterations,unit:iterations, name:Intl.NumberFormat.prototype.format accumulatedTime,unit:ms, name:Intl.NumberFormat.prototype.format perCallTime,unit:ms, name:Intl.NumberFormat.prototype.formatToParts iterations,unit:iterations, name:Intl.NumberFormat.prototype.formatToParts accumulatedTime,unit:ms, name:Intl.NumberFormat.prototype.formatToParts perCallTime,unit:ms
 --verbose

**Test the speed of the Intl.NumberFormat implementation.**

perftest_pluralRules.js
=======================

:owner: Internationalization Team
:name: Intl.PluralRules
:tags: intl,ecma402
:Default options:

::

 --perfherder
 --perfherder-metrics name:Intl.PluralRules constructor iterations,unit:iterations, name:Intl.PluralRules constructor accumulatedTime,unit:ms, name:Intl.PluralRules constructor perCallTime,unit:ms, name:Intl.PluralRules.prototype.select iterations,unit:iterations, name:Intl.PluralRules.prototype.select accumulatedTime,unit:ms, name:Intl.PluralRules.prototype.select perCallTime,unit:ms, name:Intl.PluralRules pluralCategories iterations,unit:iterations, name:Intl.PluralRules pluralCategories accumulatedTime,unit:ms, name:Intl.PluralRules pluralCategories perCallTime,unit:ms
 --verbose

**Test the speed of the Intl.PluralRules implementation.**


netwerk/test/perf
-----------------
Performance tests from the 'network/test/perf' folder.

perftest_http3_cloudflareblog.js
================================

:owner: Network Team
:name: cloudflare

**User-journey live site test for Cloudflare blog.**

perftest_http3_controlled.js
============================

:owner: Network Team
:name: controlled
:tags: throttlable

**User-journey live site test for controlled server**

perftest_http3_facebook_scroll.js
=================================

:owner: Network Team
:name: facebook-scroll

**Measures the number of requests per second after a scroll.**

perftest_http3_google_image.js
==============================

:owner: Network Team
:name: g-image

**Measures the number of images per second after a scroll.**

perftest_http3_google_search.js
===============================

:owner: Network Team
:name: g-search

**User-journey live site test for google search**

perftest_http3_lucasquicfetch.js
================================

:owner: Network Team
:name: lq-fetch

**Measures the amount of time it takes to load a set of images.**

perftest_http3_youtube_watch.js
===============================

:owner: Network Team
:name: youtube-noscroll

**Measures quality of the video being played.**

perftest_http3_youtube_watch_scroll.js
======================================

:owner: Network Team
:name: youtube-scroll

**Measures quality of the video being played.**


netwerk/test/unit
-----------------
Performance tests from the 'netwerk/test/unit' folder.

test_http3_perf.js
==================

:owner: Network Team
:name: http3 raw
:tags: network,http3,quic
:Default options:

::

 --perfherder
 --perfherder-metrics name:speed,unit:bps
 --xpcshell-cycles 13
 --verbose
 --try-platform linux, mac

**XPCShell tests that verifies the lib integration against a local server**


testing/performance
-------------------
Performance tests from the 'testing/performance' folder.

perftest_bbc_link.js
====================

:owner: Performance Team
:name: BBC Link

**Measures time to load BBC homepage**

perftest_facebook.js
====================

:owner: Performance Team
:name: Facebook

**Measures time to log in to Facebook**

perftest_jsconf_cold.js
=======================

:owner: Performance Team
:name: JSConf (cold)

**Measures time to load JSConf page (cold)**

perftest_jsconf_warm.js
=======================

:owner: Performance Team
:name: JSConf (warm)

**Measures time to load JSConf page (warm)**

perftest_politico_link.js
=========================

:owner: Performance Team
:name: Politico Link

**Measures time to load Politico homepage**

perftest_youtube_link.js
========================

:owner: Performance Team
:name: YouTube Link

**Measures time to load YouTube video**

perftest_pageload.js
====================

:owner: Performance Team
:name: pageload

**Measures time to load mozilla page**

perftest_perfstats.js
=====================

:owner: Performance Team
:name: perfstats

**Collect perfstats for the given site**

This test launches browsertime with the perfStats option (will collect low-overhead timings, see Bug 1553254). The test currently runs a short user journey. A selection of popular sites are visited, first as cold pageloads, and then as warm.

perftest_WPT_chrome_init_file.js
================================

:owner: Performance Testing Team
:name: webpagetest-chrome

**Run webpagetest performance pageload tests on Chrome against Alexa top 50 websites**

This mozperftest gets webpagetest to run pageload tests on Chrome against the 50 most popular websites and provide data. The full list of data returned from webpagetest: firstContentfulPaint, visualComplete90, firstPaint, visualComplete99, visualComplete, SpeedIndex, bytesIn,bytesOut, TTFB, fullyLoadedCPUms, fullyLoadedCPUpct, domElements, domContentLoadedEventStart, domContentLoadedEventEnd, loadEventStart, loadEventEnd

perftest_WPT_firefox_init_file.js
=================================

:owner: Performance Testing Team
:name: webpagetest-firefox

**Run webpagetest performance pageload tests on Firefox against Alexa top 50 websites**

This mozperftest gets webpagetest to run pageload tests on Firefox against the 50 most popular websites and provide data. The full list of data returned from webpagetest: firstContentfulPaint, timeToContentfulPaint, visualComplete90, firstPaint, visualComplete99, visualComplete, SpeedIndex, bytesIn, bytesOut, TTFB, fullyLoadedCPUms, fullyLoadedCPUpct, domElements, domContentLoadedEventStart, domContentLoadedEventEnd, loadEventStart, loadEventEnd


toolkit/components/ml/tests/browser
-----------------------------------
Performance tests running through Mochitest for ML Models

browser_ml_semantic_history_search_perf.js
==========================================

:owner: GenAI Team
:name: ML Semantic History Search
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:True, name:memory,unit:MB,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Test for latency for ML Semantic Search History Feature**

browser_ml_smart_tab_clustering_perf.js
=======================================

:owner: GenAI Team
:name: ML Smart Tab Clustering
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:False, name:memory,unit:MiB,shouldAlert:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Testing Smart Tab Clustering**

browser_ml_autofill_perf.js
===========================

:owner: GenAI Team
:name: browser_ml_autofill_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:AUTOFILL-pipeline-ready-latency,unit:ms,shouldAlert:False, name:AUTOFILL-initialization-latency,unit:ms,shouldAlert:False, name:AUTOFILL-model-run-latency,unit:ms,shouldAlert:False, name:AUTOFILL-total-memory-usage,unit:MiB,shouldAlert:False, name:tokenSpeed,unit:tokens/s,shouldAlert:False,lowerIsBetter:False, name:charactersSpeed,unit:chars/s,shouldAlert:False,lowerIsBetter:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Template test for latency for ML Autofill model**

browser_ml_engine_multi_perf.js
===============================

:owner: GenAI Team
:name: browser_ml_engine_multi_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:False, name:memory,unit:MiB,shouldAlert:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Testing model execution concurrently**

browser_ml_engine_perf.js
=========================

:owner: GenAI Team
:name: browser_ml_engine_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:False, name:memory,unit:MiB,shouldAlert:False, name:tokenSpeed,unit:tokens/s,shouldAlert:False,lowerIsBetter:False, name:charactersSpeed,unit:chars/s,shouldAlert:False,lowerIsBetter:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Template test for latency for ml models**

browser_ml_llama_summarizer_perf.js
===================================

:owner: GenAI Team
:name: browser_ml_llama_summarizer_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:False, name:memory,unit:MB,shouldAlert:False, name:tokenSpeed,unit:tokens/s,shouldAlert:False,lowerIsBetter:False, name:charactersSpeed,unit:chars/s,shouldAlert:False,lowerIsBetter:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Template test for latency for Summarizer model using Llama.cpp WASM**

browser_ml_smart_tab_perf.js
============================

:owner: GenAI Team
:name: browser_ml_smart_tab_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:False, name:memory,unit:MiB,shouldAlert:False, name:tokenSpeed,unit:tokens/s,shouldAlert:False,lowerIsBetter:False, name:charactersSpeed,unit:chars/s,shouldAlert:False,lowerIsBetter:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Testing Smart Tab Models**

browser_ml_suggest_feature_perf.js
==================================

:owner: GenAI Team
:name: browser_ml_suggest_feature_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:True, name:memory,unit:MiB,shouldAlert:True
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Template test for latency for ML suggest Feature**

browser_ml_suggest_inference.js
===============================

:owner: GenAI Team
:name: browser_ml_suggest_inference.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:inference-pipeline-ready-latency,unit:ms,shouldAlert:False, name:inference-initialization-latency,unit:ms,shouldAlert:False, name:inference-model-run-latency,unit:ms,shouldAlert:False, name:inference-total-memory-usage,unit:ms,shouldAlert:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Template test for ML suggest Inference Model**

browser_ml_summarizer_perf.js
=============================

:owner: GenAI Team
:name: browser_ml_summarizer_perf.js
:Default options:

::

 --perfherder
 --perfherder-metrics name:latency,unit:ms,shouldAlert:True, name:memory,unit:MiB,shouldAlert:True, name:tokenSpeed,unit:tokens/s,shouldAlert:True,lowerIsBetter:False, name:charactersSpeed,unit:chars/s,shouldAlert:True,lowerIsBetter:False
 --verbose
 --manifest perftest.toml
 --manifest-flavor browser-chrome
 --try-platform linux, mac, win

**Template test for latency for Summarizer model**


If you have any questions, please see this `wiki page <https://wiki.mozilla.org/TestEngineering/Performance#Where_to_find_us>`_.
