'use strict'

const types = require('./types')
const rcodes = require('./rcodes')
exports.rcodes = rcodes;
const opcodes = require('./opcodes')
const classes = require('./classes')
const optioncodes = require('./optioncodes')
const ip = require('../node_ip')

const QUERY_FLAG = 0
const RESPONSE_FLAG = 1 << 15
const FLUSH_MASK = 1 << 15
const NOT_FLUSH_MASK = ~FLUSH_MASK
const QU_MASK = 1 << 15
const NOT_QU_MASK = ~QU_MASK

const name = exports.txt = exports.name = {}

name.encode = function (str, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(name.encodingLength(str))
  if (!offset) offset = 0
  const oldOffset = offset

  // strip leading and trailing .
  const n = str.replace(/^\.|\.$/gm, '')
  if (n.length) {
    const list = n.split('.')

    for (let i = 0; i < list.length; i++) {
      const len = buf.write(list[i], offset + 1)
      buf[offset] = len
      offset += len + 1
    }
  }

  buf[offset++] = 0

  name.encode.bytes = offset - oldOffset
  return buf
}

name.encode.bytes = 0

name.decode = function (buf, offset) {
  if (!offset) offset = 0

  const list = []
  const oldOffset = offset
  let len = buf[offset++]

  if (len === 0) {
    name.decode.bytes = 1
    return '.'
  }
  if (len >= 0xc0) {
    const res = name.decode(buf, buf.readUInt16BE(offset - 1) - 0xc000)
    name.decode.bytes = 2
    return res
  }

  while (len) {
    if (len >= 0xc0) {
      list.push(name.decode(buf, buf.readUInt16BE(offset - 1) - 0xc000))
      offset++
      break
    }

    list.push(buf.toString('utf-8', offset, offset + len))
    offset += len
    len = buf[offset++]
  }

  name.decode.bytes = offset - oldOffset
  return list.join('.')
}

name.decode.bytes = 0

name.encodingLength = function (n) {
  if (n === '.') return 1
  return Buffer.byteLength(n) + 2
}

const string = {}

string.encode = function (s, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(string.encodingLength(s))
  if (!offset) offset = 0

  const len = buf.write(s, offset + 1)
  buf[offset] = len
  string.encode.bytes = len + 1
  return buf
}

string.encode.bytes = 0

string.decode = function (buf, offset) {
  if (!offset) offset = 0

  const len = buf[offset]
  const s = buf.toString('utf-8', offset + 1, offset + 1 + len)
  string.decode.bytes = len + 1
  return s
}

string.decode.bytes = 0

string.encodingLength = function (s) {
  return Buffer.byteLength(s) + 1
}

const header = {}

header.encode = function (h, buf, offset) {
  if (!buf) buf = header.encodingLength(h)
  if (!offset) offset = 0

  const flags = (h.flags || 0) & 32767
  const type = h.type === 'response' ? RESPONSE_FLAG : QUERY_FLAG

  buf.writeUInt16BE(h.id || 0, offset)
  buf.writeUInt16BE(flags | type, offset + 2)
  buf.writeUInt16BE(h.questions.length, offset + 4)
  buf.writeUInt16BE(h.answers.length, offset + 6)
  buf.writeUInt16BE(h.authorities.length, offset + 8)
  buf.writeUInt16BE(h.additionals.length, offset + 10)

  return buf
}

header.encode.bytes = 12

header.decode = function (buf, offset) {
  if (!offset) offset = 0
  if (buf.length < 12) throw new Error('Header must be 12 bytes')
  const flags = buf.readUInt16BE(offset + 2)

  return {
    id: buf.readUInt16BE(offset),
    type: flags & RESPONSE_FLAG ? 'response' : 'query',
    flags: flags & 32767,
    flag_qr: ((flags >> 15) & 0x1) === 1,
    opcode: opcodes.toString((flags >> 11) & 0xf),
    flag_aa: ((flags >> 10) & 0x1) === 1,
    flag_tc: ((flags >> 9) & 0x1) === 1,
    flag_rd: ((flags >> 8) & 0x1) === 1,
    flag_ra: ((flags >> 7) & 0x1) === 1,
    flag_z: ((flags >> 6) & 0x1) === 1,
    flag_ad: ((flags >> 5) & 0x1) === 1,
    flag_cd: ((flags >> 4) & 0x1) === 1,
    rcode: rcodes.toString(flags & 0xf),
    questions: new Array(buf.readUInt16BE(offset + 4)),
    answers: new Array(buf.readUInt16BE(offset + 6)),
    authorities: new Array(buf.readUInt16BE(offset + 8)),
    additionals: new Array(buf.readUInt16BE(offset + 10))
  }
}

header.decode.bytes = 12

header.encodingLength = function () {
  return 12
}

const runknown = exports.unknown = {}

runknown.encode = function (data, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(runknown.encodingLength(data))
  if (!offset) offset = 0

  buf.writeUInt16BE(data.length, offset)
  data.copy(buf, offset + 2)

  runknown.encode.bytes = data.length + 2
  return buf
}

runknown.encode.bytes = 0

runknown.decode = function (buf, offset) {
  if (!offset) offset = 0

  const len = buf.readUInt16BE(offset)
  const data = buf.slice(offset + 2, offset + 2 + len)
  runknown.decode.bytes = len + 2
  return data
}

runknown.decode.bytes = 0

runknown.encodingLength = function (data) {
  return data.length + 2
}

const rns = exports.ns = {}

rns.encode = function (data, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rns.encodingLength(data))
  if (!offset) offset = 0

  name.encode(data, buf, offset + 2)
  buf.writeUInt16BE(name.encode.bytes, offset)
  rns.encode.bytes = name.encode.bytes + 2
  return buf
}

rns.encode.bytes = 0

rns.decode = function (buf, offset) {
  if (!offset) offset = 0

  const len = buf.readUInt16BE(offset)
  const dd = name.decode(buf, offset + 2)

  rns.decode.bytes = len + 2
  return dd
}

rns.decode.bytes = 0

rns.encodingLength = function (data) {
  return name.encodingLength(data) + 2
}

const rsoa = exports.soa = {}

rsoa.encode = function (data, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rsoa.encodingLength(data))
  if (!offset) offset = 0

  const oldOffset = offset
  offset += 2
  name.encode(data.mname, buf, offset)
  offset += name.encode.bytes
  name.encode(data.rname, buf, offset)
  offset += name.encode.bytes
  buf.writeUInt32BE(data.serial || 0, offset)
  offset += 4
  buf.writeUInt32BE(data.refresh || 0, offset)
  offset += 4
  buf.writeUInt32BE(data.retry || 0, offset)
  offset += 4
  buf.writeUInt32BE(data.expire || 0, offset)
  offset += 4
  buf.writeUInt32BE(data.minimum || 0, offset)
  offset += 4

  buf.writeUInt16BE(offset - oldOffset - 2, oldOffset)
  rsoa.encode.bytes = offset - oldOffset
  return buf
}

