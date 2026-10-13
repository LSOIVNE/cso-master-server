#include "packet_favoritemanager.h"
#include "usermanager.h"
#include "serverconsole.h"

Packet_FavoriteManager packet_FavoriteManager;

void Packet_FavoriteManager::ParsePacket_Favorite(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (!userManager.IsUserLoggedIn(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_FavoriteManager ] Client ({}) has sent Packet_Favorite, but it's not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_FavoriteManager ] Parsing Packet_Favorite from user ({})\n", user->GetUserLogName()));

	unsigned char type = packet->ReadUInt8();

	switch (type) {
		case Packet_FavoriteType::UserBuyMenu: {
			parsePacket_Favorite_UserBuyMenu(user, packet);
			break;
		}
		case Packet_FavoriteType::UserBookMark: {
			parsePacket_Favorite_UserBookMark(user, packet);
			break;
		}
		case Packet_FavoriteType::UserItemSet: {
			parsePacket_Favorite_UserItemSet(user, packet);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_FavoriteManager ] User ({}) has sent unregistered Packet_Favorite type: {}!\n", user->GetUserLogName(), type));
			break;
		}
	}
}

void Packet_FavoriteManager::SendPacket_Favorite_UserBuyMenu(TCPConnection::pointer connection, const vector<BuyMenu>& userBuyMenus) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Favorite });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_FavoriteType::UserBuyMenu);

	for (unsigned char cat = 0; cat < BUYMENU_TOTAL_CATEGORIES; ++cat) {
		packet->WriteUInt8(cat);
		const BuyMenu* foundMenu = nullptr;
		for (const auto& bm : userBuyMenus) {
			if (bm.categoryID == cat) {
				foundMenu = &bm;
				break;
			}
		}
		for (size_t i = 0; i < 9; ++i) {
			unsigned short itemID = (foundMenu && i < foundMenu->items.size()) ? foundMenu->items[i] : 0;
			packet->WriteUInt16_LE(itemID);
		}
	}

	packet->Send();
}

void Packet_FavoriteManager::SendPacket_Favorite_UserBookMark(TCPConnection::pointer connection, const vector<BookMark>& userBookMarks) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Favorite });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_FavoriteType::UserBookMark);

	for (unsigned char slot = 0; slot < 5; ++slot) {
		const BookMark* foundBm = nullptr;
		for (const auto& bm : userBookMarks) {
			if (bm.slotID == slot) {
				foundBm = &bm;
				break;
			}
		}

		packet->WriteUInt8(slot);
		packet->WriteString(foundBm ? foundBm->name : "");
		packet->WriteUInt16_LE(foundBm ? foundBm->primaryItemID : 0);
		packet->WriteUInt16_LE(foundBm ? (unsigned short)foundBm->primaryAmmo : 0);
		packet->WriteUInt16_LE(foundBm ? foundBm->secondaryItemID : 0);
		packet->WriteUInt16_LE(foundBm ? (unsigned short)foundBm->secondaryAmmo : 0);
		packet->WriteUInt16_LE(foundBm ? foundBm->flashbang : 0);
		packet->WriteUInt16_LE(foundBm ? (unsigned short)foundBm->hegrenade : 0);
		packet->WriteUInt16_LE(foundBm ? (unsigned short)foundBm->smokegrenade : 0);
		packet->WriteUInt16_LE(foundBm ? (unsigned short)foundBm->defusekit : 0);
		packet->WriteUInt16_LE(foundBm ? (unsigned short)foundBm->nightvision : 0);
		packet->WriteUInt16_LE(foundBm ? foundBm->kevlar : 0);
		packet->WriteUInt16_LE(foundBm ? foundBm->unk1 : 0);
	}

	packet->Send();
}

