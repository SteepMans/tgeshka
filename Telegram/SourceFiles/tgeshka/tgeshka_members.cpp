/*
This file is part of TGeshka, an unofficial client
based on Telegram Desktop (GPLv3).
*/
#include "tgeshka/tgeshka_members.h"

#include "tgeshka/tgeshka_gateway.h"
#include "tgeshka/tgeshka_lang.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "base/timer.h"
#include "core/application.h"
#include "data/data_peer.h"
#include "logs.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "settings.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "ui/painter.h"
#include "ui/rp_widget.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "window/themes/window_theme.h"
#include "styles/style_settings.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtGui/QPainterPath>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

namespace Tgeshka::Members {
namespace {

using namespace Settings::Builder;

constexpr auto kRefreshInterval = 60 * 60 * crl::time(1000);
constexpr auto kPostRepeatDelay = 60 * crl::time(1000);
constexpr auto kReloadDelay = 2 * crl::time(1000);
constexpr auto kMaxCacheSize = 4 * 1024 * 1024;
constexpr auto kCacheFile = "tgeshka_members.json";
constexpr auto kHiddenFile = "tgeshka_member_hidden";

struct Posted {
	crl::time when = 0;
	bool visible = false;
};

struct State {
	bool loaded = false;
	bool started = false;
	bool showMine = true;
	base::flat_set<BareId> ids;
	QString version;
	bool loading = false;
	bool loadAgain = false;
	base::flat_map<BareId, Posted> posted;
	base::flat_map<not_null<Main::Account*>, rpl::lifetime> watched;
	rpl::event_stream<> updates;
	std::unique_ptr<base::Timer> refreshTimer;
	std::unique_ptr<base::Timer> reloadTimer;
	rpl::lifetime lifetime;
};

[[nodiscard]] State &Data() {
	static auto result = State();
	return result;
}

[[nodiscard]] QString DataPath(const char *name) {
	return cWorkingDir() + u"tdata/"_q + QString::fromLatin1(name);
}

void RepaintAll() {
	for (const auto widget : QApplication::topLevelWidgets()) {
		widget->update();
		for (const auto child : widget->findChildren<QWidget*>()) {
			child->update();
		}
	}
}

void Changed() {
	Data().updates.fire({});
	RepaintAll();
}

// The gateway answer: {"version": "<hex>", "user_ids": [...]}.
// Returns false for a bad answer, sets changed if the list is new.
bool Apply(const QByteArray &body, bool &changed) {
	changed = false;
	auto error = QJsonParseError();
	const auto document = QJsonDocument::fromJson(body, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		return false;
	}
	const auto object = document.object();
	const auto list = object.value(u"user_ids"_q);
	if (!list.isArray()) {
		return false;
	}
	auto ids = base::flat_set<BareId>();
	for (const auto &value : list.toArray()) {
		const auto id = value.toDouble();
		if (id >= 1.) {
			ids.emplace(BareId(id));
		}
	}
	auto &data = Data();
	data.version = object.value(u"version"_q).toString();
	if (data.ids != ids) {
		data.ids = std::move(ids);
		changed = true;
	}
	return true;
}

void EnsureLoaded() {
	auto &data = Data();
	if (data.loaded) {
		return;
	}
	data.loaded = true;
	data.showMine = !QFile::exists(DataPath(kHiddenFile));
	auto file = QFile(DataPath(kCacheFile));
	if (file.open(QIODevice::ReadOnly)) {
		auto changed = false;
		Apply(file.read(kMaxCacheSize), changed);
	}
}

void SaveCache(const QByteArray &body) {
	const auto path = DataPath(kCacheFile);
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QFile(path);
	if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		file.write(body);
	}
}

void LoadList() {
	auto &data = Data();
	if (data.loading) {
		data.loadAgain = true;
		return;
	}
	auto etag = QByteArray();
	if (!data.version.isEmpty()) {
		etag.append('"').append(data.version.toUtf8()).append('"');
	}
	data.loading = GatewayApiRequest(
		"GET",
		u"/v1/members"_q,
		QByteArray(),
		etag,
		[](GatewayReply reply) {
			auto &state = Data();
			state.loading = false;
			auto changed = false;
			if (reply.code == 200) {
				if (Apply(reply.body, changed)) {
					SaveCache(reply.body);
				} else {
					LOG(("TGeshka Members: bad list answer."));
				}
				if (changed) {
					Changed();
				}
			} else if (reply.code != 304) {
				// Network error or the gateway is down: the cache stays.
				LOG(("TGeshka Members: list request failed, code %1."
					).arg(reply.code));
			}
			if (base::take(state.loadAgain)) {
				LoadList();
			}
		});
}

void ReloadSoon() {
	if (const auto timer = Data().reloadTimer.get()) {
		timer->callOnce(kReloadDelay);
	} else {
		LoadList();
	}
}

void PostMine(BareId userId, bool force) {
	if (!userId) {
		return;
	}
	auto &data = Data();
	const auto visible = data.showMine;
	auto &posted = data.posted[userId];
	if (!force
		&& posted.when
		&& posted.visible == visible
		&& crl::now() - posted.when < kPostRepeatDelay) {
		return;
	}
	auto object = QJsonObject();
	object.insert(u"user_id"_q, QJsonValue(qint64(userId)));
	object.insert(u"visible"_q, visible);
	const auto sent = GatewayApiRequest(
		"POST",
		u"/v1/members/me"_q,
		QJsonDocument(object).toJson(QJsonDocument::Compact),
		QByteArray(),
		[=](GatewayReply reply) {
			auto &entry = Data().posted[userId];
			if (reply.code == 200) {
				entry = { .when = crl::now(), .visible = visible };
				ReloadSoon();
			} else if (reply.code == 429) {
				// More accounts than the key allows: not repeated this hour.
				entry = { .when = crl::now(), .visible = visible };
				LOG(("TGeshka Members: account limit of the key."));
			} else {
				entry = Posted();
				LOG(("TGeshka Members: mark request failed, code %1."
					).arg(reply.code));
			}
		});
	if (sent) {
		posted = { .when = crl::now(), .visible = visible };
	}
}

void PostAll(bool force) {
	auto &domain = Core::App().domain();
	if (!domain.started()) {
		return;
	}
	for (const auto &entry : domain.accounts()) {
		const auto account = entry.account.get();
		if (account && account->sessionExists()) {
			PostMine(account->session().userId().bare, force);
		}
	}
}

void WatchAccounts() {
	auto &domain = Core::App().domain();
	if (!domain.started()) {
		return;
	}
	auto &data = Data();
	auto alive = base::flat_set<not_null<Main::Account*>>();
	for (const auto &entry : domain.accounts()) {
		const auto raw = entry.account.get();
		if (!raw) {
			continue;
		}
		const auto account = not_null<Main::Account*>(raw);
		alive.emplace(account);
		if (data.watched.contains(account)) {
			continue;
		}
		account->sessionValue(
		) | rpl::on_next([](Main::Session *session) {
			if (session) {
				PostMine(session->userId().bare, false);
			}
		}, data.watched[account]);
	}
	for (auto i = data.watched.begin(); i != data.watched.end();) {
		if (alive.contains(i->first)) {
			++i;
		} else {
			i = data.watched.erase(i);
		}
	}
}

[[nodiscard]] QColor BadgeColor() {
	// The lavender accent of the TGeshka theme, see ApplyDefaultAccent().
	return Window::Theme::IsNightMode()
		? QColor(0x9a, 0x86, 0xe8)
		: QColor(0x84, 0x70, 0xde);
}

} // namespace

