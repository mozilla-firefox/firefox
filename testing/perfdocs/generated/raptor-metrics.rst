################
Metrics Gathered
################

.. contents::
   :depth: 2
   :local:

**WARNING: This page is still being actively developed.**

This document contains information about the metrics gathered in Browsertime tests, as well as detailed information about how they are gathered.

Pageload Tests
--------------

For browsertime pageload tests, there is a limited set of metrics that we collect (which can easily be expanded). Currently we divide these into two sets of metrics: (i) visual metrics, and (ii) technical metrics. These are gathered through two types of tests called warm and cold pageload tests. We have combined these two into a single "Chimera" mode which you'll find in the Treeherder tasks.

Below, you can find the process of how we run Warm, Cold, and Chimera pageload tests.

Warm Pageload
==============

In this pageload test type, we open the browser, then repeatedly navigate to the same page without restarting the browser in between cycles.

* A new, or conditioned, browser profile is created
* The browser is started up
* Post-startup browser settle pause of 30 seconds or 1 second if using a conditioned profile
* A new tab is opened
* The test URL is loaded; measurements taken
* The tab is reloaded ``X`` more times (for ``X`` replicates); measurements taken each time

NOTES:
- The measurements from the first page-load are not included in overall results metrics because of first load noise; however they are listed in the JSON artifacts
- The bytecode cache gets populated on the first test cycle, and subsequent iterations will already have the cache built to reduce noise.

Cold Pageload
==============

In this pageload test type, we open the browser, navigate to the page, then restart the browser before performing the next cycle.

* A new, or conditioned, browser profile is created
* The browser is started up
* Post-startup browser settle pause of 30 seconds or 1 second if using a conditioned profile
* A new tab is opened
* The test URL is loaded; measurements taken
* The browser is shut down
* Entire process is repeated for the remaining browser cycles

NOTE: The measurements from all browser cycles are used to calculate overall results.

Chimera Pageload
================

A new mode for pageload testing is called Chimera mode. It combines the warm and cold variants into a single test. This test mode is used in our Taskcluster tasks.

* A new, or conditioned, browser profile is created
* The browser is started up
* Post-startup browser settle pause of 30 seconds or 1 second if using a conditioned profile
* A new tab is opened
* The test URL is loaded; measurements taken for ``Cold pageload``
* Navigate to a secondary URL (to preserve the content process)
* The test URL is loaded again; measurements taken for ``Warm pageload``
* The desktop browser is shut down
* Entire process is repeated for the remaining browser cycles

NOTE: The bytecode cache mentioned in Warm pageloads still applies here.

Technical Metrics
=================

Technical metrics are values obtained directly from the browser. This includes metrics like First Paint, DOM Content Flushed, etc..

Visual Metrics
==============

Visual metrics can be obtained by running Raptor Browsertime with ``--browsertime-visualmetrics``, it will record a video of the page being loaded and then process this video to build the metrics. The video is either produced using FFMPEG (with ``--browsertime-no-ffwindowrecorder``) or the Firefox Window Recorder (default).


Benchmarks
----------

Benchmarks gather their own custom metrics, unlike the pageload tests above. Please ping the owners of those benchmarks to determine what they mean and how they are produced, or reach out to the Performance Test and Tooling team in #perftest on Element.

Metric Definitions
------------------

The following documents all available metrics that current alert in Raptor Browsertime tests.

Contentful Speed Index
======================
Similar to SpeedIndex, except that it uses the contentfulness of a frame to determine visual completeness, rather than relying on histogram differences. The contentfulness is determined by calculating the number of edges found in the frame. A lower number of edges detected gives a smaller contentfulness value (and a smaller visually complete value).


  * **Aliases**: ContentfulSpeedIndex
  * **Tests using it**:
     * **Custom**: `connect <raptor.html#connect-c>`__, `process-switch <raptor.html#process-switch-c>`__, `throttled <raptor.html#throttled-c>`__, `welcome <raptor.html#welcome-c>`__
     * **Desktop**: `amazon <raptor.html#amazon-d>`__, `bing-search <raptor.html#bing-search-d>`__, `buzzfeed <raptor.html#buzzfeed-d>`__, `cnn <raptor.html#cnn-d>`__, `docomo <raptor.html#docomo-d>`__, `ebay <raptor.html#ebay-d>`__, `espn <raptor.html#espn-d>`__, `expedia <raptor.html#expedia-d>`__, `facebook <raptor.html#facebook-d>`__, `fandom <raptor.html#fandom-d>`__, `google-docs <raptor.html#google-docs-d>`__, `google-mail <raptor.html#google-mail-d>`__, `google-search <raptor.html#google-search-d>`__, `google-slides <raptor.html#google-slides-d>`__, `imdb <raptor.html#imdb-d>`__, `imgur <raptor.html#imgur-d>`__, `instagram <raptor.html#instagram-d>`__, `linkedin <raptor.html#linkedin-d>`__, `microsoft <raptor.html#microsoft-d>`__, `netflix <raptor.html#netflix-d>`__, `nytimes <raptor.html#nytimes-d>`__, `office <raptor.html#office-d>`__, `openai <raptor.html#openai-d>`__, `outlook <raptor.html#outlook-d>`__, `paypal <raptor.html#paypal-d>`__, `pinterest <raptor.html#pinterest-d>`__, `reddit <raptor.html#reddit-d>`__, `samsung <raptor.html#samsung-d>`__, `tiktok <raptor.html#tiktok-d>`__, `tumblr <raptor.html#tumblr-d>`__, `twitch <raptor.html#twitch-d>`__, `twitter <raptor.html#twitter-d>`__, `weather <raptor.html#weather-d>`__, `wikia <raptor.html#wikia-d>`__, `wikipedia <raptor.html#wikipedia-d>`__, `yahoo-mail <raptor.html#yahoo-mail-d>`__, `youtube <raptor.html#youtube-d>`__
     * **Interactive**: `cnn-nav <raptor.html#cnn-nav-i>`__, `facebook-nav <raptor.html#facebook-nav-i>`__, `reddit-billgates-ama <raptor.html#reddit-billgates-ama-i>`__, `reddit-billgates-post-1 <raptor.html#reddit-billgates-post-1-i>`__, `reddit-billgates-post-2 <raptor.html#reddit-billgates-post-2-i>`__
     * **Live**: `booking-sf <raptor.html#booking-sf-l>`__, `discord <raptor.html#discord-l>`__, `fashionbeans <raptor.html#fashionbeans-l>`__, `google-accounts <raptor.html#google-accounts-l>`__, `imdb-firefox <raptor.html#imdb-firefox-l>`__, `medium-article <raptor.html#medium-article-l>`__, `people-article <raptor.html#people-article-l>`__, `reddit-thread <raptor.html#reddit-thread-l>`__, `rumble-fox <raptor.html#rumble-fox-l>`__, `stackoverflow-question <raptor.html#stackoverflow-question-l>`__, `urbandictionary-define <raptor.html#urbandictionary-define-l>`__, `wikia-marvel <raptor.html#wikia-marvel-l>`__
     * **Mobile**: `allrecipes <raptor.html#allrecipes-m>`__, `amazon <raptor.html#amazon-m>`__, `amazon-search <raptor.html#amazon-search-m>`__, `bild-de <raptor.html#bild-de-m>`__, `bing <raptor.html#bing-m>`__, `bing-search-restaurants <raptor.html#bing-search-restaurants-m>`__, `booking <raptor.html#booking-m>`__, `cnn <raptor.html#cnn-m>`__, `cnn-ampstories <raptor.html#cnn-ampstories-m>`__, `dailymail <raptor.html#dailymail-m>`__, `ebay-kleinanzeigen <raptor.html#ebay-kleinanzeigen-m>`__, `ebay-kleinanzeigen-search <raptor.html#ebay-kleinanzeigen-search-m>`__, `espn <raptor.html#espn-m>`__, `facebook <raptor.html#facebook-m>`__, `facebook-cristiano <raptor.html#facebook-cristiano-m>`__, `google <raptor.html#google-m>`__, `google-maps <raptor.html#google-maps-m>`__, `google-search-restaurants <raptor.html#google-search-restaurants-m>`__, `imdb <raptor.html#imdb-m>`__, `instagram <raptor.html#instagram-m>`__, `microsoft-support <raptor.html#microsoft-support-m>`__, `reddit <raptor.html#reddit-m>`__, `sina <raptor.html#sina-m>`__, `stackoverflow <raptor.html#stackoverflow-m>`__, `wikipedia <raptor.html#wikipedia-m>`__, `youtube <raptor.html#youtube-m>`__, `youtube-watch <raptor.html#youtube-watch-m>`__


