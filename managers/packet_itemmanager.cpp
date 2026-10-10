#include "packet_itemmanager.h"
#include "usermanager.h"
#include "roommanager.h"
#include "databasemanager.h"
#include "packetmanager.h"
#include "serverconsole.h"
#include "itemdefinitionmanager.h"
#include "itemboxmanager.h"
#include "rewarditemmanager.h"
#include "craftmanager.h"
#include "attendancemanager.h"
#include "enhancemanager.h"
#include "packet_updateinfomanager.h"
#include "packet_umsgmanager.h"
#include <unordered_set>
#include <format>
#include <ctime>

Packet_ItemManager packet_ItemManager;

void Packet_ItemManager::ParsePacket_Item(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (!userManager.IsUserLoggedIn(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] Client ({}) has sent Packet_Item, but it's not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] Parsing Packet_Item from user ({})\n", user->GetUserLogName()));

	unsigned char type = packet->ReadUInt8();

	switch (type) {
		case Packet_ItemType::EquipItem: {
			parsePacket_Item_EquipItem(user, packet);
			break;
		}
		case Packet_ItemType::SelectClass: {
			parsePacket_Item_SelectClass(user, packet);
			break;
		}
		case Packet_ItemType::BuyWeapon: {
			parsePacket_Item_BuyWeapon(user, packet);
			break;
		}
		case Packet_ItemType::OpenBox: {
			parsePacket_Item_OpenBox(user, packet);
			break;
		}
		case Packet_ItemType::Craft: {
			parsePacket_Item_Craft(user, packet);
			break;
		}
		case Packet_ItemType::MaterialAssem: {
			parsePacket_Item_MaterialAssem(user, packet);
			break;
		}
		case Packet_ItemType::Disassemble: {
			parsePacket_Item_Disassemble(user, packet);
			break;
		}
		case Packet_ItemType::Roulette: {
			parsePacket_Item_Roulette(user, packet);
			break;
		}
		case Packet_ItemType::Tattoo: {
			parsePacket_Item_Tattoo(user, packet);
			break;
		}
		case Packet_ItemType::Enhance: {
			parsePacket_Item_Enhance(user, packet);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] User ({}) has sent unregistered Packet_Item type {}!\n", user->GetUserLogName(), type));
			break;
		}
	}
}

void Packet_ItemManager::parsePacket_Item_SelectClass(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] User ({}) has sent Packet_Item SelectClass, but it's not in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] User ({}) has sent Packet_Item SelectClass, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] User ({}) has sent Packet_Item SelectClass, but its room doesn't have a gameMatch!\n", user->GetUserLogName()));
		return;
	}

	unsigned char classItemID = packet->ReadUInt8();

	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) has sent Packet_Item SelectClass - classItemID: {}\n", user->GetUserLogName(), classItemID));
}

void Packet_ItemManager::parsePacket_Item_BuyWeapon(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] User ({}) has sent Packet_Item BuyWeapon, but it's not in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] User ({}) has sent Packet_Item BuyWeapon, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] User ({}) has sent Packet_Item BuyWeapon, but its room doesn't have a gameMatch!\n", user->GetUserLogName()));
		return;
	}

	unsigned char primaryItemID = packet->ReadUInt8();
	unsigned char secondaryItemID = packet->ReadUInt8();

	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) has sent Packet_Item BuyWeapon - primaryItemID: {}, secondaryItemID: {}\n", user->GetUserLogName(), primaryItemID, secondaryItemID));
}

bool IsTattooItem(unsigned short itemID) {
	switch (itemID) {
		case 373: // Chaos Tattoo
		case 374: // Tiger Tattoo
		case 375: // Dragon Tattoo
		case 386: // Bambou Tattoo
		case 387: // Custom Tattoo
		case 9997: // Letterk3 Tattoo
		case 9998: // Letterk2 Tattoo
		case 9999: // Letterk1 Tattoo
			return true;
		default:
			return false;
	}
}

