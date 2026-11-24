/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/details/mtproto_abstract_socket.h"
#include "mtproto/mtproto_proxy_data.h"

#include <QtCore/QByteArray>
#include <QtNetwork/QSslSocket>

namespace MTP::details {

// One WebSocket (RFC 6455, client side) over TLS to the gateway,
// carrying the same bytes TcpSocket would send to a DC. Minimal framing
// without QtWebSockets: binary messages, masking, ping / pong, close.
class GatewaySocket final : public AbstractSocket {
public:
	GatewaySocket(
		not_null<QThread*> thread,
		const ProxyData &gateway);
	~GatewaySocket();

	void connectToHost(const QString &address, int port) override;
	bool isGoodStartNonce(bytes::const_span nonce) override;
	void timedOut() override;
	bool isConnected() override;
	bool hasBytesAvailable() override;
	int64 read(bytes::span buffer) override;
	void write(bytes::const_span prefix, bytes::const_span buffer) override;

	int32 debugState() override;
	QString debugPostfix() const override;

private:
	enum class State {
		NotConnected,
		Connecting,
		Upgrading,
		Connected,
		Closing,
		Disconnected,
		Error,
	};

	void socketEncrypted();
	void socketReadyRead();
	void socketDisconnected();
	void socketError(QAbstractSocket::SocketError error);
	void socketSslErrors(const QList<QSslError> &errors);

	void sendUpgradeRequest();
	bool readUpgradeResponse();
	bool readFrames();
	void sendFrame(uchar opcode, bytes::const_span payload);
	void sendFrame(
		uchar opcode,
		bytes::const_span prefix,
		bytes::const_span payload);
	void handleClose(bytes::const_span payload);

	void finishDisconnected();
	void finishError(int code, const QString &reason);

	const ProxyData _gateway;
	QSslSocket _socket;
	QString _target;
	QByteArray _key;
	QByteArray _raw;
	QByteArray _incoming;
	int _incomingOffset = 0;
	State _state = State::NotConnected;

};

} // namespace MTP::details
