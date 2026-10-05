#include "attendancemanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "databasemanager.h"
#include "packetmanager.h"
#include "packet_umsgmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_itemmanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <random>
#include <ctime>

using json = nlohmann::json;

AttendanceManager attendanceManager;

bool AttendanceManager::Init(const std::string& configPath) {
	serverConsole.Print(PrefixType::Info, "[ AttendanceManager ] Initializing daily attendance & roulette system...\n");
	return LoadConfig(configPath);
}

bool AttendanceManager::LoadConfig(const std::string& configPath) {
	std::vector<std::string> searchPaths = {
		configPath,
		"configs/attendance.json",
		"configs\\attendance.json",
		"bin/configs/attendance.json",
		"bin\\configs\\attendance.json",
		"../configs/attendance.json"
	};

	std::string resolvedPath = "";
	for (const auto& path : searchPaths) {
		if (std::filesystem::exists(path)) {
			resolvedPath = path;
			break;
		}
	}

	if (resolvedPath.empty()) {
		serverConsole.Print(PrefixType::Warn, format("[ AttendanceManager ] Failed to locate attendance config file! (tried {})\n", configPath));
		return false;
	}

	try {
		std::ifstream file(resolvedPath);
		if (!file.is_open()) {
			serverConsole.Print(PrefixType::Error, format("[ AttendanceManager ] Failed to open attendance config: {}\n", resolvedPath));
			return false;
		}

		json root;
		file >> root;

		_config.enabled = root.value("enabled", true);
		_config.autoPopupOnLogin = root.value("autoPopupOnLogin", true);
		_config.dailyRewards.clear();
		_config.candidatePool.clear();

		if (root.contains("dailyRewards") && root["dailyRewards"].is_array()) {
			for (const auto& itemJson : root["dailyRewards"]) {
				AttendanceDailyReward reward;
				reward.day = itemJson.value("day", (unsigned char)1);
				reward.item.itemID = itemJson.value("itemID", (unsigned int)0);
				reward.item.name = itemJson.value("name", "");
				reward.item.count = itemJson.value("count", (unsigned short)1);
				reward.item.period = itemJson.value("period", (unsigned short)0);
				reward.item.expirationTime = itemJson.value("expirationTime", (unsigned int)0);
				reward.item.isEvent = itemJson.value("isEvent", (unsigned char)0);
				reward.item.points = itemJson.value("points", (unsigned int)0);
				reward.item.weight = itemJson.value("weight", (unsigned int)10);
				_config.dailyRewards.push_back(reward);
			}
		}

		if (root.contains("candidatePool") && root["candidatePool"].is_array()) {
			for (const auto& itemJson : root["candidatePool"]) {
				AttendanceRewardItem item;
				item.itemID = itemJson.value("itemID", (unsigned int)0);
				item.name = itemJson.value("name", "");
				item.count = itemJson.value("count", (unsigned short)1);
				item.period = itemJson.value("period", (unsigned short)0);
				item.expirationTime = itemJson.value("expirationTime", (unsigned int)0);
				item.isEvent = itemJson.value("isEvent", (unsigned char)0);
				item.points = itemJson.value("points", (unsigned int)0);
				item.weight = itemJson.value("weight", (unsigned int)10);
				_config.candidatePool.push_back(item);
			}
		}

		serverConsole.Print(PrefixType::Info, format("[ AttendanceManager ] Loaded {} daily ladder rewards and {} candidate items from {}\n",
			_config.dailyRewards.size(), _config.candidatePool.size(), resolvedPath));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ AttendanceManager ] Exception loading config: {}\n", e.what()));
		return false;
	}
}

std::vector<AttendanceRewardItem> AttendanceManager::selectCandidateItems() {
	std::vector<AttendanceRewardItem> candidates;
	if (_config.candidatePool.size() >= 4) {
		for (size_t i = 0; i < 4; ++i) {
			candidates.push_back(_config.candidatePool[i]);
		}
	}
	else {
		candidates = _config.candidatePool;
		// Pad with defaults if less than 4
		while (candidates.size() < 4) {
			AttendanceRewardItem fallback;
			if (candidates.size() == 0) {
				fallback = { 261, "領航者 M79 (3日)", 1, 3, 259200, 0, 0, 25 };
			}
			else if (candidates.size() == 1) {
				fallback = { 166, "普通密碼箱 (2個)", 2, 0, 0, 0, 0, 35 };
			}
			else if (candidates.size() == 2) {
				fallback = { 173, "電子破譯晶片 (1個)", 1, 0, 0, 1, 0, 20 };
			}
			else {
				fallback = { 108, "Point加倍卡 (1日) + 1500 Point", 1, 1, 86400, 0, 1500, 20 };
			}
			candidates.push_back(fallback);
		}
	}
	return candidates;
}

