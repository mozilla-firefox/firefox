# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Error page titles

neterror-page-title = Problem loading page
certerror-page-title = Warning: Potential Security Risk Ahead
certerror-sts-page-title = Did Not Connect: Potential Security Issue
neterror-blocked-by-policy-page-title = Blocked Page
neterror-captive-portal-page-title = Log in to network
neterror-dns-not-found-title = Server Not Found
neterror-malformed-uri-page-title = Invalid URL
general-body-title = Be careful. Something doesn’t look right.
problem-with-this-site-title = Looks like there’s a problem with this site
neterror-http-auth-scheme-disabled-title = This site’s sign-in method is turned off

## Error page actions

neterror-advanced-button = Advanced…
neterror-copy-to-clipboard-button = Copy text to clipboard
neterror-learn-more-link = Learn more…
neterror-open-portal-login-page-button = Open Network Login Page
neterror-override-exception-button = Accept the Risk and Continue
neterror-pref-reset-button = Restore default settings
neterror-return-to-previous-page-button = Go Back
neterror-return-to-previous-page-recommended-button = Go Back (Recommended)
neterror-try-again-button-2 = Try Again
    .accesskey = T
neterror-add-exception-button = Always continue for this site
neterror-settings-button = Change DNS settings
neterror-view-certificate-link = View Certificate

## Search call-to-action shown on the online dnsNotFound error page when
## browser.netError.searchCTA.enabled is true. Offers a one-click web search
## derived from the address that failed to resolve.

neterror-search-cta-title = This site can’t be reached

# $hostname (String) - The host of the address that failed to load, including
# any subdomains, for example "docs.example.com". The host is emphasized.
neterror-search-cta-intro2 = Could not connect to the server at <strong>{ $hostname }</strong>.

# Heading above the list of recovery hints on the search CTA error page.
neterror-search-cta-things-to-try = Try these steps:

neterror-search-cta-hint-check-address = Double-check the website address

# Shown when no Search button is offered, so no specific query can be named.
neterror-search-cta-hint-search = Search the web to find the site

# Shown when the Search button is offered, naming the exact query it will run.
# $query (String) - The search query derived from the address that failed to
# load, beginning with the site's name, for example "example best hiking
# boots". The query and the quotation marks around it are emphasized.
neterror-search-cta-hint-search-query = Search the web for <strong>“{ $query }”</strong>

# .tooltiptext doubles as the button's accessible description, so it explains
# that results open in a new tab.
neterror-search-cta-search-button =
    .label = Search
    .accesskey = c
    .tooltiptext = Opens search results in a new tab

neterror-search-cta-reload-button =
    .label = Reload
    .accesskey = R

# Shown in place of the Search button when the user clicks it but connectivity
# has dropped since the page loaded, so the search cannot be performed.
neterror-search-cta-offline = You appear to be offline. Reconnect and try again.

# $error (String) - The error code, for example "dnsNotFound".
neterror-search-cta-error-code = Error code: { $error }

# The search CTA's own "Learn more" link text (no trailing ellipsis).
neterror-search-cta-learn-more = Learn more

##

neterror-pref-reset = It looks like your network security settings might be causing this. Do you want the default settings to be restored?

## Shown on about:neterror and about:certerror when the SSLKEYLOGFILE
## environment variable is set, which causes { -brand-short-name } to log TLS
## session keys that can be used to decrypt encrypted network traffic.

neterror-sslkeylogging-warning =
    .heading = Your connection may not be private
    .message = An app or service may see your encrypted traffic from this site.

## Specific error messages

neterror-generic-error = { -brand-short-name } can’t load this page for some reason.

neterror-load-error-try-again = The site could be temporarily unavailable or too busy. Try again in a few moments.
neterror-load-error-connection = If you are unable to load any pages, check your computer’s network connection.
neterror-load-error-firewall = If your computer or network is protected by a firewall or proxy, make sure that { -brand-short-name } is permitted to access the web.
# This warning is only shown on macOS Sequoia and later (see bug 1929377)
neterror-load-osx-permission = If you are trying to load a local network page, please check that { -brand-short-name } has been granted Local Network permissions in the macOS Privacy & Security settings.

