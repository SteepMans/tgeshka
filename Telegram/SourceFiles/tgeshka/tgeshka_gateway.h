/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#pragma once

namespace Tgeshka {

// Connection settings for the WebSocket gateway (docs/gateway.md).
// The gateway is always on in this build: no direct connections.
struct GatewayConfig {
	QString url;
	QString token;

	[[nodiscard]] bool hasToken() const {
		return !token.isEmpty();
	}
	[[nodiscard]] bool hasUrl() const {
		return !url.isEmpty();
	}
	// No key or no gateway address yet: nothing can connect.
	[[nodiscard]] bool needsToken() const {
		return !hasToken() || !hasUrl();
	}

	friend inline bool operator==(
		const GatewayConfig &,
		const GatewayConfig &) = default;
};

// Set at build time (TGESHKA_GATEWAY_URL), may be empty.
[[nodiscard]] QString DefaultGatewayUrl();

// Stored in tdata/gateway, encrypted the same way as the application
// settings file with proxy passwords (local salt + legacy local key).
[[nodiscard]] const GatewayConfig &CurrentGatewayConfig();
void SetGatewayConfig(GatewayConfig config);
[[nodiscard]] rpl::producer<GatewayConfig> GatewayConfigValue();

// What the transport reports about the live connection.
enum class GatewayStatus {
	Unknown,
	Connecting,
	Connected,
	Rejected, // 404 on upgrade or close 4401: bad or revoked key.
	Failed,
};
void SetGatewayStatus(GatewayStatus status);
[[nodiscard]] rpl::producer<GatewayStatus> GatewayStatusValue();

// The last connection error from the gateway, for the state screen.
struct GatewayLastError {
	int code = 0;
	QString reason;
	QDateTime when;

	explicit operator bool() const {
		return when.isValid();
	}
	friend inline bool operator==(
		const GatewayLastError &,
		const GatewayLastError &) = default;
};
void SetGatewayLastError(int code, const QString &reason);
[[nodiscard]] rpl::producer<GatewayLastError> GatewayLastErrorValue();
[[nodiscard]] QString GatewayErrorText(int code, const QString &reason);

[[nodiscard]] bool IsValidToken(const QString &token);
[[nodiscard]] bool IsValidGatewayUrl(const QString &url);

// Accepts a bare key "tc_..." or a link
// tgeshka://gateway?url=...&token=... and fills the result.
// Returns false if the text is neither.
[[nodiscard]] bool ParseAccessKey(const QString &text, GatewayConfig &result);
[[nodiscard]] bool IsImportLink(const QUrl &url);

// GET /v1/info with the key, without opening a WebSocket.
struct KeyCheckResult {
	enum class Type {
		Ok,
		Rejected,
		Unreachable,
	};
	Type type = Type::Unreachable;
	int activeConnections = 0;
	int maxConnections = 0;
	QString description; // Key name given by the gateway owner.
	QString status; // "active" or other key state from the gateway.
	crl::time latency = 0; // Request round trip, ms.
};
void CheckAccessKey(
	const QString &url,
	const QString &token,
	Fn<void(KeyCheckResult)> done);

} // namespace Tgeshka
