/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_whats_new.h"

#include "tgeshka/tgeshka_brand.h"
#include "tgeshka/tgeshka_gateway.h"
#include "tgeshka/tgeshka_lang.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "settings.h"
#include "ui/layers/generic_box.h"
#include "ui/layers/show.h"
#include "ui/vertical_list.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"

#include <QtCore/QDate>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

namespace Tgeshka::WhatsNew {
namespace {

constexpr auto kChangelog = ":/tgeshka/changelog.json";
constexpr auto kSeenMarker = "tdata/tgeshka_whatsnew";
constexpr auto kLangMarker = "tdata/tgeshka_lang";
constexpr auto kShowDelay = crl::time(3000);

bool ExistingDataFolder = false;

struct Release {
	QString version;
	QString date;
	std::vector<QString> items;
};

[[nodiscard]] std::vector<Release> LoadChangelog() {
	auto file = QFile(QString::fromLatin1(kChangelog));
	if (!file.open(QIODevice::ReadOnly)) {
		LOG(("TGeshka Error: built-in changelog not found."));
		return {};
	}
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(file.readAll(), &error);
	if (error.error != QJsonParseError::NoError || !document.isArray()) {
		LOG(("TGeshka Error: bad built-in changelog."));
		return {};
	}
	auto result = std::vector<Release>();
	for (const auto &value : document.array()) {
		const auto object = value.toObject();
		auto release = Release{
			.version = object.value(u"version"_q).toString(),
			.date = object.value(u"date"_q).toString(),
		};
		for (const auto &item : object.value(u"items"_q).toArray()) {
			if (const auto text = item.toString(); !text.isEmpty()) {
				release.items.push_back(text);
			}
		}
		if (!release.version.isEmpty() && !release.items.empty()) {
			result.push_back(std::move(release));
		}
	}
	return result;
}

// 0.1.2-beta.3 -> 0.1.2
[[nodiscard]] QString BaseVersion(const QString &version) {
	return version.section(QChar('-'), 0, 0);
}

// A beta build gets the entry of its version (0.1.3-beta.2 shows
// 0.1.3-beta), the stable one the same entry until it is renamed.
[[nodiscard]] std::optional<Release> FindRelease(
		const std::vector<Release> &releases,
		const QString &version) {
	for (const auto &release : releases) {
		if (release.version == version) {
			return release;
		}
	}
	const auto base = BaseVersion(version);
	for (const auto &release : releases) {
		if (BaseVersion(release.version) == base) {
			return release;
		}
	}
	return std::nullopt;
}

[[nodiscard]] QString CurrentVersion() {
	return QString::fromLatin1(TGESHKA_VERSION);
}

[[nodiscard]] QString MarkerPath(const char *name) {
	return cWorkingDir() + QString::fromLatin1(name);
}

[[nodiscard]] QString ReadSeen() {
	auto file = QFile(MarkerPath(kSeenMarker));
	return file.open(QIODevice::ReadOnly)
		? QString::fromUtf8(file.readAll()).trimmed()
		: QString();
}

void WriteSeen(const QString &version) {
	const auto path = MarkerPath(kSeenMarker);
	QDir().mkpath(QFileInfo(path).absolutePath());
	if (auto file = QFile(path); file.open(QIODevice::WriteOnly)) {
		file.write(version.toUtf8());
	}
}

[[nodiscard]] QString DateText(const QString &date) {
	const auto parsed = QDate::fromString(date, Qt::ISODate);
	return parsed.isValid() ? parsed.toString(u"dd.MM.yyyy"_q) : date;
}

void AddItems(not_null<Ui::GenericBox*> box, const Release &release) {
	for (const auto &item : release.items) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(QString::fromUtf8("\xe2\x80\xa2 ") + item),
			st::boxLabel));
		box->addSkip(st::boxLittleSkip);
	}
}

void AddRelease(not_null<Ui::GenericBox*> box, const Release &release) {
	auto title = release.version;
	if (!release.date.isEmpty()) {
		title += u", "_q + DateText(release.date);
	}
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(title),
		st::defaultSubsectionTitle));
	box->addSkip(st::boxLittleSkip);
	AddItems(box, release);
}

void AfterUpdateBox(
		not_null<Ui::GenericBox*> box,
		const Release &release) {
	box->setTitle(rpl::single(Tr(
		u"What's new in %1"_q,
		u"Что нового в %1"_q).arg(CurrentVersion())));
	box->setWidth(st::boxWideWidth);
	AddItems(box, release);
	box->addButton(rpl::single(Tr(u"Close"_q, u"Закрыть"_q)), [=] {
		box->closeBox();
	});
	box->addLeftButton(
		rpl::single(Tr(u"All changes"_q, u"Все изменения"_q)),
		[=] {
			box->uiShow()->showBox(::Box(ListBox));
			box->closeBox();
		});
}

} // namespace

void ListBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(Tr(u"What's new"_q, u"Что нового"_q)));
	box->setWidth(st::boxWideWidth);
	const auto releases = LoadChangelog();
	if (releases.empty()) {
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			rpl::single(Tr(
				u"The list of changes is not available."_q,
				u"Список изменений недоступен."_q)),
			st::boxLabel));
	}
	for (const auto &release : releases) {
		AddRelease(box, release);
	}
	box->addButton(rpl::single(Tr(u"Close"_q, u"Закрыть"_q)), [=] {
		box->closeBox();
	});
}

void RememberDataFolder() {
	ExistingDataFolder = QFile::exists(MarkerPath(kLangMarker))
		|| QFile::exists(MarkerPath(kSeenMarker));
}

void ShowAfterUpdate() {
	const auto release = FindRelease(LoadChangelog(), CurrentVersion());
	if (!release) {
		return;
	}
	const auto seen = ReadSeen();
	if (seen == release->version) {
		return;
	} else if (seen.isEmpty() && !ExistingDataFolder) {
		// A fresh install: everything is new, nothing to tell.
		WriteSeen(release->version);
		return;
	}
	base::call_delayed(kShowDelay, [=] {
		// Not over the access key screen: shown on a later launch.
		if (CurrentGatewayConfig().needsToken()) {
			return;
		}
		const auto window = Core::App().activePrimaryWindow();
		if (!window) {
			return;
		}
		WriteSeen(release->version);
		LOG(("TGeshka: what's new in %1 shown.").arg(release->version));
		window->show(::Box(AfterUpdateBox, *release));
	});
}

} // namespace Tgeshka::WhatsNew
