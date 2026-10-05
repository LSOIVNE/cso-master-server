#pragma once
#include "tcp_connection.h"

constexpr auto NICKNAME_MIN_SIZE = 4;
constexpr auto NICKNAME_MAX_SIZE = 16;
constexpr auto NICKNAME_MAX_CHAR_COUNT = 3;

Packet_ReplyType ValidateNickname(const string& nickName);

class Packet_CharacterManager {
public:
	void ParsePacket_RecvCharacter(TCPConnection::Packet::pointer packet);
	void SendPacket_Character(TCPConnection::pointer connection);
};

extern Packet_CharacterManager packet_CharacterManager;