neterror-http-error-page = Check to make sure you’ve typed the website address correctly.
neterror-http-empty-response = Check to make sure you’ve typed the website address correctly and try again in a few moments.

neterror-captive-portal = You must log in to this network before you can access the internet.

# Variables:
# $hostAndPath (String) - a suggested site (e.g. "www.example.com") that the user may have meant instead.
neterror-dns-not-found-with-suggestion = Did you mean to go to <a data-l10n-name="website">{ $hostAndPath }</a>?
neterror-dns-not-found-hint-header = <strong>If you entered the right address, you can:</strong>
neterror-dns-not-found-hint-try-again = Try again later
neterror-dns-not-found-hint-check-network = Check your network connection
neterror-dns-not-found-hint-firewall = Check that { -brand-short-name } has permission to access the web (you might be connected but behind a firewall)
neterror-dns-not-found-hint-check-network-2 = Check your network connection.
neterror-dns-not-found-hint-firewall-2 = Check that { -brand-short-name } has permission to access the web (you might be connected but behind a firewall).
neterror-dns-not-found-offline-hint-header = <strong>What can you do about it?</strong>
neterror-dns-not-found-offline-hint-different-device = Try connecting on a different device.
neterror-dns-not-found-offline-hint-modem = Check your modem or router.
neterror-dns-not-found-offline-hint-reconnect = Disconnect and reconnect to Wi-Fi.

## Hints shown, as one list of steps, when a connection to this computer
## (localhost) is refused.
## Variables:
##   $port (String) - The port number the connection was refused on.

neterror-loopback-refused-not-running = Nothing is accepting connections on port { $port }. Check that your local server is running.
# "it" is the local server that the user is trying to reach.
neterror-loopback-refused-wrong-port = If it is running, check that it is using port { $port }.
# "this address" is the IP address of the page that failed to load, for example
# 127.0.0.1 or [::1]. Do not translate or reformat the literal addresses [::1]
# and 127.0.0.1 in the text.
neterror-loopback-refused-address = Check that the server accepts connections on this address. Some servers listen only on [::1] or only on 127.0.0.1.

## TRR-only specific messages
## Variables:
##   $hostname (String) - Hostname of the website to which the user was trying to connect.
##   $trrDomain (String) - Hostname of the DNS over HTTPS server that is currently in use.

neterror-dns-not-found-trr-only-reason2 = { -brand-short-name } can’t protect your request for this site’s address through our secure DNS provider. Here’s why:
neterror-dns-not-found-trr-third-party-warning2 = You can continue with your default DNS resolver. However, a third-party might be able to see what websites you visit.

neterror-dns-not-found-trr-only-could-not-connect = { -brand-short-name } wasn’t able to connect to { $trrDomain }.
neterror-dns-not-found-trr-only-timeout = The connection to { $trrDomain } took longer than expected.
neterror-dns-not-found-trr-unknown-host2 = This website wasn’t found by { $trrDomain }.
neterror-dns-not-found-trr-server-problem = There was a problem with { $trrDomain }.
neterror-dns-not-found-bad-trr-url = Invalid URL.
neterror-dns-not-found-system-sleep = System is in sleep mode.
neterror-dns-not-found-trr-unknown-problem = Unexpected problem.

##

neterror-file-not-found-filename = Check the file name for capitalization or other typing errors.
neterror-file-not-found-moved = Check to see if the file was moved, renamed or deleted.

# Variables:
#   $path (String) - Path of the local file that could not be found.
neterror-file-not-found-intro = { -brand-short-name } can’t find the file at <strong>{ $path }</strong>. It either doesn’t exist or the path is incorrect.
neterror-file-not-found-what-can-you-do = If you typed the address manually, check for capitalization mistakes or typos in the file name or path. If you followed a saved bookmark or link, the file may have been moved, renamed, or deleted since it was saved. Try locating it using your file manager or a recent search.