rsoa.encode.bytes = 0

rsoa.decode = function (buf, offset) {
  if (!offset) offset = 0

  const oldOffset = offset

  const data = {}
  offset += 2
  data.mname = name.decode(buf, offset)
  offset += name.decode.bytes
  data.rname = name.decode(buf, offset)
  offset += name.decode.bytes
  data.serial = buf.readUInt32BE(offset)
  offset += 4
  data.refresh = buf.readUInt32BE(offset)
  offset += 4
  data.retry = buf.readUInt32BE(offset)
  offset += 4
  data.expire = buf.readUInt32BE(offset)
  offset += 4
  data.minimum = buf.readUInt32BE(offset)
  offset += 4

  rsoa.decode.bytes = offset - oldOffset
  return data
}

rsoa.decode.bytes = 0

rsoa.encodingLength = function (data) {
  return 22 + name.encodingLength(data.mname) + name.encodingLength(data.rname)
}

const rtxt = exports.txt = {}

rtxt.encode = function (data, buf, offset) {
  if (!Array.isArray(data)) data = [data]
  for (let i = 0; i < data.length; i++) {
    if (typeof data[i] === 'string') {
      data[i] = Buffer.from(data[i])
    }
    if (!Buffer.isBuffer(data[i])) {
      throw new Error('Must be a Buffer')
    }
  }

  if (!buf) buf = Buffer.allocUnsafe(rtxt.encodingLength(data))
  if (!offset) offset = 0

  const oldOffset = offset
  offset += 2

  data.forEach(function (d) {
    buf[offset++] = d.length
    d.copy(buf, offset, 0, d.length)
    offset += d.length
  })

  buf.writeUInt16BE(offset - oldOffset - 2, oldOffset)
  rtxt.encode.bytes = offset - oldOffset
  return buf
}

rtxt.encode.bytes = 0

rtxt.decode = function (buf, offset) {
  if (!offset) offset = 0
  const oldOffset = offset
  let remaining = buf.readUInt16BE(offset)
  offset += 2

  let data = []
  while (remaining > 0) {
    const len = buf[offset++]
    --remaining
    if (remaining < len) {
      throw new Error('Buffer overflow')
    }
    data.push(buf.slice(offset, offset + len))
    offset += len
    remaining -= len
  }

  rtxt.decode.bytes = offset - oldOffset
  return data
}

rtxt.decode.bytes = 0

rtxt.encodingLength = function (data) {
  if (!Array.isArray(data)) data = [data]
  let length = 2
  data.forEach(function (buf) {
    if (typeof buf === 'string') {
      length += Buffer.byteLength(buf) + 1
    } else {
      length += buf.length + 1
    }
  })
  return length
}

const rnull = exports.null = {}

rnull.encode = function (data, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rnull.encodingLength(data))
  if (!offset) offset = 0

  if (typeof data === 'string') data = Buffer.from(data)
  if (!data) data = Buffer.allocUnsafe(0)

  const oldOffset = offset
  offset += 2

  const len = data.length
  data.copy(buf, offset, 0, len)
  offset += len

  buf.writeUInt16BE(offset - oldOffset - 2, oldOffset)
  rnull.encode.bytes = offset - oldOffset
  return buf
}

rnull.encode.bytes = 0

rnull.decode = function (buf, offset) {
  if (!offset) offset = 0
  const oldOffset = offset
  const len = buf.readUInt16BE(offset)

  offset += 2

  const data = buf.slice(offset, offset + len)
  offset += len

  rnull.decode.bytes = offset - oldOffset
  return data
}

rnull.decode.bytes = 0

rnull.encodingLength = function (data) {
  if (!data) return 2
  return (Buffer.isBuffer(data) ? data.length : Buffer.byteLength(data)) + 2
}

const rhinfo = exports.hinfo = {}

rhinfo.encode = function (data, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rhinfo.encodingLength(data))
  if (!offset) offset = 0

  const oldOffset = offset
  offset += 2
  string.encode(data.cpu, buf, offset)
  offset += string.encode.bytes
  string.encode(data.os, buf, offset)
  offset += string.encode.bytes
  buf.writeUInt16BE(offset - oldOffset - 2, oldOffset)
  rhinfo.encode.bytes = offset - oldOffset
  return buf
}

rhinfo.encode.bytes = 0

rhinfo.decode = function (buf, offset) {
  if (!offset) offset = 0

  const oldOffset = offset

  const data = {}
  offset += 2
  data.cpu = string.decode(buf, offset)
  offset += string.decode.bytes
  data.os = string.decode(buf, offset)
  offset += string.decode.bytes
  rhinfo.decode.bytes = offset - oldOffset
  return data
}

rhinfo.decode.bytes = 0

rhinfo.encodingLength = function (data) {
  return string.encodingLength(data.cpu) + string.encodingLength(data.os) + 2
}

const rptr = exports.ptr = {}
const rcname = exports.cname = rptr
const rdname = exports.dname = rptr

rptr.encode = function (data, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rptr.encodingLength(data))
  if (!offset) offset = 0

  name.encode(data, buf, offset + 2)
  buf.writeUInt16BE(name.encode.bytes, offset)
  rptr.encode.bytes = name.encode.bytes + 2
  return buf
}

rptr.encode.bytes = 0

rptr.decode = function (buf, offset) {
  if (!offset) offset = 0

  const data = name.decode(buf, offset + 2)
  rptr.decode.bytes = name.decode.bytes + 2
  return data
}

rptr.decode.bytes = 0

rptr.encodingLength = function (data) {
  return name.encodingLength(data) + 2
}

const rsrv = exports.srv = {}

rsrv.encode = function (data, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rsrv.encodingLength(data))
  if (!offset) offset = 0

  buf.writeUInt16BE(data.priority || 0, offset + 2)
  buf.writeUInt16BE(data.weight || 0, offset + 4)
  buf.writeUInt16BE(data.port || 0, offset + 6)
  name.encode(data.target, buf, offset + 8)

  const len = name.encode.bytes + 6
  buf.writeUInt16BE(len, offset)

  rsrv.encode.bytes = len + 2
  return buf
}

rsrv.encode.bytes = 0

