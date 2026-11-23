/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_boxes.h"

#include "tgeshka/tgeshka_gateway.h"
#include "tgeshka/tgeshka_lang.h"
#include "ui/layers/generic_box.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>

namespace Tgeshka {
namespace {

[[nodiscard]] QString StatusLine(
		const GatewayConfig &config,
		GatewayStatus status) {
	if (!config.hasUrl()) {
		return Tr(u"No gateway address"_q, u"Нет адреса шлюза"_q);
	} else if (!config.hasToken()) {
		return Tr(u"No access key"_q, u"Нет ключа доступа"_q);
	}
	switch (status) {
	case GatewayStatus::Connected: return Tr(u"Connected"_q, u"Подключено"_q);
	case GatewayStatus::Rejected: return Tr(u"The gateway rejected the key"_q, u"Шлюз не принял ключ"_q);
	case GatewayStatus::Failed: return Tr(u"The gateway is unreachable"_q, u"Шлюз недоступен"_q);
	case GatewayStatus::Connecting:
	case GatewayStatus::Unknown: break;
	}
	return Tr(u"Connecting to the gateway..."_q, u"Подключение к шлюзу..."_q);
}

[[nodiscard]] QString CheckLine(const KeyCheckResult &result) {
	switch (result.type) {
	case KeyCheckResult::Type::Ok:
		return result.maxConnections
			? Tr(
				u"The key is valid, connections: %1 of %2"_q,
				u"Ключ действителен, соединений: %1 из %2"_q)
				.arg(result.activeConnections)
				.arg(result.maxConnections)
			: Tr(u"The key is valid"_q, u"Ключ действителен"_q);
	case KeyCheckResult::Type::Rejected:
		return Tr(u"The gateway rejected the key: check the key and the address"_q, u"Шлюз не принял ключ: проверьте ключ и адрес"_q);
	case KeyCheckResult::Type::Unreachable:
		break;
	}
	return Tr(u"The gateway is unreachable now"_q, u"Шлюз сейчас недоступен"_q);
}

[[nodiscard]] QString ClipboardText() {
	const auto clipboard = QGuiApplication::clipboard();
	return clipboard ? clipboard->text().trimmed() : QString();
}

not_null<Ui::FlatLabel*> AddLabel(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> text,
		const style::FlatLabel &st) {
	return box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		std::move(text),
		st));
}

// PasswordInput is a QLineEdit, not an RpWidget: it lives in a wrap row.
not_null<Ui::PasswordInput*> AddPasswordField(
		not_null<Ui::GenericBox*> box,
		const QString &placeholder,
		const QString &value) {
	const auto wrap = box->addRow(object_ptr<Ui::RpWidget>(box));
	const auto field = Ui::CreateChild<Ui::PasswordInput>(
		wrap,
		st::defaultInputField,
		rpl::single(placeholder),
		value);
	field->move(0, 0);
	field->heightValue(
	) | rpl::on_next([=](int height) {
		wrap->resize(wrap->width(), height);
	}, field->lifetime());
	wrap->widthValue(
	) | rpl::on_next([=](int width) {
		field->resize(width, field->height());
	}, field->lifetime());
	return field;
}

} // namespace

rpl::producer<QString> GatewayStatusText() {
	return rpl::combine(
		GatewayConfigValue(),
		GatewayStatusValue()
	) | rpl::map([](const GatewayConfig &config, GatewayStatus status) {
		return StatusLine(config, status);
	});
}

void GatewaySettingsBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(Tr(u"Gateway connection"_q, u"Подключение через шлюз"_q)));
	box->setWidth(st::boxWideWidth);

	const auto initial = CurrentGatewayConfig();

	AddLabel(
		box,
		rpl::single(Tr(
			u"The client reaches Telegram through your gateway over "
			"WebSocket. The access key is issued by the gateway owner."_q,
			u"Клиент ходит в Telegram через ваш шлюз по WebSocket. "
			"Ключ доступа выдаёт владелец шлюза."_q)),
		st::boxDividerLabel);
	Ui::AddSkip(box->verticalLayout());

	const auto url = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(Tr(u"Gateway address"_q, u"Адрес шлюза"_q)),
		initial.url));
	Ui::AddSkip(box->verticalLayout());

	const auto token = AddPasswordField(
		box,
		(initial.hasToken()
			? Tr(u"The key is saved, enter a new one to replace it"_q, u"Ключ сохранён, введите новый для замены"_q)
			: Tr(u"Access key"_q, u"Ключ доступа"_q)),
		QString());
	Ui::AddSkip(box->verticalLayout());

	const auto paste = box->addRow(object_ptr<Ui::LinkButton>(
		box,
		Tr(u"Paste a key or a link from the clipboard"_q, u"Вставить ключ или ссылку из буфера обмена"_q)));
	Ui::AddSkip(box->verticalLayout());

	const auto check = box->lifetime().make_state<rpl::variable<QString>>();
	AddLabel(
		box,
		rpl::combine(
			GatewayStatusText(),
			check->value()
		) | rpl::map([](const QString &status, const QString &check) {
			return check.isEmpty()
				? (Tr(u"Status: "_q, u"Статус: "_q) + status)
				: (Tr(u"Status: "_q, u"Статус: "_q) + status + '\n' + check);
		}),
		st::boxDividerLabel);

	const auto runCheck = [=](const QString &gateway, const QString &key) {
		if (key.isEmpty()) {
			*check = QString();
			return;
		}
		*check = Tr(u"Checking the key..."_q, u"Проверка ключа..."_q);
		const auto weak = QPointer<Ui::GenericBox>(box.get());
		CheckAccessKey(gateway, key, [=](KeyCheckResult result) {
			if (weak) {
				*check = CheckLine(result);
			}
		});
	};
	if (initial.hasToken()) {
		runCheck(initial.url, initial.token);
	}

	paste->setClickedCallback([=] {
		auto parsed = GatewayConfig{ .url = url->getLastText().trimmed() };
		if (!ParseAccessKey(ClipboardText(), parsed)) {
			*check = Tr(u"No access key in the clipboard"_q, u"В буфере обмена нет ключа доступа"_q);
			return;
		}
		url->setText(parsed.url);
		token->setText(parsed.token);
		runCheck(parsed.url, parsed.token);
	});

	const auto save = [=] {
		auto result = CurrentGatewayConfig();
		const auto newUrl = url->getLastText().trimmed();
		if (!newUrl.isEmpty() && !IsValidGatewayUrl(newUrl)) {
			url->showError();
			return;
		}
		result.url = newUrl.isEmpty() ? DefaultGatewayUrl() : newUrl;
		const auto newToken = token->getLastText().trimmed();
		if (!newToken.isEmpty()) {
			auto parsed = result;
			if (!ParseAccessKey(newToken, parsed)) {
				token->showError();
				return;
			}
			result = parsed;
		}
		if (!IsValidGatewayUrl(result.url)) {
			url->showError();
			return;
		}
		SetGatewayConfig(result);
		box->closeBox();
	};
	box->addButton(rpl::single(Tr(u"Save"_q, u"Сохранить"_q)), save);
	box->addButton(rpl::single(Tr(u"Cancel"_q, u"Отмена"_q)), [=] { box->closeBox(); });
}

void AccessKeyBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(Tr(u"Paste the access key"_q, u"Вставьте ключ доступа"_q)));
	box->setWidth(st::boxWideWidth);
	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);

	const auto askUrl = !CurrentGatewayConfig().hasUrl();
	AddLabel(
		box,
		rpl::single(askUrl
			? Tr(
				u"The gateway owner issues the address and the key: a link "
				"tgeshka://gateway?url=...&token=... or an address wss://... "
				"and a key tc_... Then the usual Telegram login opens."_q,
				u"Ключ и адрес шлюза выдаёт его владелец: ссылка "
				"tgeshka://gateway?url=...&token=... или адрес wss://... "
				"и ключ tc_... После них откроется обычный вход в Telegram."_q)
			: Tr(
				u"The gateway owner issues the key: a string tc_... "
				"or a link tgeshka://gateway?... "
				"Then the usual Telegram login opens."_q,
				u"Ключ выдаёт владелец шлюза: строка вида tc_... "
				"или ссылка tgeshka://gateway?... "
				"После него откроется обычный вход в Telegram."_q)),
		st::boxLabel);
	Ui::AddSkip(box->verticalLayout());

	auto fromClipboard = GatewayConfig{ .url = CurrentGatewayConfig().url };
	const auto found = ParseAccessKey(ClipboardText(), fromClipboard);

	Ui::InputField *urlField = nullptr;
	if (askUrl) {
		urlField = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(Tr(
				u"Gateway address (not needed for a link)"_q,
				u"Адрес шлюза (не нужен для ссылки)"_q)),
			found ? fromClipboard.url : QString()));
		Ui::AddSkip(box->verticalLayout());
	}

	const auto field = AddPasswordField(
		box,
		Tr(u"Access key or link"_q, u"Ключ доступа или ссылка"_q),
		found ? fromClipboard.token : QString());
	Ui::AddSkip(box->verticalLayout());

	const auto pending = box->lifetime().make_state<GatewayConfig>(
		found ? fromClipboard : CurrentGatewayConfig());
	const auto status = box->lifetime().make_state<rpl::variable<QString>>(
		found ? Tr(u"A key was found in the clipboard"_q, u"Ключ найден в буфере обмена"_q) : QString());
	AddLabel(box, status->value(), st::boxDividerLabel);

	const auto busy = box->lifetime().make_state<bool>(false);
	const auto weak = QPointer<Ui::GenericBox>(box.get());
	const auto finish = [=](GatewayConfig config) {
		// A real change closes the box through GatewayConfigValue below.
		const auto changed = !(CurrentGatewayConfig() == config);
		SetGatewayConfig(std::move(config));
		if (!changed && weak) {
			weak->closeBox();
		}
	};
	const auto submit = [=] {
		if (*busy) {
			return;
		}
		auto parsed = *pending;
		if (urlField) {
			const auto typed = urlField->getLastText().trimmed();
			if (!typed.isEmpty() && !IsValidGatewayUrl(typed)) {
				urlField->showError();
				*status = Tr(u"The gateway address starts with wss://"_q, u"Адрес шлюза начинается с wss://"_q);
				return;
			}
			parsed.url = typed;
		}
		const auto text = field->getLastText().trimmed();
		if (!ParseAccessKey(text, parsed)
			&& !ParseAccessKey(ClipboardText(), parsed)) {
			field->showError();
			*status = Tr(u"This does not look like an access key"_q, u"Это не похоже на ключ доступа"_q);
			return;
		}
		if (!IsValidGatewayUrl(parsed.url)) {
			if (urlField) {
				urlField->showError();
			}
			*status = Tr(u"Enter the gateway address or paste a link"_q, u"Укажите адрес шлюза или вставьте ссылку"_q);
			return;
		}
		*busy = true;
		*status = Tr(u"Checking the key..."_q, u"Проверка ключа..."_q);
		CheckAccessKey(parsed.url, parsed.token, [=](KeyCheckResult r) {
			if (!weak) {
				return;
			}
			*busy = false;
			switch (r.type) {
			case KeyCheckResult::Type::Ok:
				finish(parsed);
				break;
			case KeyCheckResult::Type::Rejected:
				field->showError();
				*status = CheckLine(r);
				break;
			case KeyCheckResult::Type::Unreachable:
				// Keep the key: the gateway may be down only for a moment.
				finish(parsed);
				break;
			}
		});
	};
	box->addButton(rpl::single(Tr(u"Continue"_q, u"Продолжить"_q)), submit);
	box->addLeftButton(rpl::single(Tr(u"Later"_q, u"Позже"_q)), [=] { box->closeBox(); });
	box->setFocusCallback([=] { field->setFocusFast(); });

	// The key may arrive by a tgeshka:// link while the box is open.
	GatewayConfigValue(
	) | rpl::filter([](const GatewayConfig &config) {
		return !config.needsToken();
	}) | rpl::take(1) | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
}

} // namespace Tgeshka
