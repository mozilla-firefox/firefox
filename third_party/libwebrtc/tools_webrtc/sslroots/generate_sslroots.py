#!/usr/bin/env vpython3

# -*- coding:utf-8 -*-
# Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.
import argparse
import logging
from pathlib import Path
import tempfile
from typing import Tuple, Any, List, ByteString
from datetime import datetime, timezone
from hashlib import sha256
from urllib.request import urlopen
from asn1crypto import pem, x509

_GENERATED_FILE = 'ssl_roots.h'


def main():
    parser = argparse.ArgumentParser(
        description='This is a tool to transform a crt file '
        f'into a C/C++ header: {_GENERATED_FILE}.')

    parser.add_argument('source_path_or_url',
                        help='File path or URL to PEM storage file. '
                        'The supported cert files are: '
                        '- Google: https://pki.goog/roots.pem; '
                        '- Mozilla: https://curl.se/ca/cacert.pem')
    parser.add_argument('-v',
                        '--verbose',
                        dest='verbose',
                        action='store_true',
                        help='Print output while running')
    parser.add_argument('-f',
                        '--full_cert',
                        dest='full_cert',
                        action='store_true',
                        help='Add public key and certificate name. '
                        'Default is to skip and reduce generated file size.')
    args = parser.parse_args()
    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.WARNING)

    with tempfile.TemporaryDirectory() as temp_dir:
        cert_file = Path(temp_dir) / "cacert.pem"

        if args.source_path_or_url.startswith(
                'https://') or args.source_path_or_url.startswith('http://'):
            _download_certificates_store(args.source_path_or_url, cert_file)
            destination_dir = Path.cwd()
        else:
            source_path = Path(args.source_path_or_url)
            cert_file.write_bytes(source_path.read_bytes())
            destination_dir = source_path.parent

        logging.debug('Stored certificate from %s into %s',
                      args.source_path_or_url, cert_file)

        header_file = destination_dir / _GENERATED_FILE

        digest, certificates = _load_certificates_store(cert_file)
        _generate_c_header(header_file, args.source_path_or_url, digest,
                           certificates, args.full_cert)

        logging.debug('Did generate %s from %s [%s]', header_file,
                      args.source_path_or_url, digest)


def _download_certificates_store(pem_url: str, destination_file: Path):
    with urlopen(pem_url) as response:
        pem_file = response.read()
        logging.info('Got response with status [%d]: %s', response.status,
                     pem_url)

    if destination_file.parent.exists():
        logging.debug('Creating directory and it\'s parents %s',
                      destination_file.parent)
        destination_file.parent.mkdir(parents=True, exist_ok=True)
    if destination_file.exists():
        logging.debug('Unlink existing file %s', destination_file)
        destination_file.unlink(missing_ok=True)

    destination_file.write_bytes(pem_file)
    logging.info('Stored downloaded %d bytes pem file to `%s`', len(pem_file),
                 destination_file)


def _load_certificates_store(
        source_file: Path) -> Tuple[str, List[x509.Certificate]]:
    pem_bytes = source_file.read_bytes()

    certificates = [
        x509.Certificate.load(der)
        for type, _, der in pem.unarmor(pem_bytes, True)
        if type == 'CERTIFICATE'
    ]
    digest = f'sha256:{sha256(pem_bytes).hexdigest()}'
    logging.debug('Loaded %d certificates from %s [%s] ', len(certificates),
                  source_file, digest)
    return digest, certificates