neterror-access-denied = It may have been removed, moved, or file permissions may be preventing access.

neterror-unknown-protocol = You might need to install other software to open this address.

neterror-redirect-loop = This problem can sometimes be caused by disabling or refusing to accept cookies.

neterror-unknown-socket-type-client-config = This might be due to a configuration error with your client.

neterror-not-cached-intro = The requested document is not available in { -brand-short-name }’s cache.
neterror-not-cached-sensitive = As a security precaution, { -brand-short-name } does not automatically re-request sensitive documents.
neterror-not-cached-try-again = Click Try Again to re-request the document from the website.

neterror-net-offline = Press “Try Again” to switch to online mode and reload the page.

neterror-proxy-resolve-failure-settings = Check the proxy settings to make sure that they are correct.
neterror-proxy-resolve-failure-connection = Check to make sure your computer has a working network connection.
neterror-proxy-resolve-failure-firewall = If your computer or network is protected by a firewall or proxy, make sure that { -brand-short-name } is permitted to access the web.

neterror-proxy-connect-failure-settings = Check the proxy settings to make sure that they are correct.
neterror-proxy-connect-failure-contact-admin = Contact your network administrator to make sure the proxy server is working.

neterror-content-encoding-error = Please contact the website owners to inform them of this problem.

neterror-unsafe-content-type = Please contact the website owners to inform them of this problem.

# Variables:
# $hostname (String) - Hostname of the website to which the user was trying to connect.
neterror-basic-http-auth = { -brand-short-name } doesn’t trust { $hostname } because the connection isn’t secure. Try changing the URL to HTTPS.

neterror-http-auth-scheme-disabled = If you need to sign in to this site, contact your system administrator.

neterror-nss-failure-not-verified = The page you are trying to view cannot be shown because the authenticity of the received data could not be verified.
neterror-nss-failure-contact-website = Please contact the website owners to inform them of this problem.

# Variables:
# $hostname (String) - Hostname of the website to which the user was trying to connect.
certerror-intro = { -brand-short-name } detected a potential security threat and did not continue to <b>{ $hostname }</b>. If you visit this site, attackers could try to steal information like your passwords, emails, or credit card details.
# Variables:
# $hostname (String) - Hostname of the website to which the user was trying to connect.
certerror-sts-intro = { -brand-short-name } detected a potential security threat and did not continue to <b>{ $hostname }</b> because this website requires a secure connection.
# Variables:
# $hostname (String) - Hostname of the website to which the user was trying to connect.
certerror-expired-cert-intro = { -brand-short-name } detected an issue and did not continue to <b>{ $hostname }</b>. The website is either misconfigured or your computer clock is set to the wrong time.
# Variables:
# $hostname (String) - Hostname of the website to which the user was trying to connect.
# $mitm (String) - The name of the software intercepting communications between you and the website (or “man in the middle”)
certerror-mitm = <b>{ $hostname }</b> is most likely a safe site, but a secure connection could not be established. This issue is caused by <b>{ $mitm }</b>, which is either software on your computer or your network.

neterror-corrupted-content-intro = The page you are trying to view cannot be shown because an error in the data transmission was detected.
neterror-corrupted-content-contact-website = Please contact the website owners to inform them of this problem.

# Do not translate "SSL_ERROR_UNSUPPORTED_VERSION".
neterror-sslv3-used = Advanced info: SSL_ERROR_UNSUPPORTED_VERSION

# Variables:
# $hostname (String) - Hostname of the website to which the user was trying to connect.
neterror-inadequate-security-intro = <b>{ $hostname }</b> uses security technology that is outdated and vulnerable to attack. An attacker could easily reveal information which you thought to be safe. The website administrator will need to fix the server first before you can visit the site.
# Do not translate "NS_ERROR_NET_INADEQUATE_SECURITY".
neterror-inadequate-security-code = Error code: NS_ERROR_NET_INADEQUATE_SECURITY