void Start() {
	auto &data = Data();
	if (data.started) {
		return;
	}
	data.started = true;
	EnsureLoaded();

	data.reloadTimer = std::make_unique<base::Timer>([] { LoadList(); });
	data.refreshTimer = std::make_unique<base::Timer>([] {
		PostAll(true);
		LoadList();
	});
	data.refreshTimer->callEach(kRefreshInterval);

	GatewayConfigValue(
	) | rpl::on_next([](const GatewayConfig &config) {
		if (config.needsToken()) {
			return;
		}
		// A new key or gateway: everything is sent again.
		Data().posted.clear();
		PostAll(false);
		LoadList();
	}, data.lifetime);

	// Accounts that log in later are marked as soon as they have a session.
	auto &domain = Core::App().domain();
	rpl::merge(
		domain.activeValue() | rpl::to_empty,
		domain.accountsChanges()
	) | rpl::on_next([] {
		WatchAccounts();
	}, data.lifetime);
}

bool IsMember(PeerData *peer) {
	if (!peer || !peer->isUser()) {
		return false;
	}
	EnsureLoaded();
	const auto &data = Data();
	if (peer->isSelf()) {
		return data.showMine && !CurrentGatewayConfig().needsToken();
	}
	return data.ids.contains(peerToUser(peer->id).bare);
}

