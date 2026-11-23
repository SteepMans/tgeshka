/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_update_verify.h"

#include <openssl/evp.h>

#include <memory>

namespace Tgeshka::Update {
namespace {

// Release signing key, the private half is the TGESHKA_UPDATE_KEY
// secret of the repository (docs/updates.md).
constexpr auto kPublicKeyBase64 = "yVJ7S3+RnSixfcNDEWllLh5LYIDBmBE6NAf1kmJsk5Y=";
constexpr auto kDigestSize = 32;
constexpr auto kSignatureSize = 64;
constexpr auto kPublicKeySize = 32;

} // namespace

bool VerifyPackageDigestWithKey(
		const QByteArray &sha256,
		const QByteArray &signature,
		const QByteArray &publicKey) {
	if (sha256.size() != kDigestSize
		|| signature.size() != kSignatureSize
		|| publicKey.size() != kPublicKeySize) {
		return false;
	}
	const auto key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>(
		EVP_PKEY_new_raw_public_key(
			EVP_PKEY_ED25519,
			nullptr,
			reinterpret_cast<const unsigned char*>(publicKey.constData()),
			publicKey.size()),
		&EVP_PKEY_free);
	if (!key) {
		return false;
	}
	const auto context = std::unique_ptr<
		EVP_MD_CTX,
		decltype(&EVP_MD_CTX_free)>(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
	if (!context
		|| EVP_DigestVerifyInit(
			context.get(),
			nullptr,
			nullptr,
			nullptr,
			key.get()) != 1) {
		return false;
	}
	return EVP_DigestVerify(
		context.get(),
		reinterpret_cast<const unsigned char*>(signature.constData()),
		signature.size(),
		reinterpret_cast<const unsigned char*>(sha256.constData()),
		sha256.size()) == 1;
}

bool VerifyPackageDigest(
		const QByteArray &sha256,
		const QByteArray &signature) {
	static const auto key = QByteArray::fromBase64(kPublicKeyBase64);
	return VerifyPackageDigestWithKey(sha256, signature, key);
}

} // namespace Tgeshka::Update
