/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://w3c.github.io/webcrypto/
 * https://wicg.github.io/webcrypto-modern-algos/
 */

typedef DOMString KeyType;
typedef DOMString KeyUsage;
typedef DOMString NamedCurve;
typedef Uint8Array BigInteger;

/***** Algorithm dictionaries *****/

dictionary Algorithm {
  required DOMString name;
};

[GenerateInit]
dictionary AesCbcParams : Algorithm {
  required BufferSource iv;
};

[GenerateInit]
dictionary AesCtrParams : Algorithm {
  required BufferSource counter;
  required [EnforceRange] octet length;
};

[GenerateInit]
dictionary AesGcmParams : Algorithm {
  required BufferSource iv;
  BufferSource additionalData;
  [EnforceRange] octet tagLength;
};

dictionary HmacImportParams : Algorithm {
  required AlgorithmIdentifier hash;
};

[GenerateInit]
dictionary Pbkdf2Params : Algorithm {
  required BufferSource salt;
  required [EnforceRange] unsigned long iterations;
  required AlgorithmIdentifier hash;
};

[GenerateInit]
dictionary RsaHashedImportParams {
  required AlgorithmIdentifier hash;
};

dictionary AesKeyGenParams : Algorithm {
  required [EnforceRange] unsigned short length;
};

[GenerateInit]
dictionary HmacKeyGenParams : Algorithm {
  required AlgorithmIdentifier hash;
  [EnforceRange] unsigned long length;
};

[GenerateInit]
dictionary RsaHashedKeyGenParams : Algorithm {
  required [EnforceRange] unsigned long modulusLength;
  required BigInteger publicExponent;
  required AlgorithmIdentifier hash;
};

[GenerateInit]
dictionary RsaOaepParams : Algorithm {
  BufferSource label;
};

[GenerateInit]
dictionary RsaPssParams : Algorithm {
  required [EnforceRange] unsigned long saltLength;
};

[GenerateInit]
dictionary EcKeyGenParams : Algorithm {
  required NamedCurve namedCurve;
};

[GenerateInit]
dictionary AesDerivedKeyParams : Algorithm {
  required [EnforceRange] unsigned long length;
};

[GenerateInit]
dictionary HmacDerivedKeyParams : HmacImportParams {
  [EnforceRange] unsigned long length;
};

[GenerateInit]
dictionary EcdhKeyDeriveParams : Algorithm {
  required CryptoKey public;
};

[GenerateInit]
dictionary DhImportKeyParams : Algorithm {
  required BigInteger prime;
  required BigInteger generator;
};

[GenerateInit]
dictionary EcdsaParams : Algorithm {
  required AlgorithmIdentifier hash;
};

[GenerateInit]
dictionary EcKeyImportParams : Algorithm {
  NamedCurve namedCurve;
};

[GenerateInit]
dictionary HkdfParams : Algorithm {
  required AlgorithmIdentifier hash;
  required BufferSource salt;
  required BufferSource info;
};

/***** JWK *****/

dictionary RsaOtherPrimesInfo {
  // The following fields are defined in Section 6.3.2.7 of JSON Web Algorithms
  required DOMString r;
  required DOMString d;
  required DOMString t;
};

[GenerateInitFromJSON, GenerateToJSON]
dictionary JsonWebKey {
  // The following fields are defined in Section 3.1 of JSON Web Key
  required DOMString kty;
  DOMString use;
  sequence<DOMString> key_ops;
  DOMString alg;

  // The following fields are defined in JSON Web Key Parameters Registration
  boolean ext;

  // The following fields are defined in Section 6 of JSON Web Algorithms
  DOMString crv;
  DOMString x;
  DOMString y;
  DOMString d;
  DOMString n;
  DOMString e;
  DOMString p;
  DOMString q;
  DOMString dp;
  DOMString dq;
  DOMString qi;
  sequence<RsaOtherPrimesInfo> oth;
  DOMString k;
};


/***** The Main API *****/

[Serializable,
 SecureContext,
 Exposed=(Window,Worker)]
interface CryptoKey {
  readonly attribute KeyType type;
  readonly attribute boolean extractable;
  [Cached, Constant, Throws] readonly attribute object algorithm;
  [Cached, Constant, Frozen] readonly attribute sequence<KeyUsage> usages;
};

[GenerateConversionToJS]
dictionary CryptoKeyPair {
  required CryptoKey publicKey;
  required CryptoKey privateKey;
};

