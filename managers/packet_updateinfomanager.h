#pragma once
#include "user.h"

enum class Packet_UpdateInfoType {
	Unk0 = 0,
	Unk1 = 1
};

class Packet_UpdateInfoManager {
public:
	void ParsePacket_UpdateInfo(TCPConnection::Packet::pointer packet);
	void SendPacket_UpdateInfo(const GameUser& gameUser);
	void SendPacket_UserUpdateInfo_ChatCount(TCPConnection::pointer connection, unsigned long userID, unsigned char count = 255);
	void SendPacket_UserUpdateInfo_CBoxCount(TCPConnection::pointer connection, unsigned long userID, unsigned short count);

private:
	void parsePacket_UpdateInfo_Unk0(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_UpdateInfo_Unk1(User* user);
};

extern Packet_UpdateInfoManager packet_UpdateInfoManager;