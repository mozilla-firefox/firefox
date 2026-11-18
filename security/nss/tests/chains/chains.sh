#!/bin/bash
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

########################################################################
#
# mozilla/security/nss/tests/cert/chains.sh
#
# Script to test certificate chains validity. 
#
# needs to work on all Unix and Windows platforms
#
# special strings
# ---------------
#   FIXME ... known problems, search for this string
#   NOTE .... unexpected behavior
########################################################################

########################### is_httpserv_alive ##########################
# local shell function to exit with a fatal error if selfserver is not
# running
########################################################################
is_httpserv_alive()
{
  if [ ! -f "${HTTPPID}" ]; then
      echo "$SCRIPTNAME: Error - httpserv PID file ${HTTPPID} doesn't exist"
      sleep 5
      if [ ! -f "${HTTPPID}" ]; then
          Exit 9 "Fatal - httpserv pid file ${HTTPPID} does not exist"
      fi
  fi
  
  if [ "${OS_ARCH}" = "WINNT" ] && \
     [ "$OS_NAME" = "CYGWIN_NT" -o "$OS_NAME" = "MINGW32_NT" -o "$OS_NAME" = "MSYS_NT" ]; then
      PID=${SHELL_HTTPPID}
  else
      PID=`cat ${HTTPPID}`
  fi

  echo "kill -0 ${PID} >/dev/null 2>/dev/null" 
  kill -0 ${PID} >/dev/null 2>/dev/null || Exit 10 "Fatal - httpserv process not detectable"

  echo "httpserv with PID ${PID} found at `date`"
}

########################### wait_for_httpserv ##########################
# local shell function to wait until httpserver is running and initialized
########################################################################
wait_for_httpserv()
{
  echo "trying to connect to httpserv at `date`"
  echo "tstclnt -4 -p ${NSS_AIA_PORT} -h ${HOSTADDR} -q -v"
  ${BINDIR}/tstclnt -4 -p ${NSS_AIA_PORT} -h ${HOSTADDR} -q -v
  if [ $? -ne 0 ]; then
      sleep 5
      echo "retrying to connect to httpserv at `date`"
      echo "tstclnt -4 -p ${NSS_AIA_PORT} -h ${HOSTADDR} -q -v"
      ${BINDIR}/tstclnt -4 -p ${NSS_AIA_PORT} -h ${HOSTADDR} -q -v
      if [ $? -ne 0 ]; then
          html_failed "Waiting for Server"
      fi
  fi
  is_httpserv_alive
}

########################### kill_httpserv ##############################
# local shell function to kill the httpserver after the tests are done
########################################################################
kill_httpserv()
{
  PID=`cat ${HTTPPID}`

  echo "trying to kill httpserv with PID ${PID} at `date`"

  safe_kill ${PID} ${SHELL_HTTPPID}

  # On Linux httpserv needs up to 30 seconds to fully die and free
  # the port.  Wait until the port is free. (Bug 129701)
  if [ "${OS_ARCH}" = "Linux" ]; then
      echo "httpserv -b -p ${NSS_AIA_PORT} 2>/dev/null;"
      until ${BINDIR}/httpserv -b -p ${NSS_AIA_PORT} 2>/dev/null; do
          echo "RETRY: httpserv -b -p ${NSS_AIA_PORT} 2>/dev/null;"
          sleep 1
      done
  fi

  echo "httpserv with PID ${PID} killed at `date`"

  rm ${HTTPPID}
  html_detect_core_force "kill_httpserv core detection step"
}

########################### start_httpserv #############################
# local shell function to start the httpserver with the parameters required 
# for this test and log information (parameters, start time)
# also: wait until the server is up and running
########################################################################
start_httpserv()
{
  HTTP_METHOD=$1

  if [ -n "$testname" ] ; then
      echo "$SCRIPTNAME: $testname ----"
  fi
  echo "httpserv starting at `date`"
  ODDIR="${HOSTDIR}/chains/OCSPD"
  echo "httpserv -D -p ${NSS_AIA_PORT} ${SERVER_OPTIONS} \\"
  echo "         -A OCSPRoot -C ${ODDIR}/OCSPRoot.crl -A OCSPCA1 -C ${ODDIR}/OCSPCA1.crl \\"
  echo "         -A OCSPCA2  -C ${ODDIR}/OCSPCA2.crl  -A OCSPCA3 -C ${ODDIR}/OCSPCA3.crl \\"
  echo "         -O ${HTTP_METHOD} -d ${ODDIR}/ServerDB/ -f ${ODDIR}/ServerDB/dbpasswd \\"
  echo "         -i ${HTTPPID} $verbose &"
  ${PROFTOOL} ${BINDIR}/httpserv -D -p ${NSS_AIA_PORT} ${SERVER_OPTIONS} \
                 -A OCSPRoot -C ${ODDIR}/OCSPRoot.crl -A OCSPCA1 -C ${ODDIR}/OCSPCA1.crl \
                 -A OCSPCA2  -C ${ODDIR}/OCSPCA2.crl  -A OCSPCA3 -C ${ODDIR}/OCSPCA3.crl \
                 -O ${HTTP_METHOD} -d ${ODDIR}/ServerDB/ -f ${ODDIR}/ServerDB/dbpasswd \
                 -i ${HTTPPID} $verbose &
  RET=$?

  # The PID $! returned by the MKS or Cygwin shell is not the PID of
  # the real background process, but rather the PID of a helper
  # process (sh.exe).  MKS's kill command has a bug: invoking kill
  # on the helper process does not terminate the real background
  # process.  Our workaround has been to have httpserv save its PID
  # in the ${HTTPPID} file and "kill" that PID instead.  But this
  # doesn't work under Cygwin; its kill command doesn't recognize
  # the PID of the real background process, but it does work on the
  # PID of the helper process.  So we save the value of $! in the
  # SHELL_HTTPPID variable, and use it instead of the ${HTTPPID}
  # file under Cygwin.  (In fact, this should work in any shell
  # other than the MKS shell.)
  SHELL_HTTPPID=$!
  wait_for_httpserv

  echo "httpserv with PID `cat ${HTTPPID}` started at `date`"
}