void Packet_FavoriteManager::parsePacket_Favorite_UserBuyMenu(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	unsigned char categoryID = packet->ReadUInt8();

	if (categoryID >= BUYMENU_TOTAL_CATEGORIES) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_FavoriteManager ] User ({}) has sent Packet_Favorite UserBuyMenu, but its categoryID is invalid: {}!\n", user->GetUserLogName(), categoryID));
		return;
	}

	unsigned char slotID = packet->ReadUInt8();

	if (slotID >= BUYMENU_MAX_SLOT) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_FavoriteManager ] User ({}) has sent Packet_Favorite UserBuyMenu, but its slotID is invalid: {}!\n", user->GetUserLogName(), slotID));
		return;
	}

	unsigned short itemID = packet->ReadUInt16_LE();

	if (user->SaveUserBuyMenu(categoryID, slotID, itemID)) {
		serverConsole.Print(PrefixType::Info, format("[ Packet_FavoriteManager ] User ({}) has sent Packet_Favorite UserBuyMenu, userBuyMenu saved successfully!\n", user->GetUserLogName()));
	}
	else {
		serverConsole.Print(PrefixType::Error, format("[ Packet_FavoriteManager ] User ({}) has sent Packet_Favorite UserBuyMenu, failed to save userBuyMenu!\n", user->GetUserLogName()));
	}
}

void Packet_FavoriteManager::parsePacket_Favorite_UserBookMark(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	unsigned char slotID = packet->ReadUInt8();

	if (slotID >= BOOKMARK_MAX_SLOT) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_FavoriteManager ] User ({}) has sent Packet_Favorite UserBookMark, but its slotID is invalid: {}!\n", user->GetUserLogName(), slotID));
		return;
	}

	string name = packet->ReadString();

	if (name.size() > BOOKMARK_NAME_MAX_SIZE) {
		name.resize(BOOKMARK_NAME_MAX_SIZE);
	}

	unsigned short primaryItemID = packet->ReadUInt16_LE();
	unsigned short primaryAmmo = packet->ReadUInt16_LE();
	unsigned short secondaryItemID = packet->ReadUInt16_LE();
	unsigned short secondaryAmmo = packet->ReadUInt16_LE();
	unsigned short flashbang = packet->ReadUInt16_LE();
	unsigned short hegrenade = packet->ReadUInt16_LE();
	unsigned short smokegrenade = packet->ReadUInt16_LE();
	unsigned short defusekit = packet->ReadUInt16_LE();
	unsigned short nightvision = packet->ReadUInt16_LE();
	unsigned short kevlar = packet->ReadUInt16_LE();
	unsigned short unk1 = packet->ReadUInt16_LE();

	BookMark userBookMark{ slotID, name, primaryItemID, primaryAmmo, secondaryItemID, secondaryAmmo, flashbang, hegrenade, smokegrenade, defusekit, nightvision, kevlar, unk1 };

	if (user->SaveUserBookMark(userBookMark)) {
		serverConsole.Print(PrefixType::Info, format("[ Packet_FavoriteManager ] User ({}) has sent Packet_Favorite UserBookMark, userBookMark saved successfully!\n", user->GetUserLogName()));
	}
	else {
		serverConsole.Print(PrefixType::Error, format("[ Packet_FavoriteManager ] User ({}) has sent Packet_Favorite UserBookMark, failed to save UserBookMark!\n", user->GetUserLogName()));
	}
}

void Packet_FavoriteManager::SendPacket_Favorite_UserItemSet(TCPConnection::pointer connection, const ItemSet& itemSet) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Favorite });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_FavoriteType::UserItemSet);
	packet->WriteUInt16_LE(itemSet.characterItemID);
	packet->WriteUInt8(itemSet.activeTab % 3);
	packet->WriteUInt8(0); // padding
	packet->WriteUInt8(4); // 4 weapons per loadout

	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < 4; ++j) {
			packet->WriteUInt16_LE(itemSet.weapons[i][j]);
		}
	}

	packet->Send();
}

void Packet_FavoriteManager::parsePacket_Favorite_UserItemSet(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	unsigned char slotID = packet->ReadUInt8();
	unsigned short itemID = packet->ReadUInt16_LE();

	if (user->SaveUserItemSetSlot(slotID, itemID)) {
		serverConsole.Print(PrefixType::Info, format("[ Packet_FavoriteManager ] User ({}) has sent Packet_Favorite UserItemSet, saved slot {} -> itemID {} successfully!\n", user->GetUserLogName(), slotID, itemID));
	}
	else {
		serverConsole.Print(PrefixType::Error, format("[ Packet_FavoriteManager ] User ({}) has sent Packet_Favorite UserItemSet, failed to save slot {} -> itemID {}!\n", user->GetUserLogName(), slotID, itemID));
	}
}