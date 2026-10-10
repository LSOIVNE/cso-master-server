#include "attendancemanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "databasemanager.h"
#include "packetmanager.h"
#include "packet_umsgmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_itemmanager.h"
#include "itemdefinitionmanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <random>
#include <ctime>

using json = nlohmann::json;

AttendanceManager attendanceManager;

static void ParseRewardItemJson(const json& j, AttendanceRewardItem& item) {
	item.itemID = j.contains("ItemID") ? j["ItemID"].get<unsigned int>() : j.value("itemID", 0u);
	item.duration = j.contains("Duration") ? j["Duration"].get<unsigned short>() : j.value("duration", (unsigned short)0);
	item.count = j.contains("Count") ? j["Count"].get<unsigned short>() : j.value("count", (unsigned short)0);
	item.enhance = j.contains("Enhance") ? j["Enhance"].get<unsigned int>() : j.value("enhance", 0u);
	item.isEvent = j.contains("IsEvent") ? j["IsEvent"].get<unsigned char>() : j.value("isEvent", (unsigned char)0);
	item.weight = j.contains("Weight") ? j["Weight"].get<unsigned int>() : j.value("weight", 10u);
	item.name = j.contains("Name") ? j["Name"].get<std::string>() : j.value("name", "");

	if (item.name.empty() && item.itemID > 0) {
		const ItemDefinition* def = itemDefinitionManager.GetItem((unsigned short)item.itemID);
		if (def) {
			item.name = def->name;
		}
	}
}

static std::string FormatRewardDisplay(const AttendanceRewardItem& it) {
	std::string s = it.name.empty() ? format("道具 #{}", it.itemID) : it.name;
	if (it.duration > 0 && it.count > 0) {
		s += format(" ({}日 {}個)", it.duration, it.count);
	}
	else if (it.duration > 0) {
		s += format(" ({}日)", it.duration);
	}
	else if (it.count > 0) {
		s += format(" ({}個)", it.count);
	}
	return s;
}

bool AttendanceManager::Init(const std::string& configPath) {
	serverConsole.Print(PrefixType::Info, "[ AttendanceManager ] Initializing daily attendance & roulette system...\n");
	return LoadConfig(configPath);
}

