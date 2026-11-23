/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "core/core_gateway.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "mtproto/mtp_instance.h"

namespace Core {

void ApplyGateway(const MTP::GatewaySettings &settings) {
	if (MTP::SetGateway(settings)) {
		ReconnectGateway();
	}
}

void ReconnectGateway() {
	if (!IsAppLaunched()) {
		return;
	}
	// Re-applying the current proxy settings restarts the sessions of
	// all accounts, MTP::Session::refreshOptions() reads the gateway.
	const auto &proxy = App().settings().proxy();
	App().setCurrentProxy(proxy.selected(), proxy.settings());
}

void RestartGatewayConnections() {
	if (!IsAppLaunched()) {
		return;
	}
	for (const auto &[index, account] : App().domain().accounts()) {
		account->mtpValue() | rpl::take(1) | rpl::on_next([](
				not_null<MTP::Instance*> instance) {
			instance->restart();
		}, account->lifetime());
	}
}

} // namespace Core
