#pragma once
#include "user.h"
#include "gamematchuser.h"

class Room;

constexpr auto SAVEDATA_MAX_SIZE = 51200;

enum Packet_HostType {
	HostStart = 0,
	ServerIsActivated = 0,
	HostJoin = 1,
	SaveData = 1,
	HostRestart = 2,
	SendUserStatus = 2,
	HostEndSession = 3,
	PlayerKilled = 3,
	ResultClose = 4,
	EndGame = 5,
	RejoinFail = 6,
	AddKill = 7,
	AddDeath = 8,
	RoundResult = 9,
	SelectTeam = 10,
	GameEvent = 11,
	PlayerBuyItem = 14,
	PlayerPosition = 16,
	ItemConsumed = 100,
	UserWeapons = 101,
	FlyerFlock = 102
};

class Packet_HostManager {
public:
	void ParsePacket_Host(TCPConnection::Packet::pointer packet);
	void SendPacket_Host_HostStart(User* user);
	void SendPacket_Host_HostJoin(TCPConnection::pointer connection, unsigned long userID);
	void SendPacket_Host_HostRestart(TCPConnection::pointer connection, bool isRoomHost, unsigned long roomHostUserID, const vector<GameMatchUser*>& gameMatchUsers = {}, const vector<unsigned char>& saveData = {});
	void SendPacket_Host_ResultClose(TCPConnection::pointer connection);
	void SendPacket_Host_ItemConsumed(TCPConnection::pointer connection, unsigned long targetUserID, unsigned short itemID, unsigned char result);
	void SendPacket_Host_UserWeapons(TCPConnection::pointer connection, unsigned long targetUserID);
	void SendPacket_Host_FlyerFlock(TCPConnection::pointer connection, const string& name);
	void SendPacket_Host_HostEndSession(TCPConnection::pointer connection);
	void ProcessEndGame(Room* room, unsigned char winTeam, unsigned short terWins, unsigned char ctWins, unsigned long matchDuration);

private:
	void parsePacket_Host_ServerIsActivated(User* user);
	void parsePacket_Host_SaveData(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Host_SendUserStatus(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Host_PlayerKilled(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Host_AddKill(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Host_AddDeath(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Host_RoundResult(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Host_EndGame(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Host_RejoinFail(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Host_SelectTeam(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Host_ItemConsumed(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Host_UserWeapons(User* user, TCPConnection::Packet::pointer packet);
};

extern Packet_HostManager packet_HostManager;