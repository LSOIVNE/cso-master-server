#include "baseballeventmanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "packetmanager.h"
#include "packet_umsgmanager.h"
#include "itemdefinitionmanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <algorithm>
#include <ctime>

using json = nlohmann::json;

BaseballEventManager baseballEventManager;

static std::string GetCurrentDateString() {
	time_t t = time(nullptr);
	tm localTm{};
	localtime_s(&localTm, &t);
	char buf[32];
	strftime(buf, sizeof(buf), "%Y-%m-%d", &localTm);
	return std::string(buf);
}

BaseballEventManager::BaseballEventManager() {
}

BaseballEventManager::~BaseballEventManager() {
}

bool BaseballEventManager::Init(const std::string& configPath) {
	serverConsole.Print(PrefixType::Info, "[ BaseballEventManager ] Initializing Baseball Event system...\n");
	return LoadConfig(configPath);
}

bool BaseballEventManager::LoadConfig(const std::string& configPath) {
	std::string resolvedPath = configPath.empty() ? "configs/baseballevent.json" : configPath;
	if (!std::filesystem::exists(resolvedPath)) {
		serverConsole.Print(PrefixType::Error, format("[ BaseballEventManager ] Config file not found: {}\n", resolvedPath));
		return false;
	}

	try {
		std::ifstream file(resolvedPath);
		if (!file.is_open()) {
			serverConsole.Print(PrefixType::Error, format("[ BaseballEventManager ] Could not open file: {}\n", resolvedPath));
			return false;
		}

		json root;
		file >> root;

		if (root.contains("Settings")) {
			const auto& s = root["Settings"];
			_settings.active = s.value("Active", true);
			_settings.playtimeSecondsPerBall = s.value("PlaytimeSecondsPerBall", 1200);
			_settings.maxBalls = (unsigned char)s.value("MaxBalls", 99);

			if (s.contains("HitWeights")) {
				const auto& hw = s["HitWeights"];
				_settings.hitWeights.out = hw.value("Out", 38);
				_settings.hitWeights.single = hw.value("Single", 32);
				_settings.hitWeights.double_hit = hw.value("Double", 18);
				_settings.hitWeights.triple = hw.value("Triple", 8);
				_settings.hitWeights.homeRun = hw.value("HomeRun", 4);
			}
		}

		_dailyRewards.clear();
		if (root.contains("DailyScoreRewards") && root["DailyScoreRewards"].is_array()) {
			for (const auto& it : root["DailyScoreRewards"]) {
				BaseballScoreRewardTier tier;
				tier.minScore = it.value("MinScore", 0);
				tier.maxScore = it.value("MaxScore", 0);
				tier.itemID = (unsigned short)it.value("ItemID", 0);
				tier.count = (unsigned short)it.value("Count", 1);
				tier.duration = it.value("Duration", 0);
				tier.name = it.value("Name", "");
				_dailyRewards.push_back(tier);
			}
		}

		serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] Loaded config with {} reward tiers.\n", _dailyRewards.size()));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ BaseballEventManager ] JSON parse error: {}\n", e.what()));
		return false;
	}
}

bool BaseballEventManager::GetOrInitUserData(User* user, UserBaseballData& outData) {
	if (!user) return false;
	return databaseManager.GetUserBaseballData(user->GetUserID(), outData);
}

