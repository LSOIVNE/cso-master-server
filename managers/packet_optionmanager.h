#pragma once
#include "user.h"

constexpr auto OPTION_MAX_SIZE = 5120;

enum Packet_OptionType {
	UserOption = 0,
	Unk1 = 1,
	Unk2 = 2
};

class Packet_OptionManager {
public:
	void ParsePacket_Option(TCPConnection::Packet::pointer packet);
	void SendPacket_Option_UserOption(TCPConnection::pointer connection, const vector<unsigned char>& userOption);

private:
	void parsePacket_Option_UserOption(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Option_Unk1(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Option_Unk2(User* user, TCPConnection::Packet::pointer packet);
};

extern Packet_OptionManager packet_OptionManager;