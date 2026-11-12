# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import ipaddr
import socket
import hmac
import hashlib
import passlib.utils  # for saslprep
import copy
import random
import operator
import os
import platform
import six
import string
import time
from functools import reduce
from string import Template
from twisted.internet import reactor, protocol
from twisted.internet.task import LoopingCall
from twisted.internet.address import IPv4Address
from twisted.internet.address import IPv6Address

MAGIC_COOKIE = 0x2112A442

REQUEST = 0
INDICATION = 1
SUCCESS_RESPONSE = 2
ERROR_RESPONSE = 3

BINDING = 0x001
ALLOCATE = 0x003
REFRESH = 0x004
SEND = 0x006
DATA_MSG = 0x007
CREATE_PERMISSION = 0x008
CHANNEL_BIND = 0x009

# STUN spec chose silly values for these
STUN_IPV4 = 1
STUN_IPV6 = 2

MAPPED_ADDRESS = 0x0001
USERNAME = 0x0006
MESSAGE_INTEGRITY = 0x0008
ERROR_CODE = 0x0009
UNKNOWN_ATTRIBUTES = 0x000A
LIFETIME = 0x000D
DATA_ATTR = 0x0013
XOR_PEER_ADDRESS = 0x0012
REALM = 0x0014
NONCE = 0x0015
XOR_RELAYED_ADDRESS = 0x0016
REQUESTED_TRANSPORT = 0x0019
DONT_FRAGMENT = 0x001A
XOR_MAPPED_ADDRESS = 0x0020
SOFTWARE = 0x8022
ALTERNATE_SERVER = 0x8023
FINGERPRINT = 0x8028

STUN_PORT = 3478
STUNS_PORT = 5349

TURN_REDIRECT_PORT = 3479
TURNS_REDIRECT_PORT = 5350
TURN_WRONG_RELAYED_ADDR_PORT = 3480


def unpack_uint(bytes_buf):
    result = 0
    for byte in bytes_buf:
        result = (result << 8) + byte
    return result


def pack_uint(value, width):
    if value < 0:
        raise ValueError("Invalid value: {}".format(value))
    buf = bytearray([0] * width)
    for i in range(0, width):
        buf[i] = (value >> (8 * (width - i - 1))) & 0xFF

    return buf


def unpack(bytes_buf, format_array):
    results = ()
    for width in format_array:
        results = results + (unpack_uint(bytes_buf[0:width]),)
        bytes_buf = bytes_buf[width:]
    return results


def pack(values, format_array):
    if len(values) != len(format_array):
        raise ValueError()
    buf = bytearray()
    for i in range(0, len(values)):
        buf.extend(pack_uint(values[i], format_array[i]))
    return buf


def bitwise_pack(source, dest, start_bit, num_bits):
    if num_bits <= 0 or num_bits > start_bit + 1:
        raise ValueError(
            "Invalid num_bits: {}, start_bit = {}".format(num_bits, start_bit)
        )
    last_bit = start_bit - num_bits + 1
    source = source >> last_bit
    dest = dest << num_bits
    mask = (1 << num_bits) - 1
    dest += source & mask
    return dest


def to_ipaddress(protocol, host, port):
    if ":" not in host:
        return IPv4Address(protocol, host, port)

    return IPv6Address(protocol, host, port)


class StunAttribute(object):
    """
    Represents a STUN attribute in a raw format, according to the following:

     0                   1                   2                   3
     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |   StunAttribute.attr_type     |  Length (derived as needed)   |
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |           StunAttribute.data (variable length)             ....
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    """

    __attr_header_fmt = [2, 2]
    __attr_header_size = reduce(operator.add, __attr_header_fmt)

    def __init__(self, attr_type=0, buf=bytearray()):
        self.attr_type = attr_type
        self.data = buf

    def build(self):
        buf = pack((self.attr_type, len(self.data)), self.__attr_header_fmt)
        buf.extend(self.data)
        # add padding if necessary
        if len(buf) % 4:
            buf.extend([0] * (4 - (len(buf) % 4)))
        return buf

    def parse(self, buf):
        if self.__attr_header_size > len(buf):
            raise Exception("truncated at attribute: incomplete header")

        self.attr_type, length = unpack(buf, self.__attr_header_fmt)
        length += self.__attr_header_size

        if length > len(buf):
            raise Exception("truncated at attribute: incomplete contents")

        self.data = buf[self.__attr_header_size : length]

        # verify padding
        while length % 4:
            if buf[length]:
                raise ValueError("Non-zero padding")
            length += 1

        return length


