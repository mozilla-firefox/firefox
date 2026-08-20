# Metrics Gathered

```{contents}
:depth: 2
:local: true
```

**WARNING: This page is still being actively developed.**

This document contains information about the metrics gathered in Browsertime tests, as well as detailed information about how they are gathered.

## Pageload Tests

For Browsertime pageload tests, there is a limited set of metrics that we collect (which can easily be expanded). Currently we divide these into two sets of metrics: (i) visual metrics, and (ii) technical metrics. These are gathered through two types of tests called warm and cold pageload tests. We have combined these two into a single "Chimera" mode which you'll find in the Treeherder tasks.

Below, you can find the process of how we run Warm, Cold, and Chimera pageload tests.

### Warm Pageload

In this pageload test type, we open the browser, then repeatedly navigate to the same page without restarting the browser in between cycles.

- A new, or conditioned, browser profile is created
- The browser is started up
- Post-startup browser settle pause of 30 seconds or 1 second if using a conditioned profile
- A new tab is opened
- The test URL is loaded; measurements taken
- The tab is reloaded `X` more times (for `X` replicates); measurements taken each time

NOTES:
\- The measurements from the first page-load are not included in overall results metrics because of first load noise; however they are listed in the JSON artifacts
\- The bytecode cache gets populated on the first test cycle, and subsequent iterations will already have the cache built to reduce noise.

### Cold Pageload

In this pageload test type, we open the browser, navigate to the page, then restart the browser before performing the next cycle.

- A new, or conditioned, browser profile is created
- The browser is started up
- Post-startup browser settle pause of 30 seconds or 1 second if using a conditioned profile
- A new tab is opened
- The test URL is loaded; measurements taken
- The browser is shut down
- Entire process is repeated for the remaining browser cycles

NOTE: The measurements from all browser cycles are used to calculate overall results.

### Chimera Pageload

A new mode for pageload testing is called Chimera mode. It combines the warm and cold variants into a single test. This test mode is used in our Taskcluster tasks.

- A new, or conditioned, browser profile is created
- The browser is started up
- Post-startup browser settle pause of 30 seconds or 1 second if using a conditioned profile
- A new tab is opened
- The test URL is loaded; measurements taken for `Cold pageload`
- Navigate to a secondary URL (to preserve the content process)
- The test URL is loaded again; measurements taken for `Warm pageload`
- The desktop browser is shut down
- Entire process is repeated for the remaining browser cycles

NOTE: The bytecode cache mentioned in Warm pageloads still applies here.

### Technical Metrics

Technical metrics are values obtained directly from the browser. This includes metrics like First Paint, DOM Content Flushed, etc..

### Visual Metrics

Visual metrics can be obtained by running Raptor Browsertime with `--browsertime-visualmetrics`, it will record a video of the page being loaded and then process this video to build the metrics. The video is either produced using FFMPEG (with `--browsertime-no-ffwindowrecorder`) or the Firefox Window Recorder (default).

## Benchmarks

Benchmarks gather their own custom metrics, unlike the pageload tests above. Please ping the owners of those benchmarks to determine what they mean and how they are produced, or reach out to the Performance Test and Tooling team in #perftest on Element.

## Metric Definitions

The following documents all available metrics that current alert in Raptor Browsertime tests.

### Contentful Speed Index

Similar to SpeedIndex, except that it uses the contentfulness of a frame to determine visual completeness, rather than relying on histogram differences. The contentfulness is determined by calculating the number of edges found in the frame. A lower number of edges detected gives a smaller contentfulness value (and a smaller visually complete value).


* **Aliases**: ContentfulSpeedIndex
* **Tests using it**:
  * **Custom**: {ref}`connect <connect-c>`, {ref}`process-switch <process-switch-c>`, {ref}`throttled <throttled-c>`, {ref}`welcome <welcome-c>`
  * **Desktop**: {ref}`amazon <amazon-d>`, {ref}`bing-search <bing-search-d>`, {ref}`buzzfeed <buzzfeed-d>`, {ref}`cnn <cnn-d>`, {ref}`docomo <docomo-d>`, {ref}`ebay <ebay-d>`, {ref}`espn <espn-d>`, {ref}`expedia <expedia-d>`, {ref}`facebook <facebook-d>`, {ref}`fandom <fandom-d>`, {ref}`google-docs <google-docs-d>`, {ref}`google-mail <google-mail-d>`, {ref}`google-search <google-search-d>`, {ref}`google-slides <google-slides-d>`, {ref}`imdb <imdb-d>`, {ref}`imgur <imgur-d>`, {ref}`instagram <instagram-d>`, {ref}`linkedin <linkedin-d>`, {ref}`microsoft <microsoft-d>`, {ref}`netflix <netflix-d>`, {ref}`nytimes <nytimes-d>`, {ref}`office <office-d>`, {ref}`openai <openai-d>`, {ref}`outlook <outlook-d>`, {ref}`paypal <paypal-d>`, {ref}`pinterest <pinterest-d>`, {ref}`reddit <reddit-d>`, {ref}`samsung <samsung-d>`, {ref}`tiktok <tiktok-d>`, {ref}`tumblr <tumblr-d>`, {ref}`twitch <twitch-d>`, {ref}`twitter <twitter-d>`, {ref}`weather <weather-d>`, {ref}`wikia <wikia-d>`, {ref}`wikipedia <wikipedia-d>`, {ref}`yahoo-mail <yahoo-mail-d>`, {ref}`youtube <youtube-d>`
  * **Interactive**: {ref}`cnn-nav <cnn-nav-i>`, {ref}`facebook-nav <facebook-nav-i>`, {ref}`reddit-billgates-ama <reddit-billgates-ama-i>`, {ref}`reddit-billgates-post-1 <reddit-billgates-post-1-i>`, {ref}`reddit-billgates-post-2 <reddit-billgates-post-2-i>`
  * **Live**: {ref}`booking-sf <booking-sf-l>`, {ref}`discord <discord-l>`, {ref}`fashionbeans <fashionbeans-l>`, {ref}`google-accounts <google-accounts-l>`, {ref}`imdb-firefox <imdb-firefox-l>`, {ref}`medium-article <medium-article-l>`, {ref}`people-article <people-article-l>`, {ref}`reddit-thread <reddit-thread-l>`, {ref}`rumble-fox <rumble-fox-l>`, {ref}`stackoverflow-question <stackoverflow-question-l>`, {ref}`urbandictionary-define <urbandictionary-define-l>`, {ref}`wikia-marvel <wikia-marvel-l>`
  * **Mobile**: {ref}`allrecipes <allrecipes-m>`, {ref}`amazon <amazon-m>`, {ref}`amazon-search <amazon-search-m>`, {ref}`bild-de <bild-de-m>`, {ref}`bing <bing-m>`, {ref}`bing-search-restaurants <bing-search-restaurants-m>`, {ref}`booking <booking-m>`, {ref}`cnn <cnn-m>`, {ref}`cnn-ampstories <cnn-ampstories-m>`, {ref}`dailymail <dailymail-m>`, {ref}`ebay-kleinanzeigen <ebay-kleinanzeigen-m>`, {ref}`ebay-kleinanzeigen-search <ebay-kleinanzeigen-search-m>`, {ref}`espn <espn-m>`, {ref}`facebook <facebook-m>`, {ref}`facebook-cristiano <facebook-cristiano-m>`, {ref}`google <google-m>`, {ref}`google-maps <google-maps-m>`, {ref}`google-search-restaurants <google-search-restaurants-m>`, {ref}`imdb <imdb-m>`, {ref}`instagram <instagram-m>`, {ref}`microsoft-support <microsoft-support-m>`, {ref}`reddit <reddit-m>`, {ref}`sina <sina-m>`, {ref}`stackoverflow <stackoverflow-m>`, {ref}`wikipedia <wikipedia-m>`, {ref}`youtube <youtube-m>`, {ref}`youtube-watch <youtube-watch-m>`


