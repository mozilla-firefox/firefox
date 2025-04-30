# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Variables:
#   $hostname (string) - Hostname of the website with cert error.
cert-error-intro = { $hostname } uses an invalid security certificate.

cert-error-mitm-intro = Websites prove their identity via certificates, which are issued by certificate authorities.

cert-error-mitm-mozilla = { -brand-short-name } is backed by the non-profit Mozilla, which administers a completely open certificate authority (CA) store. The CA store helps ensure that certificate authorities are following best practices for user security.

cert-error-mitm-connection = { -brand-short-name } uses the Mozilla CA store to verify that a connection is secure, rather than certificates supplied by the user’s operating system. So, if an antivirus program or a network is intercepting a connection with a security certificate issued by a CA that is not in the Mozilla CA store, the connection is considered unsafe.

cert-error-trust-unknown-issuer-intro = Someone could be trying to impersonate the site and you should not continue.

# Variables:
#   $hostname (string) - Hostname of the website with cert error.
cert-error-trust-unknown-issuer = Websites prove their identity via certificates. { -brand-short-name } does not trust { $hostname } because its certificate issuer is unknown, the certificate is self-signed, or the server is not sending the correct intermediate certificates.

cert-error-trust-cert-invalid = The certificate is not trusted because it was issued by an invalid CA certificate.

cert-error-trust-untrusted-issuer = The certificate is not trusted because the issuer certificate is not trusted.

cert-error-trust-signature-algorithm-disabled = The certificate is not trusted because it was signed using a signature algorithm that was disabled because that algorithm is not secure.

cert-error-trust-expired-issuer = The certificate is not trusted because the issuer certificate has expired.

cert-error-trust-self-signed = The certificate is not trusted because it is self-signed.

cert-error-trust-symantec = Certificates issued by GeoTrust, RapidSSL, Symantec, Thawte, and VeriSign are no longer considered safe because these certificate authorities failed to follow security practices in the past.

# Variables:
#   $hostname (string) - Hostname of the website with cert error.
cert-error-trust-certificate-transparency = { -brand-short-name } doesn’t trust { $hostname } because it couldn’t prove it meets public certificate transparency requirements.

# Variables:
#   $hostname (string) - Hostname of the website with cert error.
cert-error-revoked = Websites prove their identity via certificates. { -brand-short-name } doesn’t trust { $hostname } because it uses a certificate that has been revoked.

cert-error-untrusted-default = The certificate does not come from a trusted source.

# Variables:
#   $hostname (string) - Hostname of the website with cert error.
cert-error-domain-mismatch = Websites prove their identity via certificates. { -brand-short-name } does not trust this site because it uses a certificate that is not valid for { $hostname }.

# Variables:
#   $hostname (string) - Hostname of the website with cert error.
#   $alt-name (string) - Alternate domain name for which the cert is valid.
cert-error-domain-mismatch-single = Websites prove their identity via certificates. { -brand-short-name } does not trust this site because it uses a certificate that is not valid for { $hostname }. The certificate is only valid for <a data-l10n-name="domain-mismatch-link">{ $alt-name }</a>.

# Variables:
#   $hostname (string) - Hostname of the website with cert error.
#   $alt-name (string) - Alternate domain name for which the cert is valid.
cert-error-domain-mismatch-single-nolink = Websites prove their identity via certificates. { -brand-short-name } does not trust this site because it uses a certificate that is not valid for { $hostname }. The certificate is only valid for { $alt-name }.

# Variables:
#   $hostname (string) - Hostname of the website with cert error.
#   $subject-alt-names (string) - Alternate domain names for which the cert is valid.
cert-error-domain-mismatch-multiple = Websites prove their identity via certificates. { -brand-short-name } does not trust this site because it uses a certificate that is not valid for { $hostname }. The certificate is only valid for the following names: { $subject-alt-names }

# Variables:
#   $hostname (string) - Hostname of the website with cert error.
#   $not-after-local-time (Date) - Certificate is not valid after this time.
cert-error-expired-now = Websites prove their identity via certificates, which are valid for a set time period. The certificate for { $hostname } expired on { $not-after-local-time }.

# Variables:
#   $hostname (string) - Hostname of the website with cert error.
#   $not-before-local-time (Date) - Certificate is not valid before this time.
cert-error-not-yet-valid-now = Websites prove their identity via certificates, which are valid for a set time period. The certificate for { $hostname } will not be valid until { $not-before-local-time }.

# Variables:
#   $error (string) - NSS error code string that specifies type of cert error. e.g. unknown issuer, invalid cert, etc.
cert-error-code-prefix = Error code: { $error }