rsrv.decode = function (buf, offset) {
  if (!offset) offset = 0

  const len = buf.readUInt16BE(offset)

  const data = {}
  data.priority = buf.readUInt16BE(offset + 2)
  data.weight = buf.readUInt16BE(offset + 4)
  data.port = buf.readUInt16BE(offset + 6)
  data.target = name.decode(buf, offset + 8)

  rsrv.decode.bytes = len + 2
  return data
}

rsrv.decode.bytes = 0

rsrv.encodingLength = function (data) {
  return 8 + name.encodingLength(data.target)
}

const rcaa = exports.caa = {}

rcaa.ISSUER_CRITICAL = 1 << 7

rcaa.encode = function (data, buf, offset) {
  const len = rcaa.encodingLength(data)

  if (!buf) buf = Buffer.allocUnsafe(rcaa.encodingLength(data))
  if (!offset) offset = 0

  if (data.issuerCritical) {
    data.flags = rcaa.ISSUER_CRITICAL
  }

  buf.writeUInt16BE(len - 2, offset)
  offset += 2
  buf.writeUInt8(data.flags || 0, offset)
  offset += 1
  string.encode(data.tag, buf, offset)
  offset += string.encode.bytes
  buf.write(data.value, offset)
  offset += Buffer.byteLength(data.value)

  rcaa.encode.bytes = len
  return buf
}

rcaa.encode.bytes = 0

rcaa.decode = function (buf, offset) {
  if (!offset) offset = 0

  const len = buf.readUInt16BE(offset)
  offset += 2

  const oldOffset = offset
  const data = {}
  data.flags = buf.readUInt8(offset)
  offset += 1
  data.tag = string.decode(buf, offset)
  offset += string.decode.bytes
  data.value = buf.toString('utf-8', offset, oldOffset + len)

  data.issuerCritical = !!(data.flags & rcaa.ISSUER_CRITICAL)

  rcaa.decode.bytes = len + 2

  return data
}

rcaa.decode.bytes = 0

rcaa.encodingLength = function (data) {
  return string.encodingLength(data.tag) + string.encodingLength(data.value) + 2
}

const rmx = exports.mx = {}

rmx.encode = function (data, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rmx.encodingLength(data))
  if (!offset) offset = 0

  const oldOffset = offset
  offset += 2
  buf.writeUInt16BE(data.preference || 0, offset)
  offset += 2
  name.encode(data.exchange, buf, offset)
  offset += name.encode.bytes

  buf.writeUInt16BE(offset - oldOffset - 2, oldOffset)
  rmx.encode.bytes = offset - oldOffset
  return buf
}

rmx.encode.bytes = 0

rmx.decode = function (buf, offset) {
  if (!offset) offset = 0

  const oldOffset = offset

  const data = {}
  offset += 2
  data.preference = buf.readUInt16BE(offset)
  offset += 2
  data.exchange = name.decode(buf, offset)
  offset += name.decode.bytes

  rmx.decode.bytes = offset - oldOffset
  return data
}

rmx.encodingLength = function (data) {
  return 4 + name.encodingLength(data.exchange)
}

const ra = exports.a = {}

ra.encode = function (host, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(ra.encodingLength(host))
  if (!offset) offset = 0

  buf.writeUInt16BE(4, offset)
  offset += 2
  ip.toBuffer(host, buf, offset)
  ra.encode.bytes = 6
  return buf
}

ra.encode.bytes = 0

ra.decode = function (buf, offset) {
  if (!offset) offset = 0

  offset += 2
  const host = ip.toString(buf, offset, 4)
  ra.decode.bytes = 6
  return host
}
ra.decode.bytes = 0

ra.encodingLength = function () {
  return 6
}

const raaaa = exports.aaaa = {}

raaaa.encode = function (host, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(raaaa.encodingLength(host))
  if (!offset) offset = 0

  buf.writeUInt16BE(16, offset)
  offset += 2
  ip.toBuffer(host, buf, offset)
  raaaa.encode.bytes = 18
  return buf
}

raaaa.encode.bytes = 0

raaaa.decode = function (buf, offset) {
  if (!offset) offset = 0

  offset += 2
  const host = ip.toString(buf, offset, 16)
  raaaa.decode.bytes = 18
  return host
}

raaaa.decode.bytes = 0

raaaa.encodingLength = function () {
  return 18
}

const roption = exports.option = {}

roption.encode = function (option, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(roption.encodingLength(option))
  if (!offset) offset = 0
  const oldOffset = offset

  const code = optioncodes.toCode(option.code)
  buf.writeUInt16BE(code, offset)
  offset += 2
  if (option.data) {
    buf.writeUInt16BE(option.data.length, offset)
    offset += 2
    option.data.copy(buf, offset)
    offset += option.data.length
  } else {
    switch (code) {
      // case 3: NSID.  No encode makes sense.
      // case 5,6,7: Not implementable
      case 8: // ECS
        // note: do IP math before calling
        const spl = option.sourcePrefixLength || 0
        const fam = option.family || (ip.isV4Format(option.ip) ? 1 : 2)
        const ipBuf = ip.toBuffer(option.ip)
        const ipLen = Math.ceil(spl / 8)
        buf.writeUInt16BE(ipLen + 4, offset)
        offset += 2
        buf.writeUInt16BE(fam, offset)
        offset += 2
        buf.writeUInt8(spl, offset++)
        buf.writeUInt8(option.scopePrefixLength || 0, offset++)

        ipBuf.copy(buf, offset, 0, ipLen)
        offset += ipLen
        break
      // case 9: EXPIRE (experimental)
      // case 10: COOKIE.  No encode makes sense.
      case 11: // KEEP-ALIVE
        if (option.timeout) {
          buf.writeUInt16BE(2, offset)
          offset += 2
          buf.writeUInt16BE(option.timeout, offset)
          offset += 2
        } else {
          buf.writeUInt16BE(0, offset)
          offset += 2
        }
        break
      case 12: // PADDING
        const len = option.length || 0
        buf.writeUInt16BE(len, offset)
        offset += 2
        buf.fill(0, offset, offset + len)
        offset += len
        break
      // case 13:  CHAIN.  Experimental.
      case 14: // KEY-TAG
        const tagsLen = option.tags.length * 2
        buf.writeUInt16BE(tagsLen, offset)
        offset += 2
        for (const tag of option.tags) {
          buf.writeUInt16BE(tag, offset)
          offset += 2
        }
        break
      case 15: // EDNS_ERROR
        const text = option.text || "";
        buf.writeUInt16BE(text.length + 2, offset)
        offset += 2;
        buf.writeUInt16BE(option.extended_error, offset)
        offset += 2;
        buf.write(text, offset);
        offset += option.text.length;
        break;
      default:
        throw new Error(`Unknown roption code: ${option.code}`)
    }
  }

  roption.encode.bytes = offset - oldOffset
  return buf
}

