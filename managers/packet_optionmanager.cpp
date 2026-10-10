#include "packet_optionmanager.h"
#include "usermanager.h"
#include "serverconsole.h"

Packet_OptionManager packet_OptionManager;

void Packet_OptionManager::ParsePacket_Option(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (!userManager.IsUserLoggedIn(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_OptionManager ] Client ({}) has sent Packet_Option, but it's not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_OptionManager ] Parsing Packet_Option from user ({})\n", user->GetUserLogName()));

	unsigned char type = packet->ReadUInt8();

	switch (type) {
		case Packet_OptionType::UserOption: {
			parsePacket_Option_UserOption(user, packet);
			break;
		}
		case Packet_OptionType::Unk1: {
			parsePacket_Option_Unk1(user, packet);
			break;
		}
		case Packet_OptionType::Unk2: {
			parsePacket_Option_Unk2(user, packet);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_OptionManager ] User ({}) has sent unregistered Packet_OptionManager type: {}!\n", user->GetUserLogName(), type));
			break;
		}
	}
}

void Packet_OptionManager::SendPacket_Option_UserOption(TCPConnection::pointer connection, const vector<unsigned char>& userOption) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Option });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_OptionType::UserOption);
	packet->WriteUInt16_LE((unsigned short)userOption.size());
	packet->WriteArray_UInt8(userOption);

	packet->Send();
}

void Packet_OptionManager::parsePacket_Option_UserOption(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	unsigned short size = packet->ReadUInt16_LE();

	if (size == 0 || size > OPTION_MAX_SIZE) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_OptionManager ] User ({}) has sent Packet_Option UserOption, but its size is invalid: {}!\n", user->GetUserLogName(), size));
		return;
	}

	const vector<unsigned char>& userOption = packet->ReadArray_UInt8(size);

	if (user->SaveUserOption(userOption)) {
		serverConsole.Print(PrefixType::Info, format("[ Packet_OptionManager ] User ({}) has sent Packet_Option UserOption, userOption saved successfully!\n", user->GetUserLogName()));
	}
	else {
		serverConsole.Print(PrefixType::Error, format("[ Packet_OptionManager ] User ({}) has sent Packet_Option UserOption, failed to save userOption!\n", user->GetUserLogName()));
	}
}

void Packet_OptionManager::parsePacket_Option_Unk1(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	unsigned long unk1 = packet->ReadUInt32_LE();

	serverConsole.Print(PrefixType::Info, format("[ Packet_OptionManager ] User ({}) has sent Packet_Option Unk1 - unk1: {}\n", user->GetUserLogName(), unk1));
}

void Packet_OptionManager::parsePacket_Option_Unk2(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	unsigned char unk2 = packet->ReadUInt8();

	serverConsole.Print(PrefixType::Info, format("[ Packet_OptionManager ] User ({}) has sent Packet_Option Unk2 - unk2: {}\n", user->GetUserLogName(), (int)unk2));
}