### Estimated Frame Latency (Any)

Similar to estimatedFirstFrameLatency, except that it uses all identified frames during video playback, normalized to be an estimate of when the first frame was displayed by using the expected time offset from the video itself.


* **Aliases**: estimatedAnyFrameLatency
* **Tests using it**:
  * **Custom**: {ref}`vpl-av1 <vpl-av1-c>`, {ref}`vpl-h264 <vpl-h264-c>`, {ref}`vpl-vp9 <vpl-vp9-c>`


### Estimated Frame Latency (First)

A metric used to denote the latency on displaying the first frame of a video. Calculated by using videos of the pageload from which key frames are identified by matching a given solid RGB color with fuzz.


* **Aliases**: estimatedFirstFrameLatency
* **Tests using it**:
  * **Custom**: {ref}`vpl-av1 <vpl-av1-c>`, {ref}`vpl-h264 <vpl-h264-c>`, {ref}`vpl-vp9 <vpl-vp9-c>`


### First Paint

Denotes the first time the browser performs a paint that has content in it (in ms).


* **Aliases**: First Contentful Composite, First Contentful Paint, fcp
* **Tests using it**:
  * **Custom**: {ref}`browsertime <browsertime-c>`, {ref}`connect <connect-c>`, {ref}`process-switch <process-switch-c>`, {ref}`throttled <throttled-c>`, {ref}`welcome <welcome-c>`
  * **Desktop**: {ref}`amazon <amazon-d>`, {ref}`bing-search <bing-search-d>`, {ref}`buzzfeed <buzzfeed-d>`, {ref}`cnn <cnn-d>`, {ref}`docomo <docomo-d>`, {ref}`ebay <ebay-d>`, {ref}`espn <espn-d>`, {ref}`expedia <expedia-d>`, {ref}`facebook <facebook-d>`, {ref}`fandom <fandom-d>`, {ref}`google-docs <google-docs-d>`, {ref}`google-mail <google-mail-d>`, {ref}`google-search <google-search-d>`, {ref}`google-slides <google-slides-d>`, {ref}`imdb <imdb-d>`, {ref}`imgur <imgur-d>`, {ref}`instagram <instagram-d>`, {ref}`linkedin <linkedin-d>`, {ref}`microsoft <microsoft-d>`, {ref}`netflix <netflix-d>`, {ref}`nytimes <nytimes-d>`, {ref}`office <office-d>`, {ref}`openai <openai-d>`, {ref}`outlook <outlook-d>`, {ref}`paypal <paypal-d>`, {ref}`pinterest <pinterest-d>`, {ref}`reddit <reddit-d>`, {ref}`samsung <samsung-d>`, {ref}`tiktok <tiktok-d>`, {ref}`tumblr <tumblr-d>`, {ref}`twitch <twitch-d>`, {ref}`twitter <twitter-d>`, {ref}`weather <weather-d>`, {ref}`wikia <wikia-d>`, {ref}`wikipedia <wikipedia-d>`, {ref}`yahoo-mail <yahoo-mail-d>`, {ref}`youtube <youtube-d>`
  * **Interactive**: {ref}`cnn-nav <cnn-nav-i>`, {ref}`facebook-nav <facebook-nav-i>`, {ref}`reddit-billgates-ama <reddit-billgates-ama-i>`, {ref}`reddit-billgates-post-1 <reddit-billgates-post-1-i>`, {ref}`reddit-billgates-post-2 <reddit-billgates-post-2-i>`
  * **Live**: {ref}`booking-sf <booking-sf-l>`, {ref}`discord <discord-l>`, {ref}`fashionbeans <fashionbeans-l>`, {ref}`google-accounts <google-accounts-l>`, {ref}`imdb-firefox <imdb-firefox-l>`, {ref}`medium-article <medium-article-l>`, {ref}`people-article <people-article-l>`, {ref}`reddit-thread <reddit-thread-l>`, {ref}`rumble-fox <rumble-fox-l>`, {ref}`stackoverflow-question <stackoverflow-question-l>`, {ref}`urbandictionary-define <urbandictionary-define-l>`, {ref}`wikia-marvel <wikia-marvel-l>`
  * **Mobile**: {ref}`allrecipes <allrecipes-m>`, {ref}`amazon <amazon-m>`, {ref}`amazon-search <amazon-search-m>`, {ref}`bild-de <bild-de-m>`, {ref}`bing <bing-m>`, {ref}`bing-search-restaurants <bing-search-restaurants-m>`, {ref}`booking <booking-m>`, {ref}`cnn <cnn-m>`, {ref}`cnn-ampstories <cnn-ampstories-m>`, {ref}`dailymail <dailymail-m>`, {ref}`ebay-kleinanzeigen <ebay-kleinanzeigen-m>`, {ref}`ebay-kleinanzeigen-search <ebay-kleinanzeigen-search-m>`, {ref}`espn <espn-m>`, {ref}`facebook <facebook-m>`, {ref}`facebook-cristiano <facebook-cristiano-m>`, {ref}`google <google-m>`, {ref}`google-maps <google-maps-m>`, {ref}`google-search-restaurants <google-search-restaurants-m>`, {ref}`imdb <imdb-m>`, {ref}`instagram <instagram-m>`, {ref}`microsoft-support <microsoft-support-m>`, {ref}`reddit <reddit-m>`, {ref}`sina <sina-m>`, {ref}`stackoverflow <stackoverflow-m>`, {ref}`wikipedia <wikipedia-m>`, {ref}`youtube <youtube-m>`, {ref}`youtube-watch <youtube-watch-m>`
  * **Unittests**: {ref}`test-page-1 <test-page-1-u>`, {ref}`test-page-2 <test-page-2-u>`, {ref}`test-page-3 <test-page-3-u>`, {ref}`test-page-4 <test-page-4-u>`


