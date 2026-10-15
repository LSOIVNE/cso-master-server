#include "scenariorewardmanager.h"
#include "serverconsole.h"
#include "nlohmann/json.hpp"
#include "questmanager.h"
#include "databasemanager.h"
#include "itemdefinitionmanager.h"
#include "packet_umsgmanager.h"
#include "packet_updateinfomanager.h"
#include "packetmanager.h"
#include "user/user.h"
#include "room/room.h"
#include "roommanager.h"

#include <fstream>
#include <filesystem>
#include <format>
#include <algorithm>
#include <chrono>

using json = nlohmann::json;
using namespace std;

ScenarioRewardManager scenarioRewardManager;

bool ScenarioRewardManager::Init(const std::string& configPath) {
	serverConsole.Print(PrefixType::Info, "[ ScenarioRewardManager ] Initializing ScenarioReward system...\n");
	return LoadConfig(configPath);
}

bool ScenarioRewardManager::LoadConfig(const std::string& configPath) {
	std::string resolvedPath = configPath.empty() ? "configs/scenario_reward.json" : configPath;
	if (!std::filesystem::exists(resolvedPath)) {
		serverConsole.Print(PrefixType::Warn, format("[ ScenarioRewardManager ] Config file not found: {}\n", resolvedPath));
		return false;
	}

	try {
		std::ifstream file(resolvedPath);
		if (!file.is_open()) {
			serverConsole.Print(PrefixType::Error, format("[ ScenarioRewardManager ] Could not open file: {}\n", resolvedPath));
			return false;
		}

		json root = json::parse(file);

		// 1. Settings
		if (root.contains("settings") && root["settings"].is_object()) {
			const auto& s = root["settings"];
			_settings.enabled = s.value("enabled", true);
			_settings.defaultTitleKey = s.value("default_title_key", "HSCENARIO_MONSTER_DROP_ITEM_TITLE");
			_settings.defaultMsgKey = s.value("default_msg_key", "HSCENARIO_MONSTER_DROP_ITEM_GIVE");
		}

		auto ParseCandidate = [](const json& j) -> ScenarioRewardCandidate {
			ScenarioRewardCandidate c;
			c.itemID = j.value("itemID", (unsigned short)0);
			c.points = j.value("points", (unsigned int)0);
			c.name = j.value("name", "");
			c.count = j.value("count", (unsigned short)1);
			c.duration = j.value("duration", (short)0);
			c.weight = j.value("weight", (unsigned int)10);
			if (j.contains("PackageItems") && j["PackageItems"].is_array()) {
				for (const auto& elem : j["PackageItems"]) {
					if (elem.is_number()) c.packageItems.push_back(elem.get<unsigned short>());
				}
			}
			else if (j.contains("packageItems") && j["packageItems"].is_array()) {
				for (const auto& elem : j["packageItems"]) {
					if (elem.is_number()) c.packageItems.push_back(elem.get<unsigned short>());
				}
			}
			return c;
		};

		auto ParseRewardEntries = [&](const json& j, std::vector<ScenarioRewardEntry>& outEntries) {
			if (j.contains("rewards") && j["rewards"].is_array()) {
				for (const auto& elem : j["rewards"]) {
					if (elem.contains("randomItems")) {
						ScenarioRewardEntry entry;
						entry.type = ScenarioRewardEntryType::RandomPool;
						entry.pickCount = elem.value("pickCount", (unsigned short)1);
						if (elem["randomItems"].is_array()) {
							for (const auto& rItem : elem["randomItems"]) {
								auto cand = ParseCandidate(rItem);
								if (cand.itemID > 0 || cand.points > 0 || !cand.packageItems.empty() || cand.weight > 0) {
									entry.randomItems.push_back(cand);
								}
							}
						}
						outEntries.push_back(entry);
					}
					else {
						auto cand = ParseCandidate(elem);
						if (cand.itemID > 0 || cand.points > 0 || !cand.packageItems.empty()) {
							ScenarioRewardEntry entry;
							entry.type = ScenarioRewardEntryType::DirectItem;
							entry.directItem = cand;
							outEntries.push_back(entry);
						}
					}
				}
			}
		};

		// 2. Default reward (optional)
		_defaultReward.reset();
		if (root.contains("default_reward") && root["default_reward"].is_object()) {
			const auto& defJson = root["default_reward"];
			auto defRew = std::make_unique<ScenarioMapReward>();
			defRew->mapID = 0;
			defRew->mapName = "default";
			defRew->description = defJson.value("description", "預設通關獎勵");
			defRew->gameMode = defJson.value("game_mode", (unsigned char)0);
			defRew->bonusExp = defJson.value("exp", (unsigned short)0);
			defRew->bonusPoints = defJson.value("points", (unsigned short)0);
			defRew->titleKey = defJson.value("title_key", _settings.defaultTitleKey);
			defRew->msgKey = defJson.value("msg_key", _settings.defaultMsgKey);
			defRew->enabled = defJson.value("enabled", true);

			ParseRewardEntries(defJson, defRew->rewardEntries);

			if (defRew->enabled) {
				_defaultReward = std::move(defRew);
			}
		}

		// 3. Rewards list
		_rewards.clear();
		if (root.contains("rewards") && root["rewards"].is_array()) {
			for (const auto& rewJson : root["rewards"]) {
				ScenarioMapReward r;
				r.mapID = rewJson.value("map_id", (unsigned char)0);
				r.mapName = rewJson.value("map_name", "");
				r.description = rewJson.value("description", "");
				r.gameMode = rewJson.value("game_mode", (unsigned char)0);
				r.bonusExp = rewJson.value("exp", (unsigned short)0);
				r.bonusPoints = rewJson.value("points", (unsigned short)0);
				r.titleKey = rewJson.value("title_key", _settings.defaultTitleKey);
				r.msgKey = rewJson.value("msg_key", _settings.defaultMsgKey);
				r.enabled = rewJson.value("enabled", true);

				ParseRewardEntries(rewJson, r.rewardEntries);

				if (r.enabled) {
					_rewards.push_back(r);
				}
			}
		}

		// 4. Box rewards (in-game drop box / supply pickup)
		_boxRewards.clear();
		_defaultBoxReward.reset();
		_boxSettings = ScenarioBoxRewardSettings();

		if (root.contains("box_rewards") && root["box_rewards"].is_object()) {
			const auto& boxRoot = root["box_rewards"];
			_boxSettings.enabled = boxRoot.value("enabled", true);
			_boxSettings.noticeKey = boxRoot.value("notice_key", "EVT_SCENARIO_ITEM_POINT_REWARD");

			if (boxRoot.contains("default_box") && boxRoot["default_box"].is_object()) {
				const auto& defBox = boxRoot["default_box"];
				auto defBoxReward = std::make_unique<ScenarioBoxReward>();
				defBoxReward->rewardID = 0;
				defBoxReward->name = defBox.value("name", "預設戰利品箱");
				defBoxReward->titleKey = defBox.value("title_key", "");
				defBoxReward->msgKey = defBox.value("msg_key", "");
				defBoxReward->enabled = defBox.value("enabled", true);
				ParseRewardEntries(defBox, defBoxReward->rewardEntries);
				_defaultBoxReward = std::move(defBoxReward);
			}

			if (boxRoot.contains("boxes") && boxRoot["boxes"].is_object()) {
				for (auto& [key, boxJson] : boxRoot["boxes"].items()) {
					if (!boxJson.is_object()) continue;
					try {
						unsigned int rewID = (unsigned int)std::stoul(key);
						ScenarioBoxReward b;
						b.rewardID = rewID;
						b.name = boxJson.value("name", "戰利品補給箱");
						b.titleKey = boxJson.value("title_key", "");
						b.msgKey = boxJson.value("msg_key", "");
						b.enabled = boxJson.value("enabled", true);
						ParseRewardEntries(boxJson, b.rewardEntries);
						_boxRewards[rewID] = b;
					}
					catch (...) {}
				}
			}
		}

		serverConsole.Print(PrefixType::Info, format(
			"[ ScenarioRewardManager ] Loaded {} map reward configs (default: {}), {} box reward configs (default box: {}).\n",
			_rewards.size(), _defaultReward ? "enabled" : "none",
			_boxRewards.size(), _defaultBoxReward ? "enabled" : "none"
		));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ ScenarioRewardManager ] Exception parsing config: {}\n", e.what()));
		return false;
	}
}