void Packet_ItemManager::parsePacket_Item_EquipItem(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned short slotID = packet->ReadUInt16_LE();
	unsigned char action = packet->ReadUInt8();

	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) has sent Packet_Item EquipItem - slotID: {}, action: {}\n", user->GetUserLogName(), slotID, action));

	unsigned long userID = user->GetUserID();
	auto inventory = user->GetUserInventory();

	auto it = find_if(inventory.begin(), inventory.end(), [slotID](const InventoryItem& item) {
		return item.slotID == slotID;
	});

	if (it != inventory.end()) {
		const ItemDefinition* itemDef = itemDefinitionManager.GetItem(it->itemID);
		bool isCostume = (itemDef != NULL && itemDef->IsCostume());
		CostumeSlot costumeSlot = isCostume ? itemDef->GetCostumeSlot() : CostumeSlot::None;

		if (itemDef != NULL && (itemDef->className == "RewardItem" || itemDef->category == 8 || itemDef->category == 9 || itemDef->id == 80)) {
			if (action == 0) {
				parsePacket_Item_UseRewardItem(user, *it, *itemDef);
				return;
			}
		}

		if (isCostume) {
			if (action != 0 || it->inUse) {
				// Unequip costume
				databaseManager.SetUserInventoryItemInUse(userID, slotID, false);
			}
			else {
				// Equipping costume: unequip any other costume in the same slot
				if (costumeSlot != CostumeSlot::None) {
					for (const auto& item : inventory) {
						if (item.inUse && item.slotID != slotID) {
							const ItemDefinition* otherDef = itemDefinitionManager.GetItem(item.itemID);
							if (otherDef != NULL && otherDef->IsCostume() && otherDef->GetCostumeSlot() == costumeSlot) {
								databaseManager.SetUserInventoryItemInUse(userID, item.slotID, false);
							}
						}
					}
				}
				databaseManager.SetUserInventoryItemInUse(userID, slotID, true);
			}
		}
		else if (IsTattooItem(it->itemID)) {
			if (action != 0 || it->inUse) {
				databaseManager.SetUserInventoryItemInUse(userID, slotID, false);
			}
			else {
				for (const auto& item : inventory) {
					if (item.inUse && item.slotID != slotID && IsTattooItem(item.itemID)) {
						databaseManager.SetUserInventoryItemInUse(userID, item.slotID, false);
					}
				}
				databaseManager.SetUserInventoryItemInUse(userID, slotID, true);
			}
		}
		else {
			if (action == 0) {
				databaseManager.SetUserInventoryItemInUse(userID, slotID, true);
			}
			else {
				databaseManager.SetUserInventoryItemInUse(userID, slotID, false);
			}
		}
	}

	// Reply to client: Packet 78, type 0, status 0, message ""
	auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (replyPacket != NULL) {
		replyPacket->WriteUInt8((unsigned char)Packet_ItemType::EquipItem);
		replyPacket->WriteUInt8(1); // status 1 = success (hw.dll OnEquipResult checks status != 0)
		replyPacket->WriteString(""); // message = ""
		replyPacket->Send();
	}

	// Send refreshed inventory so client updates equipped badges/buttons
	packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
}

