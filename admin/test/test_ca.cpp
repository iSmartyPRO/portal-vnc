#include <catch2/catch_test_macros.hpp>

#include <mbedtls/x509_crt.h>

#include "ca.h"

using namespace pvnc;

namespace {
// Parses a PEM cert chain; caller frees.
void parse(mbedtls_x509_crt& c, const std::string& pem) {
  REQUIRE(mbedtls_x509_crt_parse(&c, (const unsigned char*)pem.data(), pem.size() + 1) == 0);
}
}  // namespace

TEST_CASE("issued leaf verifies against the CA", "[ca]") {
  KeyPair caKey = generateRsaKey(2048);
  std::string caCert = makeSelfSignedCa(caKey.privPem, "PortalVNC CA", 3650);

  KeyPair leaf = generateRsaKey(2048);
  std::string leafCert = issueCert(caCert, caKey.privPem, leaf.pubPem, "agent-01", 825, 2);

  mbedtls_x509_crt ca, lf;
  mbedtls_x509_crt_init(&ca);
  mbedtls_x509_crt_init(&lf);
  parse(ca, caCert);
  parse(lf, leafCert);

  uint32_t flags = 0;
  int rc = mbedtls_x509_crt_verify(&lf, &ca, nullptr, nullptr, &flags,
                                   nullptr, nullptr);
  CHECK(rc == 0);
  CHECK(flags == 0);

  mbedtls_x509_crt_free(&lf);
  mbedtls_x509_crt_free(&ca);
}

TEST_CASE("CSR sign roundtrip verifies", "[ca]") {
  KeyPair caKey = generateRsaKey(2048);
  std::string caCert = makeSelfSignedCa(caKey.privPem, "PortalVNC CA", 3650);

  KeyPair sub = generateRsaKey(2048);
  std::string csr = makeCsr(sub.privPem, "client-bob");
  std::string leafCert = signCsr(caCert, caKey.privPem, csr, 825, 3);

  mbedtls_x509_crt ca, lf;
  mbedtls_x509_crt_init(&ca);
  mbedtls_x509_crt_init(&lf);
  parse(ca, caCert);
  parse(lf, leafCert);

  uint32_t flags = 0;
  int rc = mbedtls_x509_crt_verify(&lf, &ca, nullptr, nullptr, &flags,
                                   nullptr, nullptr);
  CHECK(rc == 0);
  CHECK(flags == 0);

  mbedtls_x509_crt_free(&lf);
  mbedtls_x509_crt_free(&ca);
}