# Variables:
# $hostname (String) - Hostname of the website to which the user was trying to connect.
# $now (Date) - The current datetime, to be formatted as a date
neterror-clock-skew-error = Your computer thinks it is { DATETIME($now, dateStyle: "medium") }, which prevents { -brand-short-name } from connecting securely. To visit <b>{ $hostname }</b>, update your computer clock in your system settings to the current date, time, and time zone, and then refresh <b>{ $hostname }</b>.

neterror-network-protocol-error-intro = The page you are trying to view cannot be shown because an error in the network protocol was detected.
neterror-network-protocol-error-contact-website = Please contact the website owners to inform them of this problem.

certerror-expired-cert-second-para = It’s likely the website’s certificate is expired, which prevents { -brand-short-name } from connecting securely. If you visit this site, attackers could try to steal information like your passwords, emails, or credit card details.
certerror-expired-cert-sts-second-para = It’s likely the website’s certificate is expired, which prevents { -brand-short-name } from connecting securely.

certerror-what-can-you-do-about-it-title = What can you do about it?

certerror-unknown-issuer-what-can-you-do-about-it-website = The issue is most likely with the website, and there is nothing you can do to resolve it.
certerror-unknown-issuer-what-can-you-do-about-it-contact-admin = If you are on a corporate network or using antivirus software, you can reach out to the support teams for assistance. You can also notify the website’s administrator about the problem.

# Variables:
# $hostname (String) - Hostname of the website to which the user was trying to connect.
# $now (Date) - The current datetime, to be formatted as a date
certerror-expired-cert-what-can-you-do-about-it-clock = Your computer clock is set to { DATETIME($now, dateStyle: "medium") }. Make sure your computer is set to the correct date, time, and time zone in your system settings, and then refresh <b>{ $hostname }</b>.
certerror-expired-cert-what-can-you-do-about-it-contact-website = If your clock is already set to the right time, the website is likely misconfigured, and there is nothing you can do to resolve the issue. You can notify the website’s administrator about the problem.

certerror-bad-cert-domain-what-can-you-do-about-it = The issue is most likely with the website, and there is nothing you can do to resolve it. You can notify the website’s administrator about the problem.

certerror-mitm-what-can-you-do-about-it-antivirus = If your antivirus software includes a feature that scans encrypted connections (often called “web scanning” or “https scanning”), you can disable that feature. If that doesn’t work, you can remove and reinstall the antivirus software.
certerror-mitm-what-can-you-do-about-it-corporate = If you are on a corporate network, you can contact your IT department.
# Variables:
# $mitm (String) - The name of the software intercepting communications between you and the website (or “man in the middle”)
certerror-mitm-what-can-you-do-about-it-attack = If you are not familiar with <b>{ $mitm }</b>, then this could be an attack and you should not continue to the site.

# Variables:
# $mitm (String) - The name of the software intercepting communications between you and the website (or “man in the middle”)
certerror-mitm-what-can-you-do-about-it-attack-sts = If you are not familiar with <b>{ $mitm }</b>, then this could be an attack, and there is nothing you can do to access the site.

# Variables:
# $hostname (String) - Hostname of the website to which the user was trying to connect.
certerror-what-should-i-do-bad-sts-cert-explanation = <b>{ $hostname }</b> has a security policy called HTTP Strict Transport Security (HSTS), which means that { -brand-short-name } can only connect to it securely. You can’t add an exception to visit this site.

cert-error-trust-certificate-transparency-what-can-you-do-about-it = Probably nothing, since it’s likely there’s a problem with the site itself.

certerror-blocked-by-corp-headers-description = Sometimes websites set up protections for themselves and people like you from unwanted interactions with other sites.
certerror-coop-learn-more = Learn more about Cross Origin Opener Policies (COOP)
certerror-coep-learn-more = Learn more about Cross Origin Embedder Policies (COEP)