class StunMessage(object):
    """
    Represents a STUN message. Contains a method, msg_class, cookie,
    transaction_id, and attributes (as an array of StunAttribute).

    Has various functions for getting/adding attributes.
    """

    def __init__(self):
        self.method = 0
        self.msg_class = 0
        self.cookie = MAGIC_COOKIE
        self.transaction_id = 0
        self.attributes = []

    #      0                   1                   2                   3
    #      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    #     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    #     |0 0|M M M M M|C|M M M|C|M M M M|         Message Length        |
    #     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    #     |                         Magic Cookie                          |
    #     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    #     |                                                               |
    #     |                     Transaction ID (96 bits)                  |
    #     |                                                               |
    #     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    __header_fmt = [2, 2, 4, 12]
    __header_size = reduce(operator.add, __header_fmt)

    # Returns how many bytes were parsed if buf was large enough, or how many
    # bytes we would have needed if not. Throws if buf is malformed.
    def parse(self, buf):
        min_buf_size = self.__header_size
        if len(buf) < min_buf_size:
            return min_buf_size

        message_type, length, cookie, self.transaction_id = unpack(
            buf, self.__header_fmt
        )
        min_buf_size += length
        if len(buf) < min_buf_size:
            return min_buf_size

        # Avert your eyes...
        self.method = bitwise_pack(message_type, 0, 13, 5)
        self.msg_class = bitwise_pack(message_type, 0, 8, 1)
        self.method = bitwise_pack(message_type, self.method, 7, 3)
        self.msg_class = bitwise_pack(message_type, self.msg_class, 4, 1)
        self.method = bitwise_pack(message_type, self.method, 3, 4)

        if cookie != self.cookie:
            raise Exception("Invalid cookie: {}".format(cookie))

        buf = buf[self.__header_size : min_buf_size]
        while len(buf):
            attr = StunAttribute()
            length = attr.parse(buf)
            buf = buf[length:]
            self.attributes.append(attr)

        return min_buf_size

    # stop_after_attr_type is useful for calculating MESSAGE-DIGEST
    def build(self, stop_after_attr_type=0):
        attrs = bytearray()
        for attr in self.attributes:
            attrs.extend(attr.build())
            if attr.attr_type == stop_after_attr_type:
                break

        message_type = bitwise_pack(self.method, 0, 11, 5)
        message_type = bitwise_pack(self.msg_class, message_type, 1, 1)
        message_type = bitwise_pack(self.method, message_type, 6, 3)
        message_type = bitwise_pack(self.msg_class, message_type, 0, 1)
        message_type = bitwise_pack(self.method, message_type, 3, 4)

        message = pack(
            (message_type, len(attrs), self.cookie, self.transaction_id),
            self.__header_fmt,
        )
        message.extend(attrs)

        return message

    def add_error_code(self, code, phrase=None):
        #      0                   1                   2                   3
        #      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        #     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        #     |           Reserved, should be 0         |Class|     Number    |
        #     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        #     |      Reason Phrase (variable)                                ..
        #     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        error_code_fmt = [3, 1]
        error_code = pack((code // 100, code % 100), error_code_fmt)
        if phrase != None:
            error_code.extend(bytearray(phrase, "utf-8"))
        self.attributes.append(StunAttribute(ERROR_CODE, error_code))

    #     0                   1                   2                   3
    #     0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    #    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    #    |x x x x x x x x|    Family     |         X-Port                |
    #    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    #    |                X-Address (Variable)
    #    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    __v4addr_fmt = [1, 1, 2, 4]
    __v6addr_fmt = [1, 1, 2, 16]
    __v4addr_size = reduce(operator.add, __v4addr_fmt)
    __v6addr_size = reduce(operator.add, __v6addr_fmt)

    def add_address(self, ip_address, version, port, attr_type):
        if version == STUN_IPV4:
            address = pack((0, STUN_IPV4, port, ip_address), self.__v4addr_fmt)
        elif version == STUN_IPV6:
            address = pack((0, STUN_IPV6, port, ip_address), self.__v6addr_fmt)
        else:
            raise ValueError("Invalid ip version: {}".format(version))
        self.attributes.append(StunAttribute(attr_type, address))

    def get_xaddr(self, ip_addr, version):
        if version == STUN_IPV4:
            return self.cookie ^ ip_addr
        elif version == STUN_IPV6:
            return ((self.cookie << 96) + self.transaction_id) ^ ip_addr
        else:
            raise ValueError("Invalid family: {}".format(version))

    def get_xport(self, port):
        return (self.cookie >> 16) ^ port

    def add_xor_address(self, addr_port, attr_type):
        ip_address = ipaddr.IPAddress(addr_port.host)
        version = STUN_IPV6 if ip_address.version == 6 else STUN_IPV4
        xaddr = self.get_xaddr(int(ip_address), version)
        xport = self.get_xport(addr_port.port)
        self.add_address(xaddr, version, xport, attr_type)

    def add_data(self, buf):
        self.attributes.append(StunAttribute(DATA_ATTR, buf))

    def find(self, attr_type):
        for attr in self.attributes:
            if attr.attr_type == attr_type:
                return attr
        return None

    def get_xor_address(self, attr_type):
        addr_attr = self.find(attr_type)
        if not addr_attr:
            return None

        padding, family, xport, xaddr = unpack(addr_attr.data, self.__v4addr_fmt)
        addr_ctor = IPv4Address
        if family == STUN_IPV6:
            padding, family, xport, xaddr = unpack(addr_attr.data, self.__v6addr_fmt)
            addr_ctor = IPv6Address
        elif family != STUN_IPV4:
            raise ValueError("Invalid family: {}".format(family))

        return addr_ctor(
            "UDP",
            str(ipaddr.IPAddress(self.get_xaddr(xaddr, family))),
            self.get_xport(xport),
        )

    def add_nonce(self, nonce):
        self.attributes.append(StunAttribute(NONCE, bytearray(nonce, "utf-8")))

    def add_realm(self, realm):
        self.attributes.append(StunAttribute(REALM, bytearray(realm, "utf-8")))

    def calculate_message_digest(self, username, realm, password):
        digest_buf = self.build(MESSAGE_INTEGRITY)
        # Trim off the MESSAGE-INTEGRITY attr
        digest_buf = digest_buf[: len(digest_buf) - 24]
        password = passlib.utils.saslprep(six.text_type(password))
        key_string = "{}:{}:{}".format(username, realm, password)
        md5 = hashlib.md5()
        md5.update(bytearray(key_string, "utf-8"))
        key = md5.digest()
        return bytearray(hmac.new(key, digest_buf, hashlib.sha1).digest())

    def add_lifetime(self, lifetime):
        self.attributes.append(StunAttribute(LIFETIME, pack_uint(lifetime, 4)))

    def get_lifetime(self):
        lifetime_attr = self.find(LIFETIME)
        if not lifetime_attr:
            return None
        return unpack_uint(lifetime_attr.data[0:4])

    def get_username(self):
        username = self.find(USERNAME)
        if not username:
            return None
        return str(username.data)

    def add_message_integrity(self, username, realm, password):
        dummy_value = bytearray([0] * 20)
        self.attributes.append(StunAttribute(MESSAGE_INTEGRITY, dummy_value))
        digest = self.calculate_message_digest(username, realm, password)
        self.find(MESSAGE_INTEGRITY).data = digest

    def add_alternate_server(self, host, port):
        address = ipaddr.IPAddress(host)
        version = STUN_IPV6 if address.version == 6 else STUN_IPV4
        self.add_address(int(address), version, port, ALTERNATE_SERVER)


class Allocation(protocol.DatagramProtocol):
    """
    Comprises the socket for a TURN allocation, a back-reference to the
    transport we will forward received traffic on, the allocator's address and
    username, the set of permissions for the allocation, and the allocation's
    expiry.
    """

    def __init__(self, other_transport_handler, allocator_address, username):
        self.permissions = set()  # str, int tuples
        # Handler to use when sending stuff that arrives on the allocation
        self.other_transport_handler = other_transport_handler
        self.allocator_address = allocator_address
        self.username = username
        self.expiry = time.time()
        self.port = reactor.listenUDP(0, self, interface=v4_address)

    def datagramReceived(self, data, address):
        host = address[0]
        port = address[1]
        if not host in self.permissions:
            print(
                "Dropping packet from {}:{}, no permission on allocation {}".format(
                    host, port, self.transport.getHost()
                )
            )
            return

        data_indication = StunMessage()
        data_indication.method = DATA_MSG
        data_indication.msg_class = INDICATION
        data_indication.transaction_id = random.getrandbits(96)

        # Only handles UDP allocations. Doubtful that we need more than this.
        data_indication.add_xor_address(
            to_ipaddress("UDP", host, port), XOR_PEER_ADDRESS
        )
        data_indication.add_data(data)

        self.other_transport_handler.write(
            data_indication.build(), self.allocator_address
        )

    def close(self):
        self.port.stopListening()
        self.port = None


class StunHandler(object):
    """
    Frames and handles STUN messages. This is the core logic of the TURN
    server, along with Allocation.
    """

    def __init__(self, transport_handler):
        self.client_address = None
        self.data = bytearray()
        self.transport_handler = transport_handler

    def data_received(self, data, address):
        self.data += data
        while True:
            stun_message = StunMessage()
            parsed_len = stun_message.parse(self.data)
            if parsed_len > len(self.data):
                break
            self.data = self.data[parsed_len:]

            response = self.handle_stun(stun_message, address)
            if response:
                self.transport_handler.write(response, address)

    def handle_stun(self, stun_message, address):
        self.client_address = address
        if stun_message.msg_class == INDICATION:
            if stun_message.method == SEND:
                self.handle_send_indication(stun_message)
            else:
                print(
                    "Dropping unknown indication method: {}".format(stun_message.method)
                )
            return None

        if stun_message.msg_class != REQUEST:
            print("Dropping STUN response, method: {}".format(stun_message.method))
            return None

        if stun_message.method == BINDING:
            return self.make_success_response(stun_message).build()
        elif stun_message.method == ALLOCATE:
            return self.handle_allocation(stun_message).build()
        elif stun_message.method == REFRESH:
            return self.handle_refresh(stun_message).build()
        elif stun_message.method == CREATE_PERMISSION:
            return self.handle_permission(stun_message).build()
        else:
            return self.make_error_response(
                stun_message,
                400,
                ("Unsupported STUN request, method: {}".format(stun_message.method)),
            ).build()

    def get_allocation_tuple(self):
        return (
            self.client_address.host,
            self.client_address.port,
            self.transport_handler.transport.getHost().type,
            self.transport_handler.transport.getHost().host,
            self.transport_handler.transport.getHost().port,
        )

    def get_relayed_address(self, allocation):
        return allocation.transport.getHost()

    def handle_allocation(self, request):
        allocate_response = self.check_long_term_auth(request)
        if allocate_response.msg_class == SUCCESS_RESPONSE:
            if self.get_allocation_tuple() in allocations:
                return self.make_error_response(
                    request,
                    437,
                    (
                        "Duplicate allocation request for tuple {}".format(
                            self.get_allocation_tuple()
                        )
                    ),
                )

            allocation = Allocation(
                self.transport_handler, self.client_address, request.get_username()
            )

            allocate_response.add_xor_address(
                self.get_relayed_address(allocation), XOR_RELAYED_ADDRESS
            )

            lifetime = request.get_lifetime()
            if lifetime == None:
                return self.make_error_response(
                    request, 400, "Missing lifetime attribute in allocation request"
                )

            lifetime = min(lifetime, 3600)
            allocate_response.add_lifetime(lifetime)
            allocation.expiry = time.time() + lifetime

            allocate_response.add_message_integrity(turn_user, turn_realm, turn_pass)
            allocations[self.get_allocation_tuple()] = allocation
            print("Allocation created for {}".format(self.get_allocation_tuple()))
        return allocate_response

    def handle_refresh(self, request):
        refresh_response = self.check_long_term_auth(request)
        if refresh_response.msg_class == SUCCESS_RESPONSE:
            try:
                allocation = allocations[self.get_allocation_tuple()]
            except KeyError:
                return self.make_error_response(
                    request,
                    437,
                    (
                        "Refresh request for non-existing allocation, tuple {}".format(
                            self.get_allocation_tuple()
                        )
                    ),
                )

            if allocation.username != request.get_username():
                return self.make_error_response(
                    request,
                    441,
                    (
                        "Refresh request with wrong user, exp {}, got {}".format(
                            allocation.username, request.get_username()
                        )
                    ),
                )

            lifetime = request.get_lifetime()
            if lifetime == None:
                return self.make_error_response(
                    request, 400, "Missing lifetime attribute in allocation request"
                )

            lifetime = min(lifetime, 3600)
            refresh_response.add_lifetime(lifetime)
            allocation.expiry = time.time() + lifetime

            refresh_response.add_message_integrity(turn_user, turn_realm, turn_pass)
        return refresh_response

    def handle_permission(self, request):
        permission_response = self.check_long_term_auth(request)
        if permission_response.msg_class == SUCCESS_RESPONSE:
            try:
                allocation = allocations[self.get_allocation_tuple()]
            except KeyError:
                return self.make_error_response(
                    request,
                    437,
                    (
                        "No such allocation for permission request, tuple {}".format(
                            self.get_allocation_tuple()
                        )
                    ),
                )

            if allocation.username != request.get_username():
                return self.make_error_response(
                    request,
                    441,
                    (
                        "Permission request with wrong user, exp {}, got {}".format(
                            allocation.username, request.get_username()
                        )
                    ),
                )

            # TODO: Handle multiple XOR-PEER-ADDRESS
            peer_address = request.get_xor_address(XOR_PEER_ADDRESS)
            if not peer_address:
                return self.make_error_response(
                    request, 400, "Missing XOR-PEER-ADDRESS on permission request"
                )

            permission_response.add_message_integrity(turn_user, turn_realm, turn_pass)
            allocation.permissions.add(peer_address.host)

        return permission_response

    def handle_send_indication(self, indication):
        try:
            allocation = allocations[self.get_allocation_tuple()]
        except KeyError:
            print(
                "Dropping send indication; no allocation for tuple {}".format(
                    self.get_allocation_tuple()
                )
            )
            return

        peer_address = indication.get_xor_address(XOR_PEER_ADDRESS)
        if not peer_address:
            print("Dropping send indication, missing XOR-PEER-ADDRESS")
            return

        data_attr = indication.find(DATA_ATTR)
        if not data_attr:
            print("Dropping send indication, missing DATA")
            return

        if indication.find(DONT_FRAGMENT):
            print("Dropping send indication, DONT-FRAGMENT set")
            return

        if not peer_address.host in allocation.permissions:
            print(
                "Dropping send indication, no permission for {} on tuple {}".format(
                    peer_address.host, self.get_allocation_tuple()
                )
            )
            return

        allocation.transport.write(
            data_attr.data, (peer_address.host, peer_address.port)
        )

    def make_success_response(self, request):
        response = copy.deepcopy(request)
        response.attributes = []
        response.add_xor_address(self.client_address, XOR_MAPPED_ADDRESS)
        response.msg_class = SUCCESS_RESPONSE
        return response

    def make_error_response(self, request, code, reason=None):
        if reason:
            print("{}: rejecting with {}".format(reason, code))
        response = copy.deepcopy(request)
        response.attributes = []
        response.add_error_code(code, reason)
        response.msg_class = ERROR_RESPONSE
        return response

    def make_challenge_response(self, request, reason=None):
        response = self.make_error_response(request, 401, reason)
        # 65 means the hex encoding will need padding half the time
        response.add_nonce("{:x}".format(random.getrandbits(65)))
        response.add_realm(turn_realm)
        return response

    def check_long_term_auth(self, request):
        message_integrity = request.find(MESSAGE_INTEGRITY)
        if not message_integrity:
            return self.make_challenge_response(request)

        username = request.find(USERNAME)
        realm = request.find(REALM)
        nonce = request.find(NONCE)
        if not username or not realm or not nonce:
            return self.make_error_response(
                request, 400, "Missing either USERNAME, NONCE, or REALM"
            )

        if username.data.decode("utf-8") != turn_user:
            return self.make_challenge_response(
                request, "Wrong user {}, exp {}".format(username.data, turn_user)
            )

        expected_message_digest = request.calculate_message_digest(
            turn_user, turn_realm, turn_pass
        )
        if message_integrity.data != expected_message_digest:
            return self.make_challenge_response(request, "Incorrect message disgest")

        return self.make_success_response(request)


class StunRedirectHandler(StunHandler):
    """
    Frames and handles STUN messages by redirecting to the "real" server port.
    Performs the redirect with auth, so does a 401 to unauthed requests.
    Can be used to test port-based redirect handling.
    """

    def handle_stun(self, stun_message, address):
        self.client_address = address
        if stun_message.msg_class == REQUEST:
            challenge_response = self.check_long_term_auth(stun_message)

            if challenge_response.msg_class == SUCCESS_RESPONSE:
                return self.make_redirect_response(stun_message).build()

            return challenge_response.build()

    def make_redirect_response(self, request):
        response = self.make_error_response(request, 300, "Try alternate")
        port = STUN_PORT
        if self.transport_handler.transport.getHost().port == TURNS_REDIRECT_PORT:
            port = STUNS_PORT

        response.add_alternate_server(
            self.transport_handler.transport.getHost().host, port
        )

        response.add_message_integrity(turn_user, turn_realm, turn_pass)
        return response


class UdpStunHandler(protocol.DatagramProtocol):
    """
    Represents a UDP listen port for TURN.
    """

    def datagramReceived(self, data, address):
        stun_handler = StunHandler(self)
        stun_handler.data_received(data, to_ipaddress("UDP", address[0], address[1]))

    def write(self, data, address):
        self.transport.write(bytes(data), (address.host, address.port))


class UdpStunRedirectHandler(protocol.DatagramProtocol):
    """
    Represents a UDP listen port for TURN that will redirect.
    """

    def datagramReceived(self, data, address):
        stun_handler = StunRedirectHandler(self)
        stun_handler.data_received(data, to_ipaddress("UDP", address[0], address[1]))

    def write(self, data, address):
        self.transport.write(bytes(data), (address.host, address.port))


class TcpStunHandlerFactory(protocol.Factory):
    """
    Represents a TCP listen port for TURN.
    """

    def buildProtocol(self, addr):
        return TcpStunHandler(addr)


class TcpStunHandlerBase(protocol.Protocol):
    def __init__(self, addr):
        self.address = addr
        self.stun_handler = None

    def connectionLost(self, reason):
        print("Lost connection from {}".format(self.address))
        # Destroy allocations that this connection made
        keys_to_delete = []
        for key, allocation in allocations.items():
            if allocation.other_transport_handler == self:
                print("Closing allocation due to dropped connection: {}".format(key))
                keys_to_delete.append(key)
                allocation.close()

        for key in keys_to_delete:
            del allocations[key]

    def write(self, data, address):
        self.transport.write(bytes(data))

    def makeStunHandler(self):
        raise NotImplementedError("Must implement makeStunHandler!")

    def dataReceived(self, data):
        # This needs to persist, since it handles framing. Framing matters here
        # because we do a round of auth before redirecting.
        if not self.stun_handler:
            self.stun_handler = self.makeStunHandler()
        self.stun_handler.data_received(data, self.address)


class TcpStunHandler(TcpStunHandlerBase):
    """
    Represents a connected TCP port for TURN.
    """

    def makeStunHandler(self):
        return StunHandler(self)


class TcpStunRedirectHandlerFactory(protocol.Factory):
    """
    Represents a TCP listen port for TURN that will redirect.
    """

    def buildProtocol(self, addr):
        return TcpStunRedirectHandler(addr)


class TcpStunRedirectHandler(TcpStunHandlerBase):
    def makeStunHandler(self):
        return StunRedirectHandler(self)


class StunWrongRelayedAddrHandler(StunHandler):
    """
    Lies about XOR-RELAYED-ADDRESS in allocate responses, to simulate address
    translation applied on the TURN server's relayed address, causing the
    remote peer to see ICE checks arriving from somewhere other than the
    relayed address. The allocation's real socket is still used for relay
    traffic, so the relay path works end-to-end; only the advertised address is
    wrong. TEST-NET-1 (RFC 5737) is used so nothing tries to route to it.
    """

    def get_relayed_address(self, allocation):
        real = allocation.transport.getHost()
        return to_ipaddress(real.type, "192.0.2.1", real.port)


class UdpStunWrongRelayedAddrHandler(protocol.DatagramProtocol):
    """
    Represents a UDP listen port for TURN that lies about
    XOR-RELAYED-ADDRESS in allocate responses.
    """

    def datagramReceived(self, data, address):
        stun_handler = StunWrongRelayedAddrHandler(self)
        stun_handler.data_received(data, to_ipaddress("UDP", address[0], address[1]))

    def write(self, data, address):
        self.transport.write(bytes(data), (address.host, address.port))


class TcpStunWrongRelayedAddrHandlerFactory(protocol.Factory):
    """
    Represents a TCP listen port for TURN that lies about
    XOR-RELAYED-ADDRESS in allocate responses.
    """

    def buildProtocol(self, addr):
        return TcpStunWrongRelayedAddrHandler(addr)


class TcpStunWrongRelayedAddrHandler(TcpStunHandlerBase):
    def makeStunHandler(self):
        return StunWrongRelayedAddrHandler(self)


def get_default_route(family):
    dummy_socket = socket.socket(family, socket.SOCK_DGRAM)
    if family is socket.AF_INET:
        dummy_socket.connect(("8.8.8.8", 53))
    else:
        dummy_socket.connect(("2001:4860:4860::8888", 53))

    default_route = dummy_socket.getsockname()[0]
    dummy_socket.close()
    return default_route


turn_user = "foo"
turn_pass = "bar"
turn_realm = "mozilla.invalid"
allocations = {}
v4_address = get_default_route(socket.AF_INET)
try:
    v6_address = get_default_route(socket.AF_INET6)
except:
    v6_address = ""


def prune_allocations():
    now = time.time()
    keys_to_delete = []
    for key, allocation in allocations.items():
        if allocation.expiry < now:
            print("Allocation expired: {}".format(key))
            keys_to_delete.append(key)
            allocation.close()

    for key in keys_to_delete:
        del allocations[key]


CERT_FILE = "selfsigned.crt"
KEY_FILE = "private.key"


def create_self_signed_cert(name):
    # pyOpenSSL used to have some wrappers to help with this, but those have
    # been deprecated, and they have instructed users to use stuff from
    # cryptography.hazmat directly. This strikes me as a bad idea, but here we
    # go...

    from cryptography.hazmat.primitives.asymmetric import rsa
    from cryptography import x509
    from cryptography.x509.oid import NameOID
    from cryptography.hazmat.primitives import hashes
    import datetime
    from cryptography.hazmat.primitives import serialization

    # Not ideal, but in order to avoid generating certs with duplicate serial
    # numbers, we don't regenerate if there's one there already. If we wanted
    # to regenerate, we'd need to load the cert if it was there, determine its
    # serial number, and then make a new cert with a higher serial number.
    if os.path.isfile(CERT_FILE) and os.path.isfile(KEY_FILE):
        return

    # Key size does not need to be big, this is a self-signed cert for testing,
    # but I'm going to use something common to avoid warnings that might come
    # up in the future.
    # Why 65537? Because the documentation says so, citing a document written
    # by Colin Percival in 2009. Will this ever be out of date? Is it out of
    # date already? Who knows!
    key = rsa.generate_private_key(key_size=2048, public_exponent=65537)

    subject = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "US"),
        x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME, "TX"),
        x509.NameAttribute(NameOID.LOCALITY_NAME, "Dallas"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Mozilla test iceserver"),
        x509.NameAttribute(NameOID.COMMON_NAME, name),
    ])

    # create a self-signed cert
    cert = (
        x509
        .CertificateBuilder()
        .subject_name(subject)
        .issuer_name(subject)
        .serial_number(1000)
        .not_valid_before(datetime.datetime.now(datetime.timezone.utc))
        .not_valid_after(
            datetime.datetime.now(datetime.timezone.utc) + datetime.timedelta(days=365)
        )
        .public_key(key.public_key())
        .add_extension(
            x509.SubjectAlternativeName([x509.DNSName(name)]),
            critical=False,
        )
        .sign(key, hashes.SHA256())
    )

    open(CERT_FILE, "wb").write(cert.public_bytes(encoding=serialization.Encoding.PEM))
    open(KEY_FILE, "wb").write(
        key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption(),
        )
    )


