/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_settings.h"

#include "tgeshka/tgeshka_boxes.h"
#include "tgeshka/tgeshka_brand.h"
#include "tgeshka/tgeshka_gateway.h"
#include "tgeshka/tgeshka_lang.h"
#include "tgeshka/tgeshka_members.h"
#include "tgeshka/tgeshka_update_ui.h"
#include "base/timer.h"
#include "boxes/about_box.h"
#include "core/core_gateway.h"
#include "core/version.h"
#include "lang/lang_keys.h"
#include "mtproto/mtproto_gateway.h"
#include "settings/sections/settings_main.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "settings/settings_common_session.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QtGui/QDesktopServices>

namespace Tgeshka {
namespace {

using namespace Settings;
using namespace Settings::Builder;

constexpr auto kInfoRefreshInterval = crl::time(5000);
constexpr auto kTrafficRefreshInterval = crl::time(1000);

struct LiveState {
	rpl::variable<QString> latency;
	rpl::variable<QString> connections;
	rpl::variable<QString> key;
	rpl::variable<QString> traffic;
	base::Timer infoTimer;
	base::Timer trafficTimer;
	bool infoRequested = false;
};

[[nodiscard]] QString KeyStatusText(const KeyCheckResult &result) {
	switch (result.type) {
	case KeyCheckResult::Type::Rejected:
		return Tr(u"not accepted"_q, u"не принят"_q);
	case KeyCheckResult::Type::Unreachable:
		return Tr(u"unknown"_q, u"неизвестно"_q);
	case KeyCheckResult::Type::Ok:
		break;
	}
	const auto status = (result.status.isEmpty()
		|| result.status == u"active"_q)
		? Tr(u"active"_q, u"активен"_q)
		: result.status;
	return result.description.isEmpty()
		? status
		: (result.description + u", "_q + status);
}

[[nodiscard]] QString TrafficText() {
	const auto traffic = MTP::GatewayTrafficTotal();
	return Tr(u"received %1, sent %2"_q, u"принято %1, отправлено %2"_q)
		.arg(Ui::FormatSizeText(traffic.received))
		.arg(Ui::FormatSizeText(traffic.sent));
}

void AddStateRow(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title,
		rpl::producer<QString> value) {
	AddButtonWithLabel(
		container,
		std::move(title),
		std::move(value),
		st::settingsButtonNoIcon);
}

} // namespace

void FillGatewayState(not_null<Ui::VerticalLayout*> container) {
	const auto state = container->lifetime().make_state<LiveState>();
	const auto weak = QPointer<Ui::VerticalLayout>(container.get());
	const auto dash = QString::fromUtf8("\xe2\x80\x94");
	state->latency = dash;
	state->connections = dash;
	state->key = dash;

	const auto refreshInfo = [=] {
		const auto &config = CurrentGatewayConfig();
		if (config.needsToken() || state->infoRequested) {
			return;
		}
		state->infoRequested = true;
		CheckAccessKey(config.url, config.token, [=](KeyCheckResult r) {
			if (!weak) {
				return;
			}
			state->infoRequested = false;
			state->key = KeyStatusText(r);
			if (r.type == KeyCheckResult::Type::Unreachable) {
				state->latency = Tr(u"no answer"_q, u"нет ответа"_q);
				state->connections = dash;
				return;
			}
			state->latency = Tr(u"%1 ms"_q, u"%1 мс"_q).arg(r.latency);
			state->connections = (r.type == KeyCheckResult::Type::Ok)
				? (r.maxConnections
					? Tr(u"%1 of %2"_q, u"%1 из %2"_q)
						.arg(r.activeConnections)
						.arg(r.maxConnections)
					: QString::number(r.activeConnections))
				: dash;
		});
	};
	const auto refreshTraffic = [=] {
		state->traffic = TrafficText();
	};
	state->infoTimer.setCallback(refreshInfo);
	state->trafficTimer.setCallback(refreshTraffic);
	state->infoTimer.callEach(kInfoRefreshInterval);
	state->trafficTimer.callEach(kTrafficRefreshInterval);
	refreshInfo();
	refreshTraffic();

	GatewayConfigValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		refreshInfo();
	}, container->lifetime());

	AddStateRow(
		container,
		TrValue(u"Status"_q, u"Состояние"_q),
		GatewayStatusText());
	AddStateRow(
		container,
		TrValue(u"Gateway response time"_q, u"Время ответа шлюза"_q),
		state->latency.value());
	AddStateRow(
		container,
		TrValue(u"Key connections"_q, u"Соединения ключа"_q),
		state->connections.value());
	AddStateRow(
		container,
		TrValue(u"Access key"_q, u"Ключ доступа"_q),
		state->key.value());
	AddStateRow(
		container,
		TrValue(u"Traffic this session"_q, u"Трафик за сессию"_q),
		state->traffic.value());
	AddStateRow(
		container,
		TrValue(u"Last error"_q, u"Последняя ошибка"_q),
		GatewayLastErrorValue(
		) | rpl::map([](const GatewayLastError &error) {
			return error
				? (QLocale().toString(error.when.time(), QLocale::ShortFormat)
					+ u", "_q
					+ GatewayErrorText(error.code, error.reason))
				: Tr(u"none"_q, u"нет"_q);
		}));
	AddButtonWithIcon(
		container,
		TrValue(u"Reconnect"_q, u"Переподключить"_q),
		st::settingsButton,
		{ &st::menuIconRestore }
	)->addClickHandler([] {
		Core::RestartGatewayConnections();
	});
}

