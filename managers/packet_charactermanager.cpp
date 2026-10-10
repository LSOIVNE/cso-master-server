#include "packet_charactermanager.h"
#include "usermanager.h"
#include "packetmanager.h"
#include "serverconfig.h"
#include "serverconsole.h"
#include "databasemanager.h"

Packet_CharacterManager packet_CharacterManager;

Packet_ReplyType ValidateNickname(const string& nickName) {
	if (nickName.size() < NICKNAME_MIN_SIZE) {
		return Packet_ReplyType::ID_TOO_SHORT;
	}

	if (nickName.size() > NICKNAME_MAX_SIZE) {
		return Packet_ReplyType::ID_TOO_LONG;
	}

	// Character validation (ASCII alphanumeric + Big-5 2-byte characters)
	size_t i = 0;
	while (i < nickName.size()) {
		unsigned char c = (unsigned char)nickName[i];
		if (c >= 0x81 && c <= 0xFE) {
			if (i + 1 >= nickName.size()) {
				return Packet_ReplyType::INVALID_CHAR;
			}
			unsigned char c2 = (unsigned char)nickName[i + 1];
			if (!((c2 >= 0x40 && c2 <= 0x7E) || (c2 >= 0xA1 && c2 <= 0xFE))) {
				return Packet_ReplyType::INVALID_CHAR;
			}
			i += 2;
		}
		else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
			i += 1;
		}
		else {
			return Packet_ReplyType::INVALID_CHAR;
		}
	}

	// Digits cannot precede characters
	size_t first_number_pos = nickName.find_first_of("0123456789");
	size_t last_char_pos = nickName.find_last_not_of("0123456789");

	if (first_number_pos != string::npos && last_char_pos > first_number_pos) {
		return Packet_ReplyType::ID_DIGIT_BEFORE_CHAR;
	}

	// Repetition check: no character can appear > NICKNAME_MAX_CHAR_COUNT (3) times
	i = 0;
	while (i < nickName.size()) {
		unsigned char c = (unsigned char)nickName[i];
		if (c >= 0x81 && c <= 0xFE) {
			string ch = nickName.substr(i, 2);
			int cnt = 0;
			for (size_t j = 0; j + 1 < nickName.size(); ) {
				if ((unsigned char)nickName[j] >= 0x81 && (unsigned char)nickName[j] <= 0xFE) {
					if (nickName.substr(j, 2) == ch) {
						cnt++;
					}
					j += 2;
				}
				else {
					j += 1;
				}
			}
			if (cnt > NICKNAME_MAX_CHAR_COUNT) {
				return Packet_ReplyType::ID_EXCEED_CHAR_COUNT;
			}
			i += 2;
		}
		else {
			int cnt = 0;
			for (size_t j = 0; j < nickName.size(); ) {
				if ((unsigned char)nickName[j] >= 0x81 && (unsigned char)nickName[j] <= 0xFE) {
					j += 2;
				}
				else {
					if (nickName[j] == (char)c) {
						cnt++;
					}
					j += 1;
				}
			}
			if (cnt > NICKNAME_MAX_CHAR_COUNT) {
				return Packet_ReplyType::ID_EXCEED_CHAR_COUNT;
			}
			i += 1;
		}
	}

	// Prohibited names check
	string nickNameLower = nickName;
	transform(nickNameLower.begin(), nickNameLower.end(), nickNameLower.begin(),
		[](unsigned char c) { return tolower(c); });

	for (auto& name : serverConfig.prohibitedNames) {
		if (!name.empty() && nickNameLower.find(name) != string::npos) {
			return Packet_ReplyType::ID_PROHIBITED;
		}
	}

	return Packet_ReplyType::NoReply;
}

void Packet_CharacterManager::ParsePacket_RecvCharacter(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (user == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_CharacterManager ] Client ({}) has sent Packet_RecvCharacter, but it's not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	if (user->GetUserStatus() != UserStatus::InLogin) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_CharacterManager ] User ({}) has sent Packet_RecvCharacter, but it's not in character creation!\n", user->GetUserLogName()));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_CharacterManager ] Parsing Packet_RecvCharacter from user ({})\n", user->GetUserLogName()));

	const string& nickName = packet->ReadString();

	serverConsole.Print(PrefixType::Info, format("[ Packet_CharacterManager ] User ({}) has sent Packet_RecvCharacter - nickName: {}\n", user->GetUserLogName(), Cp950ToUtf8(nickName)));

	char userCharacterExistsResult = user->IsUserCharacterExists();
	if (userCharacterExistsResult < 0) {
		packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
		return;
	}
	else if (userCharacterExistsResult) {
		return;
	}

	Packet_ReplyType validateReply = ValidateNickname(nickName);
	if (validateReply != Packet_ReplyType::NoReply) {
		packetManager.SendPacket_Reply(connection, validateReply);
		return;
	}

	if (databaseManager.IsNickNameExists(nickName)) {
		packetManager.SendPacket_Reply(connection, Packet_ReplyType::ALREADY_EXIST);
		return;
	}

	char createUserBuymenusResult = user->CreateUserBuymenus();
	if (!createUserBuymenusResult) {
		if (createUserBuymenusResult < 0) {
			packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
			return;
		}

		packetManager.SendPacket_Reply(connection, Packet_ReplyType::ALREADY_EXIST);
		return;
	}

	char createUserBookmarksResult = user->CreateUserBookmarks();
	if (!createUserBookmarksResult) {
		if (createUserBookmarksResult < 0) {
			packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
			return;
		}

		packetManager.SendPacket_Reply(connection, Packet_ReplyType::ALREADY_EXIST);
		return;
	}

	char createUserInventoryResult = user->CreateUserInventory();
	if (!createUserInventoryResult) {
		if (createUserInventoryResult < 0) {
			packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
			return;
		}

		packetManager.SendPacket_Reply(connection, Packet_ReplyType::ALREADY_EXIST);
		return;
	}

	char createUserItemsetsResult = user->CreateUserItemsets();
	if (!createUserItemsetsResult) {
		if (createUserItemsetsResult < 0) {
			packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
			return;
		}

		packetManager.SendPacket_Reply(connection, Packet_ReplyType::ALREADY_EXIST);
		return;
	}

	char createUserCharacterResult = user->CreateUserCharacter(nickName);
	if (!createUserCharacterResult) {
		if (createUserCharacterResult < 0) {
			packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
			return;
		}

		packetManager.SendPacket_Reply(connection, Packet_ReplyType::ALREADY_EXIST);
		return;
	}

	if (!userManager.SendLoginPackets(user, Packet_ReplyType::CreateCharacterSuccess) && connection) {
		connection->DisconnectClient();
		return;
	}
}

void Packet_CharacterManager::SendPacket_Character(TCPConnection::pointer connection) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Character });
	if (packet == NULL) {
		return;
	}

	packet->Send();
}