############################# chains_init ##############################
# local shell function to initialize this script
########################################################################
chains_init()
{
    if [ -z "${CLEANUP}" ] ; then   # if nobody else is responsible for
        CLEANUP="${SCRIPTNAME}"     # cleaning this script will do it
    fi
    if [ -z "${INIT_SOURCED}" ] ; then
        cd ../common
        . ./init.sh
    fi

    SCRIPTNAME="chains.sh"

    CHAINS_DIR="${HOSTDIR}/chains"
    mkdir -p ${CHAINS_DIR}
    cd ${CHAINS_DIR}

    CHAINS_SCENARIOS="${QADIR}/chains/scenarios/scenarios"

    CERT_SN_CNT=$(date '+%m%d%H%M%S' | sed "s/^0*//")
    CERT_SN_FIX=$(expr ${CERT_SN_CNT} - 1000)

    PK7_NONCE=${CERT_SN_CNT}
    SCEN_CNT=${CERT_SN_CNT}

    AIA_FILES="${HOSTDIR}/aiafiles"

    CU_DATA=${HOSTDIR}/cu_data
    CRL_DATA=${HOSTDIR}/crl_data

    DEFAULT_AIA_BASE_PORT=$(expr ${PORT:-8631} + 10)
    NSS_AIA_PORT=${NSS_AIA_PORT:-$DEFAULT_AIA_BASE_PORT}
    DEFAULT_UNUSED_PORT=$(expr ${PORT:-8631} + 11)
    NSS_UNUSED_PORT=${NSS_UNUSED_PORT:-$DEFAULT_UNUSED_PORT}
    NSS_AIA_HTTP=${NSS_AIA_HTTP:-"http://${HOSTADDR}:${NSS_AIA_PORT}"}
    NSS_AIA_PATH=${NSS_AIA_PATH:-$HOSTDIR/aiahttp}
    NSS_AIA_OCSP=${NSS_AIA_OCSP:-$NSS_AIA_HTTP/ocsp}
    NSS_OCSP_UNUSED=${NSS_AIA_OCSP_UNUSED:-"http://${HOSTADDR}:${NSS_UNUSED_PORT}"}

    html_head "Certificate Chains Tests"
}

chains_run_httpserv()
{
    HTTP_METHOD=$1

    if [ -n "${NSS_AIA_PATH}" ]; then
        HTTPPID=${NSS_AIA_PATH}/http_pid.$$
        mkdir -p "${NSS_AIA_PATH}"
        SAVEPWD=`pwd`
        cd "${NSS_AIA_PATH}"
        # Start_httpserv sets environment variables, which are required for
        # correct cleanup. (Running it in a subshell doesn't work, the
        # value of $SHELL_HTTPPID wouldn't arrive in this scope.)
        start_httpserv ${HTTP_METHOD}
        cd "${SAVEPWD}"
    fi
}

chains_stop_httpserv()
{
    if [ -n "${NSS_AIA_PATH}" ]; then
        kill_httpserv
    fi
}

############################ chains_cleanup ############################
# local shell function to finish this script (no exit since it might be
# sourced)
########################################################################
chains_cleanup()
{
    html "</TABLE><BR>"
    cd ${QADIR}
    . common/cleanup.sh
}

############################ print_cu_data #############################
# local shell function to print certutil input data
########################################################################
print_cu_data()
{
    echo "=== Certutil input data ==="
    cat ${CU_DATA}
    echo "==="
}

set_cert_sn()
{
    if [ -z "${SERIAL}" ]; then
        CERT_SN_CNT=$(expr ${CERT_SN_CNT} + 1)
        CERT_SN=${CERT_SN_CNT}
    else
        echo ${SERIAL} | cut -b 1 | grep '+' > /dev/null
        if [ $? -eq 0 ]; then
            CERT_SN=$(echo ${SERIAL} | cut -b 2-)
            CERT_SN=$(expr ${CERT_SN_FIX} + ${CERT_SN})
        else
            CERT_SN=${SERIAL}
        fi 
    fi
}

############################# create_db ################################
# local shell function to create certificate database
########################################################################
create_db()
{
    DB=$1

    [ -d "${DB}" ] && rm -rf ${DB}
    mkdir -p ${DB}

    echo "${DB}passwd" > ${DB}/dbpasswd

    TESTNAME="Creating DB ${DB}"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "certutil -N -d ${DB} -f ${DB}/dbpasswd" 
    ${BINDIR}/certutil -N -d ${DB} -f ${DB}/dbpasswd
    html_msg $? 0 "${SCENARIO}${TESTNAME}" 
}

############################ set_ctype_opt #############################
# local shell function to convert the entity key type into certutil
# options. A key type may name a parameter set, e.g. mldsa:ML-DSA-65.
########################################################################
set_ctype_opt()
{
    CTYPE_OPT=
    if [ -n "${CTYPE}" ]; then
        CTYPE_OPT="-k $(echo ${CTYPE} | cut -d: -f1)"
        case "${CTYPE}" in
        *:*)
            CTYPE_OPT="${CTYPE_OPT} -q $(echo ${CTYPE} | cut -d: -f2)"
            ;;
        esac
    fi
}

########################### create_root_ca #############################
# local shell function to generate self-signed root certificate
########################################################################
create_root_ca()
{
    ENTITY=$1
    ENTITY_DB=${ENTITY}DB

    set_cert_sn
    date >> ${NOISE_FILE} 2>&1

    set_ctype_opt

    echo "5
6
9
n
y
-1
n
5
6
7
9
n
" > ${CU_DATA}

    TESTNAME="Creating Root CA ${ENTITY}"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "certutil -s \"CN=${ENTITY} ROOT CA, O=${ENTITY}, C=US\" -S -n ${ENTITY} ${CTYPE_OPT} -t CTu,CTu,CTu -v 600 -x -d ${ENTITY_DB} -1 -2 -5 -f ${ENTITY_DB}/dbpasswd -z ${NOISE_FILE} -m ${CERT_SN} < ${CU_DATA}"
    print_cu_data
    ${BINDIR}/certutil -s "CN=${ENTITY} ROOT CA, O=${ENTITY}, C=US" -S -n ${ENTITY} ${CTYPE_OPT} -t CTu,CTu,CTu -v 600 -x -d ${ENTITY_DB} -1 -2 -5 -f ${ENTITY_DB}/dbpasswd -z ${NOISE_FILE} -m ${CERT_SN} < ${CU_DATA}
    html_msg $? 0 "${SCENARIO}${TESTNAME}"

    TESTNAME="Exporting Root CA ${ENTITY}.der"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "certutil -L -d ${ENTITY_DB} -r -n ${ENTITY} -o ${ENTITY}.der"
    ${BINDIR}/certutil -L -d ${ENTITY_DB} -r -n ${ENTITY} -o ${ENTITY}.der
    html_msg $? 0 "${SCENARIO}${TESTNAME}"
}