roption.encode.bytes = 0

roption.decode = function (buf, offset) {
  if (!offset) offset = 0
  const option = {}
  option.code = buf.readUInt16BE(offset)
  option.type = optioncodes.toString(option.code)
  offset += 2
  const len = buf.readUInt16BE(offset)
  offset += 2
  option.data = buf.slice(offset, offset + len)
  switch (option.code) {
    // case 3: NSID.  No decode makes sense.
    case 8: // ECS
      option.family = buf.readUInt16BE(offset)
      offset += 2
      option.sourcePrefixLength = buf.readUInt8(offset++)
      option.scopePrefixLength = buf.readUInt8(offset++)
      const padded = Buffer.alloc((option.family === 1) ? 4 : 16)
      buf.copy(padded, 0, offset, offset + len - 4)
      option.ip = ip.toString(padded)
      break
    // case 12: Padding.  No decode makes sense.
    case 11: // KEEP-ALIVE
      if (len > 0) {
        option.timeout = buf.readUInt16BE(offset)
        offset += 2
      }
      break
    case 14:
      option.tags = []
      for (let i = 0; i < len; i += 2) {
        option.tags.push(buf.readUInt16BE(offset))
        offset += 2
      }
    // don't worry about default.  caller will use data if desired
  }

  roption.decode.bytes = len + 4
  return option
}

roption.decode.bytes = 0

roption.encodingLength = function (option) {
  if (option.data) {
    return option.data.length + 4
  }
  const code = optioncodes.toCode(option.code)
  switch (code) {
    case 8: // ECS
      const spl = option.sourcePrefixLength || 0
      return Math.ceil(spl / 8) + 8
    case 11: // KEEP-ALIVE
      return (typeof option.timeout === 'number') ? 6 : 4
    case 12: // PADDING
      return option.length + 4
    case 14: // KEY-TAG
      return 4 + (option.tags.length * 2)
    case 15: // EDNS_ERROR
      return 4 + 2 + option.text.length
  }
  throw new Error(`Unknown roption code: ${option.code}`)
}

const ropt = exports.opt = {}

ropt.encode = function (options, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(ropt.encodingLength(options))
  if (!offset) offset = 0
  const oldOffset = offset

  const rdlen = encodingLengthList(options, roption)
  buf.writeUInt16BE(rdlen, offset)
  offset = encodeList(options, roption, buf, offset + 2)

  ropt.encode.bytes = offset - oldOffset
  return buf
}

ropt.encode.bytes = 0

ropt.decode = function (buf, offset) {
  if (!offset) offset = 0
  const oldOffset = offset

  const options = []
  let rdlen = buf.readUInt16BE(offset)
  offset += 2
  let o = 0
  while (rdlen > 0) {
    options[o++] = roption.decode(buf, offset)
    offset += roption.decode.bytes
    rdlen -= roption.decode.bytes
  }
  ropt.decode.bytes = offset - oldOffset
  return options
}

ropt.decode.bytes = 0

ropt.encodingLength = function (options) {
  return 2 + encodingLengthList(options || [], roption)
}

const rdnskey = exports.dnskey = {}

rdnskey.PROTOCOL_DNSSEC = 3
rdnskey.ZONE_KEY = 0x80
rdnskey.SECURE_ENTRYPOINT = 0x8000

rdnskey.encode = function (key, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rdnskey.encodingLength(key))
  if (!offset) offset = 0
  const oldOffset = offset

  const keydata = key.key
  if (!Buffer.isBuffer(keydata)) {
    throw new Error('Key must be a Buffer')
  }

  offset += 2 // Leave space for length
  buf.writeUInt16BE(key.flags, offset)
  offset += 2
  buf.writeUInt8(rdnskey.PROTOCOL_DNSSEC, offset)
  offset += 1
  buf.writeUInt8(key.algorithm, offset)
  offset += 1
  keydata.copy(buf, offset, 0, keydata.length)
  offset += keydata.length

  rdnskey.encode.bytes = offset - oldOffset
  buf.writeUInt16BE(rdnskey.encode.bytes - 2, oldOffset)
  return buf
}

rdnskey.encode.bytes = 0

rdnskey.decode = function (buf, offset) {
  if (!offset) offset = 0
  const oldOffset = offset

  var key = {}
  var length = buf.readUInt16BE(offset)
  offset += 2
  key.flags = buf.readUInt16BE(offset)
  offset += 2
  if (buf.readUInt8(offset) !== rdnskey.PROTOCOL_DNSSEC) {
    throw new Error('Protocol must be 3')
  }
  offset += 1
  key.algorithm = buf.readUInt8(offset)
  offset += 1
  key.key = buf.slice(offset, oldOffset + length + 2)
  offset += key.key.length
  rdnskey.decode.bytes = offset - oldOffset
  return key
}

rdnskey.decode.bytes = 0

rdnskey.encodingLength = function (key) {
  return 6 + Buffer.byteLength(key.key)
}

const rrrsig = exports.rrsig = {}

rrrsig.encode = function (sig, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rrrsig.encodingLength(sig))
  if (!offset) offset = 0
  const oldOffset = offset

  const signature = sig.signature
  if (!Buffer.isBuffer(signature)) {
    throw new Error('Signature must be a Buffer')
  }

  offset += 2 // Leave space for length
  buf.writeUInt16BE(types.toType(sig.typeCovered), offset)
  offset += 2
  buf.writeUInt8(sig.algorithm, offset)
  offset += 1
  buf.writeUInt8(sig.labels, offset)
  offset += 1
  buf.writeUInt32BE(sig.originalTTL, offset)
  offset += 4
  buf.writeUInt32BE(sig.expiration, offset)
  offset += 4
  buf.writeUInt32BE(sig.inception, offset)
  offset += 4
  buf.writeUInt16BE(sig.keyTag, offset)
  offset += 2
  name.encode(sig.signersName, buf, offset)
  offset += name.encode.bytes
  signature.copy(buf, offset, 0, signature.length)
  offset += signature.length

  rrrsig.encode.bytes = offset - oldOffset
  buf.writeUInt16BE(rrrsig.encode.bytes - 2, oldOffset)
  return buf
}

rrrsig.encode.bytes = 0

