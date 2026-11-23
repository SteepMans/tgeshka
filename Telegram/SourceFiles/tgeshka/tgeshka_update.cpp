/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_update.h"

#include "tgeshka/tgeshka_brand.h"
#include "tgeshka/tgeshka_gateway.h"
#include "tgeshka/tgeshka_update_verify.h"
#include "base/timer.h"
#include "base/zlib_help.h"
#include "core/application.h"
#include "logs.h"
#include "settings.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QDirIterator>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

#include <array>

#ifdef Q_OS_WIN
#include <windows.h>
#endif // Q_OS_WIN

namespace Tgeshka::Update {
namespace {

constexpr auto kCheckInterval = 6 * 60 * 60 * crl::time(1000);
constexpr auto kRetryInterval = 15 * 60 * crl::time(1000);
constexpr auto kManifestTimeout = 30000;
constexpr auto kDownloadTimeout = 120000; // Without a single byte.
constexpr auto kMaxManifestSize = 256 * 1024;
constexpr auto kMaxPackageSize = qint64(512) * 1024 * 1024;
constexpr auto kMaxEntrySize = 512 * 1024 * 1024;
constexpr auto kPlatform = "desktop";
constexpr auto kPackageFile = "package.zip";
constexpr auto kPartFile = "package.part";
constexpr auto kStateFile = "state.json";
constexpr auto kPreviousDir = "previous";
constexpr auto kVersionFile = "tgeshka-version.txt";

struct Offer {
	QString version;
	QString minVersion;
	QUrl url;
	QString sha256;
	qint64 size = 0;
	QByteArray signature;
};

struct Runtime {
	bool loaded = false;
	bool started = false;
	bool prepared = false;
	bool justInstalled = false;
	rpl::variable<Channel> channel = Channel::Stable;
	rpl::variable<Status> status;

	// Persistent, tgeshka_update/state.json in the working directory.
	QString skipVersion; // Rolled back from, not offered again.
	QString readyVersion;
	QString readySha256;
	QString partSha256; // Expected hash of the partial download.
	QString installing; // Set right before Updater.exe runs.

	std::unique_ptr<QNetworkAccessManager> network;
	QPointer<QNetworkReply> reply;
	std::unique_ptr<QFile> part;
	std::unique_ptr<QCryptographicHash> hash;
	bool partFailed = false;
	bool checking = false;
	Offer downloading;
	std::unique_ptr<base::Timer> timer;
	rpl::lifetime lifetime;
};

[[nodiscard]] Runtime &Data() {
	static auto result = Runtime();
	return result;
}

[[nodiscard]] QString UpdateDir() {
	return cWorkingDir() + u"tgeshka_update/"_q;
}

[[nodiscard]] QString UpdatesDir() {
	return cWorkingDir() + u"tupdates"_q;
}

[[nodiscard]] QString TempDir() {
	return cWorkingDir() + u"tupdates/temp/"_q;
}

[[nodiscard]] QString ChannelName(Channel channel) {
	return (channel == Channel::Beta) ? u"beta"_q : u"stable"_q;
}

void UpdateStatus(Fn<void(Status&)> change) {
	auto status = Data().status.current();
	change(status);
	Data().status = std::move(status);
}

void Save() {
	const auto &data = Data();
	auto object = QJsonObject();
	object.insert(u"channel"_q, ChannelName(data.channel.current()));
	const auto put = [&](const QString &key, const QString &value) {
		if (!value.isEmpty()) {
			object.insert(key, value);
		}
	};
	put(u"skip"_q, data.skipVersion);
	put(u"ready_version"_q, data.readyVersion);
	put(u"ready_sha256"_q, data.readySha256);
	put(u"part_sha256"_q, data.partSha256);
	put(u"installing"_q, data.installing);
	QDir().mkpath(UpdateDir());
	auto file = QFile(UpdateDir() + kStateFile);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
	}
}

void Load() {
	auto &data = Data();
	if (data.loaded) {
		return;
	}
	data.loaded = true;
	auto file = QFile(UpdateDir() + kStateFile);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	const auto object = QJsonDocument::fromJson(
		file.read(kMaxManifestSize)).object();
	const auto get = [&](const QString &key) {
		return object.value(key).toString();
	};
	data.channel = (get(u"channel"_q) == u"beta"_q)
		? Channel::Beta
		: Channel::Stable;
	data.skipVersion = get(u"skip"_q);
	data.readyVersion = get(u"ready_version"_q);
	data.readySha256 = get(u"ready_sha256"_q);
	data.partSha256 = get(u"part_sha256"_q);
	data.installing = get(u"installing"_q);
}

[[nodiscard]] QByteArray FileSha256(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QByteArray();
	}
	auto hash = QCryptographicHash(QCryptographicHash::Sha256);
	if (!hash.addData(&file)) {
		return QByteArray();
	}
	return hash.result();
}