### First Visual Change

The first visual change detected in the test (in ms).

* **Aliases**: FirstVisualChange
* **Tests using it**:
  * **Custom**: {ref}`connect <connect-c>`, {ref}`process-switch <process-switch-c>`, {ref}`throttled <throttled-c>`, {ref}`welcome <welcome-c>`
  * **Desktop**: {ref}`amazon <amazon-d>`, {ref}`bing-search <bing-search-d>`, {ref}`buzzfeed <buzzfeed-d>`, {ref}`cnn <cnn-d>`, {ref}`docomo <docomo-d>`, {ref}`ebay <ebay-d>`, {ref}`espn <espn-d>`, {ref}`expedia <expedia-d>`, {ref}`facebook <facebook-d>`, {ref}`fandom <fandom-d>`, {ref}`google-docs <google-docs-d>`, {ref}`google-mail <google-mail-d>`, {ref}`google-search <google-search-d>`, {ref}`google-slides <google-slides-d>`, {ref}`imdb <imdb-d>`, {ref}`imgur <imgur-d>`, {ref}`instagram <instagram-d>`, {ref}`linkedin <linkedin-d>`, {ref}`microsoft <microsoft-d>`, {ref}`netflix <netflix-d>`, {ref}`nytimes <nytimes-d>`, {ref}`office <office-d>`, {ref}`openai <openai-d>`, {ref}`outlook <outlook-d>`, {ref}`paypal <paypal-d>`, {ref}`pinterest <pinterest-d>`, {ref}`reddit <reddit-d>`, {ref}`samsung <samsung-d>`, {ref}`tiktok <tiktok-d>`, {ref}`tumblr <tumblr-d>`, {ref}`twitch <twitch-d>`, {ref}`twitter <twitter-d>`, {ref}`weather <weather-d>`, {ref}`wikia <wikia-d>`, {ref}`wikipedia <wikipedia-d>`, {ref}`yahoo-mail <yahoo-mail-d>`, {ref}`youtube <youtube-d>`
  * **Interactive**: {ref}`cnn-nav <cnn-nav-i>`, {ref}`facebook-nav <facebook-nav-i>`, {ref}`reddit-billgates-ama <reddit-billgates-ama-i>`, {ref}`reddit-billgates-post-1 <reddit-billgates-post-1-i>`, {ref}`reddit-billgates-post-2 <reddit-billgates-post-2-i>`
  * **Live**: {ref}`booking-sf <booking-sf-l>`, {ref}`discord <discord-l>`, {ref}`fashionbeans <fashionbeans-l>`, {ref}`google-accounts <google-accounts-l>`, {ref}`imdb-firefox <imdb-firefox-l>`, {ref}`medium-article <medium-article-l>`, {ref}`people-article <people-article-l>`, {ref}`reddit-thread <reddit-thread-l>`, {ref}`rumble-fox <rumble-fox-l>`, {ref}`stackoverflow-question <stackoverflow-question-l>`, {ref}`urbandictionary-define <urbandictionary-define-l>`, {ref}`wikia-marvel <wikia-marvel-l>`
  * **Mobile**: {ref}`allrecipes <allrecipes-m>`, {ref}`amazon <amazon-m>`, {ref}`amazon-search <amazon-search-m>`, {ref}`bild-de <bild-de-m>`, {ref}`bing <bing-m>`, {ref}`bing-search-restaurants <bing-search-restaurants-m>`, {ref}`booking <booking-m>`, {ref}`cnn <cnn-m>`, {ref}`cnn-ampstories <cnn-ampstories-m>`, {ref}`dailymail <dailymail-m>`, {ref}`ebay-kleinanzeigen <ebay-kleinanzeigen-m>`, {ref}`ebay-kleinanzeigen-search <ebay-kleinanzeigen-search-m>`, {ref}`espn <espn-m>`, {ref}`facebook <facebook-m>`, {ref}`facebook-cristiano <facebook-cristiano-m>`, {ref}`google <google-m>`, {ref}`google-maps <google-maps-m>`, {ref}`google-search-restaurants <google-search-restaurants-m>`, {ref}`imdb <imdb-m>`, {ref}`instagram <instagram-m>`, {ref}`microsoft-support <microsoft-support-m>`, {ref}`reddit <reddit-m>`, {ref}`sina <sina-m>`, {ref}`stackoverflow <stackoverflow-m>`, {ref}`wikipedia <wikipedia-m>`, {ref}`youtube <youtube-m>`, {ref}`youtube-watch <youtube-watch-m>`


