#include "packet_titlemanager.h"
#include "titlemanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include <format>

Packet_TitleManager packet_TitleManager;

void Packet_TitleManager::ParsePacket_Title(TCPConnection::Packet::pointer packet) {
	if (packet == nullptr) {
		return;
	}

	auto connection = packet->GetConnection();
	if (connection == nullptr) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (user == nullptr) {
		return;
	}

	unsigned char type = packet->ReadUInt8();
	serverConsole.Print(PrefixType::Info, std::format("[ Packet_TitleManager ] User ({}) sent Packet_Title type {}\n",
		user->GetUserLogName(), (int)type));

	switch (type) {
		case 0: {
			parsePacket_Title_EquipTitle(user, packet);
			break;
		}
		case 1: {
			parsePacket_Title_EquipBadge(user, packet);
			break;
		}
		case 2: {
			parsePacket_Title_UnequipBadge(user, packet);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, std::format("[ Packet_TitleManager ] User ({}) sent unknown Title subpacket type {}\n",
				user->GetUserLogName(), (int)type));
			break;
		}
	}
}

void Packet_TitleManager::parsePacket_Title_EquipTitle(User* user, TCPConnection::Packet::pointer packet) {
	unsigned short prefixID = packet->ReadUInt16_LE();
	serverConsole.Print(PrefixType::Info, std::format("[ Packet_TitleManager ] User ({}) EquipTitle prefixID: {}\n",
		user->GetUserLogName(), prefixID));

	titleManager.EquipTitle(user, prefixID);
}

void Packet_TitleManager::parsePacket_Title_EquipBadge(User* user, TCPConnection::Packet::pointer packet) {
	unsigned char slot = packet->ReadUInt8();
	unsigned short titleID = packet->ReadUInt16_LE();

	serverConsole.Print(PrefixType::Info, std::format("[ Packet_TitleManager ] User ({}) EquipBadge slot: {}, titleID: {}\n",
		user->GetUserLogName(), (int)slot, titleID));

	titleManager.EquipBadge(user, slot, (unsigned char)titleID);
}

void Packet_TitleManager::parsePacket_Title_UnequipBadge(User* user, TCPConnection::Packet::pointer packet) {
	unsigned char slot = packet->ReadUInt8();

	serverConsole.Print(PrefixType::Info, std::format("[ Packet_TitleManager ] User ({}) UnequipBadge slot: {}\n",
		user->GetUserLogName(), (int)slot));

	titleManager.UnequipBadge(user, slot);
}

void Packet_TitleManager::SendPacket_Title_EquipResult(TCPConnection::pointer connection, unsigned short prefixID) {
	if (connection == nullptr) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Title });
	if (packet != nullptr) {
		packet->WriteUInt8(0); // type 0 = equip title confirmation (triggers client CSOFacade vtable[29])
		packet->WriteUInt16_LE(prefixID);
		packet->Send();
	}
}