bool AttendanceManager::grantRewardItem(User* user, const AttendanceRewardItem& item) {
	if (!user) return false;
	unsigned long userID = user->GetUserID();
	time_t now = time(nullptr);

	if (item.points > 0) {
		databaseManager.AddUserFunds(userID, true, item.points);
	}

	if (item.itemID > 0) {
		auto currentInv = user->GetUserInventory();
		bool alreadyOwned = false;
		InventoryItem ownedItem;
		unsigned short maxSlotID = 0;

		for (const auto& inv : currentInv) {
			if (inv.slotID > maxSlotID) {
				maxSlotID = inv.slotID;
			}
			if (inv.itemID == (unsigned short)item.itemID) {
				alreadyOwned = true;
				ownedItem = inv;
			}
		}

		if (item.period == 0) {
			// Consumable count item (e.g. Decoders, C-Box)
			if (alreadyOwned) {
				databaseManager.UpdateUserInventoryItemCount(userID, (unsigned short)item.itemID, item.count);
			}
			else {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = (unsigned short)item.itemID;
				newItem.count = item.count;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = 0;
				newItem.enhanceBits = 0;
				databaseManager.AddUserInventoryItems(userID, { newItem });
			}
		}
		else {
			// Duration period item
			if (alreadyOwned && ownedItem.expirationTime > 0) {
				unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
				unsigned long newExp = baseTime + (unsigned long)item.period * 86400;
				databaseManager.UpdateUserInventoryExpiration(userID, (unsigned short)item.itemID, newExp);
			}
			else if (!alreadyOwned) {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = (unsigned short)item.itemID;
				newItem.count = 1;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = (unsigned long)(now + (unsigned long)item.period * 86400);
				newItem.enhanceBits = 0;
				databaseManager.AddUserInventoryItems(userID, { newItem });
			}
		}
	}

	return true;
}

void AttendanceManager::OnUserEnterLobby(User* user) {
	if (!user || !_config.enabled) return;

	unsigned long userID = user->GetUserID();
	AttendanceRecord record;
	databaseManager.GetUserAttendance(userID, record);

	// If eligible today and configured to auto-popup on login, send attendance info packet
	if (_config.autoPopupOnLogin && record.canCheckIn) {
		SendAttendanceInfo(user, true);
	}
}

void AttendanceManager::SendAttendanceInfo(User* user, bool forceOpen) {
	if (!user) return;
	auto connection = user->GetConnection();
	if (!connection) return;

	unsigned long userID = user->GetUserID();
	AttendanceRecord record;
	databaseManager.GetUserAttendance(userID, record);

	unsigned char canSpin = record.canCheckIn ? 1 : 0;
	unsigned char displayStreak = 0;

	if (!record.canCheckIn) {
		displayStreak = record.consecutiveDays;
	}
	else {
		if (record.daysSinceLast == 1) {
			displayStreak = record.consecutiveDays;
		}
		else {
			displayStreak = 0;
		}
	}

	auto candidates = selectCandidateItems();

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	packet->WriteUInt8(Packet_ItemType::Roulette); // 8
	packet->WriteUInt8(0);                          // Action 0 (Status & list)
	packet->WriteUInt8(canSpin);                    // canSpin / chances
	packet->WriteUInt32_LE(0);                      // unknown (0)

	// 4 Candidate items
	for (size_t i = 0; i < 4; ++i) {
		const auto& item = candidates[i];
		packet->WriteUInt32_LE(item.itemID);
		packet->WriteUInt16_LE(item.count);
		packet->WriteUInt16_LE(item.period);
		packet->WriteUInt32_LE(item.expirationTime);
		packet->WriteUInt8(item.isEvent);
	}

	// Current consecutive days streak (1 ~ 7 or 0)
	packet->WriteUInt8(displayStreak);

	// Daily items count (7)
	unsigned char dailyCount = (unsigned char)_config.dailyRewards.size();
	packet->WriteUInt8(dailyCount);

	// 7 Daily ladder items
	for (const auto& daily : _config.dailyRewards) {
		const auto& item = daily.item;
		packet->WriteUInt32_LE(item.itemID);
		packet->WriteUInt16_LE(item.count);
		packet->WriteUInt16_LE(item.period);
		packet->WriteUInt32_LE(item.expirationTime);
		packet->WriteUInt8(item.isEvent);
	}

	packet->Send();
}