### Largest Contentful Paint

The time (in ms) at which the largest piece of content on the page was rendered/painted.


* **Aliases**: largestContentfulPaint, lcp
* **Tests using it**:
  * **Desktop**: {ref}`amazon <amazon-d>`, {ref}`bing-search <bing-search-d>`, {ref}`buzzfeed <buzzfeed-d>`, {ref}`cnn <cnn-d>`, {ref}`docomo <docomo-d>`, {ref}`ebay <ebay-d>`, {ref}`espn <espn-d>`, {ref}`expedia <expedia-d>`, {ref}`facebook <facebook-d>`, {ref}`fandom <fandom-d>`, {ref}`google-docs <google-docs-d>`, {ref}`google-mail <google-mail-d>`, {ref}`google-search <google-search-d>`, {ref}`google-slides <google-slides-d>`, {ref}`imdb <imdb-d>`, {ref}`imgur <imgur-d>`, {ref}`instagram <instagram-d>`, {ref}`linkedin <linkedin-d>`, {ref}`microsoft <microsoft-d>`, {ref}`netflix <netflix-d>`, {ref}`nytimes <nytimes-d>`, {ref}`office <office-d>`, {ref}`openai <openai-d>`, {ref}`outlook <outlook-d>`, {ref}`paypal <paypal-d>`, {ref}`pinterest <pinterest-d>`, {ref}`reddit <reddit-d>`, {ref}`samsung <samsung-d>`, {ref}`tiktok <tiktok-d>`, {ref}`tumblr <tumblr-d>`, {ref}`twitter <twitter-d>`, {ref}`weather <weather-d>`, {ref}`wikia <wikia-d>`, {ref}`wikipedia <wikipedia-d>`, {ref}`yahoo-mail <yahoo-mail-d>`, {ref}`youtube <youtube-d>`
  * **Mobile**: {ref}`allrecipes <allrecipes-m>`, {ref}`amazon <amazon-m>`, {ref}`amazon-search <amazon-search-m>`, {ref}`bild-de <bild-de-m>`, {ref}`bing <bing-m>`, {ref}`bing-search-restaurants <bing-search-restaurants-m>`, {ref}`booking <booking-m>`, {ref}`cnn <cnn-m>`, {ref}`cnn-ampstories <cnn-ampstories-m>`, {ref}`dailymail <dailymail-m>`, {ref}`ebay-kleinanzeigen <ebay-kleinanzeigen-m>`, {ref}`ebay-kleinanzeigen-search <ebay-kleinanzeigen-search-m>`, {ref}`espn <espn-m>`, {ref}`facebook <facebook-m>`, {ref}`facebook-cristiano <facebook-cristiano-m>`, {ref}`google <google-m>`, {ref}`google-maps <google-maps-m>`, {ref}`google-search-restaurants <google-search-restaurants-m>`, {ref}`imdb <imdb-m>`, {ref}`instagram <instagram-m>`, {ref}`microsoft-support <microsoft-support-m>`, {ref}`reddit <reddit-m>`, {ref}`sina <sina-m>`, {ref}`stackoverflow <stackoverflow-m>`, {ref}`wikipedia <wikipedia-m>`, {ref}`youtube <youtube-m>`, {ref}`youtube-watch <youtube-watch-m>`


### Last Visual Change

The last visual change detected in the test (in ms).

