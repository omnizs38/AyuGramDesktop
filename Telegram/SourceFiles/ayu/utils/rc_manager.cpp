// This is the source code of AyuGram for Desktop.
//
// We do not and cannot prevent the use of our code,
// but be respectful and credit the original author.
//
// Copyright @Radolyn, 2026
#include "ayu/utils/rc_manager.h"

#include <QJsonArray>
#include <qjsondocument.h>
#include <QRegularExpression>
#include <QTimer>

namespace {

constexpr auto kPrimaryUrl = "https://update.ayugram.one/rc/current/desktop2";
constexpr auto kExteraUrl = "https://api.exteragram.app/api/v1/profiles/compact";
constexpr auto kFetchTimeout = 15 * 1000;
constexpr auto kMaxResponseSize = 512 * 1024;
constexpr auto kMaxRemoteEntries = 10000;
constexpr auto kMaxBadgeTextSize = 64;

bool ValidDonationUsername(const QString &value) {
	static const auto expression = QRegularExpression(
		u"^@?[A-Za-z][A-Za-z0-9_]{4,31}$"_q);
	return expression.match(value).hasMatch();
}

bool ValidDonationAmount(const QString &value) {
	static const auto expression = QRegularExpression(
		u"^[0-9]{1,7}([.,][0-9]{1,2})?$"_q);
	return expression.match(value).hasMatch();
}

}

std::unordered_set<ID> default_developers = {
	139303278, 168769611, 668557709, 880708503, 963080346, 1156270028, 1282540315, 1348136086, 1374434073, 1752394339,
	1773117711, 2135966128, 5079320635, 5118627360, 5184725450, 5330087923, 5800413909, 6007644928, 7380551229,
	7738913005, 7818249287, 8083933640, 8512951856
};

std::unordered_set<ID> default_channels = {
	1172503281, 1434550607, 1524581881, 1559501352, 1571726392, 1632728092, 1725670701, 1754537498, 1794457129,
	1815864846, 1877362358, 1905581924, 1947958814, 1976430343, 2130395384, 2331068091, 2401498637, 2562664432,
	2564770112, 2685666919, 3116497667, 3212977677, 3572293253
};

void RCManager::start() {
	DEBUG_LOG(("RCManager: starting"));
	_manager = std::make_unique<QNetworkAccessManager>();

	makeRequest();

	_timer = new QTimer(this);
	connect(_timer, &QTimer::timeout, this, &RCManager::makeRequest);
	_timer->start(60 * 60 * 1000); // 1 hour
}

void RCManager::makeRequest() {
	_useExteraFallback = false;
	_retryAttempted = false;
	sendRequest();
}

void RCManager::sendRequest() {
	if (!_manager) {
		return;
	}

	const auto url = QString::fromLatin1(_useExteraFallback ? kExteraUrl : kPrimaryUrl);
	LOG(("RCManager: requesting map"));

	clearSentRequest();

	auto request = QNetworkRequest(QUrl(url));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setTransferTimeout(kFetchTimeout);
	const auto reply = _manager->get(request);
	_reply = reply;
	connect(reply, &QNetworkReply::downloadProgress, [=](
			qint64 received,
			qint64 total) {
		if (received > kMaxResponseSize || total > kMaxResponseSize) {
			reply->abort();
		}
	});
	connect(reply, &QNetworkReply::finished, [=] {
		gotResponse(reply);
	});
	connect(reply, &QNetworkReply::errorOccurred, [=](auto error) {
		gotFailure(reply, error);
	});
}

bool RCManager::tryRetryWithExteraFallback() {
	if (_retryAttempted || _useExteraFallback) {
		return false;
	}
	LOG(("RCManager: switching to extera fallback endpoint"));
	_useExteraFallback = true;
	_retryAttempted = true;
	sendRequest();
	return true;
}

