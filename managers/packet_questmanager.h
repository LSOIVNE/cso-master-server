#pragma once

#include "tcp_connection.h"

class User;

struct QuestRewardItem {
	unsigned char type = 0; // 0=Item, 1=Points, 2=EXP, 3=Honor, 4=Cash
	unsigned short itemID = 0;
	unsigned char count = 0;
	unsigned char period = 0;
	unsigned int amount = 0;
};

class Packet_QuestManager {
public:
	void ParsePacket_Quest(TCPConnection::Packet::pointer packet);
	void SendPacket_Quest_InitList(User* user);
	void SendPacket_Quest_UpdateQuest(TCPConnection::pointer connection, unsigned int questID, unsigned char state);
	void SendPacket_Quest_UpdateCondition(TCPConnection::pointer connection, unsigned int questID, unsigned char condIndex, unsigned short target, unsigned short current);
	void SendPacket_Quest_ClaimRewardReply(TCPConnection::pointer connection, unsigned int questID, unsigned char status, const std::vector<QuestRewardItem>& rewards);

private:
	void parsePacket_Quest_ClaimReward(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Quest_TrackQuest(User* user, TCPConnection::Packet::pointer packet);
};

extern Packet_QuestManager packet_QuestManager;
