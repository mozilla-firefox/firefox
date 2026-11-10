#!/usr/bin/env python
import json


class Mod:
    """
    A nserror module. When used with a `with` statement, binds the itself to
    Mod.active.
    """

    active = None

    def __init__(self, num):
        self.num = num

    def __enter__(self):
        Mod.active = self

    def __exit__(self, _type, _value, _traceback):
        Mod.active = None


modules = {}

# To add error code to your module, you need to do the following:
#
# 1) Add a module offset code.  Add yours to the bottom of the list
# right below this comment, adding 1.
#
# 2) In your module, define a header file which uses one of the
# NE_ERROR_GENERATExxxxxx macros.  Some examples below:
#
#    #define NS_ERROR_MYMODULE_MYERROR1 \
#        NS_ERROR_GENERATE(NS_ERROR_SEVERITY_ERROR,NS_ERROR_MODULE_MYMODULE,1)
#    #define NS_ERROR_MYMODULE_MYERROR2 NS_ERROR_GENERATE_SUCCESS(NS_ERROR_MODULE_MYMODULE,2)
#    #define NS_ERROR_MYMODULE_MYERROR3 NS_ERROR_GENERATE_FAILURE(NS_ERROR_MODULE_MYMODULE,3)

# @name Standard Module Offset Code. Each Module should identify a unique number
#       and then all errors associated with that module become offsets from the
#       base associated with that module id. There are 16 bits of code bits for
#       each module.

modules["XPCOM"] = Mod(1)
modules["BASE"] = Mod(2)
modules["GFX"] = Mod(3)
modules["WIDGET"] = Mod(4)
modules["CALENDAR"] = Mod(5)
modules["NETWORK"] = Mod(6)
modules["PLUGINS"] = Mod(7)
modules["LAYOUT"] = Mod(8)
modules["HTMLPARSER"] = Mod(9)
modules["RDF"] = Mod(10)
modules["UCONV"] = Mod(11)
modules["REG"] = Mod(12)
modules["FILES"] = Mod(13)
modules["DOM"] = Mod(14)
modules["IMGLIB"] = Mod(15)
modules["EDITOR"] = Mod(17)
# Mod(16) was MAILNEWS (see Bug 1927029, now optional via MOZ_EXTRA_ERROR_DEFINITIONS)
modules["XPCONNECT"] = Mod(18)
modules["PROFILE"] = Mod(19)
modules["LDAP"] = Mod(20)
modules["SECURITY"] = Mod(21)
modules["DOM_XPATH"] = Mod(22)
# Mod(23) used to be NS_ERROR_MODULE_DOM_RANGE (see bug 711047)
modules["URILOADER"] = Mod(24)
modules["CONTENT"] = Mod(25)
modules["PYXPCOM"] = Mod(26)
modules["XSLT"] = Mod(27)
modules["IPC"] = Mod(28)
modules["SVG"] = Mod(29)
modules["STORAGE"] = Mod(30)
modules["SCHEMA"] = Mod(31)
modules["DOM_FILE"] = Mod(32)
modules["DOM_INDEXEDDB"] = Mod(33)
modules["DOM_FILEHANDLE"] = Mod(34)
modules["SIGNED_JAR"] = Mod(35)
modules["DOM_FILESYSTEM"] = Mod(36)
modules["DOM_BLUETOOTH"] = Mod(37)
modules["SIGNED_APP"] = Mod(38)
modules["DOM_ANIM"] = Mod(39)
modules["DOM_PUSH"] = Mod(40)
modules["DOM_MEDIA"] = Mod(41)
modules["URL_CLASSIFIER"] = Mod(42)
# ErrorResult gets its own module to reduce the chance of someone accidentally
# defining an error code matching one of the ErrorResult ones.
modules["ERRORRESULT"] = Mod(43)
# Win32 system error codes, which are not mapped to a specific other value,
# see Bug 1686041.
modules["WIN32"] = Mod(44)
modules["WDBA"] = Mod(45)
modules["DOM_QM"] = Mod(46)
modules["CLIPBOARD"] = Mod(47)
modules["DOM_SERIAL"] = Mod(48)

# NS_ERROR_MODULE_GENERAL should be used by modules that do not
# care if return code values overlap. Callers of methods that
# return such codes should be aware that they are not
# globally unique. Implementors should be careful about blindly
# returning codes from other modules that might also use
# the generic base.
modules["GENERAL"] = Mod(51)

MODULE_BASE_OFFSET = 0x45

NS_ERROR_SEVERITY_SUCCESS = 0
NS_ERROR_SEVERITY_ERROR = 1


def SUCCESS_OR_FAILURE(sev, module, code):
    return (sev << 31) | ((module + MODULE_BASE_OFFSET) << 16) | code


def FAILURE(code):
    return SUCCESS_OR_FAILURE(NS_ERROR_SEVERITY_ERROR, Mod.active.num, code)


def SUCCESS(code):
    return SUCCESS_OR_FAILURE(NS_ERROR_SEVERITY_SUCCESS, Mod.active.num, code)


# Errors is an ordered dictionary, so that we can recover the order in which
# they were defined. This is important for determining which name is the
# canonical name for an error code.
errors = {}

# Standard "it worked" return value
errors["NS_OK"] = 0

# =======================================================================
# Core errors, not part of any modules
# =======================================================================
errors["NS_ERROR_BASE"] = 0xC1F30000
# Returned when an instance is not initialized
errors["NS_ERROR_NOT_INITIALIZED"] = errors["NS_ERROR_BASE"] + 1
# Returned when an instance is already initialized
errors["NS_ERROR_ALREADY_INITIALIZED"] = errors["NS_ERROR_BASE"] + 2
# Returned by a not implemented function
errors["NS_ERROR_NOT_IMPLEMENTED"] = 0x80004001
# Returned when a given interface is not supported.
errors["NS_NOINTERFACE"] = 0x80004002
errors["NS_ERROR_NO_INTERFACE"] = errors["NS_NOINTERFACE"]
# Returned when a function aborts
errors["NS_ERROR_ABORT"] = 0x80004004
# Returned when a function fails
errors["NS_ERROR_FAILURE"] = 0x80004005
# Returned when an unexpected error occurs
errors["NS_ERROR_UNEXPECTED"] = 0x8000FFFF
# Returned when a memory allocation fails
errors["NS_ERROR_OUT_OF_MEMORY"] = 0x8007000E
# Returned when an illegal value is passed
errors["NS_ERROR_ILLEGAL_VALUE"] = 0x80070057
errors["NS_ERROR_INVALID_ARG"] = errors["NS_ERROR_ILLEGAL_VALUE"]
errors["NS_ERROR_INVALID_POINTER"] = errors["NS_ERROR_INVALID_ARG"]
errors["NS_ERROR_NULL_POINTER"] = errors["NS_ERROR_INVALID_ARG"]
# Returned when an operation can't complete due to an unavailable resource
errors["NS_ERROR_NOT_AVAILABLE"] = 0x80040111
# Returned when a class is not registered
errors["NS_ERROR_FACTORY_NOT_REGISTERED"] = 0x80040154
# Returned when a class cannot be registered, but may be tried again later
errors["NS_ERROR_FACTORY_REGISTER_AGAIN"] = 0x80040155
# Returned when a dynamically loaded factory couldn't be found
errors["NS_ERROR_FACTORY_NOT_LOADED"] = 0x800401F8
# Returned when a factory doesn't support signatures
errors["NS_ERROR_FACTORY_NO_SIGNATURE_SUPPORT"] = errors["NS_ERROR_BASE"] + 0x101
# Returned when a factory already is registered
errors["NS_ERROR_FACTORY_EXISTS"] = errors["NS_ERROR_BASE"] + 0x100


# =======================================================================
# 1: NS_ERROR_MODULE_XPCOM
# =======================================================================
with modules["XPCOM"]:
    # Result codes used by nsIVariant
    errors["NS_ERROR_CANNOT_CONVERT_DATA"] = FAILURE(1)
    errors["NS_ERROR_OBJECT_IS_IMMUTABLE"] = FAILURE(2)
    errors["NS_ERROR_LOSS_OF_SIGNIFICANT_DATA"] = FAILURE(3)
    # Result code used by nsIThreadManager
    errors["NS_ERROR_NOT_SAME_THREAD"] = FAILURE(4)
    # Various operations are not permitted during XPCOM shutdown and will fail
    # with this exception.
    errors["NS_ERROR_ILLEGAL_DURING_SHUTDOWN"] = FAILURE(30)
    errors["NS_ERROR_SERVICE_NOT_AVAILABLE"] = FAILURE(22)
    # nsAppRunner fatal errors
    errors["NS_ERROR_OMNIJAR_CORRUPT"] = FAILURE(40)
    errors["NS_ERROR_OMNIJAR_OR_DIR_MISSING"] = FAILURE(41)

    errors["NS_SUCCESS_LOSS_OF_INSIGNIFICANT_DATA"] = SUCCESS(1)
    # Used by nsCycleCollectionParticipant
    errors["NS_SUCCESS_INTERRUPTED_TRAVERSE"] = SUCCESS(2)

# =======================================================================
# 2: NS_ERROR_MODULE_BASE
# =======================================================================
with modules["BASE"]:
    # I/O Errors

    #  Stream closed
    errors["NS_BASE_STREAM_CLOSED"] = FAILURE(2)
    #  Error from the operating system
    errors["NS_BASE_STREAM_OSERROR"] = FAILURE(3)
    #  Illegal arguments
    errors["NS_BASE_STREAM_ILLEGAL_ARGS"] = FAILURE(4)
    #  For unichar streams
    errors["NS_BASE_STREAM_NO_CONVERTER"] = FAILURE(5)
    #  For unichar streams
    errors["NS_BASE_STREAM_BAD_CONVERSION"] = FAILURE(6)
    errors["NS_BASE_STREAM_WOULD_BLOCK"] = FAILURE(7)


# =======================================================================
# 3: NS_ERROR_MODULE_GFX
# =======================================================================
with modules["GFX"]:
    # no printer available (e.g. cannot find _any_ printer)
    errors["NS_ERROR_GFX_PRINTER_NO_PRINTER_AVAILABLE"] = FAILURE(1)
    # _specified_ (by name) printer not found
    errors["NS_ERROR_GFX_PRINTER_NAME_NOT_FOUND"] = FAILURE(2)
    # print-to-file: could not open output file
    errors["NS_ERROR_GFX_PRINTER_COULD_NOT_OPEN_FILE"] = FAILURE(3)
    # print: starting document
    errors["NS_ERROR_GFX_PRINTER_STARTDOC"] = FAILURE(4)
    # print: ending document
    errors["NS_ERROR_GFX_PRINTER_ENDDOC"] = FAILURE(5)
    # print: starting page
    errors["NS_ERROR_GFX_PRINTER_STARTPAGE"] = FAILURE(6)
    # The document is still being loaded
    errors["NS_ERROR_GFX_PRINTER_DOC_IS_BUSY"] = FAILURE(7)

    # Font cmap is strangely structured - avoid this font!
    errors["NS_ERROR_GFX_CMAP_MALFORMED"] = FAILURE(51)


