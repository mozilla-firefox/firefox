#! /bin/bash
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

########################################################################
#
# mozilla/security/nss/tests/ssl/ssl.sh
#
# Script to test NSS SSL
#
# Needs to work on all Unix and Windows platforms
#
# Testing schema:
# ---------------
#                           all.sh                       ~  (main)
#                              |                               |
#          +------------+------------+-----------+       ~  run_cycles
#          |            |            |           |             |
#      standard       pkix       upgradedb     sharedb   ~  run_cycle_*
#         ...           |           ...         ...            |
#                +------+------+----->                   ~  run_tests
#                |      |      |                               |
#               ...    ssl    ...                        ~   ssl.sh
#                       |                                      |
#          +-------+-------+-----------------+           ~  ssl_run_tests
#          |       |       |                 |                 |
#         crl     iopr   policy    permute(normal,fips)  ~  ssl_run_test_*
#                                         | | | |              |
#         +------+------+------+------+---+-+-+-+---->   ~  ssl_run
#         |      |      |      |      |      |                 |
#    stapling   cov   auth  stress  dtls    ...          ~  ssl_run_*
#
# special strings
# ---------------
#   FIXME ... known problems, search for this string
#   NOTE .... unexpected behavior
#
########################################################################

############################## ssl_init ################################
# local shell function to initialize this script
########################################################################
ssl_init()
{
  SCRIPTNAME=ssl.sh      # sourced - $0 would point to all.sh

  if [ -z "${CLEANUP}" ] ; then     # if nobody else is responsible for
      CLEANUP="${SCRIPTNAME}"       # cleaning this script will do it
  fi

  if [ -z "${INIT_SOURCED}" -o "${INIT_SOURCED}" != "TRUE" ]; then
      cd ../common
      . ./init.sh
  fi
  if [ -z "${IOPR_SSL_SOURCED}" ]; then
      . ../iopr/ssl_iopr.sh
  fi
  if [ ! -r $CERT_LOG_FILE ]; then  # we need certificates here
      cd ../cert
      . ./cert.sh
  fi
  SCRIPTNAME=ssl.sh
  echo "$SCRIPTNAME: SSL tests ==============================="

  grep "SUCCESS: SSL passed" $CERT_LOG_FILE >/dev/null || {
      html_head "SSL Test failure"
      Exit 8 "Fatal - cert.sh needs to pass first"
  }

  if [ -z "$NSS_TEST_DISABLE_CRL" ] ; then
      grep "SUCCESS: SSL CRL prep passed" $CERT_LOG_FILE >/dev/null || {
          html_head "SSL Test failure"
          Exit 8 "Fatal - SSL of cert.sh needs to pass first"
      }
  fi

  PORT=${PORT-8443}
  # Avoid port conflicts when multiple tests are running on the same machine.
  if [ -n "$NSS_TASKCLUSTER_MAC" ]; then
    cwd=$(cd $(dirname $0); pwd -P)
    padd=$(echo $cwd | cut -d "/" -f4 | sed 's/[^0-9]//g')
    PORT=$(($PORT + $padd))
  fi

  # Check if the port is already in use before starting tests.
  if command -v ss > /dev/null 2>&1; then
    if ss -tln 2>/dev/null | grep -q ":${PORT} "; then
      echo "$SCRIPTNAME: ERROR: Port ${PORT} is already in use." >&2
      echo "  Set a different port with: PORT=9443 ./all.sh" >&2
      Exit 10 "Port ${PORT} is already in use"
    fi
  fi

  NSS_SSL_TESTS=${NSS_SSL_TESTS:-normal_normal}
  nss_ssl_run="stapling signed_cert_timestamps cov auth dtls scheme exporter"
  NSS_SSL_RUN=${NSS_SSL_RUN:-$nss_ssl_run}

  # Test case files
  SSLCOV=${QADIR}/ssl/sslcov.txt
  SSLAUTH=${QADIR}/ssl/sslauth.txt
  SSLSTRESS=${QADIR}/ssl/sslstress.txt
  SSLPOLICY=${QADIR}/ssl/sslpolicy.txt
  REQUEST_FILE=${QADIR}/ssl/sslreq.dat

  #temparary files
  SERVEROUTFILE=${TMP}/tests_server.$$
  SERVERPID=${TMP}/tests_pid.$$

  R_SERVERPID=../tests_pid.$$

  TEMPFILES="$TMPFILES ${SERVEROUTFILE}  ${SERVERPID}"

  fileout=0 #FIXME, looks like all.sh tried to turn this on but actually didn't
  #fileout=1
  #verbose="-v" #FIXME - see where this is usefull

  USER_NICKNAME=TestUser
  NORM_EXT=""

  EC_SUITES=":C001:C002:C003:C004:C005:C006:C007:C008:C009:C00A:C00B:C00C:C00D"
  EC_SUITES="${EC_SUITES}:C00E:C00F:C010:C011:C012:C013:C014:C023:C024:C027"
  EC_SUITES="${EC_SUITES}:C028:C02B:C02C:C02F:C030:CCA8:CCA9:CCAA"

  NON_EC_SUITES=":0016:0032:0033:0038:0039:003B:003C:003D:0040:0041:0067:006A:006B"
  NON_EC_SUITES="${NON_EC_SUITES}:0084:009C:009D:009E:009F:00A2:00A3:CCAAcdeinvyz"
  TLS13_SUITES=":1301:1302:1303"

  # List of cipher suites to test, including ECC cipher suites.
  CIPHER_SUITES="-c ${EC_SUITES}${NON_EC_SUITES}"
  TLS13_CIPHER_SUITES="-c ${TLS13_SUITES}${EC_SUITES}${NON_EC_SUITES}"

  # in fips mode, turn off curve25519 until it's NIST approved
  FIPS_OPTIONS=""
  # in fips mode, turn off curve25519 until it's NIST approved
  ALL_GROUPS="P256,P384,P521,x25519,FF2048,FF3072,FF4096,FF6144,FF8192,x25519mlkem768,secp256r1mlkem768,secp384r1mlkem1024"
  NON_PQ_GROUPS="P256,P384,P521,x25519,FF2048,FF3072,FF4096,FF6144,FF8192"
  FIPS_GROUPS="P256,P384,P521,FF2048,FF3072,FF4096,FF6144,FF8192,x25519mlkem768,secp256r1mlkem768,secp384r1mlkem1024"
  FIPS_NON_PQ_GROUPS="P256,P384,P521,FF2048,FF3072,FF4096,FF6144,FF8192"


  # in non-fips mode, tstclnt may run without the db password in some
  # cases, but in fips mode it's always needed
  CLIENT_PW=""
  CLIENT_PW_FIPS="-w nss"
  CLIENT_PW_NORMAL=""

  if [ "${OS_ARCH}" != "WINNT" ]; then
      ulimit -n 1000 # make sure we have enough file descriptors
  fi

  cd ${CLIENTDIR}
}

########################### is_selfserv_alive ##########################
# local shell function to exit with a fatal error if selfserver is not
# running
########################################################################
is_selfserv_alive()
{
  if [ ! -f "${SERVERPID}" ]; then
      echo "$SCRIPTNAME: Error - selfserv PID file ${SERVERPID} doesn't exist"
      sleep 5
      if [ ! -f "${SERVERPID}" ]; then
          Exit 9 "Fatal - selfserv pid file ${SERVERPID} does not exist"
      fi
  fi

  if [ "${OS_ARCH}" = "WINNT" ] && \
     [ "$OS_NAME" = "CYGWIN_NT" -o "$OS_NAME" = "MINGW32_NT" -o "$OS_NAME" = "MSYS_NT" ]; then
      PID=${SHELL_SERVERPID}
  else
      PID=`cat ${SERVERPID}`
  fi

  echo "kill -0 ${PID} >/dev/null 2>/dev/null"
  kill -0 ${PID} >/dev/null 2>/dev/null || Exit 10 "Fatal - selfserv process not detectable"

  echo "selfserv with PID ${PID} found at `date`"
}

