#include "craftmanager.h"
#include "databasemanager.h"
#include "itemdefinitionmanager.h"
#include "packetmanager.h"
#include "packet_umsgmanager.h"
#include "packet_updateinfomanager.h"
#include "serverconsole.h"
#include "usermanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <random>
#include <format>
#include <ctime>

CraftManager craftManager;

bool CraftManager::Init() {
	return Reload();
}

bool CraftManager::Reload() {
	const std::vector<std::string> searchPaths = {
		"configs\\craft.json",
		"configs/craft.json",
		"craft.json",
		"..\\configs\\craft.json",
		"..\\..\\configs\\craft.json",
		"bin\\configs\\craft.json"
	};

	std::ifstream file;
	std::string resolvedPath;
	for (const auto& p : searchPaths) {
		file.open(p);
		if (file.is_open()) {
			resolvedPath = p;
			break;
		}
	}

	if (!file.is_open()) {
		serverConsole.Print(PrefixType::Error, "[ CraftManager ] Failed to locate craft.json in search paths!\n");
		return false;
	}

	try {
		nlohmann::json root;
		file >> root;
		file.close();

		// 1. Settings
		if (root.contains("Settings") && root["Settings"].is_object()) {
			const auto& s = root["Settings"];
			_settings.normalSuccessChance = s.value("NormalSuccessChance", 50);
			_settings.advancedSuccessChance = s.value("AdvancedSuccessChance", 80);
			_settings.luckyBonusChance = s.value("LuckyBonusChance", 20);
			_settings.normalCraftFee = s.value("NormalCraftFeePoint", 500UL);
			_settings.advancedCraftFee = s.value("AdvancedCraftFeePoint", 1000UL);
			_settings.disassembleFeePerItem = s.value("DisassembleFeePerItem", 50UL);
		}

		// 2. Durability Mapping
		_durabilityMapping.clear();
		if (root.contains("DurabilityMapping") && root["DurabilityMapping"].is_object()) {
			for (auto& [durKey, durVal] : root["DurabilityMapping"].items()) {
				unsigned short durID = (unsigned short)std::stoul(durKey);
				int days = durVal.get<int>();
				_durabilityMapping[durID] = days;
			}
		}

		// 3. Failure Recycle Items
		_normalRecycleItems.clear();
		_bonusRecycleItems.clear();
		if (root.contains("FailureRecycle") && root["FailureRecycle"].is_object()) {
			const auto& fr = root["FailureRecycle"];
			if (fr.contains("NormalRecycleItems") && fr["NormalRecycleItems"].is_array()) {
				for (const auto& it : fr["NormalRecycleItems"]) {
					RecycleItem r;
					r.itemID = it.value("ItemID", (unsigned short)5203);
					r.count = it.value("Count", (unsigned short)1);
					_normalRecycleItems.push_back(r);
				}
			}
			if (fr.contains("BonusRecycleItems") && fr["BonusRecycleItems"].is_array()) {
				for (const auto& it : fr["BonusRecycleItems"]) {
					RecycleItem r;
					r.itemID = it.value("ItemID", (unsigned short)5203);
					r.count = it.value("Count", (unsigned short)1);
					_bonusRecycleItems.push_back(r);
				}
			}
		}

		// 4. Blueprints
		_blueprints.clear();
		if (root.contains("Blueprints") && root["Blueprints"].is_object()) {
			for (auto& [bpKey, bpVal] : root["Blueprints"].items()) {
				unsigned short bpID = (unsigned short)std::stoul(bpKey);
				BlueprintConfig cfg;
				cfg.name = bpVal.value("Name", "");
				cfg.type = bpVal.value("Type", "");
				cfg.grade = bpVal.value("Grade", "Normal");
				cfg.successChance = bpVal.value("SuccessChance", (cfg.grade == "Advanced" ? _settings.advancedSuccessChance : _settings.normalSuccessChance));
				cfg.craftFee = bpVal.value("CraftFeePoint", (cfg.grade == "Advanced" ? _settings.advancedCraftFee : _settings.normalCraftFee));

				if (bpVal.contains("Items") && bpVal["Items"].is_array()) {
					for (const auto& itemVal : bpVal["Items"]) {
						CraftRewardItem item;
						item.itemID = itemVal.value("ItemID", (unsigned short)0);
						item.name = itemVal.value("Name", "");
						item.weight = itemVal.value("Weight", 10);
						if (item.itemID != 0) {
							cfg.items.push_back(item);
						}
					}
				}
				_blueprints[bpID] = cfg;
			}
		}

		serverConsole.Print(PrefixType::Info, format("[ CraftManager ] Successfully loaded {} blueprints from {}\n",
			_blueprints.size(), resolvedPath));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ CraftManager ] JSON parse error in craft.json: {}\n", e.what()));
		return false;
	}
}

