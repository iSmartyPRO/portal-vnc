#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pvnc {

// Encrypts a PEM private key under a password for at-rest storage. The blob is
//   magic(8) | iters(4, big-endian) | salt(16) | iv(12) | tag(16) | ciphertext
// with the header authenticated as AAD. Key = PBKDF2-HMAC-SHA256(password,salt).
// Cipher = AES-256-GCM. The plaintext working copy is zeroed before returning.
std::vector<uint8_t> sealKey(const std::string& pem, const std::string& password);

// Decrypts a blob produced by sealKey. Returns false (and leaves pemOut empty)
// on a wrong password, tampering, or a malformed/short blob; true on success.
bool openKey(const std::vector<uint8_t>& blob, const std::string& password,
             std::string& pemOut);

}  // namespace pvnc