if __name__ == "__main__":
    random.seed()

    if platform.system() == "Windows":
        # Windows is finicky about allowing real interfaces to talk to loopback.
        interface_4 = v4_address
        interface_6 = v6_address
        hostname = socket.gethostname()
    else:
        # Our linux builders do not have a hostname that resolves to the real
        # interface.
        interface_4 = "127.0.0.1"
        interface_6 = "::1"
        hostname = "localhost"

    reactor.listenUDP(STUN_PORT, UdpStunHandler(), interface=interface_4)
    reactor.listenTCP(STUN_PORT, TcpStunHandlerFactory(), interface=interface_4)

    reactor.listenUDP(
        TURN_REDIRECT_PORT, UdpStunRedirectHandler(), interface=interface_4
    )
    reactor.listenTCP(
        TURN_REDIRECT_PORT, TcpStunRedirectHandlerFactory(), interface=interface_4
    )

    reactor.listenUDP(
        TURN_WRONG_RELAYED_ADDR_PORT,
        UdpStunWrongRelayedAddrHandler(),
        interface=interface_4,
    )
    reactor.listenTCP(
        TURN_WRONG_RELAYED_ADDR_PORT,
        TcpStunWrongRelayedAddrHandlerFactory(),
        interface=interface_4,
    )

    try:
        reactor.listenUDP(STUN_PORT, UdpStunHandler(), interface=interface_6)
        reactor.listenTCP(STUN_PORT, TcpStunHandlerFactory(), interface=interface_6)

        reactor.listenUDP(
            TURN_REDIRECT_PORT, UdpStunRedirectHandler(), interface=interface_6
        )
        reactor.listenTCP(
            TURN_REDIRECT_PORT, TcpStunRedirectHandlerFactory(), interface=interface_6
        )

        reactor.listenUDP(
            TURN_WRONG_RELAYED_ADDR_PORT,
            UdpStunWrongRelayedAddrHandler(),
            interface=interface_6,
        )
        reactor.listenTCP(
            TURN_WRONG_RELAYED_ADDR_PORT,
            TcpStunWrongRelayedAddrHandlerFactory(),
            interface=interface_6,
        )
    except:
        pass

    try:
        from twisted.internet import ssl
        from OpenSSL import SSL

        create_self_signed_cert(hostname)
        tls_context_factory = ssl.DefaultOpenSSLContextFactory(
            KEY_FILE, CERT_FILE, SSL.TLSv1_2_METHOD
        )
        reactor.listenSSL(
            STUNS_PORT,
            TcpStunHandlerFactory(),
            tls_context_factory,
            interface=interface_4,
        )

        try:
            reactor.listenSSL(
                STUNS_PORT,
                TcpStunHandlerFactory(),
                tls_context_factory,
                interface=interface_6,
            )

            reactor.listenSSL(
                TURNS_REDIRECT_PORT,
                TcpStunRedirectHandlerFactory(),
                tls_context_factory,
                interface=interface_6,
            )
        except:
            pass

        f = open(CERT_FILE, "r")
        lines = f.readlines()
        lines.pop(0)  # Remove BEGIN CERTIFICATE
        lines.pop()  # Remove END CERTIFICATE
        # pylint --py3k: W1636 W1649
        lines = list(map(str.strip, lines))
        certbase64 = "".join(lines)  # pylint --py3k: W1649

        turns_url = ', "turns:' + hostname + '"'
        cert_prop = ', "cert":"' + certbase64 + '"'
    except:
        turns_url = ""
        cert_prop = ""
        pass

    allocation_pruner = LoopingCall(prune_allocations)
    allocation_pruner.start(1)

    template = Template(
        '[\
{"urls":["stun:$hostname"]}, \
{"username":"$user","credential":"$pwd", \
"urls": ["turn:$hostname", "turn:$hostname?transport=tcp" $turns_url], \
"turn_redirect_port":"$TURN_REDIRECT_PORT","turns_redirect_port":"$TURNS_REDIRECT_PORT", \
"turn_wrong_relayed_addr_port":"$TURN_WRONG_RELAYED_ADDR_PORT" \
$cert_prop}]'  # Hack to make it easier to override cert checks
    )

    print(
        template.substitute(
            user=turn_user,
            pwd=turn_pass,
            hostname=hostname,
            turns_url=turns_url,
            cert_prop=cert_prop,
            TURN_REDIRECT_PORT=TURN_REDIRECT_PORT,
            TURNS_REDIRECT_PORT=TURNS_REDIRECT_PORT,
            TURN_WRONG_RELAYED_ADDR_PORT=TURN_WRONG_RELAYED_ADDR_PORT,
        )
    )

    reactor.run()