const ScenarioMapReward* ScenarioRewardManager::GetRewardForMap(unsigned char mapID, unsigned char gameModeID) const {
	if (!_settings.enabled) return nullptr;

	std::string mapFileName = questManager.GetMapFileName(mapID);
	std::string mapToken = questManager.GetMapToken(mapID);

	std::string lowerFileName = mapFileName;
	std::transform(lowerFileName.begin(), lowerFileName.end(), lowerFileName.begin(), [](unsigned char c) { return (char)tolower(c); });

	std::string lowerToken = mapToken;
	std::transform(lowerToken.begin(), lowerToken.end(), lowerToken.begin(), [](unsigned char c) { return (char)tolower(c); });

	auto MatchesMap = [&](const ScenarioMapReward& r) -> bool {
		if (!r.enabled) return false;
		if (r.mapID > 0 && r.mapID == mapID) {
			return true;
		}
		if (!r.mapName.empty()) {
			std::string lowerName = r.mapName;
			std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), [](unsigned char c) { return (char)tolower(c); });

			if (lowerName == lowerFileName || lowerName == lowerToken) {
				return true;
			}
			if (lowerName + ".bsp" == lowerFileName) {
				return true;
			}
		}
		return false;
	};

	// 1st pass: exact game mode match
	for (const auto& r : _rewards) {
		if (r.gameMode == gameModeID && MatchesMap(r)) {
			return &r;
		}
	}

	// 2nd pass: wildcard mode match (gameMode == 0)
	for (const auto& r : _rewards) {
		if (r.gameMode == 0 && MatchesMap(r)) {
			return &r;
		}
	}

	// Fallback: default reward
	if (_defaultReward && _defaultReward->enabled) {
		if (_defaultReward->gameMode == 0 || _defaultReward->gameMode == gameModeID) {
			return _defaultReward.get();
		}
	}

	return nullptr;
}

