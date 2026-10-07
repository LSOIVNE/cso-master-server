#include "enhancemanager.h"
#include "databasemanager.h"
#include "packetmanager.h"
#include "packet_umsgmanager.h"
#include "serverconsole.h"
#include "usermanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <random>
#include <format>
#include <algorithm>

EnhanceManager enhanceManager;

bool EnhanceManager::Init() {
	return Reload();
}

bool EnhanceManager::Reload() {
	const std::vector<std::string> searchPaths = {
		"configs\\enhance.json",
		"configs/enhance.json",
		"enhance.json",
		"..\\configs\\enhance.json",
		"..\\..\\configs\\enhance.json",
		"bin\\configs\\enhance.json"
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
		serverConsole.Print(PrefixType::Error, "[ EnhanceManager ] Failed to locate enhance.json in search paths!\n");
		return false;
	}

	try {
		nlohmann::json root;
		file >> root;
		file.close();

		// 1. Settings
		_settings = EnhanceSettings();
		if (root.contains("Settings") && root["Settings"].is_object()) {
			const auto& s = root["Settings"];
			_settings.normalPartMaxLevel = s.value("NormalPartMaxLevel", (uint8_t)5);
			_settings.maxTotalLevel = s.value("MaxTotalLevel", (uint8_t)8);
			int legacyDownChance = s.value("LevelDownChance", 0);
			_settings.normalPartLevelDownChance = s.value("NormalPartLevelDownChance", legacyDownChance);
			_settings.advancedPartLevelDownChance = s.value("AdvancedPartLevelDownChance", legacyDownChance);

			if (s.contains("NormalPartSuccessRates") && s["NormalPartSuccessRates"].is_array()) {
				_settings.normalPartSuccessRates.clear();
				for (const auto& v : s["NormalPartSuccessRates"]) {
					_settings.normalPartSuccessRates.push_back(v.get<int>());
				}
			}
			if (s.contains("AdvancedPartSuccessRates") && s["AdvancedPartSuccessRates"].is_array()) {
				_settings.advancedPartSuccessRates.clear();
				for (const auto& v : s["AdvancedPartSuccessRates"]) {
					_settings.advancedPartSuccessRates.push_back(v.get<int>());
				}
			}

			// 失敗返還完美屬性強化材料配置
			auto parseRefundWeights = [&](const nlohmann::json& rw) {
				if (rw.contains("Damage")) _settings.failureRefundWeights.damage = rw["Damage"].get<int>();
				else if (rw.contains("5212")) _settings.failureRefundWeights.damage = rw["5212"].get<int>();

				if (rw.contains("Weight")) _settings.failureRefundWeights.weight = rw["Weight"].get<int>();
				else if (rw.contains("5213")) _settings.failureRefundWeights.weight = rw["5213"].get<int>();

				if (rw.contains("Accuracy")) _settings.failureRefundWeights.accuracy = rw["Accuracy"].get<int>();
				else if (rw.contains("5214")) _settings.failureRefundWeights.accuracy = rw["5214"].get<int>();

				if (rw.contains("FireRate")) _settings.failureRefundWeights.fireRate = rw["FireRate"].get<int>();
				else if (rw.contains("Firerate")) _settings.failureRefundWeights.fireRate = rw["Firerate"].get<int>();
				else if (rw.contains("5215")) _settings.failureRefundWeights.fireRate = rw["5215"].get<int>();

				if (rw.contains("KickBack")) _settings.failureRefundWeights.kickback = rw["KickBack"].get<int>();
				else if (rw.contains("Kickback")) _settings.failureRefundWeights.kickback = rw["Kickback"].get<int>();
				else if (rw.contains("5216")) _settings.failureRefundWeights.kickback = rw["5216"].get<int>();

				if (rw.contains("Ammo")) _settings.failureRefundWeights.ammo = rw["Ammo"].get<int>();
				else if (rw.contains("5217")) _settings.failureRefundWeights.ammo = rw["5217"].get<int>();
			};

			if (s.contains("FailureRefund") && s["FailureRefund"].is_object()) {
				const auto& fr = s["FailureRefund"];
				_settings.failureRefundEnabled = fr.value("Enabled", true);
				_settings.failureRefundCount = fr.value("Count", 1);
				if (fr.contains("Weights") && fr["Weights"].is_object()) {
					parseRefundWeights(fr["Weights"]);
				}
			}
			else if (s.contains("RefundMaterial") && s["RefundMaterial"].is_object()) {
				const auto& fr = s["RefundMaterial"];
				_settings.failureRefundEnabled = fr.value("Enabled", true);
				_settings.failureRefundCount = fr.value("Count", 1);
				if (fr.contains("Weights") && fr["Weights"].is_object()) {
					parseRefundWeights(fr["Weights"]);
				}
			}

			// 同時支援平面鍵名 (Flat keys)
			if (s.contains("FailureRefundEnabled")) {
				_settings.failureRefundEnabled = s.value("FailureRefundEnabled", true);
			}
			else if (s.contains("EnableFailureRefund")) {
				_settings.failureRefundEnabled = s.value("EnableFailureRefund", true);
			}
			else if (s.contains("RefundMaterialEnabled")) {
				_settings.failureRefundEnabled = s.value("RefundMaterialEnabled", true);
			}

			if (s.contains("FailureRefundCount")) {
				_settings.failureRefundCount = s.value("FailureRefundCount", 1);
			}
			else if (s.contains("RefundMaterialCount")) {
				_settings.failureRefundCount = s.value("RefundMaterialCount", 1);
			}

			if (s.contains("FailureRefundWeights") && s["FailureRefundWeights"].is_object()) {
				parseRefundWeights(s["FailureRefundWeights"]);
			}
			else if (s.contains("RefundMaterialWeights") && s["RefundMaterialWeights"].is_object()) {
				parseRefundWeights(s["RefundMaterialWeights"]);
			}
		}

		// 2. Weapons
		_weapons.clear();
		if (root.contains("Weapons") && root["Weapons"].is_object()) {
			for (auto& [wKey, wVal] : root["Weapons"].items()) {
				unsigned short itemID = (unsigned short)std::stoul(wKey);
				WeaponEnhanceLimit limit;
				limit.name = wVal.value("Name", "");
				limit.accuracy = wVal.value("Accuracy", (uint8_t)0);
				limit.damage = wVal.value("Damage", (uint8_t)0);
				limit.kickback = wVal.value("KickBack", (uint8_t)0);
				limit.weight = wVal.value("Weight", (uint8_t)0);
				limit.fireRate = wVal.value("FireRate", (uint8_t)0);
				limit.ammo = wVal.value("Ammo", (uint8_t)0);
				if (wVal.contains("StatWeights") && wVal["StatWeights"].is_object()) {
					const auto& sw = wVal["StatWeights"];
					limit.hasCustomStatWeights = true;
					limit.statWeights.accuracy = sw.value("Accuracy", 100);
					limit.statWeights.damage = sw.value("Damage", 100);
					limit.statWeights.kickback = sw.value("Kickback", sw.value("KickBack", 100));
					limit.statWeights.weight = sw.value("Weight", 100);
					limit.statWeights.fireRate = sw.value("FireRate", 100);
					limit.statWeights.ammo = sw.value("Ammo", 100);
				}
				_weapons[itemID] = limit;
			}
		}

		serverConsole.Print(PrefixType::Info, format("[ EnhanceManager ] Successfully loaded {} enhanceable weapons from {}\n",
			_weapons.size(), resolvedPath));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ EnhanceManager ] JSON parse error in enhance.json: {}\n", e.what()));
		return false;
	}
}