void Packet_ItemManager::parsePacket_Item_UseRewardItem(User* user, const InventoryItem& item, const ItemDefinition& itemDef) {
	if (user == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned long userID = user->GetUserID();

	// Special case: ItemID 80 (更改個人地區券) / ItemID 115 (所屬網咖初始化 (免費))
	if (item.itemID == 80 || item.itemID == 115 || itemDef.className == "ResetCafe") {
		auto charRes = user->GetUserCharacter(USERCHARACTER_FLAG_LOCATION);
		unsigned short curCity = charRes.result ? charRes.userCharacter.city : 0;

		if (curCity == 0) {
			auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
			if (replyPacket != NULL) {
				replyPacket->WriteUInt8((unsigned char)Packet_ItemType::EquipItem);
				replyPacket->WriteUInt8(0);
				replyPacket->WriteString("");
				replyPacket->Send();
			}
			packet_UMsgManager.SendPacket_UMsg_DialogBox(connection, "您尚未登入個人地區，無需使用此票券！\n請直接至大廳主畫面點擊右下方「登入地區」按鈕進行設定。");
			return;
		}

		// 1. Consume 1 ticket
		bool deleted = false;
		if (!databaseManager.ConsumeUserInventoryItemBySlot(userID, item.slotID, 1, deleted)) {
			serverConsole.Print(PrefixType::Error, format("[ Packet_ItemManager ] Failed to consume ItemID {} slot {} for user ({})\n", item.itemID, item.slotID, user->GetUserLogName()));
			return;
		}

		// 2. Reset location in database (city: 0, county: 0, neighborhood: 0)
		databaseManager.UpdateUserLocation(userID, 0, 0, 0);

		// 3. Reply Packet 78 EquipItem success to client
		auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
		if (replyPacket != NULL) {
			replyPacket->WriteUInt8((unsigned char)Packet_ItemType::EquipItem);
			replyPacket->WriteUInt8(1);
			replyPacket->WriteString("");
			replyPacket->Send();
		}

		// 4. Sync updated inventory to client
		vector<InventoryItem> updatedInv = user->GetUserInventory();
		vector<unsigned short> deletedSlots;
		if (deleted) deletedSlots.push_back(item.slotID);
		packetManager.SendPacket_Inventory(connection, updatedInv, deletedSlots);

		// 5. Sync updated character location (city:0, county:0, neighborhood:0) to client via Packet 157 UserUpdateInfo
		auto updatedChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		if (updatedChar.result) {
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, updatedChar.userCharacter });
		}

		// 6. Pop up modal dialog
		packet_UMsgManager.SendPacket_UMsg_DialogBox(connection, "已成功使用【更改個人地區券】！\n個人地區已重置為未登入狀態，請返回大廳主畫面點擊右下方「登入地區」按鈕重新選擇您的地區。");
		serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) successfully used ItemID {} (更改個人地區券) and reset region\n", user->GetUserLogName(), item.itemID));
		return;
	}

	// Special case: ItemID 82 (戰績重置券)
	if (item.itemID == 82 || itemDef.className == "ResetWinLose") {
		bool deleted = false;
		if (!databaseManager.ConsumeUserInventoryItemBySlot(userID, item.slotID, 1, deleted)) {
			serverConsole.Print(PrefixType::Error, format("[ Packet_ItemManager ] Failed to consume ItemID 82 slot {} for user ({})\n", item.slotID, user->GetUserLogName()));
			return;
		}

		databaseManager.ResetUserWinLose(userID);

		auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
		if (replyPacket != NULL) {
			replyPacket->WriteUInt8((unsigned char)Packet_ItemType::EquipItem);
			replyPacket->WriteUInt8(1);
			replyPacket->WriteString("");
			replyPacket->Send();
		}

		vector<InventoryItem> updatedInv = user->GetUserInventory();
		vector<unsigned short> deletedSlots;
		if (deleted) deletedSlots.push_back(item.slotID);
		packetManager.SendPacket_Inventory(connection, updatedInv, deletedSlots);

		auto updatedChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		if (updatedChar.result) {
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, updatedChar.userCharacter });
		}

		packet_UMsgManager.SendPacket_UMsg_DialogBox(connection, "已成功使用【戰績重置券】！\n您的勝負戰績已全部重置為 0。");
		serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) successfully used ItemID 82 and reset win/lose record\n", user->GetUserLogName()));
		return;
	}

	// Special case: ItemID 103 (殺敵/陣亡重置券)
	if (item.itemID == 103 || itemDef.className == "ResetKillDeath") {
		bool deleted = false;
		if (!databaseManager.ConsumeUserInventoryItemBySlot(userID, item.slotID, 1, deleted)) {
			serverConsole.Print(PrefixType::Error, format("[ Packet_ItemManager ] Failed to consume ItemID 103 slot {} for user ({})\n", item.slotID, user->GetUserLogName()));
			return;
		}

		databaseManager.ResetUserKillDeath(userID);

		auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
		if (replyPacket != NULL) {
			replyPacket->WriteUInt8((unsigned char)Packet_ItemType::EquipItem);
			replyPacket->WriteUInt8(1);
			replyPacket->WriteString("");
			replyPacket->Send();
		}

		vector<InventoryItem> updatedInv = user->GetUserInventory();
		vector<unsigned short> deletedSlots;
		if (deleted) deletedSlots.push_back(item.slotID);
		packetManager.SendPacket_Inventory(connection, updatedInv, deletedSlots);

		auto updatedChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		if (updatedChar.result) {
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, updatedChar.userCharacter });
		}

		packet_UMsgManager.SendPacket_UMsg_DialogBox(connection, "已成功使用【殺敵/陣亡重置券】！\n您的殺敵/陣亡記錄已全部重置為 0。");
		serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) successfully used ItemID 103 and reset kill/death record\n", user->GetUserLogName()));
		return;
	}

	// Level requirement check
	const RewardItemConfig* cfg = rewardItemManager.GetRewardItem(item.itemID);
	unsigned char reqLevel = itemDef.level;
	if (cfg != nullptr && cfg->requiredLevel > reqLevel) {
		reqLevel = cfg->requiredLevel;
	}

	if (reqLevel > 0) {
		auto charRes = user->GetUserCharacter(USERCHARACTER_FLAG_LEVEL);
		if (charRes.result && charRes.userCharacter.level < reqLevel) {
			auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
			if (replyPacket != NULL) {
				replyPacket->WriteUInt8((unsigned char)Packet_ItemType::EquipItem);
				replyPacket->WriteUInt8(0);
				replyPacket->WriteString("");
				replyPacket->Send();
			}
			return;
		}
	}

	// Item requirements check (if configured)
	if (cfg != nullptr && !cfg->requireItems.empty()) {
		auto currentInv = user->GetUserInventory();
		std::unordered_map<unsigned short, unsigned int> userItemCounts;
		for (const auto& invItem : currentInv) {
			userItemCounts[invItem.itemID] += invItem.count;
		}

		bool canUse = true;
		for (const auto& req : cfg->requireItems) {
			if (userItemCounts[req.itemID] < req.count) {
				canUse = false;
				break;
			}
		}

		if (!canUse) {
			auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
			if (replyPacket != NULL) {
				replyPacket->WriteUInt8((unsigned char)Packet_ItemType::EquipItem);
				replyPacket->WriteUInt8(0);
				replyPacket->WriteString("");
				replyPacket->Send();
			}

			if (!cfg->failMsgKey.empty()) {
				std::string cleanKey = cfg->failMsgKey;
				if (cleanKey.rfind("#CSO_Warning_", 0) == 0) {
					cleanKey = cleanKey.substr(13);
				}
				else if (cleanKey.rfind("CSO_Warning_", 0) == 0) {
					cleanKey = cleanKey.substr(12);
				}
				packet_UMsgManager.SendPacket_UMsg_ServerMessage(connection, Packet_UMsgType::WarningMessage, cleanKey);
			}

			serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] User ({}) attempted to use RewardItem {} ({}) but required items are insufficient!\n",
				user->GetUserLogName(), item.itemID, itemDef.name));
			return;
		}
	}

	RolledRewards rolled;
	if (!rewardItemManager.RollRewards(item.itemID, rolled)) {
		auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
		if (replyPacket != NULL) {
			replyPacket->WriteUInt8((unsigned char)Packet_ItemType::EquipItem);
			replyPacket->WriteUInt8(0);
			replyPacket->WriteString("");
			replyPacket->Send();
		}
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] User ({}) failed to roll rewards for Item {} ({})\n",
			user->GetUserLogName(), item.itemID, itemDef.name));
		return;
	}

	std::string titleKey = rolled.titleKey;
	std::string msgKey = rolled.msgKey;
	unsigned long long pointsToAdd = rolled.points;
	unsigned long long expToAdd = rolled.exp;
	std::string rewardSummary = "";

	struct RewardItemGrant {
		unsigned short itemID;
		short duration; // -1 or 0 = perm, > 0 = days
		unsigned short count;
	};

	std::vector<RewardItemGrant> itemsToGrant;
	for (const auto& r : rolled.items) {
		itemsToGrant.push_back({ r.itemID, r.duration, r.count });

		const auto* rDef = itemDefinitionManager.GetItem(r.itemID);
		std::string rName = (rDef != nullptr) ? rDef->name : r.name;
		bool rIsCount = (rDef != nullptr && !rDef->isPeriod);
		if (!rewardSummary.empty()) rewardSummary += "、";
		if (rIsCount || r.duration == 0) {
			if (r.count > 1) {
				rewardSummary += format("{}({}個)", rName, r.count);
			}
			else {
				rewardSummary += rName;
			}
		}
		else if (r.duration > 0) {
			rewardSummary += format("{}({}日)", rName, r.duration);
		}
		else if (r.duration == -1) {
			rewardSummary += format("{}(永久)", rName);
		}
	}

	if (pointsToAdd > 0) {
		if (!rewardSummary.empty()) rewardSummary += "、";
		rewardSummary += format("{} Point", pointsToAdd);
	}
	if (expToAdd > 0) {
		if (!rewardSummary.empty()) rewardSummary += "、";
		rewardSummary += format("{} EXP", expToAdd);
	}

	// 1. Consume required items and/or target reward item
	vector<unsigned short> deletedSlots;

	bool selfInRequired = false;
	unsigned short selfRequiredCount = 0;
	if (cfg != nullptr && !cfg->requireItems.empty()) {
		for (const auto& req : cfg->requireItems) {
			if (req.itemID == item.itemID) {
				selfInRequired = true;
				selfRequiredCount = req.count;
				break;
			}
		}
	}

	if (selfInRequired) {
		unsigned short remainingToConsume = selfRequiredCount;

		// 1. First consume from the clicked slot
		unsigned short clickedConsume = min((unsigned short)item.count, remainingToConsume);
		bool clickedDeleted = false;
		if (!databaseManager.ConsumeUserInventoryItemBySlot(userID, item.slotID, clickedConsume, clickedDeleted)) {
			serverConsole.Print(PrefixType::Error, format("[ Packet_ItemManager ] Failed to consume RewardItem slot {} for user ({})\n", item.slotID, user->GetUserLogName()));
			return;
		}
		if (clickedDeleted) deletedSlots.push_back(item.slotID);
		remainingToConsume -= clickedConsume;

		// 2. Consume from other slots of item.itemID if needed
		if (remainingToConsume > 0) {
			auto inv = user->GetUserInventory();
			for (const auto& invItem : inv) {
				if (invItem.itemID == item.itemID && invItem.slotID != item.slotID && remainingToConsume > 0) {
					unsigned short toConsume = min((unsigned short)invItem.count, remainingToConsume);
					bool del = false;
					databaseManager.ConsumeUserInventoryItemBySlot(userID, invItem.slotID, toConsume, del);
					if (del) deletedSlots.push_back(invItem.slotID);
					remainingToConsume -= toConsume;
				}
			}
		}

		// 3. Consume other items in requireItems (if any)
		for (const auto& req : cfg->requireItems) {
			if (req.itemID != item.itemID) {
				unsigned short reqRem = req.count;
				auto inv = user->GetUserInventory();
				for (const auto& invItem : inv) {
					if (invItem.itemID == req.itemID && reqRem > 0) {
						unsigned short toConsume = min((unsigned short)invItem.count, reqRem);
						bool del = false;
						databaseManager.ConsumeUserInventoryItemBySlot(userID, invItem.slotID, toConsume, del);
						if (del) deletedSlots.push_back(invItem.slotID);
						reqRem -= toConsume;
					}
				}
			}
		}
	}
	else {
		// Standard consumption: consume 1 of the clicked reward item
		bool deleted = false;
		if (!databaseManager.ConsumeUserInventoryItemBySlot(userID, item.slotID, 1, deleted)) {
			serverConsole.Print(PrefixType::Error, format("[ Packet_ItemManager ] Failed to consume RewardItem slot {} for user ({})\n", item.slotID, user->GetUserLogName()));
			return;
		}
		if (deleted) deletedSlots.push_back(item.slotID);

		// Consume all items in requireItems (if any)
		if (cfg != nullptr && !cfg->requireItems.empty()) {
			for (const auto& req : cfg->requireItems) {
				unsigned short reqRem = req.count;
				auto inv = user->GetUserInventory();
				for (const auto& invItem : inv) {
					if (invItem.itemID == req.itemID && reqRem > 0) {
						unsigned short toConsume = min((unsigned short)invItem.count, reqRem);
						bool del = false;
						databaseManager.ConsumeUserInventoryItemBySlot(userID, invItem.slotID, toConsume, del);
						if (del) deletedSlots.push_back(invItem.slotID);
						reqRem -= toConsume;
					}
				}
			}
		}
	}

	// 2. Grant rewards
	auto curInv = user->GetUserInventory();
	unsigned short maxSlotID = 0;
	for (const auto& invItem : curInv) {
		if (invItem.slotID > maxSlotID) maxSlotID = invItem.slotID;
	}

	time_t now = time(nullptr);
	std::vector<InventoryItem> newItemsToAdd;

	for (const auto& g : itemsToGrant) {
		const auto* def = itemDefinitionManager.GetItem(g.itemID);
		bool isCountItem = (def != nullptr && !def->isPeriod);
		bool isPeriod = !isCountItem && ((g.duration != 0) || (def != nullptr && def->isPeriod));

		auto found = std::find_if(curInv.begin(), curInv.end(), [&](const InventoryItem& x) {
			return x.itemID == g.itemID;
		});

		if (isCountItem || (!isPeriod && g.duration == 0)) {
			// Count item
			if (found != curInv.end()) {
				databaseManager.UpdateUserInventoryItemCount(userID, g.itemID, g.count);
			}
			else {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = g.itemID;
				newItem.count = g.count;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = 0;
				newItem.enhanceBits = 0;
				newItemsToAdd.push_back(newItem);
			}
		}
		else {
			// Duration item
			bool isPerm = (g.duration == -1 || g.duration == 0);
			if (found != curInv.end()) {
				if (found->expirationTime == 0) {
					// already perm
				}
				else if (isPerm) {
					databaseManager.UpdateUserInventoryExpiration(userID, g.itemID, 0);
				}
				else {
					unsigned long baseTime = (found->expirationTime > (unsigned long)now) ? found->expirationTime : (unsigned long)now;
					unsigned long newExp = baseTime + (unsigned long)g.duration * 86400;
					databaseManager.UpdateUserInventoryExpiration(userID, g.itemID, newExp);
				}
			}
			else {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = g.itemID;
				newItem.count = 1;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = isPerm ? 0 : (unsigned long)(now + (unsigned long)g.duration * 86400);
				newItem.enhanceBits = 0;
				newItemsToAdd.push_back(newItem);
			}
		}
	}

	if (!newItemsToAdd.empty()) {
		databaseManager.AddUserInventoryItems(userID, newItemsToAdd);
	}

	// 3. Grant points & EXP
	if (pointsToAdd > 0) {
		databaseManager.AddUserFunds(userID, true, pointsToAdd);
	}
	if (expToAdd > 0) {
		unsigned char newLevel = 0;
		databaseManager.AddUserExp(userID, expToAdd, &newLevel);
	}

	// 4. Send Packet 78 success reply to client
	auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (replyPacket != NULL) {
		replyPacket->WriteUInt8((unsigned char)Packet_ItemType::EquipItem);
		replyPacket->WriteUInt8(1); // success
		replyPacket->WriteString("");
		replyPacket->Send();
	}

	// 5. Sync updated inventory to client
	// 5. Sync updated inventory to client
	vector<InventoryItem> updatedInv = user->GetUserInventory();
	packetManager.SendPacket_Inventory(connection, updatedInv, deletedSlots);

	// 6. Sync updated character (Points, EXP, Level)
	auto updatedChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (updatedChar.result) {
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, updatedChar.userCharacter });
	}

	// 7. Pop up ItemAcceptDlg modal (ItemAwardNotice - Packet 67 Type 60)
	std::vector<AwardItem> awardItems;
	for (const auto& g : itemsToGrant) {
		const auto* def = itemDefinitionManager.GetItem(g.itemID);
		bool isCountItem = (def != nullptr && !def->isPeriod);

		AwardItem aItem;
		aItem.itemID = g.itemID;
		aItem.count = g.count;
		aItem.duration = isCountItem ? 0 : ((g.duration > 0) ? (unsigned short)g.duration : (g.duration == -1 ? 0xFFFF : 0));
		aItem.flags = 0;
		awardItems.push_back(aItem);
	}

	auto SanitizeKey = [](const std::string& key) -> std::string {
		if (key.rfind("#CSO_Warning_", 0) == 0) return key.substr(13);
		if (key.rfind("CSO_Warning_", 0) == 0) return key.substr(12);
		return key;
	};

	packet_UMsgManager.SendPacket_UMsg_ItemAwardNotice(
		connection,
		awardItems,
		(unsigned int)pointsToAdd,
		(unsigned int)expToAdd,
		0,
		SanitizeKey(titleKey),
		SanitizeKey(msgKey)
	);

	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) opened RewardItem {} ({}), rewards: {}\n",
		user->GetUserLogName(), item.itemID, itemDef.name, rewardSummary));
}