void ClearReady() {
	auto &data = Data();
	QFile::remove(UpdateDir() + kPackageFile);
	QFile::remove(UpdateDir() + kPartFile);
	data.readyVersion = QString();
	data.readySha256 = QString();
	data.partSha256 = QString();
}

[[nodiscard]] QUrl GatewayBase() {
	auto result = QUrl(CurrentGatewayConfig().url);
	result.setScheme(u"https"_q);
	result.setPath(u"/"_q);
	result.setQuery(QString());
	return result;
}

[[nodiscard]] QUrl ManifestUrl() {
	auto result = GatewayBase().resolved(QUrl(u"/v1/update/manifest"_q));
	auto query = QUrlQuery();
	query.addQueryItem(u"platform"_q, QString::fromLatin1(kPlatform));
	query.addQueryItem(
		u"channel"_q,
		ChannelName(Data().channel.current()));
	result.setQuery(query);
	return result;
}

// Only the gateway, with the access key: no redirects anywhere else.
[[nodiscard]] QNetworkRequest MakeRequest(const QUrl &url, int timeout) {
	auto request = QNetworkRequest(url);
	request.setRawHeader(
		"Authorization",
		"Bearer " + CurrentGatewayConfig().token.toUtf8());
	request.setRawHeader(
		"X-TGeshka-Client",
		QByteArray(kPlatform) + '/' + CurrentVersion().toUtf8());
	request.setTransferTimeout(timeout);
	request.setAttribute(
		QNetworkRequest::RedirectPolicyAttribute,
		QNetworkRequest::ManualRedirectPolicy);
	return request;
}

[[nodiscard]] QNetworkAccessManager &Network() {
	auto &data = Data();
	if (!data.network) {
		data.network = std::make_unique<QNetworkAccessManager>();
	}
	return *data.network;
}

void Fail(const QString &reason) {
	LOG(("TGeshka Update Error: %1").arg(reason));
	UpdateStatus([](Status &status) {
		status.state = State::Failed;
		status.received = status.total = 0;
	});
	if (const auto timer = Data().timer.get()) {
		timer->callOnce(kRetryInterval);
	}
}

void ClosePart() {
	auto &data = Data();
	if (data.part) {
		data.part->close();
		data.part = nullptr;
	}
	data.hash = nullptr;
}

void DownloadFinished(not_null<QNetworkReply*> reply);