rrrsig.decode = function (buf, offset) {
  if (!offset) offset = 0
  const oldOffset = offset

  var sig = {}
  var length = buf.readUInt16BE(offset)
  offset += 2
  sig.typeCovered = types.toString(buf.readUInt16BE(offset))
  offset += 2
  sig.algorithm = buf.readUInt8(offset)
  offset += 1
  sig.labels = buf.readUInt8(offset)
  offset += 1
  sig.originalTTL = buf.readUInt32BE(offset)
  offset += 4
  sig.expiration = buf.readUInt32BE(offset)
  offset += 4
  sig.inception = buf.readUInt32BE(offset)
  offset += 4
  sig.keyTag = buf.readUInt16BE(offset)
  offset += 2
  sig.signersName = name.decode(buf, offset)
  offset += name.decode.bytes
  sig.signature = buf.slice(offset, oldOffset + length + 2)
  offset += sig.signature.length
  rrrsig.decode.bytes = offset - oldOffset
  return sig
}

rrrsig.decode.bytes = 0

rrrsig.encodingLength = function (sig) {
  return 20 +
    name.encodingLength(sig.signersName) +
    Buffer.byteLength(sig.signature)
}

const rrp = exports.rp = {}

rrp.encode = function (data, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rrp.encodingLength(data))
  if (!offset) offset = 0
  const oldOffset = offset

  offset += 2 // Leave space for length
  name.encode(data.mbox || '.', buf, offset)
  offset += name.encode.bytes
  name.encode(data.txt || '.', buf, offset)
  offset += name.encode.bytes
  rrp.encode.bytes = offset - oldOffset
  buf.writeUInt16BE(rrp.encode.bytes - 2, oldOffset)
  return buf
}

rrp.encode.bytes = 0

rrp.decode = function (buf, offset) {
  if (!offset) offset = 0
  const oldOffset = offset

  const data = {}
  offset += 2
  data.mbox = name.decode(buf, offset) || '.'
  offset += name.decode.bytes
  data.txt = name.decode(buf, offset) || '.'
  offset += name.decode.bytes
  rrp.decode.bytes = offset - oldOffset
  return data
}

rrp.decode.bytes = 0

rrp.encodingLength = function (data) {
  return 2 + name.encodingLength(data.mbox || '.') + name.encodingLength(data.txt || '.')
}

const typebitmap = {}

typebitmap.encode = function (typelist, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(typebitmap.encodingLength(typelist))
  if (!offset) offset = 0
  const oldOffset = offset

  var typesByWindow = []
  for (var i = 0; i < typelist.length; i++) {
    var typeid = types.toType(typelist[i])
    if (typesByWindow[typeid >> 8] === undefined) {
      typesByWindow[typeid >> 8] = []
    }
    typesByWindow[typeid >> 8][(typeid >> 3) & 0x1F] |= 1 << (7 - (typeid & 0x7))
  }

  for (i = 0; i < typesByWindow.length; i++) {
    if (typesByWindow[i] !== undefined) {
      var windowBuf = Buffer.from(typesByWindow[i])
      buf.writeUInt8(i, offset)
      offset += 1
      buf.writeUInt8(windowBuf.length, offset)
      offset += 1
      windowBuf.copy(buf, offset)
      offset += windowBuf.length
    }
  }

  typebitmap.encode.bytes = offset - oldOffset
  return buf
}

typebitmap.encode.bytes = 0

typebitmap.decode = function (buf, offset, length) {
  if (!offset) offset = 0
  const oldOffset = offset

  var typelist = []
  while (offset - oldOffset < length) {
    var window = buf.readUInt8(offset)
    offset += 1
    var windowLength = buf.readUInt8(offset)
    offset += 1
    for (var i = 0; i < windowLength; i++) {
      var b = buf.readUInt8(offset + i)
      for (var j = 0; j < 8; j++) {
        if (b & (1 << (7 - j))) {
          var typeid = types.toString((window << 8) | (i << 3) | j)
          typelist.push(typeid)
        }
      }
    }
    offset += windowLength
  }

  typebitmap.decode.bytes = offset - oldOffset
  return typelist
}

typebitmap.decode.bytes = 0

typebitmap.encodingLength = function (typelist) {
  var extents = []
  for (var i = 0; i < typelist.length; i++) {
    var typeid = types.toType(typelist[i])
    extents[typeid >> 8] = Math.max(extents[typeid >> 8] || 0, typeid & 0xFF)
  }

  var len = 0
  for (i = 0; i < extents.length; i++) {
    if (extents[i] !== undefined) {
      len += 2 + Math.ceil((extents[i] + 1) / 8)
    }
  }

  return len
}

const rnsec = exports.nsec = {}

rnsec.encode = function (record, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rnsec.encodingLength(record))
  if (!offset) offset = 0
  const oldOffset = offset

  offset += 2 // Leave space for length
  name.encode(record.nextDomain, buf, offset)
  offset += name.encode.bytes
  typebitmap.encode(record.rrtypes, buf, offset)
  offset += typebitmap.encode.bytes

  rnsec.encode.bytes = offset - oldOffset
  buf.writeUInt16BE(rnsec.encode.bytes - 2, oldOffset)
  return buf
}

rnsec.encode.bytes = 0

rnsec.decode = function (buf, offset) {
  if (!offset) offset = 0
  const oldOffset = offset

  var record = {}
  var length = buf.readUInt16BE(offset)
  offset += 2
  record.nextDomain = name.decode(buf, offset)
  offset += name.decode.bytes
  record.rrtypes = typebitmap.decode(buf, offset, length - (offset - oldOffset))
  offset += typebitmap.decode.bytes

  rnsec.decode.bytes = offset - oldOffset
  return record
}

rnsec.decode.bytes = 0

rnsec.encodingLength = function (record) {
  return 2 +
    name.encodingLength(record.nextDomain) +
    typebitmap.encodingLength(record.rrtypes)
}

const rnsec3 = exports.nsec3 = {}

rnsec3.encode = function (record, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rnsec3.encodingLength(record))
  if (!offset) offset = 0
  const oldOffset = offset

  const salt = record.salt
  if (!Buffer.isBuffer(salt)) {
    throw new Error('salt must be a Buffer')
  }

  const nextDomain = record.nextDomain
  if (!Buffer.isBuffer(nextDomain)) {
    throw new Error('nextDomain must be a Buffer')
  }

  offset += 2 // Leave space for length
  buf.writeUInt8(record.algorithm, offset)
  offset += 1
  buf.writeUInt8(record.flags, offset)
  offset += 1
  buf.writeUInt16BE(record.iterations, offset)
  offset += 2
  buf.writeUInt8(salt.length, offset)
  offset += 1
  salt.copy(buf, offset, 0, salt.length)
  offset += salt.length
  buf.writeUInt8(nextDomain.length, offset)
  offset += 1
  nextDomain.copy(buf, offset, 0, nextDomain.length)
  offset += nextDomain.length
  typebitmap.encode(record.rrtypes, buf, offset)
  offset += typebitmap.encode.bytes

  rnsec3.encode.bytes = offset - oldOffset
  buf.writeUInt16BE(rnsec3.encode.bytes - 2, oldOffset)
  return buf
}