const BlueprintConfig* CraftManager::GetBlueprintConfig(unsigned short bpID) const {
	auto it = _blueprints.find(bpID);
	if (it != _blueprints.end()) {
		return &it->second;
	}
	return nullptr;
}

int CraftManager::GetDurabilityDays(unsigned short durID) const {
	auto it = _durabilityMapping.find(durID);
	if (it != _durabilityMapping.end()) {
		return it->second;
	}
	switch (durID) {
		case 5101: return 1;
		case 5102: return 3;
		case 5103: return 10;
		case 5104: return 30;
		case 5105: return 90;
		case 5106: return 0; // Permanent
		default: return 1;
	}
}

unsigned long CraftManager::GetBlueprintFee(unsigned short bpID) const {
	auto it = _blueprints.find(bpID);
	if (it != _blueprints.end()) {
		if (it->second.craftFee > 0) return it->second.craftFee;
		return (it->second.grade == "Advanced") ? _settings.advancedCraftFee : _settings.normalCraftFee;
	}
	return _settings.normalCraftFee;
}

bool CraftManager::QueryCraftPreview(User* user, unsigned short bpSlotID, unsigned short durSlotID, unsigned short secSlotID1, unsigned short secSlotID2) {
	if (user == nullptr) {
		return false;
	}

	auto& connection = user->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	auto inv = user->GetUserInventory();
	const InventoryItem* bpItem = nullptr;
	const InventoryItem* durItem = nullptr;
	const InventoryItem* secItem1 = nullptr;
	const InventoryItem* secItem2 = nullptr;

	for (const auto& it : inv) {
		if (it.slotID == bpSlotID && it.count > 0) {
			bpItem = &it;
		}
		if (it.slotID == durSlotID && it.count > 0) {
			durItem = &it;
		}
		if (secSlotID1 != 0xFFFF && it.slotID == secSlotID1 && it.count > 0) {
			secItem1 = &it;
		}
		if (secSlotID2 != 0xFFFF && it.slotID == secSlotID2 && it.count > 0) {
			secItem2 = &it;
		}
	}

	if (bpItem == nullptr || durItem == nullptr) {
		sendCraftPreview(connection, 0, 0);
		return false;
	}

	const BlueprintConfig* bpCfg = GetBlueprintConfig(bpItem->itemID);
	if (bpCfg == nullptr) {
		sendCraftPreview(connection, 0, 0);
		return false;
	}

	unsigned long feePoint = GetBlueprintFee(bpItem->itemID);

	int finalChance = bpCfg->successChance;
	if ((secItem1 != nullptr && secItem1->itemID == 5201) ||
		(secItem2 != nullptr && secItem2->itemID == 5201)) {
		finalChance += _settings.luckyBonusChance;
	}

	if (finalChance > 100) finalChance = 100;
	if (finalChance < 1) finalChance = 1;

	// Success level: 0 (#CSO_SuccessLevel_0 很低) ~ 4 (很高)
	// 0: <= 20%, 1: <= 40%, 2: <= 60%, 3: <= 80%, 4: > 80%
	unsigned char level = 2; // Default 普通
	if (finalChance <= 20) {
		level = 0; // 很低
	}
	else if (finalChance <= 40) {
		level = 1; // 低
	}
	else if (finalChance <= 60) {
		level = 2; // 普通
	}
	else if (finalChance <= 80) {
		level = 3; // 高
	}
	else {
		level = 4; // 很高
	}

	serverConsole.Print(PrefixType::Info, format("[ CraftManager ] User ({}) preview craft bp={} dur={} fee={} -> chance={}% (level={})\n",
		user->GetUserLogName(), bpItem->itemID, durItem->itemID, feePoint, finalChance, (int)level));

	sendCraftPreview(connection, level, feePoint);
	return true;
}