rpl::producer<> Updates() {
	return Data().updates.events();
}

bool ShowMine() {
	EnsureLoaded();
	return Data().showMine;
}

void SetShowMine(bool show) {
	EnsureLoaded();
	auto &data = Data();
	if (data.showMine == show) {
		return;
	}
	data.showMine = show;
	const auto path = DataPath(kHiddenFile);
	if (show) {
		QFile::remove(path);
	} else {
		QDir().mkpath(QFileInfo(path).absolutePath());
		if (auto file = QFile(path); file.open(QIODevice::WriteOnly)) {
			file.write("1");
		}
	}
	Changed();
	if (data.started) {
		PostAll(true);
	}
}

void PaintBadge(QPainter &p, QRect box) {
	const auto height = box.height();
	if (height <= 0) {
		return;
	}
	const auto size = height * 0.875;
	const auto gap = std::max(box.width() - height, 0);
	const auto left = box.x() + gap + (height - size) / 2.;
	const auto top = box.y() + (height - size) / 2.;

	// The disc and the paper plane of the logo, as in tray_monochrome.svg:
	// a 15.2 disc at (0.4, 0.4) in a 16 x 16 box.
	const auto scale = size / 15.2;
	const auto point = [&](double x, double y) {
		return QPointF(left + (x - 0.4) * scale, top + (y - 0.4) * scale);
	};
	auto plane = QPainterPath();
	plane.moveTo(point(2.56, 7.68));
	plane.lineTo(point(12.80, 3.52));
	plane.lineTo(point(10.88, 12.48));
	plane.lineTo(point(8.00, 10.56));
	plane.lineTo(point(6.40, 12.16));
	plane.lineTo(point(6.24, 9.44));
	plane.closeSubpath();

	const auto pen = p.pen();
	const auto brush = p.brush();
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(BadgeColor());
		p.drawEllipse(QRectF(left, top, size, size));
		p.setBrush(QColor(255, 255, 255));
		p.drawPath(plane);
	}
	p.setPen(pen);
	p.setBrush(brush);
}

not_null<Ui::RpWidget*> CreateProfileBadge(
		not_null<QWidget*> parent,
		not_null<PeerData*> peer,
		QSize size) {
	const auto result = Ui::CreateChild<Ui::RpWidget>(parent.get());
	result->resize(size);
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	result->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(result);
		PaintBadge(p, result->rect());
	}, result->lifetime());
	rpl::single(
		rpl::empty
	) | rpl::then(
		Updates()
	) | rpl::on_next([=] {
		result->setVisible(IsMember(peer));
	}, result->lifetime());
	return result;
}

void AddSettings(SectionBuilder &builder) {
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle(TrValue(u"Badge"_q, u"Значок"_q));
	const auto toggle = builder.addButton({
		.id = u"tgeshka/show_member"_q,
		.title = TrValue(
			u"Show that I use TGeshka"_q,
			u"Показывать, что я пользуюсь TGeshka"_q),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(ShowMine()),
		.keywords = { u"badge"_q, u"значок"_q, u"tgeshka"_q },
	});
	if (toggle) {
		toggle->toggledValue(
		) | rpl::filter([](bool checked) {
			return checked != ShowMine();
		}) | rpl::on_next([](bool checked) {
			SetShowMine(checked);
		}, toggle->lifetime());
	}
	builder.addSkip();
}

} // namespace Tgeshka::Members