Estimated Frame Latency (Any)
=============================
Similar to estimatedFirstFrameLatency, except that it uses all identified frames during video playback, normalized to be an estimate of when the first frame was displayed by using the expected time offset from the video itself.


  * **Aliases**: estimatedAnyFrameLatency
  * **Tests using it**:
     * **Custom**: `vpl-av1 <raptor.html#vpl-av1-c>`__, `vpl-h264 <raptor.html#vpl-h264-c>`__, `vpl-vp9 <raptor.html#vpl-vp9-c>`__


Estimated Frame Latency (First)
===============================
A metric used to denote the latency on displaying the first frame of a video. Calculated by using videos of the pageload from which key frames are identified by matching a given solid RGB color with fuzz.


  * **Aliases**: estimatedFirstFrameLatency
  * **Tests using it**:
     * **Custom**: `vpl-av1 <raptor.html#vpl-av1-c>`__, `vpl-h264 <raptor.html#vpl-h264-c>`__, `vpl-vp9 <raptor.html#vpl-vp9-c>`__


First Paint
===========
Denotes the first time the browser performs a paint that has content in it (in ms).


  * **Aliases**: First Contentful Composite, First Contentful Paint, fcp
  * **Tests using it**:
     * **Custom**: `browsertime <raptor.html#browsertime-c>`__, `connect <raptor.html#connect-c>`__, `process-switch <raptor.html#process-switch-c>`__, `throttled <raptor.html#throttled-c>`__, `welcome <raptor.html#welcome-c>`__
     * **Desktop**: `amazon <raptor.html#amazon-d>`__, `bing-search <raptor.html#bing-search-d>`__, `buzzfeed <raptor.html#buzzfeed-d>`__, `cnn <raptor.html#cnn-d>`__, `docomo <raptor.html#docomo-d>`__, `ebay <raptor.html#ebay-d>`__, `espn <raptor.html#espn-d>`__, `expedia <raptor.html#expedia-d>`__, `facebook <raptor.html#facebook-d>`__, `fandom <raptor.html#fandom-d>`__, `google-docs <raptor.html#google-docs-d>`__, `google-mail <raptor.html#google-mail-d>`__, `google-search <raptor.html#google-search-d>`__, `google-slides <raptor.html#google-slides-d>`__, `imdb <raptor.html#imdb-d>`__, `imgur <raptor.html#imgur-d>`__, `instagram <raptor.html#instagram-d>`__, `linkedin <raptor.html#linkedin-d>`__, `microsoft <raptor.html#microsoft-d>`__, `netflix <raptor.html#netflix-d>`__, `nytimes <raptor.html#nytimes-d>`__, `office <raptor.html#office-d>`__, `openai <raptor.html#openai-d>`__, `outlook <raptor.html#outlook-d>`__, `paypal <raptor.html#paypal-d>`__, `pinterest <raptor.html#pinterest-d>`__, `reddit <raptor.html#reddit-d>`__, `samsung <raptor.html#samsung-d>`__, `tiktok <raptor.html#tiktok-d>`__, `tumblr <raptor.html#tumblr-d>`__, `twitch <raptor.html#twitch-d>`__, `twitter <raptor.html#twitter-d>`__, `weather <raptor.html#weather-d>`__, `wikia <raptor.html#wikia-d>`__, `wikipedia <raptor.html#wikipedia-d>`__, `yahoo-mail <raptor.html#yahoo-mail-d>`__, `youtube <raptor.html#youtube-d>`__
     * **Interactive**: `cnn-nav <raptor.html#cnn-nav-i>`__, `facebook-nav <raptor.html#facebook-nav-i>`__, `reddit-billgates-ama <raptor.html#reddit-billgates-ama-i>`__, `reddit-billgates-post-1 <raptor.html#reddit-billgates-post-1-i>`__, `reddit-billgates-post-2 <raptor.html#reddit-billgates-post-2-i>`__
     * **Live**: `booking-sf <raptor.html#booking-sf-l>`__, `discord <raptor.html#discord-l>`__, `fashionbeans <raptor.html#fashionbeans-l>`__, `google-accounts <raptor.html#google-accounts-l>`__, `imdb-firefox <raptor.html#imdb-firefox-l>`__, `medium-article <raptor.html#medium-article-l>`__, `people-article <raptor.html#people-article-l>`__, `reddit-thread <raptor.html#reddit-thread-l>`__, `rumble-fox <raptor.html#rumble-fox-l>`__, `stackoverflow-question <raptor.html#stackoverflow-question-l>`__, `urbandictionary-define <raptor.html#urbandictionary-define-l>`__, `wikia-marvel <raptor.html#wikia-marvel-l>`__
     * **Mobile**: `allrecipes <raptor.html#allrecipes-m>`__, `amazon <raptor.html#amazon-m>`__, `amazon-search <raptor.html#amazon-search-m>`__, `bild-de <raptor.html#bild-de-m>`__, `bing <raptor.html#bing-m>`__, `bing-search-restaurants <raptor.html#bing-search-restaurants-m>`__, `booking <raptor.html#booking-m>`__, `cnn <raptor.html#cnn-m>`__, `cnn-ampstories <raptor.html#cnn-ampstories-m>`__, `dailymail <raptor.html#dailymail-m>`__, `ebay-kleinanzeigen <raptor.html#ebay-kleinanzeigen-m>`__, `ebay-kleinanzeigen-search <raptor.html#ebay-kleinanzeigen-search-m>`__, `espn <raptor.html#espn-m>`__, `facebook <raptor.html#facebook-m>`__, `facebook-cristiano <raptor.html#facebook-cristiano-m>`__, `google <raptor.html#google-m>`__, `google-maps <raptor.html#google-maps-m>`__, `google-search-restaurants <raptor.html#google-search-restaurants-m>`__, `imdb <raptor.html#imdb-m>`__, `instagram <raptor.html#instagram-m>`__, `microsoft-support <raptor.html#microsoft-support-m>`__, `reddit <raptor.html#reddit-m>`__, `sina <raptor.html#sina-m>`__, `stackoverflow <raptor.html#stackoverflow-m>`__, `wikipedia <raptor.html#wikipedia-m>`__, `youtube <raptor.html#youtube-m>`__, `youtube-watch <raptor.html#youtube-watch-m>`__
     * **Unittests**: `test-page-1 <raptor.html#test-page-1-u>`__, `test-page-2 <raptor.html#test-page-2-u>`__, `test-page-3 <raptor.html#test-page-3-u>`__, `test-page-4 <raptor.html#test-page-4-u>`__