# =======================================================================
# 4:  NS_ERROR_MODULE_WIDGET
# =======================================================================
with modules["WIDGET"]:
    # Used by:
    #   - nsIWidget::NotifyIME()
    # Returned when the notification or the event is handled and it's consumed
    # by somebody.
    errors["NS_SUCCESS_EVENT_CONSUMED"] = SUCCESS(1)


# =======================================================================
# 6: NS_ERROR_MODULE_NETWORK
# =======================================================================
with modules["NETWORK"]:
    # General async request error codes:
    #
    # These error codes are commonly passed through callback methods to indicate
    # the status of some requested async request.
    #
    # For example, see nsIRequestObserver::onStopRequest.

    # The async request completed successfully.
    errors["NS_BINDING_SUCCEEDED"] = errors["NS_OK"]

    # The async request failed for some unknown reason.
    errors["NS_BINDING_FAILED"] = FAILURE(1)
    # The async request failed because it was aborted by some user action.
    errors["NS_BINDING_ABORTED"] = FAILURE(2)
    # The async request has been "redirected" to a different async request.
    # (e.g., an HTTP redirect occurred).
    #
    # This error code is used with load groups to notify the load group observer
    # when a request in the load group is redirected to another request.
    errors["NS_BINDING_REDIRECTED"] = FAILURE(3)
    # The async request has been "retargeted" to a different "handler."
    #
    # This error code is used with load groups to notify the load group observer
    # when a request in the load group is removed from the load group and added
    # to a different load group.
    errors["NS_BINDING_RETARGETED"] = FAILURE(4)

    # Miscellaneous error codes: These errors are not typically passed via
    # onStopRequest.

    # The URI is malformed.
    errors["NS_ERROR_MALFORMED_URI"] = FAILURE(10)
    # The requested action could not be completed while the object is busy.
    # Implementations of nsIChannel::asyncOpen will commonly return this error
    # if the channel has already been opened (and has not yet been closed).
    errors["NS_ERROR_IN_PROGRESS"] = FAILURE(15)
    # Returned from nsIChannel::asyncOpen to indicate that OnDataAvailable will
    # not be called because there is no content available.  This is used by
    # helper app style protocols (e.g., mailto).  XXX perhaps this should be a
    # success code.
    errors["NS_ERROR_NO_CONTENT"] = FAILURE(17)
    # The URI scheme corresponds to an unknown protocol handler.
    errors["NS_ERROR_UNKNOWN_PROTOCOL"] = FAILURE(18)
    # The content encoding of the source document was incorrect, for example
    # returning a plain HTML document advertised as Content-Encoding: gzip
    errors["NS_ERROR_INVALID_CONTENT_ENCODING"] = FAILURE(27)
    # A transport level corruption was found in the source document. for example
    # a document with a calculated checksum that does not match the Content-MD5
    # http header.
    errors["NS_ERROR_CORRUPTED_CONTENT"] = FAILURE(29)
    # A content signature verification failed for some reason. This can be either
    # an actual verification error, or any other error that led to the fact that
    # a content signature that was expected couldn't be verified.
    errors["NS_ERROR_INVALID_SIGNATURE"] = FAILURE(58)
    # While parsing for the first component of a header field using syntax as in
    # Content-Disposition or Content-Type, the first component was found to be
    # empty, such as in: Content-Disposition: ; filename=foo
    errors["NS_ERROR_FIRST_HEADER_FIELD_COMPONENT_EMPTY"] = FAILURE(34)
    # Returned from nsIChannel::asyncOpen when trying to open the channel again
    # (reopening is not supported).
    errors["NS_ERROR_ALREADY_OPENED"] = FAILURE(73)

    # Connectivity error codes:

    # The connection is already established.  XXX unused - consider removing.
    errors["NS_ERROR_ALREADY_CONNECTED"] = FAILURE(11)
    # The connection does not exist.  XXX unused - consider removing.
    errors["NS_ERROR_NOT_CONNECTED"] = FAILURE(12)
    # The connection attempt failed, for example, because no server was
    # listening at specified host:port.
    errors["NS_ERROR_CONNECTION_REFUSED"] = FAILURE(13)
    # The connection was lost due to a timeout error.
    errors["NS_ERROR_NET_TIMEOUT"] = FAILURE(14)
    # The requested action could not be completed while the networking library
    # is in the offline state.
    errors["NS_ERROR_OFFLINE"] = FAILURE(16)
    # The requested action was prohibited because it would have caused the
    # networking library to establish a connection to an unsafe or otherwise
    # banned port.
    errors["NS_ERROR_PORT_ACCESS_NOT_ALLOWED"] = FAILURE(19)
    # The connection was established, but no data was ever received.
    errors["NS_ERROR_NET_RESET"] = FAILURE(20)
    # The connection was established, but browser received an error response from the server
    errors["NS_ERROR_NET_ERROR_RESPONSE"] = FAILURE(35)
    # The connection was established, but browser received an empty page with 4xx, 5xx error response
    errors["NS_ERROR_NET_EMPTY_RESPONSE"] = FAILURE(36)
    # The connection was established, but the data transfer was interrupted.
    errors["NS_ERROR_NET_INTERRUPT"] = FAILURE(71)
    # The connection attempt to a proxy failed.
    errors["NS_ERROR_PROXY_CONNECTION_REFUSED"] = FAILURE(72)
    # A transfer was only partially done when it completed.
    errors["NS_ERROR_NET_PARTIAL_TRANSFER"] = FAILURE(76)
    # HTTP/2 detected invalid TLS configuration
    errors["NS_ERROR_NET_INADEQUATE_SECURITY"] = FAILURE(82)
    # HTTP/2 sent a GOAWAY
    errors["NS_ERROR_NET_HTTP2_SENT_GOAWAY"] = FAILURE(83)
    # HTTP/3 protocol internal error
    errors["NS_ERROR_NET_HTTP3_PROTOCOL_ERROR"] = FAILURE(84)
    # A timeout error code that can be used to cancel requests.
    errors["NS_ERROR_NET_TIMEOUT_EXTERNAL"] = FAILURE(85)
    # An HTTP/2 or HTTP/3 session was closed without a clean shutdown (e.g. no
    # GOAWAY) and no response bytes were received for the stream, so it is
    # safe to retry on a new connection.
    errors["NS_ERROR_NET_UNCLEAN_SHUTDOWN"] = FAILURE(101)
    # An error related to HTTPS-only mode
    errors["NS_ERROR_HTTPS_ONLY"] = FAILURE(86)
    # A WebSocket connection is failed.
    errors["NS_ERROR_WEBSOCKET_CONNECTION_REFUSED"] = FAILURE(87)
    # A connection to a non local address is refused because
    # xpc::AreNonLocalConnectionsDisabled() returns true.
    errors["NS_ERROR_NON_LOCAL_CONNECTION_REFUSED"] = FAILURE(88)
    # Connection to a sts host without a hsts header.
    errors["NS_ERROR_BAD_HSTS_CERT"] = FAILURE(89)
    # Error parsing the status line of an HTTP response
    errors["NS_ERROR_PARSING_HTTP_STATUS_LINE"] = FAILURE(90)
    # The user attempted basic HTTP authentication while
    # the basic_http_auth pref is disabled
    errors["NS_ERROR_BASIC_HTTP_AUTH_DISABLED"] = FAILURE(92)
    errors["NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED"] = FAILURE(93)
    # Used to indicate cases where we need to fall back from HTTP/2
    # to HTTP/1.1.
    errors["NS_ERROR_HTTP2_FALLBACK_TO_HTTP1"] = FAILURE(94)
    # The connection was blocked by the OS itself because this app lacks the
    # platform-level local-network permission (e.g. Android 16+'s Local
    # Network Protection, gated on ACCESS_LOCAL_NETWORK/NEARBY_DEVICES).
    # Distinct from NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED, which is Firefox's
    # own site-permission decision made after a successful connect -- this
    # error means the connect itself never had a chance to succeed, so it
    # must not be routed through that (unrelated) content-permission flow.
    errors["NS_ERROR_OS_LOCAL_NETWORK_ACCESS_DENIED"] = FAILURE(95)
    # The request body can only be sent once, so the request cannot be
    # retried, resubmitted with credentials, or carried on a connection that
    # would need its length up front.
    errors["NS_ERROR_NET_BODY_NOT_REPLAYABLE"] = FAILURE(96)

    # XXX really need to better rationalize these error codes.  are consumers of
    # necko really expected to know how to discern the meaning of these??
    # This request is not resumable, but it was tried to resume it, or to
    # request resume-specific data.
    errors["NS_ERROR_NOT_RESUMABLE"] = FAILURE(25)
    # The request failed as a result of a detected redirection loop.
    errors["NS_ERROR_REDIRECT_LOOP"] = FAILURE(31)
    # It was attempted to resume the request, but the entity has changed in the
    # meantime.
    errors["NS_ERROR_ENTITY_CHANGED"] = FAILURE(32)
    # The request failed because the content type returned by the server was not
    # a type expected by the channel (for nested channels such as the JAR
    # channel).
    errors["NS_ERROR_UNSAFE_CONTENT_TYPE"] = FAILURE(74)
    # The request resulted in an error page being displayed.
    errors["NS_ERROR_LOAD_SHOWED_ERRORPAGE"] = FAILURE(77)
    # The request occurred in docshell that lacks a treeowner, so it is
    # probably in the process of being torn down.
    errors["NS_ERROR_DOCSHELL_DYING"] = FAILURE(78)
    # A document channel opened in the parent process to make a process
    # selection decision was canceled because the parent channel was never
    # linked up with a channel in the selected content process (which instead
    # opened its own independent channel, e.g. for about: documents). This is an
    # expected outcome rather than a real load failure.
    errors["NS_ERROR_DOCUMENT_LOAD_LISTENER_NO_PARENT_CHANNEL"] = FAILURE(79)

    # DNS specific error codes:

    # The lookup of a hostname failed.  This generally refers to the hostname
    # from the URL being loaded.
    errors["NS_ERROR_UNKNOWN_HOST"] = FAILURE(30)
    # A low or medium priority DNS lookup failed because the pending queue was
    # already full. High priorty (the default) always makes room
    errors["NS_ERROR_DNS_LOOKUP_QUEUE_FULL"] = FAILURE(33)
    # The lookup of a proxy hostname failed.  If a channel is configured to
    # speak to a proxy server, then it will generate this error if the proxy
    # hostname cannot be resolved.
    errors["NS_ERROR_UNKNOWN_PROXY_HOST"] = FAILURE(42)
    # This DNS error will occur when the resolver uses the Extended DNS Error
    # option to indicate an error code for which we should not fall back to the
    # default DNS resolver. This means the DNS failure is definitive.
    errors["NS_ERROR_DEFINITIVE_UNKNOWN_HOST"] = FAILURE(43)

    # Socket specific error codes:

    # The specified socket type does not exist.
    errors["NS_ERROR_UNKNOWN_SOCKET_TYPE"] = FAILURE(51)
    # The specified socket type could not be created.
    errors["NS_ERROR_SOCKET_CREATE_FAILED"] = FAILURE(52)
    # The operating system doesn't support the given type of address.
    errors["NS_ERROR_SOCKET_ADDRESS_NOT_SUPPORTED"] = FAILURE(53)
    # The address to which we tried to bind the socket was busy.
    errors["NS_ERROR_SOCKET_ADDRESS_IN_USE"] = FAILURE(54)

    # Cache specific error codes:
    errors["NS_ERROR_CACHE_KEY_NOT_FOUND"] = FAILURE(61)
    errors["NS_ERROR_CACHE_DATA_IS_STREAM"] = FAILURE(62)
    errors["NS_ERROR_CACHE_DATA_IS_NOT_STREAM"] = FAILURE(63)
    errors["NS_ERROR_CACHE_WAIT_FOR_VALIDATION"] = FAILURE(64)
    errors["NS_ERROR_CACHE_ENTRY_DOOMED"] = FAILURE(65)
    errors["NS_ERROR_CACHE_READ_ACCESS_DENIED"] = FAILURE(66)
    errors["NS_ERROR_CACHE_WRITE_ACCESS_DENIED"] = FAILURE(67)
    errors["NS_ERROR_CACHE_IN_USE"] = FAILURE(68)
    # Error passed through onStopRequest if the document could not be fetched
    # from the cache.
    errors["NS_ERROR_DOCUMENT_NOT_CACHED"] = FAILURE(70)

    # Effective TLD Service specific error codes:

    # The requested number of domain levels exceeds those present in the host
    # string.
    errors["NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS"] = FAILURE(80)
    # The host string is an IP address.
    errors["NS_ERROR_HOST_IS_IP_ADDRESS"] = FAILURE(81)

    # StreamLoader specific result codes:

    # Result code returned by nsIStreamLoaderObserver to indicate that the
    # observer is taking over responsibility for the data buffer, and the loader
    # should NOT free it.
    errors["NS_SUCCESS_ADOPTED_DATA"] = SUCCESS(90)

    # This success code may be returned by nsIAuthModule::getNextToken to
    # indicate that the authentication is finished and thus there's no need
    # to call getNextToken again.
    errors["NS_SUCCESS_AUTH_FINISHED"] = SUCCESS(40)

    # These are really not "results", they're statuses, used by nsITransport and
    # friends.  This is abuse of nsresult, but we'll put up with it for now.
    # nsITransport
    errors["NS_NET_STATUS_READING"] = SUCCESS(8)
    errors["NS_NET_STATUS_WRITING"] = SUCCESS(9)

    # nsISocketTransport
    errors["NS_NET_STATUS_RESOLVING_HOST"] = SUCCESS(3)
    errors["NS_NET_STATUS_RESOLVED_HOST"] = SUCCESS(11)
    errors["NS_NET_STATUS_CONNECTING_TO"] = SUCCESS(7)
    errors["NS_NET_STATUS_CONNECTED_TO"] = SUCCESS(4)
    errors["NS_NET_STATUS_TLS_HANDSHAKE_STARTING"] = SUCCESS(12)
    errors["NS_NET_STATUS_TLS_HANDSHAKE_ENDED"] = SUCCESS(13)
    errors["NS_NET_STATUS_SENDING_TO"] = SUCCESS(5)
    errors["NS_NET_STATUS_WAITING_FOR"] = SUCCESS(10)
    errors["NS_NET_STATUS_RECEIVING_FROM"] = SUCCESS(6)

    # nsIInterceptedChannel
    # Generic error for non-specific failures during service worker interception
    errors["NS_ERROR_INTERCEPTION_FAILED"] = FAILURE(100)

    # WebTransport Session Limit (mWebTransportMaxSessions) Exceeded
    errors["NS_ERROR_WEBTRANSPORT_SESSION_LIMIT_EXCEEDED"] = FAILURE(199)

    errors["NS_ERROR_WEBTRANSPORT_CODE_BASE"] = FAILURE(200)
    errors["NS_ERROR_WEBTRANSPORT_CODE_END"] = (
        errors["NS_ERROR_WEBTRANSPORT_CODE_BASE"] + 255
    )

    # All Http proxy CONNECT response codes
    errors["NS_ERROR_PROXY_CODE_BASE"] = FAILURE(1000)
    # Redirection 3xx
    errors["NS_ERROR_PROXY_MULTIPLE_CHOICES"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 300
    errors["NS_ERROR_PROXY_MOVED_PERMANENTLY"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 301
    )
    errors["NS_ERROR_PROXY_FOUND"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 302
    errors["NS_ERROR_PROXY_SEE_OTHER"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 303
    errors["NS_ERROR_PROXY_NOT_MODIFIED"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 304
    errors["NS_ERROR_PROXY_TEMPORARY_REDIRECT"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 307
    )
    errors["NS_ERROR_PROXY_PERMANENT_REDIRECT"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 308
    )

    # Client error 4xx
    errors["NS_ERROR_PROXY_BAD_REQUEST"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 400
    errors["NS_ERROR_PROXY_UNAUTHORIZED"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 401
    errors["NS_ERROR_PROXY_PAYMENT_REQUIRED"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 402
    errors["NS_ERROR_PROXY_FORBIDDEN"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 403
    errors["NS_ERROR_PROXY_NOT_FOUND"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 404
    errors["NS_ERROR_PROXY_METHOD_NOT_ALLOWED"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 405
    )
    errors["NS_ERROR_PROXY_NOT_ACCEPTABLE"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 406
    # The proxy requires authentication; used when we can't easily propagate 407s.
    errors["NS_ERROR_PROXY_AUTHENTICATION_FAILED"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 407
    )
    errors["NS_ERROR_PROXY_REQUEST_TIMEOUT"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 408
    errors["NS_ERROR_PROXY_CONFLICT"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 409
    errors["NS_ERROR_PROXY_GONE"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 410
    errors["NS_ERROR_PROXY_LENGTH_REQUIRED"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 411
    errors["NS_ERROR_PROXY_PRECONDITION_FAILED"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 412
    )
    errors["NS_ERROR_PROXY_REQUEST_ENTITY_TOO_LARGE"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 413
    )
    errors["NS_ERROR_PROXY_REQUEST_URI_TOO_LONG"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 414
    )
    errors["NS_ERROR_PROXY_UNSUPPORTED_MEDIA_TYPE"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 415
    )
    errors["NS_ERROR_PROXY_REQUESTED_RANGE_NOT_SATISFIABLE"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 416
    )
    errors["NS_ERROR_PROXY_EXPECTATION_FAILED"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 417
    )
    errors["NS_ERROR_PROXY_MISDIRECTED_REQUEST"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 421
    )
    errors["NS_ERROR_PROXY_TOO_EARLY"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 425
    errors["NS_ERROR_PROXY_UPGRADE_REQUIRED"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 426
    errors["NS_ERROR_PROXY_PRECONDITION_REQUIRED"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 428
    )
    # Indicates that we have sent too many requests in a given amount of time.
    errors["NS_ERROR_PROXY_TOO_MANY_REQUESTS"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 429
    )
    errors["NS_ERROR_PROXY_REQUEST_HEADER_FIELDS_TOO_LARGE"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 431
    )
    errors["NS_ERROR_PROXY_UNAVAILABLE_FOR_LEGAL_REASONS"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 451
    )

    # Server error 5xx
    errors["NS_ERROR_PROXY_INTERNAL_SERVER_ERROR"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 500
    )
    errors["NS_ERROR_PROXY_NOT_IMPLEMENTED"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 501
    errors["NS_ERROR_PROXY_BAD_GATEWAY"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 502
    errors["NS_ERROR_PROXY_SERVICE_UNAVAILABLE"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 503
    )
    # The proxy did get any response from the remote server in time.
    errors["NS_ERROR_PROXY_GATEWAY_TIMEOUT"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 504
    errors["NS_ERROR_PROXY_VERSION_NOT_SUPPORTED"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 505
    )
    errors["NS_ERROR_PROXY_VARIANT_ALSO_NEGOTIATES"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 506
    )
    errors["NS_ERROR_PROXY_NOT_EXTENDED"] = errors["NS_ERROR_PROXY_CODE_BASE"] + 510
    errors["NS_ERROR_PROXY_NETWORK_AUTHENTICATION_REQUIRED"] = (
        errors["NS_ERROR_PROXY_CODE_BASE"] + 511
    )

# =======================================================================
# 7: NS_ERROR_MODULE_PLUGINS
# =======================================================================
with modules["PLUGINS"]:
    errors["NS_ERROR_PLUGINS_PLUGINSNOTCHANGED"] = FAILURE(1000)
    errors["NS_ERROR_PLUGIN_DISABLED"] = FAILURE(1001)
    errors["NS_ERROR_PLUGIN_TIME_RANGE_NOT_SUPPORTED"] = FAILURE(1003)
    errors["NS_ERROR_PLUGIN_CLICKTOPLAY"] = FAILURE(1004)


# =======================================================================
# 8: NS_ERROR_MODULE_LAYOUT
# =======================================================================
with modules["LAYOUT"]:
    # Return code for SheetLoadData::VerifySheetReadyToParse
    errors["NS_OK_PARSE_SHEET"] = SUCCESS(1)


# =======================================================================
# 9: NS_ERROR_MODULE_HTMLPARSER
# =======================================================================
with modules["HTMLPARSER"]:
    errors["NS_ERROR_HTMLPARSER_CONTINUE"] = errors["NS_OK"]
    errors["NS_ERROR_HTMLPARSER_EOF"] = FAILURE(1000)
    errors["NS_ERROR_HTMLPARSER_BADURL"] = FAILURE(1005)
    errors["NS_ERROR_HTMLPARSER_INVALIDPARSERCONTEXT"] = FAILURE(1006)
    errors["NS_ERROR_HTMLPARSER_INTERRUPTED"] = FAILURE(1007)
    errors["NS_ERROR_HTMLPARSER_BLOCK"] = FAILURE(1008)
    errors["NS_ERROR_HTMLPARSER_UNRESOLVEDDTD"] = FAILURE(1011)
    errors["NS_ERROR_HTMLPARSER_STOPPARSING"] = FAILURE(1015)
    errors["NS_ERROR_HTMLPARSER_HIERARCHYTOODEEP"] = FAILURE(1017)


# =======================================================================
# 10: NS_ERROR_MODULE_RDF
# =======================================================================
with modules["RDF"]:
    # Returned from nsIRDFDataSource::Assert() and Unassert() if the assertion
    # (or unassertion was accepted by the datasource
    errors["NS_RDF_ASSERTION_ACCEPTED"] = errors["NS_OK"]
    # Returned from nsIRDFDataSource::GetSource() and GetTarget() if the
    # source/target has no value
    errors["NS_RDF_NO_VALUE"] = SUCCESS(2)
    # Returned from nsIRDFDataSource::Assert() and Unassert() if the assertion
    # (or unassertion) was rejected by the datasource; i.e., the datasource was
    # not willing to record the statement.
    errors["NS_RDF_ASSERTION_REJECTED"] = SUCCESS(3)
    # Return this from rdfITripleVisitor to stop cycling
    errors["NS_RDF_STOP_VISIT"] = SUCCESS(4)


# =======================================================================
# 11: NS_ERROR_MODULE_UCONV
# =======================================================================
with modules["UCONV"]:
    errors["NS_ERROR_UCONV_NOCONV"] = FAILURE(1)
    errors["NS_ERROR_UDEC_ILLEGALINPUT"] = FAILURE(14)

    errors["NS_OK_HAD_REPLACEMENTS"] = SUCCESS(3)
    errors["NS_OK_UDEC_MOREINPUT"] = SUCCESS(12)
    errors["NS_OK_UDEC_MOREOUTPUT"] = SUCCESS(13)
    errors["NS_OK_UENC_MOREOUTPUT"] = SUCCESS(34)
    errors["NS_ERROR_UENC_NOMAPPING"] = SUCCESS(35)

    # BEGIN DEPRECATED
    errors["NS_ERROR_ILLEGAL_INPUT"] = errors["NS_ERROR_UDEC_ILLEGALINPUT"]
    # END DEPRECATED


# =======================================================================
# 13: NS_ERROR_MODULE_FILES
# =======================================================================
with modules["FILES"]:
    errors["NS_ERROR_FILE_UNRECOGNIZED_PATH"] = FAILURE(1)
    errors["NS_ERROR_FILE_UNRESOLVABLE_SYMLINK"] = FAILURE(2)
    errors["NS_ERROR_FILE_EXECUTION_FAILED"] = FAILURE(3)
    errors["NS_ERROR_FILE_UNKNOWN_TYPE"] = FAILURE(4)
    errors["NS_ERROR_FILE_DESTINATION_NOT_DIR"] = FAILURE(5)
    errors["NS_ERROR_FILE_COPY_OR_MOVE_FAILED"] = FAILURE(7)
    errors["NS_ERROR_FILE_ALREADY_EXISTS"] = FAILURE(8)
    errors["NS_ERROR_FILE_INVALID_PATH"] = FAILURE(9)
    errors["NS_ERROR_FILE_CORRUPTED"] = FAILURE(11)
    errors["NS_ERROR_FILE_NOT_DIRECTORY"] = FAILURE(12)
    errors["NS_ERROR_FILE_IS_DIRECTORY"] = FAILURE(13)
    errors["NS_ERROR_FILE_IS_LOCKED"] = FAILURE(14)
    errors["NS_ERROR_FILE_TOO_BIG"] = FAILURE(15)
    errors["NS_ERROR_FILE_NO_DEVICE_SPACE"] = FAILURE(16)
    errors["NS_ERROR_FILE_NAME_TOO_LONG"] = FAILURE(17)
    errors["NS_ERROR_FILE_NOT_FOUND"] = FAILURE(18)
    errors["NS_ERROR_FILE_READ_ONLY"] = FAILURE(19)
    errors["NS_ERROR_FILE_DIR_NOT_EMPTY"] = FAILURE(20)
    errors["NS_ERROR_FILE_ACCESS_DENIED"] = FAILURE(21)
    errors["NS_ERROR_FILE_FS_CORRUPTED"] = FAILURE(22)
    errors["NS_ERROR_FILE_DEVICE_FAILURE"] = FAILURE(23)
    errors["NS_ERROR_FILE_DEVICE_TEMPORARY_FAILURE"] = FAILURE(24)
    errors["NS_ERROR_FILE_INVALID_HANDLE"] = FAILURE(25)

    errors["NS_SUCCESS_FILE_DIRECTORY_EMPTY"] = SUCCESS(1)
    # Result codes used by nsIDirectoryServiceProvider2
    errors["NS_SUCCESS_AGGREGATE_RESULT"] = SUCCESS(2)


# =======================================================================
# 14: NS_ERROR_MODULE_DOM
# =======================================================================
with modules["DOM"]:
    # XXX If you add a new DOM error code, also add an error string to
    # dom/base/domerr.msg

    # Standard DOM error codes: http://dvcs.w3.org/hg/domcore/raw-file/tip/Overview.html
    errors["NS_ERROR_DOM_INDEX_SIZE_ERR"] = FAILURE(1)
    errors["NS_ERROR_DOM_HIERARCHY_REQUEST_ERR"] = FAILURE(3)
    errors["NS_ERROR_DOM_WRONG_DOCUMENT_ERR"] = FAILURE(4)
    errors["NS_ERROR_DOM_INVALID_CHARACTER_ERR"] = FAILURE(5)
    errors["NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR"] = FAILURE(7)
    errors["NS_ERROR_DOM_NOT_FOUND_ERR"] = FAILURE(8)
    errors["NS_ERROR_DOM_NOT_SUPPORTED_ERR"] = FAILURE(9)
    errors["NS_ERROR_DOM_INUSE_ATTRIBUTE_ERR"] = FAILURE(10)
    errors["NS_ERROR_DOM_INVALID_STATE_ERR"] = FAILURE(11)
    errors["NS_ERROR_DOM_SYNTAX_ERR"] = FAILURE(12)
    errors["NS_ERROR_DOM_INVALID_MODIFICATION_ERR"] = FAILURE(13)
    errors["NS_ERROR_DOM_NAMESPACE_ERR"] = FAILURE(14)
    errors["NS_ERROR_DOM_INVALID_ACCESS_ERR"] = FAILURE(15)
    errors["NS_ERROR_DOM_TYPE_MISMATCH_ERR"] = FAILURE(17)
    errors["NS_ERROR_DOM_SECURITY_ERR"] = FAILURE(18)
    errors["NS_ERROR_DOM_NETWORK_ERR"] = FAILURE(19)
    errors["NS_ERROR_DOM_ABORT_ERR"] = FAILURE(20)
    errors["NS_ERROR_DOM_URL_MISMATCH_ERR"] = FAILURE(21)
    errors["NS_ERROR_DOM_QUOTA_EXCEEDED_ERR"] = FAILURE(22)
    errors["NS_ERROR_DOM_TIMEOUT_ERR"] = FAILURE(23)
    errors["NS_ERROR_DOM_INVALID_NODE_TYPE_ERR"] = FAILURE(24)
    errors["NS_ERROR_DOM_DATA_CLONE_ERR"] = FAILURE(25)
    # StringEncoding API errors from http://wiki.whatwg.org/wiki/StringEncoding
    errors["NS_ERROR_DOM_ENCODING_NOT_SUPPORTED_ERR"] = FAILURE(28)
    # WebCrypto API errors from http://www.w3.org/TR/WebCryptoAPI/
    errors["NS_ERROR_DOM_UNKNOWN_ERR"] = FAILURE(30)
    errors["NS_ERROR_DOM_DATA_ERR"] = FAILURE(31)
    errors["NS_ERROR_DOM_OPERATION_ERR"] = FAILURE(32)
    # https://heycam.github.io/webidl/#notallowederror
    errors["NS_ERROR_DOM_NOT_ALLOWED_ERR"] = FAILURE(33)
    # DOM error codes defined by us
    errors["NS_ERROR_DOM_WRONG_TYPE_ERR"] = FAILURE(1002)
    errors["NS_ERROR_DOM_NOT_NUMBER_ERR"] = FAILURE(1005)
    errors["NS_ERROR_DOM_PROP_ACCESS_DENIED"] = FAILURE(1010)
    errors["NS_ERROR_DOM_XPCONNECT_ACCESS_DENIED"] = FAILURE(1011)
    errors["NS_ERROR_DOM_BAD_URI"] = FAILURE(1012)
    errors["NS_ERROR_DOM_RETVAL_UNDEFINED"] = FAILURE(1013)

    # A way to represent uncatchable exceptions
    errors["NS_ERROR_UNCATCHABLE_EXCEPTION"] = FAILURE(1015)

    errors["NS_ERROR_DOM_MALFORMED_URI"] = FAILURE(1016)
    errors["NS_ERROR_DOM_INVALID_HEADER_NAME"] = FAILURE(1017)

    errors["NS_ERROR_DOM_INVALID_STATE_XHR_HAS_INVALID_CONTEXT"] = FAILURE(1018)

    # When manipulating the bytecode cache with the JS API, some transcoding
    # errors, such as a different bytecode format can cause failures of the
    # decoding process.
    errors["NS_ERROR_DOM_JS_DECODING_ERROR"] = FAILURE(1026)

    # Image decode errors.
    errors["NS_ERROR_DOM_IMAGE_INACTIVE_DOCUMENT"] = FAILURE(1027)
    errors["NS_ERROR_DOM_IMAGE_INVALID_REQUEST"] = FAILURE(1028)
    errors["NS_ERROR_DOM_IMAGE_BROKEN"] = FAILURE(1029)

    # Used to indicate that a resource with the Cross-Origin-Resource-Policy
    # response header set failed the origin check.
    # https://fetch.spec.whatwg.org/#cross-origin-resource-policy-header
    errors["NS_ERROR_DOM_CORP_FAILED"] = FAILURE(1036)

    # Used to indicate that a URI may not be loaded into a cross-origin
    # context.
    errors["NS_ERROR_DOM_BAD_CROSS_ORIGIN_URI"] = FAILURE(1037)

    # The request failed because there are too many recursive iframes or
    # objects being loaded.
    errors["NS_ERROR_RECURSIVE_DOCUMENT_LOAD"] = FAILURE(1038)

    # WebExtension content script may not load this URL.
    errors["NS_ERROR_DOM_WEBEXT_CONTENT_SCRIPT_URI"] = FAILURE(1039)

    # Used to indicate that a resource load was blocked because of the
    # Cross-Origin-Embedder-Policy response header.
    # https://html.spec.whatwg.org/multipage/origin.html#coep
    errors["NS_ERROR_DOM_COEP_FAILED"] = FAILURE(1040)

    # Used to indicate that a resource load was blocked because of the
    # Cross-Origin-Opener-Policy response header.
    # https://html.spec.whatwg.org/multipage/origin.html#cross-origin-opener-policies
    errors["NS_ERROR_DOM_COOP_FAILED"] = FAILURE(1041)

    errors["NS_ERROR_DOM_INVALID_HEADER_VALUE"] = FAILURE(1042)

    # May be used to indicate when e.g. setting a property value didn't
    # actually change the value, like for obj.foo = "bar"; obj.foo = "bar";
    # the second assignment throws NS_SUCCESS_DOM_NO_OPERATION.
    errors["NS_SUCCESS_DOM_NO_OPERATION"] = SUCCESS(1)

    # A success code that indicates that evaluating a string of JS went
    # just fine except it threw an exception. Only for legacy use by
    # nsJSUtils.
    errors["NS_SUCCESS_DOM_SCRIPT_EVALUATION_THREW"] = SUCCESS(2)

    # A success code that indicates that evaluating a string of JS went
    # just fine except it was killed by an uncatchable exception.
    # Only for legacy use by nsJSUtils.
    errors["NS_SUCCESS_DOM_SCRIPT_EVALUATION_THREW_UNCATCHABLE"] = SUCCESS(3)


# =======================================================================
# 15: NS_ERROR_MODULE_IMGLIB
# =======================================================================
with modules["IMGLIB"]:
    errors["NS_IMAGELIB_ERROR_FAILURE"] = FAILURE(5)
    errors["NS_IMAGELIB_ERROR_NO_DECODER"] = FAILURE(6)
    errors["NS_IMAGELIB_ERROR_NOT_FINISHED"] = FAILURE(7)
    errors["NS_IMAGELIB_ERROR_NO_ENCODER"] = FAILURE(9)


# =======================================================================
# 17: NS_ERROR_MODULE_EDITOR
# =======================================================================
with modules["EDITOR"]:
    errors["NS_ERROR_EDITOR_DESTROYED"] = FAILURE(1)

    # An error code that indicates that the DOM tree has been modified by
    # web app or add-on while the editor modifying the tree.  However,
    # this shouldn't be exposed to the web because the result should've
    # been expected by the web app.
    errors["NS_ERROR_EDITOR_UNEXPECTED_DOM_TREE"] = FAILURE(2)

    # An error code that indicates that the edit action canceled by
    # clipboard event listener or beforeinput event listener.  Note that
    # don't make this as a success code since it's not check with NS_FAILED()
    # and may keep handling the operation unexpectedly.
    errors["NS_ERROR_EDITOR_ACTION_CANCELED"] = FAILURE(3)

    # An error code that indicates that there is no editable selection ranges.
    # E.g., Selection has no range, caret is in non-editable element,
    # non-collapsed range crosses editing host boundaries.
    errors["NS_ERROR_EDITOR_NO_EDITABLE_RANGE"] = FAILURE(4)

    # An error code that indicates that there is no deletable selection ranges
    # even though there are some editable ranges.  E.g., if each editable range
    # is in a replaced element or a void element.
    errors["NS_ERROR_EDITOR_NO_DELETABLE_RANGE"] = FAILURE(5)

    errors["NS_SUCCESS_EDITOR_ELEMENT_NOT_FOUND"] = SUCCESS(1)
    errors["NS_SUCCESS_EDITOR_FOUND_TARGET"] = SUCCESS(2)

    # If most callers ignore error except serious error (like
    # NS_ERROR_EDITOR_DESTROYED), this success code is useful.  E.g. such
    # callers can do:
    # nsresult rv = Foo();
    # if (MOZ_UNLIKELY(NS_FAILED(rv))) {
    #   NS_WARNING("Foo() failed");
    #   return rv;
    # }
    # NS_WARNING_ASSERTION(
    #   rv != NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR,
    #   "Foo() failed, but ignored");
    errors["NS_SUCCESS_EDITOR_BUT_IGNORED_TRIVIAL_ERROR"] = SUCCESS(3)


# =======================================================================
# 18: NS_ERROR_MODULE_XPCONNECT
# =======================================================================
with modules["XPCONNECT"]:
    errors["NS_ERROR_XPC_NOT_ENOUGH_ARGS"] = FAILURE(1)
    errors["NS_ERROR_XPC_NEED_OUT_OBJECT"] = FAILURE(2)
    errors["NS_ERROR_XPC_CANT_SET_OUT_VAL"] = FAILURE(3)
    errors["NS_ERROR_XPC_NATIVE_RETURNED_FAILURE"] = FAILURE(4)
    errors["NS_ERROR_XPC_CANT_GET_INTERFACE_INFO"] = FAILURE(5)
    errors["NS_ERROR_XPC_CANT_GET_PARAM_IFACE_INFO"] = FAILURE(6)
    errors["NS_ERROR_XPC_CANT_GET_METHOD_INFO"] = FAILURE(7)
    errors["NS_ERROR_XPC_UNEXPECTED"] = FAILURE(8)
    errors["NS_ERROR_XPC_BAD_CONVERT_JS"] = FAILURE(9)
    errors["NS_ERROR_XPC_BAD_CONVERT_NATIVE"] = FAILURE(10)
    errors["NS_ERROR_XPC_BAD_CONVERT_JS_NULL_REF"] = FAILURE(11)
    errors["NS_ERROR_XPC_BAD_OP_ON_WN_PROTO"] = FAILURE(12)
    errors["NS_ERROR_XPC_CANT_CONVERT_WN_TO_FUN"] = FAILURE(13)
    errors["NS_ERROR_XPC_CANT_DEFINE_PROP_ON_WN"] = FAILURE(14)
    errors["NS_ERROR_XPC_CANT_WATCH_WN_STATIC"] = FAILURE(15)
    errors["NS_ERROR_XPC_CANT_EXPORT_WN_STATIC"] = FAILURE(16)
    errors["NS_ERROR_XPC_SCRIPTABLE_CALL_FAILED"] = FAILURE(17)
    errors["NS_ERROR_XPC_SCRIPTABLE_CTOR_FAILED"] = FAILURE(18)
    errors["NS_ERROR_XPC_CANT_CALL_WO_SCRIPTABLE"] = FAILURE(19)
    errors["NS_ERROR_XPC_CANT_CTOR_WO_SCRIPTABLE"] = FAILURE(20)
    errors["NS_ERROR_XPC_CI_RETURNED_FAILURE"] = FAILURE(21)
    errors["NS_ERROR_XPC_GS_RETURNED_FAILURE"] = FAILURE(22)
    errors["NS_ERROR_XPC_BAD_CID"] = FAILURE(23)
    errors["NS_ERROR_XPC_BAD_IID"] = FAILURE(24)
    errors["NS_ERROR_XPC_CANT_CREATE_WN"] = FAILURE(25)
    errors["NS_ERROR_XPC_JS_THREW_EXCEPTION"] = FAILURE(26)
    errors["NS_ERROR_XPC_JS_THREW_NATIVE_OBJECT"] = FAILURE(27)
    errors["NS_ERROR_XPC_JS_THREW_JS_OBJECT"] = FAILURE(28)
    errors["NS_ERROR_XPC_JS_THREW_NULL"] = FAILURE(29)
    errors["NS_ERROR_XPC_JS_THREW_STRING"] = FAILURE(30)
    errors["NS_ERROR_XPC_JS_THREW_NUMBER"] = FAILURE(31)
    errors["NS_ERROR_XPC_JAVASCRIPT_ERROR"] = FAILURE(32)
    errors["NS_ERROR_XPC_JAVASCRIPT_ERROR_WITH_DETAILS"] = FAILURE(33)
    errors["NS_ERROR_XPC_CANT_CONVERT_PRIMITIVE_TO_ARRAY"] = FAILURE(34)
    errors["NS_ERROR_XPC_CANT_CONVERT_OBJECT_TO_ARRAY"] = FAILURE(35)
    errors["NS_ERROR_XPC_NOT_ENOUGH_ELEMENTS_IN_ARRAY"] = FAILURE(36)
    errors["NS_ERROR_XPC_CANT_GET_ARRAY_INFO"] = FAILURE(37)
    errors["NS_ERROR_XPC_NOT_ENOUGH_CHARS_IN_STRING"] = FAILURE(38)
    errors["NS_ERROR_XPC_SECURITY_MANAGER_VETO"] = FAILURE(39)
    errors["NS_ERROR_XPC_INTERFACE_NOT_SCRIPTABLE"] = FAILURE(40)
    errors["NS_ERROR_XPC_INTERFACE_NOT_FROM_NSISUPPORTS"] = FAILURE(41)
    errors["NS_ERROR_XPC_CANT_SET_READ_ONLY_CONSTANT"] = FAILURE(43)
    errors["NS_ERROR_XPC_CANT_SET_READ_ONLY_ATTRIBUTE"] = FAILURE(44)
    errors["NS_ERROR_XPC_CANT_SET_READ_ONLY_METHOD"] = FAILURE(45)
    errors["NS_ERROR_XPC_CANT_ADD_PROP_TO_WRAPPED_NATIVE"] = FAILURE(46)
    errors["NS_ERROR_XPC_CALL_TO_SCRIPTABLE_FAILED"] = FAILURE(47)
    errors["NS_ERROR_XPC_JSOBJECT_HAS_NO_FUNCTION_NAMED"] = FAILURE(48)
    errors["NS_ERROR_XPC_BAD_ID_STRING"] = FAILURE(49)
    errors["NS_ERROR_XPC_BAD_INITIALIZER_NAME"] = FAILURE(50)
    errors["NS_ERROR_XPC_HAS_BEEN_SHUTDOWN"] = FAILURE(51)
    errors["NS_ERROR_XPC_CANT_MODIFY_PROP_ON_WN"] = FAILURE(52)
    errors["NS_ERROR_XPC_BAD_CONVERT_JS_ZERO_ISNOT_NULL"] = FAILURE(53)
    # any new errors here should have an associated entry added in xpc.msg


# =======================================================================
# 19: NS_ERROR_MODULE_PROFILE
# =======================================================================
with modules["PROFILE"]:
    errors["NS_ERROR_LAUNCHED_CHILD_PROCESS"] = FAILURE(200)
    errors["NS_ERROR_SHOW_PROFILE_MANAGER"] = FAILURE(201)
    errors["NS_ERROR_DATABASE_CHANGED"] = FAILURE(202)


# =======================================================================
# 21: NS_ERROR_MODULE_SECURITY
# =======================================================================
with modules["SECURITY"]:
    # Error code for XFO
    errors["NS_ERROR_XFO_VIOLATION"] = FAILURE(96)

    # Error code for CSP
    errors["NS_ERROR_CSP_FORM_ACTION_VIOLATION"] = FAILURE(97)
    errors["NS_ERROR_CSP_FRAME_ANCESTOR_VIOLATION"] = FAILURE(98)

    # Error code for Connection Allowlists
    errors["NS_ERROR_CONNECTION_ALLOWLIST_VIOLATION"] = FAILURE(99)

    # Error code for Sub-Resource Integrity
    errors["NS_ERROR_SRI_CORRUPT"] = FAILURE(200)
    errors["NS_ERROR_SRI_NOT_ELIGIBLE"] = FAILURE(201)
    errors["NS_ERROR_SRI_UNEXPECTED_HASH_TYPE"] = FAILURE(202)
    errors["NS_ERROR_SRI_IMPORT"] = FAILURE(203)

    # CMS specific nsresult error codes.  Note: the numbers used here correspond
    # to the values in nsICMSMessageErrors.idl.
    errors["NS_ERROR_CMS_VERIFY_NOT_SIGNED"] = FAILURE(1024)
    errors["NS_ERROR_CMS_VERIFY_NO_CONTENT_INFO"] = FAILURE(1025)
    errors["NS_ERROR_CMS_VERIFY_BAD_DIGEST"] = FAILURE(1026)
    errors["NS_ERROR_CMS_VERIFY_NOCERT"] = FAILURE(1028)
    errors["NS_ERROR_CMS_VERIFY_UNTRUSTED"] = FAILURE(1029)
    errors["NS_ERROR_CMS_VERIFY_ERROR_UNVERIFIED"] = FAILURE(1031)
    errors["NS_ERROR_CMS_VERIFY_ERROR_PROCESSING"] = FAILURE(1032)
    errors["NS_ERROR_CMS_VERIFY_BAD_SIGNATURE"] = FAILURE(1033)
    errors["NS_ERROR_CMS_VERIFY_DIGEST_MISMATCH"] = FAILURE(1034)
    errors["NS_ERROR_CMS_VERIFY_UNKNOWN_ALGO"] = FAILURE(1035)
    errors["NS_ERROR_CMS_VERIFY_UNSUPPORTED_ALGO"] = FAILURE(1036)
    errors["NS_ERROR_CMS_VERIFY_MALFORMED_SIGNATURE"] = FAILURE(1037)
    errors["NS_ERROR_CMS_VERIFY_HEADER_MISMATCH"] = FAILURE(1038)
    errors["NS_ERROR_CMS_VERIFY_NOT_YET_ATTEMPTED"] = FAILURE(1039)
    errors["NS_ERROR_CMS_VERIFY_CERT_WITHOUT_ADDRESS"] = FAILURE(1040)
    errors["NS_ERROR_CMS_ENCRYPT_NO_BULK_ALG"] = FAILURE(1056)
    errors["NS_ERROR_CMS_ENCRYPT_INCOMPLETE"] = FAILURE(1057)


# =======================================================================
# 24: NS_ERROR_MODULE_URILOADER
# =======================================================================
with modules["URILOADER"]:
    errors["NS_ERROR_WONT_HANDLE_CONTENT"] = FAILURE(1)
    # The load has been cancelled because it was found on a malware or phishing
    # list.
    errors["NS_ERROR_MALWARE_URI"] = FAILURE(30)
    errors["NS_ERROR_PHISHING_URI"] = FAILURE(31)
    errors["NS_ERROR_TRACKING_URI"] = FAILURE(34)
    errors["NS_ERROR_UNWANTED_URI"] = FAILURE(35)
    errors["NS_ERROR_BLOCKED_URI"] = FAILURE(37)
    errors["NS_ERROR_HARMFUL_URI"] = FAILURE(38)
    errors["NS_ERROR_FINGERPRINTING_URI"] = FAILURE(41)
    errors["NS_ERROR_CRYPTOMINING_URI"] = FAILURE(42)
    errors["NS_ERROR_SOCIALTRACKING_URI"] = FAILURE(43)
    errors["NS_ERROR_EMAILTRACKING_URI"] = FAILURE(44)
    errors["NS_ERROR_RESTRICTED_CONTENT"] = FAILURE(45)
    errors["NS_ERROR_HARMFULADDON_URI"] = FAILURE(46)
    # Used when "Save Link As..." doesn't see the headers quickly enough to
    # choose a filename.  See nsContextMenu.js.
    errors["NS_ERROR_SAVE_LINK_AS_TIMEOUT"] = FAILURE(32)
    # Used when the data from a channel has already been parsed and cached so it
    # doesn't need to be reparsed from the original source.
    errors["NS_ERROR_PARSED_DATA_CACHED"] = FAILURE(33)

    # When SHIP is enabled and a load gets cancelled due to another one
    # starting, the error is NS_BINDING_CANCELLED_OLD_LOAD.
    errors["NS_BINDING_CANCELLED_OLD_LOAD"] = FAILURE(39)


# =======================================================================
# 25: NS_ERROR_MODULE_CONTENT
# =======================================================================
with modules["CONTENT"]:
    # Error codes for content policy blocking
    errors["NS_ERROR_CONTENT_BLOCKED"] = FAILURE(6)
    errors["NS_ERROR_CONTENT_BLOCKED_SHOW_ALT"] = FAILURE(7)
    # Success variations of content policy blocking
    errors["NS_PROPTABLE_PROP_NOT_THERE"] = FAILURE(10)
    # Error code for when the content process crashed
    errors["NS_ERROR_CONTENT_CRASHED"] = FAILURE(16)
    # Error code for when a subframe process crashed
    errors["NS_ERROR_FRAME_CRASHED"] = FAILURE(14)
    # Error code for when the content process had a different buildID than the
    # parent
    errors["NS_ERROR_BUILDID_MISMATCH"] = FAILURE(17)

    errors["NS_PROPTABLE_PROP_OVERWRITTEN"] = SUCCESS(11)
    # Error codes for FindBroadcaster in XULBroadcastManager.cpp
    errors["NS_FINDBROADCASTER_NOT_FOUND"] = SUCCESS(12)
    errors["NS_FINDBROADCASTER_FOUND"] = SUCCESS(13)


# =======================================================================
# 27: NS_ERROR_MODULE_XSLT
# =======================================================================
with modules["XSLT"]:
    errors["NS_ERROR_XPATH_INVALID_ARG"] = errors["NS_ERROR_INVALID_ARG"]

    errors["NS_ERROR_XSLT_PARSE_FAILURE"] = FAILURE(1)
    errors["NS_ERROR_XPATH_PARSE_FAILURE"] = FAILURE(2)
    errors["NS_ERROR_XSLT_ALREADY_SET"] = FAILURE(3)
    errors["NS_ERROR_XSLT_EXECUTION_FAILURE"] = FAILURE(4)
    errors["NS_ERROR_XPATH_UNKNOWN_FUNCTION"] = FAILURE(5)
    errors["NS_ERROR_XSLT_BAD_RECURSION"] = FAILURE(6)
    errors["NS_ERROR_XSLT_BAD_VALUE"] = FAILURE(7)
    errors["NS_ERROR_XSLT_NODESET_EXPECTED"] = FAILURE(8)
    errors["NS_ERROR_XSLT_ABORTED"] = FAILURE(9)
    errors["NS_ERROR_XSLT_NETWORK_ERROR"] = FAILURE(10)
    errors["NS_ERROR_XSLT_WRONG_MIME_TYPE"] = FAILURE(11)
    errors["NS_ERROR_XSLT_LOAD_RECURSION"] = FAILURE(12)
    errors["NS_ERROR_XPATH_BAD_ARGUMENT_COUNT"] = FAILURE(13)
    errors["NS_ERROR_XPATH_BAD_EXTENSION_FUNCTION"] = FAILURE(14)
    errors["NS_ERROR_XPATH_PAREN_EXPECTED"] = FAILURE(15)
    errors["NS_ERROR_XPATH_INVALID_AXIS"] = FAILURE(16)
    errors["NS_ERROR_XPATH_NO_NODE_TYPE_TEST"] = FAILURE(17)
    errors["NS_ERROR_XPATH_BRACKET_EXPECTED"] = FAILURE(18)
    errors["NS_ERROR_XPATH_INVALID_VAR_NAME"] = FAILURE(19)
    errors["NS_ERROR_XPATH_UNEXPECTED_END"] = FAILURE(20)
    errors["NS_ERROR_XPATH_OPERATOR_EXPECTED"] = FAILURE(21)
    errors["NS_ERROR_XPATH_UNCLOSED_LITERAL"] = FAILURE(22)
    errors["NS_ERROR_XPATH_BAD_COLON"] = FAILURE(23)
    errors["NS_ERROR_XPATH_BAD_BANG"] = FAILURE(24)
    errors["NS_ERROR_XPATH_ILLEGAL_CHAR"] = FAILURE(25)
    errors["NS_ERROR_XPATH_BINARY_EXPECTED"] = FAILURE(26)
    errors["NS_ERROR_XSLT_LOAD_BLOCKED_ERROR"] = FAILURE(27)
    errors["NS_ERROR_XPATH_INVALID_EXPRESSION_EVALUATED"] = FAILURE(28)
    errors["NS_ERROR_XPATH_UNBALANCED_CURLY_BRACE"] = FAILURE(29)
    errors["NS_ERROR_XSLT_BAD_NODE_NAME"] = FAILURE(30)
    errors["NS_ERROR_XSLT_VAR_ALREADY_SET"] = FAILURE(31)
    errors["NS_ERROR_XSLT_CALL_TO_KEY_NOT_ALLOWED"] = FAILURE(32)

    errors["NS_XSLT_GET_NEW_HANDLER"] = SUCCESS(1)


# =======================================================================
# 28: NS_ERROR_MODULE_IPC
# =======================================================================
with modules["IPC"]:
    # Initial creation of a Transport object failed internally for unknown reasons.
    errors["NS_ERROR_TRANSPORT_INIT"] = FAILURE(1)
    # Generic error related to duplicating handle failures.
    errors["NS_ERROR_DUPLICATE_HANDLE"] = FAILURE(2)
    # Bridging: failure trying to open the connection to the parent
    errors["NS_ERROR_BRIDGE_OPEN_PARENT"] = FAILURE(3)
    # Bridging: failure trying to open the connection to the child
    errors["NS_ERROR_BRIDGE_OPEN_CHILD"] = FAILURE(4)


# =======================================================================
# 30: NS_ERROR_MODULE_STORAGE
# =======================================================================
with modules["STORAGE"]:
    # To add additional errors to Storage, please append entries to the bottom
    # of the list in the following format:
    #   NS_ERROR_STORAGE_YOUR_ERR,  FAILURE(n)
    # where n is the next unique positive integer.  You must also add an entry
    # to js/xpconnect/src/xpc.msg under the code block beginning with the
    # comment 'storage related codes (from mozStorage.h)', in the following
    # format: 'XPC_MSG_DEF(NS_ERROR_STORAGE_YOUR_ERR, "brief description of your
    # error")'
    errors["NS_ERROR_STORAGE_BUSY"] = FAILURE(1)
    errors["NS_ERROR_STORAGE_IOERR"] = FAILURE(2)
    errors["NS_ERROR_STORAGE_CONSTRAINT"] = FAILURE(3)


# =======================================================================
# 32: NS_ERROR_MODULE_DOM_FILE
# =======================================================================
with modules["DOM_FILE"]:
    errors["NS_ERROR_DOM_FILE_NOT_FOUND_ERR"] = FAILURE(0)
    errors["NS_ERROR_DOM_FILE_NOT_READABLE_ERR"] = FAILURE(1)


# =======================================================================
# 33: NS_ERROR_MODULE_DOM_INDEXEDDB
# =======================================================================
with modules["DOM_INDEXEDDB"]:
    # IndexedDB error codes http://dvcs.w3.org/hg/IndexedDB/raw-file/tip/Overview.html
    errors["NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR"] = FAILURE(1)
    errors["NS_ERROR_DOM_INDEXEDDB_NOT_FOUND_ERR"] = FAILURE(3)
    errors["NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR"] = FAILURE(4)
    errors["NS_ERROR_DOM_INDEXEDDB_DATA_ERR"] = FAILURE(5)
    errors["NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR"] = FAILURE(6)
    errors["NS_ERROR_DOM_INDEXEDDB_TRANSACTION_INACTIVE_ERR"] = FAILURE(7)
    errors["NS_ERROR_DOM_INDEXEDDB_ABORT_ERR"] = FAILURE(8)
    errors["NS_ERROR_DOM_INDEXEDDB_READ_ONLY_ERR"] = FAILURE(9)
    errors["NS_ERROR_DOM_INDEXEDDB_QUOTA_ERR"] = FAILURE(11)
    errors["NS_ERROR_DOM_INDEXEDDB_VERSION_ERR"] = FAILURE(12)
    errors["NS_ERROR_DOM_INDEXEDDB_NOT_READABLE_ERR"] = FAILURE(13)
    errors["NS_ERROR_DOM_INDEXEDDB_KEY_ERR"] = FAILURE(1002)
    errors["NS_ERROR_DOM_INDEXEDDB_RENAME_OBJECT_STORE_ERR"] = FAILURE(1003)
    errors["NS_ERROR_DOM_INDEXEDDB_RENAME_INDEX_ERR"] = FAILURE(1004)


# =======================================================================
# 34: NS_ERROR_MODULE_DOM_FILEHANDLE
# =======================================================================
with modules["DOM_FILEHANDLE"]:
    errors["NS_ERROR_DOM_FILEHANDLE_UNKNOWN_ERR"] = FAILURE(1)
    errors["NS_ERROR_DOM_FILEHANDLE_NOT_ALLOWED_ERR"] = FAILURE(2)
    errors["NS_ERROR_DOM_FILEHANDLE_INACTIVE_ERR"] = FAILURE(3)
    errors["NS_ERROR_DOM_FILEHANDLE_ABORT_ERR"] = FAILURE(4)
    errors["NS_ERROR_DOM_FILEHANDLE_READ_ONLY_ERR"] = FAILURE(5)
    errors["NS_ERROR_DOM_FILEHANDLE_QUOTA_ERR"] = FAILURE(6)


# =======================================================================
# 35: NS_ERROR_MODULE_SIGNED_JAR
# =======================================================================
with modules["SIGNED_JAR"]:
    errors["NS_ERROR_SIGNED_JAR_NOT_SIGNED"] = FAILURE(1)
    errors["NS_ERROR_SIGNED_JAR_MODIFIED_ENTRY"] = FAILURE(2)
    errors["NS_ERROR_SIGNED_JAR_UNSIGNED_ENTRY"] = FAILURE(3)
    errors["NS_ERROR_SIGNED_JAR_ENTRY_MISSING"] = FAILURE(4)
    errors["NS_ERROR_SIGNED_JAR_WRONG_SIGNATURE"] = FAILURE(5)
    errors["NS_ERROR_SIGNED_JAR_ENTRY_TOO_LARGE"] = FAILURE(6)
    errors["NS_ERROR_SIGNED_JAR_ENTRY_INVALID"] = FAILURE(7)
    errors["NS_ERROR_SIGNED_JAR_MANIFEST_INVALID"] = FAILURE(8)


# =======================================================================
# 36: NS_ERROR_MODULE_DOM_FILESYSTEM
# =======================================================================
with modules["DOM_FILESYSTEM"]:
    errors["NS_ERROR_DOM_FILESYSTEM_INVALID_PATH_ERR"] = FAILURE(1)
    errors["NS_ERROR_DOM_FILESYSTEM_INVALID_MODIFICATION_ERR"] = FAILURE(2)
    errors["NS_ERROR_DOM_FILESYSTEM_NO_MODIFICATION_ALLOWED_ERR"] = FAILURE(3)
    errors["NS_ERROR_DOM_FILESYSTEM_PATH_EXISTS_ERR"] = FAILURE(4)
    errors["NS_ERROR_DOM_FILESYSTEM_TYPE_MISMATCH_ERR"] = FAILURE(5)
    errors["NS_ERROR_DOM_FILESYSTEM_UNKNOWN_ERR"] = FAILURE(6)


# =======================================================================
# 38: NS_ERROR_MODULE_SIGNED_APP
# =======================================================================
with modules["SIGNED_APP"]:
    errors["NS_ERROR_SIGNED_APP_MANIFEST_INVALID"] = FAILURE(1)


# =======================================================================
# 40: NS_ERROR_MODULE_DOM_PUSH
# =======================================================================
with modules["DOM_PUSH"]:
    errors["NS_ERROR_DOM_PUSH_DENIED_ERR"] = FAILURE(2)
    errors["NS_ERROR_DOM_PUSH_ABORT_ERR"] = FAILURE(3)
    errors["NS_ERROR_DOM_PUSH_SERVICE_UNREACHABLE"] = FAILURE(4)
    errors["NS_ERROR_DOM_PUSH_INVALID_KEY_ERR"] = FAILURE(5)
    errors["NS_ERROR_DOM_PUSH_MISMATCHED_KEY_ERR"] = FAILURE(6)


# =======================================================================
# 41: NS_ERROR_MODULE_DOM_MEDIA
# =======================================================================
with modules["DOM_MEDIA"]:
    # HTMLMediaElement API errors from
    # https://html.spec.whatwg.org/multipage/embedded-content.html#media-elements
    errors["NS_ERROR_DOM_MEDIA_ABORT_ERR"] = FAILURE(1)
    errors["NS_ERROR_DOM_MEDIA_NOT_ALLOWED_ERR"] = FAILURE(2)
    errors["NS_ERROR_DOM_MEDIA_NOT_SUPPORTED_ERR"] = FAILURE(3)

    # HTMLMediaElement internal decoding error
    errors["NS_ERROR_DOM_MEDIA_DECODE_ERR"] = FAILURE(4)
    errors["NS_ERROR_DOM_MEDIA_FATAL_ERR"] = FAILURE(5)
    errors["NS_ERROR_DOM_MEDIA_METADATA_ERR"] = FAILURE(6)
    errors["NS_ERROR_DOM_MEDIA_OVERFLOW_ERR"] = FAILURE(7)
    errors["NS_ERROR_DOM_MEDIA_END_OF_STREAM"] = FAILURE(8)
    errors["NS_ERROR_DOM_MEDIA_WAITING_FOR_DATA"] = FAILURE(9)
    errors["NS_ERROR_DOM_MEDIA_CANCELED"] = FAILURE(10)
    errors["NS_ERROR_DOM_MEDIA_MEDIASINK_ERR"] = FAILURE(11)
    errors["NS_ERROR_DOM_MEDIA_DEMUXER_ERR"] = FAILURE(12)
    errors["NS_ERROR_DOM_MEDIA_CDM_ERR"] = FAILURE(13)
    errors["NS_ERROR_DOM_MEDIA_NEED_NEW_DECODER"] = FAILURE(14)
    errors["NS_ERROR_DOM_MEDIA_INITIALIZING_DECODER"] = FAILURE(15)
    errors["NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_RDD_OR_GPU_ERR"] = FAILURE(16)
    errors["NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_UTILITY_ERR"] = FAILURE(17)
    errors["NS_ERROR_DOM_MEDIA_REMOTE_CRASHED_MF_CDM_ERR"] = FAILURE(18)

    # QuotaExceededError specializations
    errors["NS_ERROR_DOM_MEDIA_KEY_QUOTA_EXCEEDED_ERR"] = FAILURE(30)
    errors["NS_ERROR_DOM_MEDIA_SOURCE_MAX_BUFFER_QUOTA_EXCEEDED_ERR"] = FAILURE(31)
    errors["NS_ERROR_DOM_MEDIA_SOURCE_FULL_BUFFER_QUOTA_EXCEEDED_ERR"] = FAILURE(32)

    # Internal CDM error
    errors["NS_ERROR_DOM_MEDIA_CDM_NO_SESSION_ERR"] = FAILURE(50)
    errors["NS_ERROR_DOM_MEDIA_CDM_SESSION_OPERATION_ERR"] = FAILURE(51)
    errors["NS_ERROR_DOM_MEDIA_CDM_HDCP_NOT_SUPPORT"] = FAILURE(52)
    errors["NS_ERROR_DOM_MEDIA_CDM_NOT_FOUND_ERR"] = FAILURE(53)

    # Internal platform-related errors
    errors["NS_ERROR_DOM_MEDIA_CUBEB_INITIALIZATION_ERR"] = FAILURE(101)
    errors["NS_ERROR_DOM_MEDIA_EXTERNAL_ENGINE_NOT_SUPPORTED_ERR"] = FAILURE(102)
    errors["NS_ERROR_DOM_MEDIA_CDM_PROXY_NOT_SUPPORTED_ERR"] = FAILURE(103)
    errors["NS_ERROR_DOM_MEDIA_DENIED_IN_NON_UTILITY"] = FAILURE(104)
    errors["NS_ERROR_DOM_MEDIA_RANGE_ERR"] = FAILURE(105)
    errors["NS_ERROR_DOM_MEDIA_TYPE_ERR"] = FAILURE(106)
    errors["NS_ERROR_DOM_MEDIA_MEDIA_ENGINE_INITIALIZATION_ERR"] = FAILURE(107)
    errors["NS_ERROR_DOM_MEDIA_DROPPED_BY_ENCODER_ERR"] = FAILURE(108)

# =======================================================================
# 42: NS_ERROR_MODULE_URL_CLASSIFIER
# =======================================================================
with modules["URL_CLASSIFIER"]:
    # Errors during list updates
    errors["NS_ERROR_UC_UPDATE_UNKNOWN"] = FAILURE(1)
    errors["NS_ERROR_UC_UPDATE_DUPLICATE_PREFIX"] = FAILURE(2)
    errors["NS_ERROR_UC_UPDATE_INFINITE_LOOP"] = FAILURE(3)
    errors["NS_ERROR_UC_UPDATE_WRONG_REMOVAL_INDICES"] = FAILURE(4)
    errors["NS_ERROR_UC_UPDATE_CHECKSUM_MISMATCH"] = FAILURE(5)
    errors["NS_ERROR_UC_UPDATE_MISSING_CHECKSUM"] = FAILURE(6)
    errors["NS_ERROR_UC_UPDATE_SHUTDOWNING"] = FAILURE(7)
    errors["NS_ERROR_UC_UPDATE_TABLE_NOT_FOUND"] = FAILURE(8)
    errors["NS_ERROR_UC_UPDATE_BUILD_PREFIX_FAILURE"] = FAILURE(9)
    errors["NS_ERROR_UC_UPDATE_FAIL_TO_WRITE_DISK"] = FAILURE(10)
    errors["NS_ERROR_UC_UPDATE_UNEXPECTED_VERSION"] = FAILURE(11)

    # Specific errors while parsing pver2/pver4 responses
    errors["NS_ERROR_UC_PARSER_MISSING_PARAM"] = FAILURE(12)
    errors["NS_ERROR_UC_PARSER_DECODE_FAILURE"] = FAILURE(13)
    errors["NS_ERROR_UC_PARSER_UNKNOWN_THREAT"] = FAILURE(14)
    errors["NS_ERROR_UC_PARSER_MISSING_VALUE"] = FAILURE(15)


# =======================================================================
# 43: NS_ERROR_MODULE_ERRORRESULT
# =======================================================================
with modules["ERRORRESULT"]:
    # Represents a JS Value being thrown as an exception.
    errors["NS_ERROR_INTERNAL_ERRORRESULT_JS_EXCEPTION"] = FAILURE(1)
    # Used to indicate that we want to throw a DOMException.
    errors["NS_ERROR_INTERNAL_ERRORRESULT_DOMEXCEPTION"] = FAILURE(2)
    # Used to indicate that an exception is already pending on the JSContext.
    errors["NS_ERROR_INTERNAL_ERRORRESULT_EXCEPTION_ON_JSCONTEXT"] = FAILURE(3)
    # Used to indicate that we want to throw a TypeError.
    errors["NS_ERROR_INTERNAL_ERRORRESULT_TYPEERROR"] = FAILURE(4)
    # Used to indicate that we want to throw a RangeError.
    errors["NS_ERROR_INTERNAL_ERRORRESULT_RANGEERROR"] = FAILURE(5)


# =======================================================================
# 45: NS_ERROR_MODULE_WDBA
# =======================================================================
with modules["WDBA"]:
    errors["NS_ERROR_WDBA_NO_PROGID"] = FAILURE(1)
    errors["NS_ERROR_WDBA_HASH_CHECK"] = FAILURE(2)
    errors["NS_ERROR_WDBA_REJECTED"] = FAILURE(3)
    errors["NS_ERROR_WDBA_BUILD"] = FAILURE(4)

# =======================================================================
# 46: NS_ERROR_MODULE_DOM_QM
# =======================================================================
with modules["DOM_QM"]:
    errors["NS_ERROR_DOM_QM_CLIENT_INIT_ORIGIN_UNINITIALIZED"] = FAILURE(1)

# =======================================================================
# 47: NS_ERROR_MODULE_CLIPBOARD
# =======================================================================
with modules["CLIPBOARD"]:
    errors["NS_ERROR_CLIPBOARD_TOO_BIG"] = FAILURE(1)

# =======================================================================
# 48: NS_ERROR_MODULE_DOM_SERIAL
# =======================================================================
with modules["DOM_SERIAL"]:
    # Web Serial receive errors, surfaced on the readable stream.
    errors["NS_ERROR_DOM_SERIAL_PARITY_ERROR"] = FAILURE(1)

# =======================================================================
# 51: NS_ERROR_MODULE_GENERAL
# =======================================================================
with modules["GENERAL"]:
    # Error code used internally by the incremental downloader to cancel the
    # network channel when the download is already complete.
    errors["NS_ERROR_DOWNLOAD_COMPLETE"] = FAILURE(1)
    # Error code used internally by the incremental downloader to cancel the
    # network channel when the response to a range request is 200 instead of
    # 206.
    errors["NS_ERROR_DOWNLOAD_NOT_PARTIAL"] = FAILURE(2)
    errors["NS_ERROR_UNORM_MOREOUTPUT"] = FAILURE(33)

    errors["NS_ERROR_DOCSHELL_REQUEST_REJECTED"] = FAILURE(1001)
    # This is needed for displaying an error message when navigation is
    # attempted on a document when printing The value arbitrary as long as it
    # doesn't conflict with any of the other values in the errors in
    # DisplayLoadError
    errors["NS_ERROR_DOCUMENT_IS_PRINTMODE"] = FAILURE(2001)

    errors["NS_SUCCESS_DONT_FIXUP"] = SUCCESS(1)
    # This success code may be returned by nsIAppStartup::Run to indicate that
    # the application should be restarted.  This condition corresponds to the
    # case in which nsIAppStartup::Quit was called with the eRestart flag.
    errors["NS_SUCCESS_RESTART_APP"] = SUCCESS(1)

    # a11y
    # raised when current pivot's position is needed but it's not in the tree
    errors["NS_ERROR_NOT_IN_TREE"] = FAILURE(38)

    # see nsTextEquivUtils
    errors["NS_OK_NO_NAME_CLAUSE_HANDLED"] = SUCCESS(34)

    # Error code used to indicate that functionality has been blocked by the
    # Policy Manager
    errors["NS_ERROR_BLOCKED_BY_POLICY"] = FAILURE(3)


def import_extra_errors(infile):
    """Import extra error definitions from a json file.

    The new errors are added to the global `error` object, and also returned independently.

    Example json file (to add module):
    ```
    {
        "MAILNEWS": {
            "description": "Extra error codes for comm/mail",
            "code": 16,
            "members": {
                "NS_MSG_ERROR_MBOX_MALFORMED": {
                    "severity": "FAILURE",
                    "code": 36,
                    "description": "Mbox message doesn't start with 'From ' separator line.",
                },
            },
        }
    }
    ```
    """

    new_errors = []

    with open(infile) as f:
        data = json.load(f)
        for module_name, module_details in data.items():
            mod = modules.get(module_name)
            if mod:
                # An existing module.
                if module_details["code"] != mod.num:
                    raise ValueError(
                        f"Mismatched module code for {module_name} in {infile}"
                    )
            else:
                # Create a new module.
                mod = Mod(module_details["code"])
                modules[module_name] = mod

            with mod:
                # Add all the error codes defined for this module.
                for name, details in module_details["members"].items():
                    if name in errors:
                        raise ValueError(
                            f"Cannot redefine existing error {name} in {infile}"
                        )
                    severity = details["severity"]
                    if severity == "FAILURE":
                        errors[name] = FAILURE(details["code"])
                    elif severity == "SUCCESS":
                        errors[name] = SUCCESS(details["code"])
                    else:
                        raise ValueError(
                            f"Invalid severity value ({severity}) in {infile}"
                        )

                    new_errors.append({
                        "name": name,
                        "description": details["description"],
                    })

    return new_errors


# ============================================================================
# Write out the resulting module declarations to C++ and rust files
# ============================================================================


def error_list_h(output, *extra_errors):
    for infile in extra_errors:
        import_extra_errors(infile)

    output.write(
        """
/* THIS FILE IS GENERATED BY ErrorList.py - DO NOT EDIT */

#ifndef ErrorList_h__
#define ErrorList_h__

#include <stdint.h>

"""
    )

    output.write(f"#define NS_ERROR_MODULE_BASE_OFFSET {MODULE_BASE_OFFSET}\n")

    for mod, val in modules.items():
        output.write(f"#define NS_ERROR_MODULE_{mod} {val.num}\n")

    items = []
    for error, val in errors.items():
        items.append(f"  {error} = 0x{val:X}")
    output.write(
        """
enum class nsresult : uint32_t
{{
{}
}};

""".format(",\n".join(items))
    )

    items = []
    for error, val in errors.items():
        items.append(f"  {error} = nsresult::{error}")

    output.write(
        """
const nsresult
{}
;

#endif // ErrorList_h__
""".format(",\n".join(items))
    )


def error_names_internal_h(output, *extra_errors):
    """Generate ErrorNamesInternal.h, which is a header file declaring one
    function, const char* GetErrorNameInternal(nsresult). This method is not
    intended to be used by consumer code, which should instead call
    GetErrorName in ErrorNames.h."""

    for infile in extra_errors:
        import_extra_errors(infile)

    output.write(
        """
/* THIS FILE IS GENERATED BY ErrorList.py - DO NOT EDIT */

#ifndef ErrorNamesInternal_h__
#define ErrorNamesInternal_h__

#include "ErrorNames.h"

namespace {

const char*
GetErrorNameInternal(nsresult rv)
{
  switch (rv) {
"""
    )

    # NOTE: Making sure we don't write out duplicate values is important as
    # we're using a switch statement to implement this.
    seen = set()
    for error, val in errors.items():
        if val not in seen:
            output.write(f'  case nsresult::{error}: return "{error}";\n')
        seen.add(val)

    output.write(
        """
  default: return nullptr;
  }
} // GetErrorNameInternal

} // namespace

#endif // ErrorNamesInternal_h__
"""
    )


def error_list_rs(output, *extra_errors):
    for infile in extra_errors:
        import_extra_errors(infile)

    output.write(
        """
/* THIS FILE IS GENERATED BY ErrorList.py - DO NOT EDIT */

use super::nsresult;

"""
    )

    output.write(
        f"pub const NS_ERROR_MODULE_BASE_OFFSET: nsresult = nsresult({MODULE_BASE_OFFSET});\n"
    )

    for mod, val in modules.items():
        output.write(
            f"pub const NS_ERROR_MODULE_{mod}: nsresult = nsresult({val.num});\n"
        )

    for error, val in errors.items():
        output.write(f"pub const {error}: nsresult = nsresult(0x{val:X});\n")


def gen_jinja(output, input_filename):
    # This is used to generate Java code for error lists, and can be expanded to
    # other required contexts in the future if desired.
    import os

    from jinja2 import Environment, FileSystemLoader, StrictUndefined

    # FileSystemLoader requires the path to the directory containing templates,
    # not the file name of the template itself.
    (path, leaf) = os.path.split(input_filename)
    env = Environment(
        loader=FileSystemLoader(path, encoding="utf-8"),
        undefined=StrictUndefined,
    )
    tpl = env.get_template(leaf)

    context = {
        "MODULE_BASE_OFFSET": MODULE_BASE_OFFSET,
        "modules": ((mod, val.num) for mod, val in modules.items()),
        "errors": errors.items(),
    }

    tpl.stream(context).dump(output, encoding="utf-8")


def error_list_json(output, *extra_errors):
    """
    Export error_list.json, used by tools/ts to build lib.gecko.nsresult.d.ts.
    """
    for infile in extra_errors:
        import_extra_errors(infile)

    json.dump(errors, output, indent=2)
    output.write("\n")


def extra_xpc_msg(output, *extra_errors):
    """
    Generates `extra_xpc_msg.h`, which is used by XPCException.cpp to generate
    the `Cr` object used by JavaScript code.

    This file contains only the extra codes included in the files referred to by
    `extra_errors`, as the common error codes are already defined in
    `js/xpconnect/src/xpc.msg`.
    """

    output.write("/* THIS FILE IS GENERATED BY ErrorList.py - DO NOT EDIT */\n")

    new_errors = []

    for infile in extra_errors:
        new_imported = import_extra_errors(infile)
        new_errors = new_imported + new_errors

    for error in new_errors:
        output.write(f'XPC_MSG_DEF({error["name"]}, "{error["description"]}")\n')
