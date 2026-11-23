/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).

The only place where the settings UI talks to the gateway transport
(mtproto/mtproto_gateway.h, core/core_gateway.h, docs/gateway.md).
*/
#pragma once

namespace Tgeshka {

struct GatewayConfig;

// Before the first MTProto connection: hands the stored settings
// to the transport and starts listening to its errors.
void StartTransport();

// Called after the user changed the address, the key or the switch:
// stores them in the transport and reconnects all sessions.
void ApplyToTransport(const GatewayConfig &config);

} // namespace Tgeshka