bool CraftManager::CraftWeapon(User* user, unsigned short bpSlotID, unsigned short durSlotID, unsigned short secSlotID1, unsigned short secSlotID2) {
	if (user == nullptr) {
		return false;
	}

	auto& connection = user->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	unsigned long userID = user->GetUserID();
	auto inv = user->GetUserInventory();

	// 1. Locate Blueprint in inventory
	const InventoryItem* bpItem = nullptr;
	const InventoryItem* durItem = nullptr;
	const InventoryItem* secItem1 = nullptr;
	const InventoryItem* secItem2 = nullptr;

	for (const auto& it : inv) {
		if (it.slotID == bpSlotID && it.count > 0) {
			bpItem = &it;
		}
		if (it.slotID == durSlotID && it.count > 0) {
			durItem = &it;
		}
		if (secSlotID1 != 0xFFFF && it.slotID == secSlotID1 && it.count > 0) {
			secItem1 = &it;
		}
		if (secSlotID2 != 0xFFFF && it.slotID == secSlotID2 && it.count > 0) {
			secItem2 = &it;
		}
	}

	if (bpItem == nullptr || durItem == nullptr) {
		serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) attempted craft with invalid slots: bpSlot={}, durSlot={}\n",
			user->GetUserLogName(), bpSlotID, durSlotID));
		sendCraftResultFailure(connection, {});
		return false;
	}

	const BlueprintConfig* bpCfg = GetBlueprintConfig(bpItem->itemID);
	if (bpCfg == nullptr || bpCfg->items.empty()) {
		serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) blueprint {} has no valid craft config!\n",
			user->GetUserLogName(), bpItem->itemID));
		sendCraftResultFailure(connection, {});
		return false;
	}

	unsigned long feePoint = GetBlueprintFee(bpItem->itemID);
	if (feePoint > 0) {
		if (!databaseManager.DeductUserFunds(userID, true, feePoint)) {
			serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) has insufficient Point for craft: need {}\n",
				user->GetUserLogName(), feePoint));
			sendCraftResultFailure(connection, {});
			packet_UMsgManager.SendPacket_UMsg_SystemReply(connection, Packet_UMsgType::WarningMessage, "FAB_NOT_ENOUGH_POINT", {});
			return false;
		}
		auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
	}

	int durationDays = GetDurabilityDays(durItem->itemID);

	// Check extra auxiliary items
	bool hasLucky = false;
	bool hasRecycle = false;
	if (secItem1 != nullptr) {
		if (secItem1->itemID == 5201) hasLucky = true;
		if (secItem1->itemID == 5202) hasRecycle = true;
	}
	if (secItem2 != nullptr) {
		if (secItem2->itemID == 5201) hasLucky = true;
		if (secItem2->itemID == 5202) hasRecycle = true;
	}

	// Calculate success chance
	int finalChance = bpCfg->successChance;
	if (hasLucky) {
		finalChance += _settings.luckyBonusChance;
	}
	if (finalChance > 100) finalChance = 100;
	if (finalChance < 1) finalChance = 1;

	// Consume ingredients atomically
	std::vector<unsigned short> deletedSlots;
	bool deleted = false;
	databaseManager.ConsumeUserInventoryItemBySlot(userID, bpSlotID, 1, deleted);
	if (deleted) deletedSlots.push_back(bpSlotID);

	databaseManager.ConsumeUserInventoryItemBySlot(userID, durSlotID, 1, deleted);
	if (deleted) deletedSlots.push_back(durSlotID);

	if (secItem1 != nullptr) {
		databaseManager.ConsumeUserInventoryItemBySlot(userID, secSlotID1, 1, deleted);
		if (deleted) deletedSlots.push_back(secSlotID1);
	}
	if (secItem2 != nullptr) {
		databaseManager.ConsumeUserInventoryItemBySlot(userID, secSlotID2, 1, deleted);
		if (deleted) deletedSlots.push_back(secSlotID2);
	}

	// Roll RNG
	thread_local std::mt19937 rng(std::random_device{}());
	std::uniform_int_distribution<int> dist100(1, 100);
	int roll = dist100(rng);
	bool success = (roll <= finalChance);

	serverConsole.Print(PrefixType::Info, format("[ CraftManager ] User ({}) craft bp={} dur={} lucky={} recycle={} -> roll={}/{} -> {}\n",
		user->GetUserLogName(), bpItem->itemID, durItem->itemID, hasLucky, hasRecycle, roll, finalChance, success ? "SUCCESS" : "FAILURE"));

	if (success) {
		// Pick weapon based on weights
		int totalWeight = 0;
		for (const auto& wItem : bpCfg->items) {
			totalWeight += (wItem.weight > 0 ? wItem.weight : 1);
		}

		std::uniform_int_distribution<int> weightDist(1, totalWeight);
		int wRoll = weightDist(rng);
		int curWeight = 0;
		const CraftRewardItem* chosenWeapon = &bpCfg->items.front();
		for (const auto& wItem : bpCfg->items) {
			curWeight += (wItem.weight > 0 ? wItem.weight : 1);
			if (wRoll <= curWeight) {
				chosenWeapon = &wItem;
				break;
			}
		}

		unsigned short wonItemID = chosenWeapon->itemID;
		std::string weaponName = chosenWeapon->name;
		const auto* itemDef = itemDefinitionManager.GetItem(wonItemID);
		if (itemDef != nullptr && !itemDef->name.empty()) {
			weaponName = itemDef->name;
		}

		// Update or grant item in user inventory
		unsigned short maxSlotID = 0;
		bool alreadyOwned = false;
		InventoryItem ownedItem;
		for (const auto& it : user->GetUserInventory()) {
			if (it.slotID > maxSlotID) maxSlotID = it.slotID;
			if (it.itemID == wonItemID) {
				alreadyOwned = true;
				ownedItem = it;
			}
		}

		time_t now = time(nullptr);
		if (durationDays == 0) {
			// Permanent weapon
			if (alreadyOwned) {
				databaseManager.UpdateUserInventoryExpiration(userID, wonItemID, 0);
			}
			else {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = wonItemID;
				newItem.count = 1;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = 0;
				newItem.enhanceBits = 0;
				databaseManager.AddUserInventoryItems(userID, { newItem });
			}
		}
		else {
			// Period weapon
			unsigned long addSecs = (unsigned long)durationDays * 86400;
			if (alreadyOwned) {
				if (ownedItem.expirationTime == 0) {
					// Already permanent, keep permanent
				}
				else {
					unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
					databaseManager.UpdateUserInventoryExpiration(userID, wonItemID, baseTime + addSecs);
				}
			}
			else {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = wonItemID;
				newItem.count = 1;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = (unsigned long)now + addSecs;
				newItem.enhanceBits = 0;
				databaseManager.AddUserInventoryItems(userID, { newItem });
			}
		}

		// Refresh client inventory with deleted slots BEFORE triggering result dialog
		packetManager.SendPacket_Inventory(connection, user->GetUserInventory(), deletedSlots);

		// Reply Craft Result Success: Packet 78, Type 5, Status 1, ResultFlag 1
		sendCraftResultSuccess(connection, wonItemID, (unsigned short)durationDays);
		return true;
	}
	else {
		// Craft Failed: Only return recycling parts when user actually used auxiliary item 5202 (再生工法)
		// If 5202 was not used, returnedItemIDs is empty -> client displays standard "#CSO_WeaponAssem_Failure"
		// If 5202 was used, returnedItemIDs has items -> client displays "#CSO_WeaponAssem_Failure_Recycle"
		std::vector<unsigned short> returnedItemIDs;
		if (hasRecycle) {
			const auto& recyclePool = _bonusRecycleItems.empty() ? _normalRecycleItems : _bonusRecycleItems;

			unsigned short maxSlotID = 0;
			auto currentInv = user->GetUserInventory();
			for (const auto& it : currentInv) {
				if (it.slotID > maxSlotID) maxSlotID = it.slotID;
			}

			for (const auto& rItem : recyclePool) {
				returnedItemIDs.push_back(rItem.itemID);
				bool found = false;
				for (const auto& it : currentInv) {
					if (it.itemID == rItem.itemID) {
						found = true;
						databaseManager.UpdateUserInventoryItemCount(userID, rItem.itemID, rItem.count);
						break;
					}
				}
				if (!found) {
					InventoryItem newItem;
					newItem.slotID = ++maxSlotID;
					newItem.itemID = rItem.itemID;
					newItem.count = rItem.count;
					newItem.activeFlag = 1;
					newItem.inUse = false;
					newItem.obtainTime = (unsigned long)time(nullptr);
					newItem.expirationTime = 0;
					newItem.enhanceBits = 0;
					databaseManager.AddUserInventoryItems(userID, { newItem });
				}
			}
		}

		// Refresh client inventory with deleted slots BEFORE triggering result dialog
		packetManager.SendPacket_Inventory(connection, user->GetUserInventory(), deletedSlots);

		// Reply Craft Result Failure: Packet 78, Type 5, Status 1, ResultFlag 0
		sendCraftResultFailure(connection, returnedItemIDs);
		return true;
	}
}

