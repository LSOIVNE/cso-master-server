#include "itemboxmanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "databasemanager.h"
#include "packetmanager.h"
#include "packet_umsgmanager.h"
#include "packet_itemmanager.h"
#include "packet_updateinfomanager.h"
#include "itemdefinitionmanager.h"
#include "bingomanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <random>
#include <ctime>

using json = nlohmann::json;

ItemBoxManager itemBoxManager;

static void ReplaceAll(std::string& str, const std::string& from, const std::string& to) {
	if (from.empty()) return;
	size_t startPos = 0;
	while ((startPos = str.find(from, startPos)) != std::string::npos) {
		str.replace(startPos, from.length(), to);
		startPos += to.length();
	}
}

bool ItemBoxManager::Init(const std::string& configPath) {
	serverConsole.Print(PrefixType::Info, "[ ItemBoxManager ] Initializing decoder & itembox system...\n");
	return LoadConfig(configPath);
}

bool ItemBoxManager::LoadConfig(const std::string& configPath) {
	std::vector<std::string> searchPaths = {
		configPath,
		"configs/itembox.json",
		"configs\\itembox.json",
		"bin/configs/itembox.json",
		"bin\\configs\\itembox.json",
		"../configs/itembox.json"
	};

	std::string resolvedPath = "";
	for (const auto& path : searchPaths) {
		if (std::filesystem::exists(path)) {
			resolvedPath = path;
			break;
		}
	}

	if (resolvedPath.empty()) {
		serverConsole.Print(PrefixType::Error, format("[ ItemBoxManager ] Failed to locate itembox config file! (tried {})\n", configPath));
		return false;
	}

	try {
		std::ifstream file(resolvedPath);
		if (!file.is_open()) {
			serverConsole.Print(PrefixType::Error, format("[ ItemBoxManager ] Could not open file: {}\n", resolvedPath));
			return false;
		}

		json root = json::parse(file);

		// 1. Parse Settings
		if (root.contains("Settings") && root["Settings"].is_object()) {
			const auto& s = root["Settings"];
			_settings.broadcastGoldGrade = s.value("BroadcastGoldGrade", true);
			_settings.broadcastFormat = s.value("BroadcastFormat", "[伺服器訊息] ***** 恭喜[{PlayerName}]使用[{DecoderName}]獲得[{GradeName}][{ItemName}] *****");
			_settings.goldGradeName = s.value("GoldGradeName", "金獎勳");
			_settings.silverGradeName = s.value("SilverGradeName", "銀獎勳");
		}

		// 2. Parse Boxes
		_boxes.clear();
		if (root.contains("Boxes") && root["Boxes"].is_object()) {
			for (auto& [boxKey, boxVal] : root["Boxes"].items()) {
				unsigned short decoderID = (unsigned short)std::stoi(boxKey);
				ItemBoxConfig cfg;
				cfg.decoderID = decoderID;
				cfg.name = boxVal.value("Name", "");
				cfg.boxName = boxVal.value("BoxName", "");
				cfg.requireBoxID = boxVal.value("RequireBoxID", (unsigned short)0);

				if (boxVal.contains("Rates") && boxVal["Rates"].is_object()) {
					const auto& r = boxVal["Rates"];
					cfg.rates.grade1 = r.value("Grade1", 68.0);
					cfg.rates.grade2 = r.value("Grade2", 20.0);
					cfg.rates.grade3 = r.value("Grade3", 9.0);
					cfg.rates.grade4 = r.value("Grade4", 3.0);
				}

				if (boxVal.contains("Items") && boxVal["Items"].is_array()) {
					for (const auto& itemVal : boxVal["Items"]) {
						ItemBoxReward item;
						item.itemID = itemVal.value("ItemID", (unsigned short)0);
						item.name = itemVal.value("Name", "");
						item.duration = itemVal.value("Duration", -1);
						item.count = itemVal.value("Count", (unsigned short)1);
						item.grade = itemVal.value("Grade", 1);
						item.broadcast = itemVal.value("Broadcast", false);
						cfg.items.push_back(item);
					}
				}

				_boxes[decoderID] = cfg;
			}
		}

		serverConsole.Print(PrefixType::Info, format("[ ItemBoxManager ] Successfully loaded {} decoder box pool(s) from {}\n",
			_boxes.size(), resolvedPath));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ ItemBoxManager ] JSON parse error: {}\n", e.what()));
		return false;
	}
}

