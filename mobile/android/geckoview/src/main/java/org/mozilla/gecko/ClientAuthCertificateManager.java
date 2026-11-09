/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.gecko;

import android.security.KeyChain;
import android.security.KeyChainException;
import android.util.Log;
import java.security.InvalidKeyException;
import java.security.NoSuchAlgorithmException;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.Signature;
import java.security.SignatureException;
import java.security.cert.CertificateEncodingException;
import java.security.cert.X509Certificate;
import java.security.interfaces.ECPublicKey;
import java.security.interfaces.RSAPublicKey;
import java.util.ArrayList;
import java.util.Arrays;
import javax.crypto.BadPaddingException;
import javax.crypto.Cipher;
import javax.crypto.IllegalBlockSizeException;
import javax.crypto.NoSuchPaddingException;
import org.mozilla.gecko.annotation.WrapForJNI;
import org.mozilla.gecko.mozglue.JNIObject;

// ClientAuthCertificateManager is a singleton that manages any client
// authentication certificates that the user has made available by selecting
// them via the dialog created by `KeyChain.choosePrivateKeyAlias` (see
// `CertificatePicker` in `mozilla.components.feature.prompts.certificate`).
// Once a certificate has been made available, this class can use its alias
// to obtain the bytes of the certificate and issuer chain as well as create
// signatures using the private key of the certificate.
public class ClientAuthCertificateManager {

  // It would make sense to make the log tag "ClientAuthCertificateManager",
  // but that's 28 characters, and the limit is 23.
  private static final String LOGTAG = "ClientAuthCertManager";
  private static ClientAuthCertificateManager sClientAuthCertificateManager = null;

  // Internal list of cached client auth certificates.
  private final ArrayList<ClientAuthCertificate> mCertificates =
      new ArrayList<ClientAuthCertificate>();

  private ClientAuthCertificateManager() {}

  private static ClientAuthCertificateManager getSingleton() {
    synchronized (ClientAuthCertificateManager.class) {
      if (sClientAuthCertificateManager == null) {
        sClientAuthCertificateManager = new ClientAuthCertificateManager();
      }
      return sClientAuthCertificateManager;
    }
  }

  // Find the cached client auth certificate with the given alias, if any.
  private ClientAuthCertificate findCertificateByAlias(final String alias) {
    for (final ClientAuthCertificate certificate : mCertificates) {
      if (certificate.mAlias.equals(alias)) {
        return certificate;
      }
    }
    return null;
  }

  // Given the alias of a certificate, return its bytes, if available.
  // This also caches the certificate for later use.
  @WrapForJNI(calledFrom = "any")
  private static byte[] getCertificateFromAlias(final String alias) {
    // The certificate may have already been cached. If so, return it.
    final ClientAuthCertificateManager singleton = getSingleton();
    synchronized (singleton) {
      ClientAuthCertificate certificate = singleton.findCertificateByAlias(alias);
      if (certificate != null) {
        return certificate.getCertificateBytes();
      }
      // Otherwise, get the certificate chain corresponding to the alias, make a
      // ClientAuthCertificate out of that, cache it, and return it.
      final X509Certificate[] chain;
      try {
        chain = KeyChain.getCertificateChain(GeckoAppShell.getApplicationContext(), alias);
      } catch (final InterruptedException | KeyChainException e) {
        Log.e(LOGTAG, "getCertificateChain failed", e);
        return null;
      }
      if (chain == null || chain.length < 1) {
        return null;
      }
      try {
        certificate = new ClientAuthCertificate(alias, chain);
        singleton.mCertificates.add(certificate);
        return certificate.getCertificateBytes();
      } catch (final UnsuitableCertificateException e) {
        Log.e(LOGTAG, "unsuitable certificate", e);
      }
    }
    return null;
  }

  // List all known client authentication certificates.
  @WrapForJNI(calledFrom = "any")
  private static ClientAuthCertificate[] getClientAuthCertificates() {
    final ClientAuthCertificateManager singleton = ClientAuthCertificateManager.getSingleton();
    synchronized (singleton) {
      return singleton.mCertificates.toArray(new ClientAuthCertificate[0]);
    }
  }

  // Find the cached certificate with the given bytes, if any.
  private ClientAuthCertificate findCertificateByBytes(final byte[] certificateBytes) {
    for (final ClientAuthCertificate certificate : mCertificates) {
      if (Arrays.equals(certificate.getCertificateBytes(), certificateBytes)) {
        return certificate;
      }
    }
    return null;
  }

  // Given the bytes of a certificate previously returned by
  // `getClientAuthCertificates()`, returns the issuer certificate chain bytes.
  @WrapForJNI(calledFrom = "any")
  private static byte[][] getCertificateIssuersBytes(final byte[] certificateBytes) {
    final ClientAuthCertificateManager singleton = ClientAuthCertificateManager.getSingleton();
    synchronized (singleton) {
      final ClientAuthCertificate certificate = singleton.findCertificateByBytes(certificateBytes);
      if (certificate == null) {
        return null;
      }
      return certificate.getIssuersBytes();
    }
  }