void AttendanceManager::ClaimAttendanceReward(User* user) {
	if (!user) return;
	auto connection = user->GetConnection();
	if (!connection) return;

	if (!_config.enabled) {
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, "[每日戰場補給] 活動目前未開啟。");
		return;
	}

	unsigned long userID = user->GetUserID();
	AttendanceRecord record;
	databaseManager.GetUserAttendance(userID, record);

	if (!record.canCheckIn) {
		serverConsole.Print(PrefixType::Warn, format("[ AttendanceManager ] User ({}) attempted duplicate claim today!\n", user->GetUserLogName()));
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, "[每日戰場補給] 您今天已經領取過簽到獎勵囉，請明天再來！");
		return;
	}

	unsigned char newConsecutiveDays = 1;
	if (record.daysSinceLast == 1) {
		newConsecutiveDays = (record.consecutiveDays % 7) + 1;
	}
	else {
		newConsecutiveDays = 1;
	}

	if (!databaseManager.RecordUserAttendance(userID, newConsecutiveDays)) {
		serverConsole.Print(PrefixType::Warn, format("[ AttendanceManager ] User ({}) atomic check-in rejected (already claimed)!\n", user->GetUserLogName()));
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, "[每日戰場補給] 您今天已經領取過簽到獎勵囉，請明天再來！");
		return;
	}

	auto candidates = selectCandidateItems();
	unsigned int totalWeight = 0;
	for (const auto& c : candidates) {
		totalWeight += (c.weight > 0 ? c.weight : 1);
	}

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<unsigned int> dist(0, totalWeight > 0 ? totalWeight - 1 : 0);
	unsigned int roll = dist(gen);

	AttendanceRewardItem wonCandidate = candidates[0];
	unsigned int currentW = 0;
	for (const auto& c : candidates) {
		currentW += (c.weight > 0 ? c.weight : 1);
		if (roll < currentW) {
			wonCandidate = c;
			break;
		}
	}

	AttendanceRewardItem wonDaily;
	for (const auto& d : _config.dailyRewards) {
		if (d.day == newConsecutiveDays) {
			wonDaily = d.item;
			break;
		}
	}
	if (wonDaily.itemID == 0 && wonDaily.points == 0 && !_config.dailyRewards.empty()) {
		wonDaily = _config.dailyRewards[0].item;
	}

	grantRewardItem(user, wonCandidate);
	grantRewardItem(user, wonDaily);

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	packet->WriteUInt8(Packet_ItemType::Roulette); // 8
	packet->WriteUInt8(1);                          // Action 1 (Claim result)

	// Won Candidate Item (13 bytes)
	packet->WriteUInt32_LE(wonCandidate.itemID);
	packet->WriteUInt16_LE(wonCandidate.count);
	packet->WriteUInt16_LE(wonCandidate.period);
	packet->WriteUInt32_LE(wonCandidate.expirationTime);
	packet->WriteUInt8(wonCandidate.isEvent);

	// Consecutive day reached (1 ~ 7)
	packet->WriteUInt8(newConsecutiveDays);

	// Won Daily ladder item (13 bytes)
	packet->WriteUInt32_LE(wonDaily.itemID);
	packet->WriteUInt16_LE(wonDaily.count);
	packet->WriteUInt16_LE(wonDaily.period);
	packet->WriteUInt32_LE(wonDaily.expirationTime);
	packet->WriteUInt8(wonDaily.isEvent);

	packet->Send();

	// Sync inventory and currency
	packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
	auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });

	std::string notice = format("[每日戰場補給] 簽到成功！連續登入第 {} 天！獲得「{}」與「{}」！",
		newConsecutiveDays, wonDaily.name, wonCandidate.name);
	packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, notice);

	serverConsole.Print(PrefixType::Info, format("[ AttendanceManager ] User ({}) claimed Day {} attendance! Won '{}' and '{}'\n",
		user->GetUserLogName(), newConsecutiveDays, wonDaily.name, wonCandidate.name));
}