* **Aliases**: LastVisualChange
* **Tests using it**:
  * **Custom**: {ref}`process-switch <process-switch-c>`, {ref}`welcome <welcome-c>`
  * **Desktop**: {ref}`amazon <amazon-d>`, {ref}`bing-search <bing-search-d>`, {ref}`buzzfeed <buzzfeed-d>`, {ref}`cnn <cnn-d>`, {ref}`docomo <docomo-d>`, {ref}`ebay <ebay-d>`, {ref}`espn <espn-d>`, {ref}`expedia <expedia-d>`, {ref}`facebook <facebook-d>`, {ref}`fandom <fandom-d>`, {ref}`google-docs <google-docs-d>`, {ref}`google-mail <google-mail-d>`, {ref}`google-search <google-search-d>`, {ref}`google-slides <google-slides-d>`, {ref}`imdb <imdb-d>`, {ref}`imgur <imgur-d>`, {ref}`instagram <instagram-d>`, {ref}`linkedin <linkedin-d>`, {ref}`microsoft <microsoft-d>`, {ref}`netflix <netflix-d>`, {ref}`nytimes <nytimes-d>`, {ref}`office <office-d>`, {ref}`openai <openai-d>`, {ref}`outlook <outlook-d>`, {ref}`paypal <paypal-d>`, {ref}`pinterest <pinterest-d>`, {ref}`reddit <reddit-d>`, {ref}`samsung <samsung-d>`, {ref}`tiktok <tiktok-d>`, {ref}`tumblr <tumblr-d>`, {ref}`twitch <twitch-d>`, {ref}`twitter <twitter-d>`, {ref}`weather <weather-d>`, {ref}`wikia <wikia-d>`, {ref}`wikipedia <wikipedia-d>`, {ref}`yahoo-mail <yahoo-mail-d>`, {ref}`youtube <youtube-d>`
  * **Interactive**: {ref}`cnn-nav <cnn-nav-i>`, {ref}`facebook-nav <facebook-nav-i>`, {ref}`reddit-billgates-ama <reddit-billgates-ama-i>`, {ref}`reddit-billgates-post-1 <reddit-billgates-post-1-i>`, {ref}`reddit-billgates-post-2 <reddit-billgates-post-2-i>`
  * **Live**: {ref}`booking-sf <booking-sf-l>`, {ref}`discord <discord-l>`, {ref}`fashionbeans <fashionbeans-l>`, {ref}`google-accounts <google-accounts-l>`, {ref}`imdb-firefox <imdb-firefox-l>`, {ref}`medium-article <medium-article-l>`, {ref}`people-article <people-article-l>`, {ref}`reddit-thread <reddit-thread-l>`, {ref}`rumble-fox <rumble-fox-l>`, {ref}`stackoverflow-question <stackoverflow-question-l>`, {ref}`urbandictionary-define <urbandictionary-define-l>`, {ref}`wikia-marvel <wikia-marvel-l>`
  * **Mobile**: {ref}`allrecipes <allrecipes-m>`, {ref}`amazon <amazon-m>`, {ref}`amazon-search <amazon-search-m>`, {ref}`bild-de <bild-de-m>`, {ref}`bing <bing-m>`, {ref}`bing-search-restaurants <bing-search-restaurants-m>`, {ref}`booking <booking-m>`, {ref}`cnn <cnn-m>`, {ref}`cnn-ampstories <cnn-ampstories-m>`, {ref}`dailymail <dailymail-m>`, {ref}`ebay-kleinanzeigen <ebay-kleinanzeigen-m>`, {ref}`ebay-kleinanzeigen-search <ebay-kleinanzeigen-search-m>`, {ref}`espn <espn-m>`, {ref}`facebook <facebook-m>`, {ref}`facebook-cristiano <facebook-cristiano-m>`, {ref}`google <google-m>`, {ref}`google-maps <google-maps-m>`, {ref}`google-search-restaurants <google-search-restaurants-m>`, {ref}`imdb <imdb-m>`, {ref}`instagram <instagram-m>`, {ref}`microsoft-support <microsoft-support-m>`, {ref}`reddit <reddit-m>`, {ref}`sina <sina-m>`, {ref}`stackoverflow <stackoverflow-m>`, {ref}`wikipedia <wikipedia-m>`, {ref}`youtube <youtube-m>`, {ref}`youtube-watch <youtube-watch-m>`


### Load Time

The time it took for the page to complete loading (in ms).

* **Aliases**: loadtime
* **Tests using it**:
  * **Custom**: {ref}`browsertime <browsertime-c>`, {ref}`connect <connect-c>`, {ref}`process-switch <process-switch-c>`, {ref}`throttled <throttled-c>`, {ref}`welcome <welcome-c>`
  * **Desktop**: {ref}`amazon <amazon-d>`, {ref}`bing-search <bing-search-d>`, {ref}`buzzfeed <buzzfeed-d>`, {ref}`cnn <cnn-d>`, {ref}`docomo <docomo-d>`, {ref}`ebay <ebay-d>`, {ref}`espn <espn-d>`, {ref}`expedia <expedia-d>`, {ref}`facebook <facebook-d>`, {ref}`fandom <fandom-d>`, {ref}`google-docs <google-docs-d>`, {ref}`google-mail <google-mail-d>`, {ref}`google-search <google-search-d>`, {ref}`google-slides <google-slides-d>`, {ref}`imdb <imdb-d>`, {ref}`imgur <imgur-d>`, {ref}`instagram <instagram-d>`, {ref}`linkedin <linkedin-d>`, {ref}`microsoft <microsoft-d>`, {ref}`netflix <netflix-d>`, {ref}`nytimes <nytimes-d>`, {ref}`office <office-d>`, {ref}`openai <openai-d>`, {ref}`outlook <outlook-d>`, {ref}`paypal <paypal-d>`, {ref}`pinterest <pinterest-d>`, {ref}`reddit <reddit-d>`, {ref}`samsung <samsung-d>`, {ref}`tiktok <tiktok-d>`, {ref}`tumblr <tumblr-d>`, {ref}`twitch <twitch-d>`, {ref}`twitter <twitter-d>`, {ref}`weather <weather-d>`, {ref}`wikia <wikia-d>`, {ref}`wikipedia <wikipedia-d>`, {ref}`yahoo-mail <yahoo-mail-d>`, {ref}`youtube <youtube-d>`
  * **Interactive**: {ref}`cnn-nav <cnn-nav-i>`, {ref}`facebook-nav <facebook-nav-i>`, {ref}`reddit-billgates-ama <reddit-billgates-ama-i>`, {ref}`reddit-billgates-post-1 <reddit-billgates-post-1-i>`, {ref}`reddit-billgates-post-2 <reddit-billgates-post-2-i>`
  * **Live**: {ref}`booking-sf <booking-sf-l>`, {ref}`discord <discord-l>`, {ref}`fashionbeans <fashionbeans-l>`, {ref}`google-accounts <google-accounts-l>`, {ref}`imdb-firefox <imdb-firefox-l>`, {ref}`medium-article <medium-article-l>`, {ref}`people-article <people-article-l>`, {ref}`reddit-thread <reddit-thread-l>`, {ref}`rumble-fox <rumble-fox-l>`, {ref}`stackoverflow-question <stackoverflow-question-l>`, {ref}`urbandictionary-define <urbandictionary-define-l>`, {ref}`wikia-marvel <wikia-marvel-l>`
  * **Mobile**: {ref}`allrecipes <allrecipes-m>`, {ref}`amazon <amazon-m>`, {ref}`amazon-search <amazon-search-m>`, {ref}`bild-de <bild-de-m>`, {ref}`bing <bing-m>`, {ref}`bing-search-restaurants <bing-search-restaurants-m>`, {ref}`booking <booking-m>`, {ref}`cnn <cnn-m>`, {ref}`cnn-ampstories <cnn-ampstories-m>`, {ref}`dailymail <dailymail-m>`, {ref}`ebay-kleinanzeigen <ebay-kleinanzeigen-m>`, {ref}`ebay-kleinanzeigen-search <ebay-kleinanzeigen-search-m>`, {ref}`espn <espn-m>`, {ref}`facebook <facebook-m>`, {ref}`facebook-cristiano <facebook-cristiano-m>`, {ref}`google <google-m>`, {ref}`google-maps <google-maps-m>`, {ref}`google-search-restaurants <google-search-restaurants-m>`, {ref}`imdb <imdb-m>`, {ref}`instagram <instagram-m>`, {ref}`microsoft-support <microsoft-support-m>`, {ref}`reddit <reddit-m>`, {ref}`sina <sina-m>`, {ref}`stackoverflow <stackoverflow-m>`, {ref}`wikipedia <wikipedia-m>`, {ref}`youtube <youtube-m>`, {ref}`youtube-watch <youtube-watch-m>`
  * **Unittests**: {ref}`test-page-1 <test-page-1-u>`, {ref}`test-page-2 <test-page-2-u>`, {ref}`test-page-3 <test-page-3-u>`, {ref}`test-page-4 <test-page-4-u>`


