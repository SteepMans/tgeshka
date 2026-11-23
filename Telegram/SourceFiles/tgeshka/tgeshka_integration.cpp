/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_integration.h"

#include "tgeshka/tgeshka_boxes.h"
#include "tgeshka/tgeshka_brand.h"
#include "tgeshka/tgeshka_gateway.h"
#include "tgeshka/tgeshka_lang.h"
#include "tgeshka/tgeshka_transport_bridge.h"
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

#include "core/core_settings.h"
#include "mtproto/mtproto_gateway.h"
#include "window/themes/window_themes_embedded.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>

namespace Tgeshka {
namespace {

void ShowToast(const QString &text) {
	if (const auto window = Core::App().activeWindow()) {
		window->showToast(text);
	}
}

// Lavender accent of the logo for the built-in day and night themes and
// the colored tray icon, once per data folder: later user choices stay.
void ApplyDefaultAccent() {
	const auto marker = cWorkingDir() + u"tdata/tgeshka_theme"_q;
	if (QFile::exists(marker)) {
		return;
	}
	QDir().mkpath(QFileInfo(marker).absolutePath());
	if (auto file = QFile(marker); file.open(QIODevice::WriteOnly)) {
		file.write("1");
	}
	using Type = Window::Theme::EmbeddedType;
	auto &settings = Core::App().settings();
	auto &colors = settings.themesAccentColors();
	const auto set = [&](Type type, QColor color) {
		if (!colors.get(type)) {
			colors.set(type, color);
		}
	};
	set(Type::Default, QColor(0x84, 0x70, 0xde));
	set(Type::DayBlue, QColor(0x84, 0x70, 0xde));
	set(Type::Night, QColor(0x9a, 0x86, 0xe8));
	set(Type::NightGreen, QColor(0x9a, 0x86, 0xe8));
	// The colored lavender logo in the tray, not the white silhouette.
	settings.setTrayIconMonochrome(false);
	Core::App().saveSettingsDelayed();
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
	ApplyBuiltInLanguage();
	ApplyDefaultAccent();
	MTP::SetGatewayClientId(u"desktop/"_q
		+ QString::fromLatin1(TGESHKA_VERSION));

	// A tgeshka:// link that launched the app goes in before connecting.
	ConsumeStartUrls(cRefStartUrls());
	StartTransport();
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
			ShowToast(Tr(u"The gateway access key is saved"_q, u"Ключ доступа к шлюзу сохранён"_q));
		} else {
			LOG(("TGeshka: bad gateway link."));
			ShowToast(Tr(u"The access key link is not recognized"_q, u"Ссылка с ключом доступа не распознана"_q));
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

rpl::producer<TextWithEntities> AboutText() {
	auto result = TextWithEntities{
		AppName.utf16() + Tr(
			u" is an unofficial client based on Telegram Desktop (GPLv3), "
			"not affiliated with Telegram FZ-LLC. Source code: "_q,
			u" - неофициальный клиент на базе Telegram Desktop (GPLv3), "
			"не связан с Telegram FZ-LLC. Исходный код: "_q),
	};
	result.append(tr::link(
		QString::fromLatin1(TGESHKA_SOURCES_URL).mid(8),
		QString::fromLatin1(TGESHKA_SOURCES_URL)));
	return rpl::single(std::move(result));
}

} // namespace Tgeshka
