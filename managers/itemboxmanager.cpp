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
#include <unordered_set>

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
	std::string resolvedPath = configPath.empty() ? "configs/itembox.json" : configPath;
	if (!std::filesystem::exists(resolvedPath)) {
		serverConsole.Print(PrefixType::Error, format("[ ItemBoxManager ] Config file not found: {}\n", resolvedPath));
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
			if (s.contains("BroadcastMinGrade")) {
				_settings.broadcastMinGrade = s.value("BroadcastMinGrade", 3);
			}
			else if (s.contains("BroadcastGoldGrade")) {
				_settings.broadcastMinGrade = s.value("BroadcastGoldGrade", true) ? 3 : 999;
			}
			if (s.contains("SkipOwnedPermanent")) {
				_settings.skipOwnedPermanent = s.value("SkipOwnedPermanent", true);
			}
			else if (s.contains("AllowOwnedPermanent")) {
				_settings.skipOwnedPermanent = !s.value("AllowOwnedPermanent", false);
			}
			if (s.contains("SkipPeriodIfPermanentOwned")) {
				_settings.skipPeriodIfPermanentOwned = s.value("SkipPeriodIfPermanentOwned", true);
			}
			else {
				_settings.skipPeriodIfPermanentOwned = _settings.skipOwnedPermanent;
			}
		}

		// 2. Parse Boxes
		_boxes.clear();
		if (root.contains("Boxes") && root["Boxes"].is_object()) {
			for (auto& [boxKey, boxVal] : root["Boxes"].items()) {
				unsigned short decoderID = (unsigned short)std::stoi(boxKey);
				ItemBoxConfig cfg;
				cfg.decoderID = decoderID;
				cfg.name = boxVal.value("Name", "");

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
						if (itemVal.contains("Broadcast")) {
							if (itemVal["Broadcast"].is_boolean()) {
								item.broadcast = itemVal["Broadcast"].get<bool>() ? 1 : 0;
							}
							else if (itemVal["Broadcast"].is_number()) {
								item.broadcast = (itemVal["Broadcast"].get<int>() != 0) ? 1 : 0;
							}
						}
						if (itemVal.contains("BonusPoints") && itemVal["BonusPoints"].is_number()) {
							item.bonusPoints = itemVal["BonusPoints"].get<unsigned long long>();
						}
						if (itemVal.contains("PackageItems") && itemVal["PackageItems"].is_array()) {
							for (const auto& pkgId : itemVal["PackageItems"]) {
								if (pkgId.is_number()) item.bundleItems.push_back(pkgId.get<unsigned short>());
							}
						}
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

const ItemBoxReward* ItemBoxManager::RollReward(const ItemBoxConfig& boxDef, const std::vector<InventoryItem>* userInventory) const {
	if (boxDef.items.empty()) {
		return nullptr;
	}

	std::unordered_set<unsigned short> permanentlyOwned;
	if (userInventory != nullptr) {
		for (const auto& it : *userInventory) {
			if (it.count > 0 && it.expirationTime == 0) {
				const auto* def = itemDefinitionManager.GetItem(it.itemID);
				if (def != nullptr && def->isPeriod) {
					permanentlyOwned.insert(it.itemID);
				}
				else if (def == nullptr) {
					permanentlyOwned.insert(it.itemID);
				}
			}
		}
	}

	auto isEligible = [&](const ItemBoxReward& item) -> bool {
		bool isUserPermOwned = (permanentlyOwned.count(item.itemID) > 0);
		if (isUserPermOwned) {
			const auto* def = itemDefinitionManager.GetItem(item.itemID);
			bool isPermReward = (item.duration == -1) || (item.duration == 0 && def != nullptr && def->isPeriod);
			bool isPeriodReward = (item.duration > 0);

			if (isPermReward && _settings.skipOwnedPermanent) {
				return false;
			}
			if (isPeriodReward && _settings.skipPeriodIfPermanentOwned) {
				return false;
			}
		}
		return true;
	};

	std::vector<const ItemBoxReward*> gradeCandidates[5]; // index 1..4
	for (const auto& item : boxDef.items) {
		int g = item.grade;
		if (g < 1 || g > 4) g = 1;
		if (isEligible(item)) {
			gradeCandidates[g].push_back(&item);
		}
	}

	double w4 = gradeCandidates[4].empty() ? 0.0 : boxDef.rates.grade4;
	double w3 = gradeCandidates[3].empty() ? 0.0 : boxDef.rates.grade3;
	double w2 = gradeCandidates[2].empty() ? 0.0 : boxDef.rates.grade2;
	double w1 = gradeCandidates[1].empty() ? 0.0 : boxDef.rates.grade1;

	double totalWeight = w4 + w3 + w2 + w1;

	thread_local std::mt19937_64 rng(std::random_device{}());

	if (totalWeight > 0.0) {
		std::uniform_real_distribution<double> dist(0.0, totalWeight);
		double roll = dist(rng);

		int selectedGrade = 1;
		if (w4 > 0.0 && roll < w4) {
			selectedGrade = 4;
		}
		else if (w3 > 0.0 && roll < (w4 + w3)) {
			selectedGrade = 3;
		}
		else if (w2 > 0.0 && roll < (w4 + w3 + w2)) {
			selectedGrade = 2;
		}
		else if (w1 > 0.0) {
			selectedGrade = 1;
		}
		else {
			for (int g = 4; g >= 1; --g) {
				if (!gradeCandidates[g].empty()) {
					selectedGrade = g;
					break;
				}
			}
		}

		const auto& pool = gradeCandidates[selectedGrade];
		if (!pool.empty()) {
			std::uniform_int_distribution<size_t> itemDist(0, pool.size() - 1);
			return pool[itemDist(rng)];
		}
	}

	// Fallback only if all items across all grades are ineligible (e.g. user owns entire box pool)
	std::vector<const ItemBoxReward*> allCandidates;
	for (const auto& item : boxDef.items) {
		allCandidates.push_back(&item);
	}
	if (!allCandidates.empty()) {
		std::uniform_int_distribution<size_t> itemDist(0, allCandidates.size() - 1);
		return allCandidates[itemDist(rng)];
	}

	return nullptr;
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

	// 2. Locate required C-Box item (All decoders require C-Box itemID 166 in this version)
	const unsigned short CBOX_ITEM_ID = 166;
	const InventoryItem* boxItem = nullptr;
	unsigned short totalCBoxCount = 0;
	unsigned short totalDecoderCount = 0;

	for (const auto& it : inv) {
		if (it.itemID == CBOX_ITEM_ID && it.count > 0) {
			totalCBoxCount += it.count;
			if (boxItem == nullptr || it.slotID == boxSlotID) {
				boxItem = &it;
				boxSlotID = it.slotID;
			}
		}
		if (it.itemID == decoderItem->itemID && it.count > 0) {
			totalDecoderCount += it.count;
		}
	}

	if (!boxItem || totalCBoxCount == 0) {
		serverConsole.Print(PrefixType::Warn, format("[ ItemBoxManager ] User ({}) lacks required C-Box (166) for decoder {}!\n",
			user->GetUserLogName(), boxDef->decoderID));
		SendPacket_Item_OpenBoxResult(connection, 0, 1);
		return false;
	}

	// 3. Roll reward (filters permanently owned items if configured)
	const ItemBoxReward* wonItem = RollReward(*boxDef, &inv);
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

	// 5. Consume C-Box item (166)
	bool boxDeleted = false;
	if (boxItem != nullptr) {
		if (!databaseManager.ConsumeUserInventoryItemBySlot(userID, boxSlotID, 1, boxDeleted)) {
			serverConsole.Print(PrefixType::Error, format("[ ItemBoxManager ] Failed to consume C-Box slot {} for user ({})\n", boxSlotID, user->GetUserLogName()));
		}
	}

	// Track delta inventory changes for high-performance microsecond synchronization (0 dropped frames)
	vector<InventoryItem> deltaUpdatedItems;
	vector<unsigned short> deletedSlots;

	if (decoderDeleted) {
		deletedSlots.push_back(decoderSlotID);
	}
	else {
		InventoryItem updatedDecoder = *decoderItem;
		updatedDecoder.count = (updatedDecoder.count > 1) ? (updatedDecoder.count - 1) : 0;
		deltaUpdatedItems.push_back(updatedDecoder);
	}

	if (boxDeleted) {
		deletedSlots.push_back(boxSlotID);
	}
	else if (boxItem != nullptr) {
		InventoryItem updatedBox = *boxItem;
		updatedBox.count = (updatedBox.count > 1) ? (updatedBox.count - 1) : 0;
		deltaUpdatedItems.push_back(updatedBox);
	}

	unsigned short remainingCBoxCount = (totalCBoxCount > 1) ? (totalCBoxCount - 1) : 0;
	unsigned short remainingDecoderCount = (totalDecoderCount > 1) ? (totalDecoderCount - 1) : 0;

	auto AddOrUpdateDelta = [&](const InventoryItem& item) {
		for (auto& d : deltaUpdatedItems) {
			if (d.slotID == item.slotID) {
				d = item;
				return;
			}
		}
		deltaUpdatedItems.push_back(item);
	};

	// 6. Grant reward to user
	unsigned short maxSlotID = 0;
	bool alreadyOwned = false;
	InventoryItem ownedItem{};
	for (const auto& it : inv) {
		if (it.slotID > maxSlotID) {
			maxSlotID = it.slotID;
		}
		if (it.itemID == wonItem->itemID) {
			alreadyOwned = true;
			ownedItem = it;
		}
	}

	time_t now = time(nullptr);

	// Check if this is a bundle / multi-item package
	const std::vector<unsigned short>& subItems = wonItem->bundleItems;
	unsigned long long bonusPts = wonItem->bonusPoints;

	if (!subItems.empty() || bonusPts > 0) {
		// Grant all sub-items in the bundle
		for (unsigned short subID : subItems) {
			const auto* sDef = itemDefinitionManager.GetItem(subID);
			bool sIsCount = (sDef != nullptr && !sDef->isPeriod);
			bool sIsPeriod = !sIsCount && ((wonItem->duration != 0) || (sDef != nullptr && sDef->isPeriod));
			bool sIsPerm = !sIsCount && (wonItem->duration == -1 || wonItem->duration == 0);

			bool sAlreadyOwned = false;
			InventoryItem sOwned{};
			for (const auto& it : inv) {
				if (it.itemID == subID) {
					sAlreadyOwned = true;
					sOwned = it;
					break;
				}
			}

			if (sIsCount || (!sIsPeriod && wonItem->duration == 0)) {
				if (sAlreadyOwned) {
					databaseManager.UpdateUserInventoryItemCount(userID, subID, 1);
					sOwned.count += 1;
					AddOrUpdateDelta(sOwned);
					if (subID == CBOX_ITEM_ID) {
						remainingCBoxCount += 1;
					}
				}
				else {
					InventoryItem newItem;
					newItem.slotID = ++maxSlotID;
					newItem.itemID = subID;
					newItem.count = 1;
					newItem.activeFlag = 1;
					newItem.inUse = false;
					newItem.obtainTime = (unsigned long)now;
					newItem.expirationTime = 0;
					newItem.enhanceBits = 0;
					databaseManager.AddUserInventoryItems(userID, { newItem });
					AddOrUpdateDelta(newItem);
					if (subID == CBOX_ITEM_ID) {
						remainingCBoxCount += 1;
					}
				}
			}
			else {
				if (sAlreadyOwned) {
					if (sOwned.expirationTime == 0) {
						// already perm
					}
					else if (sIsPerm) {
						databaseManager.UpdateUserInventoryExpiration(userID, subID, 0);
						sOwned.expirationTime = 0;
						AddOrUpdateDelta(sOwned);
					}
					else {
						unsigned long baseTime = (sOwned.expirationTime > (unsigned long)now) ? sOwned.expirationTime : (unsigned long)now;
						unsigned long newExp = baseTime + (unsigned long)wonItem->duration * 86400;
						databaseManager.UpdateUserInventoryExpiration(userID, subID, newExp);
						sOwned.expirationTime = newExp;
						AddOrUpdateDelta(sOwned);
					}
				}
				else {
					InventoryItem newItem;
					newItem.slotID = ++maxSlotID;
					newItem.itemID = subID;
					newItem.count = 1;
					newItem.activeFlag = 1;
					newItem.inUse = false;
					newItem.obtainTime = (unsigned long)now;
					newItem.expirationTime = sIsPerm ? 0 : (unsigned long)(now + (unsigned long)wonItem->duration * 86400);
					newItem.enhanceBits = 0;
					databaseManager.AddUserInventoryItems(userID, { newItem });
					AddOrUpdateDelta(newItem);
				}
			}
		}

		// Add bonus points if any (e.g. sozoPoint +55,000 Point)
		if (bonusPts > 0) {
			databaseManager.AddUserFunds(userID, true, bonusPts);
			auto updatedChar = user->GetUserCharacter(USERCHARACTER_FLAG_POINTS);
			if (updatedChar.result) {
				packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, updatedChar.userCharacter });
			}
		}
	}
	else {
		// Single-item grant
		const auto* itemDef = itemDefinitionManager.GetItem(wonItem->itemID);
		bool isCountItem = (itemDef != nullptr && !itemDef->isPeriod);
		bool isPeriodItem = !isCountItem && ((wonItem->duration != 0) || (itemDef != nullptr && itemDef->isPeriod));

		if (isCountItem || (!isPeriodItem && wonItem->duration == 0)) {
			// Count-based item
			if (alreadyOwned) {
				databaseManager.UpdateUserInventoryItemCount(userID, wonItem->itemID, wonItem->count);
				ownedItem.count += wonItem->count;
				AddOrUpdateDelta(ownedItem);
				if (wonItem->itemID == CBOX_ITEM_ID) {
					remainingCBoxCount += wonItem->count;
				}
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
				AddOrUpdateDelta(newItem);
				if (wonItem->itemID == CBOX_ITEM_ID) {
					remainingCBoxCount += wonItem->count;
				}
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
					ownedItem.expirationTime = 0;
					AddOrUpdateDelta(ownedItem);
				}
				else {
					// Extend expiration
					unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
					unsigned long newExp = baseTime + (unsigned long)wonItem->duration * 86400;
					databaseManager.UpdateUserInventoryExpiration(userID, wonItem->itemID, newExp);
					ownedItem.expirationTime = newExp;
					AddOrUpdateDelta(ownedItem);
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
				AddOrUpdateDelta(newItem);
			}
		}
	}

	// 7. Check canOpenAgain directly in memory without re-querying the database
	bool canOpenAgain = (remainingDecoderCount > 0) && (remainingCBoxCount > 0);

	serverConsole.Print(PrefixType::Info, format("[ ItemBoxManager ] User ({}) opened {} -> won itemID {} ({}) (Grade {}, Duration {}), canOpenAgain: {}\n",
		user->GetUserLogName(), boxDef->name, wonItem->itemID, wonItem->name, wonItem->grade, wonItem->duration, canOpenAgain ? 1 : 0));

	// 8. Send instant inventory delta update & CBox count (Packet 154 / 157)
	// Sending only modified slots (< 50 bytes) instead of full 266+ items eliminates main UI thread stall!
	packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, deletedSlots);
	packet_UpdateInfoManager.SendPacket_UserUpdateInfo_CBoxCount(connection, userID, remainingCBoxCount);

	// 9. Send Packet 78 result to client (grade 1=Normal, 2=Bronze/Silver, 3=Silver, 4=Gold fireworks)
	const auto* wonDef = itemDefinitionManager.GetItem(wonItem->itemID);
	bool wonIsCount = (wonDef != nullptr && !wonDef->isPeriod);
	short displayDuration = wonIsCount ? 0 : (short)wonItem->duration;
	SendPacket_Item_OpenBoxResult(connection, 1, (unsigned char)wonItem->grade, wonItem->itemID, displayDuration, wonItem->count);

	// 10. Broadcast announcement if won item qualifies (by default, grade >= broadcastMinGrade)
	bool shouldBroadcast = (wonItem->broadcast == 1) || (wonItem->broadcast == -1 && wonItem->grade >= _settings.broadcastMinGrade);
	if (shouldBroadcast) {
		string playerName = user->GetUserName();
		auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_NICKNAME);
		if (charResult.result && !charResult.userCharacter.nickName.empty()) {
			playerName = charResult.userCharacter.nickName;
		}

		bool isEvent = (boxDef->decoderID == 555 || boxDef->decoderID == 552 || boxDef->name.find("活動") != string::npos);
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