########################### wait_for_selfserv ##########################
# local shell function to wait until selfserver is running and initialized
########################################################################
wait_for_selfserv()
{
  #verbose="-v"
  echo "trying to connect to selfserv at `date`"
  echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} ${CLIENT_OPTIONS} -q \\"
  echo "        -d ${P_R_CLIENTDIR} $verbose < ${REQUEST_FILE}"
  ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} ${CLIENT_OPTIONS} -q \
          -d ${P_R_CLIENTDIR} $verbose < ${REQUEST_FILE}
  if [ $? -ne 0 ]; then
      sleep 5
      echo "retrying to connect to selfserv at `date`"
      echo "tstclnt -p ${PORT} -h ${HOSTADDR} ${CLIENT_OPTIONS} -q \\"
      echo "        -d ${P_R_CLIENTDIR} $verbose < ${REQUEST_FILE}"
      ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} ${CLIENT_OPTIONS} -q \
              -d ${P_R_CLIENTDIR} $verbose < ${REQUEST_FILE}
      if [ $? -ne 0 ]; then
          html_failed "Waiting for Server"
      fi
  fi
  is_selfserv_alive
}

########################### kill_selfserv ##############################
# local shell function to kill the selfserver after the tests are done
########################################################################
kill_selfserv()
{
  PID=`cat ${SERVERPID}`

  echo "trying to kill selfserv with PID ${PID} at `date`"

  safe_kill ${PID} ${SHELL_SERVERPID}
  if [ ${fileout} -eq 1 ]; then
      cat ${SERVEROUTFILE}
  fi

  # On Linux selfserv needs up to 30 seconds to fully die and free
  # the port.  Wait until the port is free. (Bug 129701)
  if [ "${OS_ARCH}" = "Linux" ]; then
      echo "selfserv -b -p ${PORT} 2>/dev/null;"
      until ${BINDIR}/selfserv -b -p ${PORT} 2>/dev/null; do
          echo "RETRY: selfserv -b -p ${PORT} 2>/dev/null;"
          sleep 1
      done
  fi

  echo "selfserv with PID ${PID} killed at `date`"

  rm ${SERVERPID}
  html_detect_core_force "kill_selfserv core detection step"
}

########################### start_selfserv #############################
# local shell function to start the selfserver with the parameters required
# for this test and log information (parameters, start time)
# also: wait until the server is up and running
########################################################################
start_selfserv()
{
  if [ -n "$testname" ] ; then
      echo "$SCRIPTNAME: $testname ----"
  fi
  if [ -z "$NO_ECC_CERTS" -o "$NO_ECC_CERTS" != "1" ] ; then
      ECC_OPTIONS="-e ${HOSTADDR}-ecmixed -e ${HOSTADDR}-ec "
  else
      ECC_OPTIONS=""
  fi
  if [ -z "$NO_ML_DSA_CERTS" -o "$NO_ML_DSA_CERTS" != "1" ] ; then
      ML_DSA_OPTIONS="-e ${HOSTADDR}-ml-dsa-44 -e ${HOSTADDR}-ml-dsa-65 -e ${HOSTADDR}-ml-dsa-87 "
  else
      ML_DSA_OPTIONS=""
  fi
  if [ -z "$RSA_PSS_CERT" -o "$RSA_PSS_CERT" != "1" ] ; then
      RSA_OPTIONS="-n ${HOSTADDR}"
  else
      RSA_OPTIONS="-n ${HOSTADDR}-rsa-pss"
  fi
  if [ -z "$NSS_DISABLE_DSA" ]; then
      DSA_OPTIONS="-S ${HOSTADDR}-dsa "
  else
      DSA_OPTIONS=""
  fi

  SERVER_VMIN=${SERVER_VMIN-ssl3}
  SERVER_VMAX=${SERVER_VMAX-tls1.2}
  echo "selfserv starting at `date`"
  echo "selfserv -D -p ${PORT} -d ${P_R_SERVERDIR} ${RSA_OPTIONS} ${SERVER_OPTIONS} \\"
  echo "         ${ECC_OPTIONS}${DSA_OPTIONS}${ML_DSA_OPTIONS}-w nss "$@" -i ${R_SERVERPID}\\"
  echo "         -V ${SERVER_VMIN}:${SERVER_VMAX} $verbose -H 1 &"
  if [ ${fileout} -eq 1 ]; then
      ${PROFTOOL} ${BINDIR}/selfserv -D -p ${PORT} -d ${P_R_SERVERDIR} ${RSA_OPTIONS} ${SERVER_OPTIONS} \
               ${ECC_OPTIONS}${DSA_OPTIONS}${ML_DSA_OPTIONS}-w nss "$@" -i ${R_SERVERPID} -V ${SERVER_VMIN}:${SERVER_VMAX} $verbose -H 1 \
               > ${SERVEROUTFILE} 2>&1 &
      RET=$?
  else
      ${PROFTOOL} ${BINDIR}/selfserv -D -p ${PORT} -d ${P_R_SERVERDIR} ${RSA_OPTIONS} ${SERVER_OPTIONS} \
               ${ECC_OPTIONS}${DSA_OPTIONS}${ML_DSA_OPTIONS}-w nss "$@" -i ${R_SERVERPID} -V ${SERVER_VMIN}:${SERVER_VMAX} $verbose -H 1 &
      RET=$?
  fi

  # The PID $! returned by the MKS or Cygwin shell is not the PID of
  # the real background process, but rather the PID of a helper
  # process (sh.exe).  MKS's kill command has a bug: invoking kill
  # on the helper process does not terminate the real background
  # process.  Our workaround has been to have selfserv save its PID
  # in the ${SERVERPID} file and "kill" that PID instead.  But this
  # doesn't work under Cygwin; its kill command doesn't recognize
  # the PID of the real background process, but it does work on the
  # PID of the helper process.  So we save the value of $! in the
  # SHELL_SERVERPID variable, and use it instead of the ${SERVERPID}
  # file under Cygwin.  (In fact, this should work in any shell
  # other than the MKS shell.)
  SHELL_SERVERPID=$!
  wait_for_selfserv

  echo "selfserv with PID `cat ${SERVERPID}` started at `date`"
}