# Variables:
#   $error (string) - NSS error code string that specifies type of cert error. e.g. unknown issuer, invalid cert, etc.
cert-error-code-prefix-link = Error code: <a data-l10n-name="error-code-link">{ $error }</a>

# Variables:
#   $hostname (string) - Hostname of the website with SSL error.
#   $errorMessage (string) - Error message corresponding to the type of error we are experiencing.
cert-error-ssl-connection-error = An error occurred during a connection to { $hostname }. { $errorMessage }

# Variables:
#   $hostname (string) - Hostname of the website with cert error.
cert-error-symantec-distrust-description = Websites prove their identity via certificates, which are issued by certificate authorities. Most browsers no longer trust certificates issued by GeoTrust, RapidSSL, Symantec, Thawte, and VeriSign. { $hostname } uses a certificate from one of these authorities and so the website’s identity cannot be proven.

cert-error-symantec-distrust-admin = You may notify the website’s administrator about this problem.

cert-error-old-tls-version = This website might not support the TLS 1.2 protocol, which is the minimum version supported by { -brand-short-name }.

# Variables:
#   $hasHSTS (Boolean) - Indicates whether HSTS header is present.
cert-error-details-hsts-label = HTTP Strict Transport Security: { $hasHSTS }

# Variables:
#   $hasHPKP (Boolean) - Indicates whether HPKP header is present.
cert-error-details-key-pinning-label = HTTP Public Key Pinning: { $hasHPKP }

cert-error-details-cert-chain-label = Certificate chain:

# Variables:
#   $hostname (string) - Hostname of the website blocked by csp or xfo error.
csp-xfo-blocked-long-desc = To protect your security, { $hostname } will not allow { -brand-short-name } to display the page if another site has embedded it. To see this page, you need to open it in a new window.

## Messages used for certificate error titles

connectionFailure-title = Unable to connect
deniedPortAccess-title = This address is restricted
# "Hmm" is a sound made when considering or puzzling over something.
# You don't have to include it in your translation if your language does not have a written word like this.
dnsNotFound-title = Hmm. We’re having trouble finding that site.
internet-connection-offline-title = Looks like there’s a problem with your internet connection.

fileNotFound-title = File not found
fileAccessDenied-title = Access to the file was denied
generic-title = Oops.
captivePortal-title = Log in to network
# "Hmm" is a sound made when considering or puzzling over something.
# You don't have to include it in your translation if your language does not have a written word like this.
malformedURI-title = Hmm. That address doesn’t look right.
netInterrupt-title = The connection was interrupted
notCached-title = Document Expired
netOffline-title = Offline mode
contentEncodingError-title = Content Encoding Error
unsafeContentType-title = Unsafe File Type
netReset-title = The connection was reset
netTimeout-title = The connection has timed out
httpErrorPage-title = Looks like there’s a problem with this site
serverError-title = Looks like there’s a problem with this site
unknownProtocolFound-title = The address wasn’t understood
proxyConnectFailure-title = The proxy server is refusing connections
proxyResolveFailure-title = Unable to find the proxy server
redirectLoop-title = The page isn’t redirecting properly
unknownSocketType-title = Unexpected response from server
nssFailure2-title = Secure Connection Failed
csp-xfo-error-title = { -brand-short-name } Can’t Open This Page
corruptedContentError-title = Corrupted Content Error
sslv3Used-title = Unable to Connect Securely
inadequateSecurityError-title = Your connection is not secure
blockedByPolicy-title = Blocked Page
clockSkewError-title = Your Computer Clock is Wrong
networkProtocolError-title = Network Protocol Error
nssBadCert-title = Warning: Potential Security Risk Ahead
nssBadCert-sts-title = Did Not Connect: Potential Security Issue
certerror-mitm-title = Software is Preventing { -brand-short-name } From Safely Connecting to This Site

## Felt Privacy V1 Strings

fp-certerror-page-title = Warning: Security Risk
fp-certerror-body-title = Be careful. Something doesn’t look right.

fp-certerror-why-site-dangerous = What makes the site look dangerous?
fp-certerror-what-can-you-do = What can you do about it?

fp-certerror-advanced-title = Advanced

fp-certerror-advanced-button = Advanced
fp-certerror-hide-advanced-button = Hide advanced

## Variables:
##   $hostname (String) - Hostname of the website to which the user was trying to connect.

fp-certerror-override-exception-button = Proceed to { $hostname } (Risky)
fp-certerror-intro = { -brand-short-name } spotted a potentially serious security issue with <strong>{ $hostname }</strong>. Someone pretending to be the site could try to steal things like credit card info, passwords, or emails.
fp-certerror-expired-into = { -brand-short-name } spotted a security issue with <strong>{ $hostname }</strong>. Either the site isn’t set up right or your device’s clock is set to the wrong date/time.

##

