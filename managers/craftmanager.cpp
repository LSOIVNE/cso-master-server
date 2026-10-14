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
	const std::string resolvedPath = "configs/craft.json";
	std::ifstream file(resolvedPath);
	if (!file.is_open()) {
		serverConsole.Print(PrefixType::Error, "[ CraftManager ] Failed to open configs/craft.json!\n");
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
			_settings.permanentDisplayMode = s.value("PermanentDisplayMode", "Permanent");
			_settings.singleRecycleBlueprintWeight = s.value("SingleRecycleBlueprintWeight", s.value("RecycleBlueprintWeight", 50));
			_settings.singleRecycleDurabilityWeight = s.value("SingleRecycleDurabilityWeight", s.value("RecycleDurabilityWeight", 50));
			_settings.longTermPermanentChance = s.value("LongTermPermanentChance", s.value("LongTermPermChance", 10));
		}

		// 2. Durability Mapping
		_durabilityMapping.clear();
		if (root.contains("DurabilityMapping") && root["DurabilityMapping"].is_object()) {
			for (auto& [durKey, durVal] : root["DurabilityMapping"].items()) {
				unsigned short durID = (unsigned short)std::stoul(durKey);
				DurabilityConfig dCfg;
				if (durVal.is_object()) {
					dCfg.minDays = durVal.value("MinDays", 1);
					dCfg.maxDays = durVal.value("MaxDays", dCfg.minDays);
					// 只有長期耐久材料 (5106) 有機會產出永久武器，其餘材料強制為 0
					if (durID == 5106) {
						dCfg.permChance = durVal.value("PermChance", _settings.longTermPermanentChance);
					}
					else {
						dCfg.permChance = 0;
					}
				}
				else if (durVal.is_number()) {
					int days = durVal.get<int>();
					dCfg.minDays = days;
					dCfg.maxDays = days;
					dCfg.permChance = (durID == 5106 ? _settings.longTermPermanentChance : 0);
				}
				_durabilityMapping[durID] = dCfg;
			}
		}

		// 3. Disassemble Settings
		auto parseCategory = [](const nlohmann::json& cJson, DisassembleCategoryConfig& target) {
			target.enabled = cJson.value("Enabled", target.enabled);
			target.dropChance = cJson.value("DropChance", cJson.value("CategoryWeight", target.dropChance));
			target.minAmount = cJson.value("MinAmount", target.minAmount);
			target.maxAmount = cJson.value("MaxAmount", target.maxAmount);
			if (cJson.contains("Items") && cJson["Items"].is_array()) {
				target.items.clear();
				for (const auto& it : cJson["Items"]) {
					DisassembleRewardEntry entry;
					entry.itemID = it.value("ItemID", (unsigned short)0);
					entry.name = it.value("Name", "");
					entry.weight = it.value("Weight", 10);
					if (entry.itemID != 0) {
						target.items.push_back(entry);
					}
				}
			}
		};

		// Default categories
		_disassembleCfg.durability = { true, 40, 1, 1, { { 5101, "1日耐久材料", 50 }, { 5102, "3日耐久材料", 30 }, { 5103, "10日耐久材料", 15 }, { 5104, "30日耐久材料", 5 } } };
		_disassembleCfg.blueprint = { true, 35, 1, 1, { { 5001, "一般手槍設計圖", 20 }, { 5002, "一般散彈槍設計圖", 20 }, { 5003, "一般衝鋒槍設計圖", 20 }, { 5004, "一般步槍設計圖", 20 }, { 5006, "一般機槍設計圖", 10 }, { 5007, "一般近戰武器設計圖", 10 } } };
		_disassembleCfg.scaling = { 10, 3, 30 };

		if (root.contains("DisassembleSettings") && root["DisassembleSettings"].is_object()) {
			const auto& ds = root["DisassembleSettings"];
			if (ds.contains("Scaling") && ds["Scaling"].is_object()) {
				const auto& sc = ds["Scaling"];
				_disassembleCfg.scaling.permanentShares = sc.value("PermanentShares", 10);
				_disassembleCfg.scaling.daysPerShare = sc.value("DaysPerShare", 3);
				_disassembleCfg.scaling.maxPeriodShares = sc.value("MaxPeriodShares", 30);
			}

			if (ds.contains("Categories") && ds["Categories"].is_object()) {
				const auto& cats = ds["Categories"];
				if (cats.contains("Durability") && cats["Durability"].is_object()) {
					parseCategory(cats["Durability"], _disassembleCfg.durability);
				}
				if (cats.contains("Blueprint") && cats["Blueprint"].is_object()) {
					parseCategory(cats["Blueprint"], _disassembleCfg.blueprint);
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

		// 5. Material Combine Settings
		_combineSettings = MaterialCombineSettings{};
		if (root.contains("MaterialCombineSettings") && root["MaterialCombineSettings"].is_object()) {
			const auto& mc = root["MaterialCombineSettings"];
			_combineSettings.feePoint = mc.value("FeePoint", 100UL);
			_combineSettings.successRate2Items = mc.value("SuccessRate2Items", 55);
			_combineSettings.successRate3Items = mc.value("SuccessRate3Items", 80);
			_combineSettings.failLowGradeChance = mc.value("FailLowGradeChance", 80);
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

int CraftManager::RollDurabilityDays(unsigned short durID) const {
	DurabilityConfig dCfg;
	auto it = _durabilityMapping.find(durID);
	if (it != _durabilityMapping.end()) {
		dCfg = it->second;
	}
	else {
		switch (durID) {
			case 5101: dCfg = { 1, 1, 0 }; break;
			case 5102: dCfg = { 1, 3, 0 }; break;
			case 5103: dCfg = { 3, 10, 0 }; break;
			case 5104: dCfg = { 10, 30, 0 }; break;
			case 5105: dCfg = { 30, 90, 0 }; break;
			case 5106: dCfg = { 72, 120, _settings.longTermPermanentChance }; break;
			default: dCfg = { 1, 1, 0 }; break;
		}
	}

	thread_local std::mt19937 rng(std::random_device{}());

	// 1. Check permanent chance first
	if (dCfg.permChance > 0) {
		std::uniform_int_distribution<int> permDist(1, 100);
		if (permDist(rng) <= dCfg.permChance) {
			return 0; // 0 represents permanent
		}
	}

	// 2. Roll random days between minDays and maxDays
	if (dCfg.maxDays <= dCfg.minDays) {
		return dCfg.minDays;
	}

	std::uniform_int_distribution<int> daysDist(dCfg.minDays, dCfg.maxDays);
	return daysDist(rng);
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

	for (const auto& it : inv) {
		if (it.slotID == bpSlotID && it.count > 0) {
			bpItem = &it;
		}
		if (it.slotID == durSlotID && it.count > 0) {
			durItem = &it;
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

	// Check auxiliary items for stacked lucky bonus
	std::unordered_map<unsigned short, unsigned short> requestedAuxSlots;
	if (secSlotID1 != 0xFFFF) requestedAuxSlots[secSlotID1] += 1;
	if (secSlotID2 != 0xFFFF) requestedAuxSlots[secSlotID2] += 1;

	int luckyCount = 0;
	for (const auto& [slotID, reqCount] : requestedAuxSlots) {
		for (const auto& it : inv) {
			if (it.slotID == slotID && it.count >= reqCount) {
				if (it.itemID == 5201) {
					luckyCount += reqCount;
				}
				break;
			}
		}
	}

	int finalChance = bpCfg->successChance + (luckyCount * _settings.luckyBonusChance);
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

	for (const auto& it : inv) {
		if (it.slotID == bpSlotID && it.count > 0) {
			bpItem = &it;
		}
		if (it.slotID == durSlotID && it.count > 0) {
			durItem = &it;
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
	bool pointDeducted = false;
	if (feePoint > 0) {
		if (!databaseManager.DeductUserFunds(userID, true, feePoint)) {
			serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) has insufficient Point for craft: need {}\n",
				user->GetUserLogName(), feePoint));
			packet_UMsgManager.SendPacket_UMsg_SystemReply(connection, Packet_UMsgType::WarningMessage, "FAB_NOT_ENOUGH_POINT", {});
			return false;
		}
		pointDeducted = true;
	}

	int durationDays = RollDurabilityDays(durItem->itemID);

	// Check extra auxiliary items (supports 1~2 lucky or recycle items, stacking properly)
	std::unordered_map<unsigned short, unsigned short> requestedAuxSlots;
	if (secSlotID1 != 0xFFFF) requestedAuxSlots[secSlotID1] += 1;
	if (secSlotID2 != 0xFFFF) requestedAuxSlots[secSlotID2] += 1;

	int luckyCount = 0;
	int recycleCount = 0;
	for (const auto& [slotID, reqCount] : requestedAuxSlots) {
		const InventoryItem* itm = nullptr;
		for (const auto& it : inv) {
			if (it.slotID == slotID && it.count >= reqCount) {
				itm = &it;
				break;
			}
		}
		if (itm == nullptr) {
			serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) has insufficient aux item on slot {}\n",
				user->GetUserLogName(), slotID));
			sendCraftResultFailure(connection, {});
			return false;
		}
		if (itm->itemID == 5201) {
			luckyCount += reqCount;
		}
		else if (itm->itemID == 5202) {
			recycleCount += reqCount;
		}
	}

	// Calculate success chance with stacked lucky bonus
	int finalChance = bpCfg->successChance + (luckyCount * _settings.luckyBonusChance);
	if (finalChance > 100) finalChance = 100;
	if (finalChance < 1) finalChance = 1;

	// Roll RNG
	thread_local std::mt19937 rng(std::random_device{}());
	std::uniform_int_distribution<int> dist100(1, 100);
	int roll = dist100(rng);
	bool success = (roll <= finalChance);

	serverConsole.Print(PrefixType::Info, format("[ CraftManager ] User ({}) craft bp={} dur={} luckyCount={} recycleCount={} -> roll={}/{} -> {} (durDays={})\n",
		user->GetUserLogName(), bpItem->itemID, durItem->itemID, luckyCount, recycleCount, roll, finalChance, success ? "SUCCESS" : "FAILURE", durationDays));

	// Build instant memory delta updates to eliminate client display latency
	std::vector<InventoryItem> deltaUpdatedItems;
	std::vector<unsigned short> deltaDeletedSlots;

	// Aux slots delta (consumed regardless of success/fail)
	for (const auto& [slotID, reqCount] : requestedAuxSlots) {
		for (const auto& it : inv) {
			if (it.slotID == slotID) {
				if (it.count > reqCount) {
					InventoryItem item = it;
					item.count -= reqCount;
					deltaUpdatedItems.push_back(item);
				}
				else {
					deltaDeletedSlots.push_back(slotID);
				}
				break;
			}
		}
	}

	if (success) {
		// Blueprint slot delta (consumed on success)
		if (bpItem->count > 1) {
			InventoryItem item = *bpItem;
			item.count -= 1;
			deltaUpdatedItems.push_back(item);
		}
		else {
			deltaDeletedSlots.push_back(bpSlotID);
		}

		// Durability slot delta (consumed on success)
		if (durItem->count > 1) {
			InventoryItem item = *durItem;
			item.count -= 1;
			deltaUpdatedItems.push_back(item);
		}
		else {
			deltaDeletedSlots.push_back(durSlotID);
		}

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
		for (const auto& it : inv) {
			if (it.slotID > maxSlotID) maxSlotID = it.slotID;
			if (it.itemID == wonItemID) {
				alreadyOwned = true;
				ownedItem = it;
			}
		}

		time_t now = time(nullptr);
		InventoryItem grantedItem;
		bool isNewItem = false;

		if (durationDays == 0) {
			// Permanent weapon
			if (alreadyOwned) {
				grantedItem = ownedItem;
				grantedItem.expirationTime = 0;
			}
			else {
				isNewItem = true;
				grantedItem.slotID = ++maxSlotID;
				grantedItem.itemID = wonItemID;
				grantedItem.count = 1;
				grantedItem.activeFlag = 1;
				grantedItem.inUse = false;
				grantedItem.obtainTime = (unsigned long)now;
				grantedItem.expirationTime = 0;
				grantedItem.enhanceBits = 0;
			}
		}
		else {
			// Period weapon
			unsigned long addSecs = (unsigned long)durationDays * 86400;
			if (alreadyOwned) {
				grantedItem = ownedItem;
				if (grantedItem.expirationTime != 0) {
					unsigned long baseTime = (grantedItem.expirationTime > (unsigned long)now) ? grantedItem.expirationTime : (unsigned long)now;
					grantedItem.expirationTime = baseTime + addSecs;
				}
			}
			else {
				isNewItem = true;
				grantedItem.slotID = ++maxSlotID;
				grantedItem.itemID = wonItemID;
				grantedItem.count = 1;
				grantedItem.activeFlag = 1;
				grantedItem.inUse = false;
				grantedItem.obtainTime = (unsigned long)now;
				grantedItem.expirationTime = (unsigned long)now + addSecs;
				grantedItem.enhanceBits = 0;
			}
		}

		deltaUpdatedItems.push_back(grantedItem);

		// Send instant delta update FIRST with microsecond latency!
		packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, deltaDeletedSlots);

		// Reply Craft Result Success: 0xFFFF represents permanent (#CSO_Permanent), 0 with count represents quantity unit (#CSO_Buying_Unit_Quantity)!
		unsigned short packetDurationDays = (durationDays == 0) ? 0xFFFF : (unsigned short)durationDays;
		unsigned short packetCount = 1;
		if (durationDays == 0 && _settings.permanentDisplayMode == "Count") {
			packetDurationDays = 0;
			packetCount = 1;
		}
		sendCraftResultSuccess(connection, wonItemID, packetDurationDays, packetCount);

		// Persist changes to MySQL asynchronously / sequentially
		bool deleted = false;
		databaseManager.ConsumeUserInventoryItemBySlot(userID, bpSlotID, 1, deleted);
		databaseManager.ConsumeUserInventoryItemBySlot(userID, durSlotID, 1, deleted);
		for (const auto& [slotID, reqCount] : requestedAuxSlots) {
			databaseManager.ConsumeUserInventoryItemBySlot(userID, slotID, reqCount, deleted);
		}

		if (durationDays == 0) {
			if (alreadyOwned) {
				databaseManager.UpdateUserInventoryExpiration(userID, wonItemID, 0);
			}
			else {
				databaseManager.AddUserInventoryItems(userID, { grantedItem });
			}
		}
		else {
			if (alreadyOwned) {
				if (ownedItem.expirationTime != 0) {
					databaseManager.UpdateUserInventoryExpiration(userID, wonItemID, grantedItem.expirationTime);
				}
			}
			else {
				databaseManager.AddUserInventoryItems(userID, { grantedItem });
			}
		}

		if (pointDeducted) {
			auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_POINTS);
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
		}

		return true;
	}
	else {
		// Craft Failed
		std::vector<unsigned short> returnedItemIDs;

		if (recycleCount >= 2) {
			// 放入 2 個再生工法：100% 同時返還設計圖與耐久材料（兩者皆受到保護，無損毀）！
			returnedItemIDs.push_back(bpSlotID);
			returnedItemIDs.push_back(durSlotID);

			// 下發 Delta：僅扣除輔助材料（再生工法 x2），設計圖與耐久材料保留
			packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, deltaDeletedSlots);

			// 下發再生工法返還結果（狀態 1，結果 0，返還清單包含設計圖與耐久材料）
			sendCraftResultFailure(connection, returnedItemIDs);

			// 持久化：僅扣除輔助材料，不扣除設計圖與耐久材料
			bool deleted = false;
			for (const auto& [slotID, reqCount] : requestedAuxSlots) {
				databaseManager.ConsumeUserInventoryItemBySlot(userID, slotID, reqCount, deleted);
			}
		}
		else if (recycleCount == 1) {
			// 放入 1 個再生工法：根據權重 (SingleRecycleBlueprintWeight vs SingleRecycleDurabilityWeight) 決定返還設計圖還是耐久材料
			int bpWeight = _settings.singleRecycleBlueprintWeight;
			int durWeight = _settings.singleRecycleDurabilityWeight;
			if (bpWeight < 0) bpWeight = 0;
			if (durWeight < 0) durWeight = 0;
			int totalWeight = bpWeight + durWeight;
			if (totalWeight <= 0) {
				bpWeight = 50;
				totalWeight = 100;
			}

			thread_local std::mt19937 rng(std::random_device{}());
			std::uniform_int_distribution<int> weightDist(1, totalWeight);
			bool keepBlueprint = (weightDist(rng) <= bpWeight);

			if (keepBlueprint) {
				// 保留設計圖：不扣除設計圖，耐久材料損毀扣除
				returnedItemIDs.push_back(bpSlotID);

				if (durItem->count > 1) {
					InventoryItem item = *durItem;
					item.count -= 1;
					deltaUpdatedItems.push_back(item);
				}
				else {
					deltaDeletedSlots.push_back(durSlotID);
				}
			}
			else {
				// 保留耐久材料：不扣除耐久材料，設計圖損毀扣除
				returnedItemIDs.push_back(durSlotID);

				if (bpItem->count > 1) {
					InventoryItem item = *bpItem;
					item.count -= 1;
					deltaUpdatedItems.push_back(item);
				}
				else {
					deltaDeletedSlots.push_back(bpSlotID);
				}
			}

			// 下發 Delta：包含輔助材料扣除以及損毀的那一個材料扣除
			packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, deltaDeletedSlots);

			// 下發再生工法返還結果（狀態 1，結果 0，返還清單僅包含被保留的材料 slotID）
			sendCraftResultFailure(connection, returnedItemIDs);

			// 持久化：扣除輔助材料與未受保護的損毀材料
			bool deleted = false;
			for (const auto& [slotID, reqCount] : requestedAuxSlots) {
				databaseManager.ConsumeUserInventoryItemBySlot(userID, slotID, reqCount, deleted);
			}

			if (keepBlueprint) {
				databaseManager.ConsumeUserInventoryItemBySlot(userID, durSlotID, 1, deleted);
			}
			else {
				databaseManager.ConsumeUserInventoryItemBySlot(userID, bpSlotID, 1, deleted);
			}
		}
		else {
			// 普通失敗（未添加再生工法）：材料全部損毀消耗
			if (bpItem->count > 1) {
				InventoryItem item = *bpItem;
				item.count -= 1;
				deltaUpdatedItems.push_back(item);
			}
			else {
				deltaDeletedSlots.push_back(bpSlotID);
			}

			if (durItem->count > 1) {
				InventoryItem item = *durItem;
				item.count -= 1;
				deltaUpdatedItems.push_back(item);
			}
			else {
				deltaDeletedSlots.push_back(durSlotID);
			}

			// 下發材料全部扣除的 Delta
			packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, deltaDeletedSlots);

			// 普通失敗（空返還清單）
			sendCraftResultFailure(connection, {});

			// 持久化：扣除所有消耗材料
			bool deleted = false;
			databaseManager.ConsumeUserInventoryItemBySlot(userID, bpSlotID, 1, deleted);
			databaseManager.ConsumeUserInventoryItemBySlot(userID, durSlotID, 1, deleted);
			for (const auto& [slotID, reqCount] : requestedAuxSlots) {
				databaseManager.ConsumeUserInventoryItemBySlot(userID, slotID, reqCount, deleted);
			}
		}

		if (pointDeducted) {
			auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_POINTS);
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
		}

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

	// 1. Check if user has sufficient Points for the max potential disassemble fee
	unsigned long maxDisFee = (unsigned long)weaponSlots.size() * _settings.disassembleFeePerItem;
	auto charPointsResult = user->GetUserCharacter(USERCHARACTER_FLAG_POINTS);
	if (maxDisFee > 0 && charPointsResult.userCharacter.points < maxDisFee) {
		serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) has insufficient Point for disassemble: need {}, has {}\n",
			user->GetUserLogName(), maxDisFee, charPointsResult.userCharacter.points));
		// Pop up standard Warning Dialog: "#CSO_Warning_DIS_NOT_ENOUGH_POINT" ("分解所需手續費不足")
		packet_UMsgManager.SendPacket_UMsg_SystemReply(connection, Packet_UMsgType::WarningMessage, "DIS_NOT_ENOUGH_POINT", {});
		return false;
	}

	thread_local std::mt19937 rng(std::random_device{}());

	auto rollCategoryReward = [](const DisassembleCategoryConfig& cat, std::unordered_map<unsigned short, unsigned short>& rewards, std::mt19937& gen) {
		if (!cat.enabled || cat.items.empty() || cat.dropChance <= 0) {
			return;
		}
		std::uniform_int_distribution<int> chanceDist(1, 100);
		if (chanceDist(gen) > cat.dropChance) {
			return;
		}

		int totalWeight = 0;
		for (const auto& entry : cat.items) {
			totalWeight += (entry.weight > 0 ? entry.weight : 1);
		}
		if (totalWeight <= 0) return;

		std::uniform_int_distribution<int> wDist(1, totalWeight);
		int roll = wDist(gen);
		int cur = 0;
		unsigned short chosenID = cat.items.front().itemID;
		for (const auto& entry : cat.items) {
			cur += (entry.weight > 0 ? entry.weight : 1);
			if (roll <= cur) {
				chosenID = entry.itemID;
				break;
			}
		}

		int minAmt = cat.minAmount > 0 ? cat.minAmount : 1;
		int maxAmt = cat.maxAmount >= minAmt ? cat.maxAmount : minAmt;
		int amt = minAmt;
		if (maxAmt > minAmt) {
			std::uniform_int_distribution<int> amtDist(minAmt, maxAmt);
			amt = amtDist(gen);
		}
		rewards[chosenID] += (unsigned short)amt;
	};

	std::vector<unsigned short> deletedSlots;
	std::unordered_map<unsigned short, unsigned short> rewardsToGrant; // ItemID -> Count
	size_t successfulItemsCount = 0;
	time_t now = time(nullptr);

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

		// Quantity items consume the ENTIRE stack (e.g. 28 Primary Treatment Tickets all 28 consumed at once)
		unsigned short consumeCount = it->count;
		bool deleted = false;
		if (!databaseManager.ConsumeUserInventoryItemBySlot(userID, slotID, consumeCount, deleted)) {
			continue;
		}
		deletedSlots.push_back(slotID);

		// Determine item type: Permanent, Period, or Quantity
		bool isPermanent = (itemDef->isPeriod && it->expirationTime == 0);
		bool isPeriod = (itemDef->isPeriod && it->expirationTime > 0);

		int shares = 1;

		if (isPermanent) {
			shares = _disassembleCfg.scaling.permanentShares;
		}
		else if (isPeriod) {
			int remainingDays = 1;
			if (it->expirationTime > (unsigned long)now) {
				remainingDays = (int)((it->expirationTime - (unsigned long)now + 86399) / 86400);
			}
			if (remainingDays < 1) remainingDays = 1;

			int daysPerShare = (_disassembleCfg.scaling.daysPerShare > 0) ? _disassembleCfg.scaling.daysPerShare : 3;
			shares = 1 + (remainingDays - 1) / daysPerShare;
			if (shares > _disassembleCfg.scaling.maxPeriodShares) {
				shares = _disassembleCfg.scaling.maxPeriodShares;
			}
		}
		else {
			// Quantity type item
			int count = (int)consumeCount;
			if (count < 1) count = 1;
			shares = count;
		}

		if (shares < 1) shares = 1;

		serverConsole.Print(PrefixType::Info, format("[ CraftManager ] User ({}) disassembling {} ({}, count={}, perm={}, period={}, shares={})\n",
			user->GetUserLogName(), it->itemID, itemDef->name, consumeCount, isPermanent, isPeriod, shares));

		successfulItemsCount++;
		for (int s = 0; s < shares; ++s) {
			rollCategoryReward(_disassembleCfg.durability, rewardsToGrant, rng);
			rollCategoryReward(_disassembleCfg.blueprint, rewardsToGrant, rng);
		}
	}

	if (deletedSlots.empty()) {
		return false;
	}

	// Guaranteed reward: If random roll yielded no items, grant at least 1 durability material
	if (rewardsToGrant.empty()) {
		if (!_disassembleCfg.durability.items.empty()) {
			std::uniform_int_distribution<size_t> dDist(0, _disassembleCfg.durability.items.size() - 1);
			rewardsToGrant[_disassembleCfg.durability.items[dDist(rng)].itemID] = 1;
		}
		else if (!_disassembleCfg.blueprint.items.empty()) {
			std::uniform_int_distribution<size_t> bDist(0, _disassembleCfg.blueprint.items.size() - 1);
			rewardsToGrant[_disassembleCfg.blueprint.items[bDist(rng)].itemID] = 1;
		}
	}

	// Deduct fee ONLY for successful items
	unsigned long actualFee = (unsigned long)successfulItemsCount * _settings.disassembleFeePerItem;
	if (actualFee > 0) {
		databaseManager.DeductUserFunds(userID, true, actualFee);
		auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
	}

	// Grant all accumulated materials
	unsigned short maxSlotID = 0;
	auto updatedInv = user->GetUserInventory();
	for (const auto& it : updatedInv) {
		if (it.slotID > maxSlotID) maxSlotID = it.slotID;
	}

	std::vector<InventoryItem> deltaUpdatedItems;

	for (const auto& [itemID, count] : rewardsToGrant) {
		bool found = false;
		for (auto& it : updatedInv) {
			if (it.itemID == itemID) {
				found = true;
				databaseManager.UpdateUserInventoryItemCount(userID, itemID, count);
				it.count += count;
				deltaUpdatedItems.push_back(it);
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
			deltaUpdatedItems.push_back(newItem);
			updatedInv.push_back(newItem);
		}
	}

	// 1. Send refreshed inventory with deleted slots FIRST (Delta 增量同步)
	packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, deletedSlots);

	// 2. Pop up Disassemble Result dialog via ItemAwardNotice (Packet 67 Type 60)
	std::vector<AwardItem> awardItems;
	for (const auto& [rItemID, rCount] : rewardsToGrant) {
		AwardItem item;
		item.itemID = rItemID;
		item.count = rCount;
		item.duration = 0; // 0 for count unit ("個")
		item.flags = 0;
		awardItems.push_back(item);
	}
	packet_UMsgManager.SendPacket_UMsg_ItemAwardNotice(connection, awardItems, "Dismantle_TITLE", "Dismantle_MSG");

	serverConsole.Print(PrefixType::Info, format("[ CraftManager ] User ({}) successfully disassembled {} items (fee: {} points). Awarded {} reward types.\n",
		user->GetUserLogName(), successfulItemsCount, actualFee, awardItems.size()));

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