bool AttendanceManager::LoadConfig(const std::string& configPath) {
	std::string resolvedPath = configPath.empty() ? "configs/attendance.json" : configPath;
	if (!std::filesystem::exists(resolvedPath)) {
		serverConsole.Print(PrefixType::Warn, format("[ AttendanceManager ] Config file not found: {}\n", resolvedPath));
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

		_config.enabled = root.contains("Enabled") ? root["Enabled"].get<bool>() : root.value("enabled", true);
		_config.autoPopupOnLogin = root.contains("AutoPopupOnLogin") ? root["AutoPopupOnLogin"].get<bool>() : root.value("autoPopupOnLogin", true);
		_config.serverMessageEnabled = root.contains("ServerMessageEnabled") ? root["ServerMessageEnabled"].get<bool>() : root.value("serverMessageEnabled", true);
		_config.dailyRewards.clear();
		_config.candidatePool.clear();

		auto parseArray = [](const json& parent, const std::string& key1, const std::string& key2) -> const json* {
			if (parent.contains(key1) && parent[key1].is_array()) return &parent[key1];
			if (parent.contains(key2) && parent[key2].is_array()) return &parent[key2];
			return nullptr;
		};

		const json* dailyArr = parseArray(root, "DailyRewards", "dailyRewards");
		if (dailyArr) {
			for (const auto& itemJson : *dailyArr) {
				AttendanceDailyReward reward;
				reward.day = itemJson.contains("Day") ? itemJson["Day"].get<unsigned char>() : itemJson.value("day", (unsigned char)1);
				ParseRewardItemJson(itemJson, reward.item);
				_config.dailyRewards.push_back(reward);
			}
		}

		const json* candArr = parseArray(root, "CandidatePool", "candidatePool");
		if (candArr) {
			for (const auto& itemJson : *candArr) {
				AttendanceRewardItem item;
				ParseRewardItemJson(itemJson, item);
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

void AttendanceManager::ClearUserCandidates(unsigned long userID) {
	_userCandidates.erase(userID);
}

std::vector<AttendanceRewardItem> AttendanceManager::selectCandidateItems(User* user) {
	if (user != nullptr) {
		auto it = _userCandidates.find(user->GetUserID());
		if (it != _userCandidates.end() && it->second.size() == 4) {
			return it->second;
		}
	}

	std::vector<AttendanceRewardItem> candidates;
	if (_config.candidatePool.size() <= 4) {
		candidates = _config.candidatePool;
	}
	else {
		// Randomly select 4 distinct items from CandidatePool (weighted sampling without replacement)
		std::vector<AttendanceRewardItem> poolCopy = _config.candidatePool;
		std::random_device rd;
		std::mt19937 gen(rd());

		for (int i = 0; i < 4 && !poolCopy.empty(); ++i) {
			unsigned int totalWeight = 0;
			for (const auto& item : poolCopy) {
				totalWeight += (item.weight > 0 ? item.weight : 1);
			}
			std::uniform_int_distribution<unsigned int> dist(0, totalWeight > 0 ? totalWeight - 1 : 0);
			unsigned int roll = dist(gen);
			unsigned int currentW = 0;
			size_t chosenIdx = 0;
			for (size_t j = 0; j < poolCopy.size(); ++j) {
				currentW += (poolCopy[j].weight > 0 ? poolCopy[j].weight : 1);
				if (roll < currentW) {
					chosenIdx = j;
					break;
				}
			}
			candidates.push_back(poolCopy[chosenIdx]);
			poolCopy.erase(poolCopy.begin() + chosenIdx);
		}
	}

	// Pad with defaults if less than 4
	while (candidates.size() < 4) {
		AttendanceRewardItem fallback;
		if (candidates.size() == 0) {
			fallback = { 261, 3, 0, 0, 0, 25, "領航者 M79" };
		}
		else if (candidates.size() == 1) {
			fallback = { 166, 0, 2, 0, 0, 35, "普通密碼箱" };
		}
		else if (candidates.size() == 2) {
			fallback = { 173, 0, 1, 0, 1, 20, "電子破譯晶片" };
		}
		else {
			fallback = { 108, 1, 0, 0, 0, 20, "Point加倍卡" };
		}
		candidates.push_back(fallback);
	}

	if (user != nullptr) {
		_userCandidates[user->GetUserID()] = candidates;
	}

	return candidates;
}

bool AttendanceManager::grantRewardItem(User* user, const AttendanceRewardItem& item) {
	if (!user) return false;
	unsigned long userID = user->GetUserID();
	time_t now = time(nullptr);

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

		if (item.duration == 0) {
			// Consumable count item (e.g. Decoders, C-Box, permanent)
			unsigned short addedCount = item.count > 0 ? item.count : 1;
			if (alreadyOwned) {
				databaseManager.UpdateUserInventoryItemCount(userID, (unsigned short)item.itemID, addedCount);
			}
			else {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = (unsigned short)item.itemID;
				newItem.count = addedCount;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = 0;
				newItem.enhanceBits = item.enhance;
				databaseManager.AddUserInventoryItems(userID, { newItem });
			}
		}
		else {
			// Duration period item (e.g. 1 day, 3 days)
			if (alreadyOwned && ownedItem.expirationTime > 0) {
				unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
				unsigned long newExp = baseTime + (unsigned long)item.duration * 86400;
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
				newItem.expirationTime = (unsigned long)(now + (unsigned long)item.duration * 86400);
				newItem.enhanceBits = item.enhance;
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

	auto candidates = selectCandidateItems(user);

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	packet->WriteUInt8(Packet_ItemType::Roulette); // 8
	packet->WriteUInt8(0);                          // Action 0 (Status & list)
	packet->WriteUInt8(canSpin);                    // canSpin / chances
	packet->WriteUInt32_LE(0);                      // unknown (0)

	// 4 Candidate items
	// Wire format per item:
	// Field 1 (UInt32 LE): itemID
	// Field 2 (UInt16 LE): duration (天數 -> 格式化為 %d日)
	// Field 3 (UInt16 LE): count (數量 -> 格式化為 %d個)
	// Field 4 (UInt32 LE): enhance (武器強化屬性，0 表示無強化，不顯示 (+X))
	// Field 5 (UInt8): isEvent (活動角標)
	for (size_t i = 0; i < 4; ++i) {
		const auto& item = candidates[i];
		packet->WriteUInt32_LE(item.itemID);
		packet->WriteUInt16_LE(item.duration);
		packet->WriteUInt16_LE(item.count);
		packet->WriteUInt32_LE(item.enhance);
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
		packet->WriteUInt16_LE(item.duration);
		packet->WriteUInt16_LE(item.count);
		packet->WriteUInt32_LE(item.enhance);
		packet->WriteUInt8(item.isEvent);
	}

	packet->Send();
}

void AttendanceManager::ClaimAttendanceReward(User* user) {
	if (!user) return;
	auto connection = user->GetConnection();
	if (!connection) return;

	if (!_config.enabled) {
		packet_UMsgManager.SendPacket_UMsg_ServerMessageText(connection, "[新戰場補給] 活動目前未開啟。");
		return;
	}

	unsigned long userID = user->GetUserID();
	AttendanceRecord record;
	databaseManager.GetUserAttendance(userID, record);

	if (!record.canCheckIn) {
		serverConsole.Print(PrefixType::Warn, format("[ AttendanceManager ] User ({}) attempted duplicate claim today!\n", user->GetUserLogName()));
		packet_UMsgManager.SendPacket_UMsg_ServerMessageText(connection, "[新戰場補給] 您今天已經領取過簽到獎勵囉，請明天再來！");
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
		packet_UMsgManager.SendPacket_UMsg_ServerMessageText(connection, "[新戰場補給] 您今天已經領取過簽到獎勵囉，請明天再來！");
		return;
	}

	auto candidates = selectCandidateItems(user);
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
	if (wonDaily.itemID == 0 && !_config.dailyRewards.empty()) {
		wonDaily = _config.dailyRewards[0].item;
	}

	grantRewardItem(user, wonCandidate);
	grantRewardItem(user, wonDaily);

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	packet->WriteUInt8(Packet_ItemType::Roulette); // 8
	packet->WriteUInt8(1);                          // Action 1 (Claim result)

	// Won Candidate Item (13 bytes)
	// Field 1: itemID, Field 2: duration, Field 3: count, Field 4: enhance, Field 5: isEvent
	packet->WriteUInt32_LE(wonCandidate.itemID);
	packet->WriteUInt16_LE(wonCandidate.duration);
	packet->WriteUInt16_LE(wonCandidate.count);
	packet->WriteUInt32_LE(wonCandidate.enhance);
	packet->WriteUInt8(wonCandidate.isEvent);

	// Consecutive day reached (1 ~ 7)
	packet->WriteUInt8(newConsecutiveDays);

	// Won Daily ladder item (13 bytes)
	packet->WriteUInt32_LE(wonDaily.itemID);
	packet->WriteUInt16_LE(wonDaily.duration);
	packet->WriteUInt16_LE(wonDaily.count);
	packet->WriteUInt32_LE(wonDaily.enhance);
	packet->WriteUInt8(wonDaily.isEvent);

	packet->Send();

	// Clear cached candidate items so next claim opportunity will sample a fresh set
	ClearUserCandidates(userID);

	// Sync inventory and currency
	packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
	auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });

	std::string displayDaily = FormatRewardDisplay(wonDaily);
	std::string displayCand = FormatRewardDisplay(wonCandidate);

	if (_config.serverMessageEnabled) {
		std::string notice = format("[新戰場補給] 簽到成功！連續登入第 {} 天！獲得「{}」與「{}」！",
			newConsecutiveDays, displayDaily, displayCand);
		packet_UMsgManager.SendPacket_UMsg_ServerMessageText(connection, notice);
	}

	serverConsole.Print(PrefixType::Info, format("[ AttendanceManager ] User ({}) claimed Day {} attendance! Won '{}' and '{}'\n",
		user->GetUserLogName(), newConsecutiveDays, displayDaily, displayCand));
}