typedef DOMString KeyFormat;
typedef (object or DOMString) AlgorithmIdentifier;

[Exposed=(Window,Worker),
 SecureContext]
interface SubtleCrypto {
  [NewObject]
  Promise<any> encrypt(AlgorithmIdentifier algorithm,
                       CryptoKey key,
                       BufferSource data);
  [NewObject]
  Promise<any> decrypt(AlgorithmIdentifier algorithm,
                       CryptoKey key,
                       BufferSource data);
  [NewObject]
  Promise<any> sign(AlgorithmIdentifier algorithm,
                     CryptoKey key,
                     BufferSource data);
  [NewObject]
  Promise<any> verify(AlgorithmIdentifier algorithm,
                      CryptoKey key,
                      BufferSource signature,
                      BufferSource data);
  [NewObject]
  Promise<any> digest(AlgorithmIdentifier algorithm,
                      BufferSource data);

  [NewObject]
  Promise<any> generateKey(AlgorithmIdentifier algorithm,
                           boolean extractable,
                           sequence<KeyUsage> keyUsages );
  [NewObject]
  Promise<any> deriveKey(AlgorithmIdentifier algorithm,
                         CryptoKey baseKey,
                         AlgorithmIdentifier derivedKeyType,
                         boolean extractable,
                         sequence<KeyUsage> keyUsages );
  [NewObject]
  Promise<any> deriveBits(AlgorithmIdentifier algorithm,
                          CryptoKey baseKey,
                          optional [EnforceRange] unsigned long? length = null);

  [NewObject]
  Promise<any> importKey(KeyFormat format,
                         object keyData,
                         AlgorithmIdentifier algorithm,
                         boolean extractable,
                         sequence<KeyUsage> keyUsages );
  [NewObject]
  Promise<any> exportKey(KeyFormat format, CryptoKey key);

  [NewObject]
  Promise<any> wrapKey(KeyFormat format,
                       CryptoKey key,
                       CryptoKey wrappingKey,
                       AlgorithmIdentifier wrapAlgorithm);

  [NewObject]
  Promise<any> unwrapKey(KeyFormat format,
                         BufferSource wrappedKey,
                         CryptoKey unwrappingKey,
                         AlgorithmIdentifier unwrapAlgorithm,
                         AlgorithmIdentifier unwrappedKeyAlgorithm,
                         boolean extractable,
                         sequence<KeyUsage> keyUsages );
};

// https://wicg.github.io/webcrypto-modern-algos/

[GenerateConversionToJS]
dictionary EncapsulatedKey {
  required CryptoKey sharedKey;
  required ArrayBuffer ciphertext;
};

[GenerateConversionToJS]
dictionary EncapsulatedBits {
  required ArrayBuffer sharedKey;
  required ArrayBuffer ciphertext;
};

partial interface SubtleCrypto {
  [NewObject, Pref="dom.webcrypto.encapsulation.enabled"]
  Promise<EncapsulatedKey> encapsulateKey(AlgorithmIdentifier encapsulationAlgorithm,
                                          CryptoKey encapsulationKey,
                                          AlgorithmIdentifier sharedKeyAlgorithm,
                                          boolean extractable,
                                          sequence<KeyUsage> keyUsages );
  [NewObject, Pref="dom.webcrypto.encapsulation.enabled"]
  Promise<EncapsulatedBits> encapsulateBits(AlgorithmIdentifier encapsulationAlgorithm,
                                            CryptoKey encapsulationKey);
  [NewObject, Pref="dom.webcrypto.encapsulation.enabled"]
  Promise<CryptoKey> decapsulateKey(AlgorithmIdentifier decapsulationAlgorithm,
                                    CryptoKey decapsulationKey,
                                    BufferSource ciphertext,
                                    AlgorithmIdentifier sharedKeyAlgorithm,
                                    boolean extractable,
                                    sequence<KeyUsage> keyUsages );
  [NewObject, Pref="dom.webcrypto.encapsulation.enabled"]
  Promise<ArrayBuffer> decapsulateBits(AlgorithmIdentifier decapsulationAlgorithm,
                                       CryptoKey decapsulationKey,
                                       BufferSource ciphertext);
};

partial dictionary JsonWebKey {
  // The following fields are defined in RFC 9964
  DOMString pub;
  DOMString priv;
};