void CraftManager::sendCraftResultSuccess(TCPConnection::pointer connection, unsigned short itemID, unsigned short durationDays, unsigned short count) {
	if (connection == nullptr) {
		return;
	}

	auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (replyPacket != nullptr) {
		replyPacket->WriteUInt8(5); // Packet_ItemType = 5 (WeaponCraft)
		replyPacket->WriteUInt8(1); // status = 1
		replyPacket->WriteUInt8(1); // resultFlag = 1 (Success)
		replyPacket->WriteUInt32_LE(itemID); // ItemID
		replyPacket->WriteUInt16_LE(durationDays); // Duration days (0xFFFF for permanent, 0 for count unit)
		replyPacket->WriteUInt16_LE(count); // Count (used when days <= 0 to display "%d個")
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

bool CraftManager::AssembleMaterials(User* user, const std::vector<unsigned short>& slots) {
	if (user == nullptr) {
		return false;
	}

	auto& connection = user->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	// 1. 槽位數量必須在 2 ~ 3 之間
	if (slots.size() < 2 || slots.size() > 3) {
		serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) invalid material assembly slot count: {}\n",
			user->GetUserLogName(), slots.size()));
		return false;
	}

	unsigned long userID = user->GetUserID();
	auto inv = user->GetUserInventory();

	// 2. 統計每個 slotID 請求消耗的數量（支援同一個堆疊槽位多次投入）
	std::unordered_map<unsigned short, unsigned short> requestedSlots;
	for (unsigned short slotID : slots) {
		requestedSlots[slotID]++;
	}

	// 3. 驗證玩家背包中槽位物品存在且數量充足
	for (const auto& [slotID, reqCount] : requestedSlots) {
		auto it = std::find_if(inv.begin(), inv.end(), [slotID](const InventoryItem& item) {
			return item.slotID == slotID;
		});
		if (it == inv.end() || it->count < reqCount) {
			serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) has insufficient item in slot {}: req {}, has {}\n",
				user->GetUserLogName(), slotID, reqCount, (it != inv.end() ? it->count : 0)));
			return false;
		}
	}

	// 4. 手續費驗證（預設 100 Points）
	unsigned long fee = _combineSettings.feePoint;
	auto charPointsResult = user->GetUserCharacter(USERCHARACTER_FLAG_POINTS);
	if (charPointsResult.userCharacter.points < fee) {
		serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) has insufficient Point for material assemble: need {}, has {}\n",
			user->GetUserLogName(), fee, charPointsResult.userCharacter.points));
		// 原版提示框："#CSO_Warning_COMB_NOT_ENOUGH_POINT"
		packet_UMsgManager.SendPacket_UMsg_SystemReply(connection, Packet_UMsgType::WarningMessage, "COMB_NOT_ENOUGH_POINT", {});
		return false;
	}

	// 5. 展開所有投入材料的 ItemID
	std::vector<unsigned short> inputItemIDs;
	for (unsigned short slotID : slots) {
		auto it = std::find_if(inv.begin(), inv.end(), [slotID](const InventoryItem& item) {
			return item.slotID == slotID;
		});
		if (it != inv.end()) {
			inputItemIDs.push_back(it->itemID);
		}
	}

	if (inputItemIDs.size() < 2) {
		return false;
	}

	// 6. 類別同質性檢驗（1: 耐久材料 5101~5106, 2: 設計圖 5001~5014）
	auto getCategory = [](unsigned short id) -> int {
		if (id >= 5101 && id <= 5106) return 1; // Durability
		if (id >= 5001 && id <= 5014) return 2; // Blueprint
		return 0; // Other
	};

	int category = getCategory(inputItemIDs[0]);
	if (category == 0) {
		serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) put non-combinable item {}\n",
			user->GetUserLogName(), inputItemIDs[0]));
		return false;
	}

	for (size_t i = 1; i < inputItemIDs.size(); ++i) {
		if (getCategory(inputItemIDs[i]) != category) {
			serverConsole.Print(PrefixType::Warn, format("[ CraftManager ] User ({}) mixed material categories in assembly\n",
				user->GetUserLogName()));
			return false;
		}
	}

	// 7. 頂級材料檢查（選項 1：下發 COMB_NOTHING_BETTER，不扣點數與材料）
	bool allTopTier = true;
	if (category == 1) { // 耐久材料：全為 5106 (長期耐久材料)
		for (unsigned short id : inputItemIDs) {
			if (id != 5106) {
				allTopTier = false;
				break;
			}
		}
	}
	else if (category == 2) { // 設計圖：全為高級設計圖 (5008 ~ 5014)
		for (unsigned short id : inputItemIDs) {
			if (id < 5008 || id > 5014) {
				allTopTier = false;
				break;
			}
		}
	}

	if (allTopTier) {
		serverConsole.Print(PrefixType::Info, format("[ CraftManager ] User ({}) attempted combining all top-tier materials. Sending COMB_NOTHING_BETTER.\n",
			user->GetUserLogName()));
		packet_UMsgManager.SendPacket_UMsg_SystemReply(connection, Packet_UMsgType::WarningMessage, "COMB_NOTHING_BETTER", {});
		return false;
	}

	// 8. 成功率判定
	int successRate = (slots.size() >= 3) ? _combineSettings.successRate3Items : _combineSettings.successRate2Items;
	thread_local std::mt19937 rng(std::random_device{}());
	std::uniform_int_distribution<int> rateDist(1, 100);
	bool isSuccess = (rateDist(rng) <= successRate);

	// 9. 計算產出結果材料 ItemID
	unsigned short grantedItemID = 0;

	if (category == 1) {
		// 耐久材料 (5101 ~ 5106)
		int maxGrade = 1;
		int minGrade = 6;
		for (unsigned short id : inputItemIDs) {
			int g = (id - 5101) + 1;
			if (g > maxGrade) maxGrade = g;
			if (g < minGrade) minGrade = g;
		}

		if (isSuccess) {
			// 成功：比投入材料最高等級高 1 級（上限 Grade 6: 5106）
			int targetGrade = std::min(6, maxGrade + 1);
			grantedItemID = (unsigned short)(5101 + targetGrade - 1);
		}
		else {
			// 失敗（保底）：依設定機率退回投入材料中的最低階，其餘則從投入材料中隨機退回 1 個
			std::uniform_int_distribution<int> failDist(1, 100);
			if (failDist(rng) <= _combineSettings.failLowGradeChance) {
				grantedItemID = (unsigned short)(5101 + minGrade - 1);
			}
			else {
				std::uniform_int_distribution<size_t> pickDist(0, inputItemIDs.size() - 1);
				grantedItemID = inputItemIDs[pickDist(rng)];
			}
		}
	}
	else if (category == 2) {
		// 設計圖 (5001 ~ 5007 一般, 5008 ~ 5014 高級)
		if (isSuccess) {
			// 成功：依投入材料種類加權隨機抽選一種，並轉為高級版本
			std::vector<unsigned short> targetAdvCandidates;
			for (unsigned short id : inputItemIDs) {
				if (id >= 5001 && id <= 5007) {
					targetAdvCandidates.push_back(id + 7); // 轉高級
				}
				else if (id >= 5008 && id <= 5014) {
					targetAdvCandidates.push_back(id); // 已是高級
				}
			}
			std::uniform_int_distribution<size_t> candDist(0, targetAdvCandidates.size() - 1);
			grantedItemID = targetAdvCandidates[candDist(rng)];
		}
		else {
			// 失敗（保底）：優先退回投入的一般設計圖之一；若無一般設計圖則從投入材料中隨機退回
			std::vector<unsigned short> norCandidates;
			for (unsigned short id : inputItemIDs) {
				if (id >= 5001 && id <= 5007) {
					norCandidates.push_back(id);
				}
			}
			if (!norCandidates.empty()) {
				std::uniform_int_distribution<size_t> norDist(0, norCandidates.size() - 1);
				grantedItemID = norCandidates[norDist(rng)];
			}
			else {
				std::uniform_int_distribution<size_t> anyDist(0, inputItemIDs.size() - 1);
				grantedItemID = inputItemIDs[anyDist(rng)];
			}
		}
	}

	serverConsole.Print(PrefixType::Info, format("[ CraftManager ] User ({}) material assembly: count={}, isSuccess={}, resultItemID={}\n",
		user->GetUserLogName(), slots.size(), isSuccess, grantedItemID));

	// 10. 扣除手續費
	if (fee > 0) {
		databaseManager.DeductUserFunds(userID, true, fee);
		auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
	}

	// 11. 扣除投入材料
	std::vector<unsigned short> deletedSlots;
	std::vector<InventoryItem> deltaUpdatedItems;
	for (const auto& [slotID, reqCount] : requestedSlots) {
		bool deleted = false;
		databaseManager.ConsumeUserInventoryItemBySlot(userID, slotID, reqCount, deleted);
		if (deleted) {
			deletedSlots.push_back(slotID);
		}
		else {
			auto it = std::find_if(inv.begin(), inv.end(), [slotID](const InventoryItem& x) { return x.slotID == slotID; });
			if (it != inv.end()) {
				InventoryItem updatedMat = *it;
				updatedMat.count = (updatedMat.count > reqCount) ? (updatedMat.count - reqCount) : 0;
				deltaUpdatedItems.push_back(updatedMat);
			}
		}
	}

	// 12. 發放產出的材料道具（數量 1 個）
	auto updatedInv = user->GetUserInventory();
	unsigned short maxSlotID = 0;
	bool found = false;
	InventoryItem foundItem{};
	for (const auto& it : updatedInv) {
		if (it.slotID > maxSlotID) maxSlotID = it.slotID;
		if (it.itemID == grantedItemID) {
			found = true;
			foundItem = it;
		}
	}

	if (found) {
		databaseManager.UpdateUserInventoryItemCount(userID, grantedItemID, 1);
		foundItem.count += 1;
		bool inDelta = false;
		for (auto& d : deltaUpdatedItems) {
			if (d.slotID == foundItem.slotID) {
				d.count = foundItem.count;
				inDelta = true;
				break;
			}
		}
		if (!inDelta) {
			deltaUpdatedItems.push_back(foundItem);
		}
	}
	else {
		InventoryItem newItem;
		newItem.slotID = ++maxSlotID;
		newItem.itemID = grantedItemID;
		newItem.count = 1;
		newItem.activeFlag = 1;
		newItem.inUse = false;
		newItem.obtainTime = (unsigned long)time(nullptr);
		newItem.expirationTime = 0;
		newItem.enhanceBits = 0;
		databaseManager.AddUserInventoryItems(userID, { newItem });
		deltaUpdatedItems.push_back(newItem);
	}

	// 13. 刷新並同步玩家背包 (Delta 增量同步)
	packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, deletedSlots);

	// 14. 下發材料組合結果面板封包（isSuccess 成功升階時下發 1 播放 Assemble.bik 動畫；失敗保底時下發 0 跳過動畫）
	unsigned char resultFlag = isSuccess ? 1 : 0;
	sendMaterialAssemResult(connection, resultFlag, grantedItemID, 1);

	return true;
}

void CraftManager::sendMaterialAssemResult(TCPConnection::pointer connection, unsigned char resultFlag, unsigned short itemID, unsigned short count) {
	if (connection == nullptr) {
		return;
	}

	auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (replyPacket != nullptr) {
		replyPacket->WriteUInt8(6); // Packet_ItemType = 6 (MaterialAssem)
		replyPacket->WriteUInt8(resultFlag); // 1 = Success
		replyPacket->WriteUInt32_LE(itemID); // ItemID
		replyPacket->WriteUInt16_LE(count); // Count = 1
		replyPacket->WriteUInt16_LE(0); // DurationDays = 0 (數量型道具)
		replyPacket->Send();
	}
}