void Packet_ItemManager::parsePacket_Item_Tattoo(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned short slotID = packet->ReadUInt16_LE();

	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) has sent Packet_Item Tattoo - slotID: {}\n", user->GetUserLogName(), slotID));

	unsigned long userID = user->GetUserID();
	auto inventory = user->GetUserInventory();

	auto it = find_if(inventory.begin(), inventory.end(), [slotID](const InventoryItem& item) {
		return item.slotID == slotID;
	});

	if (it != inventory.end()) {
		if (it->inUse) {
			// Unequip target tattoo
			databaseManager.SetUserInventoryItemInUse(userID, slotID, false);
		}
		else {
			// Equipping target tattoo: unequip any other tattoo currently equipped
			for (const auto& item : inventory) {
				if (item.inUse && item.slotID != slotID && IsTattooItem(item.itemID)) {
					databaseManager.SetUserInventoryItemInUse(userID, item.slotID, false);
				}
			}
			// Equip target tattoo
			databaseManager.SetUserInventoryItemInUse(userID, slotID, true);
		}
	}

	// Reply to client: Packet 78, type 9, status 1
	auto replyPacket = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Item });
	if (replyPacket != NULL) {
		replyPacket->WriteUInt8((unsigned char)Packet_ItemType::Tattoo);
		replyPacket->WriteUInt8(1); // status: 1 = success
		replyPacket->Send();
	}

	// Send refreshed inventory so client updates equipped badges and button text (卸除 / 裝備)
	packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
}