bool CraftManager::DisassembleItems(User* user, const std::vector<unsigned short>& weaponSlots) {
	if (user == nullptr || weaponSlots.empty()) {
		return false;
	}

	auto& connection = user->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	unsigned long userID = user->GetUserID();
	auto inv = user->GetUserInventory();

	unsigned long disFee = (unsigned long)weaponSlots.size() * _settings.disassembleFeePerItem;
	if (disFee > 0) {
		if (!databaseManager.DeductUserFunds(userID, true, disFee)) {
			serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) has insufficient Point for disassemble: need {}\n",
				user->GetUserLogName(), disFee));
			sendDisassembleFailure(connection, "DIS_NOT_ENOUGH_POINT");
			packet_UMsgManager.SendPacket_UMsg_SystemReply(connection, Packet_UMsgType::WarningMessage, "DIS_NOT_ENOUGH_POINT", {});
			return false;
		}
		auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
	}

	std::vector<unsigned short> deletedSlots;
	std::unordered_map<unsigned short, unsigned short> rewardsToGrant; // ItemID -> Count

	thread_local std::mt19937 rng(std::random_device{}());

	for (unsigned short slotID : weaponSlots) {
		auto it = std::find_if(inv.begin(), inv.end(), [slotID](const InventoryItem& item) {
			return item.slotID == slotID && item.count > 0;
		});

		if (it == inv.end()) {
			continue;
		}

		const auto* itemDef = itemDefinitionManager.GetItem(it->itemID);
		if (itemDef == nullptr || !itemDef->CanDisassemble()) {
			serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) tried to disassemble non-disassemblable item {} (slot {})\n",
				user->GetUserLogName(), it->itemID, slotID));
			continue;
		}

		bool deleted = false;
		if (!databaseManager.ConsumeUserInventoryItemBySlot(userID, slotID, 1, deleted)) {
			continue;
		}
		deletedSlots.push_back(slotID);

		// Disassembly Yield Calculations
		bool isPermanent = (it->expirationTime == 0);
		unsigned char category = itemDef->category;

		// 1. Guaranteed Normal Enhancement Parts (5203)
		int partsCount = 1;
		if (isPermanent) {
			partsCount = 3 + (rng() % 3); // 3 ~ 5 parts
		}
		else {
			partsCount = 1 + (rng() % 2); // 1 ~ 2 parts
		}
		rewardsToGrant[5203] += (unsigned short)partsCount;

		// 2. Durability Materials (5101 ~ 5103)
		int durRoll = rng() % 100;
		if (isPermanent) {
			if (durRoll < 30) {
				rewardsToGrant[5103] += 1; // 10 days
			}
			else {
				rewardsToGrant[5102] += 1; // 3 days
			}
		}
		else {
			if (durRoll < 40) {
				rewardsToGrant[5101] += 1; // 1 day
			}
		}

		// 3. Bonus Rare Rewards for Permanent or Advanced Weapons
		if (isPermanent && (rng() % 100 < 25)) {
			rewardsToGrant[5204] += 1; // Advanced Enhancement Part (5204)
		}

		// 4. Normal Blueprint reward corresponding to weapon category
		if (rng() % 100 < 35) {
			unsigned short bpID = 5001; // Default Pistol
			if (category == 1) bpID = 5001;       // Pistol
			else if (category == 2) bpID = 5002;  // Shotgun
			else if (category == 3) bpID = 5003;  // SMG
			else if (category == 4) bpID = 5004;  // Rifle
			else if (category == 5) bpID = 5006;  // MG
			else if (category == 11) bpID = 5007; // Melee
			rewardsToGrant[bpID] += 1;
		}

		serverConsole.Print(PrefixType::Info, format("[ CraftManager ] User ({}) disassembled item {} ({}, perm={}) -> slot {}\n",
			user->GetUserLogName(), it->itemID, itemDef->name, isPermanent, slotID));
	}

	if (deletedSlots.empty()) {
		return false;
	}

	// Grant all accumulated materials
	unsigned short maxSlotID = 0;
	auto updatedInv = user->GetUserInventory();
	for (const auto& it : updatedInv) {
		if (it.slotID > maxSlotID) maxSlotID = it.slotID;
	}

	for (const auto& [itemID, count] : rewardsToGrant) {
		bool found = false;
		for (const auto& it : updatedInv) {
			if (it.itemID == itemID) {
				found = true;
				databaseManager.UpdateUserInventoryItemCount(userID, itemID, count);
				break;
			}
		}
		if (!found) {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = itemID;
			newItem.count = count;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)time(nullptr);
			newItem.expirationTime = 0;
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
		}
	}

	// 1. Send refreshed inventory with deleted slots FIRST
	packetManager.SendPacket_Inventory(connection, user->GetUserInventory(), deletedSlots);

	// 2. Send Disassemble Result packet: Packet 78, Type 4, Status 1 (Disassemble Success)
	unsigned short mainRewardItemID = 5203;
	unsigned short mainRewardCount = 1;
	if (!rewardsToGrant.empty()) {
		auto it = rewardsToGrant.find(5203);
		if (it != rewardsToGrant.end()) {
			mainRewardItemID = 5203;
			mainRewardCount = it->second;
		}
		else {
			mainRewardItemID = rewardsToGrant.begin()->first;
			mainRewardCount = rewardsToGrant.begin()->second;
		}
	}
	sendDisassembleResult(connection, mainRewardItemID, mainRewardCount);

	// 3. Pop up Disassemble Result dialog via BulletinNotice (Packet 65)
	std::string noticeTitle = "分解結果";
	std::string noticeMsg = "分解道具後獲得下列材料\n";
	for (const auto& [rItemID, rCount] : rewardsToGrant) {
		const auto* def = itemDefinitionManager.GetItem(rItemID);
		std::string rName = (def != nullptr && !def->name.empty()) ? def->name : ("道具 " + std::to_string(rItemID));
		noticeMsg += format("\n- {} x{}", rName, rCount);
	}
	packet_UMsgManager.SendPacket_UMsg_BulletinNotice(connection, 0, 1, noticeTitle, noticeMsg);

	return true;
}

