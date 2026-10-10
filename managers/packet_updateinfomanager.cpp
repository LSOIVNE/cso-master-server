#include "packet_updateinfomanager.h"
#include "packetmanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "databasemanager.h"
#include "packet_charactermanager.h"
#include "roommanager.h"
#include "packet_umsgmanager.h"

Packet_UpdateInfoManager packet_UpdateInfoManager;

void Packet_UpdateInfoManager::ParsePacket_UpdateInfo(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (!userManager.IsUserLoggedIn(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_UpdateInfoManager ] Client ({}) has sent Packet_UpdateInfo, but it's not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_UpdateInfoManager ] Parsing Packet_UpdateInfo from user ({})\n", user->GetUserLogName()));

	unsigned char type = packet->ReadUInt8();
	
	switch (type) {
		case 0: {
			parsePacket_UpdateInfo_Unk0(user, packet);
			break;
		}
		case 1: {
			parsePacket_UpdateInfo_Unk1(user);
			break;
		}
		case 2: {
			parsePacket_UpdateInfo_ChangeNickname(user, packet);
			break;
		}
		case 3: {
			parsePacket_UpdateInfo_ChangeCamoNickname(user, packet);
			break;
		}
		case 4: {
			parsePacket_UpdateInfo_SetRegion(user, packet);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_UpdateInfoManager ] User ({}) has sent unregistered Packet_UpdateInfo type {}!\n", user->GetUserLogName(), type));
			break;
		}
	}
}

void Packet_UpdateInfoManager::SendPacket_UpdateInfo(const GameUser& gameUser) {
	if (gameUser.user == NULL) {
		return;
	}

	auto& connection = gameUser.user->GetConnection();
	if (connection == NULL) {
		return;
	}

	// 2013 client registers UserUpdateInfo (157) for CPlayerManager HUD and local player initialization
	auto packet2013 = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UserUpdateInfo });
	if (packet2013 != NULL) {
		packet2013->WriteUInt32_LE(gameUser.user->GetUserID());
		packetManager.BuildUserCharacter(packet2013, gameUser.userCharacter);
		packet2013->Send();
	}
}

void Packet_UpdateInfoManager::SendPacket_UpdateInfo_ChangeNicknameResult(TCPConnection::pointer connection, bool success, const string& errorToken, unsigned char type) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UpdateInfo });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(type); // 2 = nickname change, 3 = camo nickname change
	if (success) {
		packet->WriteUInt8(2); // result = 2 (SUCCESS: client shows #CSO_Success_Change_NickName / #CSO_Success_Change_CNickName and closes modal)
	}
	else {
		packet->WriteUInt8(1); // result = 1 (failure: client reads error string)
		packet->WriteString(errorToken); // client prepends '#' and looks up localization token
	}

	packet->Send();
}

void Packet_UpdateInfoManager::SendPacket_UserUpdateInfo_ChatCount(TCPConnection::pointer connection, unsigned long userID, unsigned char count) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UserUpdateInfo });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt32_LE(userID);
	packet->WriteUInt32_LE(USERCHARACTER_FLAG_CHATCOUNT); // 0x200000
	packet->WriteUInt8(count);                            // remaining lobby chat quota (255)
	packet->WriteUInt32_LE(0);                            // expiration / timeout (0 = never expires)

	packet->Send();
}

void Packet_UpdateInfoManager::SendPacket_UserUpdateInfo_CBoxCount(TCPConnection::pointer connection, unsigned long userID, unsigned short count) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UserUpdateInfo });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt32_LE(userID);
	packet->WriteUInt32_LE(USERCHARACTER_FLAG_CBOXCOUNT); // 0x10000: CBox count (UInt16)
	packet->WriteUInt16_LE(count);

	packet->Send();
}

void Packet_UpdateInfoManager::parsePacket_UpdateInfo_Unk0(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	const string& unk1 = packet->ReadString();

	serverConsole.Print(PrefixType::Info, format("[ Packet_UpdateInfoManager ] User ({}) has sent Packet_UpdateInfo Unk0 - unk1: {}\n", user->GetUserLogName(), unk1));
}

void Packet_UpdateInfoManager::parsePacket_UpdateInfo_Unk1(User* user) {
	if (user == NULL) {
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_UpdateInfoManager ] User ({}) has sent Packet_UpdateInfo Unk1\n", user->GetUserLogName()));
}

