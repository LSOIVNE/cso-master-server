#pragma once

#include "tcp_connection.h"
#include "questmanager.h"

class User;

class Packet_QuestManager {
public:
	void ParsePacket_Quest(TCPConnection::Packet::pointer packet);
	void SendPacket_Quest_InitList(User* user);
	void SendPacket_Quest_UpdateQuest(TCPConnection::pointer connection, unsigned int questID, unsigned char state, unsigned char isTracked = 0, bool updateTracked = false);
	void SendPacket_Quest_UpdateCondition(TCPConnection::pointer connection, unsigned int questID, unsigned char condIndex, unsigned short target, unsigned short current);
	void SendPacket_Quest_ClaimRewardReply(TCPConnection::pointer connection, unsigned int questID, unsigned char status, const std::vector<QuestRewardItem>& rewards);
	void SendPacket_Quest_UpdateReward(TCPConnection::pointer connection, unsigned int questID, unsigned char rewardIndex, unsigned int imageID, unsigned char isUnknown);
	void SendPacket_Quest_UpdateStats(TCPConnection::pointer connection, const struct UserQuestStats& stats);
	void SendPacket_Quest_UpdateStats(TCPConnection::pointer connection, unsigned int honorPoints, unsigned char resetCount);

private:
	void parsePacket_Quest_ClaimReward(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Quest_TrackQuest(User* user, TCPConnection::Packet::pointer packet);
};

extern Packet_QuestManager packet_QuestManager;
