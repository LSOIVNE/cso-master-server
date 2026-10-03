#include "packet_updateinfomanager.h"
#include "packetmanager.h"
#include "usermanager.h"
#include "serverconsole.h"

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

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UpdateInfo });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt32_LE(gameUser.user->GetUserID());
	packetManager.BuildUserCharacter(packet, gameUser.userCharacter);

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