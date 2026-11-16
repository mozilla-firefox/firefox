# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import time
import unittest
from unittest import mock

from mozunit import main

from mozbuild.controller.building import BuildMonitor, CCacheStats

TIMESTAMP = time.time()
TIMESTAMP2 = time.time() + 10
TIMESTAMP_STR = time.strftime("%c", time.localtime(TIMESTAMP))
TIMESTAMP2_STR = time.strftime("%c", time.localtime(TIMESTAMP2))


class TestCcacheStats(unittest.TestCase):
    STAT_GARBAGE = """A garbage line which should be failed to parse"""

    STAT0 = """
    cache directory                     /home/tlin/.ccache
    cache hit (direct)                     0
    cache hit (preprocessed)               0
    cache miss                             0
    files in cache                         0
    cache size                             0 Kbytes
    max cache size                      16.0 Gbytes"""

    STAT1 = """
    cache directory                     /home/tlin/.ccache
    cache hit (direct)                   100
    cache hit (preprocessed)             200
    cache miss                          2500
    called for link                      180
    called for preprocessing               6
    compile failed                        11
    preprocessor error                     3
    bad compiler arguments                 6
    unsupported source language            9
    autoconf compile/link                 60
    unsupported compiler option            2
    no input file                         21
    files in cache                      7344
    cache size                           1.9 Gbytes
    max cache size                      16.0 Gbytes"""

    STAT2 = """
    cache directory                     /home/tlin/.ccache
    cache hit (direct)                  1900
    cache hit (preprocessed)             300
    cache miss                          2600
    called for link                      361
    called for preprocessing              12
    compile failed                        22
    preprocessor error                     6
    bad compiler arguments                12
    unsupported source language           18
    autoconf compile/link                120
    unsupported compiler option            4
    no input file                         48
    files in cache                      7392
    cache size                           2.0 Gbytes
    max cache size                      16.0 Gbytes"""

    STAT3 = """
    cache directory                     /Users/tlin/.ccache
    primary config                      /Users/tlin/.ccache/ccache.conf
    secondary config      (readonly)    /usr/local/Cellar/ccache/3.2/etc/ccache.conf
    cache hit (direct)                 12004
    cache hit (preprocessed)            1786
    cache miss                         26348
    called for link                     2338
    called for preprocessing            6313
    compile failed                       399
    preprocessor error                   390
    bad compiler arguments                86
    unsupported source language           66
    autoconf compile/link               2439
    unsupported compiler option          187
    no input file                       1068
    files in cache                     18044
    cache size                           7.5 GB
    max cache size                       8.6 GB
    """

    STAT4 = """
    cache directory                     /Users/tlin/.ccache
    primary config                      /Users/tlin/.ccache/ccache.conf
    secondary config      (readonly)    /usr/local/Cellar/ccache/3.2.1/etc/ccache.conf
    cache hit (direct)                 21039
    cache hit (preprocessed)            2315
    cache miss                         39370
    called for link                     3651
    called for preprocessing            6693
    compile failed                       723
    ccache internal error                  1
    preprocessor error                   588
    bad compiler arguments               128
    unsupported source language           99
    autoconf compile/link               3669
    unsupported compiler option          187
    no input file                       1711
    files in cache                     18313
    cache size                           6.3 GB
    max cache size                       6.0 GB
    """

    STAT5 = """
    cache directory                     /Users/tlin/.ccache
    primary config                      /Users/tlin/.ccache/ccache.conf
    secondary config      (readonly)    /usr/local/Cellar/ccache/3.2.1/etc/ccache.conf
    cache hit (direct)                 21039
    cache hit (preprocessed)            2315
    cache miss                         39372
    called for link                     3653
    called for preprocessing            6693
    compile failed                       723
    ccache internal error                  1
    preprocessor error                   588
    bad compiler arguments               128
    unsupported source language           99
    autoconf compile/link               3669
    unsupported compiler option          187
    no input file                       1711
    files in cache                     17411
    cache size                           6.0 GB
    max cache size                       6.0 GB
    """

    STAT6 = """
    cache directory                     /Users/tlin/.ccache
    primary config                      /Users/tlin/.ccache/ccache.conf
    secondary config      (readonly)    /usr/local/Cellar/ccache/3.3.2/etc/ccache.conf
    cache hit (direct)                319287
    cache hit (preprocessed)          125987
    cache miss                        749959
    cache hit rate                     37.25 %
    called for link                    87978
    called for preprocessing          418591
    multiple source files               1861
    compiler produced no output          122
    compiler produced empty output       174
    compile failed                     14330
    ccache internal error                  1
    preprocessor error                  9459
    can't use precompiled header           4
    bad compiler arguments              2077
    unsupported source language        18195
    autoconf compile/link              51485
    unsupported compiler option          322
    no input file                     309538
    cleanups performed                     1
    files in cache                     17358
    cache size                          15.4 GB
    max cache size                      17.2 GB
    """

    STAT7 = """
    cache directory                     /Users/tlin/.ccache
    primary config                      /Users/tlin/.ccache/ccache.conf
    secondary config      (readonly)    /usr/local/Cellar/ccache/3.3.3/etc/ccache.conf
    cache hit (direct)                 27035
    cache hit (preprocessed)           13939
    cache miss                         62630
    cache hit rate                     39.55 %
    called for link                     1280
    called for preprocessing             736
    compile failed                       550
    preprocessor error                   638
    bad compiler arguments                20
    autoconf compile/link               1751
    unsupported code directive             2
    no input file                       2378
    cleanups performed                  1792
    files in cache                      3479
    cache size                           4.4 GB
    max cache size                       5.0 GB
    """

    # Substitute a locally-generated timestamp because the timestamp format is
    # locale-dependent.
    STAT8 = f"""
    cache directory                     /home/psimonyi/.ccache
    primary config                      /home/psimonyi/.ccache/ccache.conf
    secondary config      (readonly)    /etc/ccache.conf
    stats zero time                     {TIMESTAMP_STR}
    cache hit (direct)                   571
    cache hit (preprocessed)            1203
    cache miss                         11747
    cache hit rate                     13.12 %
    called for link                      623
    called for preprocessing            7194
    compile failed                        32
    preprocessor error                   137
    bad compiler arguments                 4
    autoconf compile/link                348
    no input file                        162
    cleanups performed                    77
    files in cache                     13464
    cache size                           6.2 GB
    max cache size                       7.0 GB
    """

    STAT9 = f"""
    cache directory                     /Users/tlin/.ccache
    primary config                      /Users/tlin/.ccache/ccache.conf
    secondary config      (readonly)    /usr/local/Cellar/ccache/3.5/etc/ccache.conf
    stats updated                       {TIMESTAMP2_STR}
    stats zeroed                        {TIMESTAMP_STR}
    cache hit (direct)                 80147
    cache hit (preprocessed)           21413
    cache miss                        191128
    cache hit rate                     34.70 %
    called for link                     5194
    called for preprocessing            1721
    compile failed                       825
    preprocessor error                  3838
    cache file missing                  4863
    bad compiler arguments                32
    autoconf compile/link               3554
    unsupported code directive             4
    no input file                       5545
    cleanups performed                  3154
    files in cache                     18525
    cache size                          13.4 GB
    max cache size                      15.0 GB
    """

    VERSION_3_5_GIT = """
    ccache version 3.5.1+2_gf5309092_dirty

    Copyright (C) 2002-2007 Andrew Tridgell
    Copyright (C) 2009-2019 Joel Rosdahl

    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free Software
    Foundation; either version 3 of the License, or (at your option) any later
    version.
    """

    VERSION_4_2 = """
    ccache version 4.2.1

    Copyright (C) 2002-2007 Andrew Tridgell
    Copyright (C) 2009-2021 Joel Rosdahl and other contributors

    See <https://ccache.dev/credits.html> for a complete list of contributors.

    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free Software
    Foundation; either version 3 of the License, or (at your option) any later
    version.
    """

    VERSION_4_4 = """
    ccache version 4.4
    Features: file-storage http-storage

    Copyright (C) 2002-2007 Andrew Tridgell
    Copyright (C) 2009-2021 Joel Rosdahl and other contributors

    See <https://ccache.dev/credits.html> for a complete list of contributors.

    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free Software
    Foundation; either version 3 of the License, or (at your option) any later
    version.
    """

    VERSION_4_4_2 = """
    ccache version 4.4.2
    Features: file-storage http-storage

    Copyright (C) 2002-2007 Andrew Tridgell
    Copyright (C) 2009-2021 Joel Rosdahl and other contributors

    See <https://ccache.dev/credits.html> for a complete list of contributors.

    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free Software
    Foundation; either version 3 of the License, or (at your option) any later
    version.
    """

    VERSION_4_5 = """
    ccache version 4.5.1
    Features: file-storage http-storage redis-storage

    Copyright (C) 2002-2007 Andrew Tridgell
    Copyright (C) 2009-2021 Joel Rosdahl and other contributors

    See <https://ccache.dev/credits.html> for a complete list of contributors.

    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free Software
    Foundation; either version 3 of the License, or (at your option) any later
    version.
    """

    STAT10 = f"""\
stats_updated_timestamp\t{int(TIMESTAMP)}
stats_zeroed_timestamp\t0
direct_cache_hit\t197
preprocessed_cache_hit\t719
cache_miss\t8427
called_for_link\t569
called_for_preprocessing\t110
multiple_source_files\t0
compiler_produced_stdout\t0
compiler_produced_no_output\t0
compiler_produced_empty_output\t0
compile_failed\t49
internal_error\t1
preprocessor_error\t90
could_not_use_precompiled_header\t0
could_not_use_modules\t0
could_not_find_compiler\t0
missing_cache_file\t1
bad_compiler_arguments\t6
unsupported_source_language\t0
compiler_check_failed\t0
autoconf_test\t418
unsupported_compiler_option\t0
unsupported_code_directive\t1
output_to_stdout\t0
bad_output_file\t0
no_input_file\t9
error_hashing_extra_file\t0
cleanups_performed\t161
files_in_cache\t4425
cache_size_kibibyte\t4624220
"""

    STAT11 = f"""\
stats_updated_timestamp\t{int(TIMESTAMP)}
stats_zeroed_timestamp\t{int(TIMESTAMP2)}
direct_cache_hit\t0
preprocessed_cache_hit\t0
cache_miss\t0
called_for_link\t0
called_for_preprocessing\t0
multiple_source_files\t0
compiler_produced_stdout\t0
compiler_produced_no_output\t0
compiler_produced_empty_output\t0
compile_failed\t0
internal_error\t0
preprocessor_error\t0
could_not_use_precompiled_header\t0
could_not_use_modules\t0
could_not_find_compiler\t0
missing_cache_file\t0
bad_compiler_arguments\t0
unsupported_source_language\t0
compiler_check_failed\t0
autoconf_test\t0
unsupported_compiler_option\t0
unsupported_code_directive\t0
output_to_stdout\t0
bad_output_file\t0
no_input_file\t0
error_hashing_extra_file\t0
cleanups_performed\t16
files_in_cache\t0
cache_size_kibibyte\t0
"""

    STAT12 = """\
stats_updated_timestamp\t0
stats_zeroed_timestamp\t0
direct_cache_hit\t0
preprocessed_cache_hit\t0
cache_miss\t0
called_for_link\t0
called_for_preprocessing\t0
multiple_source_files\t0
compiler_produced_stdout\t0
compiler_produced_no_output\t0
compiler_produced_empty_output\t0
compile_failed\t0
internal_error\t0
preprocessor_error\t0
could_not_use_precompiled_header\t0
could_not_use_modules\t0
could_not_find_compiler\t0
missing_cache_file\t0
bad_compiler_arguments\t0
unsupported_source_language\t0
compiler_check_failed\t0
autoconf_test\t0
unsupported_compiler_option\t0
unsupported_code_directive\t0
output_to_stdout\t0
bad_output_file\t0
no_input_file\t0
error_hashing_extra_file\t0
cleanups_performed\t16
files_in_cache\t0
cache_size_kibibyte\t0
"""

    STAT13 = f"""\
stats_updated_timestamp\t{int(TIMESTAMP)}
stats_zeroed_timestamp\t{int(TIMESTAMP2)}
direct_cache_hit\t280542
preprocessed_cache_hit\t0
cache_miss\t387653
called_for_link\t0
called_for_preprocessing\t0
multiple_source_files\t0
compiler_produced_stdout\t0
compiler_produced_no_output\t0
compiler_produced_empty_output\t0
compile_failed\t1665
internal_error\t1
preprocessor_error\t0
could_not_use_precompiled_header\t0
could_not_use_modules\t0
could_not_find_compiler\t0
missing_cache_file\t0
bad_compiler_arguments\t0
unsupported_source_language\t0
compiler_check_failed\t0
autoconf_test\t0
unsupported_compiler_option\t0
unsupported_code_directive\t0
output_to_stdout\t0
bad_output_file\t0
no_input_file\t2
error_hashing_extra_file\t0
cleanups_performed\t364
files_in_cache\t335104
cache_size_kibibyte\t18224250
"""

    maxDiff = None

    def test_parse_garbage_stats_message(self):
        self.assertRaises(ValueError, CCacheStats, self.STAT_GARBAGE)

    def test_parse_zero_stats_message(self):
        stats = CCacheStats(self.STAT0)
        self.assertEqual(stats.hit_rates(), (0, 0, 0))

    def test_hit_rate_of_diff_stats(self):
        stats1 = CCacheStats(self.STAT1)
        stats2 = CCacheStats(self.STAT2)
        stats_diff = stats2 - stats1
        self.assertEqual(stats_diff.hit_rates(), (0.9, 0.05, 0.05))

    def test_stats_contains_data(self):
        stats0 = CCacheStats(self.STAT0)
        stats1 = CCacheStats(self.STAT1)
        stats2 = CCacheStats(self.STAT2)
        stats_diff_zero = stats1 - stats1
        stats_diff_negative1 = stats0 - stats1
        stats_diff_negative2 = stats1 - stats2

        self.assertFalse(stats0)
        self.assertTrue(stats1)
        self.assertTrue(stats2)
        self.assertFalse(stats_diff_zero)
        self.assertFalse(stats_diff_negative1)
        self.assertFalse(stats_diff_negative2)

    def test_stats_version32(self):
        stat2 = CCacheStats(self.STAT2)
        stat3 = CCacheStats(self.STAT3)
        stats_diff = stat3 - stat2
        self.assertEqual(
            str(stat3),
            "cache hit (direct)                   12004\n"
            "cache hit (preprocessed)              1786\n"
            "cache miss                           26348\n"
            "called for link                       2338\n"
            "called for preprocessing              6313\n"
            "compile failed                         399\n"
            "preprocessor error                     390\n"
            "bad compiler arguments                  86\n"
            "unsupported source language             66\n"
            "autoconf compile/link                 2439\n"
            "unsupported compiler option            187\n"
            "no input file                         1068\n"
            "files in cache                       18044\n"
            "cache size                             7.5 Gbytes\n"
            "max cache size                         8.6 Gbytes",
        )
        self.assertEqual(
            str(stats_diff),
            "cache hit (direct)                   10104\n"
            "cache hit (preprocessed)              1486\n"
            "cache miss                           23748\n"
            "called for link                       1977\n"
            "called for preprocessing              6301\n"
            "compile failed                         377\n"
            "preprocessor error                     384\n"
            "bad compiler arguments                  74\n"
            "unsupported source language             48\n"
            "autoconf compile/link                 2319\n"
            "unsupported compiler option            183\n"
            "no input file                         1020\n"
            "files in cache                       18044\n"
            "cache size                             7.5 Gbytes\n"
            "max cache size                         8.6 Gbytes",
        )

    def test_cache_size_shrinking(self):
        stat4 = CCacheStats(self.STAT4)
        stat5 = CCacheStats(self.STAT5)
        stats_diff = stat5 - stat4
        self.assertEqual(
            str(stat4),
            "cache hit (direct)                   21039\n"
            "cache hit (preprocessed)              2315\n"
            "cache miss                           39370\n"
            "called for link                       3651\n"
            "called for preprocessing              6693\n"
            "compile failed                         723\n"
            "ccache internal error                    1\n"
            "preprocessor error                     588\n"
            "bad compiler arguments                 128\n"
            "unsupported source language             99\n"
            "autoconf compile/link                 3669\n"
            "unsupported compiler option            187\n"
            "no input file                         1711\n"
            "files in cache                       18313\n"
            "cache size                             6.3 Gbytes\n"
            "max cache size                         6.0 Gbytes",
        )
        self.assertEqual(
            str(stat5),
            "cache hit (direct)                   21039\n"
            "cache hit (preprocessed)              2315\n"
            "cache miss                           39372\n"
            "called for link                       3653\n"
            "called for preprocessing              6693\n"
            "compile failed                         723\n"
            "ccache internal error                    1\n"
            "preprocessor error                     588\n"
            "bad compiler arguments                 128\n"
            "unsupported source language             99\n"
            "autoconf compile/link                 3669\n"
            "unsupported compiler option            187\n"
            "no input file                         1711\n"
            "files in cache                       17411\n"
            "cache size                             6.0 Gbytes\n"
            "max cache size                         6.0 Gbytes",
        )
        self.assertEqual(
            str(stats_diff),
            "cache hit (direct)                       0\n"
            "cache hit (preprocessed)                 0\n"
            "cache miss                               2\n"
            "called for link                          2\n"
            "called for preprocessing                 0\n"
            "compile failed                           0\n"
            "ccache internal error                    0\n"
            "preprocessor error                       0\n"
            "bad compiler arguments                   0\n"
            "unsupported source language              0\n"
            "autoconf compile/link                    0\n"
            "unsupported compiler option              0\n"
            "no input file                            0\n"
            "files in cache                       17411\n"
            "cache size                             6.0 Gbytes\n"
            "max cache size                         6.0 Gbytes",
        )

    def test_stats_version33(self):
        # Test stats for 3.3.2.
        stat3 = CCacheStats(self.STAT3)
        stat6 = CCacheStats(self.STAT6)
        stats_diff = stat6 - stat3
        self.assertEqual(
            str(stat6),
            "cache hit (direct)                  319287\n"
            "cache hit (preprocessed)            125987\n"
            "cache hit rate                          37\n"
            "cache miss                          749959\n"
            "called for link                      87978\n"
            "called for preprocessing            418591\n"
            "multiple source files                 1861\n"
            "compiler produced no output            122\n"
            "compiler produced empty output         174\n"
            "compile failed                       14330\n"
            "ccache internal error                    1\n"
            "preprocessor error                    9459\n"
            "can't use precompiled header             4\n"
            "bad compiler arguments                2077\n"
            "unsupported source language          18195\n"
            "autoconf compile/link                51485\n"
            "unsupported compiler option            322\n"
            "no input file                       309538\n"
            "cleanups performed                       1\n"
            "files in cache                       17358\n"
            "cache size                            15.4 Gbytes\n"
            "max cache size                        17.2 Gbytes",
        )
        self.assertEqual(
            str(stat3),
            "cache hit (direct)                   12004\n"
            "cache hit (preprocessed)              1786\n"
            "cache miss                           26348\n"
            "called for link                       2338\n"
            "called for preprocessing              6313\n"
            "compile failed                         399\n"
            "preprocessor error                     390\n"
            "bad compiler arguments                  86\n"
            "unsupported source language             66\n"
            "autoconf compile/link                 2439\n"
            "unsupported compiler option            187\n"
            "no input file                         1068\n"
            "files in cache                       18044\n"
            "cache size                             7.5 Gbytes\n"
            "max cache size                         8.6 Gbytes",
        )
        self.assertEqual(
            str(stats_diff),
            "cache hit (direct)                  307283\n"
            "cache hit (preprocessed)            124201\n"
            "cache hit rate                          37\n"
            "cache miss                          723611\n"
            "called for link                      85640\n"
            "called for preprocessing            412278\n"
            "multiple source files                 1861\n"
            "compiler produced no output            122\n"
            "compiler produced empty output         174\n"
            "compile failed                       13931\n"
            "ccache internal error                    1\n"
            "preprocessor error                    9069\n"
            "can't use precompiled header             4\n"
            "bad compiler arguments                1991\n"
            "unsupported source language          18129\n"
            "autoconf compile/link                49046\n"
            "unsupported compiler option            135\n"
            "no input file                       308470\n"
            "cleanups performed                       1\n"
            "files in cache                       17358\n"
            "cache size                            15.4 Gbytes\n"
            "max cache size                        17.2 Gbytes",
        )

        # Test stats for 3.3.3.
        stat7 = CCacheStats(self.STAT7)
        self.assertEqual(
            str(stat7),
            "cache hit (direct)                   27035\n"
            "cache hit (preprocessed)             13939\n"
            "cache hit rate                          39\n"
            "cache miss                           62630\n"
            "called for link                       1280\n"
            "called for preprocessing               736\n"
            "compile failed                         550\n"
            "preprocessor error                     638\n"
            "bad compiler arguments                  20\n"
            "autoconf compile/link                 1751\n"
            "unsupported code directive               2\n"
            "no input file                         2378\n"
            "cleanups performed                    1792\n"
            "files in cache                        3479\n"
            "cache size                             4.4 Gbytes\n"
            "max cache size                         5.0 Gbytes",
        )

    def test_stats_version34(self):
        # Test parsing 3.4 output.
        stat8 = CCacheStats(self.STAT8)
        self.assertEqual(
            str(stat8),
            f"stats zeroed                      {int(TIMESTAMP)}\n"
            "cache hit (direct)                     571\n"
            "cache hit (preprocessed)              1203\n"
            "cache hit rate                          13\n"
            "cache miss                           11747\n"
            "called for link                        623\n"
            "called for preprocessing              7194\n"
            "compile failed                          32\n"
            "preprocessor error                     137\n"
            "bad compiler arguments                   4\n"
            "autoconf compile/link                  348\n"
            "no input file                          162\n"
            "cleanups performed                      77\n"
            "files in cache                       13464\n"
            "cache size                             6.2 Gbytes\n"
            "max cache size                         7.0 Gbytes",
        )

    def test_stats_version35(self):
        # Test parsing 3.5 output.
        stat9 = CCacheStats(self.STAT9)
        self.assertEqual(
            str(stat9),
            f"stats zeroed                      {int(TIMESTAMP)}\n"
            f"stats updated                     {int(TIMESTAMP2)}\n"
            "cache hit (direct)                   80147\n"
            "cache hit (preprocessed)             21413\n"
            "cache hit rate                          34\n"
            "cache miss                          191128\n"
            "called for link                       5194\n"
            "called for preprocessing              1721\n"
            "compile failed                         825\n"
            "preprocessor error                    3838\n"
            "cache file missing                    4863\n"
            "bad compiler arguments                  32\n"
            "autoconf compile/link                 3554\n"
            "unsupported code directive               4\n"
            "no input file                         5545\n"
            "cleanups performed                    3154\n"
            "files in cache                       18525\n"
            "cache size                            13.4 Gbytes\n"
            "max cache size                        15.0 Gbytes",
        )

    def test_stats_version37(self):
        # verify version checks
        self.assertFalse(CCacheStats._is_version_3_7_or_newer(self.VERSION_3_5_GIT))
        self.assertTrue(CCacheStats._is_version_3_7_or_newer(self.VERSION_4_2))
        self.assertTrue(CCacheStats._is_version_3_7_or_newer(self.VERSION_4_4))
        self.assertTrue(CCacheStats._is_version_3_7_or_newer(self.VERSION_4_4_2))
        self.assertTrue(CCacheStats._is_version_3_7_or_newer(self.VERSION_4_5))

        # Test parsing 3.7+ output.
        stat10 = CCacheStats(self.STAT10, True)
        self.assertEqual(
            str(stat10),
            "stats zeroed                             0\n"
            f"stats updated                     {int(TIMESTAMP)}\n"
            "cache hit (direct)                     197\n"
            "cache hit (preprocessed)               719\n"
            "cache hit rate                           9\n"
            "cache miss                            8427\n"
            "called for link                        569\n"
            "called for preprocessing               110\n"
            "multiple source files                    0\n"
            "compiler produced stdout                 0\n"
            "compiler produced no output              0\n"
            "compiler produced empty output           0\n"
            "compile failed                          49\n"
            "ccache internal error                    1\n"
            "preprocessor error                      90\n"
            "can't use precompiled header             0\n"
            "couldn't find the compiler               0\n"
            "cache file missing                       1\n"
            "bad compiler arguments                   6\n"
            "unsupported source language              0\n"
            "compiler check failed                    0\n"
            "autoconf compile/link                  418\n"
            "unsupported code directive               1\n"
            "unsupported compiler option              0\n"
            "output to stdout                         0\n"
            "no input file                            9\n"
            "error hashing extra file                 0\n"
            "cleanups performed                     161\n"
            "files in cache                        4425\n"
            "cache size                             4.4 Gbytes",
        )

        stat11 = CCacheStats(self.STAT11, True)
        self.assertEqual(
            str(stat11),
            f"stats zeroed                      {int(TIMESTAMP2)}\n"
            f"stats updated                     {int(TIMESTAMP)}\n"
            "cache hit (direct)                       0\n"
            "cache hit (preprocessed)                 0\n"
            "cache hit rate                           0\n"
            "cache miss                               0\n"
            "called for link                          0\n"
            "called for preprocessing                 0\n"
            "multiple source files                    0\n"
            "compiler produced stdout                 0\n"
            "compiler produced no output              0\n"
            "compiler produced empty output           0\n"
            "compile failed                           0\n"
            "ccache internal error                    0\n"
            "preprocessor error                       0\n"
            "can't use precompiled header             0\n"
            "couldn't find the compiler               0\n"
            "cache file missing                       0\n"
            "bad compiler arguments                   0\n"
            "unsupported source language              0\n"
            "compiler check failed                    0\n"
            "autoconf compile/link                    0\n"
            "unsupported code directive               0\n"
            "unsupported compiler option              0\n"
            "output to stdout                         0\n"
            "no input file                            0\n"
            "error hashing extra file                 0\n"
            "cleanups performed                      16\n"
            "files in cache                           0\n"
            "cache size                             0.0 Kbytes",
        )

        stat12 = CCacheStats(self.STAT12, True)
        self.assertEqual(
            str(stat12),
            "stats zeroed                             0\n"
            "stats updated                            0\n"
            "cache hit (direct)                       0\n"
            "cache hit (preprocessed)                 0\n"
            "cache hit rate                           0\n"
            "cache miss                               0\n"
            "called for link                          0\n"
            "called for preprocessing                 0\n"
            "multiple source files                    0\n"
            "compiler produced stdout                 0\n"
            "compiler produced no output              0\n"
            "compiler produced empty output           0\n"
            "compile failed                           0\n"
            "ccache internal error                    0\n"
            "preprocessor error                       0\n"
            "can't use precompiled header             0\n"
            "couldn't find the compiler               0\n"
            "cache file missing                       0\n"
            "bad compiler arguments                   0\n"
            "unsupported source language              0\n"
            "compiler check failed                    0\n"
            "autoconf compile/link                    0\n"
            "unsupported code directive               0\n"
            "unsupported compiler option              0\n"
            "output to stdout                         0\n"
            "no input file                            0\n"
            "error hashing extra file                 0\n"
            "cleanups performed                      16\n"
            "files in cache                           0\n"
            "cache size                             0.0 Kbytes",
        )

        stat13 = CCacheStats(self.STAT13, True)
        self.assertEqual(
            str(stat13),
            f"stats zeroed                      {int(TIMESTAMP2)}\n"
            f"stats updated                     {int(TIMESTAMP)}\n"
            "cache hit (direct)                  280542\n"
            "cache hit (preprocessed)                 0\n"
            "cache hit rate                          41\n"
            "cache miss                          387653\n"
            "called for link                          0\n"
            "called for preprocessing                 0\n"
            "multiple source files                    0\n"
            "compiler produced stdout                 0\n"
            "compiler produced no output              0\n"
            "compiler produced empty output           0\n"
            "compile failed                        1665\n"
            "ccache internal error                    1\n"
            "preprocessor error                       0\n"
            "can't use precompiled header             0\n"
            "couldn't find the compiler               0\n"
            "cache file missing                       0\n"
            "bad compiler arguments                   0\n"
            "unsupported source language              0\n"
            "compiler check failed                    0\n"
            "autoconf compile/link                    0\n"
            "unsupported code directive               0\n"
            "unsupported compiler option              0\n"
            "output to stdout                         0\n"
            "no input file                            2\n"
            "error hashing extra file                 0\n"
            "cleanups performed                     364\n"
            "files in cache                      335104\n"
            "cache size                            17.4 Gbytes",
        )

    def test_stats_read_from_configured_cache_dir(self):
        with mock.patch.object(
            CCacheStats, "check_version_3_7_or_newer", return_value=False
        ), mock.patch(
            "subprocess.check_output", return_value=self.STAT0
        ) as check_output, mock.patch.dict(
            "os.environ", {"PATH": "/usr/bin"}, clear=True
        ):
            BuildMonitor.ccache_stats(mock.Mock(), "ccache", "/state/cache/ccache")
        self.assertEqual(
            check_output.call_args.kwargs["env"],
            {"PATH": "/usr/bin", "CCACHE_DIR": "/state/cache/ccache"},
        )


if __name__ == "__main__":
    main()