unsigned short ItemBoxManager::ResolveDecoderID(unsigned short id) const {
	if (_boxes.find(id) != _boxes.end()) {
		return id;
	}
	if (id == 555 && _boxes.find(173) != _boxes.end()) {
		return 173; // Event Normal Decoder -> Normal Decoder
	}
	if (id == 576 && _boxes.find(533) != _boxes.end()) {
		return 533; // Event B-Decoder -> B-Decoder
	}
	return id;
}

bool ItemBoxManager::HasBoxConfig(unsigned short decoderID) const {
	unsigned short resolved = ResolveDecoderID(decoderID);
	return _boxes.find(resolved) != _boxes.end();
}

const ItemBoxConfig* ItemBoxManager::GetBoxConfig(unsigned short decoderID) const {
	unsigned short resolved = ResolveDecoderID(decoderID);
	auto it = _boxes.find(resolved);
	if (it != _boxes.end()) {
		return &it->second;
	}
	return nullptr;
}

const ItemBoxReward* ItemBoxManager::RollReward(const ItemBoxConfig& boxDef) const {
	if (boxDef.items.empty()) {
		return nullptr;
	}

	thread_local std::mt19937_64 rng(std::random_device{}());

	double sumRates = boxDef.rates.grade1 + boxDef.rates.grade2 + boxDef.rates.grade3 + boxDef.rates.grade4;
	if (sumRates <= 0.0) {
		sumRates = 100.0;
	}

	std::uniform_real_distribution<double> dist(0.0, sumRates);
	double roll = dist(rng);

	int targetGrade = 1;
	// Grade 4 (Top Gold) checked first
	if (roll < boxDef.rates.grade4) {
		targetGrade = 4;
	}
	else if (roll < (boxDef.rates.grade4 + boxDef.rates.grade3)) {
		targetGrade = 3;
	}
	else if (roll < (boxDef.rates.grade4 + boxDef.rates.grade3 + boxDef.rates.grade2)) {
		targetGrade = 2;
	}
	else {
		targetGrade = 1;
	}

	std::vector<const ItemBoxReward*> candidates;
	for (const auto& item : boxDef.items) {
		if (item.grade == targetGrade) {
			candidates.push_back(&item);
		}
	}

	if (candidates.empty()) {
		for (const auto& item : boxDef.items) {
			candidates.push_back(&item);
		}
	}

	if (candidates.empty()) {
		return nullptr;
	}

	std::uniform_int_distribution<size_t> itemDist(0, candidates.size() - 1);
	return candidates[itemDist(rng)];
}

