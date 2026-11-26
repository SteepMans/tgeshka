/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_integration.h"

#include "tgeshka/tgeshka_boxes.h"
#include "tgeshka/tgeshka_brand.h"
#include "tgeshka/tgeshka_easter_egg.h"
#include "tgeshka/tgeshka_gateway.h"
#include "tgeshka/tgeshka_lang.h"
#include "tgeshka/tgeshka_members.h"
#include "tgeshka/tgeshka_transport_bridge.h"
#include "tgeshka/tgeshka_update_ui.h"
#include "tgeshka/tgeshka_whats_new.h"
#include "base/platform/base_platform_url_scheme.h"
#include "base/timer.h"
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
#include <QtCore/QSettings>

namespace Tgeshka {
namespace {

constexpr auto kTgLinksOffFile = "tgeshka_tg_links_off";
constexpr auto kTgLinksCheckInterval = 10 * 60 * crl::time(1000);

[[nodiscard]] QString TgLinksOffPath() {
	return cWorkingDir() + u"tdata/"_q + QString::fromLatin1(kTgLinksOffFile);
}

[[nodiscard]] base::Platform::UrlSchemeDescriptor SchemeDescriptor(
		const QString &protocol,
		const QString &protocolName,
		const QString &arguments) {
	return {
		.executable = Platform::ExecutablePathForShortcuts(),
		.arguments = arguments,
		.protocol = protocol,
		.protocolName = protocolName,
		// Software\Classes\tgeshka.<protocol>, next to tdesktop.tg
		// of the official client, and our own Capabilities key.
		.shortAppName = QString::fromLatin1(TGESHKA_URL_SCHEME),
		.longAppName = QCoreApplication::applicationName(),
		.displayAppName = AppName.utf16(),
		.displayAppDescription = AppName.utf16(),
	};
}

#ifdef Q_OS_WIN
// Hands tg:// back to the official client: its tdesktop.tg command if it
// was ever registered, otherwise our tg:// key is removed. Only when tg://
// still points to this executable, someone else's choice stays.
void ReleaseTgScheme() {
	auto classes = QSettings(
		u"HKEY_CURRENT_USER\\Software\\Classes"_q,
		QSettings::NativeFormat);
	const auto exe = QDir::toNativeSeparators(
		Platform::ExecutablePathForShortcuts());
	const auto current = classes.value(
		u"tg/shell/open/command/Default"_q).toString();
	if (current.contains(exe, Qt::CaseInsensitive)) {
		const auto official = classes.value(
			u"tdesktop.tg/shell/open/command/Default"_q).toString();
		if (!official.isEmpty()) {
			classes.setValue(u"tg/shell/open/command/Default"_q, official);
			const auto icon = classes.value(
				u"tdesktop.tg/DefaultIcon/Default"_q).toString();
			if (!icon.isEmpty()) {
				classes.setValue(u"tg/DefaultIcon/Default"_q, icon);
			}
			classes.setValue(u"tg/Default"_q, u"URL:Telegram Link"_q);
		} else {
			classes.remove(u"tg"_q);
		}
	}
	classes.remove(QString::fromLatin1(TGESHKA_URL_SCHEME) + u".tg"_q);
	auto associations = QSettings(
		u"HKEY_CURRENT_USER\\Software\\"_q
			+ QCoreApplication::applicationName()
			+ u"\\Capabilities\\UrlAssociations"_q,
		QSettings::NativeFormat);
	associations.remove(u"tg"_q);
	LOG(("TGeshka: tg:// links handed back."));
}
#endif // Q_OS_WIN

// The official client registers tg:// again after each of its updates.
void StartTgLinksCheck() {
	if (!TgLinksSupported()) {
		return;
	}
	// Lives until the process exits, never destroyed after the app.
	static const auto timer = new base::Timer([] {
		if (OpensTgLinks()) {
			Core::Application::RegisterUrlScheme();
		}
	});
	if (!timer->isActive()) {
		timer->callEach(kTgLinksCheckInterval);
	}
}

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
	base::Platform::RegisterUrlScheme(SchemeDescriptor(
		QString::fromLatin1(TGESHKA_URL_SCHEME),
		AppName.utf16() + u" Link"_q,
		arguments));
	if (OpensTgLinks()) {
		base::Platform::RegisterUrlScheme(SchemeDescriptor(
			u"tg"_q,
			u"Telegram Link"_q,
			arguments));
	}
}

TextWithEntities ApplyEasterEgg(TextWithEntities text) {
	if (!text.entities.isEmpty() || text.text.isEmpty()) {
		return text;
	}
	static const auto rules = [] {
		auto file = QFile(u":/tgeshka/easter-egg.txt"_q);
		return file.open(QIODevice::ReadOnly)
			? EasterEgg::Parse(file.readAll().toStdString())
			: EasterEgg::Rules();
	}();
	const auto original = text.text.toStdU16String();
	const auto result = EasterEgg::Apply(rules, original);
	if (result != original) {
		text.text = QString::fromStdU16String(result);
	}
	return text;
}

bool TgLinksSupported() {
#ifdef Q_OS_WIN
	return true;
#else // Q_OS_WIN
	return false;
#endif // Q_OS_WIN
}

bool OpensTgLinks() {
	return TgLinksSupported() && !QFile::exists(TgLinksOffPath());
}

void SetOpensTgLinks(bool enabled) {
	if (!TgLinksSupported() || OpensTgLinks() == enabled) {
		return;
	}
	const auto path = TgLinksOffPath();
	if (enabled) {
		QFile::remove(path);
		Core::Application::RegisterUrlScheme();
		LOG(("TGeshka: tg:// links taken."));
	} else {
		QDir().mkpath(QFileInfo(path).absolutePath());
		if (auto file = QFile(path); file.open(QIODevice::WriteOnly)) {
			file.write("1");
		}
#ifdef Q_OS_WIN
		ReleaseTgScheme();
#endif // Q_OS_WIN
	}
}

void OnLocalStorageReady() {
	WhatsNew::RememberDataFolder();
	ApplyBuiltInLanguage();
	ApplyDefaultAccent();
	MTP::SetGatewayClientId(u"desktop/"_q
		+ QString::fromLatin1(TGESHKA_VERSION));

	// A tgeshka:// link that launched the app goes in before connecting.
	ConsumeStartUrls(cRefStartUrls());
	StartTransport();

	// After Application::run(): windows exist for the update screens.
	crl::on_main([] {
		if (OpensTgLinks()) {
			Core::Application::RegisterUrlScheme();
		}
		StartTgLinksCheck();
		Members::Start();
		Update::StartUi();
		WhatsNew::ShowAfterUpdate();
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