void CraftManager::sendCraftPreview(TCPConnection::pointer connection, unsigned char successLevel, unsigned long feePoint) {
	if (connection == nullptr) {
		return;
	}

	auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (replyPacket != nullptr) {
		replyPacket->WriteUInt8(5); // Packet_ItemType = 5 (WeaponCraft)
		replyPacket->WriteUInt8(0); // status = 0 (Preview / Query response)
		replyPacket->WriteUInt8(successLevel); // 0~4 (#CSO_SuccessLevel_0 ~ 4)
		replyPacket->WriteUInt32_LE(feePoint); // fee Point
		replyPacket->Send();
	}
}

void CraftManager::sendCraftResultSuccess(TCPConnection::pointer connection, unsigned short itemID, unsigned short durationDays) {
	if (connection == nullptr) {
		return;
	}

	auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (replyPacket != nullptr) {
		replyPacket->WriteUInt8(5); // Packet_ItemType = 5 (WeaponCraft)
		replyPacket->WriteUInt8(1); // status = 1
		replyPacket->WriteUInt8(1); // resultFlag = 1 (Success)
		replyPacket->WriteUInt32_LE(itemID); // ItemID
		replyPacket->WriteUInt16_LE(durationDays); // Duration days (0 for permanent)
		replyPacket->WriteUInt16_LE(0); // padding
		replyPacket->Send();
	}
}