rnsec3.encode.bytes = 0

rnsec3.decode = function (buf, offset) {
  if (!offset) offset = 0
  const oldOffset = offset

  var record = {}
  var length = buf.readUInt16BE(offset)
  offset += 2
  record.algorithm = buf.readUInt8(offset)
  offset += 1
  record.flags = buf.readUInt8(offset)
  offset += 1
  record.iterations = buf.readUInt16BE(offset)
  offset += 2
  const saltLength = buf.readUInt8(offset)
  offset += 1
  record.salt = buf.slice(offset, offset + saltLength)
  offset += saltLength
  const hashLength = buf.readUInt8(offset)
  offset += 1
  record.nextDomain = buf.slice(offset, offset + hashLength)
  offset += hashLength
  record.rrtypes = typebitmap.decode(buf, offset, length - (offset - oldOffset))
  offset += typebitmap.decode.bytes

  rnsec3.decode.bytes = offset - oldOffset
  return record
}

rnsec3.decode.bytes = 0

rnsec3.encodingLength = function (record) {
  return 8 +
    record.salt.length +
    record.nextDomain.length +
    typebitmap.encodingLength(record.rrtypes)
}

const rds = exports.ds = {}

rds.encode = function (digest, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rds.encodingLength(digest))
  if (!offset) offset = 0
  const oldOffset = offset

  const digestdata = digest.digest
  if (!Buffer.isBuffer(digestdata)) {
    throw new Error('Digest must be a Buffer')
  }

  offset += 2 // Leave space for length
  buf.writeUInt16BE(digest.keyTag, offset)
  offset += 2
  buf.writeUInt8(digest.algorithm, offset)
  offset += 1
  buf.writeUInt8(digest.digestType, offset)
  offset += 1
  digestdata.copy(buf, offset, 0, digestdata.length)
  offset += digestdata.length

  rds.encode.bytes = offset - oldOffset
  buf.writeUInt16BE(rds.encode.bytes - 2, oldOffset)
  return buf
}

rds.encode.bytes = 0

rds.decode = function (buf, offset) {
  if (!offset) offset = 0
  const oldOffset = offset

  var digest = {}
  var length = buf.readUInt16BE(offset)
  offset += 2
  digest.keyTag = buf.readUInt16BE(offset)
  offset += 2
  digest.algorithm = buf.readUInt8(offset)
  offset += 1
  digest.digestType = buf.readUInt8(offset)
  offset += 1
  digest.digest = buf.slice(offset, oldOffset + length + 2)
  offset += digest.digest.length
  rds.decode.bytes = offset - oldOffset
  return digest
}

rds.decode.bytes = 0

rds.encodingLength = function (digest) {
  return 6 + Buffer.byteLength(digest.digest)
}

const svcparam = exports.svcparam = {}

svcparam.keyToNumber = function(keyName) {
  switch (keyName.toLowerCase()) {
    case 'mandatory': return 0
    case 'alpn' : return 1
    case 'no-default-alpn' : return 2
    case 'port' : return 3
    case 'ipv4hint' : return 4
    case 'echconfig' : return 5
    case 'ipv6hint' : return 6
    case 'odoh' : return 32769
    case 'key65535' : return 65535
  }
  if (!keyName.startsWith('key')) {
    throw new Error(`Name must start with key: ${keyName}`);
  }

  return Number.parseInt(keyName.substring(3));
}

svcparam.numberToKeyName = function(number) {
  switch (number) {
    case 0 : return 'mandatory'
    case 1 : return 'alpn'
    case 2 : return 'no-default-alpn'
    case 3 : return 'port'
    case 4 : return 'ipv4hint'
    case 5 : return 'echconfig'
    case 6 : return 'ipv6hint'
    case 32769 : return 'odoh'
  }

  return `key${number}`;
}

svcparam.encode = function(param, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(svcparam.encodingLength(param))
  if (!offset) offset = 0

  let key = param.key;
  if (typeof param.key !== 'number') {
    key = svcparam.keyToNumber(param.key);
  }

  buf.writeUInt16BE(key || 0, offset)
  offset += 2;
  svcparam.encode.bytes = 2;

  if (key == 0) { // mandatory
    let values = param.value;
    if (!Array.isArray(values)) values = [values];
    buf.writeUInt16BE(values.length*2, offset);
    offset += 2;
    svcparam.encode.bytes += 2;

    for (let val of values) {
      if (typeof val !== 'number') {
        val = svcparam.keyToNumber(val);
      }
      buf.writeUInt16BE(val, offset);
      offset += 2;
      svcparam.encode.bytes += 2;
    }
  } else if (key == 1) { // alpn
    let val = param.value;
    if (!Array.isArray(val)) val = [val];
    // The alpn param is prefixed by its length as a single byte, so the
    // initialValue to reduce function is the length of the array.
    let total = val.reduce(function(result, id) {
      return result += id.length;
    }, val.length);

    buf.writeUInt16BE(total, offset);
    offset += 2;
    svcparam.encode.bytes += 2;

    for (let id of val) {
      buf.writeUInt8(id.length, offset);
      offset += 1;
      svcparam.encode.bytes += 1;

      buf.write(id, offset);
      offset += id.length;
      svcparam.encode.bytes += id.length;
    }
  } else if (key == 2) { // no-default-alpn
    buf.writeUInt16BE(0, offset);
    offset += 2;
    svcparam.encode.bytes += 2;
  } else if (key == 3) { // port
    buf.writeUInt16BE(2, offset);
    offset += 2;
    svcparam.encode.bytes += 2;
    buf.writeUInt16BE(param.value || 0, offset);
    offset += 2;
    svcparam.encode.bytes += 2;
  } else if (key == 4) { //ipv4hint
    let val = param.value;
    if (!Array.isArray(val)) val = [val];
    buf.writeUInt16BE(val.length*4, offset);
    offset += 2;
    svcparam.encode.bytes += 2;

    for (let host of val) {
      ip.toBuffer(host, buf, offset)
      offset += 4;
      svcparam.encode.bytes += 4;
    }
  } else if (key == 5) { //echconfig
    if (svcparam.ech) {
      buf.writeUInt16BE(svcparam.ech.length, offset);
      offset += 2;
      svcparam.encode.bytes += 2;
      for (let i = 0; i < svcparam.ech.length; i++) {
        buf.writeUInt8(svcparam.ech[i], offset);
        offset++;
      }
      svcparam.encode.bytes += svcparam.ech.length;
    } else {
      buf.writeUInt16BE(param.value.length, offset);
      offset += 2;
      svcparam.encode.bytes += 2;
      buf.write(param.value, offset);
      offset += param.value.length;
      svcparam.encode.bytes += param.value.length;
    }
  } else if (key == 6) { //ipv6hint
    let val = param.value;
    if (!Array.isArray(val)) val = [val];
    buf.writeUInt16BE(val.length*16, offset);
    offset += 2;
    svcparam.encode.bytes += 2;

    for (let host of val) {
      ip.toBuffer(host, buf, offset)
      offset += 16;
      svcparam.encode.bytes += 16;
    }
   } else if (key == 32769) { //odoh
      if (svcparam.odoh) {
        buf.writeUInt16BE(svcparam.odoh.length, offset);
        offset += 2;
        svcparam.encode.bytes += 2;
        for (let i = 0; i < svcparam.odoh.length; i++) {
          buf.writeUInt8(svcparam.odoh[i], offset);
          offset++;
        }
        svcparam.encode.bytes += svcparam.odoh.length;
        svcparam.odoh = null;
      } else {
        buf.writeUInt16BE(param.value.length, offset);
        offset += 2;
        svcparam.encode.bytes += 2;
        buf.write(param.value, offset);
        offset += param.value.length;
        svcparam.encode.bytes += param.value.length;
      }
  } else {
    // Unknown option
    buf.writeUInt16BE(0, offset); // 0 length since we don't know how to encode
    offset += 2;
    svcparam.encode.bytes += 2;
  }

}