########################## set_req_extensions ##########################
# local shell function to generate the extension options and input for
# the certutil command that makes the certificate request
########################################################################
set_req_extensions()
{
    TYPE=$1

    CA_FLAG="-2"
    OPTIONS=

    if [ "${TYPE}" != "EE" ]; then
        EXT_DATA="y
-1
y
"
    else
        EXT_DATA="n
-1
y
"
    fi

    process_crldp
}

########################### create_cert_req ############################
# local shell function to generate certificate sign request
########################################################################
create_cert_req()
{
    ENTITY=$1
    TYPE=$2

    ENTITY_DB=${ENTITY}DB

    REQ=${ENTITY}Req.der
    KEM_PUBKEY=${ENTITY}Pub.der

    date >> ${NOISE_FILE} 2>&1

    set_ctype_opt

    if [ "$(echo ${CTYPE} | cut -d: -f1)" = "mlkem" ]; then
        # An ML-KEM key can't sign a certificate request. Export its public
        # key instead, and sign_cert will issue the certificate for it.
        TESTNAME="Creating ${TYPE} public key ${KEM_PUBKEY}"
        echo "${SCRIPTNAME}: ${TESTNAME}"
        echo "certutil ${CTYPE_OPT} -G -d ${ENTITY_DB} -f ${ENTITY_DB}/dbpasswd -z ${NOISE_FILE} -o ${KEM_PUBKEY}"
        ${BINDIR}/certutil ${CTYPE_OPT} -G -d ${ENTITY_DB} -f ${ENTITY_DB}/dbpasswd -z ${NOISE_FILE} -o ${KEM_PUBKEY}
        html_msg $? 0 "${SCENARIO}${TESTNAME}"
        return
    fi

    set_req_extensions "${TYPE}"

    echo "${EXT_DATA}" > ${CU_DATA}

    TESTNAME="Creating ${TYPE} certifiate request ${REQ}"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "certutil -s \"CN=${ENTITY} ${TYPE}, O=${ENTITY}, C=US\" ${CTYPE_OPT} -R ${CA_FLAG} -d ${ENTITY_DB} -f ${ENTITY_DB}/dbpasswd -z ${NOISE_FILE} -o ${REQ} ${OPTIONS} < ${CU_DATA}"
    print_cu_data
    ${BINDIR}/certutil -s "CN=${ENTITY} ${TYPE}, O=${ENTITY}, C=US" ${CTYPE_OPT} -R ${CA_FLAG} -d ${ENTITY_DB} -f ${ENTITY_DB}/dbpasswd -z ${NOISE_FILE} -o ${REQ} ${OPTIONS} < ${CU_DATA} 
    html_msg $? 0 "${SCENARIO}${TESTNAME}"
}

############################ create_entity #############################
# local shell function to create certificate chain entity
########################################################################
create_entity()
{
    ENTITY=$1
    TYPE=$2

    if [ -z "${ENTITY}" ]; then
        echo "Configuration error: Unnamed entity"
        exit 1
    fi

    DB=${ENTITY}DB
    ENTITY_DB=${ENTITY}DB

    case "${TYPE}" in
    "Root")
        create_db "${DB}"
        create_root_ca "${ENTITY}"
        ;;
    "Intermediate" | "Bridge" | "EE")
        create_db "${DB}"
        create_cert_req "${ENTITY}" "${TYPE}"
        ;;
    "*")
        echo "Configuration error: Unknown type ${TYPE}"
        exit 1
        ;;
    esac
}

########################################################################
# List of global variables related to certificate extensions processing:
#
# Generated by process_extensions and functions called from it:
# OPTIONS - list of command line policy extensions 
# DATA - list of inpud data related to policy extensions
#
# Generated by parse_config:
# POLICY - list of certificate policies
# MAPPING - list of policy mappings 
# INHIBIT - inhibit flag
# AIA - AIA list
########################################################################

############################ process_policy ############################
# local shell function to process policy extension parameters and 
# generate input for certutil
########################################################################
process_policy()
{
    if [ -n "${POLICY}" ]; then
        OPTIONS="${OPTIONS} --extCP"

        NEXT=
        for ITEM in ${POLICY}; do
            if [ -n "${NEXT}" ]; then
                DATA="${DATA}y
"
            fi

            NEXT=1
            DATA="${DATA}${ITEM}
1

n
"
        done

        DATA="${DATA}n
n
"
    fi
}

########################### process_mapping ############################
# local shell function to process policy mapping parameters and 
# generate input for certutil
########################################################################
process_mapping()
{
    if [ -n "${MAPPING}" ]; then
        OPTIONS="${OPTIONS} --extPM"

        NEXT=
        for ITEM in ${MAPPING}; do
            if [ -n "${NEXT}" ]; then
                DATA="${DATA}y
"
            fi

            NEXT=1
            IDP=`echo ${ITEM} | cut -d: -f1`
            SDP=`echo ${ITEM} | cut -d: -f2`
            DATA="${DATA}${IDP}
${SDP}
"
        done

        DATA="${DATA}n
n
"
    fi
}

########################### process_inhibit#############################
# local shell function to process inhibit extension and generate input 
# for certutil
########################################################################
process_inhibit()
{
    if [ -n "${INHIBIT}" ]; then
        OPTIONS="${OPTIONS} --extIA"

        DATA="${DATA}${INHIBIT}
n
"
    fi
}

