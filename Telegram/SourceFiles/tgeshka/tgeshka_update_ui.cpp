/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_update_ui.h"

#include "tgeshka/tgeshka_update.h"
#include "core/application.h"
#include "lang/lang_instance.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/toast/toast.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Tgeshka::Update {
namespace {

using namespace Settings;
using namespace Settings::Builder;

struct UiState {
	bool started = false;
	QPointer<Ui::GenericBox> requiredBox;
	rpl::lifetime lifetime;
};

// The strings are not in the Telegram language packs.
[[nodiscard]] bool IsRussian() {
	const auto &instance = Lang::GetInstance();
	return instance.id().startsWith(u"ru"_q, Qt::CaseInsensitive)
		|| instance.baseId().startsWith(u"ru"_q, Qt::CaseInsensitive);
}

[[nodiscard]] QString Tr(const QString &en, const QString &ru) {
	return IsRussian() ? ru : en;
}

[[nodiscard]] rpl::producer<QString> TrValue(QString en, QString ru) {
	return rpl::single(rpl::empty) | rpl::then(
		Lang::GetInstance().updated()
	) | rpl::map([=] {
		return Tr(en, ru);
	});
}

[[nodiscard]] UiState &Data() {
	static auto result = UiState();
	return result;
}

[[nodiscard]] QString StatusText(const Status &status) {
	switch (status.state) {
	case State::Checking:
		return Tr(u"Checking..."_q, u"Проверка..."_q);
	case State::Latest:
		return Tr(u"Latest version"_q, u"Последняя версия"_q);
	case State::Downloading:
		return (status.total > 0)
			? Tr(u"Downloading %1: %2"_q, u"Загрузка %1: %2"_q)
				.arg(status.version)
				.arg(Ui::FormatProgressText(status.received, status.total))
			: Tr(u"Downloading %1"_q, u"Загрузка %1"_q)
				.arg(status.version);
	case State::Ready:
		return Tr(u"%1 is ready"_q, u"%1 готова"_q).arg(status.version);
	case State::Failed:
		return Tr(u"Check failed"_q, u"Проверка не удалась"_q);
	case State::Idle:
		break;
	}
	return QString();
}

[[nodiscard]] rpl::producer<QString> StatusTextValue() {
	return StatusValue() | rpl::map(StatusText);
}

// min_version above this build: no way to keep using it, only update.
void RequiredBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(TrValue(u"Update required"_q, u"Нужно обновиться"_q));
	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		TrValue(
			u"This version of TGeshka is no longer supported by the "
			u"gateway. The new version is downloaded automatically, "
			u"restart to install it."_q,
			u"Эта версия TGeshka больше не поддерживается шлюзом. Новая "
			u"версия скачивается автоматически, перезапустите приложение, "
			u"чтобы установить её."_q),
		st::boxLabel));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		StatusTextValue(),
		st::boxDividerLabel));
	const auto button = box->addButton(
		TrValue(u"Restart"_q, u"Перезапустить"_q),
		[] {
			if (CurrentStatus().state == State::Ready) {
				InstallAndRestart();
			} else {
				CheckNow();
			}
		});
	StatusValue() | rpl::on_next([=](const Status &status) {
		button->setDisabled(status.state != State::Ready);
	}, button->lifetime());
}

void ShowRequiredBox() {
	auto &data = Data();
	if (data.requiredBox) {
		return;
	}
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return;
	}
	auto box = Box(RequiredBox);
	data.requiredBox = box.data();
	window->show(std::move(box));
}

void ShowInstalledToast() {
	const auto window = Core::App().activePrimaryWindow();
	if (!window) {
		return;
	}
	window->showToast(Tr(
		u"TGeshka is updated to %1"_q,
		u"TGeshka обновлена до %1"_q).arg(CurrentVersion()));
}

} // namespace

void StartUi() {
	auto &data = Data();
	if (data.started) {
		return;
	}
	data.started = true;
	Start();
	const auto installed = JustInstalled();
	StatusValue(
	) | rpl::filter([](const Status &status) {
		return status.required;
	}) | rpl::on_next([] {
		ShowRequiredBox();
	}, data.lifetime);
	if (installed) {
		crl::on_main([] { ShowInstalledToast(); });
	}
}

std::optional<QString> ReadyButtonText() {
	const auto status = CurrentStatus();
	if (status.state != State::Ready) {
		return std::nullopt;
	}
	return Tr(
		u"Update %1 is available: restart"_q,
		u"Доступно обновление %1 — Перезапустить"_q).arg(status.version);
}

rpl::producer<> ReadyButtonChanges() {
	return StatusValue(
	) | rpl::map([](const Status &status) {
		return (status.state == State::Ready) ? status.version : QString();
	}) | rpl::distinct_until_changed() | rpl::to_empty;
}

void ReadyButtonClicked() {
	InstallAndRestart();
}

void AddSettings(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addSkip();
	builder.addSubsectionTitle(TrValue(u"Updates"_q, u"Обновления"_q));
	builder.addButton({
		.id = u"tgeshka/version"_q,
		.title = TrValue(u"Version"_q, u"Версия"_q),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(CurrentVersion()),
		.keywords = { u"version"_q, u"версия"_q },
	});
	const auto beta = builder.addButton({
		.id = u"tgeshka/beta"_q,
		.title = TrValue(u"Install beta versions"_q, u"Бета-версии"_q),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(CurrentChannel() == Channel::Beta),
		.keywords = { u"beta"_q, u"channel"_q, u"бета"_q, u"канал"_q },
	});
	if (beta) {
		beta->toggledValue(
		) | rpl::filter([](bool checked) {
			return checked != (CurrentChannel() == Channel::Beta);
		}) | rpl::on_next([](bool checked) {
			SetChannel(checked ? Channel::Beta : Channel::Stable);
		}, beta->lifetime());
	}
	builder.addButton({
		.id = u"tgeshka/check_updates"_q,
		.title = TrValue(u"Check for updates"_q, u"Проверить обновления"_q),
		.st = &st::settingsButtonNoIcon,
		.label = StatusTextValue(),
		.onClick = [] {
			if (CurrentStatus().state == State::Ready) {
				InstallAndRestart();
			} else {
				CheckNow();
			}
		},
		.keywords = { u"update"_q, u"обновление"_q },
	});
	const auto previous = PreviousVersion();
	if (!previous.isEmpty()) {
		builder.addButton({
			.id = u"tgeshka/rollback"_q,
			.title = TrValue(
				u"Return to %1"_q.arg(previous),
				u"Вернуть версию %1"_q.arg(previous)),
			.st = &st::settingsButtonNoIcon,
			.onClick = [=] {
				if (!controller) {
					return;
				}
				controller->window().show(Ui::MakeConfirmBox({
					.text = Tr(
						u"Restart and return to %1? Version %2 will "
						u"not be offered again until a newer one."_q,
						u"Перезапустить и вернуть %1? Версия %2 не будет "
						u"предлагаться, пока не выйдет более новая."_q)
						.arg(previous)
						.arg(CurrentVersion()),
					.confirmed = [] { RollbackAndRestart(); },
					.confirmText = Tr(u"Restart"_q, u"Перезапустить"_q),
				}));
			},
			.keywords = { u"rollback"_q, u"откат"_q },
		});
	}
}

} // namespace Tgeshka::Update