############################## ssl_cov #################################
# local shell function to perform SSL Cipher Coverage tests
########################################################################
ssl_cov()
{
  verbose_save=${verbose}
  verbose="-v"
  html_head "SSL Cipher Coverage $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

  testname=""

  SAVE_SERVER_OPTIONS=${SERVER_OPTIONS}
  if [ "${SERVER_MODE}" = "fips" ] ; then
      SERVER_OPTIONS="${SERVER_OPTIONS} -I ${FIPS_GROUPS} ${FIPS_OPTIONS}"
  fi
  SAVE_CLIENT_OPTIONS=${CLIENT_OPTIONS}
  CLIENT_GROUPS=${NON_PQ_GROUPS}
  if [ "${CLIENT_MODE}" = "fips" ] ; then
      CLIENT_GROUPS=${FIPS_NON_PQ_GROUPS}
      CLIENT_OPTIONS="${CLIENT_OPTIONS} ${FIPS_OPTIONS}"
  fi

  start_selfserv $CIPHER_SUITES # Launch the server

  VMIN="ssl3"
  VMAX="tls1.1"
  # can't use a pipe here, because we may have to restart selfserv, and
  # doing so hides the server pid environment variable in the subshell in
  # cygwin, which means we can't kill selfserv at the end here.
  SSL_COV_TMP=$(mktemp /tmp/ssl_cov.XXXXXX)
  ignore_blank_lines ${SSLCOV} > ${SSL_COV_TMP}
  while read ectype testmax param sig testname
  do
      # Select the TLS SIG SCHEME if necessary 
      TLS_SIG_SCHEMES=""
      if [ "$sig" = "RSA-PSS" ]; then
          # RSA-PSS tests are currently handled in a separate function
          continue
      elif [ "$sig" = "ML-DSA-44" ]; then
          TLS_SIG_SCHEMES="-J mldsa44 "
      elif [ "$sig" = "ML-DSA-65" ]; then
          TLS_SIG_SCHEMES="-J mldsa65 "
      elif [ "$sig" = "ML-DSA-87" ]; then
          TLS_SIG_SCHEMES="-J mldsa87 "
      fi


      # skip DSA tests if they are disabled
      if [ -n "$NSS_DISABLE_DSA" -a "$sig" = "DSA" ]; then
          continue
      fi

      echo "$SCRIPTNAME: running $testname ----------------------------"
      VMAX="ssl3"
      if [ "$testmax" = "TLS10" ]; then
          VMAX="tls1.0"
      fi
      if [ "$testmax" = "TLS11" ]; then
          VMAX="tls1.1"
      fi
      if [ "$testmax" = "TLS12" ]; then
          VMAX="tls1.2"
      fi
      if [ "$testmax" = "TLS13" ]; then
          # if our selfserv can only do up to tls1.2
          # restart it so it can do tls1.3, This requires
          # moving VMIN up to tls1.0 because you can't enable SSL3 and
          # TLS 1.3.
          if [ "$VMIN" = "ssl3" ]; then
              SERVER_VMIN="tls1.0"
              SERVER_VMAX="tls1.3"
              kill_selfserv
              start_selfserv ${TLS13_CIPHER_SUITES}
              unset SERVER_VMIN
              unset SERVER_VMAX
              VMIN="tls1.0"
          fi
          VMAX="tls1.3"
      fi
      # if we are testing ssl3 and our server can only do down to tls1.1,
      # restart it to enable ssl3
      if [ "$VMAX" = "ssl3" -a "$VMIN" = "tls1.1" ]; then
              kill_selfserv
              start_selfserv $CIPHER_SUITES
              VMIN="ssl3"
      fi

      TLS_GROUPS=${CLIENT_GROUPS}
      if [ "$ectype" = "MLKEM219" ]; then
          TLS_GROUPS="x25519mlkem768"
      elif [ "$ectype" = "MLKEM256" ]; then
          TLS_GROUPS="secp256r1mlkem768"
      elif [ "$ectype" = "MLKEM384" ]; then
          TLS_GROUPS="secp384r1mlkem1024"
      fi

      echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -c ${param} -I \"${TLS_GROUPS}\" ${TLS_SIG_SCHEMES}-V ${VMIN}:${VMAX} ${CLIENT_OPTIONS} \\"
      echo "        -f -d ${P_R_CLIENTDIR} $verbose -w nss < ${REQUEST_FILE}"

      rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
      ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -c ${param} -I "${TLS_GROUPS}" ${TLS_SIG_SCHEMES}-V ${VMIN}:${VMAX} ${CLIENT_OPTIONS} -f \
              -d ${P_R_CLIENTDIR} $verbose -w nss < ${REQUEST_FILE} \
              >${TMP}/$HOST.tmp.$$  2>&1
      ret=$?
      cat ${TMP}/$HOST.tmp.$$
      rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
      html_msg $ret 0 "${testname}" \
               "produced a returncode of $ret, expected is 0"
  done < ${SSL_COV_TMP}
  rm -f ${SSL_COV_TMP}

  SERVER_OPTIONS=${SAVE_SERVER_OPTIONS}
  CLIENT_OPTIONS=${SAVE_CLIENT_OPTIONS}
  verbose=${verbose_save}
  kill_selfserv
  html "</TABLE><BR>"
}

ssl_cov_rsa_pss()
{
  #verbose="-v"
  html_head "SSL Cipher Coverage (RSA-PSS) $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

  testname=""

  if [ "$NORM_EXT" = "Extended Test" ] ; then
      echo "$SCRIPTNAME: skipping SSL Cipher Coverage (RSA-PSS) for $NORM_EXT"
      return 0
  fi

  RSA_PSS_CERT=1
  NO_ECC_CERTS=1
  start_selfserv $CIPHER_SUITES
  RSA_PSS_CERT=0
  NO_ECC_CERTS=0

  VMIN="tls1.2"
  VMAX="tls1.2"

  ignore_blank_lines ${SSLCOV} | \
  while read ectype testmax param sig testname
  do
      # only PSS testa are handled here.
      if [ "$sig" != "RSA-PSS" ]; then
          continue
      fi

      echo "$SCRIPTNAME: running $testname (RSA-PSS) ----------------------------"

      echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -c ${param} -V ${VMIN}:${VMAX} ${CLIENT_OPTIONS} \\"
      echo "        -f -d ${P_R_CLIENTDIR} $verbose -w nss < ${REQUEST_FILE}"

      rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
      ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -c ${param} -V ${VMIN}:${VMAX} ${CLIENT_OPTIONS} -f \
              -d ${P_R_CLIENTDIR} $verbose -w nss < ${REQUEST_FILE} \
              >${TMP}/$HOST.tmp.$$  2>&1
      ret=$?
      cat ${TMP}/$HOST.tmp.$$
      rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
      html_msg $ret 0 "${testname}" \
               "produced a returncode of $ret, expected is 0"
  done

  kill_selfserv
  html "</TABLE><BR>"
}

############################## ssl_auth ################################
# local shell function to perform SSL  Client Authentication tests
########################################################################
ssl_auth()
{
  #verbose="-v"
  html_head "SSL Client Authentication $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

  ignore_blank_lines ${SSLAUTH} | \
  while read ectype value sparam cparam testname
  do
      echo "${testname}" | grep "don't require client auth" > /dev/null
      CAUTH=$?
      echo "${testname}" | grep "TLS 1.3" > /dev/null
      TLS13=$?

      if [ "${CLIENT_MODE}" = "fips" -a "${CAUTH}" -eq 0 ] ; then
          echo "$SCRIPTNAME: skipping  $testname (non-FIPS only)"
      elif [ "$ectype" = "SNI" -a "$NORM_EXT" = "Extended Test" ] ; then
          echo "$SCRIPTNAME: skipping  $testname for $NORM_EXT"
      else
          cparam=`echo $cparam | sed -e 's;\([^\\]\)_;\1 ;g' -e 's;\\\\_;_;g' -e "s/TestUser/$USER_NICKNAME/g" `
          if [ "$ectype" = "SNI" ]; then
              cparam=`echo $cparam | sed -e "s/Host/$HOST/g" -e "s/Dom/$DOMSUF/g" `
              sparam=`echo $sparam | sed -e "s/Host/$HOST/g" -e "s/Dom/$DOMSUF/g" `
          fi
	  # SSL3 cannot be used with TLS 1.3
	  unset SERVER_VMIN
	  unset SERVER_VMAX
	  if [ $TLS13 -eq 0 ] ; then
	      SERVER_VMIN=tls1.0
	      SERVER_VMAX=tls1.3
	  fi
          start_selfserv `echo "$sparam" | sed -e 's;\([^\\]\)_;\1 ;g' -e 's;\\\\_;_;g'`

          echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f -d ${P_R_CLIENTDIR} $verbose ${CLIENT_OPTIONS} \\"
          echo "        ${cparam}  < ${REQUEST_FILE}"
          rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
          ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f ${cparam} $verbose ${CLIENT_OPTIONS} \
                  -d ${P_R_CLIENTDIR} < ${REQUEST_FILE} \
                  >${TMP}/$HOST.tmp.$$  2>&1
          ret=$?
          cat ${TMP}/$HOST.tmp.$$
          rm ${TMP}/$HOST.tmp.$$ 2>/dev/null

          #workaround for bug #402058
          [ $ret -ne 0 ] && ret=1
          [ $value -ne 0 ] && value=1

          html_msg $ret $value "${testname}" \
                   "produced a returncode of $ret, expected is $value"
          kill_selfserv
      fi
  done

  html "</TABLE><BR>"
}

ssl_stapling_stress()
{
    testname="Stress OCSP stapling, server uses random status"
    SO="-A TestCA -T random"
    value=0

    if [ "$NORM_EXT" = "Extended Test" ] ; then
        # these tests use the ext_client directory for strsclnt,
        # which doesn't contain the required "TestCA" for server cert
        # verification, I don't know if it would be OK to add it...
        echo "$SCRIPTNAME: skipping  $testname for $NORM_EXT"
        return 0
    fi

    SAVE_SERVER_OPTIONS=${SERVER_OPTIONS}
    SERVER_OPTIONS="${SERVER_OPTIONS} ${SO}"

    SAVE_P_R_SERVERDIR=${P_R_SERVERDIR}
    P_R_SERVERDIR=${P_R_SERVERDIR}/../stapling/

    echo "${testname}"
    start_selfserv

    echo "strsclnt -4 -q -p ${PORT} -d ${P_R_CLIENTDIR} ${CLIENT_OPTIONS} -w nss \\"
    echo "         -c 1000 -V ssl3:tls1.2 -N -T $verbose ${HOSTADDR}"
    echo "strsclnt started at `date`"
    ${PROFTOOL} ${BINDIR}/strsclnt -4 -q -p ${PORT} -d ${P_R_CLIENTDIR} ${CLIENT_OPTIONS} -w nss \
            -c 1000 -V ssl3:tls1.2 -N -T $verbose ${HOSTADDR}
    ret=$?

    echo "strsclnt completed at `date`"
    html_msg $ret $value \
            "${testname}" \
            "produced a returncode of $ret, expected is $value."
    kill_selfserv

    SERVER_OPTIONS=${SAVE_SERVER_OPTIONS}
    P_R_SERVERDIR=${SAVE_P_R_SERVERDIR}
}