### Perceptual Speed Index

Similar to SpeedIndex, except that it uses the structural similarity index measure (ssim) to determine visual completeness. This technique compares the luminance, contrast, and structure of the frames (a given frame vs. a final frame) to determine the completeness.


* **Aliases**: PerceptualSpeedIndex
* **Tests using it**:
  * **Custom**: {ref}`connect <connect-c>`, {ref}`process-switch <process-switch-c>`, {ref}`throttled <throttled-c>`, {ref}`welcome <welcome-c>`
  * **Desktop**: {ref}`amazon <amazon-d>`, {ref}`bing-search <bing-search-d>`, {ref}`buzzfeed <buzzfeed-d>`, {ref}`cnn <cnn-d>`, {ref}`docomo <docomo-d>`, {ref}`ebay <ebay-d>`, {ref}`espn <espn-d>`, {ref}`expedia <expedia-d>`, {ref}`facebook <facebook-d>`, {ref}`fandom <fandom-d>`, {ref}`google-docs <google-docs-d>`, {ref}`google-mail <google-mail-d>`, {ref}`google-search <google-search-d>`, {ref}`google-slides <google-slides-d>`, {ref}`imdb <imdb-d>`, {ref}`imgur <imgur-d>`, {ref}`instagram <instagram-d>`, {ref}`linkedin <linkedin-d>`, {ref}`microsoft <microsoft-d>`, {ref}`netflix <netflix-d>`, {ref}`nytimes <nytimes-d>`, {ref}`office <office-d>`, {ref}`openai <openai-d>`, {ref}`outlook <outlook-d>`, {ref}`paypal <paypal-d>`, {ref}`pinterest <pinterest-d>`, {ref}`reddit <reddit-d>`, {ref}`samsung <samsung-d>`, {ref}`tiktok <tiktok-d>`, {ref}`tumblr <tumblr-d>`, {ref}`twitch <twitch-d>`, {ref}`twitter <twitter-d>`, {ref}`weather <weather-d>`, {ref}`wikia <wikia-d>`, {ref}`wikipedia <wikipedia-d>`, {ref}`yahoo-mail <yahoo-mail-d>`, {ref}`youtube <youtube-d>`
  * **Interactive**: {ref}`cnn-nav <cnn-nav-i>`, {ref}`facebook-nav <facebook-nav-i>`, {ref}`reddit-billgates-ama <reddit-billgates-ama-i>`, {ref}`reddit-billgates-post-1 <reddit-billgates-post-1-i>`, {ref}`reddit-billgates-post-2 <reddit-billgates-post-2-i>`
  * **Live**: {ref}`booking-sf <booking-sf-l>`, {ref}`discord <discord-l>`, {ref}`fashionbeans <fashionbeans-l>`, {ref}`google-accounts <google-accounts-l>`, {ref}`imdb-firefox <imdb-firefox-l>`, {ref}`medium-article <medium-article-l>`, {ref}`people-article <people-article-l>`, {ref}`reddit-thread <reddit-thread-l>`, {ref}`rumble-fox <rumble-fox-l>`, {ref}`stackoverflow-question <stackoverflow-question-l>`, {ref}`urbandictionary-define <urbandictionary-define-l>`, {ref}`wikia-marvel <wikia-marvel-l>`
  * **Mobile**: {ref}`allrecipes <allrecipes-m>`, {ref}`amazon <amazon-m>`, {ref}`amazon-search <amazon-search-m>`, {ref}`bild-de <bild-de-m>`, {ref}`bing <bing-m>`, {ref}`bing-search-restaurants <bing-search-restaurants-m>`, {ref}`booking <booking-m>`, {ref}`cnn <cnn-m>`, {ref}`cnn-ampstories <cnn-ampstories-m>`, {ref}`dailymail <dailymail-m>`, {ref}`ebay-kleinanzeigen <ebay-kleinanzeigen-m>`, {ref}`ebay-kleinanzeigen-search <ebay-kleinanzeigen-search-m>`, {ref}`espn <espn-m>`, {ref}`facebook <facebook-m>`, {ref}`facebook-cristiano <facebook-cristiano-m>`, {ref}`google <google-m>`, {ref}`google-maps <google-maps-m>`, {ref}`google-search-restaurants <google-search-restaurants-m>`, {ref}`imdb <imdb-m>`, {ref}`instagram <instagram-m>`, {ref}`microsoft-support <microsoft-support-m>`, {ref}`reddit <reddit-m>`, {ref}`sina <sina-m>`, {ref}`stackoverflow <stackoverflow-m>`, {ref}`wikipedia <wikipedia-m>`, {ref}`youtube <youtube-m>`, {ref}`youtube-watch <youtube-watch-m>`


