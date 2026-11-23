/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).

Depends only on QtCore and OpenSSL: compiled into the client and the
focused unit test.
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>

namespace Tgeshka::Update {

// The manifest signature (SteepMans/api-gateway docs/update-v1.md):
// Ed25519 over the 32 raw bytes of the package sha256, made by the
// release CI (tools/tgeshka/sign_update.js) with TGESHKA_UPDATE_KEY.
[[nodiscard]] bool VerifyPackageDigest(
	const QByteArray &sha256,
	const QByteArray &signature);

// For the unit test: the same with any raw 32-byte public key.
[[nodiscard]] bool VerifyPackageDigestWithKey(
	const QByteArray &sha256,
	const QByteArray &signature,
	const QByteArray &publicKey);

} // namespace Tgeshka::Update