void StartDownload(const Offer &offer) {
	auto &data = Data();
	ClosePart();
	QDir().mkpath(UpdateDir());

	// Continue a partial download of the same package with Range.
	const auto partPath = UpdateDir() + kPartFile;
	auto offset = qint64(0);
	if (data.partSha256 == offer.sha256 && QFile::exists(partPath)) {
		offset = QFileInfo(partPath).size();
		if (offset >= offer.size) {
			offset = 0;
		}
	}
	if (!offset) {
		ClearReady();
	}
	data.partSha256 = offer.sha256;
	Save();

	data.part = std::make_unique<QFile>(partPath);
	const auto mode = offset
		? QIODevice::ReadWrite
		: (QIODevice::WriteOnly | QIODevice::Truncate);
	if (!data.part->open(mode)) {
		data.part = nullptr;
		Fail(u"can't write the package file"_q);
		return;
	}
	data.hash = std::make_unique<QCryptographicHash>(
		QCryptographicHash::Sha256);
	if (offset) {
		if (!data.hash->addData(data.part.get())
			|| data.part->pos() != offset) {
			ClosePart();
			QFile::remove(partPath);
			Fail(u"can't read the partial package"_q);
			return;
		}
	}
	data.partFailed = false;
	data.downloading = offer;
	UpdateStatus([&](Status &status) {
		status.state = State::Downloading;
		status.version = offer.version;
		status.received = offset;
		status.total = offer.size;
	});
	LOG(("TGeshka Update: downloading %1 (%2 bytes from %3)."
		).arg(offer.version
		).arg(offer.size
		).arg(offset));

	auto request = MakeRequest(offer.url, kDownloadTimeout);
	if (offset) {
		request.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + '-');
	}
	const auto reply = Network().get(request);
	data.reply = reply;
	QObject::connect(reply, &QNetworkReply::readyRead, [=] {
		auto &data = Data();
		if (!data.part || data.partFailed) {
			return;
		}
		if (offset && reply->property("tgeshka_seen").isNull()) {
			reply->setProperty("tgeshka_seen", true);
			const auto code = reply->attribute(
				QNetworkRequest::HttpStatusCodeAttribute).toInt();
			if (code == 200) {
				// Range ignored: the whole file comes again.
				data.part->resize(0);
				data.part->seek(0);
				data.hash->reset();
			}
		}
		const auto bytes = reply->readAll();
		if (data.part->write(bytes) != bytes.size()
			|| data.part->size() > data.downloading.size) {
			data.partFailed = true;
			reply->abort();
			return;
		}
		data.hash->addData(bytes);
		const auto received = data.part->size();
		UpdateStatus([&](Status &status) {
			status.received = received;
		});
	});
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		DownloadFinished(reply);
	});
}

void DownloadFinished(not_null<QNetworkReply*> reply) {
	reply->deleteLater();
	auto &data = Data();
	if (!data.part) {
		return;
	}
	const auto code = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();
	const auto size = data.part->size();
	const auto sha256 = data.hash->result();
	ClosePart();
	const auto offer = data.downloading;
	const auto partPath = UpdateDir() + kPartFile;
	const auto drop = [&](const QString &reason) {
		QFile::remove(partPath);
		data.partSha256 = QString();
		Save();
		Fail(reason);
	};
	if (data.partFailed) {
		drop(u"package is larger than %1"_q.arg(offer.size));
	} else if (reply->error() != QNetworkReply::NoError
		|| (code != 200 && code != 206)) {
		// The part stays for the next try with Range.
		Fail(u"download stopped at %1 of %2, code %3, %4"_q
			.arg(size)
			.arg(offer.size)
			.arg(code)
			.arg(reply->errorString()));
	} else if (size != offer.size
		|| QString::fromLatin1(sha256.toHex()) != offer.sha256) {
		drop(u"package hash mismatch, size %1 of %2"_q
			.arg(size)
			.arg(offer.size));
	} else if (!VerifyPackageDigest(sha256, offer.signature)) {
		drop(u"bad package signature for %1"_q.arg(offer.version));
	} else {
		QFile::remove(UpdateDir() + kPackageFile);
		if (!QFile::rename(partPath, UpdateDir() + kPackageFile)) {
			drop(u"can't keep the package file"_q);
			return;
		}
		data.readyVersion = offer.version;
		data.readySha256 = offer.sha256;
		data.partSha256 = QString();
		Save();
		LOG(("TGeshka Update: %1 is ready.").arg(offer.version));
		UpdateStatus([&](Status &status) {
			status.state = State::Ready;
			status.version = offer.version;
			status.received = status.total = size;
		});
	}
}