First Visual Change
===================
The first visual change detected in the test (in ms).

  * **Aliases**: FirstVisualChange
  * **Tests using it**:
     * **Custom**: `connect <raptor.html#connect-c>`__, `process-switch <raptor.html#process-switch-c>`__, `throttled <raptor.html#throttled-c>`__, `welcome <raptor.html#welcome-c>`__
     * **Desktop**: `amazon <raptor.html#amazon-d>`__, `bing-search <raptor.html#bing-search-d>`__, `buzzfeed <raptor.html#buzzfeed-d>`__, `cnn <raptor.html#cnn-d>`__, `docomo <raptor.html#docomo-d>`__, `ebay <raptor.html#ebay-d>`__, `espn <raptor.html#espn-d>`__, `expedia <raptor.html#expedia-d>`__, `facebook <raptor.html#facebook-d>`__, `fandom <raptor.html#fandom-d>`__, `google-docs <raptor.html#google-docs-d>`__, `google-mail <raptor.html#google-mail-d>`__, `google-search <raptor.html#google-search-d>`__, `google-slides <raptor.html#google-slides-d>`__, `imdb <raptor.html#imdb-d>`__, `imgur <raptor.html#imgur-d>`__, `instagram <raptor.html#instagram-d>`__, `linkedin <raptor.html#linkedin-d>`__, `microsoft <raptor.html#microsoft-d>`__, `netflix <raptor.html#netflix-d>`__, `nytimes <raptor.html#nytimes-d>`__, `office <raptor.html#office-d>`__, `openai <raptor.html#openai-d>`__, `outlook <raptor.html#outlook-d>`__, `paypal <raptor.html#paypal-d>`__, `pinterest <raptor.html#pinterest-d>`__, `reddit <raptor.html#reddit-d>`__, `samsung <raptor.html#samsung-d>`__, `tiktok <raptor.html#tiktok-d>`__, `tumblr <raptor.html#tumblr-d>`__, `twitch <raptor.html#twitch-d>`__, `twitter <raptor.html#twitter-d>`__, `weather <raptor.html#weather-d>`__, `wikia <raptor.html#wikia-d>`__, `wikipedia <raptor.html#wikipedia-d>`__, `yahoo-mail <raptor.html#yahoo-mail-d>`__, `youtube <raptor.html#youtube-d>`__
     * **Interactive**: `cnn-nav <raptor.html#cnn-nav-i>`__, `facebook-nav <raptor.html#facebook-nav-i>`__, `reddit-billgates-ama <raptor.html#reddit-billgates-ama-i>`__, `reddit-billgates-post-1 <raptor.html#reddit-billgates-post-1-i>`__, `reddit-billgates-post-2 <raptor.html#reddit-billgates-post-2-i>`__
     * **Live**: `booking-sf <raptor.html#booking-sf-l>`__, `discord <raptor.html#discord-l>`__, `fashionbeans <raptor.html#fashionbeans-l>`__, `google-accounts <raptor.html#google-accounts-l>`__, `imdb-firefox <raptor.html#imdb-firefox-l>`__, `medium-article <raptor.html#medium-article-l>`__, `people-article <raptor.html#people-article-l>`__, `reddit-thread <raptor.html#reddit-thread-l>`__, `rumble-fox <raptor.html#rumble-fox-l>`__, `stackoverflow-question <raptor.html#stackoverflow-question-l>`__, `urbandictionary-define <raptor.html#urbandictionary-define-l>`__, `wikia-marvel <raptor.html#wikia-marvel-l>`__
     * **Mobile**: `allrecipes <raptor.html#allrecipes-m>`__, `amazon <raptor.html#amazon-m>`__, `amazon-search <raptor.html#amazon-search-m>`__, `bild-de <raptor.html#bild-de-m>`__, `bing <raptor.html#bing-m>`__, `bing-search-restaurants <raptor.html#bing-search-restaurants-m>`__, `booking <raptor.html#booking-m>`__, `cnn <raptor.html#cnn-m>`__, `cnn-ampstories <raptor.html#cnn-ampstories-m>`__, `dailymail <raptor.html#dailymail-m>`__, `ebay-kleinanzeigen <raptor.html#ebay-kleinanzeigen-m>`__, `ebay-kleinanzeigen-search <raptor.html#ebay-kleinanzeigen-search-m>`__, `espn <raptor.html#espn-m>`__, `facebook <raptor.html#facebook-m>`__, `facebook-cristiano <raptor.html#facebook-cristiano-m>`__, `google <raptor.html#google-m>`__, `google-maps <raptor.html#google-maps-m>`__, `google-search-restaurants <raptor.html#google-search-restaurants-m>`__, `imdb <raptor.html#imdb-m>`__, `instagram <raptor.html#instagram-m>`__, `microsoft-support <raptor.html#microsoft-support-m>`__, `reddit <raptor.html#reddit-m>`__, `sina <raptor.html#sina-m>`__, `stackoverflow <raptor.html#stackoverflow-m>`__, `wikipedia <raptor.html#wikipedia-m>`__, `youtube <raptor.html#youtube-m>`__, `youtube-watch <raptor.html#youtube-watch-m>`__