############################ ssl_stapling ##############################
# local shell function to perform SSL Cert Status (OCSP Stapling) tests
########################################################################
ssl_stapling()
{
  html_head "SSL Cert Status (OCSP Stapling) $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

  ssl_stapling_stress

  html "</TABLE><BR>"
}

############################ ssl_signed_cert_timestamps #################
# local shell function to perform SSL Signed Certificate Timestamp tests
#########################################################################
ssl_signed_cert_timestamps()
{
  #verbose="-v"
  html_head "SSL Signed Certificate Timestamps $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

    testname="ssl_signed_cert_timestamps"
    value=0

    echo "${testname}"

    start_selfserv

    # Since we don't have server-side support, this test only covers advertising the
    # extension in the client hello.
    echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f -d ${P_R_CLIENTDIR} $verbose ${CLIENT_OPTIONS} \\"
    echo "        -U -V tls1.0:tls1.2 ${CLIENT_PW} < ${REQUEST_FILE}"
    rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
    ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f ${CLIENT_OPTIONS} \
            -d ${P_R_CLIENTDIR} $verbose -U -V tls1.0:tls1.2 ${CLIENT_PW} < ${REQUEST_FILE} \
            >${TMP}/$HOST.tmp.$$  2>&1
    ret=$?
    cat ${TMP}/$HOST.tmp.$$
    rm ${TMP}/$HOST.tmp.$$ 2>/dev/null

    html_msg $ret $value "${testname}" \
            "produced a returncode of $ret, expected is $value"
    kill_selfserv
  html "</TABLE><BR>"
}


############################## ssl_stress ##############################
# local shell function to perform SSL stress test
########################################################################
ssl_stress()
{
  html_head "SSL Stress Test $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

  ignore_blank_lines ${SSLSTRESS} | \
  while read ectype value sparam cparam testname
  do
      echo "${testname}" | grep "client auth" > /dev/null
      CAUTH=$?
      echo "${testname}" | grep "no login" > /dev/null
      NOLOGIN=$?
      echo "${testname}" | grep "_DSS_" > /dev/null
      IS_DSA=$?

      if [ "$ectype" = "SNI" -a "$NORM_EXT" = "Extended Test" ] ; then
          echo "$SCRIPTNAME: skipping  $testname for $NORM_EXT"
      elif [ "${CLIENT_MODE}" = "fips" -a "${CAUTH}" -ne 0 ] ; then
          echo "$SCRIPTNAME: skipping  $testname (non-FIPS only)"
      elif [ "${NOLOGIN}" -eq 0 ] && \
           [ "${CLIENT_MODE}" = "fips" -o "$NORM_EXT" = "Extended Test" ] ; then
          echo "$SCRIPTNAME: skipping  $testname for $NORM_EXT"
      elif [ -n "${NSS_DISABLE_DSA}" -a "$IS_DSA" -eq 0 ] ; then
          echo "$SCRIPTNAME: skipping  $testname for $NORM_EXT (DSA is disabled"
      else
          cparam=`echo $cparam | sed -e 's;_; ;g' -e "s/TestUser/$USER_NICKNAME/g" `
          if [ "$ectype" = "SNI" ]; then
              cparam=`echo $cparam | sed -e "s/Host/$HOST/g" -e "s/Dom/$DOMSUF/g" `
              sparam=`echo $sparam | sed -e "s/Host/$HOST/g" -e "s/Dom/$DOMSUF/g" `
          fi

          start_selfserv `echo "$sparam" | sed -e 's,_, ,g'`

          if [ "`uname -n`" = "sjsu" ] ; then
              echo "debugging disappearing selfserv... ps -ef | grep selfserv"
              ps -ef | grep selfserv
          fi

          if [ "${NOLOGIN}" -eq 0 ] ; then
              dbdir=${P_R_NOLOGINDIR}
          else
              dbdir=${P_R_CLIENTDIR}
          fi

          echo "strsclnt -4 -q -p ${PORT} -d ${dbdir} ${CLIENT_OPTIONS} -w nss $cparam \\"
          echo "         -V ssl3:tls1.2 $verbose ${HOSTADDR}"
          echo "strsclnt started at `date`"
          ${PROFTOOL} ${BINDIR}/strsclnt -4 -q -p ${PORT} -d ${dbdir} ${CLIENT_OPTIONS} -w nss $cparam \
                   -V ssl3:tls1.2 $verbose ${HOSTADDR}
          ret=$?
          echo "strsclnt completed at `date`"
          html_msg $ret $value \
                   "${testname}" \
                   "produced a returncode of $ret, expected is $value. "
          if [ "`uname -n`" = "sjsu" ] ; then
              echo "debugging disapering selfserv... ps -ef | grep selfserv"
              ps -ef | grep selfserv
          fi
          kill_selfserv
      fi
  done

  html "</TABLE><BR>"
}

############################ ssl_crl_ssl ###############################
# local shell function to perform SSL test with/out revoked certs tests
########################################################################
ssl_crl_ssl()
{
  #verbose="-v"
  html_head "CRL SSL Client Tests $NORM_EXT"

  # Using First CRL Group for this test. There are $CRL_GRP_1_RANGE certs in it.
  # Cert number $UNREVOKED_CERT_GRP_1 was not revoked
  CRL_GROUP_BEGIN=$CRL_GRP_1_BEGIN
  CRL_GROUP_RANGE=$CRL_GRP_1_RANGE
  UNREVOKED_CERT=$UNREVOKED_CERT_GRP_1

  ignore_blank_lines ${SSLAUTH} | \
  while read ectype value sparam cparam testname
  do
    echo "${testname}" | grep "TLS 1.3" > /dev/null
    TLS13=$?
    if [ "$ectype" = "SNI" ]; then
        continue
    else
        # SSL3 cannot be used with TLS 1.3
        unset SERVER_VMIN
        unset SERVER_VMAX
        if [ $TLS13 -eq 0 ] ; then
            SERVER_VMIN=tls1.0
            SERVER_VMAX=tls1.3
        fi
        servarg=`echo $sparam | awk '{r=split($0,a,"-r") - 1;print r;}'`
        pwd=`echo $cparam | grep nss`
        # did we select TestUser?
        user=`echo $cparam | grep TestUser`
        # did we explicitly select a cert?
        auto=`echo $cparam | grep '\\-n'`
        _cparam=$cparam
        case $servarg in
            1) if [ -z "$pwd" -o -z "$user" ]; then
                 rev_modvalue=0
               else
                 rev_modvalue=254
               fi
               ;;
            2) if [ -z "$auto" ]; then
                 rev_modvalue=0
               else
                 rev_modvalue=254
               fi
               ;;
            3) if [ -z "$pwd" -o -z "$user" ]; then
                rev_modvalue=0
                else
                rev_modvalue=1
                fi
                ;;
            4) if [ -z "$auto" ]; then
                rev_modvalue=0
                else
                rev_modvalue=1
                fi
                ;;
        esac
        TEMP_NUM=0
        while [ $TEMP_NUM -lt $CRL_GROUP_RANGE ]
          do
          CURR_SER_NUM=`expr ${CRL_GROUP_BEGIN} + ${TEMP_NUM}`
          TEMP_NUM=`expr $TEMP_NUM + 1`
          USER_NICKNAME="TestUser${CURR_SER_NUM}"
          cparam=`echo $_cparam | sed -e 's;\([^\\]\)_;\1 ;g' -e 's;\\\\_;_;g' -e "s/TestUser/$USER_NICKNAME/g" `
          start_selfserv `echo "$sparam" | sed -e 's;\([^\\]\)_;\1 ;g' -e 's;\\\\_;_;g'`

          echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f -d ${R_CLIENTDIR} $verbose \\"
          echo "        ${cparam}  < ${REQUEST_FILE}"
          rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
          ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f ${cparam} \
              -d ${R_CLIENTDIR} $verbose < ${REQUEST_FILE} \
              >${TMP}/$HOST.tmp.$$  2>&1
          ret=$?
          cat ${TMP}/$HOST.tmp.$$
          rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
          if [ $CURR_SER_NUM -ne $UNREVOKED_CERT ]; then
              modvalue=$rev_modvalue
              testAddMsg="revoked"
          else
              testAddMsg="not revoked"
              modvalue=$value
          fi

          html_msg $ret $modvalue "${testname} (cert ${USER_NICKNAME} - $testAddMsg)" \
                "produced a returncode of $ret, expected is $modvalue"
          kill_selfserv
        done
    fi
  done

  html "</TABLE><BR>"
}

