/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).

Own updates (docs/updates.md): the manifest and the package come
from the gateway (SteepMans/api-gateway docs/update-v1.md) with the
same access key as the connection. The package is the portable zip
of a GitHub release, checked by size, sha256 and the Ed25519 signature
of the release CI, and installed by the stock Updater.exe. Nothing
here talks to Telegram servers.
*/
#pragma once

namespace Tgeshka::Update {

enum class Channel {
	Stable,
	Beta,
};

enum class State {
	Idle,
	Checking,
	Latest,
	Downloading,
	Ready, // Verified package is on disk, restart installs it.
	Failed,
};

struct Status {
	State state = State::Idle;
	QString version; // Offered or ready version.
	qint64 received = 0;
	qint64 total = 0;
	bool required = false; // Running version is below min_version.

	friend inline bool operator==(const Status &, const Status &) = default;
};

// TGESHKA_VERSION of this build: "0.1.2" or "0.1.2-beta".
[[nodiscard]] QString CurrentVersion();

[[nodiscard]] Channel CurrentChannel();
void SetChannel(Channel channel);
[[nodiscard]] rpl::producer<Channel> ChannelValue();

[[nodiscard]] Status CurrentStatus();
[[nodiscard]] rpl::producer<Status> StatusValue();

// After the application is launched: checks now and every 6 hours.
void Start();
// This launch is the first one after an installed update.
[[nodiscard]] bool JustInstalled();
void CheckNow();

// Unpacks the ready package for Updater.exe and quits the application,
// the launcher starts Updater.exe after the main loop.
void InstallAndRestart();

// The version replaced by the last installed update, kept for rollback.
[[nodiscard]] QString PreviousVersion();
void RollbackAndRestart();

// Launcher hooks.
// Sandbox start: a verified package left from the previous run is
// unpacked and true is returned, the application quits and updates.
[[nodiscard]] bool PrepareReadyAtStartup();
// After the main loop: tupdates/temp is prepared by this module.
[[nodiscard]] bool InstallPrepared();
// The install folder is write-protected (Program Files): the launcher
// starts tupdates/temp/Updater.exe elevated.
[[nodiscard]] bool InstallNeedsElevation();

// Version helpers, exposed for the unit test.
[[nodiscard]] int CompareVersions(const QString &a, const QString &b);
[[nodiscard]] bool IsValidVersion(const QString &version);

} // namespace Tgeshka::Update