bool BaseballEventManager::CheckDailyReset(User* user, UserBaseballData& data) {
	if (!user) return false;

	std::string curDate = GetCurrentDateString();
	if (data.lastResetDate == curDate) {
		return false;
	}

	// Date changed: issue yesterday's reward if todayScore > 0 and previous date existed
	if (!data.lastResetDate.empty() && data.todayScore > 0) {
		for (const auto& tier : _dailyRewards) {
			if (data.todayScore >= tier.minScore && data.todayScore <= tier.maxScore) {
				InventoryItem item{};
				GrantPrize(user, tier.itemID, tier.count, tier.duration, &item);
				if (item.slotID != 0 && user->GetConnection()) {
					packetManager.SendPacket_InventoryDelta(user->GetConnection(), { item }, {});
				}

				std::string msg = format("[CSO棒球經典賽] 昨日累積分數為 {} 分，已獲得結算獎勵：{}！", (int)data.todayScore, tier.name);
				if (user->GetConnection()) {
					packet_UMsgManager.SendPacket_UMsg_Chat(user->GetConnection(), Packet_UMsgType::ChannelChat, Utf8ToCp950("系統"), Utf8ToCp950(msg));
				}
				serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) granted yesterday's reward (score {} -> itemID {})\n",
					user->GetUserLogName(), (int)data.todayScore, tier.itemID));
				break;
			}
		}
	}

	data.todayScore = 0;
	data.outCount = 0;
	data.baseStatus = 0;
	data.lastResetDate = curDate;
	databaseManager.SaveUserBaseballData(data);
	return true;
}

void BaseballEventManager::ParsePacket(User* user, TCPConnection::Packet::pointer packet) {
	if (!user || !packet) return;

	auto connection = user->GetConnection();
	if (!connection) return;

	unsigned char subAction = packet->ReadUInt8();
	serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) sent Baseball action {}\n", user->GetUserLogName(), (int)subAction));

	UserBaseballData data;
	GetOrInitUserData(user, data);
	CheckDailyReset(user, data);

	switch (subAction) {
		case 40: // Client opens dialog and requests status
		case 42: {
			SendPacket_Status(connection, data);
			break;
		}
		case 41: { // Client clicks Swing (Batting)
			if (data.outCount >= 3) {
				serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) tried to swing while 3 OUT!\n", user->GetUserLogName()));
				SendPacket_Status(connection, data);
				return;
			}

			if (data.ballCount == 0) {
				serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) tried to swing with 0 balls!\n", user->GetUserLogName()));
				SendPacket_Status(connection, data);
				return;
			}

			// Deduct 1 ball
			data.ballCount--;

			// Roll hit result
			int wOut = _settings.hitWeights.out;
			int w1 = _settings.hitWeights.single;
			int w2 = _settings.hitWeights.double_hit;
			int w3 = _settings.hitWeights.triple;
			int wHR = _settings.hitWeights.homeRun;
			int totalWeight = wOut + w1 + w2 + w3 + wHR;
			if (totalWeight <= 0) totalWeight = 100;

			int roll = rand() % totalWeight;
			unsigned char hitResult = 0;
			if (roll < wOut) {
				hitResult = 0; // Out
			}
			else if (roll < wOut + w1) {
				hitResult = 1; // 1-Base Hit
			}
			else if (roll < wOut + w1 + w2) {
				hitResult = 2; // 2-Base Hit
			}
			else if (roll < wOut + w1 + w2 + w3) {
				hitResult = 3; // 3-Base Hit
			}
			else {
				hitResult = 4; // Home Run
			}

			// Runner positions: b1 = 1st, b2 = 2nd, b3 = 3rd
			bool b1 = (data.baseStatus % 10) != 0;
			bool b2 = ((data.baseStatus % 100) / 10) != 0;
			bool b3 = (data.baseStatus / 100) != 0;
			int runsScored = 0;

			if (hitResult == 0) { // Out
				data.outCount++;
				if (data.outCount >= 3) {
					data.outCount = 3;
					data.baseStatus = 0; // Clear bases on 3 OUT
				}
			}
			else if (hitResult == 1) { // 1-Base Hit
				if (b3) runsScored++;
				bool new_b3 = b2;
				bool new_b2 = b1;
				bool new_b1 = true;
				data.baseStatus = (new_b3 ? 100 : 0) + (new_b2 ? 10 : 0) + (new_b1 ? 1 : 0);
			}
			else if (hitResult == 2) { // 2-Base Hit
				if (b3) runsScored++;
				if (b2) runsScored++;
				bool new_b3 = b1;
				bool new_b2 = true;
				bool new_b1 = false;
				data.baseStatus = (new_b3 ? 100 : 0) + (new_b2 ? 10 : 0) + (new_b1 ? 1 : 0);
			}
			else if (hitResult == 3) { // 3-Base Hit
				if (b3) runsScored++;
				if (b2) runsScored++;
				if (b1) runsScored++;
				bool new_b3 = true;
				bool new_b2 = false;
				bool new_b1 = false;
				data.baseStatus = (new_b3 ? 100 : 0) + (new_b2 ? 10 : 0) + (new_b1 ? 1 : 0);
			}
			else if (hitResult == 4) { // Home Run
				runsScored = (b3 ? 1 : 0) + (b2 ? 1 : 0) + (b1 ? 1 : 0) + 1;
				data.baseStatus = 0;
			}

			data.todayScore = (unsigned char)std::min(255, (int)data.todayScore + runsScored);
			data.totalScore = (unsigned short)std::min(65535, (int)data.totalScore + runsScored);

			databaseManager.SaveUserBaseballData(data);

			serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) swing result: hitResult={}, runs={}, todayScore={}, outCount={}, baseStatus={}\n",
				user->GetUserLogName(), (int)hitResult, runsScored, (int)data.todayScore, (int)data.outCount, (int)data.baseStatus));

			SendPacket_BattingResult(connection, data, hitResult);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ BaseballEventManager ] User ({}) sent unknown action {}\n", user->GetUserLogName(), (int)subAction));
			break;
		}
	}
}