Largest Contentful Paint
========================
The time (in ms) at which the largest piece of content on the page was rendered/painted.


  * **Aliases**: largestContentfulPaint, lcp
  * **Tests using it**:
     * **Desktop**: `amazon <raptor.html#amazon-d>`__, `bing-search <raptor.html#bing-search-d>`__, `buzzfeed <raptor.html#buzzfeed-d>`__, `cnn <raptor.html#cnn-d>`__, `docomo <raptor.html#docomo-d>`__, `ebay <raptor.html#ebay-d>`__, `espn <raptor.html#espn-d>`__, `expedia <raptor.html#expedia-d>`__, `facebook <raptor.html#facebook-d>`__, `fandom <raptor.html#fandom-d>`__, `google-docs <raptor.html#google-docs-d>`__, `google-mail <raptor.html#google-mail-d>`__, `google-search <raptor.html#google-search-d>`__, `google-slides <raptor.html#google-slides-d>`__, `imdb <raptor.html#imdb-d>`__, `imgur <raptor.html#imgur-d>`__, `instagram <raptor.html#instagram-d>`__, `linkedin <raptor.html#linkedin-d>`__, `microsoft <raptor.html#microsoft-d>`__, `netflix <raptor.html#netflix-d>`__, `nytimes <raptor.html#nytimes-d>`__, `office <raptor.html#office-d>`__, `openai <raptor.html#openai-d>`__, `outlook <raptor.html#outlook-d>`__, `paypal <raptor.html#paypal-d>`__, `pinterest <raptor.html#pinterest-d>`__, `reddit <raptor.html#reddit-d>`__, `samsung <raptor.html#samsung-d>`__, `tiktok <raptor.html#tiktok-d>`__, `tumblr <raptor.html#tumblr-d>`__, `twitter <raptor.html#twitter-d>`__, `weather <raptor.html#weather-d>`__, `wikia <raptor.html#wikia-d>`__, `wikipedia <raptor.html#wikipedia-d>`__, `yahoo-mail <raptor.html#yahoo-mail-d>`__, `youtube <raptor.html#youtube-d>`__
     * **Mobile**: `allrecipes <raptor.html#allrecipes-m>`__, `amazon <raptor.html#amazon-m>`__, `amazon-search <raptor.html#amazon-search-m>`__, `bild-de <raptor.html#bild-de-m>`__, `bing <raptor.html#bing-m>`__, `bing-search-restaurants <raptor.html#bing-search-restaurants-m>`__, `booking <raptor.html#booking-m>`__, `cnn <raptor.html#cnn-m>`__, `cnn-ampstories <raptor.html#cnn-ampstories-m>`__, `dailymail <raptor.html#dailymail-m>`__, `ebay-kleinanzeigen <raptor.html#ebay-kleinanzeigen-m>`__, `ebay-kleinanzeigen-search <raptor.html#ebay-kleinanzeigen-search-m>`__, `espn <raptor.html#espn-m>`__, `facebook <raptor.html#facebook-m>`__, `facebook-cristiano <raptor.html#facebook-cristiano-m>`__, `google <raptor.html#google-m>`__, `google-maps <raptor.html#google-maps-m>`__, `google-search-restaurants <raptor.html#google-search-restaurants-m>`__, `imdb <raptor.html#imdb-m>`__, `instagram <raptor.html#instagram-m>`__, `microsoft-support <raptor.html#microsoft-support-m>`__, `reddit <raptor.html#reddit-m>`__, `sina <raptor.html#sina-m>`__, `stackoverflow <raptor.html#stackoverflow-m>`__, `wikipedia <raptor.html#wikipedia-m>`__, `youtube <raptor.html#youtube-m>`__, `youtube-watch <raptor.html#youtube-watch-m>`__


