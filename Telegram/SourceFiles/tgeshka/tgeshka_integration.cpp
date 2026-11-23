/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_integration.h"

#include "tgeshka/tgeshka_boxes.h"
#include "tgeshka/tgeshka_brand.h"
#include "tgeshka/tgeshka_gateway.h"
#include "tgeshka/tgeshka_transport_bridge.h"
#include "tgeshka/tgeshka_update_ui.h"
#include "base/platform/base_platform_url_scheme.h"
#include "core/application.h"
#include "core/version.h"
#include "lang/lang_text_entity.h"
#include "platform/platform_specific.h"
#include "settings.h"
#include "settings/settings_builder.h"
#include "ui/layers/generic_box.h"
#include "ui/text/text_utilities.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QCoreApplication>

namespace Tgeshka {
namespace {

void ShowToast(const QString &text) {
	if (const auto window = Core::App().activeWindow()) {
		window->showToast(text);
	}
}

} // namespace

void RegisterUrlScheme(const QString &arguments) {
	const auto scheme = QString::fromLatin1(TGESHKA_URL_SCHEME);
	base::Platform::RegisterUrlScheme(base::Platform::UrlSchemeDescriptor{
		.executable = Platform::ExecutablePathForShortcuts(),
		.arguments = arguments,
		.protocol = scheme,
		.protocolName = AppName.utf16() + u" Link"_q,
		.shortAppName = scheme,
		.longAppName = QCoreApplication::applicationName(),
		.displayAppName = AppName.utf16(),
		.displayAppDescription = AppName.utf16(),
	});
}

void OnLocalStorageReady() {
	// A tgeshka:// link that launched the app goes in before connecting.
	ConsumeStartUrls(cRefStartUrls());
	StartTransport();

	// After Application::run(): windows exist for the update screens.
	crl::on_main([] {
		Update::StartUi();
	});
}

rpl::producer<bool> ProxySettingsShown() {
	// Every connection goes through the gateway, user proxies are unused.
	return rpl::single(false);
}

void ConsumeStartUrls(QList<QUrl> &urls) {
	for (auto i = urls.begin(); i != urls.end();) {
		if (!IsImportLink(*i)) {
			++i;
			continue;
		}
		auto config = CurrentGatewayConfig();
		if (ParseAccessKey(i->toString(QUrl::FullyEncoded), config)) {
			SetGatewayConfig(config);
			LOG(("TGeshka: gateway key imported from a link."));
			ShowToast(u"Ключ доступа к шлюзу сохранён"_q);
		} else {
			LOG(("TGeshka: bad gateway link."));
			ShowToast(u"Ссылка с ключом доступа не распознана"_q);
		}
		i = urls.erase(i);
	}
}

void OnIntroShown(not_null<Window::Controller*> window) {
	if (!CurrentGatewayConfig().needsToken()) {
		return;
	}
	const auto raw = window.get();
	crl::on_main(raw, [=] {
		if (CurrentGatewayConfig().needsToken()) {
			raw->show(Box(AccessKeyBox));
		}
	});
}

void AddGatewaySettingsButton(Settings::Builder::SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"advanced/tgeshka_gateway"_q,
		.title = rpl::single(u"Подключение через шлюз"_q),
		.icon = { &st::menuIconLock },
		.label = GatewayStatusText(),
		.onClick = [=] {
			if (controller) {
				controller->window().show(Box(GatewaySettingsBox));
			}
		},
		.keywords = {
			u"gateway"_q,
			u"шлюз"_q,
			u"ключ"_q,
			u"token"_q,
		},
	});
}

rpl::producer<TextWithEntities> AboutText() {
	auto result = TextWithEntities{
		AppName.utf16()
			+ u" - неофициальный клиент на базе Telegram Desktop (GPLv3), "
			"не связан с Telegram FZ-LLC. Исходный код: "_q,
	};
	result.append(tr::link(
		QString::fromLatin1(TGESHKA_SOURCES_URL).mid(8),
		QString::fromLatin1(TGESHKA_SOURCES_URL)));
	return rpl::single(std::move(result));
}

} // namespace Tgeshka