void CraftManager::sendCraftResultFailure(TCPConnection::pointer connection, const std::vector<unsigned short>& returnedItemIDs) {
	if (connection == nullptr) {
		return;
	}

	auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (replyPacket != nullptr) {
		replyPacket->WriteUInt8(5); // Packet_ItemType = 5 (WeaponCraft)
		replyPacket->WriteUInt8(1); // status = 1
		replyPacket->WriteUInt8(0); // resultFlag = 0 (Failure)
		replyPacket->WriteUInt8((unsigned char)returnedItemIDs.size());
		for (unsigned short id : returnedItemIDs) {
			replyPacket->WriteUInt16_LE(id);
		}
		replyPacket->WriteString(""); // message = ""
		replyPacket->Send();
	}
}

void CraftManager::sendDisassembleResult(TCPConnection::pointer connection, unsigned short itemID, unsigned short count) {
	if (connection == nullptr) {
		return;
	}

	auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (replyPacket != nullptr) {
		replyPacket->WriteUInt8(4); // Packet_ItemType = 4 (Disassemble)
		replyPacket->WriteUInt8(1); // status = 1 (Success)
		replyPacket->WriteUInt32_LE(itemID); // ItemID (4 bytes)
		replyPacket->WriteUInt16_LE(0); // 2 bytes
		replyPacket->WriteUInt16_LE(count); // count (2 bytes)
		replyPacket->WriteUInt8(0); // 1 byte padding
		replyPacket->Send();
	}
}

void CraftManager::sendDisassembleFailure(TCPConnection::pointer connection, const std::string& failReason) {
	if (connection == nullptr) {
		return;
	}

	auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (replyPacket != nullptr) {
		replyPacket->WriteUInt8(4); // Packet_ItemType = 4 (Disassemble)
		replyPacket->WriteUInt8(0); // status = 0 (Failure)
		replyPacket->WriteString(failReason);
		replyPacket->Send();
	}
}