Last Visual Change
==================
The last visual change detected in the test (in ms).

  * **Aliases**: LastVisualChange
  * **Tests using it**:
     * **Custom**: `process-switch <raptor.html#process-switch-c>`__, `welcome <raptor.html#welcome-c>`__
     * **Desktop**: `amazon <raptor.html#amazon-d>`__, `bing-search <raptor.html#bing-search-d>`__, `buzzfeed <raptor.html#buzzfeed-d>`__, `cnn <raptor.html#cnn-d>`__, `docomo <raptor.html#docomo-d>`__, `ebay <raptor.html#ebay-d>`__, `espn <raptor.html#espn-d>`__, `expedia <raptor.html#expedia-d>`__, `facebook <raptor.html#facebook-d>`__, `fandom <raptor.html#fandom-d>`__, `google-docs <raptor.html#google-docs-d>`__, `google-mail <raptor.html#google-mail-d>`__, `google-search <raptor.html#google-search-d>`__, `google-slides <raptor.html#google-slides-d>`__, `imdb <raptor.html#imdb-d>`__, `imgur <raptor.html#imgur-d>`__, `instagram <raptor.html#instagram-d>`__, `linkedin <raptor.html#linkedin-d>`__, `microsoft <raptor.html#microsoft-d>`__, `netflix <raptor.html#netflix-d>`__, `nytimes <raptor.html#nytimes-d>`__, `office <raptor.html#office-d>`__, `openai <raptor.html#openai-d>`__, `outlook <raptor.html#outlook-d>`__, `paypal <raptor.html#paypal-d>`__, `pinterest <raptor.html#pinterest-d>`__, `reddit <raptor.html#reddit-d>`__, `samsung <raptor.html#samsung-d>`__, `tiktok <raptor.html#tiktok-d>`__, `tumblr <raptor.html#tumblr-d>`__, `twitch <raptor.html#twitch-d>`__, `twitter <raptor.html#twitter-d>`__, `weather <raptor.html#weather-d>`__, `wikia <raptor.html#wikia-d>`__, `wikipedia <raptor.html#wikipedia-d>`__, `yahoo-mail <raptor.html#yahoo-mail-d>`__, `youtube <raptor.html#youtube-d>`__
     * **Interactive**: `cnn-nav <raptor.html#cnn-nav-i>`__, `facebook-nav <raptor.html#facebook-nav-i>`__, `reddit-billgates-ama <raptor.html#reddit-billgates-ama-i>`__, `reddit-billgates-post-1 <raptor.html#reddit-billgates-post-1-i>`__, `reddit-billgates-post-2 <raptor.html#reddit-billgates-post-2-i>`__
     * **Live**: `booking-sf <raptor.html#booking-sf-l>`__, `discord <raptor.html#discord-l>`__, `fashionbeans <raptor.html#fashionbeans-l>`__, `google-accounts <raptor.html#google-accounts-l>`__, `imdb-firefox <raptor.html#imdb-firefox-l>`__, `medium-article <raptor.html#medium-article-l>`__, `people-article <raptor.html#people-article-l>`__, `reddit-thread <raptor.html#reddit-thread-l>`__, `rumble-fox <raptor.html#rumble-fox-l>`__, `stackoverflow-question <raptor.html#stackoverflow-question-l>`__, `urbandictionary-define <raptor.html#urbandictionary-define-l>`__, `wikia-marvel <raptor.html#wikia-marvel-l>`__
     * **Mobile**: `allrecipes <raptor.html#allrecipes-m>`__, `amazon <raptor.html#amazon-m>`__, `amazon-search <raptor.html#amazon-search-m>`__, `bild-de <raptor.html#bild-de-m>`__, `bing <raptor.html#bing-m>`__, `bing-search-restaurants <raptor.html#bing-search-restaurants-m>`__, `booking <raptor.html#booking-m>`__, `cnn <raptor.html#cnn-m>`__, `cnn-ampstories <raptor.html#cnn-ampstories-m>`__, `dailymail <raptor.html#dailymail-m>`__, `ebay-kleinanzeigen <raptor.html#ebay-kleinanzeigen-m>`__, `ebay-kleinanzeigen-search <raptor.html#ebay-kleinanzeigen-search-m>`__, `espn <raptor.html#espn-m>`__, `facebook <raptor.html#facebook-m>`__, `facebook-cristiano <raptor.html#facebook-cristiano-m>`__, `google <raptor.html#google-m>`__, `google-maps <raptor.html#google-maps-m>`__, `google-search-restaurants <raptor.html#google-search-restaurants-m>`__, `imdb <raptor.html#imdb-m>`__, `instagram <raptor.html#instagram-m>`__, `microsoft-support <raptor.html#microsoft-support-m>`__, `reddit <raptor.html#reddit-m>`__, `sina <raptor.html#sina-m>`__, `stackoverflow <raptor.html#stackoverflow-m>`__, `wikipedia <raptor.html#wikipedia-m>`__, `youtube <raptor.html#youtube-m>`__, `youtube-watch <raptor.html#youtube-watch-m>`__


Load Time
=========
The time it took for the page to complete loading (in ms).

  * **Aliases**: loadtime
  * **Tests using it**:
     * **Custom**: `browsertime <raptor.html#browsertime-c>`__, `connect <raptor.html#connect-c>`__, `process-switch <raptor.html#process-switch-c>`__, `throttled <raptor.html#throttled-c>`__, `welcome <raptor.html#welcome-c>`__
     * **Desktop**: `amazon <raptor.html#amazon-d>`__, `bing-search <raptor.html#bing-search-d>`__, `buzzfeed <raptor.html#buzzfeed-d>`__, `cnn <raptor.html#cnn-d>`__, `docomo <raptor.html#docomo-d>`__, `ebay <raptor.html#ebay-d>`__, `espn <raptor.html#espn-d>`__, `expedia <raptor.html#expedia-d>`__, `facebook <raptor.html#facebook-d>`__, `fandom <raptor.html#fandom-d>`__, `google-docs <raptor.html#google-docs-d>`__, `google-mail <raptor.html#google-mail-d>`__, `google-search <raptor.html#google-search-d>`__, `google-slides <raptor.html#google-slides-d>`__, `imdb <raptor.html#imdb-d>`__, `imgur <raptor.html#imgur-d>`__, `instagram <raptor.html#instagram-d>`__, `linkedin <raptor.html#linkedin-d>`__, `microsoft <raptor.html#microsoft-d>`__, `netflix <raptor.html#netflix-d>`__, `nytimes <raptor.html#nytimes-d>`__, `office <raptor.html#office-d>`__, `openai <raptor.html#openai-d>`__, `outlook <raptor.html#outlook-d>`__, `paypal <raptor.html#paypal-d>`__, `pinterest <raptor.html#pinterest-d>`__, `reddit <raptor.html#reddit-d>`__, `samsung <raptor.html#samsung-d>`__, `tiktok <raptor.html#tiktok-d>`__, `tumblr <raptor.html#tumblr-d>`__, `twitch <raptor.html#twitch-d>`__, `twitter <raptor.html#twitter-d>`__, `weather <raptor.html#weather-d>`__, `wikia <raptor.html#wikia-d>`__, `wikipedia <raptor.html#wikipedia-d>`__, `yahoo-mail <raptor.html#yahoo-mail-d>`__, `youtube <raptor.html#youtube-d>`__
     * **Interactive**: `cnn-nav <raptor.html#cnn-nav-i>`__, `facebook-nav <raptor.html#facebook-nav-i>`__, `reddit-billgates-ama <raptor.html#reddit-billgates-ama-i>`__, `reddit-billgates-post-1 <raptor.html#reddit-billgates-post-1-i>`__, `reddit-billgates-post-2 <raptor.html#reddit-billgates-post-2-i>`__
     * **Live**: `booking-sf <raptor.html#booking-sf-l>`__, `discord <raptor.html#discord-l>`__, `fashionbeans <raptor.html#fashionbeans-l>`__, `google-accounts <raptor.html#google-accounts-l>`__, `imdb-firefox <raptor.html#imdb-firefox-l>`__, `medium-article <raptor.html#medium-article-l>`__, `people-article <raptor.html#people-article-l>`__, `reddit-thread <raptor.html#reddit-thread-l>`__, `rumble-fox <raptor.html#rumble-fox-l>`__, `stackoverflow-question <raptor.html#stackoverflow-question-l>`__, `urbandictionary-define <raptor.html#urbandictionary-define-l>`__, `wikia-marvel <raptor.html#wikia-marvel-l>`__
     * **Mobile**: `allrecipes <raptor.html#allrecipes-m>`__, `amazon <raptor.html#amazon-m>`__, `amazon-search <raptor.html#amazon-search-m>`__, `bild-de <raptor.html#bild-de-m>`__, `bing <raptor.html#bing-m>`__, `bing-search-restaurants <raptor.html#bing-search-restaurants-m>`__, `booking <raptor.html#booking-m>`__, `cnn <raptor.html#cnn-m>`__, `cnn-ampstories <raptor.html#cnn-ampstories-m>`__, `dailymail <raptor.html#dailymail-m>`__, `ebay-kleinanzeigen <raptor.html#ebay-kleinanzeigen-m>`__, `ebay-kleinanzeigen-search <raptor.html#ebay-kleinanzeigen-search-m>`__, `espn <raptor.html#espn-m>`__, `facebook <raptor.html#facebook-m>`__, `facebook-cristiano <raptor.html#facebook-cristiano-m>`__, `google <raptor.html#google-m>`__, `google-maps <raptor.html#google-maps-m>`__, `google-search-restaurants <raptor.html#google-search-restaurants-m>`__, `imdb <raptor.html#imdb-m>`__, `instagram <raptor.html#instagram-m>`__, `microsoft-support <raptor.html#microsoft-support-m>`__, `reddit <raptor.html#reddit-m>`__, `sina <raptor.html#sina-m>`__, `stackoverflow <raptor.html#stackoverflow-m>`__, `wikipedia <raptor.html#wikipedia-m>`__, `youtube <raptor.html#youtube-m>`__, `youtube-watch <raptor.html#youtube-watch-m>`__
     * **Unittests**: `test-page-1 <raptor.html#test-page-1-u>`__, `test-page-2 <raptor.html#test-page-2-u>`__, `test-page-3 <raptor.html#test-page-3-u>`__, `test-page-4 <raptor.html#test-page-4-u>`__


