#include "packet_itemmanager.h"
#include "usermanager.h"
#include "roommanager.h"
#include "databasemanager.h"
#include "packetmanager.h"
#include "serverconsole.h"
#include "itemdefinitionmanager.h"
#include "itemboxmanager.h"
#include "craftmanager.h"
#include "attendancemanager.h"
#include <unordered_set>

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

	craftManager.CraftWeapon(user, bpSlotID, durSlotID, secSlotID1, secSlotID2);
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