fp-certerror-view-certificate-link = View the site’s certificate
fp-certerror-return-to-previous-page-recommended-button = Go back (Recommended)

# This string appears after the following string: "What makes the site look dangerous?" (fp-certerror-why-site-dangerous)
# Variables:
#   $hostname (String) - Hostname of the website to which the user was trying to connect.
#   $validHosts (String) - Valid hostnames.
fp-certerror-bad-domain-why-dangerous-body = The site is set up to allow only secure connections, but there’s a problem with the site’s certificate. It’s possible that a bad actor is trying to impersonate the site. Sites use certificates issued by a certificate authority to prove they’re really who they say they are. { -brand-short-name } doesn’t trust this site because its certificate isn’t valid for { $hostname }. The certificate is only valid for: { $validHosts }.
# This string appears after the following string: "What can you do about it?" (fp-certerror-what-can-you-do)
fp-certerror-bad-domain-what-can-you-do-body = Probably nothing, since it’s likely there’s a problem with the site itself. Sites use certificates issued by a certificate authority to prove they’re really who they say they are. But if you’re on a corporate network, your support team may have more info. If you’re using antivirus software, try searching for potential conflicts or known issues.

# This string appears after the following string: "What makes the site look dangerous?" (fp-certerror-why-site-dangerous)
fp-certerror-unknown-issuer-why-dangerous-body = There’s an issue with the site’s certificate. It’s possible that a bad actor is trying to impersonate the site. Sites use certificates issued by a certificate authority to prove they’re really who they say they are. { -brand-short-name } doesn’t trust this site because we can’t tell who issued the certificate, it’s self-signed, or the site isn’t sending intermediate certificates we trust.
# This string appears after the following string: "What can you do about it?" (fp-certerror-what-can-you-do)
fp-certerror-unknown-issuer-what-can-you-do-body = Probably nothing, since it’s likely there’s a problem with the site itself. But if you’re on a corporate network, your support team may have more info. If you’re using antivirus software, it may need to be configured to work with { -brand-short-name }.

# This string appears after the following string: "What makes the site look dangerous?" (fp-certerror-why-site-dangerous)
fp-certerror-self-signed-why-dangerous-body = Because there’s an issue with the site’s certificate. Sites use certificates issued by a certificate authority to prove they’re really who they say they are. This site’s certificate is self-signed. It wasn’t issued by a recognized certificate authority – so we don’t trust it by default.
# This string appears after the following string: "What can you do about it?" (fp-certerror-what-can-you-do)
fp-certerror-self-signed-what-can-you-do-body = Not much. It’s likely there’s a problem with the site itself.
fp-certerror-self-signed-important-note = IMPORTANT NOTE: If you are trying to visit this site on a corporate intranet, your IT staff may use self-signed certificates. They can help you check their authenticity.

# This string appears after the following string: "What makes the site look dangerous?" (fp-certerror-why-site-dangerous)
# Variables:
#   $date (Date) - Certificate expiration date.
fp-certerror-expired-why-dangerous-body = Sites use certificates issued by a certificate authority to prove they’re really who they say they are. { -brand-short-name } doesn’t trust this site because it looks like the certificate expired on { DATETIME($date, month: "numeric", day: "numeric", year: "numeric") }.

# This string appears after the following string: "What makes the site look dangerous?" (fp-certerror-why-site-dangerous)
# Variables:
#   $date (Date) - Certificate start date.
fp-certerror-not-yet-valid-why-dangerous-body = Sites use certificates issued by a certificate authority to prove they’re really who they say they are. { -brand-short-name } doesn’t trust this site because it looks like the certificate will not be valid until { DATETIME($date, month: "numeric", day: "numeric", year: "numeric") }.

# This string appears after the following string: "What can you do about it?" (fp-certerror-what-can-you-do)
# Variables:
#   $date (Date) - Clock date.
fp-certerror-expired-what-can-you-do-body = Your device’s clock is set to { DATETIME($date, month: "numeric", day: "numeric", year: "numeric") }. If this is correct, the security issue is probably with the site itself. If it’s wrong, you can change it in your device’s system settings.

# Variables:
#   $error (string) - NSS error code string that specifies type of cert error. e.g. unknown issuer, invalid cert, etc.
fp-cert-error-code = Error Code: { $error }

# Variables:
#   $datetime (Date) - Current datetime.
fp-datetime = { DATETIME($datetime, month: "short", year: "numeric", day: "numeric") } { DATETIME($datetime, timeStyle: "long") }

fp-learn-more-about-secure-connection-failures = Learn more about secure connection failures
fp-learn-more-about-cert-issues = Learn more about these kinds of certificate issues
fp-learn-more-about-time-related-errors = Learn more about troubleshooting time-related errors