bool ScenarioRewardManager::GrantScenarioReward(User* user, const ScenarioMapReward* reward, const std::string& mapNameForLog) {
	if (!user || !reward) return false;
	auto connection = user->GetConnection();
	if (!connection) return false;

	unsigned long userID = user->GetUserID();
	auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	vector<InventoryItem> invItems = user->GetUserInventory();

	unsigned short maxSlotID = 0;
	for (const auto& inv : invItems) {
		if (inv.slotID > maxSlotID) {
			maxSlotID = inv.slotID;
		}
	}

	// 1. Resolve reward entries (direct items and random pools like rewarditem.json)
	unsigned int totalBonusPoints = reward->bonusPoints;
	std::vector<ScenarioRewardCandidate> resolvedItems;

	auto addCandidateToResolved = [&](const ScenarioRewardCandidate& c) {
		if (!c.packageItems.empty()) {
			for (unsigned short pkgID : c.packageItems) {
				ScenarioRewardCandidate sub = c;
				sub.itemID = pkgID;
				sub.packageItems.clear();
				resolvedItems.push_back(sub);
			}
		}
		else if (c.itemID > 0) {
			resolvedItems.push_back(c);
		}
	};

	for (const auto& entry : reward->rewardEntries) {
		if (entry.type == ScenarioRewardEntryType::DirectItem) {
			const auto& dItem = entry.directItem;
			if (dItem.points > 0) {
				totalBonusPoints += dItem.points;
			}
			addCandidateToResolved(dItem);
		}
		else if (entry.type == ScenarioRewardEntryType::RandomPool) {
			if (entry.randomItems.empty() || entry.pickCount == 0) continue;

			std::vector<ScenarioRewardCandidate> candidates = entry.randomItems;
			unsigned short toPick = (unsigned short)std::min((size_t)entry.pickCount, candidates.size());

			for (unsigned short p = 0; p < toPick && !candidates.empty(); p++) {
				unsigned int totalWeight = 0;
				for (const auto& c : candidates) {
					totalWeight += (c.weight > 0 ? c.weight : 1);
				}
				if (totalWeight == 0) break;

				unsigned int roll = (unsigned int)(rand() % totalWeight);
				unsigned int acc = 0;
				for (auto it = candidates.begin(); it != candidates.end(); ++it) {
					acc += (it->weight > 0 ? it->weight : 1);
					if (roll < acc) {
						if (it->points > 0) {
							totalBonusPoints += it->points;
						}
						addCandidateToResolved(*it);
						candidates.erase(it); // Erase candidate so it NEVER picks duplicate items!
						break;
					}
				}
			}
		}
	}

	if (totalBonusPoints > 0) {
		databaseManager.AddUserFunds(userID, true, totalBonusPoints);
		auto currentChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, currentChar.userCharacter });
	}
	if (reward->bonusExp > 0) {
		databaseManager.AddUserExp(userID, reward->bonusExp);
	}

	vector<AwardItem> awardItems;
	vector<InventoryItem> newItemsToAdd;
	vector<InventoryItem> deltaUpdatedItems;
	bool hasCBoxAward = false;

	for (const auto& item : resolvedItems) {
		if (item.itemID == 0) continue;

		const auto* def = itemDefinitionManager.GetItem(item.itemID);
		bool isCountItem = (def != nullptr && !def->isPeriod);
		bool isPerm = (item.duration == -1 || item.duration == (short)0xFFFF);

		bool alreadyOwned = false;
		InventoryItem ownedItem;
		for (const auto& inv : invItems) {
			if (inv.itemID == item.itemID) {
				alreadyOwned = true;
				ownedItem = inv;
				break;
			}
		}

		AwardItem aItem;
		aItem.itemID = item.itemID;
		aItem.count = item.count > 0 ? item.count : 1;
		aItem.flags = 0;

		if (item.itemID == 166) {
			// C-Box (itemID 166) is stored in user_characters.cboxCount, never in inventory
			unsigned short addedCount = item.count > 0 ? item.count : 1;
			databaseManager.AddUserCBoxCount(userID, addedCount);
			hasCBoxAward = true;
			aItem.duration = 0;
			awardItems.push_back(aItem);
			continue;
		}

		if (item.duration <= 0 && isCountItem) {
			// Consumable count item (e.g. C-Box 166, decoders, craft materials)
			aItem.duration = 0;
			unsigned short addedCount = item.count > 0 ? item.count : 1;
			if (alreadyOwned) {
				databaseManager.UpdateUserInventoryItemCount(userID, item.itemID, addedCount);
				ownedItem.count += addedCount;
				deltaUpdatedItems.push_back(ownedItem);
			}
			else {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = item.itemID;
				newItem.count = addedCount;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = 0;
				newItem.enhanceBits = 0;
				newItemsToAdd.push_back(newItem);
				deltaUpdatedItems.push_back(newItem);
			}
		}
		else if (isPerm) {
			// Permanent item
			aItem.duration = 0xFFFF;
			if (!alreadyOwned) {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = item.itemID;
				newItem.count = 1;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = 0;
				newItem.enhanceBits = 0;
				newItemsToAdd.push_back(newItem);
				deltaUpdatedItems.push_back(newItem);
			}
		}
		else {
			// Duration period item
			unsigned short days = (item.duration > 0) ? (unsigned short)item.duration : 1;
			aItem.duration = days;
			if (alreadyOwned && ownedItem.expirationTime > 0) {
				unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
				unsigned long newExp = baseTime + (unsigned long)days * 86400;
				databaseManager.UpdateUserInventoryExpiration(userID, item.itemID, newExp);
				ownedItem.expirationTime = newExp;
				deltaUpdatedItems.push_back(ownedItem);
			}
			else if (!alreadyOwned) {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = item.itemID;
				newItem.count = 1;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = (unsigned long)(now + (unsigned long)days * 86400);
				newItem.enhanceBits = 0;
				newItemsToAdd.push_back(newItem);
				deltaUpdatedItems.push_back(newItem);
			}
		}

		awardItems.push_back(aItem);
	}

	if (!newItemsToAdd.empty()) {
		databaseManager.AddUserInventoryItems(userID, newItemsToAdd);
	}

	// Realtime sync inventory
	packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, {});

	if (hasCBoxAward) {
		packet_UpdateInfoManager.SendPacket_UserUpdateInfo_CBoxCount(connection, userID, (unsigned short)user->GetCBoxCount());
	}

	// Send ItemAwardNotice dialog (Packet 67 Type 60)
	if (!awardItems.empty() || totalBonusPoints > 0 || reward->bonusExp > 0) {
		std::string title = reward->titleKey.empty() ? _settings.defaultTitleKey : reward->titleKey;
		std::string msg = reward->msgKey.empty() ? _settings.defaultMsgKey : reward->msgKey;

		packet_UMsgManager.SendPacket_UMsg_ItemAwardNotice(
			connection,
			awardItems,
			totalBonusPoints,
			reward->bonusExp,
			0, // honor
			title,
			msg
		);
	}

	serverConsole.Print(PrefixType::Info, format(
		"[ ScenarioRewardManager ] User ({}) cleared scenario map '{}' (ID: {}). Rewarded {} EXP, {} Points, {} item(s).\n",
		user->GetUserLogName(), mapNameForLog, reward->mapID, reward->bonusExp, totalBonusPoints, resolvedItems.size()
	));

	return true;
}