namespace {

void BuildGatewaySection(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(TrValue(u"Gateway"_q, u"Шлюз"_q));
	builder.addButton({
		.id = u"tgeshka/gateway"_q,
		.title = TrValue(u"Gateway connection"_q, u"Подключение через шлюз"_q),
		.icon = { &st::menuIconLock },
		.label = GatewayStatusText(),
		.onClick = [=] {
			if (controller) {
				controller->window().show(Box(GatewaySettingsBox));
			}
		},
		.keywords = { u"gateway"_q, u"шлюз"_q, u"ключ"_q, u"token"_q },
	});
	builder.add([](const WidgetContext &ctx) {
		auto result = object_ptr<Ui::VerticalLayout>(ctx.container);
		FillGatewayState(result.data());
		return SectionBuilder::WidgetToAdd{ .widget = std::move(result) };
	});
	builder.addSkip();
}

void BuildAboutSection(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(TrValue(u"Application"_q, u"Приложение"_q));
	builder.addButton({
		.id = u"tgeshka/version"_q,
		.title = TrValue(u"Version"_q, u"Версия"_q),
		.icon = { &st::menuIconInfo },
		.label = rpl::single(QString::fromLatin1(TGESHKA_VERSION)
			+ u" (Telegram Desktop "_q
			+ QString::fromLatin1(AppVersionStr)
			+ ')'),
	});
	builder.addButton({
		.id = u"tgeshka/about"_q,
		.title = TrValue(u"About TGeshka"_q, u"О TGeshka"_q),
		.icon = { &st::menuIconTgeshka },
		.onClick = [=] {
			if (controller) {
				controller->window().show(Box(AboutBox));
			}
		},
		.keywords = { u"about"_q, u"о программе"_q },
	});
	builder.addButton({
		.id = u"tgeshka/sources"_q,
		.title = TrValue(u"Source code"_q, u"Исходный код"_q),
		.icon = { &st::menuIconSettings },
		.onClick = [] {
			QDesktopServices::openUrl(
				QUrl(QString::fromLatin1(TGESHKA_SOURCES_URL)));
		},
		.keywords = { u"github"_q, u"source"_q, u"gpl"_q },
	});
	builder.addSkip();
}

class TgeshkaSection : public Section<TgeshkaSection> {
public:
	TgeshkaSection(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

const auto kMeta = BuildHelper({
	.id = TgeshkaSection::Id(),
	.parentId = MainId(),
	.title = &tr::lng_tgeshka_settings,
	.icon = &st::menuIconTgeshka,
}, [](SectionBuilder &builder) {
	BuildGatewaySection(builder);
	Members::AddSettings(builder);
	Update::AddSettings(builder);
	BuildAboutSection(builder);
});

const SectionBuildMethod kTgeshkaSection = kMeta.build;

TgeshkaSection::TgeshkaSection(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> TgeshkaSection::title() {
	return tr::lng_tgeshka_settings();
}

void TgeshkaSection::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	build(content, kTgeshkaSection);

	Ui::ResizeFitChild(this, content);
}

} // namespace

Settings::Type SettingsSectionId() {
	return TgeshkaSection::Id();
}

void AddSettingsSectionButton(SectionBuilder &builder) {
	builder.addSectionButton({
		.title = tr::lng_tgeshka_settings(),
		.targetSection = SettingsSectionId(),
		.icon = { &st::menuIconTgeshka },
		.keywords = { u"tgeshka"_q, u"gateway"_q, u"шлюз"_q },
	});
}

} // namespace Tgeshka
