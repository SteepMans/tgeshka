/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/mtproto_gateway.h"

namespace Core {

// Main thread. Stores the gateway settings and, if they changed,
// restarts the MTProto sessions of all accounts to reconnect with them.
void ApplyGateway(const MTP::GatewaySettings &settings);

} // namespace Core
