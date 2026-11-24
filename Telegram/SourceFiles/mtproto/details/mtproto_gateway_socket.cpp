/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/details/mtproto_gateway_socket.h"

#include "mtproto/mtproto_gateway.h"
#include "base/invoke_queued.h"
#include "base/random.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QPointer>
#include <QtCore/QtEndian>

namespace MTP::details {
namespace {

constexpr auto kMaxResponseHead = 16 * 1024;
constexpr auto kMaxFramePayload = int64(16 * 1024 * 1024);
constexpr auto kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

constexpr auto kOpContinuation = uchar(0x0);
constexpr auto kOpText = uchar(0x1);
constexpr auto kOpBinary = uchar(0x2);
constexpr auto kOpClose = uchar(0x8);
constexpr auto kOpPing = uchar(0x9);
constexpr auto kOpPong = uchar(0xA);

constexpr auto kCloseNormal = 1000;
constexpr auto kCloseGoingAway = 1001;
constexpr auto kCloseProtocolError = 1002;
constexpr auto kCloseUnsupportedData = 1003;
constexpr auto kCloseNoStatus = 1005;
constexpr auto kCloseTooBig = 1009;

[[nodiscard]] bytes::const_span ToSpan(const QByteArray &data) {
	return bytes::make_span(data);
}

[[nodiscard]] QByteArray CloseFramePayload(int code) {
	auto result = QByteArray(2, Qt::Uninitialized);
	qToBigEndian(quint16(code), result.data());
	return result;
}

} // namespace

GatewaySocket::GatewaySocket(
	not_null<QThread*> thread,
	const ProxyData &gateway)
: AbstractSocket(thread)
, _gateway(gateway) {
	Expects(gateway.type == ProxyData::Type::Gateway);

	_socket.moveToThread(thread);
	const auto wrap = [&](auto handler) {
		return [=](auto &&...args) {
			InvokeQueued(this, [=] { handler(args...); });
		};
	};
	using Error = QAbstractSocket::SocketError;
	connect(
		&_socket,
		&QSslSocket::encrypted,
		wrap([=] { socketEncrypted(); }));
	connect(
		&_socket,
		&QSslSocket::readyRead,
		wrap([=] { socketReadyRead(); }));
	connect(
		&_socket,
		&QSslSocket::disconnected,
		wrap([=] { socketDisconnected(); }));
	connect(
		&_socket,
		&QAbstractSocket::errorOccurred,
		wrap([=](Error e) { socketError(e); }));
	connect(
		&_socket,
		qOverload<const QList<QSslError>&>(&QSslSocket::sslErrors),
		wrap([=](const QList<QSslError> &errors) {
			socketSslErrors(errors);
		}));
}

GatewaySocket::~GatewaySocket() {
	QObject::disconnect(&_socket, nullptr, nullptr, nullptr);
	if (_state == State::Connected) {
		const auto payload = CloseFramePayload(kCloseNormal);
		sendFrame(kOpClose, ToSpan(payload));
		_socket.flush();
	}
	_socket.abort();
}

void GatewaySocket::connectToHost(const QString &address, int port) {
	Expects(_state == State::NotConnected);

	// The protocol accepts only IPv4 addresses as the DC hint,
	// for others the gateway picks the address by the DC id.
	_target = (!address.isEmpty() && !address.contains(':'))
		? (address + ':' + QString::number(port))
		: QString();
	if (_gateway.host.isEmpty() || !_gateway.port) {
		DEBUG_LOG(("Socket %1 Error: Gateway url is invalid.").arg(_debugId));
		_state = State::Error;
		NotifyGatewayError(kGatewayNoToken, u"no url"_q);
		InvokeQueued(this, [=] { _error.fire({}); });
		return;
	} else if (_gateway.password.isEmpty()) {
		DEBUG_LOG(("Socket %1 Error: Gateway token is empty.").arg(_debugId));
		_state = State::Error;
		NotifyGatewayError(kGatewayNoToken, u"no token"_q);
		InvokeQueued(this, [=] { _error.fire({}); });
		return;
	}
	_state = State::Connecting;
	_socket.setPeerVerifyName(_gateway.host);
	_socket.setProtocol(QSsl::TlsV1_2OrLater);
	_socket.connectToHostEncrypted(_gateway.host, quint16(_gateway.port));
}

bool GatewaySocket::isGoodStartNonce(bytes::const_span nonce) {
	Expects(nonce.size() >= 2 * sizeof(uint32));

	const auto bytes = nonce.data();
	const auto zero = gsl::to_integer<uchar>(nonce.front());
	const auto first = qFromUnaligned<uint32>(bytes);
	const auto second = qFromUnaligned<uint32>(bytes + sizeof(uint32));
	return (zero != 0xEFU)
		&& (first != 0x44414548U)
		&& (first != 0x54534F50U)
		&& (first != 0x20544547U)
		&& (first != 0xEEEEEEEEU)
		&& (first != 0xDDDDDDDDU)
		&& (first != 0x02010316U)
		&& (second != 0x00000000U);
}

void GatewaySocket::timedOut() {
}

bool GatewaySocket::isConnected() {
	return (_state == State::Connected);
}

bool GatewaySocket::hasBytesAvailable() {
	return (_incomingOffset < _incoming.size());
}

int64 GatewaySocket::read(bytes::span buffer) {
	const auto available = _incoming.size() - _incomingOffset;
	if (available <= 0 || buffer.empty()) {
		return 0;
	}
	const auto count = int(std::min<int64>(available, buffer.size()));
	bytes::copy(buffer, ToSpan(_incoming).subspan(_incomingOffset, count));
	_incomingOffset += count;
	if (_incomingOffset == _incoming.size()) {
		_incoming.clear();
		_incomingOffset = 0;
	} else if (_incomingOffset >= 64 * 1024
		&& _incomingOffset >= _incoming.size() / 2) {
		_incoming.remove(0, _incomingOffset);
		_incomingOffset = 0;
	}
	return count;
}

void GatewaySocket::write(bytes::const_span prefix, bytes::const_span buffer) {
	Expects(!buffer.empty());

	if (_state != State::Connected) {
		DEBUG_LOG(("Socket %1 Error: Gateway write while not connected."
			).arg(_debugId));
		return;
	}
	// Each write is one binary message, the gateway treats the
	// messages as a continuous byte stream.
	sendFrame(kOpBinary, prefix, buffer);
}

int32 GatewaySocket::debugState() {
	return int32(_state);
}

QString GatewaySocket::debugPostfix() const {
	return u"_gw"_q;
}

void GatewaySocket::socketEncrypted() {
	if (_state != State::Connecting) {
		return;
	}
	_state = State::Upgrading;
	sendUpgradeRequest();
}

void GatewaySocket::sendUpgradeRequest() {
	auto key = bytes::vector(16);
	bytes::set_random(key);
	_key = QByteArray(
		reinterpret_cast<const char*>(key.data()),
		key.size()).toBase64();

	auto path = _gateway.user.isEmpty() ? u"/"_q : _gateway.user;
	if (!_target.isEmpty()) {
		path += u"?a="_q + _target;
	}
	const auto host = (_gateway.port == 443)
		? _gateway.host
		: (_gateway.host + ':' + QString::number(_gateway.port));
	auto request = QByteArray();
	request.reserve(512);
	request.append("GET ").append(path.toUtf8()).append(" HTTP/1.1\r\n");
	request.append("Host: ").append(host.toUtf8()).append("\r\n");
	request.append("Upgrade: websocket\r\n");
	request.append("Connection: Upgrade\r\n");
	request.append("Sec-WebSocket-Version: 13\r\n");
	request.append("Sec-WebSocket-Key: ").append(_key).append("\r\n");
	request.append("Authorization: Bearer ");
	request.append(_gateway.password.toUtf8()).append("\r\n");
	if (const auto client = GatewayClientId(); !client.isEmpty()) {
		request.append("X-TGeshka-Client: ");
		request.append(client.toUtf8()).append("\r\n");
	}
	request.append("\r\n");
	_socket.write(request);
}

bool GatewaySocket::readUpgradeResponse() {
	const auto end = _raw.indexOf("\r\n\r\n");
	if (end < 0) {
		if (_raw.size() > kMaxResponseHead) {
			finishError(kGatewayRejected, u"bad response"_q);
		}
		return false;
	}
	const auto head = _raw.left(end);
	_raw.remove(0, end + 4);

	const auto lines = head.split('\n');
	const auto status = lines.front().trimmed().split(' ');
	const auto code = (status.size() > 1) ? status[1].toInt() : 0;
	if (code != 101) {
		LOG(("Gateway Error: upgrade refused with HTTP %1.").arg(code));
		finishError(kGatewayRejected, u"http %1"_q.arg(code));
		return false;
	}
	auto accept = QByteArray();
	for (const auto &line : lines) {
		const auto colon = line.indexOf(':');
		if (colon > 0
			&& line.left(colon).trimmed().toLower()
				== "sec-websocket-accept") {
			accept = line.mid(colon + 1).trimmed();
		}
	}
	const auto expected = QCryptographicHash::hash(
		_key + kWebSocketGuid,
		QCryptographicHash::Sha1).toBase64();
	if (accept != expected) {
		LOG(("Gateway Error: bad Sec-WebSocket-Accept."));
		finishError(kGatewayRejected, u"bad accept"_q);
		return false;
	}
	_state = State::Connected;
	return true;
}

void GatewaySocket::socketReadyRead() {
	if (_state != State::Upgrading
		&& _state != State::Connected
		&& _state != State::Closing) {
		_socket.readAll();
		return;
	}
	const auto received = _socket.readAll();
	CountGatewayTraffic(0, received.size());
	_raw.append(received);

	const auto guard = QPointer<QObject>(static_cast<QObject*>(this));
	if (_state == State::Upgrading) {
		if (!readUpgradeResponse()) {
			return;
		}
		DEBUG_LOG(("Socket %1 Info: Gateway WebSocket opened."
			).arg(_debugId));
		NotifyGatewayOpened();
		_connected.fire({});
		if (!guard || _state != State::Connected) {
			return;
		}
	}
	if (readFrames() && guard && _state == State::Connected) {
		_readyRead.fire({});
	}
}

bool GatewaySocket::readFrames() {
	auto received = false;
	auto offset = 0;
	const auto data = reinterpret_cast<const uchar*>(_raw.constData());
	while (_raw.size() - offset >= 2) {
		const auto first = data[offset];
		const auto second = data[offset + 1];
		const auto opcode = uchar(first & 0x0F);
		const auto masked = (second & 0x80) != 0;
		auto length = int64(second & 0x7F);
		auto header = 2;
		if (length == 126) {
			if (_raw.size() - offset < 4) {
				break;
			}
			length = qFromBigEndian<quint16>(data + offset + 2);
			header = 4;
		} else if (length == 127) {
			if (_raw.size() - offset < 10) {
				break;
			}
			length = int64(qFromBigEndian<quint64>(data + offset + 2));
			header = 10;
		}
		if (length < 0 || length > kMaxFramePayload) {
			const auto payload = CloseFramePayload(kCloseTooBig);
			sendFrame(kOpClose, ToSpan(payload));
			finishError(kCloseTooBig, u"frame too big"_q);
			return false;
		}
		const auto maskOffset = header;
		if (masked) {
			header += 4;
		}
		if (_raw.size() - offset < header + length) {
			break;
		}
		auto payload = _raw.mid(offset + header, int(length));
		if (masked) {
			const auto mask = data + offset + maskOffset;
			for (auto i = 0; i != payload.size(); ++i) {
				payload[i] = char(uchar(payload[i]) ^ mask[i % 4]);
			}
		}
		offset += header + int(length);

		switch (opcode) {
		case kOpContinuation:
		case kOpBinary:
			if (!payload.isEmpty()) {
				_incoming.append(payload);
				received = true;
			}
			break;
		case kOpText: {
			const auto close = CloseFramePayload(kCloseUnsupportedData);
			sendFrame(kOpClose, ToSpan(close));
			finishError(kCloseUnsupportedData, u"text frame"_q);
		} return false;
		case kOpClose:
			_raw.remove(0, offset);
			handleClose(ToSpan(payload));
			return false;
		case kOpPing:
			sendFrame(kOpPong, ToSpan(payload));
			break;
		case kOpPong:
			break;
		default: {
			const auto close = CloseFramePayload(kCloseProtocolError);
			sendFrame(kOpClose, ToSpan(close));
			finishError(kCloseProtocolError, u"bad opcode"_q);
		} return false;
		}
	}
	_raw.remove(0, offset);
	return received;
}

void GatewaySocket::handleClose(bytes::const_span payload) {
	const auto code = (payload.size() >= 2)
		? int(qFromBigEndian<quint16>(payload.data()))
		: kCloseNoStatus;
	const auto reason = (payload.size() > 2)
		? QString::fromUtf8(
			reinterpret_cast<const char*>(payload.data() + 2),
			payload.size() - 2)
		: QString();
	DEBUG_LOG(("Socket %1 Info: Gateway closed with %2 (%3)."
		).arg(_debugId
		).arg(code
		).arg(reason));
	if (_state == State::Connected) {
		const auto echo = CloseFramePayload(
			(code == kCloseNoStatus) ? kCloseNormal : code);
		sendFrame(kOpClose, ToSpan(echo));
		_socket.flush();
	}
	_state = State::Closing;
	_socket.disconnectFromHost();
	if (code == kCloseNormal || code == kCloseGoingAway) {
		finishDisconnected();
	} else {
		finishError(code, reason);
	}
}

void GatewaySocket::sendFrame(uchar opcode, bytes::const_span payload) {
	sendFrame(opcode, {}, payload);
}

void GatewaySocket::sendFrame(
		uchar opcode,
		bytes::const_span prefix,
		bytes::const_span payload) {
	const auto length = int64(prefix.size() + payload.size());
	const auto lengthBytes = (length < 126) ? 0 : (length < 65536) ? 2 : 8;
	const auto header = 2 + lengthBytes + 4;
	auto frame = QByteArray(int(header + length), Qt::Uninitialized);
	auto out = reinterpret_cast<uchar*>(frame.data());
	out[0] = uchar(0x80 | opcode);
	if (lengthBytes == 0) {
		out[1] = uchar(0x80 | length);
	} else if (lengthBytes == 2) {
		out[1] = uchar(0x80 | 126);
		qToBigEndian(quint16(length), out + 2);
	} else {
		out[1] = uchar(0x80 | 127);
		qToBigEndian(quint64(length), out + 2);
	}
	const auto mask = out + 2 + lengthBytes;
	qToUnaligned(base::RandomValue<uint32>(), mask);
	auto body = out + header;
	if (!prefix.empty()) {
		memcpy(body, prefix.data(), prefix.size());
	}
	if (!payload.empty()) {
		memcpy(body + prefix.size(), payload.data(), payload.size());
	}
	for (auto i = int64(0); i != length; ++i) {
		body[i] ^= mask[i & 3];
	}
	CountGatewayTraffic(frame.size(), 0);
	_socket.write(frame);
}

void GatewaySocket::socketDisconnected() {
	if (_state == State::Connected || _state == State::Upgrading) {
		DEBUG_LOG(("Socket %1 Info: Gateway TLS connection closed."
			).arg(_debugId));
	}
	finishDisconnected();
}

void GatewaySocket::socketError(QAbstractSocket::SocketError error) {
	if (_state == State::Error || _state == State::Disconnected) {
		return;
	}
	if (_state == State::Closing
		&& error == QAbstractSocket::RemoteHostClosedError) {
		finishDisconnected();
		return;
	}
	logError(error, _socket.errorString());
	finishError(0, QString());
}

void GatewaySocket::socketSslErrors(const QList<QSslError> &errors) {
	for (const auto &error : errors) {
		LOG(("Gateway Error: TLS - %1").arg(error.errorString()));
	}
	NotifyGatewayError(kGatewayTlsError, u"tls"_q);
	// Not ignored: the handshake fails and errorOccurred() follows.
}

void GatewaySocket::finishDisconnected() {
	if (_state == State::Disconnected || _state == State::Error) {
		return;
	}
	_state = State::Disconnected;
	_disconnected.fire({});
}

void GatewaySocket::finishError(int code, const QString &reason) {
	if (_state == State::Disconnected || _state == State::Error) {
		return;
	}
	_state = State::Error;
	if (code) {
		NotifyGatewayError(code, reason);
	}
	_socket.abort();
	_error.fire({});
}

} // namespace MTP::details