############################# process_aia ##############################
# local shell function to process AIA extension parameters and 
# generate input for certutil
########################################################################
process_aia()
{
    if [ -n "${AIA}" ]; then
        OPTIONS="${OPTIONS} --extAIA"

        DATA="${DATA}1
"

        for ITEM in ${AIA}; do
            PK7_NONCE=`expr $PK7_NONCE + 1`

            echo ${ITEM} | grep ":" > /dev/null
            if [ $? -eq 0 ]; then
                CERT_NICK=`echo ${ITEM} | cut -d: -f1`
                CERT_ISSUER=`echo ${ITEM} | cut -d: -f2`
                CERT_LOCAL="${CERT_NICK}${CERT_ISSUER}.der"
                CERT_PUBLIC="${HOST}-$$-${CERT_NICK}${CERT_ISSUER}-${PK7_NONCE}.der"
            else
                CERT_LOCAL="${ITEM}.p7"
                CERT_PUBLIC="${HOST}-$$-${ITEM}-${PK7_NONCE}.p7"
            fi

            DATA="${DATA}7
${NSS_AIA_HTTP}/${CERT_PUBLIC}
"

            if [ -n "${NSS_AIA_PATH}" ]; then
                cp ${CERT_LOCAL} ${NSS_AIA_PATH}/${CERT_PUBLIC} 2> /dev/null
                chmod a+r ${NSS_AIA_PATH}/${CERT_PUBLIC}
                echo ${NSS_AIA_PATH}/${CERT_PUBLIC} >> ${AIA_FILES}
            fi
        done

        DATA="${DATA}0
n
n"
    fi
}

process_ocsp()
{
    if [ -n "${OCSP}" ]; then
        OPTIONS="${OPTIONS} --extAIA"
 
	if [ "${OCSP}" = "offline" ]; then
	    MY_OCSP_URL=${NSS_OCSP_UNUSED}
	else
	    MY_OCSP_URL=${NSS_AIA_OCSP}
	fi

        DATA="${DATA}2
7
${MY_OCSP_URL}
0
n
n
"
    fi
}

process_crldp()
{
    if [ -n "${CRLDP}" ]; then
        OPTIONS="${OPTIONS} -4"

        EXT_DATA="${EXT_DATA}1
"

        for ITEM in ${CRLDP}; do
            CRL_PUBLIC="${HOST}-$$-${ITEM}-${SCEN_CNT}.crl"

            EXT_DATA="${EXT_DATA}7
${NSS_AIA_HTTP}/${CRL_PUBLIC}
"
        done

        EXT_DATA="${EXT_DATA}-1
-1
-1
n
n
"
    fi
}

process_ku_ns_eku()
{
    if [ -n "${EXT_KU}" ]; then
        OPTIONS="${OPTIONS} --keyUsage ${EXT_KU}"
    fi
    if [ -n "${EXT_NS}" ]; then
        EXT_NS_KEY=$(echo ${EXT_NS} | cut -d: -f1)
        EXT_NS_CODE=$(echo ${EXT_NS} | cut -d: -f2)

        OPTIONS="${OPTIONS} --nsCertType ${EXT_NS_KEY}"
        DATA="${DATA}${EXT_NS_CODE}
-1
n
"
    fi
    if [ -n "${EXT_EKU}" ]; then
        OPTIONS="${OPTIONS} --extKeyUsage ${EXT_EKU}"
    fi
}

copy_crl()

{
    if [ -z "${NSS_AIA_PATH}" ]; then
        return;
    fi

    CRL_LOCAL="${COPYCRL}.crl"
    CRL_PUBLIC="${HOST}-$$-${COPYCRL}-${SCEN_CNT}.crl"

    cp ${CRL_LOCAL} ${NSS_AIA_PATH}/${CRL_PUBLIC} 2> /dev/null
    chmod a+r ${NSS_AIA_PATH}/${CRL_PUBLIC}
    echo ${NSS_AIA_PATH}/${CRL_PUBLIC} >> ${AIA_FILES}
}

########################## process_extension ###########################
# local shell function to process entity extension parameters and 
# generate input for certutil
########################################################################
process_extensions()
{
    OPTIONS=
    DATA=

    process_policy
    process_mapping
    process_inhibit
    process_aia
    process_ocsp
    process_ku_ns_eku
}

############################## sign_cert ###############################
# local shell function to sign certificate sign reuqest
########################################################################
sign_cert()
{
    ENTITY=$1
    ISSUER=$2
    TYPE=$3

    [ -z "${ISSUER}" ] && return

    ENTITY_DB=${ENTITY}DB
    ISSUER_DB=${ISSUER}DB
    REQ=${ENTITY}Req.der
    KEM_PUBKEY=${ENTITY}Pub.der
    CERT=${ENTITY}${ISSUER}.der

    set_cert_sn

    EMAIL_OPT=
    if [ "${TYPE}" = "Bridge" ]; then
        EMAIL_OPT="-7 ${ENTITY}@${ISSUER}"

        [ -n "${EMAILS}" ] && EMAILS="${EMAILS},"
        EMAILS="${EMAILS}${ENTITY}@${ISSUER}"
    fi

    # Without a certificate request (see create_cert_req), the extensions
    # that would have been in the request are added here instead.
    REQ_OPTIONS=
    REQ_DATA=
    if [ -f "${KEM_PUBKEY}" ]; then
        set_req_extensions "${TYPE}"
        REQ_OPTIONS="${CA_FLAG} ${OPTIONS}"
        REQ_DATA="${EXT_DATA}"
    fi

    process_extensions 

    echo "${REQ_DATA}${DATA}" > ${CU_DATA}

    TESTNAME="Creating certficate ${CERT} signed by ${ISSUER}"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    if [ -f "${KEM_PUBKEY}" ]; then
        SUBJECT="CN=${ENTITY} ${TYPE}, O=${ENTITY}, C=US"
        echo "certutil -C -c ${ISSUER} -v 60 -d ${ISSUER_DB} --kem-pubkey ${KEM_PUBKEY} -s \"${SUBJECT}\" -o ${CERT} -f ${ISSUER_DB}/dbpasswd -m ${CERT_SN} ${EMAIL_OPT} ${REQ_OPTIONS} ${OPTIONS} < ${CU_DATA}"
        print_cu_data
        ${BINDIR}/certutil -C -c ${ISSUER} -v 60 -d ${ISSUER_DB} --kem-pubkey ${KEM_PUBKEY} -s "${SUBJECT}" -o ${CERT} -f ${ISSUER_DB}/dbpasswd -m ${CERT_SN} ${EMAIL_OPT} ${REQ_OPTIONS} ${OPTIONS} < ${CU_DATA}
    else
        echo "certutil -C -c ${ISSUER} -v 60 -d ${ISSUER_DB} -i ${REQ} -o ${CERT} -f ${ISSUER_DB}/dbpasswd -m ${CERT_SN} ${EMAIL_OPT} ${OPTIONS} < ${CU_DATA}"
        print_cu_data
        ${BINDIR}/certutil -C -c ${ISSUER} -v 60 -d ${ISSUER_DB} -i ${REQ} -o ${CERT} -f ${ISSUER_DB}/dbpasswd -m ${CERT_SN} ${EMAIL_OPT} ${OPTIONS} < ${CU_DATA}
    fi
    html_msg $? 0 "${SCENARIO}${TESTNAME}"

    TESTNAME="Importing certificate ${CERT} to ${ENTITY_DB} database"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "certutil -A -n ${ENTITY} -t u,u,u -d ${ENTITY_DB} -f ${ENTITY_DB}/dbpasswd -i ${CERT}"
    ${BINDIR}/certutil -A -n ${ENTITY} -t u,u,u -d ${ENTITY_DB} -f ${ENTITY_DB}/dbpasswd -i ${CERT}
    html_msg $? 0 "${SCENARIO}${TESTNAME}"
}