void Packet_ItemManager::parsePacket_Item_OpenBox(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned short decoderSlotID = packet->ReadUInt16_LE();

	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) open box request: decoderSlot {}\n",
		user->GetUserLogName(), decoderSlotID));

	itemBoxManager.OpenBox(user, decoderSlotID, 0, 0);
}

void Packet_ItemManager::parsePacket_Item_Roulette(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned char action = packet->ReadUInt8();
	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) sent Packet_Item Roulette action {}\n",
		user->GetUserLogName(), action));

	if (action == 0) {
		attendanceManager.SendAttendanceInfo(user);
	}
	else if (action == 1) {
		attendanceManager.ClaimAttendanceReward(user);
	}
	else {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] User ({}) sent unknown Roulette action {}\n",
			user->GetUserLogName(), action));
	}
}

void Packet_ItemManager::parsePacket_Item_Craft(User* user, TCPConnection::Packet::pointer packet) {
	if (user == nullptr || packet == nullptr) {
		return;
	}

	unsigned char action = packet->ReadUInt8(); // 1 = confirm craft
	unsigned short bpSlotID = packet->ReadUInt16_LE();
	unsigned short durSlotID = packet->ReadUInt16_LE();
	unsigned short secSlotID1 = packet->ReadUInt16_LE();
	unsigned short secSlotID2 = packet->ReadUInt16_LE();

	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) craft request: action={}, bpSlot={}, durSlot={}, sec1={}, sec2={}\n",
		user->GetUserLogName(), action, bpSlotID, durSlotID, secSlotID1, secSlotID2));

	if (action == 0) {
		// Action 0: Client preview / query success rate and fee
		craftManager.QueryCraftPreview(user, bpSlotID, durSlotID, secSlotID1, secSlotID2);
	}
	else if (action == 1) {
		// Action 1: Client confirmed craft ("make_done")
		craftManager.CraftWeapon(user, bpSlotID, durSlotID, secSlotID1, secSlotID2);
	}
	else {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ItemManager ] User ({}) sent unknown craft action {}\n",
			user->GetUserLogName(), action));
	}
}

