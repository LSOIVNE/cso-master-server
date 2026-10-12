#include "killdropmanager.h"
#include "packet_itemmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_umsgmanager.h"
#include "databasemanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdlib>
#include <ctime>
#include <format>
#include <algorithm>

using json = nlohmann::json;

KillDropManager killDropManager;

void KillDropManager::Init(const std::string& configPath) {
	_configPath = configPath;
	if (!LoadConfig(_configPath)) {
		serverConsole.Print(PrefixType::Warn, format("[ KillDropManager ] Failed to load config from {}, using defaults.\n", _configPath));
	}
}

void KillDropManager::Reload() {
	if (LoadConfig(_configPath)) {
		serverConsole.Print(PrefixType::Info, format("[ KillDropManager ] Successfully reloaded config from {}.\n", _configPath));
	}
	else {
		serverConsole.Print(PrefixType::Error, format("[ KillDropManager ] Failed to reload config from {}.\n", _configPath));
	}
}

bool KillDropManager::LoadConfig(const std::string& path) {
	std::ifstream file(path);
	if (!file.is_open()) {
		return false;
	}

	try {
		json j;
		file >> j;

		if (j.contains("settings")) {
			const auto& s = j["settings"];
			if (s.contains("enabled")) _settings.enabled = s["enabled"].get<bool>();
			if (s.contains("drop_rate")) _settings.dropRate = s["drop_rate"].get<float>();
			if (s.contains("enable_in_all_modes")) _settings.enableInAllModes = s["enable_in_all_modes"].get<bool>();
			if (s.contains("show_right_hud_icon")) _settings.showRightHudIcon = s["show_right_hud_icon"].get<bool>();
			if (s.contains("show_chat_notice")) _settings.showChatNotice = s["show_chat_notice"].get<bool>();
			if (s.contains("allowed_gamemodes")) {
				_settings.allowedGameModes = s["allowed_gamemodes"].get<std::vector<unsigned char>>();
			}
			_settings.modeDropRates.clear();
			if (s.contains("mode_drop_rates") && s["mode_drop_rates"].is_object()) {
				for (auto& [key, val] : s["mode_drop_rates"].items()) {
					try {
						unsigned char modeID = (unsigned char)std::stoi(key);
						_settings.modeDropRates[modeID] = val.get<float>();
					}
					catch (...) {}
				}
			}
			else if (s.contains("gamemode_drop_rates") && s["gamemode_drop_rates"].is_object()) {
				for (auto& [key, val] : s["gamemode_drop_rates"].items()) {
					try {
						unsigned char modeID = (unsigned char)std::stoi(key);
						_settings.modeDropRates[modeID] = val.get<float>();
					}
					catch (...) {}
				}
			}
		}

		_dropItems.clear();
		_totalWeight = 0;

		if (j.contains("drop_items") && j["drop_items"].is_array()) {
			for (const auto& itemJ : j["drop_items"]) {
				KillDropItem item;
				if (itemJ.contains("itemID")) item.itemID = itemJ["itemID"].get<unsigned short>();
				if (itemJ.contains("name")) item.name = itemJ["name"].get<std::string>();
				if (itemJ.contains("count")) item.count = itemJ["count"].get<unsigned short>();
				if (itemJ.contains("duration")) item.duration = itemJ["duration"].get<unsigned short>();
				if (itemJ.contains("weight")) item.weight = itemJ["weight"].get<unsigned int>();

				if (itemJ.contains("notice_type")) {
					item.noticeType = itemJ["notice_type"].get<unsigned char>();
				}
				else {
					// Auto detect notice type based on itemID:
					// 166 -> 10 (C-Box / Gachapon)
					// 5101~5110 -> 12 (FabEndu / Durability Material)
					if (item.itemID == 166) {
						item.noticeType = (unsigned char)Packet_ItemType::GachaponNotice; // 10
					}
					else {
						item.noticeType = (unsigned char)Packet_ItemType::FabEnduNotice;  // 12
					}
				}

				if (item.itemID > 0 && item.weight > 0) {
					_dropItems.push_back(item);
					_totalWeight += item.weight;
				}
			}
		}

		serverConsole.Print(PrefixType::Info, format("[ KillDropManager ] Loaded {} drop items (default dropRate: {:.2f}%, modeOverrides: {}, totalWeight: {}, showHUD: {}, showChat: {})\n",
			_dropItems.size(), _settings.dropRate * 100.0f, _settings.modeDropRates.size(), _totalWeight, _settings.showRightHudIcon, _settings.showChatNotice));

		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ KillDropManager ] JSON parse exception: {}\n", e.what()));
		return false;
	}
}

