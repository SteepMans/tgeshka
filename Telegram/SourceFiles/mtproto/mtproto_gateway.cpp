/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/mtproto_gateway.h"

#include <QtCore/QFile>
#include <QtCore/QMutex>
#include <QtCore/QUrl>

#include <atomic>

namespace MTP {
namespace {

struct State {
	QMutex mutex;
	GatewaySettings settings;
	QString clientId;
	bool loaded = false;
};

std::atomic<int64> TrafficSent = 0;
std::atomic<int64> TrafficReceived = 0;

[[nodiscard]] State &GlobalState() {
	static auto result = State();
	return result;
}

[[nodiscard]] rpl::event_stream<GatewayError> &ErrorsStream() {
	static auto result = rpl::event_stream<GatewayError>();
	return result;
}

[[nodiscard]] GatewaySettings FromEnvironment() {
	auto result = GatewaySettings();
	result.url = qEnvironmentVariable("TGGW_URL").trimmed();
	const auto tokenFile = qEnvironmentVariable("TGGW_TOKEN_FILE");
	if (!tokenFile.isEmpty()) {
		auto file = QFile(tokenFile);
		if (file.open(QIODevice::ReadOnly)) {
			result.token = QString::fromUtf8(file.read(4096)).trimmed();
		} else {
			LOG(("Gateway Error: could not read TGGW_TOKEN_FILE."));
		}
	}
	return result;
}

[[nodiscard]] rpl::event_stream<> &OpenedStream() {
	static auto result = rpl::event_stream<>();
	return result;
}

} // namespace

bool GatewaySettings::valid() const {
	const auto parsed = QUrl(url);
	return parsed.isValid()
		&& (parsed.scheme() == u"wss"_q)
		&& !parsed.host().isEmpty()
		&& !token.isEmpty();
}

bool GatewayEnabled() {
	return true;
}

GatewaySettings CurrentGateway() {
	auto &state = GlobalState();
	QMutexLocker lock(&state.mutex);
	if (!state.loaded) {
		state.loaded = true;
		state.settings = FromEnvironment();
		if (!state.settings.url.isEmpty()) {
			LOG(("Gateway Info: using TGGW_URL from environment."));
		}
	}
	return state.settings;
}

ProxyData GatewayProxyData() {
	const auto settings = CurrentGateway();
	const auto url = QUrl(settings.url);
	auto result = ProxyData();
	result.type = ProxyData::Type::Gateway;
	if (url.isValid() && url.scheme() == u"wss"_q) {
		result.host = url.host();
		result.port = url.port(443);
		result.user = url.path().isEmpty() ? u"/"_q : url.path();
	}
	result.password = settings.token;
	return result;
}

bool SetGateway(const GatewaySettings &settings) {
	auto &state = GlobalState();
	QMutexLocker lock(&state.mutex);
	const auto changed = !state.loaded || (state.settings != settings);
	state.loaded = true;
	state.settings = settings;
	return changed;
}

rpl::producer<GatewayError> GatewayErrors() {
	return ErrorsStream().events();
}

void NotifyGatewayError(int code, const QString &reason) {
	crl::on_main([=] {
		ErrorsStream().fire({ .code = code, .reason = reason });
	});
}

GatewayTraffic GatewayTrafficTotal() {
	return {
		.sent = TrafficSent.load(),
		.received = TrafficReceived.load(),
	};
}

void CountGatewayTraffic(int64 sent, int64 received) {
	if (sent) {
		TrafficSent += sent;
	}
	if (received) {
		TrafficReceived += received;
	}
}

void SetGatewayClientId(const QString &id) {
	auto &state = GlobalState();
	QMutexLocker lock(&state.mutex);
	state.clientId = id;
}

QString GatewayClientId() {
	auto &state = GlobalState();
	QMutexLocker lock(&state.mutex);
	return state.clientId;
}

rpl::producer<> GatewayOpened() {
	return OpenedStream().events();
}

void NotifyGatewayOpened() {
	crl::on_main([] {
		OpenedStream().fire({});
	});
}

} // namespace MTP