Perceptual Speed Index
======================
Similar to SpeedIndex, except that it uses the structural similarity index measure (ssim) to determine visual completeness. This technique compares the luminance, contrast, and structure of the frames (a given frame vs. a final frame) to determine the completeness.


  * **Aliases**: PerceptualSpeedIndex
  * **Tests using it**:
     * **Custom**: `connect <raptor.html#connect-c>`__, `process-switch <raptor.html#process-switch-c>`__, `throttled <raptor.html#throttled-c>`__, `welcome <raptor.html#welcome-c>`__
     * **Desktop**: `amazon <raptor.html#amazon-d>`__, `bing-search <raptor.html#bing-search-d>`__, `buzzfeed <raptor.html#buzzfeed-d>`__, `cnn <raptor.html#cnn-d>`__, `docomo <raptor.html#docomo-d>`__, `ebay <raptor.html#ebay-d>`__, `espn <raptor.html#espn-d>`__, `expedia <raptor.html#expedia-d>`__, `facebook <raptor.html#facebook-d>`__, `fandom <raptor.html#fandom-d>`__, `google-docs <raptor.html#google-docs-d>`__, `google-mail <raptor.html#google-mail-d>`__, `google-search <raptor.html#google-search-d>`__, `google-slides <raptor.html#google-slides-d>`__, `imdb <raptor.html#imdb-d>`__, `imgur <raptor.html#imgur-d>`__, `instagram <raptor.html#instagram-d>`__, `linkedin <raptor.html#linkedin-d>`__, `microsoft <raptor.html#microsoft-d>`__, `netflix <raptor.html#netflix-d>`__, `nytimes <raptor.html#nytimes-d>`__, `office <raptor.html#office-d>`__, `openai <raptor.html#openai-d>`__, `outlook <raptor.html#outlook-d>`__, `paypal <raptor.html#paypal-d>`__, `pinterest <raptor.html#pinterest-d>`__, `reddit <raptor.html#reddit-d>`__, `samsung <raptor.html#samsung-d>`__, `tiktok <raptor.html#tiktok-d>`__, `tumblr <raptor.html#tumblr-d>`__, `twitch <raptor.html#twitch-d>`__, `twitter <raptor.html#twitter-d>`__, `weather <raptor.html#weather-d>`__, `wikia <raptor.html#wikia-d>`__, `wikipedia <raptor.html#wikipedia-d>`__, `yahoo-mail <raptor.html#yahoo-mail-d>`__, `youtube <raptor.html#youtube-d>`__
     * **Interactive**: `cnn-nav <raptor.html#cnn-nav-i>`__, `facebook-nav <raptor.html#facebook-nav-i>`__, `reddit-billgates-ama <raptor.html#reddit-billgates-ama-i>`__, `reddit-billgates-post-1 <raptor.html#reddit-billgates-post-1-i>`__, `reddit-billgates-post-2 <raptor.html#reddit-billgates-post-2-i>`__
     * **Live**: `booking-sf <raptor.html#booking-sf-l>`__, `discord <raptor.html#discord-l>`__, `fashionbeans <raptor.html#fashionbeans-l>`__, `google-accounts <raptor.html#google-accounts-l>`__, `imdb-firefox <raptor.html#imdb-firefox-l>`__, `medium-article <raptor.html#medium-article-l>`__, `people-article <raptor.html#people-article-l>`__, `reddit-thread <raptor.html#reddit-thread-l>`__, `rumble-fox <raptor.html#rumble-fox-l>`__, `stackoverflow-question <raptor.html#stackoverflow-question-l>`__, `urbandictionary-define <raptor.html#urbandictionary-define-l>`__, `wikia-marvel <raptor.html#wikia-marvel-l>`__
     * **Mobile**: `allrecipes <raptor.html#allrecipes-m>`__, `amazon <raptor.html#amazon-m>`__, `amazon-search <raptor.html#amazon-search-m>`__, `bild-de <raptor.html#bild-de-m>`__, `bing <raptor.html#bing-m>`__, `bing-search-restaurants <raptor.html#bing-search-restaurants-m>`__, `booking <raptor.html#booking-m>`__, `cnn <raptor.html#cnn-m>`__, `cnn-ampstories <raptor.html#cnn-ampstories-m>`__, `dailymail <raptor.html#dailymail-m>`__, `ebay-kleinanzeigen <raptor.html#ebay-kleinanzeigen-m>`__, `ebay-kleinanzeigen-search <raptor.html#ebay-kleinanzeigen-search-m>`__, `espn <raptor.html#espn-m>`__, `facebook <raptor.html#facebook-m>`__, `facebook-cristiano <raptor.html#facebook-cristiano-m>`__, `google <raptor.html#google-m>`__, `google-maps <raptor.html#google-maps-m>`__, `google-search-restaurants <raptor.html#google-search-restaurants-m>`__, `imdb <raptor.html#imdb-m>`__, `instagram <raptor.html#instagram-m>`__, `microsoft-support <raptor.html#microsoft-support-m>`__, `reddit <raptor.html#reddit-m>`__, `sina <raptor.html#sina-m>`__, `stackoverflow <raptor.html#stackoverflow-m>`__, `wikipedia <raptor.html#wikipedia-m>`__, `youtube <raptor.html#youtube-m>`__, `youtube-watch <raptor.html#youtube-watch-m>`__