svcparam.encode.bytes = 0;

svcparam.decode = function (buf, offset) {
  let param = {};
  let id = buf.readUInt16BE(offset);
  param.key = svcparam.numberToKeyName(id);
  offset += 2;
  svcparam.decode.bytes = 2;

  let len = buf.readUInt16BE(offset);
  offset += 2;
  svcparam.decode.bytes += 2;

  param.value = buf.toString('utf-8', offset, offset + len);
  offset += len;
  svcparam.decode.bytes += len;

  return param;
}

svcparam.decode.bytes = 0;

svcparam.encodingLength = function (param) {
  // 2 bytes for type, 2 bytes for length, what's left for the value

  switch (param.key) {
    case 'mandatory' : return 4 + 2*(Array.isArray(param.value) ? param.value.length : 1)
    case 'alpn' : {
      let val = param.value;
      if (!Array.isArray(val)) val = [val];
      let total = val.reduce(function(result, id) {
        return result += id.length;
      }, val.length);
      return 4 + total;
    }
    case 'no-default-alpn' : return 4
    case 'port' : return 4 + 2
    case 'ipv4hint' : return 4 + 4 * (Array.isArray(param.value) ? param.value.length : 1)
    case 'echconfig' : {
      if (param.needBase64Decode) {
        svcparam.ech = Buffer.from(param.value, "base64");
        return 4 + svcparam.ech.length;
      }
      return 4 + param.value.length
    }
    case 'ipv6hint' : return 4 + 16 * (Array.isArray(param.value) ? param.value.length : 1)
    case 'odoh' : {
      if (param.needBase64Decode) {
        svcparam.odoh = Buffer.from(param.value, "base64");
        return 4 + svcparam.odoh.length;
      }
      return 4 + param.value.length
    }
    case 'key65535' : return 4
    default: return 4 // unknown option
  }
}

const rhttpssvc = exports.httpssvc = {}

rhttpssvc.encode = function(data, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(rhttpssvc.encodingLength(data))
  if (!offset) offset = 0

  buf.writeUInt16BE(rhttpssvc.encodingLength(data) - 2 , offset);
  offset += 2;

  buf.writeUInt16BE(data.priority || 0, offset);
  rhttpssvc.encode.bytes = 4;
  offset += 2;
  name.encode(data.name, buf, offset);
  rhttpssvc.encode.bytes += name.encode.bytes;
  offset += name.encode.bytes;

  if (data.priority == 0) {
    return;
  }

  for (let val of data.values) {
    svcparam.encode(val, buf, offset);
    offset += svcparam.encode.bytes;
    rhttpssvc.encode.bytes += svcparam.encode.bytes;
  }

  return buf;
}

rhttpssvc.encode.bytes = 0;

rhttpssvc.decode = function (buf, offset) {
  let rdlen = buf.readUInt16BE(offset);
  let oldOffset = offset;
  offset += 2;
  let record = {}
  record.priority = buf.readUInt16BE(offset);
  offset += 2;
  rhttpssvc.decode.bytes = 4;
  record.name = name.decode(buf, offset);
  offset += name.decode.bytes;
  rhttpssvc.decode.bytes += name.decode.bytes;

  while (rdlen > rhttpssvc.decode.bytes - 2) {
    let rec1 = svcparam.decode(buf, offset);
    offset += svcparam.decode.bytes;
    rhttpssvc.decode.bytes += svcparam.decode.bytes;
    record.values.push(rec1);
  }

  return record;
}

rhttpssvc.decode.bytes = 0;

rhttpssvc.encodingLength = function (data) {
  let len =
    2 + // rdlen
    2 + // priority
    name.encodingLength(data.name);
  len += data.values.map(svcparam.encodingLength).reduce((acc, len) => acc + len, 0);
  return len;
}

const renc = exports.record = function (type) {
  switch (type.toUpperCase()) {
    case 'A': return ra
    case 'PTR': return rptr
    case 'CNAME': return rcname
    case 'DNAME': return rdname
    case 'TXT': return rtxt
    case 'NULL': return rnull
    case 'AAAA': return raaaa
    case 'SRV': return rsrv
    case 'HINFO': return rhinfo
    case 'CAA': return rcaa
    case 'NS': return rns
    case 'SOA': return rsoa
    case 'MX': return rmx
    case 'OPT': return ropt
    case 'DNSKEY': return rdnskey
    case 'RRSIG': return rrrsig
    case 'RP': return rrp
    case 'NSEC': return rnsec
    case 'NSEC3': return rnsec3
    case 'DS': return rds
    case 'HTTPS': return rhttpssvc
  }
  return runknown
}

const answer = exports.answer = {}