############################# create_pkcs7##############################
# local shell function to package bridge certificates into pkcs7 
# package
########################################################################
create_pkcs7()
{
    ENTITY=$1
    ENTITY_DB=${ENTITY}DB

    TESTNAME="Generating PKCS7 package from ${ENTITY_DB} database"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "cmsutil -O -r \"${EMAILS}\" -d ${ENTITY_DB} > ${ENTITY}.p7"
    ${BINDIR}/cmsutil -O -r "${EMAILS}" -d ${ENTITY_DB} > ${ENTITY}.p7
    html_msg $? 0 "${SCENARIO}${TESTNAME}"
}

############################# import_key ###############################
# local shell function to import private key + cert into database
########################################################################
import_key()
{
    KEY_NAME=$1.p12
    DB=$2

    KEY_FILE=../OCSPD/${KEY_NAME}

    TESTNAME="Importing p12 key ${KEY_NAME} to ${DB} database"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "${BINDIR}/pk12util -d ${DB} -i ${KEY_FILE} -k ${DB}/dbpasswd -W nssnss"
    ${BINDIR}/pk12util -d ${DB} -i ${KEY_FILE} -k ${DB}/dbpasswd -W nssnss
    html_msg $? 0 "${SCENARIO}${TESTNAME}"
}

export_key()
{
    KEY_NAME=$1.p12
    DB=$2

    TESTNAME="Exporting $1 as ${KEY_NAME} from ${DB} database"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "${BINDIR}/pk12util -d ${DB} -o ${KEY_NAME} -n $1 -k ${DB}/dbpasswd -W nssnss"
    ${BINDIR}/pk12util -d ${DB} -o ${KEY_NAME} -n $1 -k ${DB}/dbpasswd -W nssnss
    html_msg $? 0 "${SCENARIO}${TESTNAME}"
}

############################# import_cert ##############################
# local shell function to import certificate into database
########################################################################
import_cert()
{
    IMPORT=$1
    DB=$2

    CERT_NICK=`echo ${IMPORT} | cut -d: -f1`
    CERT_ISSUER=`echo ${IMPORT} | cut -d: -f2`
    CERT_TRUST=`echo ${IMPORT} | cut -d: -f3`

    if [ "${CERT_ISSUER}" = "x" ]; then
        CERT_ISSUER=
        CERT=${CERT_NICK}.cert
        CERT_FILE="${QADIR}/libpkix/certs/${CERT}"
    elif [ "${CERT_ISSUER}" = "d" ]; then
        CERT_ISSUER=
        CERT=${CERT_NICK}.der
        CERT_FILE="../OCSPD/${CERT}"
    else
        CERT=${CERT_NICK}${CERT_ISSUER}.der
        CERT_FILE=${CERT}
    fi

    IS_ASCII=`grep -c -- "-----BEGIN CERTIFICATE-----" ${CERT_FILE}`

    ASCII_OPT=
    if [ "${IS_ASCII}" -gt 0 ]; then
        ASCII_OPT="-a"
    fi
   
    TESTNAME="Importing certificate ${CERT} to ${DB} database"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "certutil -A -n ${CERT_NICK} ${ASCII_OPT} -t \"${CERT_TRUST}\" -d ${DB} -f ${DB}/dbpasswd -i ${CERT_FILE}"
    ${BINDIR}/certutil -A -n ${CERT_NICK} ${ASCII_OPT} -t "${CERT_TRUST}" -d ${DB} -f ${DB}/dbpasswd -i ${CERT_FILE} 
    html_msg $? 0 "${SCENARIO}${TESTNAME}"
}

import_crl()
{
    IMPORT=$1
    DB=$2

    CRL_NICK=`echo ${IMPORT} | cut -d: -f1`
    CRL_FILE=${CRL_NICK}.crl

    if [ ! -f "${CRL_FILE}" ]; then
        return
    fi 

    TESTNAME="Importing CRL ${CRL_FILE} to ${DB} database"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "crlutil -I -d ${DB} -f ${DB}/dbpasswd -i ${CRL_FILE}"
    ${BINDIR}/crlutil -I -d ${DB} -f ${DB}/dbpasswd -i ${CRL_FILE} 
    html_msg $? 0 "${SCENARIO}${TESTNAME}"
}