Speed Index
===========
A metric used to denote the speed at which a page loaded. Lower values indicate faster pageloads. Units are in (Visually-Complete x Milliseconds). Calculated by using videos of the pageload which provide a measure of visual completeness. Visual completeness is calculated by comparing the histogram of a given frame to the final frame of the pageload. The SpeedIndex is calculated as the area between the curves of a constant line at y=1, and the graph of the visual completeness from 0ms to when visual completeness reaches 100% (or hits the y=1 line).


  * **Aliases**: SpeedIndex
  * **Tests using it**:
     * **Custom**: `connect <raptor.html#connect-c>`__, `process-switch <raptor.html#process-switch-c>`__, `throttled <raptor.html#throttled-c>`__, `welcome <raptor.html#welcome-c>`__
     * **Desktop**: `amazon <raptor.html#amazon-d>`__, `bing-search <raptor.html#bing-search-d>`__, `buzzfeed <raptor.html#buzzfeed-d>`__, `cnn <raptor.html#cnn-d>`__, `docomo <raptor.html#docomo-d>`__, `ebay <raptor.html#ebay-d>`__, `espn <raptor.html#espn-d>`__, `expedia <raptor.html#expedia-d>`__, `facebook <raptor.html#facebook-d>`__, `fandom <raptor.html#fandom-d>`__, `google-docs <raptor.html#google-docs-d>`__, `google-mail <raptor.html#google-mail-d>`__, `google-search <raptor.html#google-search-d>`__, `google-slides <raptor.html#google-slides-d>`__, `imdb <raptor.html#imdb-d>`__, `imgur <raptor.html#imgur-d>`__, `instagram <raptor.html#instagram-d>`__, `linkedin <raptor.html#linkedin-d>`__, `microsoft <raptor.html#microsoft-d>`__, `netflix <raptor.html#netflix-d>`__, `nytimes <raptor.html#nytimes-d>`__, `office <raptor.html#office-d>`__, `openai <raptor.html#openai-d>`__, `outlook <raptor.html#outlook-d>`__, `paypal <raptor.html#paypal-d>`__, `pinterest <raptor.html#pinterest-d>`__, `reddit <raptor.html#reddit-d>`__, `samsung <raptor.html#samsung-d>`__, `tiktok <raptor.html#tiktok-d>`__, `tumblr <raptor.html#tumblr-d>`__, `twitch <raptor.html#twitch-d>`__, `twitter <raptor.html#twitter-d>`__, `weather <raptor.html#weather-d>`__, `wikia <raptor.html#wikia-d>`__, `wikipedia <raptor.html#wikipedia-d>`__, `yahoo-mail <raptor.html#yahoo-mail-d>`__, `youtube <raptor.html#youtube-d>`__
     * **Interactive**: `cnn-nav <raptor.html#cnn-nav-i>`__, `facebook-nav <raptor.html#facebook-nav-i>`__, `reddit-billgates-ama <raptor.html#reddit-billgates-ama-i>`__, `reddit-billgates-post-1 <raptor.html#reddit-billgates-post-1-i>`__, `reddit-billgates-post-2 <raptor.html#reddit-billgates-post-2-i>`__
     * **Live**: `booking-sf <raptor.html#booking-sf-l>`__, `discord <raptor.html#discord-l>`__, `fashionbeans <raptor.html#fashionbeans-l>`__, `google-accounts <raptor.html#google-accounts-l>`__, `imdb-firefox <raptor.html#imdb-firefox-l>`__, `medium-article <raptor.html#medium-article-l>`__, `people-article <raptor.html#people-article-l>`__, `reddit-thread <raptor.html#reddit-thread-l>`__, `rumble-fox <raptor.html#rumble-fox-l>`__, `stackoverflow-question <raptor.html#stackoverflow-question-l>`__, `urbandictionary-define <raptor.html#urbandictionary-define-l>`__, `wikia-marvel <raptor.html#wikia-marvel-l>`__
     * **Mobile**: `allrecipes <raptor.html#allrecipes-m>`__, `amazon <raptor.html#amazon-m>`__, `amazon-search <raptor.html#amazon-search-m>`__, `bild-de <raptor.html#bild-de-m>`__, `bing <raptor.html#bing-m>`__, `bing-search-restaurants <raptor.html#bing-search-restaurants-m>`__, `booking <raptor.html#booking-m>`__, `cnn <raptor.html#cnn-m>`__, `cnn-ampstories <raptor.html#cnn-ampstories-m>`__, `dailymail <raptor.html#dailymail-m>`__, `ebay-kleinanzeigen <raptor.html#ebay-kleinanzeigen-m>`__, `ebay-kleinanzeigen-search <raptor.html#ebay-kleinanzeigen-search-m>`__, `espn <raptor.html#espn-m>`__, `facebook <raptor.html#facebook-m>`__, `facebook-cristiano <raptor.html#facebook-cristiano-m>`__, `google <raptor.html#google-m>`__, `google-maps <raptor.html#google-maps-m>`__, `google-search-restaurants <raptor.html#google-search-restaurants-m>`__, `imdb <raptor.html#imdb-m>`__, `instagram <raptor.html#instagram-m>`__, `microsoft-support <raptor.html#microsoft-support-m>`__, `reddit <raptor.html#reddit-m>`__, `sina <raptor.html#sina-m>`__, `stackoverflow <raptor.html#stackoverflow-m>`__, `wikipedia <raptor.html#wikipedia-m>`__, `youtube <raptor.html#youtube-m>`__, `youtube-watch <raptor.html#youtube-watch-m>`__