const WeaponEnhanceLimit* EnhanceManager::GetWeaponLimit(unsigned short itemID) const {
	auto it = _weapons.find(itemID);
	if (it != _weapons.end()) {
		return &it->second;
	}
	return nullptr;
}

bool EnhanceManager::IsEnhanceableWeapon(unsigned short itemID) const {
	return _weapons.find(itemID) != _weapons.end();
}

bool EnhanceManager::HandleEnhance(User* user, unsigned short weaponSlotID, unsigned short materialSlotID) {
	if (user == nullptr) {
		return false;
	}

	auto& connection = user->GetConnection();
	if (connection == nullptr) {
		return false;
	}

	if (weaponSlotID == materialSlotID) {
		return false;
	}

	unsigned long userID = user->GetUserID();
	auto inv = user->GetUserInventory();

	// 1. 尋找武器槽位物品
	auto wIt = std::find_if(inv.begin(), inv.end(), [weaponSlotID](const InventoryItem& item) {
		return item.slotID == weaponSlotID && item.count > 0;
	});

	// 2. 尋找材料槽位物品
	auto mIt = std::find_if(inv.begin(), inv.end(), [materialSlotID](const InventoryItem& item) {
		return item.slotID == materialSlotID && item.count > 0;
	});

	if (wIt == inv.end() || mIt == inv.end()) {
		serverConsole.Print(PrefixType::Warn, format("[ EnhanceManager ] User ({}) invalid slots: weaponSlot={}, matSlot={}\n",
			user->GetUserLogName(), weaponSlotID, materialSlotID));
		return false;
	}

	const auto* limit = GetWeaponLimit(wIt->itemID);
	if (limit == nullptr) {
		serverConsole.Print(PrefixType::Warn, format("[ EnhanceManager ] User ({}) attempted enhancing non-enhanceable weapon {}\n",
			user->GetUserLogName(), wIt->itemID));
		SendEnhanceResult(connection, 4, weaponSlotID, wIt->enhanceBits, 0, "");
		return false;
	}

	unsigned short materialID = mIt->itemID;
	WeaponEnhanceStats currentStats = WeaponEnhanceStats::FromBits(wIt->enhanceBits);
	uint8_t currentTotalLevel = currentStats.GetTotalLevel();

	serverConsole.Print(PrefixType::Info, format("[ EnhanceManager ] User ({}) enhance weapon {} (slot {}, lv={}) with mat {} (slot {})\n",
		user->GetUserLogName(), wIt->itemID, weaponSlotID, currentTotalLevel, materialID, materialSlotID));

	// 3. 處理全能力初始化零件 (5205: EnhAnti)
	if (materialID == 5205) {
		if (currentTotalLevel == 0) {
			serverConsole.Print(PrefixType::Warn, format("[ EnhanceManager ] User ({}) attempted reset already level 0 weapon\n",
				user->GetUserLogName()));
			SendEnhanceResult(connection, 4, weaponSlotID, 0, 0, "");
			return false;
		}

		// 消耗 1 個初始化零件
		bool deleted = false;
		databaseManager.ConsumeUserInventoryItemBySlot(userID, materialSlotID, 1, deleted);

		// 重置武器為 0
		databaseManager.UpdateUserInventoryItemEnhance(userID, weaponSlotID, 0);

		// 刷新背包
		std::vector<unsigned short> deletedSlots;
		if (deleted) deletedSlots.push_back(materialSlotID);
		packetManager.SendPacket_Inventory(connection, user->GetUserInventory(), deletedSlots);

		// 回覆 Status 3 (全能力初始化成功)
		SendEnhanceResult(connection, 3, weaponSlotID, 0, 0, "");
		return true;
	}

	// 4. 處理單項能力還原零件 (5206 ~ 5211)
	if (materialID >= 5206 && materialID <= 5211) {
		int statIndex = 0;
		switch (materialID) {
			case 5206: statIndex = 1; break; // Damage
			case 5207: statIndex = 3; break; // Weight
			case 5208: statIndex = 0; break; // Accuracy
			case 5209: statIndex = 4; break; // FireRate
			case 5210: statIndex = 2; break; // KickBack
			case 5211: statIndex = 5; break; // Ammo
		}

		uint8_t currentStatVal = currentStats.GetStatByIndex(statIndex);
		if (currentStatVal == 0) {
			serverConsole.Print(PrefixType::Warn, format("[ EnhanceManager ] User ({}) attempted restoring stat {} which is already 0\n",
				user->GetUserLogName(), statIndex));
			SendEnhanceResult(connection, 4, weaponSlotID, wIt->enhanceBits, (unsigned char)statIndex, "");
			return false;
		}

		// 消耗 1 個還原零件
		bool deleted = false;
		databaseManager.ConsumeUserInventoryItemBySlot(userID, materialSlotID, 1, deleted);

		// 扣除 1 點該屬性
		currentStats.SetStatByIndex(statIndex, currentStatVal - 1);
		uint32_t newBits = currentStats.ToBits();
		databaseManager.UpdateUserInventoryItemEnhance(userID, weaponSlotID, newBits);

		// 刷新背包
		std::vector<unsigned short> deletedSlots;
		if (deleted) deletedSlots.push_back(materialSlotID);
		packetManager.SendPacket_Inventory(connection, user->GetUserInventory(), deletedSlots);

		// 回覆 Status 2 (單項能力還原 -1)
		SendEnhanceResult(connection, 2, weaponSlotID, newBits, (unsigned char)statIndex, "");
		return true;
	}

	// 5. 處理完美屬性強化零件 (5218 ~ 5223: 100% 強化特定屬性 +1)
	if (materialID >= 5218 && materialID <= 5223) {
		int targetStat = 0;
		switch (materialID) {
			case 5218: targetStat = 1; break; // Damage (傷害)
			case 5219: targetStat = 3; break; // Weight (重量)
			case 5220: targetStat = 0; break; // Accuracy (命中率)
			case 5221: targetStat = 4; break; // FireRate (連射速度)
			case 5222: targetStat = 2; break; // KickBack (後座力)
			case 5223: targetStat = 5; break; // Ammo (載彈量)
		}

		// 總等級上限檢查 (+8)
		if (currentTotalLevel >= _settings.maxTotalLevel) {
			serverConsole.Print(PrefixType::Warn, format("[ EnhanceManager ] User ({}) attempted perfect enhance on weapon lv {} which reached max level {}\n",
				user->GetUserLogName(), currentTotalLevel, _settings.maxTotalLevel));
			SendEnhanceResult(connection, 4, weaponSlotID, wIt->enhanceBits, (unsigned char)targetStat, "");
			return false;
		}

		// 該項屬性上限檢查
		uint8_t currentStatVal = currentStats.GetStatByIndex(targetStat);
		uint8_t maxStatLimit = limit->GetLimitByIndex(targetStat);
		if (currentStatVal >= maxStatLimit) {
			serverConsole.Print(PrefixType::Warn, format("[ EnhanceManager ] User ({}) attempted perfect enhance on stat {} which already reached limit ({}) for weapon {}\n",
				user->GetUserLogName(), targetStat, maxStatLimit, wIt->itemID));
			SendEnhanceResult(connection, 4, weaponSlotID, wIt->enhanceBits, (unsigned char)targetStat, "");
			return false;
		}

		// 消耗 1 個完美強化零件
		bool deleted = false;
		databaseManager.ConsumeUserInventoryItemBySlot(userID, materialSlotID, 1, deleted);
		std::vector<unsigned short> deletedSlots;
		if (deleted) deletedSlots.push_back(materialSlotID);

		// 該屬性 +1
		currentStats.SetStatByIndex(targetStat, currentStatVal + 1);
		uint32_t newBits = currentStats.ToBits();

		databaseManager.UpdateUserInventoryItemEnhance(userID, weaponSlotID, newBits);
		packetManager.SendPacket_Inventory(connection, user->GetUserInventory(), deletedSlots);

		serverConsole.Print(PrefixType::Info, format("[ EnhanceManager ] User ({}) PERFECT ENHANCE SUCCESS: weapon {} stat {} +1, newBits={:#x}\n",
			user->GetUserLogName(), wIt->itemID, targetStat, newBits));

		// 回覆 Status 1 (成功 +1，播放 Assemble.bik 動畫、成功音效與展示特定屬性提升介面)
		SendEnhanceResult(connection, 1, weaponSlotID, newBits, (unsigned char)targetStat, "");
		return true;
	}

	// 6. 處理強化零件 (5203 一般強化零件 / 5204 特殊強化零件)
	if (materialID == 5203 || materialID == 5204) {
		bool isNormalPart = (materialID == 5203);

		// 等級上限檢查
		if (isNormalPart && currentTotalLevel >= _settings.normalPartMaxLevel) {
			serverConsole.Print(PrefixType::Warn, format("[ EnhanceManager ] Weapon lv {} exceeds normal part max level {}\n",
				currentTotalLevel, _settings.normalPartMaxLevel));
			SendEnhanceResult(connection, 4, weaponSlotID, wIt->enhanceBits, 0, "");
			return false;
		}

		if (currentTotalLevel >= _settings.maxTotalLevel) {
			serverConsole.Print(PrefixType::Warn, format("[ EnhanceManager ] Weapon already at max level {}\n",
				_settings.maxTotalLevel));
			SendEnhanceResult(connection, 4, weaponSlotID, wIt->enhanceBits, 0, "");
			return false;
		}

		// 尋找尚未達到上限的屬性候選名單
		std::vector<int> availableStats;
		for (int i = 0; i < 6; ++i) {
			if (currentStats.GetStatByIndex(i) < limit->GetLimitByIndex(i)) {
				availableStats.push_back(i);
			}
		}

		if (availableStats.empty()) {
			serverConsole.Print(PrefixType::Warn, format("[ EnhanceManager ] All stats already reached limit for weapon {}\n",
				wIt->itemID));
			SendEnhanceResult(connection, 4, weaponSlotID, wIt->enhanceBits, 0, "");
			return false;
		}

		// 計算成功率
		int successChance = 50;
		if (isNormalPart) {
			if (currentTotalLevel < _settings.normalPartSuccessRates.size()) {
				successChance = _settings.normalPartSuccessRates[currentTotalLevel];
			}
			else if (!_settings.normalPartSuccessRates.empty()) {
				successChance = _settings.normalPartSuccessRates.back();
			}
		}
		else {
			if (currentTotalLevel < _settings.advancedPartSuccessRates.size()) {
				successChance = _settings.advancedPartSuccessRates[currentTotalLevel];
			}
			else if (!_settings.advancedPartSuccessRates.empty()) {
				successChance = _settings.advancedPartSuccessRates.back();
			}
		}

		thread_local std::mt19937 rng(std::random_device{}());
		std::uniform_int_distribution<int> rollDist(1, 100);
		bool isSuccess = (rollDist(rng) <= successChance);

		// 消耗 1 個強化零件
		bool deleted = false;
		databaseManager.ConsumeUserInventoryItemBySlot(userID, materialSlotID, 1, deleted);
		std::vector<unsigned short> deletedSlots;
		if (deleted) deletedSlots.push_back(materialSlotID);

		if (isSuccess) {
			// 成功：抽選 1 個未滿屬性 +1 (若武器有自訂權重則依權重加權抽選，否則均等隨機)
			int chosenStat = availableStats[0];
			if (limit->hasCustomStatWeights) {
				int totalWeight = 0;
				for (int statIdx : availableStats) {
					totalWeight += std::max(0, limit->statWeights.GetWeightByIndex(statIdx));
				}

				if (totalWeight > 0) {
					std::uniform_int_distribution<int> weightDist(1, totalWeight);
					int rollWeight = weightDist(rng);
					int currentAcc = 0;
					for (int statIdx : availableStats) {
						currentAcc += std::max(0, limit->statWeights.GetWeightByIndex(statIdx));
						if (rollWeight <= currentAcc) {
							chosenStat = statIdx;
							break;
						}
					}
				}
				else {
					std::uniform_int_distribution<size_t> statDist(0, availableStats.size() - 1);
					chosenStat = availableStats[statDist(rng)];
				}
			}
			else {
				// 未自訂權重的武器：未滿屬性完全均等純隨機
				std::uniform_int_distribution<size_t> statDist(0, availableStats.size() - 1);
				chosenStat = availableStats[statDist(rng)];
			}

			currentStats.SetStatByIndex(chosenStat, currentStats.GetStatByIndex(chosenStat) + 1);
			uint32_t newBits = currentStats.ToBits();

			databaseManager.UpdateUserInventoryItemEnhance(userID, weaponSlotID, newBits);
			packetManager.SendPacket_Inventory(connection, user->GetUserInventory(), deletedSlots);

			serverConsole.Print(PrefixType::Info, format("[ EnhanceManager ] User ({}) enhance SUCCESS: stat {} +1, newBits={:#x}\n",
				user->GetUserLogName(), chosenStat, newBits));

			// 回覆 Status 1 (成功 +1，播放 Assemble.bik 與成功音效)
			SendEnhanceResult(connection, 1, weaponSlotID, newBits, (unsigned char)chosenStat, "");
			return true;
		}
		else {
			// 判定降級幾率 (NormalPart 與 AdvancedPart 分別配置)
			int downChance = isNormalPart ? _settings.normalPartLevelDownChance : _settings.advancedPartLevelDownChance;
			// 官方設定：精密強化零件在 +1 至 +5 絕不降級，+6 及以上 (即強化前 currentTotalLevel >= 5) 才可能降級
			if (!isNormalPart && currentTotalLevel < 5) {
				downChance = 0;
			}

			bool isLevelDown = false;
			int downStat = 0;
			uint32_t newBits = currentStats.ToBits();

			if (downChance > 0 && currentTotalLevel > 0) {
				std::uniform_int_distribution<int> downDist(1, 100);
				if (downDist(rng) <= downChance) {
					std::vector<int> upgradedStats;
					for (int i = 0; i < 6; ++i) {
						if (currentStats.GetStatByIndex(i) > 0) {
							upgradedStats.push_back(i);
						}
					}
					if (!upgradedStats.empty()) {
						std::uniform_int_distribution<size_t> uDist(0, upgradedStats.size() - 1);
						downStat = upgradedStats[uDist(rng)];
						currentStats.SetStatByIndex(downStat, currentStats.GetStatByIndex(downStat) - 1);
						newBits = currentStats.ToBits();
						databaseManager.UpdateUserInventoryItemEnhance(userID, weaponSlotID, newBits);
						isLevelDown = true;
					}
				}
			}

			// 5204 精密強化零件失敗返還完美屬性強化材料 (5212~5217)
			unsigned short refundMatID = 0;
			int refundCount = _settings.failureRefundCount;
			if (!isNormalPart && _settings.failureRefundEnabled && refundCount > 0) {
				const std::vector<unsigned short> candidates = { 5212, 5213, 5214, 5215, 5216, 5217 };
				int totalWeight = 0;
				for (unsigned short id : candidates) {
					totalWeight += std::max(0, _settings.failureRefundWeights.GetWeightByItemID(id));
				}

				if (totalWeight > 0) {
					std::uniform_int_distribution<int> weightDist(1, totalWeight);
					int rollWeight = weightDist(rng);
					int currentAcc = 0;
					for (unsigned short id : candidates) {
						currentAcc += std::max(0, _settings.failureRefundWeights.GetWeightByItemID(id));
						if (rollWeight <= currentAcc) {
							refundMatID = id;
							break;
						}
					}
				}
				else {
					std::uniform_int_distribution<size_t> matDist(0, candidates.size() - 1);
					refundMatID = candidates[matDist(rng)];
				}

				auto currentInv = user->GetUserInventory();
				unsigned short maxSlotID = 0;
				bool foundRefund = false;
				for (const auto& item : currentInv) {
					if (item.slotID > maxSlotID) maxSlotID = item.slotID;
					if (item.itemID == refundMatID) {
						foundRefund = true;
					}
				}

				if (foundRefund) {
					databaseManager.UpdateUserInventoryItemCount(userID, refundMatID, refundCount);
				}
				else {
					InventoryItem newItem;
					newItem.slotID = ++maxSlotID;
					newItem.itemID = refundMatID;
					newItem.count = refundCount;
					newItem.activeFlag = 1;
					newItem.inUse = false;
					newItem.obtainTime = (unsigned long)time(nullptr);
					newItem.expirationTime = 0;
					newItem.enhanceBits = 0;
					databaseManager.AddUserInventoryItems(userID, { newItem });
				}

				serverConsole.Print(PrefixType::Info, format("[ EnhanceManager ] User ({}) AdvEnhance failed, granted {}x refund material {}\n",
					user->GetUserLogName(), refundCount, refundMatID));
			}

			// 刷新背包
			packetManager.SendPacket_Inventory(connection, user->GetUserInventory(), deletedSlots);

			// 1. 若有返還材料，先發送獲得[完美屬性強化材料]彈窗封包
			if (refundMatID != 0) {
				AwardItem award;
				award.itemID = refundMatID;
				award.count = refundCount;
				award.duration = 0;
				award.flags = 0;
				packet_UMsgManager.SendPacket_UMsg_ItemAwardNotice(connection, { award }, "REINFORCE_PER_PART_ITEM_GIVE_TITLE", "REINFORCE_PER_PART_ITEM_GIVE_MSG");
			}

			// 2. 後發送強化失敗/掉級結果封包，確保客戶端「強化失敗」視窗覆蓋在最前端 (Z-order 最頂層)
			if (isLevelDown) {
				serverConsole.Print(PrefixType::Info, format("[ EnhanceManager ] User ({}) enhance FAILED (LevelDown: stat {} -1, newBits={:#x})\n",
					user->GetUserLogName(), downStat, newBits));
				SendEnhanceResult(connection, 2, weaponSlotID, newBits, (unsigned char)downStat, "");
			}
			else {
				serverConsole.Print(PrefixType::Info, format("[ EnhanceManager ] User ({}) enhance FAILED (no level change)\n",
					user->GetUserLogName()));
				SendEnhanceResult(connection, 0, weaponSlotID, currentStats.ToBits(), 0, "");
			}

			return true;
		}
	}

	// 其他無效材料
	serverConsole.Print(PrefixType::Warn, format("[ EnhanceManager ] User ({}) put non-enhancement material {}\n",
		user->GetUserLogName(), materialID));
	SendEnhanceResult(connection, 4, weaponSlotID, wIt->enhanceBits, 0, "");
	return false;
}

void EnhanceManager::SendEnhanceResult(TCPConnection::pointer connection, unsigned char status, unsigned short weaponSlotID, unsigned long enhanceBits, unsigned char statIndex, const std::string& message) {
	if (connection == nullptr) {
		return;
	}

	auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (replyPacket != nullptr) {
		replyPacket->WriteUInt8(14); // Packet_ItemType = 14 (Enhance)
		replyPacket->WriteUInt8(status); // ResultStatus (0=Go 0, 1=Success +1, 2=Reset -1, 3=Reset all, 4=Error)
		replyPacket->WriteUInt32_LE(weaponSlotID); // Weapon SlotID (client uses IInventory::GetItemBySlot to fetch weapon model, icon & name)
		replyPacket->WriteUInt32_LE(enhanceBits); // EnhanceBits (4 bytes)
		replyPacket->WriteUInt8(statIndex); // StatIndex (0=Acc, 1=Dmg, 2=Rebound, 3=Weight, 4=FireRate, 5=Ammo)
		replyPacket->WriteString(message); // Message (string)
		replyPacket->Send();
	}
}
