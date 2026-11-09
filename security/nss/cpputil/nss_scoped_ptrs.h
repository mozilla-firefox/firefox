/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nss_scoped_ptrs_h__
#define nss_scoped_ptrs_h__

#include <memory>

#include "cert.h"
#include "keyhi.h"
#include "nss.h"
#include "p12.h"
#include "pk11hpke.h"
#include "pk11pqg.h"
#include "pk11pub.h"
#include "pkcs11uri.h"
#include "secmod.h"

struct ScopedDelete {
  void operator()(CERTCertificate* cert) { CERT_DestroyCertificate(cert); }
  void operator()(CERTCertificateList* list) {
    CERT_DestroyCertificateList(list);
  }
  void operator()(CERTDistNames* names) { CERT_FreeDistNames(names); }
  void operator()(CERTName* name) { CERT_DestroyName(name); }
  void operator()(CERTValidity* validity) { CERT_DestroyValidity(validity); }
  void operator()(CERTCertificateRequest* req) {
    CERT_DestroyCertificateRequest(req);
  }
  void operator()(CERTCertList* list) { CERT_DestroyCertList(list); }
  void operator()(CERTSubjectPublicKeyInfo* spki) {
    SECKEY_DestroySubjectPublicKeyInfo(spki);
  }
  void operator()(HpkeContext* context) {
    PK11_HPKE_DestroyContext(context, true);
  }
  void operator()(PK11Context* context) { PK11_DestroyContext(context, true); }
  void operator()(PK11GenericObject* obj) { PK11_DestroyGenericObject(obj); }
  void operator()(PK11SlotInfo* slot) { PK11_FreeSlot(slot); }
  void operator()(PK11SlotList* slots) { PK11_FreeSlotList(slots); }
  void operator()(PK11SymKey* key) { PK11_FreeSymKey(key); }
  void operator()(PK11URI* uri) { PK11URI_DestroyURI(uri); }
  void operator()(PLArenaPool* arena) { PORT_FreeArena(arena, PR_FALSE); }
  void operator()(PQGParams* pqg) { PK11_PQG_DestroyParams(pqg); }
  void operator()(PRFileDesc* fd) { PR_Close(fd); }
  void operator()(SECAlgorithmID* id) { SECOID_DestroyAlgorithmID(id, true); }
  void operator()(SECKEYEncryptedPrivateKeyInfo* e) {
    SECKEY_DestroyEncryptedPrivateKeyInfo(e, true);
  }
  void operator()(SECItem* item) { SECITEM_FreeItem(item, true); }
  void operator()(SECItemArray* array) { SECITEM_FreeArray(array, true); }
  void operator()(SECKEYPublicKey* key) { SECKEY_DestroyPublicKey(key); }
  void operator()(SECKEYPrivateKey* key) { SECKEY_DestroyPrivateKey(key); }
  void operator()(SECKEYPrivateKeyList* list) {
    SECKEY_DestroyPrivateKeyList(list);
  }
  void operator()(SECKEYPrivateKeyInfo* pki) {
    SECKEY_DestroyPrivateKeyInfo(pki, PR_TRUE);
  }
  void operator()(SECMODModule* module) { SECMOD_DestroyModule(module); }
  void operator()(SEC_PKCS12DecoderContext* dcx) {
    SEC_PKCS12DecoderFinish(dcx);
  }
  void operator()(SEC_PKCS7DecoderContext* dcx) {
    SEC_PKCS7ContentInfo* cinfo = SEC_PKCS7DecoderFinish(dcx);
    if (cinfo) {
      SEC_PKCS7DestroyContentInfo(cinfo);
    }
  }
  void operator()(SEC_PKCS7ContentInfo* cinfo) {
    SEC_PKCS7DestroyContentInfo(cinfo);
  }
  void operator()(NSSInitContext* init) { NSS_ShutdownContext(init); }
};

template <class T>
struct ScopedMaybeDelete {
  void operator()(T* ptr) {
    if (ptr) {
      ScopedDelete del;
      del(ptr);
    }
  }
};

#define SCOPED(x) typedef std::unique_ptr<x, ScopedMaybeDelete<x> > Scoped##x

SCOPED(CERTCertList);
SCOPED(CERTCertificate);
SCOPED(CERTCertificateList);
SCOPED(CERTCertificateRequest);
SCOPED(CERTDistNames);
SCOPED(CERTName);
SCOPED(CERTSubjectPublicKeyInfo);
SCOPED(CERTValidity);
SCOPED(HpkeContext);
SCOPED(NSSInitContext);
SCOPED(PK11Context);
SCOPED(PK11GenericObject);
SCOPED(PK11SlotInfo);
SCOPED(PK11SlotList);
SCOPED(PK11SymKey);
SCOPED(PK11URI);
SCOPED(PLArenaPool);
SCOPED(PQGParams);
SCOPED(PRFileDesc);
SCOPED(SECAlgorithmID);
SCOPED(SECItem);
SCOPED(SECItemArray);
SCOPED(SECKEYEncryptedPrivateKeyInfo);
SCOPED(SECKEYPrivateKey);
SCOPED(SECKEYPrivateKeyList);
SCOPED(SECKEYPrivateKeyInfo);
SCOPED(SECKEYPublicKey);
SCOPED(SECMODModule);
SCOPED(SEC_PKCS12DecoderContext);
SCOPED(SEC_PKCS7DecoderContext);
SCOPED(SEC_PKCS7ContentInfo);

#undef SCOPED

struct StackSECItem : public SECItem {
  StackSECItem() : SECItem({siBuffer, nullptr, 0}) {}
  ~StackSECItem() { Reset(); }
  void Reset() { SECITEM_FreeItem(this, PR_FALSE); }
};

#endif  // nss_scoped_ptrs_h__