Youtube Playback Metrics
========================
Metrics starting with VP9/H264 give the number of frames dropped, and painted.


  * **Aliases**: H264, VP9
  * **Tests using it**:
     * **Benchmarks**: `youtube-playback-hfr <raptor.html#youtube-playback-hfr-b>`__


cpu Time
========
Specifies cumulative CPU usage in milliseconds across all threads of the process since the process start.


  * **Aliases**: cpuTime
  * **Tests using it**:
     * **Benchmarks**: `speedometer <raptor.html#speedometer-b>`__, `speedometer3 <raptor.html#speedometer3-b>`__, `youtube-playback-h264-1080p30 <raptor.html#youtube-playback-h264-1080p30-b>`__, `youtube-playback-h264-1080p60 <raptor.html#youtube-playback-h264-1080p60-b>`__, `youtube-playback-h264-full-1080p30 <raptor.html#youtube-playback-h264-full-1080p30-b>`__, `youtube-playback-h264-full-1080p60 <raptor.html#youtube-playback-h264-full-1080p60-b>`__, `youtube-playback-v9-1080p30 <raptor.html#youtube-playback-v9-1080p30-b>`__, `youtube-playback-v9-1080p60 <raptor.html#youtube-playback-v9-1080p60-b>`__, `youtube-playback-v9-full-1080p30 <raptor.html#youtube-playback-v9-full-1080p30-b>`__, `youtube-playback-v9-full-1080p60 <raptor.html#youtube-playback-v9-full-1080p60-b>`__
     * **Custom**: `addMab1 <raptor.html#addMab1-c>`__, `addMabN <raptor.html#addMabN-c>`__, `addMar1 <raptor.html#addMar1-c>`__, `addMarN <raptor.html#addMarN-c>`__, `addMbl1 <raptor.html#addMbl1-c>`__, `addMblN <raptor.html#addMblN-c>`__, `addkAB1 <raptor.html#addkAB1-c>`__, `addkABN <raptor.html#addkABN-c>`__, `addkAR1 <raptor.html#addkAR1-c>`__, `addkARN <raptor.html#addkARN-c>`__, `addkBL1 <raptor.html#addkBL1-c>`__, `addkBLN <raptor.html#addkBLN-c>`__, `getkeyrng <raptor.html#getkeyrng-c>`__, `idb-open-few-par <raptor.html#idb-open-few-par-c>`__, `idb-open-few-seq <raptor.html#idb-open-few-seq-c>`__, `idb-open-many-par <raptor.html#idb-open-many-par-c>`__, `idb-open-many-seq <raptor.html#idb-open-many-seq-c>`__
     * **Desktop**: `amazon <raptor.html#amazon-d>`__, `bing-search <raptor.html#bing-search-d>`__, `buzzfeed <raptor.html#buzzfeed-d>`__, `cnn <raptor.html#cnn-d>`__, `docomo <raptor.html#docomo-d>`__, `ebay <raptor.html#ebay-d>`__, `espn <raptor.html#espn-d>`__, `expedia <raptor.html#expedia-d>`__, `facebook <raptor.html#facebook-d>`__, `fandom <raptor.html#fandom-d>`__, `google-docs <raptor.html#google-docs-d>`__, `google-mail <raptor.html#google-mail-d>`__, `google-search <raptor.html#google-search-d>`__, `google-slides <raptor.html#google-slides-d>`__, `imdb <raptor.html#imdb-d>`__, `imgur <raptor.html#imgur-d>`__, `instagram <raptor.html#instagram-d>`__, `linkedin <raptor.html#linkedin-d>`__, `microsoft <raptor.html#microsoft-d>`__, `netflix <raptor.html#netflix-d>`__, `nytimes <raptor.html#nytimes-d>`__, `office <raptor.html#office-d>`__, `openai <raptor.html#openai-d>`__, `outlook <raptor.html#outlook-d>`__, `paypal <raptor.html#paypal-d>`__, `pinterest <raptor.html#pinterest-d>`__, `reddit <raptor.html#reddit-d>`__, `samsung <raptor.html#samsung-d>`__, `tiktok <raptor.html#tiktok-d>`__, `tumblr <raptor.html#tumblr-d>`__, `twitch <raptor.html#twitch-d>`__, `twitter <raptor.html#twitter-d>`__, `weather <raptor.html#weather-d>`__, `wikia <raptor.html#wikia-d>`__, `wikipedia <raptor.html#wikipedia-d>`__, `yahoo-mail <raptor.html#yahoo-mail-d>`__, `youtube <raptor.html#youtube-d>`__
     * **Interactive**: `cnn-nav <raptor.html#cnn-nav-i>`__, `facebook-nav <raptor.html#facebook-nav-i>`__, `reddit-billgates-ama <raptor.html#reddit-billgates-ama-i>`__, `reddit-billgates-post-1 <raptor.html#reddit-billgates-post-1-i>`__, `reddit-billgates-post-2 <raptor.html#reddit-billgates-post-2-i>`__
     * **Mobile**: `allrecipes <raptor.html#allrecipes-m>`__, `amazon <raptor.html#amazon-m>`__, `amazon-search <raptor.html#amazon-search-m>`__, `bild-de <raptor.html#bild-de-m>`__, `bing <raptor.html#bing-m>`__, `bing-search-restaurants <raptor.html#bing-search-restaurants-m>`__, `booking <raptor.html#booking-m>`__, `cnn <raptor.html#cnn-m>`__, `cnn-ampstories <raptor.html#cnn-ampstories-m>`__, `dailymail <raptor.html#dailymail-m>`__, `ebay-kleinanzeigen <raptor.html#ebay-kleinanzeigen-m>`__, `ebay-kleinanzeigen-search <raptor.html#ebay-kleinanzeigen-search-m>`__, `espn <raptor.html#espn-m>`__, `facebook <raptor.html#facebook-m>`__, `facebook-cristiano <raptor.html#facebook-cristiano-m>`__, `google <raptor.html#google-m>`__, `google-maps <raptor.html#google-maps-m>`__, `google-search-restaurants <raptor.html#google-search-restaurants-m>`__, `imdb <raptor.html#imdb-m>`__, `instagram <raptor.html#instagram-m>`__, `microsoft-support <raptor.html#microsoft-support-m>`__, `reddit <raptor.html#reddit-m>`__, `sina <raptor.html#sina-m>`__, `stackoverflow <raptor.html#stackoverflow-m>`__, `wikipedia <raptor.html#wikipedia-m>`__, `youtube <raptor.html#youtube-m>`__, `youtube-watch <raptor.html#youtube-watch-m>`__


