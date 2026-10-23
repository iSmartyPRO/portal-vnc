#include "ca.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/x509_csr.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

namespace pvnc {

namespace {

std::string mbedErr(const char* what, int rc) {
  char buf[128] = {0};
  mbedtls_strerror(rc, buf, sizeof(buf));
  return std::string(what) + ": " + buf;
}
void check(const char* what, int rc) {
  if (rc != 0) throw CaError(mbedErr(what, rc));
}

// Seeded CSPRNG for all CA operations.
struct Rng {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  Rng() {
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    const char* pers = "pvnc-admin-ca";
    check("ctr_drbg_seed",
          mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                (const unsigned char*)pers, std::strlen(pers)));
  }
  ~Rng() {
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
  }
};

// NUL-terminated PEM produced by an mbedtls *_pem writer into a grown buffer.
template <typename Fn>
std::string writePem(const char* what, Fn fn) {
  std::vector<unsigned char> buf(16384);
  int rc = fn(buf.data(), buf.size());
  check(what, rc);
  return std::string(reinterpret_cast<char*>(buf.data()));  // writers NUL-terminate
}

// "YYYYMMDDHHMMSS" in UTC for a time offset from now.
std::string utcStamp(std::time_t t) {
  std::tm tmv{};
  gmtime_s(&tmv, &t);
  char s[16];
  std::snprintf(s, sizeof(s), "%04d%02d%02d%02d%02d%02d", tmv.tm_year + 1900,
                tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return s;
}

void parsePrivKey(mbedtls_pk_context& pk, const std::string& pem, Rng& rng) {
  check("pk_parse_key",
        mbedtls_pk_parse_key(&pk, (const unsigned char*)pem.data(), pem.size() + 1,
                             nullptr, 0, mbedtls_ctr_drbg_random, &rng.drbg));
}

// Applies the common leaf extensions and signs the crt, returning its PEM.
std::string finishLeaf(mbedtls_x509write_cert& crt, Rng& rng) {
  check("crt_basic_constraints", mbedtls_x509write_crt_set_basic_constraints(&crt, 0, -1));
  check("crt_key_usage",
        mbedtls_x509write_crt_set_key_usage(
            &crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE | MBEDTLS_X509_KU_KEY_ENCIPHERMENT));
  mbedtls_asn1_sequence ekuClient{};
  ekuClient.buf.tag = MBEDTLS_ASN1_OID;
  ekuClient.buf.p = (unsigned char*)MBEDTLS_OID_CLIENT_AUTH;
  ekuClient.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_CLIENT_AUTH);
  mbedtls_asn1_sequence ekuServer{};
  ekuServer.buf.tag = MBEDTLS_ASN1_OID;
  ekuServer.buf.p = (unsigned char*)MBEDTLS_OID_SERVER_AUTH;
  ekuServer.buf.len = MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH);
  ekuServer.next = &ekuClient;
  check("crt_ext_key_usage", mbedtls_x509write_crt_set_ext_key_usage(&crt, &ekuServer));
  check("crt_subject_key_id", mbedtls_x509write_crt_set_subject_key_identifier(&crt));
  check("crt_authority_key_id", mbedtls_x509write_crt_set_authority_key_identifier(&crt));
  return writePem("crt_write_pem", [&](unsigned char* b, size_t n) {
    return mbedtls_x509write_crt_pem(&crt, b, n, mbedtls_ctr_drbg_random, &rng.drbg);
  });
}

}  // namespace

KeyPair generateRsaKey(int bits) {
  Rng rng;
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  try {
    check("pk_setup", mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)));
    check("rsa_gen_key",
          mbedtls_rsa_gen_key(mbedtls_pk_rsa(pk), mbedtls_ctr_drbg_random, &rng.drbg,
                              (unsigned)bits, 65537));
    KeyPair kp;
    kp.privPem = writePem("pk_write_key_pem", [&](unsigned char* b, size_t n) {
      return mbedtls_pk_write_key_pem(&pk, b, n);
    });
    kp.pubPem = writePem("pk_write_pubkey_pem", [&](unsigned char* b, size_t n) {
      return mbedtls_pk_write_pubkey_pem(&pk, b, n);
    });
    mbedtls_pk_free(&pk);
    return kp;
  } catch (...) {
    mbedtls_pk_free(&pk);
    throw;
  }
}

std::string makeSelfSignedCa(const std::string& caKeyPem, const std::string& cn, int days) {
  Rng rng;
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  mbedtls_x509write_cert crt;
  mbedtls_x509write_crt_init(&crt);
  mbedtls_mpi serial;
  mbedtls_mpi_init(&serial);
  try {
    parsePrivKey(key, caKeyPem, rng);
    std::string dn = "CN=" + cn;
    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, &key);
    mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    check("crt_subject_name", mbedtls_x509write_crt_set_subject_name(&crt, dn.c_str()));
    check("crt_issuer_name", mbedtls_x509write_crt_set_issuer_name(&crt, dn.c_str()));
    check("mpi_lset", mbedtls_mpi_lset(&serial, 1));
    check("crt_set_serial", mbedtls_x509write_crt_set_serial(&crt, &serial));
    std::time_t now = std::time(nullptr);
    std::string nb = utcStamp(now - 3600), na = utcStamp(now + (std::time_t)days * 86400);
    check("crt_validity", mbedtls_x509write_crt_set_validity(&crt, nb.c_str(), na.c_str()));
    check("crt_basic_constraints", mbedtls_x509write_crt_set_basic_constraints(&crt, 1, -1));
    check("crt_key_usage",
          mbedtls_x509write_crt_set_key_usage(
              &crt, MBEDTLS_X509_KU_KEY_CERT_SIGN | MBEDTLS_X509_KU_CRL_SIGN));
    check("crt_subject_key_id", mbedtls_x509write_crt_set_subject_key_identifier(&crt));
    std::string pem = writePem("crt_write_pem", [&](unsigned char* b, size_t n) {
      return mbedtls_x509write_crt_pem(&crt, b, n, mbedtls_ctr_drbg_random, &rng.drbg);
    });
    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    return pem;
  } catch (...) {
    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    throw;
  }
}