void Packet_UpdateInfoManager::parsePacket_UpdateInfo_ChangeNickname(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	const string& newNickName = packet->ReadString();

	serverConsole.Print(PrefixType::Info, format("[ Packet_UpdateInfoManager ] User ({}) requested nickname change to '{}'\n",
		user->GetUserLogName(), Cp950ToUtf8(newNickName)));

	// 1. Validate nickname format and rules (length, characters, repetition, prohibited)
	Packet_ReplyType validateResult = ValidateNickname(newNickName);
	if (validateResult != Packet_ReplyType::NoReply) {
		string errToken = "CSO_NICKNAME_INVALID_NAME_CHAR";
		switch (validateResult) {
			case Packet_ReplyType::ID_TOO_SHORT:
				errToken = "CSO_NICKNAME_TOO_SHORT";
				break;
			case Packet_ReplyType::ID_TOO_LONG:
				errToken = "CSO_NICKNAME_TOO_LONG";
				break;
			case Packet_ReplyType::INVALID_CHAR:
				errToken = "CSO_NICKNAME_INVALID_NAME_CHAR";
				break;
			case Packet_ReplyType::ID_DIGIT_BEFORE_CHAR:
				errToken = "CSO_NICKNAME_DIGIT_BEFORE_CHAR";
				break;
			case Packet_ReplyType::ID_EXCEED_CHAR_COUNT:
			case Packet_ReplyType::ID_PROHIBITED:
				errToken = "CSO_NICKNAME_PROHIBITED";
				break;
			default:
				errToken = "CSO_NICKNAME_INVALID_NAME_CHAR";
				break;
		}
		SendPacket_UpdateInfo_ChangeNicknameResult(connection, false, errToken);
		return;
	}

	// 2. Check if new nickname is identical to current nickname
	auto currentChar = user->GetUserCharacter(USERCHARACTER_FLAG_NICKNAME);
	if (currentChar.result && currentChar.userCharacter.nickName == newNickName) {
		SendPacket_UpdateInfo_ChangeNicknameResult(connection, false, "CSO_NICKNAME_ALREADY_EXIST");
		return;
	}

	// 3. Check if new nickname already exists in database
	if (databaseManager.IsNickNameExists(newNickName)) {
		SendPacket_UpdateInfo_ChangeNicknameResult(connection, false, "CSO_NICKNAME_ALREADY_EXIST");
		return;
	}

	// 4. Check if user owns rename ticket (Item 65)
	vector<InventoryItem> inv = user->GetUserInventory();
	bool hasTicket = false;
	unsigned short ticketSlotID = 0;
	for (const auto& item : inv) {
		if (item.itemID == 65 && item.count > 0) {
			hasTicket = true;
			ticketSlotID = item.slotID;
			break;
		}
	}

	if (!hasTicket) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_UpdateInfoManager ] User ({}) attempted to change nickname without rename ticket (item 65)!\n",
			user->GetUserLogName()));
		SendPacket_UpdateInfo_ChangeNicknameResult(connection, false, "CSO_NEED_NICKNAME_ITEM");
		return;
	}

	// 5. Update database user_characters table
	if (!databaseManager.UpdateUserNickName(user->GetUserID(), newNickName)) {
		serverConsole.Print(PrefixType::Error, format("[ Packet_UpdateInfoManager ] Failed to update nickname for user ({})\n",
			user->GetUserLogName()));
		SendPacket_UpdateInfo_ChangeNicknameResult(connection, false, "CSO_NICKNAME_ALREADY_EXIST");
		return;
	}

	// 6. Consume 1 rename ticket from user inventory
	bool ticketDeleted = false;
	if (!databaseManager.ConsumeUserInventoryItemBySlot(user->GetUserID(), ticketSlotID, 1, ticketDeleted)) {
		serverConsole.Print(PrefixType::Error, format("[ Packet_UpdateInfoManager ] Failed to consume rename ticket slot {} for user ({})\n",
			ticketSlotID, user->GetUserLogName()));
		SendPacket_UpdateInfo_ChangeNicknameResult(connection, false, "CSO_NEED_NICKNAME_ITEM");
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_UpdateInfoManager ] User ({}) successfully changed nickname to '{}'!\n",
		user->GetUserLogName(), Cp950ToUtf8(newNickName)));

	// 7. Send Packet 69 confirmation to client (triggers client's #CSO_Success_Change_NickName notice & closes modal)
	SendPacket_UpdateInfo_ChangeNicknameResult(connection, true);

	// 8. Sync updated inventory (Packet 154)
	vector<unsigned short> deletedSlots;
	if (ticketDeleted) {
		deletedSlots.push_back(ticketSlotID);
	}
	vector<InventoryItem> updatedInv = user->GetUserInventory();
	packetManager.SendPacket_Inventory(connection, updatedInv, deletedSlots);

	// 9. Sync updated user character to user (Packet 157)
	auto updatedChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (updatedChar.result) {
		SendPacket_UpdateInfo({ user, updatedChar.userCharacter });
	}

	// 10. Sync updated player info to other players
	if (user->GetCurrentRoomID() > 0) {
		Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
		if (room != NULL) {
			for (User* roomUser : room->GetRoomUsers()) {
				if (roomUser != NULL && roomUser != user && roomUser->GetConnection() != NULL) {
					auto roomPacket = TCPConnection::Packet::Create(PacketSource::Server, roomUser->GetConnection(), { (unsigned char)PacketID::UserUpdateInfo });
					if (roomPacket != NULL) {
						roomPacket->WriteUInt32_LE(user->GetUserID());
						packetManager.BuildUserCharacter(roomPacket, updatedChar.userCharacter);
						roomPacket->Send();
					}
				}
			}
		}
	}
	else {
		userManager.SendAddUserPacketToAll(user);
	}
}

