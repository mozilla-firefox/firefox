# Generate input to certutil
certscript() {
  ca=n
  while [ $# -gt 0 ]; do
    case $1 in
      sign) echo 0 ;;
      kex) echo 2 ;;
      ca) echo 5;echo 6;ca=y ;;
    esac; shift
  done;
  echo 9
  echo n
  echo $ca
  echo
  echo n
}

# $1: name
# $2: type
# $3+: usages: sign or kex
make_cert() {
  name=$1
  type=$2

  # defaults
  type_args=()
  trust=',,'
  sign=(-x)
  sighash=(-Z SHA256)

  case $type in
    dsa) type_args=(-g 1024) ;;
    rsa) type_args=(-g 1024) ;;
    rsa2048) type_args=(-g 2048);type=rsa ;;
    rsa8192) type_args=(-g 8192);type=rsa ;;
    rsapss) type_args=(-g 1024 --pss);type=rsa ;;
    rsapss384) type_args=(-g 1024 --pss);type=rsa;sighash=(-Z SHA384) ;;
    rsapss512) type_args=(-g 2048 --pss);type=rsa;sighash=(-Z SHA512) ;;
    rsapss_noparam) type_args=(-g 2048 --pss);type=rsa;sighash=() ;;
    mldsa44) type_args=(-q ml-dsa-44);type=mldsa;sighash=() ;;
    mldsa65) type_args=(-q ml-dsa-65);type=mldsa;sighash=() ;;
    mldsa87) type_args=(-q ml-dsa-87);type=mldsa;sighash=() ;;
    p256) type_args=(-q nistp256);type=ec ;;
    p384) type_args=(-q secp384r1);type=ec ;;
    p521) type_args=(-q secp521r1);type=ec ;;
    rsa_ca) type_args=(-g 1024);trust='CT,CT,CT';type=rsa ;;
    rsa_chain) type_args=(-g 1024);sign=(-c rsa_ca);type=rsa;;
    rsapss_ca) type_args=(-g 1024 --pss);trust='CT,CT,CT';type=rsa ;;
    rsapss_chain) type_args=(-g 1024);sign=(-c rsa_pss_ca);type=rsa;;
    rsa_ca_rsapss_chain) type_args=(-g 1024 --pss-sign);sign=(-c rsa_ca);type=rsa;;
    mldsa44_ca) type_args=(-q ml-dsa-44);trust='CT,CT,CT';type=mldsa ;;
    mldsa65_ca) type_args=(-q ml-dsa-65);trust='CT,CT,CT';type=mldsa ;;
    mldsa87_ca) type_args=(-q ml-dsa-87);trust='CT,CT,CT';type=mldsa ;;
    mldsa44_chain) type_args=(-q ml-dsa-44);sign=(-c mldsa44_ca);type=mldsa;;
    mldsa65_chain) type_args=(-q ml-dsa-65);sign=(-c mldsa65_ca);type=mldsa;;
    mldsa87_chain) type_args=(-q ml-dsa-87);sign=(-c mldsa87_ca);type=mldsa;;
    ecdh_rsa) type_args=(-q nistp256);sign=(-c rsa_ca);type=ec ;;
    delegator_mldsa44)
        touch empty.txt
        type_args=(-q ml-dsa-44 --extGeneric 1.3.6.1.4.1.44363.44:not-critical:empty.txt)
        type=mldsa
        ;;
    delegator_mldsa65)
        touch empty.txt
        type_args=(-q ml-dsa-65 --extGeneric 1.3.6.1.4.1.44363.44:not-critical:empty.txt)
        type=mldsa
        ;;
    delegator_mldsa87)
        touch empty.txt
        type_args=(-q ml-dsa-87 --extGeneric 1.3.6.1.4.1.44363.44:not-critical:empty.txt)
        type=mldsa
        ;;
    delegator_p256)
        touch empty.txt
        type_args=(-q nistp256 --extGeneric 1.3.6.1.4.1.44363.44:not-critical:empty.txt)
        type=ec
        ;;
    delegator_rsae2048)
        touch empty.txt
        type_args=(-g 2048 --extGeneric 1.3.6.1.4.1.44363.44:not-critical:empty.txt)
        type=rsa
        ;;
    delegator_rsa_pss2048)
        touch empty.txt
        type_args=(-g 2048 --pss --extGeneric 1.3.6.1.4.1.44363.44:not-critical:empty.txt)
        type=rsa
        ;;
    unknown_ext)
        touch empty.txt
        type_args=(-q nistp256 --extKeyUsage serverAuth --extGeneric 1.3.6.1.4.1.13769.666.666.666.1.500.9.3:not-critical:empty.txt)
        type=ec
        ;;
    unknown_critical_ext)
        touch empty.txt
        type_args=(-q nistp256 --extKeyUsage serverAuth --extGeneric 1.3.6.1.4.1.13769.666.666.666.1.500.9.3:critical:empty.txt)
        type=ec
        ;;
  esac
  msg="create certificate: $@"
  shift 2
  counter=$(($counter + 1))
  cmd=(${BINDIR}/certutil -S \
    -z "$R_NOISE_FILE" -d "$PROFILEDIR" \
    -n $name -s "CN=$name" -t "$trust" "${sign[@]}" -m "$counter" \
    -w -2 -v 120 -k "$type" "${type_args[@]}" "${sighash[@]}" -1 -2)
  echo "${cmd[@]}"
  certscript $@ | "${cmd[@]}"
  html_msg $? 0 "$msg"
}
