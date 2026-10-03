#pragma once
#include "../networking/tcp_connection.h"

class Packet_MessengerManager {
public:
	void ParsePacket_Messenger(TCPConnection::Packet::pointer packet);
	void SendPacket_Messenger_ChatCount(TCPConnection::pointer connection, unsigned char count = 255);
};

extern Packet_MessengerManager packet_MessengerManager;
