/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_proxy_data.h"

namespace MTP {

// Gateway transport: every MTProto connection to a Telegram DC goes as
// a separate WebSocket (wss://) to our gateway, which forwards the very
// same obfuscated2 byte stream to the DC. See docs/gateway.md.
//
// In this build the gateway mode is always on: there are no direct
// connections to Telegram DCs. Without an address or a token the
// connections fail and GatewayErrors() reports kGatewayNoToken.
//
// TODO(gateway v2): voice and video calls (tgcalls) still connect to
// Telegram reflectors directly, they are not routed through the gateway.

// Pseudo close codes reported in GatewayErrors() besides the WebSocket
// close codes from the protocol (1000, 1001, 4401, 4429, ...).
inline constexpr auto kGatewayNoToken = -1;
inline constexpr auto kGatewayRejected = -2; // HTTP answer instead of 101.
inline constexpr auto kGatewayTlsError = -3;

struct GatewaySettings {
	QString url; // wss://host[:port]/path, no default in the transport.
	QString token;

	[[nodiscard]] bool valid() const;
	friend inline bool operator==(
		const GatewaySettings &,
		const GatewaySettings &) = default;
};

struct GatewayError {
	int code = 0;
	QString reason;
};

// Thread-safe, may be called from MTP threads.
[[nodiscard]] bool GatewayEnabled();
[[nodiscard]] GatewaySettings CurrentGateway();
[[nodiscard]] ProxyData GatewayProxyData();

// Replaces the settings (the TGGW_URL / TGGW_TOKEN_FILE environment
// values are used only until the first call). Returns true if changed.
// Running sessions keep the old values until restarted, so from the UI
// use Core::ApplyGateway() which also restarts them.
bool SetGateway(const GatewaySettings &settings);

// Main thread. Fired when the gateway closes a connection with a code
// other than a normal 1000 / 1001, or refuses to open it.
[[nodiscard]] rpl::producer<GatewayError> GatewayErrors();

// Main thread. Fired when a WebSocket to the gateway is open.
[[nodiscard]] rpl::producer<> GatewayOpened();

// Bytes through all gateway sockets since the start, any thread.
struct GatewayTraffic {
	int64 sent = 0;
	int64 received = 0;
};
[[nodiscard]] GatewayTraffic GatewayTrafficTotal();
void CountGatewayTraffic(int64 sent, int64 received);

// Sent as X-TGeshka-Client: <platform>/<version> in the upgrade request.
void SetGatewayClientId(const QString &id);
[[nodiscard]] QString GatewayClientId();

// Any thread.
void NotifyGatewayError(int code, const QString &reason);
void NotifyGatewayOpened();

} // namespace MTP
