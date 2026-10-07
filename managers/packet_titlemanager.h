#pragma once
#include "user.h"

class Packet_TitleManager {
public:
	void ParsePacket_Title(TCPConnection::Packet::pointer packet);
	void SendPacket_Title_EquipResult(TCPConnection::pointer connection, unsigned short prefixID);

private:
	void parsePacket_Title_EquipTitle(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Title_EquipBadge(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Title_UnequipBadge(User* user, TCPConnection::Packet::pointer packet);
};

extern Packet_TitleManager packet_TitleManager;