### Seek Latency

Time (in ms) from issuing a buffered currentTime seek to the seeked event, measured with the video decoder warm (kept playing) and cold (released while paused and recreated per seek).


* **Aliases**: seekedColdLatency, seekedWarmLatency
* **Tests using it**:
  * **Custom**: {ref}`media-seek <media-seek-c>`


### Speed Index

A metric used to denote the speed at which a page loaded. Lower values indicate faster pageloads. Units are in (Visually-Complete x Milliseconds). Calculated by using videos of the pageload which provide a measure of visual completeness. Visual completeness is calculated by comparing the histogram of a given frame to the final frame of the pageload. The SpeedIndex is calculated as the area between the curves of a constant line at y=1, and the graph of the visual completeness from 0ms to when visual completeness reaches 100% (or hits the y=1 line).


* **Aliases**: SpeedIndex
* **Tests using it**:
  * **Custom**: {ref}`connect <connect-c>`, {ref}`process-switch <process-switch-c>`, {ref}`throttled <throttled-c>`, {ref}`welcome <welcome-c>`
  * **Desktop**: {ref}`amazon <amazon-d>`, {ref}`bing-search <bing-search-d>`, {ref}`buzzfeed <buzzfeed-d>`, {ref}`cnn <cnn-d>`, {ref}`docomo <docomo-d>`, {ref}`ebay <ebay-d>`, {ref}`espn <espn-d>`, {ref}`expedia <expedia-d>`, {ref}`facebook <facebook-d>`, {ref}`fandom <fandom-d>`, {ref}`google-docs <google-docs-d>`, {ref}`google-mail <google-mail-d>`, {ref}`google-search <google-search-d>`, {ref}`google-slides <google-slides-d>`, {ref}`imdb <imdb-d>`, {ref}`imgur <imgur-d>`, {ref}`instagram <instagram-d>`, {ref}`linkedin <linkedin-d>`, {ref}`microsoft <microsoft-d>`, {ref}`netflix <netflix-d>`, {ref}`nytimes <nytimes-d>`, {ref}`office <office-d>`, {ref}`openai <openai-d>`, {ref}`outlook <outlook-d>`, {ref}`paypal <paypal-d>`, {ref}`pinterest <pinterest-d>`, {ref}`reddit <reddit-d>`, {ref}`samsung <samsung-d>`, {ref}`tiktok <tiktok-d>`, {ref}`tumblr <tumblr-d>`, {ref}`twitch <twitch-d>`, {ref}`twitter <twitter-d>`, {ref}`weather <weather-d>`, {ref}`wikia <wikia-d>`, {ref}`wikipedia <wikipedia-d>`, {ref}`yahoo-mail <yahoo-mail-d>`, {ref}`youtube <youtube-d>`
  * **Interactive**: {ref}`cnn-nav <cnn-nav-i>`, {ref}`facebook-nav <facebook-nav-i>`, {ref}`reddit-billgates-ama <reddit-billgates-ama-i>`, {ref}`reddit-billgates-post-1 <reddit-billgates-post-1-i>`, {ref}`reddit-billgates-post-2 <reddit-billgates-post-2-i>`
  * **Live**: {ref}`booking-sf <booking-sf-l>`, {ref}`discord <discord-l>`, {ref}`fashionbeans <fashionbeans-l>`, {ref}`google-accounts <google-accounts-l>`, {ref}`imdb-firefox <imdb-firefox-l>`, {ref}`medium-article <medium-article-l>`, {ref}`people-article <people-article-l>`, {ref}`reddit-thread <reddit-thread-l>`, {ref}`rumble-fox <rumble-fox-l>`, {ref}`stackoverflow-question <stackoverflow-question-l>`, {ref}`urbandictionary-define <urbandictionary-define-l>`, {ref}`wikia-marvel <wikia-marvel-l>`
  * **Mobile**: {ref}`allrecipes <allrecipes-m>`, {ref}`amazon <amazon-m>`, {ref}`amazon-search <amazon-search-m>`, {ref}`bild-de <bild-de-m>`, {ref}`bing <bing-m>`, {ref}`bing-search-restaurants <bing-search-restaurants-m>`, {ref}`booking <booking-m>`, {ref}`cnn <cnn-m>`, {ref}`cnn-ampstories <cnn-ampstories-m>`, {ref}`dailymail <dailymail-m>`, {ref}`ebay-kleinanzeigen <ebay-kleinanzeigen-m>`, {ref}`ebay-kleinanzeigen-search <ebay-kleinanzeigen-search-m>`, {ref}`espn <espn-m>`, {ref}`facebook <facebook-m>`, {ref}`facebook-cristiano <facebook-cristiano-m>`, {ref}`google <google-m>`, {ref}`google-maps <google-maps-m>`, {ref}`google-search-restaurants <google-search-restaurants-m>`, {ref}`imdb <imdb-m>`, {ref}`instagram <instagram-m>`, {ref}`microsoft-support <microsoft-support-m>`, {ref}`reddit <reddit-m>`, {ref}`sina <sina-m>`, {ref}`stackoverflow <stackoverflow-m>`, {ref}`wikipedia <wikipedia-m>`, {ref}`youtube <youtube-m>`, {ref}`youtube-watch <youtube-watch-m>`
  * **Nav-bench**: {ref}`nav-bench <nav-bench-n>`


### Youtube Playback Metrics

Metrics starting with VP9/H264 give the number of frames dropped, and painted.


* **Aliases**: H264, VP9
* **Tests using it**:
  * **Benchmarks**: {ref}`youtube-playback-hfr <youtube-playback-hfr-b>`