void Packet_ItemManager::parsePacket_Item_Disassemble(User* user, TCPConnection::Packet::pointer packet) {
	if (user == nullptr || packet == nullptr) {
		return;
	}

	unsigned char count = packet->ReadUInt8();
	std::vector<unsigned short> slots;
	for (unsigned char i = 0; i < count; ++i) {
		slots.push_back(packet->ReadUInt16_LE());
	}

	unsigned char extraCount = 0;
	if ((size_t)packet->GetReadOffset() < packet->GetBuffer().size()) {
		extraCount = packet->ReadUInt8();
		for (unsigned char i = 0; i < extraCount; ++i) {
			packet->ReadUInt16_LE();
		}
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) disassemble request: count={}\n",
		user->GetUserLogName(), count));

	craftManager.DisassembleItems(user, slots);
}

void Packet_ItemManager::parsePacket_Item_MaterialAssem(User* user, TCPConnection::Packet::pointer packet) {
	if (user == nullptr || packet == nullptr) {
		return;
	}

	unsigned short slot0 = packet->ReadUInt16_LE();
	unsigned short slot1 = packet->ReadUInt16_LE();
	unsigned short slot2 = packet->ReadUInt16_LE();

	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) material assembly request: slot0={}, slot1={}, slot2={}\n",
		user->GetUserLogName(), slot0, slot1, slot2));

	std::vector<unsigned short> slots;
	if (slot0 != 0xFFFF) slots.push_back(slot0);
	if (slot1 != 0xFFFF) slots.push_back(slot1);
	if (slot2 != 0xFFFF) slots.push_back(slot2);

	craftManager.AssembleMaterials(user, slots);
}

void Packet_ItemManager::parsePacket_Item_Enhance(User* user, TCPConnection::Packet::pointer packet) {
	if (user == nullptr || packet == nullptr) {
		return;
	}

	unsigned char action = packet->ReadUInt8(); // 1
	unsigned short weaponSlotID = packet->ReadUInt16_LE();
	unsigned short materialSlotID = packet->ReadUInt16_LE();

	serverConsole.Print(PrefixType::Info, format("[ Packet_ItemManager ] User ({}) weapon enhance request: action={}, weaponSlot={}, matSlot={}\n",
		user->GetUserLogName(), action, weaponSlotID, materialSlotID));

	enhanceManager.HandleEnhance(user, weaponSlotID, materialSlotID);
}

