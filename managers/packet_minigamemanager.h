#pragma once
#include "user.h"

class Packet_MiniGameManager {
public:
	void ParsePacket_MiniGame(TCPConnection::Packet::pointer packet);

private:
	void parsePacket_MiniGame_Bingo(User* user, TCPConnection::Packet::pointer packet);
};

extern Packet_MiniGameManager packet_MiniGameManager;