create_crl()
{
    ISSUER=$1
    ISSUER_DB=${ISSUER}DB

    CRL=${ISSUER}.crl

    DATE=$(date -u '+%Y%m%d%H%M%SZ')
    DATE_LAST="${DATE}"

    UPDATE=$(expr $(date -u '+%Y') + 1)$(date -u '+%m%d%H%M%SZ')

    echo "update=${DATE}" > ${CRL_DATA}
    echo "nextupdate=${UPDATE}" >> ${CRL_DATA}

    TESTNAME="Create CRL for ${ISSUER_DB}"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "crlutil -G -d ${ISSUER_DB} -n ${ISSUER} -f ${ISSUER_DB}/dbpasswd -o ${CRL}"
    echo "=== Crlutil input data ==="
    cat ${CRL_DATA}
    echo "==="
    ${BINDIR}/crlutil -G -d ${ISSUER_DB} -n ${ISSUER} -f ${ISSUER_DB}/dbpasswd -o ${CRL} < ${CRL_DATA}
    html_msg $? 0 "${SCENARIO}${TESTNAME}"
}

revoke_cert()
{
    ISSUER=$1
    ISSUER_DB=${ISSUER}DB

    CRL=${ISSUER}.crl

    set_cert_sn

    DATE=$(date -u '+%Y%m%d%H%M%SZ')
    while [ "${DATE}" = "${DATE_LAST}" ]; do
        sleep 1
        DATE=$(date -u '+%Y%m%d%H%M%SZ')
    done
    DATE_LAST="${DATE}"

    echo "update=${DATE}" > ${CRL_DATA}
    echo "addcert ${CERT_SN} ${DATE}" >> ${CRL_DATA}

    TESTNAME="Revoking certificate with SN ${CERT_SN} issued by ${ISSUER}"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "crlutil -M -d ${ISSUER_DB} -n ${ISSUER} -f ${ISSUER_DB}/dbpasswd -o ${CRL}"
    echo "=== Crlutil input data ==="
    cat ${CRL_DATA}
    echo "==="
    ${BINDIR}/crlutil -M -d ${ISSUER_DB} -n ${ISSUER} -f ${ISSUER_DB}/dbpasswd -o ${CRL} < ${CRL_DATA}
    html_msg $? 0 "${SCENARIO}${TESTNAME}"
}

########################################################################
# List of global variables related to certificate verification:
#
# Generated by parse_config:
# DB - DB used for testing
# FETCH - fetch flag (used with AIA extension)
# POLICY - list of policies
# TRUST - trust anchor
# TRUST_AND_DB - Examine both trust anchors and the cert db for trust
# VERIFY - list of certificates to use as vfychain parameters
# EXP_RESULT - expected result
# REV_OPTS - revocation options
########################################################################

############################# verify_cert ##############################
# local shell function to verify certificate validity
########################################################################
verify_cert()
{
    ENGINE=$1

    DB_OPT=
    FETCH_OPT=
    POLICY_OPT=
    TRUST_OPT=
    VFY_CERTS=
    VFY_LIST=
    TRUST_AND_DB_OPT=

    if [ -n "${DB}" ]; then
        DB_OPT="-d ${DB}"
    fi

    if [ -n "${FETCH}" ]; then
        FETCH_OPT="-f"
        if [ -z "${NSS_AIA_HTTP}" ]; then
            echo "${SCRIPTNAME} Skipping test using AIA fetching, NSS_AIA_HTTP not defined"
            return
        fi
    fi

    if [ -n "${TRUST_AND_DB}" ]; then
        TRUST_AND_DB_OPT="-T"
    fi

    for ITEM in ${POLICY}; do
        POLICY_OPT="${POLICY_OPT} -o ${ITEM}"
    done

    for ITEM in ${TRUST}; do
        echo ${ITEM} | grep ":" > /dev/null
        if [ $? -eq 0 ]; then
            CERT_NICK=`echo ${ITEM} | cut -d: -f1`
            CERT_ISSUER=`echo ${ITEM} | cut -d: -f2`
            CERT=${CERT_NICK}${CERT_ISSUER}.der

            TRUST_OPT="${TRUST_OPT} -t ${CERT}"
        else
            TRUST_OPT="${TRUST_OPT} -t ${ITEM}"
        fi
    done

    for ITEM in ${VERIFY}; do
        CERT_NICK=`echo ${ITEM} | cut -d: -f1`
        CERT_ISSUER=`echo ${ITEM} | cut -d: -f2`

        if [ "${CERT_ISSUER}" = "x" ]; then
            CERT="${QADIR}/libpkix/certs/${CERT_NICK}.cert"
            VFY_CERTS="${VFY_CERTS} ${CERT}"
            VFY_LIST="${VFY_LIST} ${CERT_NICK}.cert"
        elif [ "${CERT_ISSUER}" = "d" ]; then
            CERT="../OCSPD/${CERT_NICK}.der"
            VFY_CERTS="${VFY_CERTS} ${CERT}"
            VFY_LIST="${VFY_LIST} ${CERT_NICK}.cert"
        else
            CERT=${CERT_NICK}${CERT_ISSUER}.der
            VFY_CERTS="${VFY_CERTS} ${CERT}"
            VFY_LIST="${VFY_LIST} ${CERT}"
        fi
    done

    VFY_OPTS_TNAME="${DB_OPT} ${ENGINE} ${TRUST_AND_DB_OPT} ${REV_OPTS} ${FETCH_OPT} ${USAGE_OPT} ${POLICY_OPT} ${TRUST_OPT}"
    VFY_OPTS_ALL="${DB_OPT} ${ENGINE} -vv ${VFY_TIME_OPT} ${TRUST_AND_DB_OPT} ${REV_OPTS} ${FETCH_OPT} ${USAGE_OPT} ${POLICY_OPT} ${VFY_CERTS} ${TRUST_OPT}"

    TESTNAME="Verifying certificate(s) ${VFY_LIST} with flags ${VFY_OPTS_TNAME}"
    echo "${SCRIPTNAME}: ${TESTNAME}"
    echo "vfychain ${VFY_OPTS_ALL}"

    if [ -z "${MEMLEAK_DBG}" ]; then
        VFY_OUT=$(${BINDIR}/vfychain ${VFY_OPTS_ALL} 2>&1)
        RESULT=$?
        echo "${VFY_OUT}"
    else 
        VFY_OUT=$(${RUN_COMMAND_DBG} ${BINDIR}/vfychain ${VFY_OPTS_ALL} 2>> ${LOGFILE})
        RESULT=$?
        echo "${VFY_OUT}"
    fi

    echo "${VFY_OUT}" | grep "ERROR -5990: I/O operation timed out" > /dev/null
    E5990=$?
    echo "${VFY_OUT}" | grep "ERROR -8030: Server returned bad HTTP response" > /dev/null
    E8030=$?

    if [ $E5990 -eq 0 -o $E8030 -eq 0 ]; then
        echo "Result of this test is not valid due to network time out."
        html_unknown "${SCENARIO}${TESTNAME}"
        return
    fi

    echo "Returned value is ${RESULT}, expected result is ${EXP_RESULT}"
    
    if [ "${EXP_RESULT}" = "pass" -a ${RESULT} -eq 0 ]; then
        html_passed "${SCENARIO}${TESTNAME}"
    elif [ "${EXP_RESULT}" = "fail" -a ${RESULT} -ne 0 ]; then
        html_passed "${SCENARIO}${TESTNAME}"
    else
        html_failed "${SCENARIO}${TESTNAME}"
    fi
}