[[nodiscard]] std::optional<Offer> ParseOffer(
		const QByteArray &json,
		QString *error) {
	auto parseError = QJsonParseError();
	const auto document = QJsonDocument::fromJson(json, &parseError);
	if (parseError.error != QJsonParseError::NoError
		|| !document.isObject()) {
		*error = u"bad manifest json"_q;
		return std::nullopt;
	}
	const auto object = document.object();
	auto result = Offer();
	result.version = object.value(u"version"_q).toString();
	result.minVersion = object.value(u"min_version"_q).toString();
	const auto path = object.value(u"url"_q).toString();
	result.sha256 = object.value(u"sha256"_q).toString().toLower();
	result.size = qint64(object.value(u"size"_q).toDouble());
	result.signature = QByteArray::fromBase64(
		object.value(u"signature"_q).toString().toLatin1());

	// The package goes through the same gateway, never another host.
	if (!path.startsWith(u"/v1/update/package/"_q) || path.contains(u".."_q)) {
		*error = u"bad package path in manifest"_q;
		return std::nullopt;
	}
	result.url = GatewayBase().resolved(QUrl(path));
	static const auto hex = QRegularExpression(u"^[0-9a-f]{64}$"_q);
	if (!IsValidVersion(result.version)
		|| (!result.minVersion.isEmpty()
			&& !IsValidVersion(result.minVersion))
		|| !hex.match(result.sha256).hasMatch()
		|| result.size <= 0
		|| result.size > kMaxPackageSize
		|| result.signature.size() != 64) {
		*error = u"bad manifest fields"_q;
		return std::nullopt;
	}
	return result;
}

void ApplyManifest(const QByteArray &json) {
	auto &data = Data();
	auto error = QString();
	const auto offer = ParseOffer(json, &error);
	if (!offer) {
		Fail(error);
		return;
	}
	const auto current = CurrentVersion();
	const auto newer = CompareVersions(offer->version, current) > 0;
	const auto required = newer
		&& !offer->minVersion.isEmpty()
		&& CompareVersions(current, offer->minVersion) < 0;
	const auto skipped = !required
		&& !data.skipVersion.isEmpty()
		&& CompareVersions(offer->version, data.skipVersion) <= 0;
	UpdateStatus([&](Status &status) {
		status.required = required;
	});
	LOG(("TGeshka Update: manifest %1 (min %2), running %3%4."
		).arg(offer->version
		).arg(offer->minVersion.isEmpty() ? u"-"_q : offer->minVersion
		).arg(current
		).arg(skipped ? u", skipped"_q : QString()));
	if (!newer || skipped) {
		UpdateStatus([&](Status &status) {
			status.state = State::Latest;
			status.version = QString();
		});
		return;
	}
	if (data.readyVersion == offer->version
		&& data.readySha256 == offer->sha256
		&& QString::fromLatin1(
			FileSha256(UpdateDir() + kPackageFile).toHex()) == offer->sha256) {
		UpdateStatus([&](Status &status) {
			status.state = State::Ready;
			status.version = offer->version;
		});
		return;
	}
	if (data.reply) {
		if (data.downloading.sha256 == offer->sha256) {
			return;
		}
		data.reply->abort();
	}
	StartDownload(*offer);
}

void Check() {
	auto &data = Data();
	if (data.timer) {
		data.timer->callOnce(kCheckInterval);
	}
	if (CurrentGatewayConfig().needsToken()) {
		return;
	}
	const auto state = data.status.current().state;
	if (data.checking || state == State::Downloading) {
		return;
	}
	data.checking = true;
	if (state != State::Ready) {
		// A ready update stays offered while the manifest is checked.
		UpdateStatus([](Status &status) {
			status.state = State::Checking;
		});
	}
	const auto reply = Network().get(
		MakeRequest(ManifestUrl(), kManifestTimeout));
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		Data().checking = false;
		const auto code = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const auto body = reply->read(kMaxManifestSize);
		if (code == 204) {
			// No release for this channel yet.
			LOG(("TGeshka Update: no release for %1."
				).arg(ChannelName(Data().channel.current())));
			UpdateStatus([](Status &status) {
				status.state = State::Latest;
				status.version = QString();
				status.required = false;
			});
		} else if (reply->error() != QNetworkReply::NoError
			|| code != 200) {
			Fail(u"manifest request failed, code %1, %2"_q
				.arg(code)
				.arg(reply->errorString()));
		} else {
			ApplyManifest(body);
		}
	});
}

[[nodiscard]] bool SafeRelativePath(const QString &path) {
	if (path.isEmpty()
		|| path.startsWith('/')
		|| path.contains(':')
		|| path.contains('\\')) {
		return false;
	}
	for (const auto &part : path.split('/')) {
		if (part.isEmpty() || part == u".."_q || part == u"."_q) {
			return false;
		}
	}
	return true;
}