void Packet_UpdateInfoManager::parsePacket_UpdateInfo_ChangeCamoNickname(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned short slotID = packet->ReadUInt16_LE();
	const string& newCamoNickName = packet->ReadString();

	serverConsole.Print(PrefixType::Info, format("[ Packet_UpdateInfoManager ] User ({}) requested camo nickname change to '{}' (slotID: {})\n",
		user->GetUserLogName(), Cp950ToUtf8(newCamoNickName), slotID));

	// 1. Validate nickname format and rules (length, characters, repetition, prohibited)
	Packet_ReplyType validateResult = ValidateNickname(newCamoNickName);
	if (validateResult != Packet_ReplyType::NoReply) {
		string errToken = "CSO_NICKNAME_INVALID_NAME_CHAR";
		switch (validateResult) {
			case Packet_ReplyType::ID_TOO_SHORT:
				errToken = "CSO_NICKNAME_TOO_SHORT";
				break;
			case Packet_ReplyType::ID_TOO_LONG:
				errToken = "CSO_NICKNAME_TOO_LONG";
				break;
			case Packet_ReplyType::INVALID_CHAR:
				errToken = "CSO_NICKNAME_INVALID_NAME_CHAR";
				break;
			case Packet_ReplyType::ID_DIGIT_BEFORE_CHAR:
				errToken = "CSO_NICKNAME_DIGIT_BEFORE_CHAR";
				break;
			case Packet_ReplyType::ID_EXCEED_CHAR_COUNT:
			case Packet_ReplyType::ID_PROHIBITED:
				errToken = "CSO_NICKNAME_PROHIBITED";
				break;
			default:
				errToken = "CSO_NICKNAME_INVALID_NAME_CHAR";
				break;
		}
		SendPacket_UpdateInfo_ChangeNicknameResult(connection, false, errToken, 3);
		return;
	}

	// 2. Check if user owns camouflage nickname ticket (Item 68) and it is not expired
	vector<InventoryItem> inv = user->GetUserInventory();
	bool hasTicket = false;
	InventoryItem targetItem;
	time_t now = time(NULL);
	for (const auto& item : inv) {
		if (item.itemID == 68 && (slotID == 0 || item.slotID == slotID)) {
			if (item.expirationTime == 0 || item.expirationTime > (unsigned long)now) {
				hasTicket = true;
				targetItem = item;
				break;
			}
		}
	}
	if (!hasTicket) {
		for (const auto& item : inv) {
			if (item.itemID == 68) {
				if (item.expirationTime == 0 || item.expirationTime > (unsigned long)now) {
					hasTicket = true;
					targetItem = item;
					break;
				}
			}
		}
	}

	if (!hasTicket) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_UpdateInfoManager ] User ({}) attempted to change camo nickname without valid camo ticket (item 68)!\n",
			user->GetUserLogName()));
		SendPacket_UpdateInfo_ChangeNicknameResult(connection, false, "CSO_NEED_NICKNAME_ITEM", 3);
		return;
	}

	// 3. Update database user_characters table (unk4_1)
	if (!databaseManager.UpdateUserCamoNickName(user->GetUserID(), newCamoNickName)) {
		serverConsole.Print(PrefixType::Error, format("[ Packet_UpdateInfoManager ] Failed to update camo nickname for user ({})\n",
			user->GetUserLogName()));
		SendPacket_UpdateInfo_ChangeNicknameResult(connection, false, "CSO_NICKNAME_INVALID_NAME_CHAR", 3);
		return;
	}

	// 4. Update item in inventory: mark inUse = true and sync delta to client
	databaseManager.SetUserInventoryItemInUse(user->GetUserID(), targetItem.slotID, true);
	targetItem.inUse = true;
	targetItem.activeFlag = 1;

	serverConsole.Print(PrefixType::Info, format("[ Packet_UpdateInfoManager ] User ({}) successfully changed camo nickname to '{}' (activated item 68 slot {})!\n",
		user->GetUserLogName(), Cp950ToUtf8(newCamoNickName), targetItem.slotID));

	// 5. Send Packet 69 confirmation to client (type 3, result 2 -> #CSO_Success_Change_CNickName)
	SendPacket_UpdateInfo_ChangeNicknameResult(connection, true, "", 3);

	// 6. Send InventoryDelta (Packet 5) to update item state from [未使用] to [使用中]!
	packetManager.SendPacket_InventoryDelta(connection, { targetItem });

	// 7. Sync updated user character to user (Packet 157) so NickNameCEntry in "我的情報" updates immediately
	auto updatedChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (updatedChar.result) {
		SendPacket_UpdateInfo({ user, updatedChar.userCharacter });
	}

	// 6. Sync updated player info to other room players if in room
	if (user->GetCurrentRoomID() > 0) {
		Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
		if (room != NULL) {
			for (User* roomUser : room->GetRoomUsers()) {
				if (roomUser != NULL && roomUser != user && roomUser->GetConnection() != NULL) {
					auto roomPacket = TCPConnection::Packet::Create(PacketSource::Server, roomUser->GetConnection(), { (unsigned char)PacketID::UserUpdateInfo });
					if (roomPacket != NULL) {
						roomPacket->WriteUInt32_LE(user->GetUserID());
						packetManager.BuildUserCharacter(roomPacket, updatedChar.userCharacter);
						roomPacket->Send();
					}
				}
			}
		}
	}
	else {
		userManager.SendAddUserPacketToAll(user);
	}
}