############################## ssl_policy ##############################
# local shell function to perform SSL Policy tests
########################################################################
ssl_policy()
{
  #verbose="-v"
  html_head "SSL POLICY $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

  testname=""

  if [ ! -f "${P_R_CLIENTDIR}/pkcs11.txt" ] ; then
      html_failed "${SCRIPTNAME}: ${P_R_CLIENTDIR} is not initialized"
      return 1;
  fi

  echo "Saving pkcs11.txt"
  save_pkcs11 ${P_R_CLIENTDIR}

  start_selfserv $CIPHER_SUITES

  ignore_blank_lines ${SSLPOLICY} | \
  while read value ectype testmax param policy testname
  do
      VMIN="ssl3"

      echo "$SCRIPTNAME: running $testname ----------------------------"
      VMAX="ssl3"
      if [ "$testmax" = "TLS10" ]; then
          VMAX="tls1.0"
      fi
      if [ "$testmax" = "TLS11" ]; then
          VMAX="tls1.1"
      fi
      if [ "$testmax" = "TLS12" ]; then
          VMAX="tls1.2"
      fi

      # load the policy
      policy=`echo ${policy} | sed -e 's;_; ;g'`
      setup_policy "$policy" ${P_R_CLIENTDIR}

      echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -c ${param} -V ${VMIN}:${VMAX} ${CLIENT_OPTIONS} \\"
      echo "        -f -d ${P_R_CLIENTDIR} $verbose -w nss < ${REQUEST_FILE}"

      rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
      ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -c ${param} -V ${VMIN}:${VMAX} ${CLIENT_OPTIONS} -f \
              -d ${P_R_CLIENTDIR} $verbose -w nss < ${REQUEST_FILE} \
              >${TMP}/$HOST.tmp.$$  2>&1
      ret=$?
      cat ${TMP}/$HOST.tmp.$$
      rm ${TMP}/$HOST.tmp.$$ 2>/dev/null

      #workaround for bug #402058
      [ $ret -ne 0 ] && ret=1
      [ ${value} -ne 0 ] && value=1

      html_msg $ret ${value} "${testname}" \
               "produced a returncode of $ret, expected is ${value}"
  done
  restore_pkcs11 ${P_R_CLIENTDIR}

  kill_selfserv
  html "</TABLE><BR>"
}

list_enabled_suites()
{
  echo "SSL_DIR=${P_R_CLIENTDIR} ${BINDIR}/listsuites"
  SSL_DIR="${P_R_CLIENTDIR}" ${BINDIR}/listsuites | tail -n+3 | \
      sed -n -e '/^TLS_/h' -e '/^ .*Enabled.*/{g;p}' | sed 's/:$//'
}

############################## ssl_policy_listsuites ###################
# local shell function to perform SSL Policy tests, using listsuites
########################################################################
ssl_policy_listsuites()
{
  #verbose="-v"
  html_head "SSL POLICY LISTSUITES $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

  testname=""

  if [ ! -f "${P_R_CLIENTDIR}/pkcs11.txt" ] ; then
      html_failed "${SCRIPTNAME}: ${P_R_CLIENTDIR} is not initialized"
      return 1;
  fi

  echo "Saving pkcs11.txt"
  cp ${P_R_CLIENTDIR}/pkcs11.txt ${P_R_CLIENTDIR}/pkcs11.txt.sav

  # Disallow all explicitly
  testname="listsuites with all cipher disallowed by policy"
  setup_policy "disallow=all" ${P_R_CLIENTDIR}
  RET_EXP=1
  list_enabled_suites | grep '^TLS_'
  RET=$?
  html_msg $RET $RET_EXP "${testname}" \
           "produced a returncode of $RET, expected is $RET_EXP"

  # Disallow RSA in key exchange explicitly
  testname="listsuites with rsa cipher disallowed by policy"
  setup_policy "disallow=rsa/ssl-key-exchange" ${P_R_CLIENTDIR}
  RET_EXP=1
  list_enabled_suites | grep '^TLS_RSA_'
  RET=$?
  html_msg $RET $RET_EXP "${testname}" \
           "produced a returncode of $RET, expected is $RET_EXP"

  # allow by policy, but disable by default
  testname="listsuites with all ciphers enabled by policy but disabled by default"
  setup_policy "allow=all disable=all" ${P_R_CLIENTDIR}
  RET_EXP=1
  list_enabled_suites | grep '^TLS_'
  RET=$?
  html_msg $RET $RET_EXP "${testname}" \
           "produced a returncode of $RET, expected is $RET_EXP"

  # allow by policy, but disable by default just rsa-kea
  testname="listsuites with all ciphers enabled by policy but rsa disabled by default"
  setup_policy "allow=all disable=rsa/ssl-key-exchange" ${P_R_CLIENTDIR}
  RET_EXP=1
  list_enabled_suites | grep '^TLS_RSA_'
  RET=$?
  html_msg $RET $RET_EXP "${testname}" \
           "produced a returncode of $RET, expected is $RET_EXP"

  # list_enabled_suites tries to set a policy value explicitly, This will
  # cause list_enabled_suites to fail if we lock the policy
  testname="listsuites with policy locked"
  setup_policy "allow=all flags=policy-lock" ${P_R_CLIENTDIR}
  RET_EXP=1
  SSL_DIR="${P_R_CLIENTDIR}" ${BINDIR}/listsuites
  RET=$?
  html_msg $RET $RET_EXP "${testname}" \
           "produced a returncode of $RET, expected is $RET_EXP"

  cp ${P_R_CLIENTDIR}/pkcs11.txt.sav ${P_R_CLIENTDIR}/pkcs11.txt

  html "</TABLE><BR>"
}

ssl_policy_pkix_ocsp()
{
  #verbose="-v"
  html_head "Check that OCSP doesn't break if we disable sha1 $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

  testname=""

  if [ ! -f "${P_R_SERVERDIR}/pkcs11.txt" ] ; then
      html_failed "${SCRIPTNAME}: ${P_R_SERVERDIR} is not initialized"
      return 1;
  fi

  echo "Saving pkcs11.txt"
  cp ${P_R_SERVERDIR}/pkcs11.txt ${P_R_SERVERDIR}/pkcs11.txt.sav

  # Disallow sha1 explicitly. This will test if we are trying to verify the sha1 signature
  # on the GlobalSign root during OCSP processing
  setup_policy "disallow=sha1" ${P_R_SERVERDIR}
  RET_EXP=0
  echo " vfyserv -o wrong.host.badssl.com -d ${P_R_SERVERDIR} 2>&1 | tee ${P_R_SERVERDIR}/vfy.out"
  vfyserv -o wrong.host.badssl.com -d ${P_R_SERVERDIR} 2>&1 | tee ${P_R_SERVERDIR}/vfy.out
  # make sure we have the domain mismatch, not bad signature error
  echo "grep -E '12276|5961' ${P_R_SERVERDIR}/vfy.out"
  grep -E '12276|5961' ${P_R_SERVERDIR}/vfy.out
  RET=$?
  html_msg $RET $RET_EXP "${testname}" \
           "produced a returncode of $RET, expected is $RET_EXP"

  cp ${P_R_SERVERDIR}/pkcs11.txt.sav ${P_R_SERVERDIR}/pkcs11.txt

  html "</TABLE><BR>"

}