void KillDropManager::OnPlayerKill(User* killer, Room* room, unsigned short killCount) {
	if (!_settings.enabled || _dropItems.empty() || _totalWeight == 0) {
		return;
	}

	if (!killer || !killer->GetConnection() || !userManager.IsUserLoggedIn(killer)) {
		return;
	}

	if (room != NULL && !_settings.enableInAllModes && !_settings.allowedGameModes.empty()) {
		unsigned char mode = room->GetRoomSettings().gameModeID;
		if (std::find(_settings.allowedGameModes.begin(), _settings.allowedGameModes.end(), mode) == _settings.allowedGameModes.end()) {
			return;
		}
	}

	for (unsigned short k = 0; k < killCount; ++k) {
		TriggerDrop(killer, room);
	}
}

bool KillDropManager::TriggerDrop(User* killer, Room* room) {
	if (_totalWeight == 0 || _dropItems.empty()) {
		return false;
	}

	float targetDropRate = _settings.dropRate;
	if (room != NULL) {
		unsigned char mode = room->GetRoomSettings().gameModeID;
		auto it = _settings.modeDropRates.find(mode);
		if (it != _settings.modeDropRates.end()) {
			targetDropRate = it->second;
		}
	}

	// Roll drop probability
	float roll = (float)rand() / (float)RAND_MAX;
	if (roll >= targetDropRate) {
		return false;
	}

	// Weighted random selection
	unsigned int targetWeight = (unsigned int)(rand() % _totalWeight);
	unsigned int currentWeight = 0;
	KillDropItem selectedItem;
	bool found = false;

	for (const auto& item : _dropItems) {
		currentWeight += item.weight;
		if (targetWeight < currentWeight) {
			selectedItem = item;
			found = true;
			break;
		}
	}

	if (!found && !_dropItems.empty()) {
		selectedItem = _dropItems.front();
	}

	GiveDropItem(killer, room, selectedItem);
	return true;
}

void KillDropManager::GiveDropItem(User* killer, Room* room, const KillDropItem& dropItem) {
	if (!killer || !killer->GetConnection() || !userManager.IsUserLoggedIn(killer)) {
		return;
	}

	auto connection = killer->GetConnection();
	unsigned long userID = killer->GetUserID();
	time_t now = time(NULL);

	std::vector<InventoryItem> invItems = killer->GetUserInventory();
	bool alreadyOwned = false;
	unsigned short maxSlotID = 0;

	for (const auto& inv : invItems) {
		if (inv.slotID > maxSlotID) {
			maxSlotID = inv.slotID;
		}
		if (inv.itemID == dropItem.itemID) {
			alreadyOwned = true;
		}
	}

	unsigned short addedCount = dropItem.count > 0 ? dropItem.count : 1;

	// 1. Persist to Database
	if (alreadyOwned) {
		databaseManager.UpdateUserInventoryItemCount(userID, dropItem.itemID, addedCount);
	}
	else {
		InventoryItem newItem;
		newItem.slotID = ++maxSlotID;
		newItem.itemID = dropItem.itemID;
		newItem.count = addedCount;
		newItem.activeFlag = 1;
		newItem.inUse = false;
		newItem.obtainTime = (unsigned long)now;
		newItem.expirationTime = 0;
		newItem.enhanceBits = 0;

		databaseManager.AddUserInventoryItems(userID, { newItem });
	}

	// 2. Right-side HUD Icon popup (Packet 78)
	if (_settings.showRightHudIcon) {
		unsigned char notice = dropItem.noticeType;
		if (notice == 0) {
			notice = (dropItem.itemID == 166) ? (unsigned char)Packet_ItemType::GachaponNotice : (unsigned char)Packet_ItemType::FabEnduNotice;
		}
		packet_ItemManager.SendPacket_Item_Notice(connection, notice);
	}

	// 3. For C-Box (itemID 166), sync updated total CBoxCount to client HUD (Packet 157)
	if (dropItem.itemID == 166) {
		unsigned short totalCBox = 0;
		for (const auto& inv : killer->GetUserInventory()) {
			if (inv.itemID == 166) {
				totalCBox += inv.count;
			}
		}
		packet_UpdateInfoManager.SendPacket_UserUpdateInfo_CBoxCount(connection, userID, totalCBox);
	}

	// 4. In-game Chat Notice (Purple Type 63)
	if (_settings.showChatNotice) {
		AwardItem award;
		award.itemID = dropItem.itemID;
		award.count = addedCount;
		award.duration = dropItem.duration;
		award.flags = 0;

		packet_UMsgManager.SendPacket_UMsg_ItemAwardNotice(
			connection,
			{ award },
			"HSCENARIO_MONSTER_DROP_ITEM_TITLE",
			"HSCENARIO_MONSTER_DROP_ITEM_GIVE",
			{},
			Packet_UMsgType::ItemAwardNotice_InGame
		);
	}

	serverConsole.Print(PrefixType::Info, format("[ KillDropManager ] User ({}) dropped item {} ('{}', x{}) on kill! NoticeType={}\n",
		killer->GetUserLogName(), dropItem.itemID, dropItem.name, addedCount, (int)dropItem.noticeType));
}
