#include "keyvault.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/gcm.h>
#include <mbedtls/pkcs5.h>

#include <cstring>
#include <stdexcept>

namespace pvnc {

namespace {
constexpr char kMagic[8] = {'P', 'V', 'N', 'C', 'K', 'V', '0', '1'};
constexpr uint32_t kIters = 200000;
constexpr size_t kSalt = 16, kIv = 12, kTag = 16, kKey = 32;
constexpr size_t kHeader = sizeof(kMagic) + 4 + kSalt + kIv + kTag;  // tag precedes ciphertext

void zero(std::vector<uint8_t>& v) {
  if (!v.empty()) mbedtls_platform_zeroize(v.data(), v.size());
}

// PBKDF2-HMAC-SHA256 → 32-byte key.
bool derive(const std::string& password, const uint8_t* salt, uint32_t iters,
            uint8_t out[kKey]) {
  return mbedtls_pkcs5_pbkdf2_hmac_ext(
             MBEDTLS_MD_SHA256, (const unsigned char*)password.data(), password.size(),
             salt, kSalt, iters, kKey, out) == 0;
}
}  // namespace

std::vector<uint8_t> sealKey(const std::string& pem, const std::string& password) {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  uint8_t key[kKey] = {0};
  try {
    const char* pers = "pvnc-keyvault";
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char*)pers, std::strlen(pers)) != 0)
      throw std::runtime_error("ctr_drbg_seed");

    std::vector<uint8_t> blob(kHeader + pem.size());
    uint8_t* p = blob.data();
    std::memcpy(p, kMagic, sizeof(kMagic));
    uint8_t* iterField = p + sizeof(kMagic);
    iterField[0] = uint8_t((kIters >> 24) & 0xFF); iterField[1] = uint8_t((kIters >> 16) & 0xFF);
    iterField[2] = uint8_t((kIters >> 8) & 0xFF);   iterField[3] = uint8_t(kIters & 0xFF);
    uint8_t* salt = iterField + 4;
    uint8_t* iv = salt + kSalt;
    uint8_t* tag = iv + kIv;
    uint8_t* ct = tag + kTag;
    if (mbedtls_ctr_drbg_random(&drbg, salt, kSalt) != 0 ||
        mbedtls_ctr_drbg_random(&drbg, iv, kIv) != 0)
      throw std::runtime_error("drbg_random");

    if (!derive(password, salt, kIters, key)) throw std::runtime_error("pbkdf2");
    if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) != 0)
      throw std::runtime_error("gcm_setkey");
    // Authenticate the whole header (magic|iters|salt|iv) as AAD.
    const size_t aadLen = sizeof(kMagic) + 4 + kSalt + kIv;
    if (mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, pem.size(), iv, kIv,
                                  p, aadLen, (const unsigned char*)pem.data(), ct,
                                  kTag, tag) != 0)
      throw std::runtime_error("gcm_encrypt");

    mbedtls_platform_zeroize(key, sizeof(key));
    mbedtls_gcm_free(&gcm);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return blob;
  } catch (...) {
    mbedtls_platform_zeroize(key, sizeof(key));
    mbedtls_gcm_free(&gcm);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    throw;
  }
}

bool openKey(const std::vector<uint8_t>& blob, const std::string& password,
             std::string& pemOut) {
  pemOut.clear();
  if (blob.size() < kHeader) return false;
  const uint8_t* p = blob.data();
  if (std::memcmp(p, kMagic, sizeof(kMagic)) != 0) return false;
  const uint8_t* iterField = p + sizeof(kMagic);
  uint32_t iters = (uint32_t(iterField[0]) << 24) | (uint32_t(iterField[1]) << 16) |
                   (uint32_t(iterField[2]) << 8) | iterField[3];
  if (iters == 0) return false;
  const uint8_t* salt = iterField + 4;
  const uint8_t* iv = salt + kSalt;
  const uint8_t* tag = iv + kIv;
  const uint8_t* ct = tag + kTag;
  size_t ctLen = blob.size() - kHeader;

  uint8_t key[kKey] = {0};
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  std::vector<uint8_t> plain(ctLen);
  bool ok = false;
  if (derive(password, salt, iters, key) &&
      mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256) == 0) {
    const size_t aadLen = sizeof(kMagic) + 4 + kSalt + kIv;
    int rc = mbedtls_gcm_auth_decrypt(&gcm, ctLen, iv, kIv, p, aadLen, tag, kTag, ct,
                                      plain.data());
    if (rc == 0) {
      pemOut.assign((const char*)plain.data(), ctLen);
      ok = true;
    }
  }
  mbedtls_platform_zeroize(key, sizeof(key));
  zero(plain);
  mbedtls_gcm_free(&gcm);
  return ok;
}

}  // namespace pvnc