############################## ssl_policy_selfserv #####################
# local shell function to perform SSL Policy tests, using selfserv
########################################################################
ssl_policy_selfserv()
{
  #verbose="-v"
  html_head "SSL POLICY SELFSERV $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

  testname=""

  if [ ! -f "${P_R_SERVERDIR}/pkcs11.txt" ] ; then
      html_failed "${SCRIPTNAME}: ${P_R_SERVERDIR} is not initialized"
      return 1;
  fi

  echo "Saving pkcs11.txt"
  cp ${P_R_SERVERDIR}/pkcs11.txt ${P_R_SERVERDIR}/pkcs11.txt.sav

  # Disallow RSA in key exchange explicitly
  testname="Disallow RSA key exchange explicitly"
  setup_policy "disallow=rsa/ssl-key-exchange" ${P_R_SERVERDIR}

  SAVE_SERVER_OPTIONS=${SERVER_OPTIONS}
  # make sure policy is working in the multiprocess case is working on
  # UNIX-like OS's. Other OS's can't properly clean up the child processes
  # when our test suite kills the parent, so just use the single process
  # self serve for them
  # if [ "${OS_ARCH}" != "WINNT" ]; then
  #    SERVER_OPTIONS="-M 3 ${SERVER_OPTIONS}"
  # fi

  start_selfserv $CIPHER_SUITES

  SERVER_OPTIONS="${SAVE_SERVER_OPTIONS}"
  VMIN="ssl3"
  VMAX="tls1.2"

  # Try to connect to the server with a ciphersuite using RSA in key exchange
  echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -c d -V ${VMIN}:${VMAX} ${CLIENT_OPTIONS} \\"
  echo "        -f -d ${P_R_CLIENTDIR} $verbose -w nss < ${REQUEST_FILE}"

  rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
  RET_EXP=254
  ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -c d -V ${VMIN}:${VMAX} ${CLIENT_OPTIONS} -f \
          -d ${P_R_CLIENTDIR} $verbose -w nss < ${REQUEST_FILE} \
          >${TMP}/$HOST.tmp.$$  2>&1
  RET=$?
  cat ${TMP}/$HOST.tmp.$$
  rm ${TMP}/$HOST.tmp.$$ 2>/dev/null

  html_msg $RET $RET_EXP "${testname}" \
           "produced a returncode of $RET, expected is $RET_EXP"

  cp ${P_R_SERVERDIR}/pkcs11.txt.sav ${P_R_SERVERDIR}/pkcs11.txt

  kill_selfserv
  html "</TABLE><BR>"
}

############################# is_revoked ###############################
# local shell function to check if certificate is revoked
########################################################################
is_revoked() {
    certNum=$1
    currLoadedGrp=$2

    found=0
    ownerGrp=1
    while [ $ownerGrp -le $TOTAL_GRP_NUM -a $found -eq 0 ]
      do
      currGrpBegin=`eval echo \$\{CRL_GRP_${ownerGrp}_BEGIN\}`
      currGrpRange=`eval echo \$\{CRL_GRP_${ownerGrp}_RANGE\}`
      currGrpEnd=`expr $currGrpBegin + $currGrpRange - 1`
      if [ $certNum -ge $currGrpBegin -a $certNum -le $currGrpEnd ]; then
          found=1
      else
          ownerGrp=`expr $ownerGrp + 1`
      fi
    done
    if [ $found -eq 1 -a $currLoadedGrp -lt $ownerGrp ]; then
        return 1
    fi
    if [ $found -eq 0 ]; then
        return 1
    fi
    unrevokedGrpCert=`eval echo \$\{UNREVOKED_CERT_GRP_${ownerGrp}\}`
    if [ $certNum -eq $unrevokedGrpCert ]; then
        return 1
    fi
    return 0
}

########################### load_group_crl #############################
# local shell function to load CRL
########################################################################
load_group_crl() {
    #verbose="-v"
    group=$1
    ectype=$2

    OUTFILE_TMP=${TMP}/$HOST.tmp.$$
    grpBegin=`eval echo \$\{CRL_GRP_${group}_BEGIN\}`
    grpRange=`eval echo \$\{CRL_GRP_${group}_RANGE\}`
    grpEnd=`expr $grpBegin + $grpRange - 1`

    if [ "$grpBegin" = "" -o "$grpRange" = "" ]; then
        ret=1
        return 1;
    fi

    # Add -ec suffix for ECC
    if [ "$ectype" = "ECC" ] ; then
      ecsuffix="-ec"
      eccomment="ECC "
    else
      ecsuffix=""
      eccomment=""
    fi

    if [ "$RELOAD_CRL" != "" ]; then
        if [ $group -eq 1 ]; then
            echo "==================== Resetting to group 1 crl ==================="
            kill_selfserv
            start_selfserv
            is_selfserv_alive
        fi
        echo "================= Reloading ${eccomment}CRL for group $grpBegin - $grpEnd ============="

        echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f -d ${R_CLIENTDIR} $verbose \\"
        echo "          -V ssl3:tls1.2 -w nss -n TestUser${UNREVOKED_CERT_GRP_1}${ecsuffix}"
        echo "Request:"
        echo "GET crl://${SERVERDIR}/root.crl_${grpBegin}-${grpEnd}${ecsuffix}"
        echo ""
        echo "RELOAD time $i"

        REQF=${R_CLIENTDIR}.crlreq
        cat > ${REQF} <<_EOF_REQUEST_
GET crl://${SERVERDIR}/root.crl_${grpBegin}-${grpEnd}${ecsuffix}

_EOF_REQUEST_

        ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f  \
            -d ${R_CLIENTDIR} $verbose -V ssl3:tls1.2 -w nss -n TestUser${UNREVOKED_CERT_GRP_1}${ecsuffix} \
            >${OUTFILE_TMP}  2>&1 < ${REQF}

        cat ${OUTFILE_TMP}
        grep "CRL ReCache Error" ${OUTFILE_TMP}
        if [ $? -eq 0 ]; then
            ret=1
            return 1
        fi
    else
        echo "=== Updating DB for group $grpBegin - $grpEnd and restarting selfserv ====="

        kill_selfserv
        CU_ACTION="Importing ${eccomment}CRL for groups $grpBegin - $grpEnd"
        crlu -d ${R_SERVERDIR} -I -i ${SERVERDIR}/root.crl_${grpBegin}-${grpEnd}${ecsuffix} \
             -p ../tests.pw.928
        ret=$?
        if [ "$ret" -eq 0 ]; then
            html_passed "${CU_ACTION}"
            return 1
        fi
        start_selfserv
    fi
    is_selfserv_alive
    ret=$?
    echo "================= CRL Reloaded ============="
}