// The release zip has one top folder (TGeshka/...), its content goes
// to tupdates/temp the way Updater.exe expects.
[[nodiscard]] bool UnpackPackage(
		const QByteArray &bytes,
		const QString &version,
		QStringList &files) {
	auto zip = zlib::FileToRead(bytes);
	if (zip.goToFirstFile() != UNZ_OK) {
		return false;
	}
	auto entries = std::vector<std::pair<QString, QByteArray>>();
	auto prefix = std::optional<QString>();
	do {
		auto name = zip.getCurrentFileName().replace('\\', '/');
		if (name.endsWith('/')) {
			continue;
		}
		auto content = zip.readCurrentFileContent(kMaxEntrySize);
		if (zip.error() != UNZ_OK) {
			return false;
		}
		const auto slash = name.indexOf('/');
		const auto top = (slash > 0) ? name.mid(0, slash + 1) : QString();
		if (!prefix) {
			prefix = top;
		} else if (*prefix != top) {
			prefix = QString();
		}
		entries.emplace_back(std::move(name), std::move(content));
	} while (zip.goToNextFile() == UNZ_OK);
	if (zip.error() != UNZ_OK && zip.error() != UNZ_END_OF_LIST_OF_FILE) {
		return false;
	}
	auto packaged = QString();
	for (auto &[name, content] : entries) {
		if (prefix && !prefix->isEmpty()) {
			name = name.mid(prefix->size());
		}
		if (!SafeRelativePath(name)) {
			LOG(("TGeshka Update Error: bad path in package '%1'."
				).arg(name));
			return false;
		} else if (name == kVersionFile) {
			packaged = QString::fromUtf8(content).trimmed();
		}
	}
	// The signature covers the package bytes only: the version inside
	// keeps an old signed package from being offered as a newer one.
	if (packaged != version) {
		LOG(("TGeshka Update Error: package says '%1', expected '%2'."
			).arg(packaged
			).arg(version));
		return false;
	}
	for (const auto &[name, content] : entries) {
		const auto path = TempDir() + name;
		QDir().mkpath(QFileInfo(path).absolutePath());
		auto file = QFile(path);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
			|| file.write(content) != content.size()) {
			return false;
		}
		files.push_back(name);
	}
	if (!files.contains(cExeName(), Qt::CaseInsensitive)) {
		LOG(("TGeshka Update Error: no '%1' in package.").arg(cExeName()));
		return false;
	}
	return true;
}

// Copies the files the update replaces, the rollback installs them back.
void SavePrevious(const QStringList &files) {
	const auto previous = UpdateDir() + kPreviousDir + '/';
	QDir(previous).removeRecursively();
	for (const auto &name : files) {
		const auto from = cExeDir() + name;
		if (!QFile::exists(from)) {
			continue;
		}
		const auto to = previous + name;
		QDir().mkpath(QFileInfo(to).absolutePath());
		QFile::copy(from, to);
	}
	auto version = QFile(previous + kVersionFile);
	if (version.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		version.write(CurrentVersion().toUtf8());
	}
}

// Same as the end of Core::checkReadyUpdate(): Updater.exe refuses a
// tree with itself, the new one replaces the current before it runs.
[[nodiscard]] bool PrepareUpdater() {
	const auto fromPath = TempDir() + u"Updater.exe"_q;
	const auto toPath = cExeDir() + u"Updater.exe"_q;
	if (!QFile::exists(fromPath)) {
		if (!QFile::exists(toPath)) {
			LOG(("TGeshka Update Error: no Updater.exe."));
			return false;
		}
		if (!QFile::copy(toPath, fromPath)) {
			return false;
		}
	}
#ifdef Q_OS_WIN
	const auto from = QDir::toNativeSeparators(fromPath).toStdWString();
	const auto to = QDir::toNativeSeparators(toPath).toStdWString();
	if (!CopyFile(from.c_str(), to.c_str(), FALSE)) {
		const auto error = GetLastError();
		if (error == ERROR_ACCESS_DENIED) {
			// Program Files: the copy in tupdates/temp runs elevated.
			cSetWriteProtected(true);
			return true;
		}
		LOG(("TGeshka Update Error: can't copy Updater.exe, %1."
			).arg(error));
		return false;
	}
	return DeleteFile(from.c_str()) != FALSE;
#else // Q_OS_WIN
	return false;
#endif // Q_OS_WIN
}

