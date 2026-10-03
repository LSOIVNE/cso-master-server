#include "packet_messengermanager.h"
#include "usermanager.h"
#include "serverconsole.h"

Packet_MessengerManager packet_MessengerManager;

void Packet_MessengerManager::ParsePacket_Messenger(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (!userManager.IsUserLoggedIn(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_MessengerManager ] Client ({}) has sent Packet_Messenger, but it's not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	// Client sends this packet (ID 93) to notify the server that a lobby chat message was sent
	// (decrements remaining chat count for low-level players).
	// We simply acknowledge it - no action needed on server side.
	unsigned char subType = packet->ReadUInt8();

	serverConsole.Print(PrefixType::Info, format("[ Packet_MessengerManager ] User ({}) sent Packet_Messenger - subType: {}\n", user->GetUserLogName(), subType));
}

void Packet_MessengerManager::SendPacket_Messenger_ChatCount(TCPConnection::pointer connection, unsigned char count) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Messenger });
	if (packet == NULL) {
		return;
	}

	// Packet format (from hw.dll Packet_Messenger::OnPacket at sub_372D6E00):
	// Byte 0: subType (0 = set chat count)
	// Byte 1: remaining chat count value
	packet->WriteUInt8(0);     // subType = 0 (set remaining count)
	packet->WriteUInt8(count); // remaining lobby chat count (255 = effectively unlimited)

	packet->Send();
}