void BaseballEventManager::OnMatchEnd(User* user, unsigned int durationSeconds) {
	if (!user || !_settings.active || durationSeconds == 0) return;

	UserBaseballData data;
	GetOrInitUserData(user, data);
	CheckDailyReset(user, data);

	data.playtimeSeconds += durationSeconds;
	unsigned char earnedBalls = 0;

	while (data.playtimeSeconds >= _settings.playtimeSecondsPerBall) {
		data.playtimeSeconds -= _settings.playtimeSecondsPerBall;
		if (data.ballCount < _settings.maxBalls) {
			data.ballCount++;
			earnedBalls++;
		}
		else {
			break;
		}
	}

	databaseManager.SaveUserBaseballData(data);

	auto conn = user->GetConnection();
	if (conn) {
		if (earnedBalls > 0) {
			std::string msg = format("[CSO棒球經典賽] 遊戲累積滿20分鐘，獲得 {} 顆棒球！目前持有：{} 顆", (int)earnedBalls, (int)data.ballCount);
			packet_UMsgManager.SendPacket_UMsg_Chat(conn, Packet_UMsgType::ChannelChat, Utf8ToCp950("系統"), Utf8ToCp950(msg));
		}
		SendPacket_Status(conn, data);
	}
}

void BaseballEventManager::SendPacket_Status(TCPConnection::pointer connection, const UserBaseballData& data) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	packet->WriteUInt8(4);  // gameType: Baseball Event
	packet->WriteUInt8(40); // subAction: Status
	packet->WriteUInt8(data.todayScore);
	packet->WriteUInt8((unsigned char)(data.totalScore > 255 ? 255 : data.totalScore));
	packet->WriteUInt8(data.ballCount);
	packet->WriteUInt8(data.baseStatus);
	packet->WriteUInt8(data.outCount);

	packet->Send();
}

void BaseballEventManager::SendPacket_BattingResult(TCPConnection::pointer connection, const UserBaseballData& data, unsigned char hitResult) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	packet->WriteUInt8(4);  // gameType: Baseball Event
	packet->WriteUInt8(41); // subAction: Batting Result
	packet->WriteUInt8(data.todayScore);
	packet->WriteUInt8((unsigned char)(data.totalScore > 255 ? 255 : data.totalScore));
	packet->WriteUInt8(data.ballCount);
	packet->WriteUInt8(data.baseStatus);
	packet->WriteUInt8(data.outCount);
	packet->WriteUInt8(hitResult);

	packet->Send();
}