const ScenarioBoxReward* ScenarioRewardManager::GetBoxReward(unsigned int rewardID) const {
	if (!_boxSettings.enabled) return nullptr;

	auto it = _boxRewards.find(rewardID);
	if (it != _boxRewards.end() && it->second.enabled) {
		return &it->second;
	}
	if (_defaultBoxReward && _defaultBoxReward->enabled) {
		return _defaultBoxReward.get();
	}
	return nullptr;
}

bool ScenarioRewardManager::HandleBoxPickup(User* user, unsigned int rewardID) {
	if (user == NULL) {
		return false;
	}

	auto connection = user->GetConnection();
	if (connection == NULL) {
		return false;
	}

	if (!_boxSettings.enabled) {
		serverConsole.Print(PrefixType::Info, format("[ ScenarioRewardManager ] User ({}) picked up box {}, but box rewards are disabled.\n",
			user->GetUserLogName(), rewardID));
		return false;
	}

	const ScenarioBoxReward* box = GetBoxReward(rewardID);
	unsigned int pointsAwarded = 0;
	std::string boxName = (box != NULL) ? box->name : "戰利品補給箱";

	unsigned long userID = user->GetUserID();
	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	bool isHumanScenario = (room != nullptr && room->GetRoomSettings().gameModeID == 17);

	// 1. Resolve box reward entries (can grant items OR points OR both)
	std::vector<ScenarioRewardCandidate> resolvedItems;

	auto addCandidateToResolved = [&](const ScenarioRewardCandidate& c) {
		if (!c.packageItems.empty()) {
			for (unsigned short pkgID : c.packageItems) {
				ScenarioRewardCandidate sub = c;
				sub.itemID = pkgID;
				sub.packageItems.clear();
				resolvedItems.push_back(sub);
			}
		}
		else if (c.itemID > 0) {
			resolvedItems.push_back(c);
		}
	};

	if (box != NULL && !box->rewardEntries.empty()) {
		for (const auto& entry : box->rewardEntries) {
			if (entry.type == ScenarioRewardEntryType::DirectItem) {
				const auto& dItem = entry.directItem;
				if (dItem.points > 0) {
					pointsAwarded += dItem.points;
				}
				addCandidateToResolved(dItem);
			}
			else if (entry.type == ScenarioRewardEntryType::RandomPool) {
				if (entry.randomItems.empty() || entry.pickCount == 0) continue;

				std::vector<ScenarioRewardCandidate> candidates = entry.randomItems;
				unsigned short toPick = (unsigned short)std::min((size_t)entry.pickCount, candidates.size());

				for (unsigned short p = 0; p < toPick && !candidates.empty(); p++) {
					unsigned int totalWeight = 0;
					for (const auto& c : candidates) {
						totalWeight += (c.weight > 0 ? c.weight : 1);
					}
					if (totalWeight == 0) break;

					unsigned int roll = (unsigned int)(rand() % totalWeight);
					unsigned int acc = 0;
					for (auto it = candidates.begin(); it != candidates.end(); ++it) {
						acc += (it->weight > 0 ? it->weight : 1);
						if (roll < acc) {
							if (it->points > 0) {
								pointsAwarded += it->points;
							}
							addCandidateToResolved(*it);
							candidates.erase(it);
							break;
						}
					}
				}
			}
		}
	}

	// 2. Grant Points (if awarded either via base points or randomItems)
	if (pointsAwarded > 0) {
		if (databaseManager.AddUserFunds(userID, true, pointsAwarded)) {
			auto currentChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);

			// Realtime update character info for HUD
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, currentChar.userCharacter });

			// Client in-game chat notice (Red text):
			// GameMode 15: CSO_Info_EVT_SCENARIO_ITEM_POINT_REWARD ("恭喜您獲得{}Point.")
			// GameMode 17: CSO_Info_EVT_HSCENARIO_ITEM_POINT_REWARD ("[戰利品] 獲得Point {}")
			string pointNoticeKey = isHumanScenario ? "EVT_HSCENARIO_ITEM_POINT_REWARD" : _boxSettings.noticeKey;
			packet_UMsgManager.SendPacket_UMsg_SystemReply(
				connection,
				Packet_UMsgType::SystemReply_Red,
				pointNoticeKey,
				{ std::to_string(pointsAwarded) }
			);
		}
	}

	// 3. Grant items if configured in this box
	if (!resolvedItems.empty()) {
			auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			vector<InventoryItem> invItems = user->GetUserInventory();
			unsigned short maxSlotID = 0;
			for (const auto& inv : invItems) {
				if (inv.slotID > maxSlotID) maxSlotID = inv.slotID;
			}

			vector<InventoryItem> newItemsToAdd;
			for (const auto& item : resolvedItems) {
				if (item.itemID == 0) continue;
				const auto* def = itemDefinitionManager.GetItem(item.itemID);
				bool isCountItem = (def != nullptr && !def->isPeriod);
				bool isPerm = (item.duration == -1 || item.duration == (short)0xFFFF);

				bool alreadyOwned = false;
				InventoryItem ownedItem;
				for (const auto& inv : invItems) {
					if (inv.itemID == item.itemID) {
						alreadyOwned = true;
						ownedItem = inv;
						break;
					}
				}

				if (item.duration <= 0 && isCountItem) {
					unsigned short addedCount = item.count > 0 ? item.count : 1;
					if (alreadyOwned) {
						databaseManager.UpdateUserInventoryItemCount(userID, item.itemID, addedCount);
					}
					else {
						InventoryItem newItem;
						newItem.slotID = ++maxSlotID;
						newItem.itemID = item.itemID;
						newItem.count = addedCount;
						newItem.activeFlag = 1;
						newItem.inUse = false;
						newItem.obtainTime = (unsigned long)now;
						newItem.expirationTime = 0;
						newItem.enhanceBits = 0;
						newItemsToAdd.push_back(newItem);
					}
				}
				else if (isPerm) {
					if (!alreadyOwned) {
						InventoryItem newItem;
						newItem.slotID = ++maxSlotID;
						newItem.itemID = item.itemID;
						newItem.count = 1;
						newItem.activeFlag = 1;
						newItem.inUse = false;
						newItem.obtainTime = (unsigned long)now;
						newItem.expirationTime = 0;
						newItem.enhanceBits = 0;
						newItemsToAdd.push_back(newItem);
					}
				}
				else {
					unsigned short days = (item.duration > 0) ? (unsigned short)item.duration : 1;
					if (alreadyOwned && ownedItem.expirationTime > 0) {
						unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
						unsigned long newExp = baseTime + (unsigned long)days * 86400;
						databaseManager.UpdateUserInventoryExpiration(userID, item.itemID, newExp);
					}
					else if (!alreadyOwned) {
						InventoryItem newItem;
						newItem.slotID = ++maxSlotID;
						newItem.itemID = item.itemID;
						newItem.count = 1;
						newItem.activeFlag = 1;
						newItem.inUse = false;
						newItem.obtainTime = (unsigned long)now;
						newItem.expirationTime = (unsigned long)(now + (unsigned long)days * 86400);
						newItem.enhanceBits = 0;
						newItemsToAdd.push_back(newItem);
					}
				}
			}

			if (!newItemsToAdd.empty()) {
				databaseManager.AddUserInventoryItems(userID, newItemsToAdd);
			}
			// Note: Do NOT send SendPacket_Inventory during live match (causes client rendering freeze).
			// The items are committed to DB and will be synced upon match completion / return to room.

			// Client in-game chat notice (Purple text via ItemAwardNotice_InGame, Type 63):
			// The CSO client natively parses Type 63 when InGame (state == 6) and formats:
			// "[戰利品] 恭喜您獲得下列道具,請至個人狀態欄確認 <獲得道具： \"道具名稱(X日)\", ...>"
			// rendering with color 60 (RGB 248, 170, 255 - Purple) in the in-game HUD chat,
			// without opening any modal dialog window or causing any rendering stutter.
			vector<AwardItem> awardItems;
			for (const auto& item : resolvedItems) {
				if (item.itemID == 0) continue;
				AwardItem award;
				award.itemID = item.itemID;
				award.count = item.count > 0 ? (unsigned short)item.count : 1;
				if (item.duration == -1 || item.duration == (short)0xFFFF) {
					award.duration = 0xFFFF; // 永久
				}
				else if (item.duration > 0) {
					award.duration = (unsigned short)item.duration; // X日
				}
				else {
					award.duration = 0; // count item (X個)
				}
				award.flags = 0;
				awardItems.push_back(award);
			}

			if (!awardItems.empty()) {
				string titleKey = (box != nullptr && !box->titleKey.empty()) ? box->titleKey : (isHumanScenario ? "HSCENARIO_MONSTER_DROP_ITEM_TITLE" : "SCENARIO_MONSTER_DROP_ITEM_TITLE");
				string msgKey = (box != nullptr && !box->msgKey.empty()) ? box->msgKey : (isHumanScenario ? "HSCENARIO_MONSTER_DROP_ITEM_GIVE" : "SCENARIO_MONSTER_DROP_ITEM_GIVE");
				packet_UMsgManager.SendPacket_UMsg_ItemAwardNotice(
					connection,
					awardItems,
					0, // points
					titleKey,
					msgKey,
					{},
					Packet_UMsgType::ItemAwardNotice_InGame
				);
			}
		}

	serverConsole.Print(PrefixType::Info, format(
		"[ ScenarioRewardManager ] User ({}) opened {} (rewardID: {}). Granted {} Points, {} item(s).\n",
		user->GetUserLogName(), boxName, rewardID, pointsAwarded, resolvedItems.size()
	));

	return true;
}