void RCManager::gotResponse(QNetworkReply *reply) {
	if (_reply != reply) {
		reply->deleteLater();
		return;
	}
	const auto status = reply->attribute(
		QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (reply->error() != QNetworkReply::NoError
		|| status < 200
		|| status >= 300) {
		gotFailure(reply, reply->error());
		return;
	}

	const auto response = reply->readAll();
	clearSentRequest(reply);
	if (response.isEmpty()
		|| response.size() > kMaxResponseSize
		|| !handleResponse(response)) {
		LOG(("RCManager: rejected map size: %1").arg(response.size()));
		if (tryRetryWithExteraFallback()) {
			LOG(("RCManager: retrying invalid response with fallback"));
		}
	}
}

bool RCManager::handleResponse(const QByteArray &response) {
	try {
		return applyResponse(response);
	} catch (...) {
		LOG(("RCManager: Failed to apply response"));
		return false;
	}
}

bool RCManager::applyResponse(const QByteArray &response) {
	auto error = QJsonParseError{0, QJsonParseError::NoError};
	const auto document = QJsonDocument::fromJson(response, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		LOG(("RCManager: invalid JSON response: %1")
			.arg(error.errorString()));
		return false;
	}
	const auto root = document.object();
	if (!root.value("developers").isArray()
		|| !root.value("officialChannels").isArray()) {
		LOG(("RCManager: response is missing required arrays"));
		return false;
	}

	const auto developers = root.value("developers").toArray();
	const auto officialChannels = root.value("officialChannels").toArray();
	const auto supporters = root.value("supporters").toArray();
	const auto supporterChannels = root.value("supporterChannels").toArray();
	const auto customBadges = root.value("customBadges").toArray();
	const auto totalEntries = developers.size()
		+ officialChannels.size()
		+ supporters.size()
		+ supporterChannels.size()
		+ customBadges.size();
	if (totalEntries > kMaxRemoteEntries) {
		LOG(("RCManager: response contains too many entries: %1")
			.arg(totalEntries));
		return false;
	}

	auto newDevelopers = std::unordered_set<ID>();
	auto newOfficialChannels = std::unordered_set<ID>();
	auto newSupporters = std::unordered_set<ID>();
	auto newSupporterChannels = std::unordered_set<ID>();
	auto newCustomBadges = std::unordered_map<ID, CustomBadge>();
	const auto addIds = [](const QJsonArray &source, auto &destination) {
		for (const auto &entry : source) {
			if (const auto id = entry.toVariant().toLongLong(); id > 0) {
				destination.insert(id);
			}
		}
	};
	addIds(developers, newDevelopers);
	addIds(officialChannels, newOfficialChannels);
	addIds(supporters, newSupporters);
	addIds(supporterChannels, newSupporterChannels);

	for (const auto &badge : customBadges) {
		if (!badge.isObject()) {
			continue;
		}
		const auto object = badge.toObject();
		const auto id = object.value("id").toVariant().toLongLong();
		const auto badgeValue = object.value("badge");
		if (id <= 0 || !badgeValue.isObject()) {
			continue;
		}
		const auto badgeData = badgeValue.toObject();
		const auto documentId = badgeData.value("documentId")
			.toVariant().toLongLong();
		const auto text = badgeData.value("text").toString();
		if (documentId <= 0 || text.size() > kMaxBadgeTextSize) {
			continue;
		}
		newCustomBadges[id] = CustomBadge{
			.emojiStatusId = EmojiStatusId(documentId),
			.text = text,
		};
	}

	auto donateUsername = _donateUsername;
	auto donateAmountUsd = _donateAmountUsd;
	auto donateAmountTon = _donateAmountTon;
	auto donateAmountRub = _donateAmountRub;
	if (const auto value = root.value("donateUsername").toString();
		ValidDonationUsername(value)) {
		donateUsername = value;
	}
	const auto applyAmount = [&](const char *key, QString &target) {
		if (const auto value = root.value(key).toString();
			ValidDonationAmount(value)) {
			target = value;
		}
	};
	applyAmount("donateAmountUsd", donateAmountUsd);
	applyAmount("donateAmountTon", donateAmountTon);
	applyAmount("donateAmountRub", donateAmountRub);

	_developers = std::move(newDevelopers);
	_officialChannels = std::move(newOfficialChannels);
	_supporters = std::move(newSupporters);
	_supporterChannels = std::move(newSupporterChannels);
	_customBadges = std::move(newCustomBadges);
	_donateUsername = std::move(donateUsername);
	_donateAmountUsd = std::move(donateAmountUsd);
	_donateAmountTon = std::move(donateAmountTon);
	_donateAmountRub = std::move(donateAmountRub);
	initialized = true;

	LOG(("RCManager: Loaded %1 developers, %2 official channels")
		.arg(_developers.size())
		.arg(_officialChannels.size()));
	return true;
}

void RCManager::gotFailure(
		QNetworkReply *reply,
		QNetworkReply::NetworkError error) {
	if (_reply != reply) {
		reply->deleteLater();
		return;
	}
	LOG(("RCManager: Error %1").arg(error));
	clearSentRequest(reply);
	if (tryRetryWithExteraFallback()) {
		LOG(("RCManager: retrying request with extera fallback endpoint"));
		return;
	}
	LOG(("RCManager: no retry left for failed request"));
}

void RCManager::clearSentRequest(QNetworkReply *expected) {
	if (expected && _reply != expected) {
		return;
	}
	const auto reply = base::take(_reply);
	if (!reply) {
		return;
	}
	disconnect(reply, &QNetworkReply::finished, nullptr, nullptr);
	disconnect(reply, &QNetworkReply::errorOccurred, nullptr, nullptr);
	reply->abort();
	reply->deleteLater();
}

RCManager::~RCManager() {
	clearSentRequest();
	_manager = nullptr;
}
