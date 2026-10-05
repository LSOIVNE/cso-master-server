#include "packet_minigamemanager.h"
#include "bingomanager.h"
#include "usermanager.h"
#include "serverconsole.h"

Packet_MiniGameManager packet_MiniGameManager;

void Packet_MiniGameManager::ParsePacket_MiniGame(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (!userManager.IsUserLoggedIn(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_MiniGameManager ] Client ({}) sent Packet_MiniGame, but is not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	unsigned char gameType = packet->ReadUInt8();
	switch (gameType) {
		case 0: { // Bingo
			parsePacket_MiniGame_Bingo(user, packet);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_MiniGameManager ] User ({}) sent unknown MiniGame gameType {}!\n", user->GetUserLogName(), (int)gameType));
			break;
		}
	}
}

void Packet_MiniGameManager::parsePacket_MiniGame_Bingo(User* user, TCPConnection::Packet::pointer packet) {
	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned char action = packet->ReadUInt8();
	serverConsole.Print(PrefixType::Info, format("[ Packet_MiniGameManager ] User ({}) sent Bingo action {}\n", user->GetUserLogName(), (int)action));

	switch (action) {
		case 0: { // Request / Sync Board
			bingoManager.OnClientRequestSyncBoard(connection, user);
			break;
		}
		case 3: { // Reset Board
			bingoManager.ResetBoard(connection, user);
			break;
		}
		case 4: { // Shuffle Board
			bingoManager.ShuffleBoard(connection, user);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_MiniGameManager ] User ({}) sent unknown Bingo action {}!\n", user->GetUserLogName(), (int)action));
			break;
		}
	}
}