[[nodiscard]] bool MarkReady(const QString &version) {
	auto ready = QFile(TempDir() + u"ready"_q);
	if (!ready.open(QIODevice::WriteOnly)) {
		return false;
	}
	ready.close();
	auto &data = Data();
	data.installing = version;
	data.prepared = true;
	Save();
	return true;
}

// Unpacks the verified package into tupdates/temp.
[[nodiscard]] bool PrepareReady() {
	Load();
	auto &data = Data();
	if (data.readyVersion.isEmpty()
		|| CompareVersions(data.readyVersion, CurrentVersion()) <= 0) {
		return false;
	}
	auto file = QFile(UpdateDir() + kPackageFile);
	if (!file.open(QIODevice::ReadOnly)) {
		ClearReady();
		Save();
		return false;
	}
	const auto bytes = file.readAll();
	file.close();
	const auto sha256 = QCryptographicHash::hash(
		bytes,
		QCryptographicHash::Sha256).toHex();
	if (QString::fromLatin1(sha256) != data.readySha256) {
		LOG(("TGeshka Update Error: ready package changed on disk."));
		ClearReady();
		Save();
		return false;
	}
	QDir(UpdatesDir()).removeRecursively();
	auto files = QStringList();
	if (!UnpackPackage(bytes, data.readyVersion, files)) {
		QDir(UpdatesDir()).removeRecursively();
		ClearReady();
		Save();
		return false;
	}
	SavePrevious(files);
	if (!PrepareUpdater() || !MarkReady(data.readyVersion)) {
		QDir(UpdatesDir()).removeRecursively();
		return false;
	}
	LOG(("TGeshka Update: %1 unpacked, installing.").arg(data.readyVersion));
	return true;
}

// Once per launch: the result of the previous install.
void CheckInstalled() {
	auto &data = Data();
	if (data.installing.isEmpty()) {
		return;
	}
	if (data.installing == CurrentVersion()) {
		LOG(("TGeshka Update: %1 installed.").arg(data.installing));
		data.justInstalled = true;
		ClearReady();
	} else {
		// No retry on every launch: the next check downloads it again.
		LOG(("TGeshka Update Error: %1 was not installed, running %2."
			).arg(data.installing
			).arg(CurrentVersion()));
		ClearReady();
	}
	data.installing = QString();
	Save();
}

} // namespace

QString CurrentVersion() {
	return QString::fromLatin1(TGESHKA_VERSION);
}

// docs/update-v1.md: 1-4 numeric parts, an optional suffix.
bool IsValidVersion(const QString &version) {
	static const auto re = QRegularExpression(
		u"^\\d{1,6}(\\.\\d{1,6}){0,3}(-[0-9A-Za-z.]{1,16})?$"_q);
	return re.match(version).hasMatch();
}

// 1.0.0-beta < 1.0.0-beta.2 < 1.0.0 < 1.0.1-beta.
int CompareVersions(const QString &a, const QString &b) {
	struct Parsed {
		std::array<int, 4> numbers = { 0, 0, 0, 0 };
		QStringList suffix;
	};
	const auto parse = [](const QString &version) {
		auto result = Parsed();
		const auto dash = version.indexOf('-');
		const auto main = version.mid(0, dash).split('.');
		for (auto i = 0; i != 4 && i < main.size(); ++i) {
			result.numbers[i] = main[i].toInt();
		}
		if (dash >= 0) {
			result.suffix = version.mid(dash + 1).split('.');
		}
		return result;
	};
	const auto first = parse(a);
	const auto second = parse(b);
	if (first.numbers != second.numbers) {
		return (first.numbers < second.numbers) ? -1 : 1;
	} else if (first.suffix.isEmpty() || second.suffix.isEmpty()) {
		return int(first.suffix.isEmpty()) - int(second.suffix.isEmpty());
	}
	for (auto i = 0; i != std::max(first.suffix.size(), second.suffix.size()); ++i) {
		if (i >= first.suffix.size()) {
			return -1;
		} else if (i >= second.suffix.size()) {
			return 1;
		}
		auto okFirst = false;
		auto okSecond = false;
		const auto x = first.suffix[i].toInt(&okFirst);
		const auto y = second.suffix[i].toInt(&okSecond);
		const auto result = (okFirst && okSecond)
			? ((x < y) ? -1 : (y < x) ? 1 : 0)
			: first.suffix[i].compare(second.suffix[i]);
		if (result) {
			return (result < 0) ? -1 : 1;
		}
	}
	return 0;
}

