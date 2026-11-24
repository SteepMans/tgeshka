/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#pragma once

class PeerData;

namespace Settings::Builder {
class SectionBuilder;
} // namespace Settings::Builder

namespace Ui {
class RpWidget;
} // namespace Ui

namespace Tgeshka::Members {

// Who else uses TGeshka: the list of user ids from the gateway
// (GET /v1/members), cached in tdata/tgeshka_members.json, and
// "I use TGeshka" sent for every logged in account (POST /v1/members/me).
// Started once the windows exist, does nothing without a gateway key.
void Start();

// A user from the gateway list, or this account while the switch is on.
[[nodiscard]] bool IsMember(PeerData *peer);

// Fired on the main thread when the list or the switch changes.
[[nodiscard]] rpl::producer<> Updates();

// "Show that I use TGeshka", on by default, one for all accounts.
[[nodiscard]] bool ShowMine();
void SetShowMine(bool show);

// The lavender disc with the paper plane of the TGeshka logo, drawn
// inside a verified / premium icon box: same height, aligned to its right.
void PaintBadge(QPainter &p, QRect box);

// For the profile cover: a child of the given size, shown for members.
[[nodiscard]] not_null<Ui::RpWidget*> CreateProfileBadge(
	not_null<QWidget*> parent,
	not_null<PeerData*> peer,
	QSize size);

// Settings > TGeshka: the "Show that I use TGeshka" switch.
void AddSettings(Settings::Builder::SectionBuilder &builder);

} // namespace Tgeshka::Members