########################### ssl_crl_cache ##############################
# local shell function to perform SSL test for crl cache functionality
# with/out revoked certs
########################################################################
ssl_crl_cache()
{
  #verbose="-v"
  html_head "Cache CRL SSL Client Tests $NORM_EXT"
  SSLAUTH_TMP=${TMP}/authin.tl.tmp
  SERV_ARG=-r_-r
  rm -f ${SSLAUTH_TMP}
  echo ${SSLAUTH_TMP}

  grep -- " $SERV_ARG " ${SSLAUTH} | grep -v "^#" | grep -v none | grep -v bogus | grep -v 'post hs' > ${SSLAUTH_TMP}
  echo $?
  while [ $? -eq 0 -a -f ${SSLAUTH_TMP} ]
    do
    start_selfserv `echo $SERV_ARG | sed -e 's,_, ,g'`
    exec < ${SSLAUTH_TMP}
    while read ectype value sparam cparam testname
      do
      [ "$ectype" = "" ] && continue
      if [ "$ectype" = "SNI" ]; then
          continue
      else
        servarg=`echo $sparam | awk '{r=split($0,a,"-r") - 1;print r;}'`
        pwd=`echo $cparam | grep nss`
        user=`echo $cparam | grep TestUser`
        _cparam=$cparam
        case $servarg in
            1) if [ -z "$pwd" -o -z "$user" ]; then
                rev_modvalue=0
                else
                rev_modvalue=254
                fi
                ;;
            2) rev_modvalue=254 ;;

            3) if [ -z "$pwd" -o -z "$user" ]; then
                rev_modvalue=0
                else
                rev_modvalue=1
                fi
                ;;
            4) rev_modvalue=1 ;;
          esac
        TEMP_NUM=0
        LOADED_GRP=1
        while [ ${LOADED_GRP} -le ${TOTAL_GRP_NUM} ]
          do
          while [ $TEMP_NUM -lt $TOTAL_CRL_RANGE ]
            do
            CURR_SER_NUM=`expr ${CRL_GRP_1_BEGIN} + ${TEMP_NUM}`
            TEMP_NUM=`expr $TEMP_NUM + 1`
            USER_NICKNAME="TestUser${CURR_SER_NUM}"
            cparam=`echo $_cparam | sed -e 's;\([^\]\)_;\1 ;g' -e 's;\\_;_;g' -e "s/TestUser/$USER_NICKNAME/g" `

            echo "Server Args: $SERV_ARG"
            echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f -d ${R_CLIENTDIR} $verbose \\"
            echo "        ${cparam}  < ${REQUEST_FILE}"
            rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
            ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f ${cparam} \
                -d ${R_CLIENTDIR} $verbose < ${REQUEST_FILE} \
                >${TMP}/$HOST.tmp.$$  2>&1
            ret=$?
            cat ${TMP}/$HOST.tmp.$$
            rm ${TMP}/$HOST.tmp.$$ 2>/dev/null
            is_revoked ${CURR_SER_NUM} ${LOADED_GRP}
            isRevoked=$?
            if [ $isRevoked -eq 0 ]; then
                modvalue=$rev_modvalue
                testAddMsg="revoked"
            else
                modvalue=$value
                testAddMsg="not revoked"
            fi

            is_selfserv_alive
            ss_status=$?
            if [ "$ss_status" -ne 0 ]; then
                html_msg $ret $modvalue \
                    "${testname}(cert ${USER_NICKNAME} - $testAddMsg)" \
                    "produced a returncode of $ret, expected is $modvalue. " \
                    "selfserv is not alive!"
            else
                html_msg $ret $modvalue \
                    "${testname}(cert ${USER_NICKNAME} - $testAddMsg)" \
                    "produced a returncode of $ret, expected is $modvalue"
            fi
          done
          LOADED_GRP=`expr $LOADED_GRP + 1`
          TEMP_NUM=0
          if [ "$LOADED_GRP" -le "$TOTAL_GRP_NUM" ]; then
              load_group_crl $LOADED_GRP $ectype
              html_msg $ret 0 "Load group $LOADED_GRP ${eccomment}crl " \
                  "produced a returncode of $ret, expected is 0"
          fi
        done
        # Restart selfserv to roll back to two initial group 1 crls
        # TestCA CRL and TestCA-ec CRL
        kill_selfserv
        start_selfserv `echo "$sparam" | sed -e 's,_, ,g'`
      fi
    done
    kill_selfserv
    SERV_ARG="${SERV_ARG}_-r"
    rm -f ${SSLAUTH_TMP}
    grep -- " $SERV_ARG " ${SSLAUTH} | grep -v "^#" | grep -v none | grep -v bogus  > ${SSLAUTH_TMP}
  done
  TEMPFILES=${SSLAUTH_TMP}
  html "</TABLE><BR>"
}

############################ ssl_dtls ###################################
# local shell function to test tstclnt acting as client and server for DTLS
#########################################################################
ssl_dtls()
{
  #verbose="-v"
  html_head "SSL DTLS $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

    testname="ssl_dtls"
    value=0

    echo "${testname}"

    echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f ${SERVER_OPTIONS} \\"
    echo "        -d ${P_R_SERVERDIR} $verbose -U -V tls1.1:tls1.2 -P server -n ${HOSTADDR} -w nss < ${REQUEST_FILE} &"

    (sleep 2; cat ${REQUEST_FILE}) | ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f ${SERVER_OPTIONS} \
                -d ${P_R_SERVERDIR} $verbose -U -V tls1.1:tls1.2 -P server -n ${HOSTADDR} -w nss 2>&1 &

    PID=$!

    sleep 1

    echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f ${CLIENT_OPTIONS} \\"
    echo "        -d ${P_R_CLIENTDIR} $verbose -U -V tls1.1:tls1.2 -P client -Q ${CLIENT_PW} < ${REQUEST_FILE}"
    ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f ${CLIENT_OPTIONS} \
            -d ${P_R_CLIENTDIR} $verbose -U -V tls1.1:tls1.2 -P client -Q ${CLIENT_PW} < ${REQUEST_FILE} 2>&1
    ret=$?
    html_msg $ret $value "${testname}" \
             "produced a returncode of $ret, expected is $value"

    kill ${PID}

  html "</TABLE><BR>"
}

############################ ssl_scheme ###################################
# local shell function to test tstclnt and selfserv handling of signature schemes
#########################################################################
ssl_scheme()
{
    html_head "SSL SCHEME $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

    NO_ECC_CERTS=1
    schemes=("rsa_pkcs1_sha256" "rsa_pss_rsae_sha256" "rsa_pkcs1_sha256,rsa_pss_rsae_sha256")
    for sscheme in "${schemes[@]}"; do
        for cscheme in "${schemes[@]}"; do
            testname="ssl_scheme server='$sscheme' client='$cscheme'"
            echo "${testname}"

            start_selfserv -V tls1.2:tls1.2 -J "$sscheme"

            echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f -d ${P_R_CLIENTDIR} $verbose ${CLIENT_OPTIONS} \\"
            echo "        -V tls1.2:tls1.2 -J "$cscheme" ${CLIENT_PW} < ${REQUEST_FILE}"
            ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f ${CLIENT_OPTIONS} \
                        -d ${P_R_CLIENTDIR} $verbose -V tls1.2:tls1.2 -J "$cscheme" ${CLIENT_PW} < ${REQUEST_FILE} 2>&1
            ret=$?
            # If both schemes include just one option and those options don't
            # match, then the test should fail; otherwise, assume that it works.
            if [ "${cscheme#*,}" = "$cscheme" -a \
                 "${sscheme#*,}" = "$sscheme" -a \
                 "$cscheme" != "$sscheme" ]; then
                expected=254
            else
                expected=0
            fi
            html_msg $ret $expected "${testname}" \
                     "produced a returncode of $ret, expected is $expected"
            kill_selfserv
        done
    done
    NO_ECC_CERTS=0

    html "</TABLE><BR>"
}

############################ ssl_scheme_stress ##########################
# local shell function to test strsclnt and selfserv handling of signature schemes
#########################################################################
ssl_scheme_stress()
{
    html_head "SSL SCHEME $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

    NO_ECC_CERTS=1
    schemes=("rsa_pkcs1_sha256" "rsa_pss_rsae_sha256" "rsa_pkcs1_sha256,rsa_pss_rsae_sha256")
    for sscheme in "${schemes[@]}"; do
        for cscheme in "${schemes[@]}"; do
            testname="ssl_scheme server='$sscheme' client='$cscheme'"
            echo "${testname}"

            start_selfserv -V tls1.2:tls1.2 -J "$sscheme"

            echo "strsclnt -4 -q -p ${PORT} -d ${P_R_CLIENTDIR} $verbose ${CLIENT_OPTIONS} \\"
            echo "         -V tls1.2:tls1.2 -J "$cscheme" ${HOSTADDR} ${CLIENT_PW} < ${REQUEST_FILE}"
            ${PROFTOOL} ${BINDIR}/strsclnt -4 -q -p ${PORT} ${CLIENT_OPTIONS} \
                        -d ${P_R_CLIENTDIR} $verbose -V tls1.2:tls1.2 -J "$cscheme" ${HOSTADDR} ${CLIENT_PW} < ${REQUEST_FILE} 2>&1
            ret=$?
            # If both schemes include just one option and those options don't
            # match, then the test should fail; otherwise, assume that it works.
            if [ "${cscheme#*,}" = "$cscheme" -a \
                 "${sscheme#*,}" = "$sscheme" -a \
                 "$cscheme" != "$sscheme" ]; then
                expected=1
            else
                expected=0
            fi
            html_msg $ret $expected "${testname}" \
                     "produced a returncode of $ret, expected is $expected"
            kill_selfserv
        done
    done
    NO_ECC_CERTS=0

    html "</TABLE><BR>"
}

