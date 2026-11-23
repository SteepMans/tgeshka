/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_lang.h"

#include "lang/lang_instance.h"
#include "lang/lang_keys.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>

namespace Tgeshka {
namespace {

constexpr auto kMarkerName = "tgeshka_lang";
constexpr auto kBuiltInRussian = ":/tgeshka/langs/ru.strings";

[[nodiscard]] QString MarkerPath() {
	return cWorkingDir() + u"tdata/"_q + QString::fromLatin1(kMarkerName);
}

} // namespace

bool IsRussian() {
	const auto &instance = Lang::GetInstance();
	return instance.id().startsWith(u"ru"_q, Qt::CaseInsensitive)
		|| instance.baseId().startsWith(u"ru"_q, Qt::CaseInsensitive);
}

QString Tr(const QString &en, const QString &ru) {
	return IsRussian() ? ru : en;
}

rpl::producer<QString> TrValue(QString en, QString ru) {
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		Lang::GetInstance().updated(),
		Lang::GetInstance().idChanges() | rpl::to_empty
	)) | rpl::map([=] {
		return Tr(en, ru);
	});
}

void ApplyBuiltInLanguage() {
	const auto marker = MarkerPath();
	if (QFile::exists(marker)) {
		return;
	}
	QDir().mkpath(QFileInfo(marker).absolutePath());
	if (auto file = QFile(marker); file.open(QIODevice::WriteOnly)) {
		file.write("1");
	}

	auto &instance = Lang::GetInstance();
	if (instance.isCustom()
		|| instance.version(Lang::Pack::Current) > 0
		|| (Lang::LanguageIdOrDefault(instance.id())
			!= Lang::DefaultLanguageId())) {
		return; // Chosen before: an existing data folder.
	}
	if (!instance.systemLangCode().startsWith(
			u"ru"_q,
			Qt::CaseInsensitive)) {
		return;
	}
	auto file = QFile(QString::fromLatin1(kBuiltInRussian));
	if (!file.open(QIODevice::ReadOnly)) {
		LOG(("TGeshka Error: built-in Russian strings not found."));
		return;
	}
	instance.switchToBuiltIn({
		.id = u"ru"_q,
		.pluralId = u"ru"_q,
		.name = u"Russian"_q,
		.nativeName = u"Русский"_q,
	}, file.readAll());
	LOG(("TGeshka: interface switched to built-in Russian."));
}

} // namespace Tgeshka