def _generate_c_header(header_file: Path, source: str, source_digest: str,
                       certificates: List[x509.Certificate], full_cert: bool):
    header_file.parent.mkdir(parents=True, exist_ok=True)
    with header_file.open('w') as output:
        output.write(_create_output_header(source, source_digest))

        named_certificates = [
            (cert, f'kCertificateWithFingerprint_{cert.sha256.hex()}')
            for cert in certificates
        ]

        names = list(map(lambda x: x[1], named_certificates))
        unique_names = list(set(names))
        if len(names) != len(unique_names):
            raise RuntimeError(
                f'There are {len(names) - len(unique_names)} non-unique '
                'certificate names generated. Generator script must be '
                'fixed to handle collision.')

        for cert, name in named_certificates:

            output.write(_create_certificate_metadata_header(cert))

            if full_cert:
                output.write(
                    _c_array_constant_definition('unsigned char',
                                                 f'{name}_subject_name',
                                                 _create_hex_list(
                                                     cert.subject.dump()),
                                                 max_items_per_line=16))
                output.write('\n')
                output.write(
                    _c_array_constant_definition('unsigned char',
                                                 f'{name}_public_key',
                                                 _create_hex_list(
                                                     cert.public_key.dump()),
                                                 max_items_per_line=16))
                output.write('\n')

            output.write(
                _c_array_constant_definition('unsigned char',
                                             f'{name}_certificate',
                                             _create_hex_list(cert.dump()),
                                             max_items_per_line=16))
            output.write('\n\n')

        if full_cert:
            output.write(
                _c_array_constant_definition(
                    'unsigned char* const', 'kSSLCertSubjectNameList',
                    [f'{name}_subject_name' for name in names]))
            output.write('\n\n')

            output.write(
                _c_array_constant_definition(
                    'unsigned char* const', 'kSSLCertPublicKeyList',
                    [f'{name}_public_key' for name in names]))
            output.write('\n\n')

        output.write(
            _c_array_constant_definition(
                'unsigned char* const', 'kSSLCertCertificateList',
                [f'{name}_certificate' for name in names]))
        output.write('\n\n')

        output.write(
            _c_array_constant_definition(
                'size_t', 'kSSLCertCertificateSizeList',
                [f'{len(cert.dump())}' for cert, _ in named_certificates]))
        output.write('\n\n')

        output.write(_create_output_footer())


def _create_hex_list(items: ByteString) -> List[str]:
    """
  Produces list of strings each item is hex literal of byte of source sequence
  """
    return [f'0x{item:02X}' for item in items]


def _c_array_constant_definition(type_name: str,
                                 array_name: str,
                                 items: List[Any],
                                 max_items_per_line: int = 1) -> str:
    """
  Produces C array definition like: `const type_name array_name = { items };`
  """
    return (f'const {type_name} {array_name}[{len(items)}]='
            f'{_c_array_initializer_list(items, max_items_per_line)};')


def _c_array_initializer_list(items: List[Any],
                              max_items_per_line: int = 1) -> str:
    """
  Produces C initializer list like: `{\\nitems[0], \\n ...}`
  """
    return '{\n' + '\n'.join([
        ','.join(items[i:i + max_items_per_line]) + ','
        for i in range(0, len(items), max_items_per_line)
    ]) + '\n}'


def _create_certificate_metadata_header(cert: x509.Certificate) -> str:
    return (f'/* subject: {cert.subject.human_friendly} */\n'
            f'/* issuer: {cert.issuer.human_friendly} */\n'
            f'/* link: https://crt.sh/?q={cert.sha256.hex()} */\n')


def _create_output_header(source_path_or_url: str, source_digest: str) -> str:
    now_utc = datetime.now(timezone.utc).replace(microsecond=0)
    output = (
        '/*\n'
        f' *  Copyright {now_utc.year} The WebRTC Project Authors. All rights '
        'reserved.\n'
        ' *\n'
        ' *  Use of this source code is governed by a BSD-style license\n'
        ' *  that can be found in the LICENSE file in the root of the '
        'source\n'
        ' *  tree. An additional intellectual property rights grant can be '
        'found\n'
        ' *  in the file PATENTS.  All contributing project authors may\n'
        ' *  be found in the AUTHORS file in the root of the source tree.\n'
        ' */\n\n'
        '#ifndef RTC_BASE_SSL_ROOTS_H_\n'
        '#define RTC_BASE_SSL_ROOTS_H_\n\n'
        '#include <cstddef>\n\n'
        '// This file is the root certificates in C form.\n\n'
        f'// It was generated at {now_utc.isoformat()} '
        'by the following script:\n'
        '// `tools_webrtc/sslroots/generate_sslroots.py '
        f'{source_path_or_url}`\n\n'
        '// clang-format off\n'
        '// Don\'t bother formatting generated code,\n'
        '// also it would breaks subject/issuer lines.\n\n'
        f'// Source bundle `{source_path_or_url}` '
        f'digest is [{source_digest}]\n\n')
    return output


def _create_output_footer():
    return '// clang-format on\n\n#endif  // RTC_BASE_SSL_ROOTS_H_\n'


if __name__ == '__main__':
    main()