bool BaseballEventManager::AddUserBalls(User* user, int deltaBalls) {
	if (!user) return false;

	UserBaseballData data;
	GetOrInitUserData(user, data);
	CheckDailyReset(user, data);

	int newCount = (int)data.ballCount + deltaBalls;
	if (newCount < 0) newCount = 0;
	if (newCount > (int)_settings.maxBalls) newCount = (int)_settings.maxBalls;

	data.ballCount = (unsigned char)newCount;
	databaseManager.SaveUserBaseballData(data);

	if (user->GetConnection()) {
		SendPacket_Status(user->GetConnection(), data);
	}
	return true;
}

bool BaseballEventManager::ResetUserToday(User* user) {
	if (!user) return false;

	UserBaseballData data;
	GetOrInitUserData(user, data);

	data.todayScore = 0;
	data.outCount = 0;
	data.baseStatus = 0;
	data.lastResetDate = GetCurrentDateString();
	databaseManager.SaveUserBaseballData(data);

	if (user->GetConnection()) {
		SendPacket_Status(user->GetConnection(), data);
	}
	return true;
}

bool BaseballEventManager::SetUserScore(User* user, unsigned char todayScore, unsigned short totalScore) {
	if (!user) return false;

	UserBaseballData data;
	GetOrInitUserData(user, data);
	CheckDailyReset(user, data);

	data.todayScore = todayScore;
	data.totalScore = totalScore;
	databaseManager.SaveUserBaseballData(data);

	if (user->GetConnection()) {
		SendPacket_Status(user->GetConnection(), data);
	}
	return true;
}

void BaseballEventManager::GrantPrize(User* user, unsigned short itemID, unsigned short count, unsigned int durationDays, InventoryItem* outItem) {
	if (!user) return;
	unsigned long userID = user->GetUserID();
	auto inv = user->GetUserInventory();

	unsigned short maxSlotID = 0;
	bool alreadyOwned = false;
	InventoryItem ownedItem{};
	for (const auto& it : inv) {
		if (it.slotID > maxSlotID) {
			maxSlotID = it.slotID;
		}
		if (it.itemID == itemID) {
			alreadyOwned = true;
			ownedItem = it;
		}
	}

	const auto* itemDef = itemDefinitionManager.GetItem(itemID);
	bool isPeriodItem = (durationDays != 0) || (itemDef != nullptr && itemDef->isPeriod);
	time_t now = time(nullptr);

	if (!isPeriodItem && durationDays == 0) {
		if (alreadyOwned) {
			databaseManager.UpdateUserInventoryItemCount(userID, itemID, count);
			if (outItem != nullptr) {
				ownedItem.count += count;
				*outItem = ownedItem;
			}
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = itemID;
			newItem.count = count;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = 0;
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
			if (outItem != nullptr) {
				*outItem = newItem;
			}
		}
	}
	else {
		bool isPermItem = (durationDays == 0);
		if (alreadyOwned) {
			if (ownedItem.expirationTime == 0) {
				serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) already owns permanent itemID {}, skipping duplicate.\n",
					user->GetUserLogName(), itemID));
			}
			else if (isPermItem) {
				ownedItem.expirationTime = 0;
				databaseManager.UpdateUserInventoryExpiration(userID, itemID, 0);
				if (outItem != nullptr) {
					*outItem = ownedItem;
				}
			}
			else {
				unsigned long addSeconds = (unsigned long)durationDays * 86400;
				unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
				ownedItem.expirationTime = baseTime + addSeconds;
				databaseManager.UpdateUserInventoryExpiration(userID, itemID, ownedItem.expirationTime);
				if (outItem != nullptr) {
					*outItem = ownedItem;
				}
			}
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = itemID;
			newItem.count = count > 0 ? count : 1;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = isPermItem ? 0 : (unsigned long)(now + (unsigned long)durationDays * 86400);
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
			if (outItem != nullptr) {
				*outItem = newItem;
			}
		}
	}
}