  // Given the bytes of a certificate previously returned by
  // `getClientAuthCertificates()`, data to sign, and an algorithm, signs the
  // data using the algorithm. "NoneWithRSA" and "NoneWithECDSA" are supported,
  // as well as "raw", which corresponds to an RSA encryption operation with no
  // padding.
  @WrapForJNI(calledFrom = "any")
  private static byte[] sign(
      final byte[] certificateBytes, final byte[] data, final String algorithm) {
    final ClientAuthCertificateManager singleton = ClientAuthCertificateManager.getSingleton();
    synchronized (singleton) {
      final ClientAuthCertificate certificate = singleton.findCertificateByBytes(certificateBytes);
      if (certificate == null) {
        return null;
      }
      final PrivateKey key;
      try {
        key = KeyChain.getPrivateKey(GeckoAppShell.getApplicationContext(), certificate.mAlias);
      } catch (final InterruptedException | KeyChainException e) {
        Log.e(LOGTAG, "getPrivateKey failed", e);
        return null;
      }
      if (key == null) {
        Log.e(LOGTAG, "couldn't get private key");
        return null;
      }

      if (algorithm.equals("raw")) {
        final Cipher cipher;
        try {
          cipher = Cipher.getInstance("RSA/None/NoPadding");
        } catch (final NoSuchAlgorithmException | NoSuchPaddingException e) {
          Log.e(LOGTAG, "getInstance failed", e);
          return null;
        }
        try {
          cipher.init(Cipher.ENCRYPT_MODE, key);
        } catch (final InvalidKeyException e) {
          Log.e(LOGTAG, "init failed", e);
          return null;
        }
        try {
          return cipher.doFinal(data);
        } catch (final BadPaddingException | IllegalBlockSizeException e) {
          Log.e(LOGTAG, "doFinal failed", e);
          return null;
        }
      }

      if (!algorithm.equals("NoneWithRSA") && !algorithm.equals("NoneWithECDSA")) {
        Log.e(LOGTAG, "given unexpected algorithm " + algorithm);
        return null;
      }

      final Signature signature;
      try {
        signature = Signature.getInstance(algorithm);
      } catch (final NoSuchAlgorithmException e) {
        Log.e(LOGTAG, "getInstance failed", e);
        return null;
      }
      try {
        signature.initSign(key);
      } catch (final InvalidKeyException e) {
        Log.e(LOGTAG, "initSign failed", e);
        return null;
      }
      try {
        signature.update(data);
      } catch (final SignatureException e) {
        Log.e(LOGTAG, "update failed", e);
        return null;
      }
      try {
        return signature.sign();
      } catch (final SignatureException e) {
        Log.e(LOGTAG, "sign failed", e);
        return null;
      }
    }
  }

  // Helper exception class thrown upon encountering an unsuitable certificate,
  // where "unsuitable" means that the implementation couldn't gather the
  // information necessary for the rest of gecko to use it.
  private static class UnsuitableCertificateException extends Exception {
    private static final long serialVersionUID = 1L;

    public UnsuitableCertificateException(final String message) {
      super(message);
    }
  }

  // Helper class returned by
  // `ClientAuthCertificateManager.getClientAuthCertificates()`. Holds the bytes
  // of the certificate, the bytes of the issuer certificate chain, bytes
  // representing relevant data about the public key, and the type of key.
  // In particular, for RSA keys, the key parameter bytes represent the public
  // modulus of the key. For EC keys, the key parameter bytes represent the
  // encoded subject public key info, which importantly contains the OID
  // identifying the curve of the key.
  private static class ClientAuthCertificate extends JNIObject {
    private static final String LOGTAG = "ClientAuthCertificate";
    // Object type value for RSA keys
    private static int sRSAKey = 2;
    // Object type value for EC keys
    private static int sECKey = 3;

    private String mAlias;
    private byte[] mCertificateBytes;
    private byte[][] mIssuersBytes;
    private byte[] mKeyParameters;
    private int mType;

    ClientAuthCertificate(final String alias, final X509Certificate[] x509Certificates)
        throws UnsuitableCertificateException {
      mAlias = alias;
      final ArrayList<byte[]> issuersBytes = new ArrayList<byte[]>();
      for (final X509Certificate certificate : x509Certificates) {
        if (mCertificateBytes == null) {
          try {
            mCertificateBytes = certificate.getEncoded();
          } catch (final CertificateEncodingException cee) {
            Log.e(LOGTAG, "getEncoded() failed", cee);
            throw new UnsuitableCertificateException("couldn't get certificate bytes");
          }
        } else {
          try {
            issuersBytes.add(certificate.getEncoded());
          } catch (final CertificateEncodingException cee) {
            Log.e(LOGTAG, "getEncoded() failed", cee);
            // This certificate may still be usable.
            break;
          }
        }
      }
      mIssuersBytes = issuersBytes.toArray(new byte[0][0]);
      final PublicKey publicKey = x509Certificates[0].getPublicKey();
      if (publicKey instanceof RSAPublicKey) {
        mKeyParameters = ((RSAPublicKey) publicKey).getModulus().toByteArray();
        mType = sRSAKey;
      } else if (publicKey instanceof ECPublicKey) {
        // getEncoded() actually returns the SPKI. This leaves it to osclientcerts
        // to decode into the OID identifying the curve.
        mKeyParameters = publicKey.getEncoded();
        mType = sECKey;
      } else {
        throw new UnsuitableCertificateException("unsupported key type");
      }
    }

    @WrapForJNI
    @Override // JNIObject
    protected native void disposeNative();

    @WrapForJNI(calledFrom = "any")
    public byte[] getCertificateBytes() {
      return mCertificateBytes;
    }

    @WrapForJNI(calledFrom = "any")
    public byte[][] getIssuersBytes() {
      return mIssuersBytes;
    }

    @WrapForJNI(calledFrom = "any")
    private byte[] getKeyParameters() {
      return mKeyParameters;
    }

    @WrapForJNI(calledFrom = "any")
    private int getType() {
      return mType;
    }
  }
}
