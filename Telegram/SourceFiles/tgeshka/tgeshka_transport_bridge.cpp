/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_transport_bridge.h"

#include "tgeshka/tgeshka_gateway.h"
#include "core/application.h"
#include "core/core_gateway.h"
#include "mtproto/mtproto_gateway.h"
#include "window/window_controller.h"

namespace Tgeshka {
namespace {

constexpr auto kCloseRevoked = 4401;
constexpr auto kToastInterval = crl::time(60000);

[[nodiscard]] MTP::GatewaySettings ToTransport(const GatewayConfig &config) {
	return {
		.url = config.url,
		.token = config.token,
	};
}

[[nodiscard]] GatewayStatus StatusFromError(int code) {
	switch (code) {
	case MTP::kGatewayNoToken: return GatewayStatus::Unknown;
	case MTP::kGatewayRejected:
	case kCloseRevoked: return GatewayStatus::Rejected;
	}
	return GatewayStatus::Failed;
}

bool Started = false;

rpl::lifetime &Lifetime() {
	static auto result = rpl::lifetime();
	return result;
}

} // namespace

void StartTransport() {
	Started = true;
	const auto &config = CurrentGatewayConfig();
	const auto developerToken = !qEnvironmentVariable(
		"TGGW_TOKEN_FILE").isEmpty();
	if (config.needsToken() && developerToken) {
		// Keep TGGW_URL / TGGW_TOKEN_FILE from the environment.
		LOG(("TGeshka: no stored gateway key, using the environment."));
	} else {
		MTP::SetGateway(ToTransport(config));
	}

	Lifetime().destroy();
	MTP::GatewayOpened(
	) | rpl::on_next([] {
		SetGatewayStatus(GatewayStatus::Connected);
	}, Lifetime());
	MTP::GatewayErrors(
	) | rpl::on_next([](const MTP::GatewayError &error) {
		const auto status = StatusFromError(error.code);
		SetGatewayStatus(status);
		static auto lastToast = crl::time();
		const auto now = crl::now();
		if (status == GatewayStatus::Rejected
			&& (!lastToast || now - lastToast > kToastInterval)) {
			lastToast = now;
			if (const auto window = Core::App().activeWindow()) {
				window->showToast(u"Шлюз не принял ключ доступа. "
					"Обновите его: Настройки > Продвинутые > "
					"Подключение через шлюз."_q);
			}
		}
	}, Lifetime());
}

void ApplyToTransport(const GatewayConfig &config) {
	if (!Started) {
		return; // StartTransport() will pass the current settings.
	}
	LOG(("TGeshka: gateway settings changed, key: %1"
		).arg(Logs::b(config.hasToken())));
	Core::ApplyGateway(ToTransport(config));
}

} // namespace Tgeshka