std::string makeCsr(const std::string& keyPem, const std::string& cn) {
  Rng rng;
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  mbedtls_x509write_csr req;
  mbedtls_x509write_csr_init(&req);
  try {
    parsePrivKey(key, keyPem, rng);
    std::string dn = "CN=" + cn;
    mbedtls_x509write_csr_set_md_alg(&req, MBEDTLS_MD_SHA256);
    mbedtls_x509write_csr_set_key(&req, &key);
    check("csr_subject_name", mbedtls_x509write_csr_set_subject_name(&req, dn.c_str()));
    std::string pem = writePem("csr_write_pem", [&](unsigned char* b, size_t n) {
      return mbedtls_x509write_csr_pem(&req, b, n, mbedtls_ctr_drbg_random, &rng.drbg);
    });
    mbedtls_x509write_csr_free(&req);
    mbedtls_pk_free(&key);
    return pem;
  } catch (...) {
    mbedtls_x509write_csr_free(&req);
    mbedtls_pk_free(&key);
    throw;
  }
}

std::string issueCert(const std::string& caCertPem, const std::string& caKeyPem,
                      const std::string& subjectKeyOrCsrPem, const std::string& cn,
                      int days, uint64_t serial) {
  Rng rng;
  mbedtls_pk_context caKey, subjKey;
  mbedtls_pk_init(&caKey);
  mbedtls_pk_init(&subjKey);
  mbedtls_x509_crt caCert;
  mbedtls_x509_crt_init(&caCert);
  mbedtls_x509_csr csr;
  mbedtls_x509_csr_init(&csr);
  mbedtls_x509write_cert crt;
  mbedtls_x509write_crt_init(&crt);
  mbedtls_mpi serialMpi;
  mbedtls_mpi_init(&serialMpi);
  bool fromCsr = subjectKeyOrCsrPem.find("REQUEST") != std::string::npos;
  try {
    parsePrivKey(caKey, caKeyPem, rng);
    check("ca_crt_parse",
          mbedtls_x509_crt_parse(&caCert, (const unsigned char*)caCertPem.data(),
                                 caCertPem.size() + 1));
    // Issuer DN taken from the CA certificate's subject.
    char issuer[512];
    int ilen = mbedtls_x509_dn_gets(issuer, sizeof(issuer), &caCert.subject);
    if (ilen < 0) throw CaError("x509_dn_gets(issuer) failed");

    std::string subjectCn = cn;
    mbedtls_pk_context* subjectPk = nullptr;
    if (fromCsr) {
      check("csr_parse",
            mbedtls_x509_csr_parse(&csr, (const unsigned char*)subjectKeyOrCsrPem.data(),
                                   subjectKeyOrCsrPem.size() + 1));
      subjectPk = &csr.pk;
      if (subjectCn.empty()) {
        char sn[512];
        int slen = mbedtls_x509_dn_gets(sn, sizeof(sn), &csr.subject);
        if (slen > 0) subjectCn = std::string(sn, slen);  // full DN form
      }
    } else {
      check("pk_parse_public_key",
            mbedtls_pk_parse_public_key(&subjKey,
                                        (const unsigned char*)subjectKeyOrCsrPem.data(),
                                        subjectKeyOrCsrPem.size() + 1));
      subjectPk = &subjKey;
    }
    if (subjectCn.empty()) subjectCn = "PortalVNC";
    // Subject may already be a DN ("CN=..., O=..."); plain CN gets the prefix.
    std::string subjectDn =
        (subjectCn.find('=') != std::string::npos) ? subjectCn : ("CN=" + subjectCn);

    mbedtls_x509write_crt_set_version(&crt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&crt, subjectPk);
    mbedtls_x509write_crt_set_issuer_key(&crt, &caKey);
    check("crt_subject_name", mbedtls_x509write_crt_set_subject_name(&crt, subjectDn.c_str()));
    check("crt_issuer_name", mbedtls_x509write_crt_set_issuer_name(&crt, issuer));
    check("mpi_lset", mbedtls_mpi_lset(&serialMpi, (mbedtls_mpi_sint)serial));
    check("crt_set_serial", mbedtls_x509write_crt_set_serial(&crt, &serialMpi));
    std::time_t now = std::time(nullptr);
    std::string nb = utcStamp(now - 3600), na = utcStamp(now + (std::time_t)days * 86400);
    check("crt_validity", mbedtls_x509write_crt_set_validity(&crt, nb.c_str(), na.c_str()));
    std::string pem = finishLeaf(crt, rng);

    mbedtls_mpi_free(&serialMpi);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_x509_csr_free(&csr);
    mbedtls_x509_crt_free(&caCert);
    mbedtls_pk_free(&subjKey);
    mbedtls_pk_free(&caKey);
    return pem;
  } catch (...) {
    mbedtls_mpi_free(&serialMpi);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_x509_csr_free(&csr);
    mbedtls_x509_crt_free(&caCert);
    mbedtls_pk_free(&subjKey);
    mbedtls_pk_free(&caKey);
    throw;
  }
}

std::string signCsr(const std::string& caCertPem, const std::string& caKeyPem,
                    const std::string& csrPem, int days, uint64_t serial) {
  return issueCert(caCertPem, caKeyPem, csrPem, "", days, serial);
}

}  // namespace pvnc