check_ocsp()
{
    OCSP_CERT=$1

    CERT_NICK=`echo ${OCSP_CERT} | cut -d: -f1`
    CERT_ISSUER=`echo ${OCSP_CERT} | cut -d: -f2`

    if [ "${CERT_ISSUER}" = "x" ]; then
        CERT_ISSUER=
        CERT=${CERT_NICK}.cert
        CERT_FILE="${QADIR}/libpkix/certs/${CERT}"
    elif [ "${CERT_ISSUER}" = "d" ]; then
        CERT_ISSUER=
        CERT=${CERT_NICK}.der
        CERT_FILE="../OCSPD/${CERT}"
    else
        CERT=${CERT_NICK}${CERT_ISSUER}.der
        CERT_FILE=${CERT}
    fi

    # sample line:
    #   URI: "http://ocsp.server:2601"
    OCSP_HOST=$(${BINDIR}/pp -w -t certificate -i ${CERT_FILE} | grep URI | sed "s/.*:\/\///" | sed "s/:.*//")
    OCSP_PORT=$(${BINDIR}/pp -w -t certificate -i ${CERT_FILE} | grep URI | sed "s/^.*:.*:\/\/.*:\([0-9]*\).*$/\1/")

    echo "tstclnt -4 -h ${OCSP_HOST} -p ${OCSP_PORT} -q -t 20"
    tstclnt -4 -h ${OCSP_HOST} -p ${OCSP_PORT} -q -t 20
    return $?
}

############################ parse_result ##############################
# local shell function to process expected result value
# this function was created for case that expected result depends on
# some conditions - in our case type of cert DB
#
# default results are pass and fail
# this function added parsable values in format:
# type1:value1 type2:value2 .... typex:valuex
#
# allowed types are dbm, sql, all (all means all other cases)
# allowed values are pass and fail
#
# if this format is not used, EXP_RESULT will stay unchanged (this also
# covers pass and fail states)
########################################################################
parse_result()
{
    for RES in ${EXP_RESULT}
    do
        RESTYPE=$(echo ${RES} | cut -d: -f1)
        RESSTAT=$(echo ${RES} | cut -d: -f2)

        if [ "${RESTYPE}" = "${NSS_DEFAULT_DB_TYPE}" -o "${RESTYPE}" = "all" ]; then
            EXP_RESULT=${RESSTAT}
            break
        fi
    done
}