bool ItemBoxManager::OpenBox(User* user, unsigned short decoderSlotID, unsigned short boxSlotID, unsigned char flag) {
	if (user == nullptr) {
		return false;
	}

	auto& connection = user->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	unsigned long userID = user->GetUserID();
	vector<InventoryItem> inv = user->GetUserInventory();

	// 1. Locate decoder item in inventory
	const InventoryItem* decoderItem = nullptr;
	for (const auto& it : inv) {
		if (it.slotID == decoderSlotID && it.count > 0) {
			decoderItem = &it;
			break;
		}
	}

	// Fallback 1: check if boxSlotID was actually the decoder slot
	if (!decoderItem || !HasBoxConfig(decoderItem->itemID)) {
		for (const auto& it : inv) {
			if (it.slotID == boxSlotID && HasBoxConfig(it.itemID) && it.count > 0) {
				decoderItem = &it;
				decoderSlotID = it.slotID;
				boxSlotID = 0;
				break;
			}
		}
	}

	// Fallback 2: search inventory for any known decoder
	if (!decoderItem || !HasBoxConfig(decoderItem->itemID)) {
		for (const auto& it : inv) {
			if (HasBoxConfig(it.itemID) && it.count > 0) {
				decoderItem = &it;
				decoderSlotID = it.slotID;
				break;
			}
		}
	}

	if (!decoderItem || decoderItem->count == 0) {
		serverConsole.Print(PrefixType::Warn, format("[ ItemBoxManager ] User ({}) has no valid decoder item in slot {}!\n", user->GetUserLogName(), decoderSlotID));
		SendPacket_Item_OpenBoxResult(connection, 0, 1);
		return false;
	}

	const ItemBoxConfig* boxDef = GetBoxConfig(decoderItem->itemID);
	if (!boxDef) {
		serverConsole.Print(PrefixType::Warn, format("[ ItemBoxManager ] Decoder itemID {} has no box configuration!\n", decoderItem->itemID));
		SendPacket_Item_OpenBoxResult(connection, 0, 1);
		return false;
	}

	// 2. Locate required box item if needed
	const InventoryItem* boxItem = nullptr;
	if (boxDef->requireBoxID != 0) {
		for (const auto& it : inv) {
			if (it.slotID == boxSlotID && it.itemID == boxDef->requireBoxID && it.count > 0) {
				boxItem = &it;
				break;
			}
		}

		if (!boxItem) {
			for (const auto& it : inv) {
				if (it.itemID == boxDef->requireBoxID && it.count > 0) {
					boxItem = &it;
					boxSlotID = it.slotID;
					break;
				}
			}
		}

		if (!boxItem || boxItem->count == 0) {
			serverConsole.Print(PrefixType::Warn, format("[ ItemBoxManager ] User ({}) lacks required box itemID {} for decoder {}!\n",
				user->GetUserLogName(), boxDef->requireBoxID, boxDef->decoderID));
			SendPacket_Item_OpenBoxResult(connection, 0, 1);
			return false;
		}
	}

	// 3. Roll reward
	const ItemBoxReward* wonItem = RollReward(*boxDef);
	if (!wonItem) {
		serverConsole.Print(PrefixType::Error, format("[ ItemBoxManager ] Failed to roll reward for decoder {}!\n", boxDef->decoderID));
		SendPacket_Item_OpenBoxResult(connection, 0, 1);
		return false;
	}

	// 4. Consume decoder item
	bool decoderDeleted = false;
	if (!databaseManager.ConsumeUserInventoryItemBySlot(userID, decoderSlotID, 1, decoderDeleted)) {
		serverConsole.Print(PrefixType::Error, format("[ ItemBoxManager ] Failed to consume decoder slot {} for user ({})\n", decoderSlotID, user->GetUserLogName()));
		SendPacket_Item_OpenBoxResult(connection, 0, 1);
		return false;
	}

	// 5. Consume box item if required
	bool boxDeleted = false;
	if (boxDef->requireBoxID != 0 && boxItem != nullptr) {
		if (!databaseManager.ConsumeUserInventoryItemBySlot(userID, boxSlotID, 1, boxDeleted)) {
			serverConsole.Print(PrefixType::Error, format("[ ItemBoxManager ] Failed to consume box slot {} for user ({})\n", boxSlotID, user->GetUserLogName()));
		}
	}

	// 6. Grant reward to user
	unsigned short maxSlotID = 0;
	bool alreadyOwned = false;
	InventoryItem ownedItem;
	for (const auto& it : inv) {
		if (it.slotID > maxSlotID) {
			maxSlotID = it.slotID;
		}
		if (it.itemID == wonItem->itemID) {
			alreadyOwned = true;
			ownedItem = it;
		}
	}

	const auto* itemDef = itemDefinitionManager.GetItem(wonItem->itemID);
	bool isPeriodItem = (wonItem->duration != 0) || (itemDef != nullptr && itemDef->isPeriod);
	time_t now = time(nullptr);

	if (!isPeriodItem && wonItem->duration == 0) {
		// Count-based item
		if (alreadyOwned) {
			databaseManager.UpdateUserInventoryItemCount(userID, wonItem->itemID, wonItem->count);
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = wonItem->itemID;
			newItem.count = wonItem->count;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = 0;
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
		}
	}
	else {
		// Duration / permanent item
		bool isPermItem = (wonItem->duration == -1 || wonItem->duration == 0);
		if (alreadyOwned) {
			if (ownedItem.expirationTime == 0) {
				// Already permanent, keep permanent
			}
			else if (isPermItem) {
				// Upgrade to permanent
				databaseManager.UpdateUserInventoryExpiration(userID, wonItem->itemID, 0);
			}
			else {
				// Extend expiration
				unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
				unsigned long newExp = baseTime + (unsigned long)wonItem->duration * 86400;
				databaseManager.UpdateUserInventoryExpiration(userID, wonItem->itemID, newExp);
			}
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = wonItem->itemID;
			newItem.count = 1;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = isPermItem ? 0 : (unsigned long)(now + (unsigned long)wonItem->duration * 86400);
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
		}
	}

	// 7. Check canOpenAgain
	vector<InventoryItem> updatedInv = user->GetUserInventory();
	bool hasDecoderRemaining = false;
	bool hasBoxRemaining = (boxDef->requireBoxID == 0);

	for (const auto& it : updatedInv) {
		if (it.itemID == decoderItem->itemID && it.count > 0) {
			hasDecoderRemaining = true;
		}
		if (boxDef->requireBoxID != 0 && it.itemID == boxDef->requireBoxID && it.count > 0) {
			hasBoxRemaining = true;
		}
	}

	bool canOpenAgain = hasDecoderRemaining && hasBoxRemaining;

	serverConsole.Print(PrefixType::Info, format("[ ItemBoxManager ] User ({}) opened {} -> won itemID {} ({}) (Grade {}, Duration {}), canOpenAgain: {}\n",
		user->GetUserLogName(), boxDef->name, wonItem->itemID, wonItem->name, wonItem->grade, wonItem->duration, canOpenAgain ? 1 : 0));

	// 8. Send Packet 78 result to client (grade 1=Normal, 2=Bronze/Silver, 3=Silver, 4=Gold fireworks)
	SendPacket_Item_OpenBoxResult(connection, 1, (unsigned char)wonItem->grade, wonItem->itemID, (short)wonItem->duration, wonItem->count);

	// 9. Sync updated inventory (Packet 154 - SendPacket_Inventory automatically filters item 166 and syncs CBox count)
	vector<unsigned short> deletedSlots;
	if (decoderDeleted) deletedSlots.push_back(decoderSlotID);
	if (boxDeleted) deletedSlots.push_back(boxSlotID);
	packetManager.SendPacket_Inventory(connection, updatedInv, deletedSlots);

	// 10. Broadcast announcement if gold grade (4) or silver grade (3) or marked broadcast
	bool shouldBroadcast = wonItem->broadcast || (wonItem->grade >= 3 && _settings.broadcastGoldGrade);
	if (shouldBroadcast) {
		string playerName = user->GetUserName();
		auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_NICKNAME);
		if (charResult.result && !charResult.userCharacter.nickName.empty()) {
			playerName = charResult.userCharacter.nickName;
		}

		bool isEvent = (boxDef->decoderID == 555 || boxDef->name.find("活動") != string::npos);
		string keyPrefix = isEvent ? "EVENT_LOTTERY_WIN_" : "LOTTERY_WIN_";
		string gradeKey = (wonItem->grade >= 4) ? "SPREMIUM" : "PREMIUM";
		string fullKey = keyPrefix + gradeKey;

		vector<string> args;
		if (wonItem->duration > 0) {
			fullKey += "_PERIOD";
			args = { playerName, to_string(wonItem->itemID), to_string(wonItem->duration) };
		}
		else if (wonItem->count > 1) {
			fullKey += "_NUM";
			args = { playerName, to_string(wonItem->itemID), to_string(wonItem->count) };
		}
		else {
			args = { playerName, to_string(wonItem->itemID) };
		}

		// Broadcast official green system reply in chat to all online users (NO modal dialog, no focus disruption!)
		packet_UMsgManager.BroadcastSystemReply(Packet_UMsgType::SystemReply_Green, fullKey, args);

		serverConsole.Print(PrefixType::Info, format("[ ItemBoxManager ] Green Broadcast: key={}, player={}, itemID={} ({}), grade={}\n",
			fullKey, playerName, wonItem->itemID, wonItem->name, wonItem->grade));
	}

	// 11. Bingo integration: roll a bingo number and check lines
	bingoManager.OnBoxOpened(connection, user, boxDef->decoderID);

	return true;
}

void ItemBoxManager::SendPacket_Item_OpenBoxResult(TCPConnection::pointer connection, unsigned char result, unsigned char grade, unsigned int itemID, short duration, unsigned short count) {
	if (connection == nullptr) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (packet == nullptr) {
		return;
	}

	packet->WriteUInt8(Packet_ItemType::OpenBox); // 4
	packet->WriteUInt8(result); // result (1 = success, 0 = fail)
	if (result == 1) {
		packet->WriteUInt32_LE(itemID);
		packet->WriteUInt16_LE((unsigned short)(duration < 0 ? 0xFFFF : duration));
		packet->WriteUInt16_LE(count);
		packet->WriteUInt8(grade); // 1 = Normal, 2 = Bronze/Silver, 3 = Silver, 4 = Gold
	}
	else {
		packet->WriteString("");
	}

	packet->Send();
}