############################ ssl_exporter ###################################
# local shell function to test tstclnt and selfserv handling of TLS exporter
#########################################################################
ssl_exporter()
{
    html_head "SSL EXPORTER $NORM_EXT - server $SERVER_MODE/client $CLIENT_MODE"

    save_fileout=${fileout}
    fileout=1
    SAVE_SERVEROUTFILE=${SERVEROUTFILE}
    SERVEROUTFILE=server.out
    exporters=("label" "label:10" "label:10:0xdeadbeef" "0x666f6f2c:10:0xdeadbeef" "label1:10:0xdeadbeef,label2:10")
    for exporter in "${exporters[@]}"; do
        start_selfserv -V tls1.2:tls1.2 -x "$exporter"

        echo "tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f -d ${P_R_CLIENTDIR} $verbose ${CLIENT_OPTIONS} \\"
        echo "        -V tls1.2:tls1.2 -x $exporter ${CLIENT_PW} < ${REQUEST_FILE}"
        ${PROFTOOL} ${BINDIR}/tstclnt -4 -p ${PORT} -h ${HOSTADDR} -f ${CLIENT_OPTIONS} \
                    -d ${P_R_CLIENTDIR} $verbose -V tls1.2:tls1.2 -x "$exporter" ${CLIENT_PW} < ${REQUEST_FILE} 2>&1 > client.out
        kill_selfserv
        diff <(LC_ALL=C grep -A1 "^ *Keying Material:" server.out) \
             <(LC_ALL=C grep -A1 "^ *Keying Material:" client.out)
        ret=$?
        html_msg $ret 0 "${testname}" \
                 "produced a returncode of $ret, expected is 0"
    done
    SERVEROUTFILE=${SAVE_SERVEROUTFILE}
    fileout=${save_fileout}

    html "</TABLE><BR>"
}

############################## ssl_cleanup #############################
# local shell function to finish this script (no exit since it might be
# sourced)
########################################################################
ssl_cleanup()
{
  rm $SERVERPID 2>/dev/null
  cd ${QADIR}
  . common/cleanup.sh
}

############################## ssl_run #################################
# local shell function to run coverage, authentication and stress tests
########################################################################
ssl_run()
{
    for SSL_RUN in ${NSS_SSL_RUN}
    do
        case "${SSL_RUN}" in
        "stapling")
            ssl_stapling
            ;;
        "signed_cert_timestamps")
            ssl_signed_cert_timestamps
            ;;
        "cov")
            ssl_cov
            ssl_cov_rsa_pss
            ;;
        "auth")
            ssl_auth
            ;;
        "stress")
            ssl_stress
            ;;
        "dtls")
            ssl_dtls
            ;;
        "scheme")
            ssl_scheme
            ssl_scheme_stress
            ;;
        "exporter")
            ssl_exporter
            ;;
         esac
    done
}

############################ ssl_run_all ###############################
# local shell function to run both standard and extended ssl tests
########################################################################
ssl_run_all()
{
    ORIG_SERVERDIR=$SERVERDIR
    ORIG_CLIENTDIR=$CLIENTDIR
    ORIG_R_SERVERDIR=$R_SERVERDIR
    ORIG_R_CLIENTDIR=$R_CLIENTDIR
    ORIG_P_R_SERVERDIR=$P_R_SERVERDIR
    ORIG_P_R_CLIENTDIR=$P_R_CLIENTDIR

    # Exercise PKCS#11 URI parsing. The token actually changes its name
    # in FIPS mode, so cope with that. Note there's also semicolon in here
    # but it doesn't need escaping/quoting; the shell copes.
    if [ "${CLIENT_MODE}" = "fips" ]; then
        USER_NICKNAME="pkcs11:token=NSS%20FIPS%20140-2%20Certificate%20DB;object=TestUser"
    else
        USER_NICKNAME="pkcs11:token=NSS%20Certificate%20DB;object=TestUser"
    fi
    NORM_EXT=""
    cd ${CLIENTDIR}

    ssl_run

    SERVERDIR=$EXT_SERVERDIR
    CLIENTDIR=$EXT_CLIENTDIR
    R_SERVERDIR=$R_EXT_SERVERDIR
    R_CLIENTDIR=$R_EXT_CLIENTDIR
    P_R_SERVERDIR=$P_R_EXT_SERVERDIR
    P_R_CLIENTDIR=$P_R_EXT_CLIENTDIR

    # A different URI test; specify CKA_LABEL but not the token.
    USER_NICKNAME="pkcs11:object=ExtendedSSLUser"
    NORM_EXT="Extended Test"
    cd ${CLIENTDIR}

    ssl_run

    # the next round of ssl tests will only run if these vars are reset
    SERVERDIR=$ORIG_SERVERDIR
    CLIENTDIR=$ORIG_CLIENTDIR
    R_SERVERDIR=$ORIG_R_SERVERDIR
    R_CLIENTDIR=$ORIG_R_CLIENTDIR
    P_R_SERVERDIR=$ORIG_P_R_SERVERDIR
    P_R_CLIENTDIR=$ORIG_P_R_CLIENTDIR

    USER_NICKNAME=TestUser
    NORM_EXT=
    cd ${QADIR}/ssl
}

############################ ssl_set_fips ##############################
# local shell function to set FIPS mode on/off
########################################################################
ssl_set_fips()
{
    CLTSRV=$1
    ONOFF=$2

    if [ ${CLTSRV} = "server" ]; then
        DBDIRS="${SERVERDIR} ${EXT_SERVERDIR}"
    else
        DBDIRS="${CLIENTDIR} ${EXT_CLIENTDIR}"
    fi

    if [ "${ONOFF}" = "on" ]; then
        FIPSMODE=true
        RET_EXP=0
    else
        FIPSMODE=false
        RET_EXP=1
    fi

    html_head "SSL - FIPS mode ${ONOFF} for ${CLTSRV}"

    for DBDIR in ${DBDIRS}
    do
        EXT_OPT=
        echo ${DBDIR} | grep ext > /dev/null
        if [ $? -eq 0 ]; then
            EXT_OPT="extended "
        fi

        echo "${SCRIPTNAME}: Turning FIPS ${ONOFF} for the ${EXT_OPT} ${CLTSRV}"

        echo "modutil -dbdir ${DBDIR} -fips ${FIPSMODE} -force"
        ${BINDIR}/modutil -dbdir ${DBDIR} -fips ${FIPSMODE} -force 2>&1
        RET=$?
        html_msg "${RET}" "0" "${TESTNAME} (modutil -fips ${FIPSMODE})" \
                 "produced a returncode of ${RET}, expected is 0"

        echo "modutil -dbdir ${DBDIR} -list"
        DBLIST=`${BINDIR}/modutil -dbdir ${DBDIR} -list 2>&1`
        RET=$?
        html_msg "${RET}" "0" "${TESTNAME} (modutil -list)" \
                 "produced a returncode of ${RET}, expected is 0"

        echo "${DBLIST}" | grep "FIPS PKCS #11"
        RET=$?
        html_msg "${RET}" "${RET_EXP}" "${TESTNAME} (grep \"FIPS PKCS #11\")" \
                 "produced a returncode of ${RET}, expected is ${RET_EXP}"
    done

    html "</TABLE><BR>"
}

############################ ssl_set_fips ##############################
# local shell function to run all tests set in NSS_SSL_TESTS variable
########################################################################
ssl_run_tests()
{
    for SSL_TEST in ${NSS_SSL_TESTS}
    do
        case "${SSL_TEST}" in
        "policy")
            if using_sql ; then
                ssl_policy_listsuites
                ssl_policy_selfserv
                ssl_policy_pkix_ocsp
                ssl_policy
            fi
            ;;
        "crl")
            ssl_crl_ssl
            ssl_crl_cache
            ;;
        "iopr")
            ssl_iopr_run
            ;;
        *)
            SERVER_MODE=`echo "${SSL_TEST}" | cut -d_ -f1`
            CLIENT_MODE=`echo "${SSL_TEST}" | cut -d_ -f2`

            case "${SERVER_MODE}" in
            "normal")
                SERVER_OPTIONS=
                ;;
            "fips")
                SERVER_OPTIONS=
                ssl_set_fips server on
                ;;
            *)
                html_failed "${SCRIPTNAME}: Error: Unknown server mode ${SERVER_MODE}"
                return 1
                ;;
            esac

            case "${CLIENT_MODE}" in
            "normal")
                CLIENT_OPTIONS=
                CLIENT_PW=${CLIENT_PW_NORMAL}
                ;;
            "fips")
                CLIENT_OPTIONS=
                CLIENT_PW=${CLIENT_PW_FIPS}
                ssl_set_fips client on
                ;;
            *)
                html_failed "${SCRIPTNAME}: Error: Unknown client mode ${CLIENT_MODE}"
                return 1
                ;;
            esac

            ssl_run_all

            if [ "${SERVER_MODE}" = "fips" ]; then
                ssl_set_fips server off
            fi

            if [ "${CLIENT_MODE}" = "fips" ]; then
                ssl_set_fips client off
            fi
            ;;
        esac
    done
}

################################# main #################################

ssl_init
ssl_run_tests
ssl_cleanup