answer.encode = function (a, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(answer.encodingLength(a))
  if (!offset) offset = 0

  const oldOffset = offset

  name.encode(a.name, buf, offset)
  offset += name.encode.bytes

  buf.writeUInt16BE(types.toType(a.type), offset)

  if (a.type.toUpperCase() === 'OPT') {
    if (a.name !== '.') {
      throw new Error('OPT name must be root.')
    }
    buf.writeUInt16BE(a.udpPayloadSize || 4096, offset + 2)
    buf.writeUInt8(a.extendedRcode || 0, offset + 4)
    buf.writeUInt8(a.ednsVersion || 0, offset + 5)
    buf.writeUInt16BE(a.flags || 0, offset + 6)

    offset += 8
    ropt.encode(a.options || [], buf, offset)
    offset += ropt.encode.bytes
  } else {
    let klass = classes.toClass(a.class === undefined ? 'IN' : a.class)
    if (a.flush) klass |= FLUSH_MASK // the 1st bit of the class is the flush bit
    buf.writeUInt16BE(klass, offset + 2)
    buf.writeUInt32BE(a.ttl || 0, offset + 4)

    offset += 8
    const enc = renc(a.type)
    enc.encode(a.data, buf, offset)
    offset += enc.encode.bytes
  }

  answer.encode.bytes = offset - oldOffset
  return buf
}

answer.encode.bytes = 0

answer.decode = function (buf, offset) {
  if (!offset) offset = 0

  const a = {}
  const oldOffset = offset

  a.name = name.decode(buf, offset)
  offset += name.decode.bytes
  a.type = types.toString(buf.readUInt16BE(offset))
  if (a.type === 'OPT') {
    a.udpPayloadSize = buf.readUInt16BE(offset + 2)
    a.extendedRcode = buf.readUInt8(offset + 4)
    a.ednsVersion = buf.readUInt8(offset + 5)
    a.flags = buf.readUInt16BE(offset + 6)
    a.flag_do = ((a.flags >> 15) & 0x1) === 1
    a.options = ropt.decode(buf, offset + 8)
    offset += 8 + ropt.decode.bytes
  } else {
    const klass = buf.readUInt16BE(offset + 2)
    a.ttl = buf.readUInt32BE(offset + 4)
    a.class = classes.toString(klass & NOT_FLUSH_MASK)
    a.flush = !!(klass & FLUSH_MASK)

    const enc = renc(a.type)
    a.data = enc.decode(buf, offset + 8)
    offset += 8 + enc.decode.bytes
  }

  answer.decode.bytes = offset - oldOffset
  return a
}

answer.decode.bytes = 0

answer.encodingLength = function (a) {
  const data = (a.data !== null && a.data !== undefined) ? a.data : a.options
  return name.encodingLength(a.name) + 8 + renc(a.type).encodingLength(data)
}

const question = exports.question = {}

question.encode = function (q, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(question.encodingLength(q))
  if (!offset) offset = 0

  const oldOffset = offset

  name.encode(q.name, buf, offset)
  offset += name.encode.bytes

  buf.writeUInt16BE(types.toType(q.type), offset)
  offset += 2

  buf.writeUInt16BE(classes.toClass(q.class === undefined ? 'IN' : q.class), offset)
  offset += 2

  question.encode.bytes = offset - oldOffset
  return q
}

question.encode.bytes = 0

question.decode = function (buf, offset) {
  if (!offset) offset = 0

  const oldOffset = offset
  const q = {}

  q.name = name.decode(buf, offset)
  offset += name.decode.bytes

  q.type = types.toString(buf.readUInt16BE(offset))
  offset += 2

  q.class = classes.toString(buf.readUInt16BE(offset))
  offset += 2

  const qu = !!(q.class & QU_MASK)
  if (qu) q.class &= NOT_QU_MASK

  question.decode.bytes = offset - oldOffset
  return q
}

question.decode.bytes = 0

question.encodingLength = function (q) {
  return name.encodingLength(q.name) + 4
}

exports.AUTHORITATIVE_ANSWER = 1 << 10
exports.TRUNCATED_RESPONSE = 1 << 9
exports.RECURSION_DESIRED = 1 << 8
exports.RECURSION_AVAILABLE = 1 << 7
exports.AUTHENTIC_DATA = 1 << 5
exports.CHECKING_DISABLED = 1 << 4
exports.DNSSEC_OK = 1 << 15

exports.encode = function (result, buf, offset) {
  if (!buf) buf = Buffer.allocUnsafe(exports.encodingLength(result))
  if (!offset) offset = 0

  const oldOffset = offset

  if (!result.questions) result.questions = []
  if (!result.answers) result.answers = []
  if (!result.authorities) result.authorities = []
  if (!result.additionals) result.additionals = []

  header.encode(result, buf, offset)
  offset += header.encode.bytes

  offset = encodeList(result.questions, question, buf, offset)
  offset = encodeList(result.answers, answer, buf, offset)
  offset = encodeList(result.authorities, answer, buf, offset)
  offset = encodeList(result.additionals, answer, buf, offset)

  exports.encode.bytes = offset - oldOffset

  return buf
}

exports.encode.bytes = 0

exports.decode = function (buf, offset) {
  if (!offset) offset = 0

  const oldOffset = offset
  const result = header.decode(buf, offset)
  offset += header.decode.bytes

  offset = decodeList(result.questions, question, buf, offset)
  offset = decodeList(result.answers, answer, buf, offset)
  offset = decodeList(result.authorities, answer, buf, offset)
  offset = decodeList(result.additionals, answer, buf, offset)

  exports.decode.bytes = offset - oldOffset

  return result
}

exports.decode.bytes = 0

exports.encodingLength = function (result) {
  return header.encodingLength(result) +
    encodingLengthList(result.questions || [], question) +
    encodingLengthList(result.answers || [], answer) +
    encodingLengthList(result.authorities || [], answer) +
    encodingLengthList(result.additionals || [], answer)
}

exports.streamEncode = function (result) {
  const buf = exports.encode(result)
  const sbuf = Buffer.allocUnsafe(2)
  sbuf.writeUInt16BE(buf.byteLength)
  const combine = Buffer.concat([sbuf, buf])
  exports.streamEncode.bytes = combine.byteLength
  return combine
}

exports.streamEncode.bytes = 0

exports.streamDecode = function (sbuf) {
  const len = sbuf.readUInt16BE(0)
  if (sbuf.byteLength < len + 2) {
    // not enough data
    return null
  }
  const result = exports.decode(sbuf.slice(2))
  exports.streamDecode.bytes = exports.decode.bytes
  return result
}

exports.streamDecode.bytes = 0

function encodingLengthList (list, enc) {
  let len = 0
  for (let i = 0; i < list.length; i++) len += enc.encodingLength(list[i])
  return len
}

function encodeList (list, enc, buf, offset) {
  for (let i = 0; i < list.length; i++) {
    enc.encode(list[i], buf, offset)
    offset += enc.encode.bytes
  }
  return offset
}

function decodeList (list, enc, buf, offset) {
  for (let i = 0; i < list.length; i++) {
    list[i] = enc.decode(buf, offset)
    offset += enc.decode.bytes
  }
  return offset
}