### cpu Time

Specifies cumulative CPU usage in milliseconds across all threads of the process since the process start.


* **Aliases**: cpuTime
* **Tests using it**:
  * **Benchmarks**: {ref}`speedometer-experimental <speedometer-experimental-b>`, {ref}`speedometer2 <speedometer2-b>`, {ref}`speedometer3 <speedometer3-b>`, {ref}`youtube-playback-h264-1080p30 <youtube-playback-h264-1080p30-b>`, {ref}`youtube-playback-h264-1080p60 <youtube-playback-h264-1080p60-b>`, {ref}`youtube-playback-h264-full-1080p30 <youtube-playback-h264-full-1080p30-b>`, {ref}`youtube-playback-h264-full-1080p60 <youtube-playback-h264-full-1080p60-b>`, {ref}`youtube-playback-v9-1080p30 <youtube-playback-v9-1080p30-b>`, {ref}`youtube-playback-v9-1080p60 <youtube-playback-v9-1080p60-b>`, {ref}`youtube-playback-v9-full-1080p30 <youtube-playback-v9-full-1080p30-b>`, {ref}`youtube-playback-v9-full-1080p60 <youtube-playback-v9-full-1080p60-b>`
  * **Custom**: {ref}`addMab1 <addMab1-c>`, {ref}`addMabN <addMabN-c>`, {ref}`addMar1 <addMar1-c>`, {ref}`addMarN <addMarN-c>`, {ref}`addMbl1 <addMbl1-c>`, {ref}`addMblN <addMblN-c>`, {ref}`addkAB1 <addkAB1-c>`, {ref}`addkABN <addkABN-c>`, {ref}`addkAR1 <addkAR1-c>`, {ref}`addkARN <addkARN-c>`, {ref}`addkBL1 <addkBL1-c>`, {ref}`addkBLN <addkBLN-c>`, {ref}`getkeyrng <getkeyrng-c>`, {ref}`idb-open-few-par <idb-open-few-par-c>`, {ref}`idb-open-few-seq <idb-open-few-seq-c>`, {ref}`idb-open-many-par <idb-open-many-par-c>`, {ref}`idb-open-many-seq <idb-open-many-seq-c>`, {ref}`idbCurNext <idbCurNext-c>`, {ref}`idbCurPKey <idbCurPKey-c>`, {ref}`idbCurPrev <idbCurPrev-c>`
  * **Desktop**: {ref}`amazon <amazon-d>`, {ref}`bing-search <bing-search-d>`, {ref}`buzzfeed <buzzfeed-d>`, {ref}`cnn <cnn-d>`, {ref}`docomo <docomo-d>`, {ref}`ebay <ebay-d>`, {ref}`espn <espn-d>`, {ref}`expedia <expedia-d>`, {ref}`facebook <facebook-d>`, {ref}`fandom <fandom-d>`, {ref}`google-docs <google-docs-d>`, {ref}`google-mail <google-mail-d>`, {ref}`google-search <google-search-d>`, {ref}`google-slides <google-slides-d>`, {ref}`imdb <imdb-d>`, {ref}`imgur <imgur-d>`, {ref}`instagram <instagram-d>`, {ref}`linkedin <linkedin-d>`, {ref}`microsoft <microsoft-d>`, {ref}`netflix <netflix-d>`, {ref}`nytimes <nytimes-d>`, {ref}`office <office-d>`, {ref}`openai <openai-d>`, {ref}`outlook <outlook-d>`, {ref}`paypal <paypal-d>`, {ref}`pinterest <pinterest-d>`, {ref}`reddit <reddit-d>`, {ref}`samsung <samsung-d>`, {ref}`tiktok <tiktok-d>`, {ref}`tumblr <tumblr-d>`, {ref}`twitch <twitch-d>`, {ref}`twitter <twitter-d>`, {ref}`weather <weather-d>`, {ref}`wikia <wikia-d>`, {ref}`wikipedia <wikipedia-d>`, {ref}`yahoo-mail <yahoo-mail-d>`, {ref}`youtube <youtube-d>`
  * **Interactive**: {ref}`cnn-nav <cnn-nav-i>`, {ref}`facebook-nav <facebook-nav-i>`, {ref}`reddit-billgates-ama <reddit-billgates-ama-i>`, {ref}`reddit-billgates-post-1 <reddit-billgates-post-1-i>`, {ref}`reddit-billgates-post-2 <reddit-billgates-post-2-i>`
  * **Mobile**: {ref}`allrecipes <allrecipes-m>`, {ref}`amazon <amazon-m>`, {ref}`amazon-search <amazon-search-m>`, {ref}`bild-de <bild-de-m>`, {ref}`bing <bing-m>`, {ref}`bing-search-restaurants <bing-search-restaurants-m>`, {ref}`booking <booking-m>`, {ref}`cnn <cnn-m>`, {ref}`cnn-ampstories <cnn-ampstories-m>`, {ref}`dailymail <dailymail-m>`, {ref}`ebay-kleinanzeigen <ebay-kleinanzeigen-m>`, {ref}`ebay-kleinanzeigen-search <ebay-kleinanzeigen-search-m>`, {ref}`espn <espn-m>`, {ref}`facebook <facebook-m>`, {ref}`facebook-cristiano <facebook-cristiano-m>`, {ref}`google <google-m>`, {ref}`google-maps <google-maps-m>`, {ref}`google-search-restaurants <google-search-restaurants-m>`, {ref}`imdb <imdb-m>`, {ref}`instagram <instagram-m>`, {ref}`microsoft-support <microsoft-support-m>`, {ref}`reddit <reddit-m>`, {ref}`sina <sina-m>`, {ref}`stackoverflow <stackoverflow-m>`, {ref}`wikipedia <wikipedia-m>`, {ref}`youtube <youtube-m>`, {ref}`youtube-watch <youtube-watch-m>`