# Variables:
#   $responsestatus (string) - HTTP response status code (e.g., 500).
#   $responsestatustext (string) - HTTP response status text (e.g., "Internal Server Error").
neterror-response-status-code = Error code: { $responsestatus } { $responsestatustext }

## Felt Privacy V1 Strings

fp-neterror-offline-body-title = Looks like there’s a problem with your internet connection

## Variables:
##   $hostname (String) - Hostname of the website to which the user was trying to connect.

fp-neterror-connection-intro = { -brand-short-name } can’t create a secure connection to the server at { $hostname }.
fp-neterror-offline-intro = { -brand-short-name } can’t connect to the server at <strong>{ $hostname }</strong>
fp-neterror-offline-intro-2 = { -brand-short-name } can’t connect to the server at <strong>{ $hostname }</strong>.
fp-neterror-net-timeout-intro = The server at <strong>{ $hostname }</strong> is taking too long to respond.
# Variables:
#   $hostname (String) - Hostname of the website to which the user was trying to connect.
#   $responsestatus (Number) - HTTP response status code (e.g., 404).
#   $responsestatustext (String) - HTTP response status text (e.g., "Not Found", always in English).
fp-neterror-http-error-intro = The server at <strong>{ $hostname }</strong> sent back an error: { $responsestatus } { $responsestatustext }
# Variables:
#   $hostname (String) - Hostname of the website to which the user was trying to connect.
fp-neterror-invalid-header-value-intro = <strong>{ $hostname }</strong> sent back a header with empty characters not allowed by web security standards.
fp-neterror-content-encoding-intro = The page you are trying to view cannot be shown because it uses an invalid or unsupported form of compression.
fp-neterror-coop-coep-intro = { -brand-short-name } didn’t load this page because it looks like the security configuration doesn’t match the previous page.
fp-neterror-blocked-by-policy-intro = Your organization has blocked access to this page or website.
fp-neterror-http-auth-disabled-intro = Someone pretending to be the site could try to steal things like your username, password, or email.
fp-neterror-http-auth-disabled-secure-connection = This site requires a secure connection, and you can’t add an exception to visit it.
# Variables:
#   $hostname (String) - Hostname of the website to which the user was trying to connect.
fp-neterror-http-auth-scheme-disabled-intro = <strong>{ $hostname }</strong> asked you to sign in using a method that has been turned off in your connection settings.

fp-neterror-why-did-this-happen = Why did this happen?

# This string appears after the following string: "What makes the site look dangerous?" (fp-certerror-why-site-dangerous)
fp-neterror-cypher-overlap-why-dangerous-body = It looks like this site is using old software with known security issues.
fp-neterror-http-auth-disabled-why-dangerous-body = { -brand-short-name } doesn’t trust { $hostname } because the connection isn’t secure.
# This string appears after the following string: "What can you do about it?" (fp-certerror-what-can-you-do)
fp-neterror-cypher-overlap-what-can-you-do-body = Make sure you’re using the latest version of { -brand-short-name }. Go to Help > About { -brand-short-name } in the menu. If you’re using the latest { -brand-short-name }, the problem is most likely with the site itself.
fp-neterror-offline-what-can-you-do-body = Try connecting on a different device. Check your modem or router. Disconnect and reconnect to Wi-Fi.
fp-neterror-http-auth-disabled-what-can-you-do-body = Try changing the URL to HTTPS. But it’s likely there’s a problem with the site itself.

# This string appears after the following string: "Why did this happen?" (fp-neterror-why-did-this-happen)
fp-neterror-coop-coep-why-did-this-happen-body = Sometimes websites set up protections for themselves from unwanted interactions with other sites.
fp-learn-more-about-https-connections = Learn about HTTPS connections

fp-neterror-vpn-error-title = Couldn’t connect to VPN
fp-neterror-vpn-error-description = Try again in a few minutes.
fp-neterror-vpn-error-continue-button = Continue without VPN
    .accesskey = C

fp-neterror-denied-port-access = This address uses a network port which is normally used for purposes other than Web browsing. { -brand-short-name } has canceled the request for your protection.