void Packet_UpdateInfoManager::parsePacket_UpdateInfo_SetRegion(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned short city = packet->ReadUInt16_LE();
	unsigned short county = packet->ReadUInt16_LE();
	unsigned short neighborhood = packet->ReadUInt16_LE();

	serverConsole.Print(PrefixType::Info, format("[ Packet_UpdateInfoManager ] User ({}) has sent SetRegion (city: {}, county: {}, neighborhood: {})\n",
		user->GetUserLogName(), city, county, neighborhood));

	unsigned long userID = user->GetUserID();
	auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_LOCATION);
	if (!charResult.result) {
		return;
	}

	unsigned short oldCity = charResult.userCharacter.city;

	// If changing region from a previously set region, check & consume ItemID 80 (更改個人地區券) if owned
	if (oldCity > 0 && oldCity != city) {
		auto inv = user->GetUserInventory();
		bool hasTicket = false;
		unsigned short ticketSlotID = 0;
		for (const auto& item : inv) {
			if (item.itemID == 80 && item.count > 0) {
				hasTicket = true;
				ticketSlotID = item.slotID;
				break;
			}
		}

		if (hasTicket) {
			bool deleted = false;
			databaseManager.ConsumeUserInventoryItemBySlot(userID, ticketSlotID, 1, deleted);
			vector<InventoryItem> updatedInv = user->GetUserInventory();
			vector<unsigned short> deletedSlots;
			if (deleted) deletedSlots.push_back(ticketSlotID);
			packetManager.SendPacket_Inventory(connection, updatedInv, deletedSlots);
			serverConsole.Print(PrefixType::Info, format("[ Packet_UpdateInfoManager ] User ({}) consumed ItemID 80 (更改個人地區券) for region change\n", user->GetUserLogName()));
		}
	}

	// 1. Update database
	databaseManager.UpdateUserLocation(userID, city, county, neighborhood);

	// 2. Fetch full updated character
	auto updatedChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (updatedChar.result) {
		// Sync updated character (Packet 157 UserUpdateInfo) to client
		SendPacket_UpdateInfo({ user, updatedChar.userCharacter });

		// Sync to room members if in room
		if (user->GetCurrentRoomID() > 0) {
			Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
			if (room != NULL) {
				for (User* roomUser : room->GetRoomUsers()) {
					if (roomUser != NULL && roomUser != user && roomUser->GetConnection() != NULL) {
						auto roomPacket = TCPConnection::Packet::Create(PacketSource::Server, roomUser->GetConnection(), { (unsigned char)PacketID::UserUpdateInfo });
						if (roomPacket != NULL) {
							roomPacket->WriteUInt32_LE(user->GetUserID());
							packetManager.BuildUserCharacter(roomPacket, updatedChar.userCharacter);
							roomPacket->Send();
						}
					}
				}
			}
		}
		else {
			userManager.SendAddUserPacketToAll(user);
		}
	}
}