Channel CurrentChannel() {
	Load();
	return Data().channel.current();
}

void SetChannel(Channel channel) {
	Load();
	auto &data = Data();
	if (data.channel.current() == channel) {
		return;
	}
	data.channel = channel;
	Save();
	CheckNow();
}

rpl::producer<Channel> ChannelValue() {
	Load();
	return Data().channel.value();
}

Status CurrentStatus() {
	return Data().status.current();
}

rpl::producer<Status> StatusValue() {
	return Data().status.value();
}

void Start() {
	Load();
	auto &data = Data();
	if (data.started) {
		return;
	}
	data.started = true;
	CheckInstalled();
	data.timer = std::make_unique<base::Timer>(Check);
	GatewayConfigValue(
	) | rpl::filter([](const GatewayConfig &config) {
		return !config.needsToken();
	}) | rpl::on_next([] {
		CheckNow();
	}, data.lifetime);
	Check();
}

bool JustInstalled() {
	return Data().justInstalled;
}

void CheckNow() {
	auto &data = Data();
	if (!data.started) {
		return;
	}
	const auto state = data.status.current().state;
	if (state == State::Downloading && data.reply) {
		return;
	}
	if (state != State::Ready) {
		UpdateStatus([](Status &status) {
			status.state = State::Idle;
		});
	}
	Check();
}

void InstallAndRestart() {
	if (!PrepareReady()) {
		Fail(u"can't prepare the update"_q);
		return;
	}
	cSetRestartingUpdate(true);
	Core::Quit();
}

QString PreviousVersion() {
	const auto previous = UpdateDir() + kPreviousDir + '/';
	auto file = QFile(previous + kVersionFile);
	if (!file.open(QIODevice::ReadOnly)) {
		return QString();
	}
	const auto version = QString::fromUtf8(file.read(64)).trimmed();
	return (IsValidVersion(version)
		&& version != CurrentVersion()
		&& QFile::exists(previous + cExeName()))
		? version
		: QString();
}

void RollbackAndRestart() {
	Load();
	const auto version = PreviousVersion();
	if (version.isEmpty()) {
		return;
	}
	auto &data = Data();
	if (data.reply) {
		data.reply->abort();
	}
	QDir(UpdatesDir()).removeRecursively();
	const auto from = UpdateDir() + kPreviousDir + '/';
	auto ok = true;
	auto it = QDirIterator(from, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const auto path = it.next();
		const auto name = path.mid(from.size());
		const auto to = TempDir() + name;
		QDir().mkpath(QFileInfo(to).absolutePath());
		ok = ok && QFile::copy(path, to);
	}
	if (!ok || !PrepareUpdater()) {
		QDir(UpdatesDir()).removeRecursively();
		Fail(u"can't prepare the rollback"_q);
		return;
	}
	data.skipVersion = CurrentVersion();
	ClearReady();
	if (!MarkReady(version)) {
		QDir(UpdatesDir()).removeRecursively();
		return;
	}
	LOG(("TGeshka Update: rolling back to %1.").arg(version));
	cSetRestartingUpdate(true);
	Core::Quit();
}

bool PrepareReadyAtStartup() {
	Load();
	if (cNoStartUpdate() || !Data().installing.isEmpty()) {
		return false;
	}
	return PrepareReady();
}

bool InstallPrepared() {
	return Data().prepared;
}

} // namespace Tgeshka::Update
