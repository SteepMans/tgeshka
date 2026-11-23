/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_gateway.h"

#include "tgeshka/tgeshka_brand.h"
#include "tgeshka/tgeshka_lang.h"
#include "mtproto/mtproto_gateway.h"
#include "tgeshka/tgeshka_transport_bridge.h"
#include "base/random.h"
#include "storage/details/storage_file_utilities.h"
#include "storage/serialize_common.h"
#include "config.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QRegularExpression>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>

namespace Tgeshka {
namespace {

using namespace Storage::details;

constexpr auto kFileName = "gateway";
constexpr auto kFileVersion = quint32(1);
constexpr auto kCheckTimeout = 15000;

struct State {
	std::optional<GatewayConfig> config;
	rpl::event_stream<GatewayConfig> changes;
	rpl::variable<GatewayStatus> status = GatewayStatus::Unknown;
	rpl::variable<GatewayLastError> lastError;
	std::unique_ptr<QNetworkAccessManager> network;
};

[[nodiscard]] State &Data() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString BasePath() {
	return cWorkingDir() + u"tdata/"_q;
}

[[nodiscard]] GatewayConfig DefaultConfig() {
	return { .url = DefaultGatewayUrl() };
}

[[nodiscard]] GatewayConfig Read() {
	auto result = DefaultConfig();
	auto file = FileReadDescriptor();
	if (!ReadFile(file, QString::fromLatin1(kFileName), BasePath())) {
		return result;
	}
	auto salt = QByteArray();
	auto encrypted = QByteArray();
	file.stream >> salt >> encrypted;
	if (!CheckStreamStatus(file.stream)
		|| salt.size() != LocalEncryptSaltSize) {
		LOG(("TGeshka Error: bad gateway settings file."));
		return result;
	}
	const auto key = CreateLegacyLocalKey(QByteArray(), salt);
	auto data = EncryptedDescriptor();
	if (!DecryptLocal(data, encrypted, key)) {
		LOG(("TGeshka Error: could not decrypt gateway settings."));
		return result;
	}
	auto version = quint32();
	auto reserved = qint32();
	auto url = QString();
	auto token = QString();
	data.stream >> version >> reserved >> url >> token;
	if (!CheckStreamStatus(data.stream) || version != kFileVersion) {
		LOG(("TGeshka Error: bad gateway settings data."));
		return result;
	}
	if (IsValidGatewayUrl(url)) {
		result.url = url;
	}
	if (IsValidToken(token)) {
		result.token = token;
	}
	return result;
}

void Write(const GatewayConfig &config) {
	const auto base = BasePath();
	if (!QDir().exists(base)) {
		QDir().mkpath(base);
	}
	auto salt = QByteArray(LocalEncryptSaltSize, Qt::Uninitialized);
	base::RandomFill(salt.data(), salt.size());
	const auto key = CreateLegacyLocalKey(QByteArray(), salt);

	const auto size = sizeof(quint32)
		+ sizeof(qint32)
		+ Serialize::stringSize(config.url)
		+ Serialize::stringSize(config.token);
	auto data = EncryptedDescriptor(size);
	data.stream
		<< kFileVersion
		<< qint32(1) // Reserved, was the direct connection switch.
		<< config.url
		<< config.token;

	auto file = FileWriteDescriptor(QString::fromLatin1(kFileName), base);
	file.writeData(salt);
	file.writeEncrypted(data, key);
}

[[nodiscard]] QUrl InfoUrl(const QString &gatewayUrl) {
	auto result = QUrl(gatewayUrl);
	result.setScheme(u"https"_q);
	auto path = result.path();
	if (path.endsWith(u"/stream"_q)) {
		path.chop(6);
		path += u"info"_q;
	} else {
		path = u"/v1/info"_q;
	}
	result.setPath(path);
	result.setQuery(QString());
	return result;
}

} // namespace

QString DefaultGatewayUrl() {
	return QString::fromLatin1(TGESHKA_DEFAULT_GATEWAY_URL);
}

const GatewayConfig &CurrentGatewayConfig() {
	auto &data = Data();
	if (!data.config) {
		data.config = Read();
	}
	return *data.config;
}

void SetGatewayConfig(GatewayConfig config) {
	if (!IsValidGatewayUrl(config.url)) {
		config.url = DefaultGatewayUrl();
	}
	if (!IsValidToken(config.token)) {
		config.token = QString();
	}
	if (CurrentGatewayConfig() == config) {
		return;
	}
	auto &data = Data();
	data.config = config;
	Write(config);
	data.status = GatewayStatus::Unknown;
	ApplyToTransport(config);
	data.changes.fire_copy(config);
}

rpl::producer<GatewayConfig> GatewayConfigValue() {
	return rpl::single(
		CurrentGatewayConfig()
	) | rpl::then(Data().changes.events());
}

void SetGatewayStatus(GatewayStatus status) {
	Data().status = status;
}

rpl::producer<GatewayStatus> GatewayStatusValue() {
	return Data().status.value();
}

void SetGatewayLastError(int code, const QString &reason) {
	Data().lastError = GatewayLastError{
		.code = code,
		.reason = reason,
		.when = QDateTime::currentDateTime(),
	};
}

rpl::producer<GatewayLastError> GatewayLastErrorValue() {
	return Data().lastError.value();
}

QString GatewayErrorText(int code, const QString &reason) {
	switch (code) {
	case MTP::kGatewayNoToken:
		return Tr(
			u"No gateway address or access key"_q,
			u"Не задан адрес шлюза или ключ доступа"_q);
	case MTP::kGatewayRejected:
		return Tr(
			u"The gateway refused the connection (%1): check the key"_q,
			u"Шлюз отказал в соединении (%1): проверьте ключ"_q
		).arg(reason);
	case MTP::kGatewayTlsError:
		return Tr(
			u"The gateway TLS certificate is not trusted"_q,
			u"Сертификат TLS шлюза не прошёл проверку"_q);
	case 1011:
		return Tr(
			u"Internal gateway error, reconnecting"_q,
			u"Внутренняя ошибка шлюза, переподключение"_q);
	case 4400:
		return Tr(
			u"The gateway could not read the connection start"_q,
			u"Шлюз не разобрал начало соединения"_q);
	case 4401:
		return Tr(
			u"The access key was revoked"_q,
			u"Ключ доступа отозван"_q);
	case 4408:
		return Tr(
			u"The connection was idle too long, reconnecting"_q,
			u"Соединение простаивало слишком долго, переподключение"_q);
	case 4429:
		return Tr(
			u"Too many connections for this key"_q,
			u"Превышен лимит соединений ключа"_q);
	case 4502:
		return Tr(
			u"The gateway cannot reach Telegram now"_q,
			u"Шлюз сейчас не может связаться с Telegram"_q);
	case 4504:
		return Tr(
			u"A Telegram server did not answer the gateway"_q,
			u"Сервер Telegram не ответил шлюзу"_q);
	}
	return Tr(
		u"Connection to the gateway was lost (%1)"_q,
		u"Соединение со шлюзом прервано (%1)"_q
	).arg(code ? QString::number(code) : reason);
}

bool IsValidToken(const QString &token) {
	static const auto re = QRegularExpression(
		u"^tc_[0-9A-Za-z]{43}$"_q);
	return re.match(token).hasMatch();
}

bool IsValidGatewayUrl(const QString &url) {
	const auto parsed = QUrl(url);
	return parsed.isValid()
		&& (parsed.scheme() == u"wss"_q)
		&& !parsed.host().isEmpty();
}

bool IsImportLink(const QUrl &url) {
	return (url.scheme().compare(
		QString::fromLatin1(TGESHKA_URL_SCHEME),
		Qt::CaseInsensitive) == 0);
}

bool ParseAccessKey(const QString &text, GatewayConfig &result) {
	const auto trimmed = text.trimmed();
	if (IsValidToken(trimmed)) {
		result.token = trimmed;
		return true;
	}
	const auto url = QUrl(trimmed);
	if (!IsImportLink(url) || url.host() != u"gateway"_q) {
		return false;
	}
	const auto query = QUrlQuery(url);
	const auto token = query.queryItemValue(
		u"token"_q,
		QUrl::FullyDecoded).trimmed();
	const auto gateway = query.queryItemValue(
		u"url"_q,
		QUrl::FullyDecoded).trimmed();
	if (!IsValidToken(token)
		|| (!gateway.isEmpty() && !IsValidGatewayUrl(gateway))) {
		return false;
	}
	result.token = token;
	if (!gateway.isEmpty()) {
		result.url = gateway;
	}
	return true;
}

void CheckAccessKey(
		const QString &url,
		const QString &token,
		Fn<void(KeyCheckResult)> done) {
	auto &data = Data();
	if (!data.network) {
		data.network = std::make_unique<QNetworkAccessManager>();
	}
	if (!IsValidGatewayUrl(url)) {
		crl::on_main([=] { done(KeyCheckResult()); });
		return;
	}
	auto request = QNetworkRequest(InfoUrl(url));
	request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
	request.setTransferTimeout(kCheckTimeout);
	const auto started = crl::now();
	const auto reply = data.network->get(request);
	QObject::connect(reply, &QNetworkReply::finished, [=] {
		reply->deleteLater();
		auto result = KeyCheckResult();
		result.latency = crl::now() - started;
		const auto code = reply->attribute(
			QNetworkRequest::HttpStatusCodeAttribute).toInt();
		if (code == 200) {
			const auto json = QJsonDocument::fromJson(reply->readAll());
			const auto object = json.object();
			result.type = KeyCheckResult::Type::Ok;
			result.activeConnections = object.value(
				u"active_connections"_q).toInt();
			result.maxConnections = object.value(
				u"max_connections"_q).toInt();
			result.description = object.value(
				u"description"_q).toString();
			result.status = object.value(u"status"_q).toString();
		} else if (code == 404) {
			result.type = KeyCheckResult::Type::Rejected;
		} else {
			result.type = KeyCheckResult::Type::Unreachable;
		}
		done(result);
	});
}

} // namespace Tgeshka
