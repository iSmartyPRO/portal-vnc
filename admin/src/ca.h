#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace pvnc {

// Thrown by any CA operation that fails (bad PEM, keygen error, write error).
struct CaError : std::runtime_error {
  explicit CaError(const std::string& what) : std::runtime_error(what) {}
};

// An RSA key pair, both halves in PEM.
struct KeyPair {
  std::string privPem;  // PKCS#8/PKCS#1 private key PEM
  std::string pubPem;   // SubjectPublicKeyInfo PEM
};

// Generates a fresh RSA key pair.
KeyPair generateRsaKey(int bits = 2048);

// Builds a self-signed CA certificate (BasicConstraints CA:TRUE, keyCertSign +
// cRLSign) valid for `days`, signed by `caKeyPem`.
std::string makeSelfSignedCa(const std::string& caKeyPem, const std::string& cn, int days);

// Builds a PKCS#10 CSR for `cn` signed by `keyPem`.
std::string makeCsr(const std::string& keyPem, const std::string& cn);

// Issues a leaf certificate signed by the CA. `subjectKeyOrCsrPem` may be either
// a public key PEM (the subject's public key) or a CSR PEM; the public key and,
// for a CSR, the subject CN are taken from it (the `cn` argument overrides when
// non-empty). Leaf has CA:FALSE, digitalSignature+keyEncipherment and EKU
// serverAuth+clientAuth. `serial` must be unique per CA.
std::string issueCert(const std::string& caCertPem, const std::string& caKeyPem,
                      const std::string& subjectKeyOrCsrPem, const std::string& cn,
                      int days, uint64_t serial);

// Convenience: parse `csrPem`, verify its self-signature, and issue a leaf for it.
std::string signCsr(const std::string& caCertPem, const std::string& caKeyPem,
                    const std::string& csrPem, int days, uint64_t serial);

}  // namespace pvnc