############################ parse_config ##############################
# local shell function to parse and process file containing certificate
# chain configuration and list of tests
########################################################################
parse_config()
{
    SCENARIO=
    LOGNAME=

    while read KEY VALUE
    do
        case "${KEY}" in
        "entity")
            ENTITY="${VALUE}"
            TYPE=
            ISSUER=
            CTYPE=
            POLICY=
            MAPPING=
            INHIBIT=
            AIA=
            CRLDP=
            OCSP=
            DB=
            EMAILS=
            EXT_KU=
            EXT_NS=
            EXT_EKU=
            SERIAL=
	    EXPORT_KEY=
            ;;
        "type")
            TYPE="${VALUE}"
            ;;
        "issuer")
            if [ -n "${ISSUER}" ]; then
                if [ -z "${DB}" ]; then
                    create_entity "${ENTITY}" "${TYPE}"
                fi
                sign_cert "${ENTITY}" "${ISSUER}" "${TYPE}"
            fi

            ISSUER="${VALUE}"
            POLICY=
            MAPPING=
            INHIBIT=
            AIA=
            EXT_KU=
            EXT_NS=
            EXT_EKU=
            ;;
        "ctype") 
            CTYPE="${VALUE}"
            ;;
        "policy")
            POLICY="${POLICY} ${VALUE}"
            ;;
        "mapping")
            MAPPING="${MAPPING} ${VALUE}"
            ;;
        "inhibit")
            INHIBIT="${VALUE}"
            ;;
        "aia")
            AIA="${AIA} ${VALUE}"
            ;;
        "crldp")
            CRLDP="${CRLDP} ${VALUE}"
            ;;
        "ocsp")
            OCSP="${VALUE}"
            ;;
        "db")
            DB="${VALUE}DB"
            create_db "${DB}"
            ;;
        "import")
            IMPORT="${VALUE}"
            import_cert "${IMPORT}" "${DB}"
            import_crl "${IMPORT}" "${DB}"
            ;;
        "import_key")
            IMPORT="${VALUE}"
            import_key "${IMPORT}" "${DB}"
            ;;
        "crl")
            ISSUER="${VALUE}"
            create_crl "${ISSUER}"
            ;;
        "revoke")
            REVOKE="${VALUE}"
            ;;
        "serial")
            SERIAL="${VALUE}"
            ;;
	"export_key")
	    EXPORT_KEY=1
	    ;;
        "copycrl")
            COPYCRL="${VALUE}"
            copy_crl "${COPYCRL}"
            ;;
        "verify")
            VERIFY="${VALUE}"
            VFY_TIME_OPT=
            TRUST=
            TRUST_AND_DB=
            POLICY=
            FETCH=
            EXP_RESULT=
            REV_OPTS=
            USAGE_OPT=
            ;;
        "at_time")
            VFY_TIME_OPT="-b ${VALUE}"
            ;;
        "cert")
            VERIFY="${VERIFY} ${VALUE}"
            ;;
        "testdb")
            if [ -n "${VALUE}" ]; then
                DB="${VALUE}DB"
            else
                DB=
            fi
            ;;
        "trust")
            TRUST="${TRUST} ${VALUE}"
            ;;
        "trust_and_db")
            TRUST_AND_DB=1
            ;;
        "fetch")
            FETCH=1
            ;;
        "result")
            EXP_RESULT="${VALUE}"
            parse_result
            ;;
        "rev_type")
            REV_OPTS="${REV_OPTS} -g ${VALUE}"
            ;;
        "rev_flags")
            REV_OPTS="${REV_OPTS} -h ${VALUE}"
            ;;
        "rev_mtype")
            REV_OPTS="${REV_OPTS} -m ${VALUE}"
            ;;
        "rev_mflags")
            REV_OPTS="${REV_OPTS} -s ${VALUE}"
            ;;
        "scenario")
            SCENARIO="${VALUE}: "

            CHAINS_DIR="${HOSTDIR}/chains/${VALUE}"
            mkdir -p ${CHAINS_DIR}
            cd ${CHAINS_DIR}

            if [ -n "${MEMLEAK_DBG}" ]; then
                LOGNAME="libpkix-${VALUE}"
                LOGFILE="${LOGDIR}/${LOGNAME}"
            fi

            SCEN_CNT=$(expr ${SCEN_CNT} + 1)
            ;;
        "sleep")
            sleep ${VALUE}
            ;;
        "break")
            break
            ;;
        "check_ocsp")
            TESTNAME="Test that OCSP server is reachable"
            check_ocsp ${VALUE}
            if [ $? -ne 0 ]; then
                html_failed "$TESTNAME"
                break;
            else
                html_passed "$TESTNAME"
            fi
            ;;
        "ku")
            EXT_KU="${VALUE}"
            ;;
        "ns")
            EXT_NS="${VALUE}"
            ;;
        "eku")
            EXT_EKU="${VALUE}"
            ;;
        "usage")
            USAGE_OPT="-u ${VALUE}"
            ;;
        "")
            if [ -n "${ENTITY}" ]; then
                if [ -z "${DB}" ]; then
                    create_entity "${ENTITY}" "${TYPE}"
                fi
                sign_cert "${ENTITY}" "${ISSUER}" "${TYPE}"
                if [ "${TYPE}" = "Bridge" ]; then
                    create_pkcs7 "${ENTITY}"
                fi
		if [ -n "${EXPORT_KEY}" ]; then
		    export_key "${ENTITY}" "${DB}"
		fi
                ENTITY=
            fi

            if [ -n "${VERIFY}" ] && \
               [ -z "$NSS_DISABLE_LIBPKIX" ]; then
                verify_cert "-pp"
		if [ -n "${VERIFY_CLASSIC_ENGINE_TOO}" ] && \
		   [ -z "$NSS_DISABLE_LIBPKIX" ]; then
		    verify_cert ""
		    verify_cert "-p"
		fi
                VERIFY=
            fi

            if [ -n "${REVOKE}" ]; then
                revoke_cert "${REVOKE}" "${DB}"
                REVOKE=
            fi
            ;;
        *)
            if [ `echo ${KEY} | cut -b 1` != "#" ]; then
                echo "Configuration error: Unknown keyword ${KEY}"
                exit 1
            fi
            ;;
        esac
    done

    if [ -n "${MEMLEAK_DBG}" ]; then
        log_parse
        html_msg $? 0 "${SCENARIO}Memory leak checking" 
    fi
}

process_scenario()
{
    SCENARIO_FILE=$1

    > ${AIA_FILES}

    parse_config < "${QADIR}/chains/scenarios/${SCENARIO_FILE}"

    while read AIA_FILE
    do
	rm ${AIA_FILE} 2> /dev/null
    done < ${AIA_FILES}
    rm ${AIA_FILES}
}

# process ipsec.cfg separately
chains_ipsec()
{
    process_scenario "ipsec.cfg"
}

# process mlkem_leaf.cfg separately
chains_mlkem_leaf()
{
    process_scenario "mlkem_leaf.cfg"
}

# process ocspd.cfg separately
chains_ocspd()
{
    process_scenario "ocspd.cfg"
}

# process ocsp.cfg separately
chains_method()
{
    process_scenario "method.cfg"
}

############################# chains_main ##############################
# local shell function to process all testing scenarios
########################################################################
chains_main()
{
    while read LINE 
    do
        [ `echo ${LINE} | cut -b 1` != "#" ] || continue

	[ ${LINE} != 'ipsec.cfg' ] || continue
	[ ${LINE} != 'ocspd.cfg' ] || continue
	[ ${LINE} != 'method.cfg' ] || continue
        [ -z "$NSS_DISABLE_DSA" -o ${LINE} != 'dsa.cfg' ] || continue

	process_scenario ${LINE}
    done < "${CHAINS_SCENARIOS}"
}

################################ main ##################################

chains_init
VERIFY_CLASSIC_ENGINE_TOO=
chains_ocspd
VERIFY_CLASSIC_ENGINE_TOO=1
chains_ipsec
chains_mlkem_leaf
chains_run_httpserv get
chains_method
chains_stop_httpserv
chains_run_httpserv post
chains_method
chains_stop_httpserv
VERIFY_CLASSIC_ENGINE_TOO=
chains_run_httpserv random
chains_main
chains_stop_httpserv
chains_run_httpserv get-unknown
chains_main
chains_stop_httpserv
export NSS_COMBO_SIGNATURES=signonly
chains_run_httpserv random
chains_main
chains_stop_httpserv
export NSS_COMBO_SIGNATURES=vfynonly
chains_run_httpserv random
chains_main
chains_stop_httpserv
chains_cleanup
