#include "packet_roommanager.h"
#include "packet_umsgmanager.h"
#include "packet_hostmanager.h"
#include "packetmanager.h"
#include "usermanager.h"
#include "roommanager.h"
#include "serverconsole.h"
#include "serverconfig.h"
#include "packet_updateinfomanager.h"
#include "databasemanager.h"
#include "battlebonusmanager.h"

Packet_RoomManager packet_RoomManager;

static unsigned char calculateModeSubFlag(User* hostUser, unsigned char gameModeID, bool bigheadDisabled = false) {
	return CalculateRoomModeSubFlag(hostUser, gameModeID, bigheadDisabled);
}

void Packet_RoomManager::ParsePacket_Room(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (!userManager.IsUserLoggedIn(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] Client ({}) has sent Packet_Room, but it's not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] Parsing Packet_Room from user ({})\n", user->GetUserLogName()));

	unsigned char type = packet->ReadUInt8();

	switch (type) {
		case Packet_RoomType::CreateRoom: {
			parsePacket_Room_CreateRoom(user, packet);
			break;
		}
		case Packet_RoomType::JoinRoom: {
			parsePacket_Room_JoinRoom(user, packet);
			break;
		}
		case Packet_RoomType::LeaveRoom: {
			parsePacket_Room_LeaveRoom(user, packet);
			break;
		}
		case Packet_RoomType::UserReady: {
			parsePacket_Room_UserReady(user, packet);
			break;
		}
		case Packet_RoomType::StartGame: {
			parsePacket_Room_StartGame(user);
			break;
		}
		case Packet_RoomType::UpdateRoomSettings: {
			parsePacket_Room_UpdateRoomSettings(user, packet);
			break;
		}
		case Packet_RoomType::ResultConfirm: {
			parsePacket_Room_ResultConfirm(user, packet);
			break;
		}
		case Packet_RoomType::ChangeTeam_Old:
		case Packet_RoomType::ChangeTeam: {
			parsePacket_Room_ChangeTeam(user, packet);
			break;
		}
		case Packet_RoomType::ReturnToRoom: {
			parsePacket_Room_ReturnToRoom(user);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent unregistered Packet_Room type {}!\n", user->GetUserLogName(), type));
			break;
		}
	}
}

void Packet_RoomManager::SendPacket_RoomList_FullRoomList(TCPConnection::pointer connection, vector<Room*> rooms, unsigned short flag) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::RoomList });
	if (packet == NULL) {
		return;
	}

	rooms.erase(remove_if(rooms.begin(), rooms.end(), [](Room* room) {
		return (room == NULL);
	}), rooms.end());

	packet->WriteUInt8(Packet_RoomListType::FullRoomList);
	packet->WriteUInt16_LE((unsigned short)rooms.size());

	for (auto& room : rooms) {
		buildRoomInfo(packet, room, flag);
	}

	packet->Send();
}

void Packet_RoomManager::SendPacket_RoomList_AddRoom(TCPConnection::pointer connection, Room* room, unsigned short flag) {
	if (connection == NULL || room == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::RoomList });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_RoomListType::AddRoom);
	buildRoomInfo(packet, room, flag);

	packet->Send();
}

void Packet_RoomManager::SendPacket_RoomList_RemoveRoom(TCPConnection::pointer connection, unsigned short roomID) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::RoomList });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_RoomListType::RemoveRoom);
	packet->WriteUInt16_LE(roomID);

	packet->Send();
}

void Packet_RoomManager::SendPacket_RoomList_UpdateRoom(TCPConnection::pointer connection, Room* room, unsigned short flag) {
	if (connection == NULL || room == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::RoomList });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_RoomListType::UpdateRoom);
	buildRoomInfo(packet, room, flag);

	packet->Send();
}

void Packet_RoomManager::SendPacket_Room_UserLeave(TCPConnection::pointer connection, unsigned long userID) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Room });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_RoomType::UserLeave);
	packet->WriteUInt32_LE(userID);

	packet->Send();
}

void Packet_RoomManager::SendPacket_Room_ReplyUpdateRoomSettings(TCPConnection::pointer connection, const RoomSettings& roomSettings) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Room });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_RoomType::ReplyUpdateRoomSettings);
	buildRoomSettings(packet, roomSettings);

	packet->Send();
}

void Packet_RoomManager::SendPacket_Room_GameResult(TCPConnection::pointer connection, Room* room, GameMatch* gameMatch, unsigned char winTeam, unsigned short terWins, unsigned char ctWins) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Room });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_RoomType::GameResult);
	packet->WriteBool(false); // false - normal match, true - clan battle

	unsigned char gameModeID = room ? room->GetRoomSettings().gameModeID : 0;

	// Extract players
	vector<GameMatchUser*> players;
	if (gameMatch) {
		players = gameMatch->GetGameMatchUsers();
		players.erase(remove_if(players.begin(), players.end(), [](GameMatchUser* gmu) {
			return (gmu == NULL || gmu->GetUser() == NULL);
		}), players.end());
	}

	// Fallback if players list is empty: ensure at least local user is included
	User* localUser = userManager.GetUserByConnection(connection);
	unique_ptr<GameMatchUser> fallbackUser;
	if (players.empty() && localUser) {
		fallbackUser = make_unique<GameMatchUser>(localUser);
		RoomUser* ru = room ? room->GetRoomUser(localUser) : NULL;
		if (ru) {
			fallbackUser->SetTeam((Team)ru->team);
		}
		players.push_back(fallbackUser.get());
	}

	unsigned char playerCount = (unsigned char)players.size();

	unsigned char outWinTeam = winTeam;
	if (IsScenarioMode(gameModeID)) {
		outWinTeam = 0;
	}

	packet->WriteUInt8(outWinTeam);             // 1 - tr, 2 - ct, 0 - draw/none (0 for Scenario)
	packet->WriteUInt8((unsigned char)terWins); // TER score (1 byte in header)
	packet->WriteUInt8(ctWins);                 // CT score
	packet->WriteUInt8(playerCount);            // num of players
	packet->WriteUInt8(gameModeID);             // gameModeID

	for (unsigned char i = 0; i < playerCount; ++i) {
		GameMatchUser* gmu = players[i];
		User* u = gmu->GetUser();

		unsigned long userID = u->GetUserID();
		unsigned char team = (unsigned char)gmu->GetTeam();
		if (team != 1 && team != 2) {
			RoomUser* ru = room ? room->GetRoomUser(u) : NULL;
			team = ru ? ru->team : 1;
		}

		unsigned short kills = gmu->GetKills();
		unsigned short deaths = gmu->GetDeaths();
		unsigned char rank = gmu->GetRank();
		unsigned char isWin = 0;
		unsigned char isLoss = 0;
		if (gameModeID == 1 || gameModeID == 22) {
			isWin = (rank == 1) ? 1 : 0;
			isLoss = (rank != 1) ? 1 : 0;
		}
		else if (IsScenarioMode(gameModeID)) {
			bool isClear = (terWins > ctWins);
			isWin = isClear ? 1 : 0;
			isLoss = isClear ? 0 : 1;
		}
		else {
			isWin = (winTeam != 0 && team == winTeam) ? 1 : 0;
			isLoss = (winTeam != 0 && team != winTeam) ? 1 : 0;
		}

		unsigned short earnedExp = gmu->GetExp();
		unsigned short bonusExp = gmu->GetBonusExp();
		unsigned short earnedPoints = gmu->GetPoints();
		unsigned short bonusPoints = gmu->GetBonusPoints();
		unsigned char levelUp = gmu->GetLevelUp();

		// 54 bytes common player block
		packet->WriteUInt32_LE(userID);
		packet->WriteUInt8(team);
		packet->WriteUInt16_LE(kills);
		packet->WriteUInt16_LE(deaths);
		packet->WriteUInt8(isWin);
		packet->WriteUInt8(isLoss);
		packet->WriteUInt16_LE(earnedExp);
		packet->WriteUInt16_LE(bonusExp);
		packet->WriteUInt16_LE(earnedPoints);
		packet->WriteUInt16_LE(bonusPoints);
		packet->WriteUInt8(rank);
		packet->WriteUInt8(levelUp);

		const auto& bonusPcts = gmu->GetBonusPercentages();
		for (int b = 0; b < 14; ++b) {
			packet->WriteUInt16_LE(bonusPcts[b]); // 14 x UInt16_LE bonus percentages
		}

		unsigned char teamBonus = 0;
		if (winTeam == 1 && team == 1) {
			teamBonus = 1;
		}
		else if (winTeam == 2 && team == 2) {
			teamBonus = 2;
		}

		packet->WriteUInt8(teamBonus);                              // teamBonus (0=none, 1=TR, 2=CT)
		packet->WriteUInt8((unsigned char)gmu->GetHeadshots());     // headshots ("CSO_headshot")
		packet->WriteUInt8((unsigned char)gmu->GetGrenadeKills());  // grenadeKills ("CSO_FragKill")
		packet->WriteUInt8((unsigned char)gmu->GetKnifeKills());    // knifeKills ("CSO_CloseCombatKill")
		packet->WriteUInt16_LE(0);                                  // clanBonusExp

		// Mode-specific extra bytes
		switch (gameModeID) {
			case 0: // Original
			case 3: // Bot
			case 6: // Official
			case 7: // Official_TieBreak
				packet->WriteUInt8((unsigned char)gmu->GetBombDeactivate()); // "CSO_BomeBreak"
				packet->WriteUInt8((unsigned char)gmu->GetBombSuccess());    // "CSO_TargetDestroy"
				packet->WriteUInt8((unsigned char)gmu->GetHostageRescue());   // "CSO_RescueHostage"
				packet->WriteUInt8((unsigned char)gmu->GetVipEscape());       // "CSO_VIPEscape"
				packet->WriteUInt8((unsigned char)gmu->GetVipKill());         // "CSO_VIPKill"
				break;
			case 8:  // Zombie 1
			case 9:  // Zombie 2
			case 14: // Zombie Hero (Zombie 3)
			case 20: // Zombie Darkness
			case 24: // Zombie Giant
				packet->WriteUInt8((unsigned char)gmu->GetSurviveCount());       // "CSO_SurviveCount"
				packet->WriteUInt8((unsigned char)gmu->GetZombieKill());         // "CSO_ZombieKill"
				packet->WriteUInt8((unsigned char)gmu->GetZombieMotherKill());   // "CSO_MotherZombieKill"
				packet->WriteUInt8((unsigned char)gmu->GetHumanKill());          // "CSO_HumanKill"
				break;
			case 10: // Phantom / Hidden Mode (魔幻空間 / 幽靈模式)
				packet->WriteUInt16_LE(gmu->GetHdHiddenKill()); // Hd_HiddenKill ("CSO_Hd_HiddenKill")
				packet->WriteUInt16_LE(gmu->GetHdHumanKill());  // Hd_HumanKill ("CSO_Hd_HumanKill")
				break;
			case 11: // Zombie Unite (殭屍大亂鬥)
			case 13: // Zombie Union (殭屍聯盟)
				packet->WriteUInt32_LE(0); // UniteHealValue
				packet->WriteUInt16_LE(0); // UniteHealPoint
				packet->WriteUInt16_LE(0); // UniteItemCount
				break;
			case 12: // Challenge Mode (挑戰模式)
				packet->WriteFloat_LE(0.0f); // ChallBestRecord
				packet->WriteUInt16_LE(0);    // ChallPassCount
				break;
			case 15: // Zombie Scenario (災厄之章)
			case 17: // Human Scenario (戰神之章)
				packet->WriteUInt32_LE((unsigned int)gmu->GetKills());                      // ZBS_KillCnt ("CSO_ZBS_KillCnt")
				packet->WriteFloat_LE(gameMatch ? gameMatch->GetMatchDuration() : 0.0f);   // ZBS_ClearTime (float seconds)
				packet->WriteUInt16_LE(terWins);                                            // ZBS_RoundCount ("CSO_ZBS_RoundCount")
				break;
			case 16: // Soccer Mode (足球模式)
				packet->WriteUInt16_LE(0); // Sc_GoalCount
				packet->WriteUInt16_LE(0); // Sc_AssistCount
				packet->WriteUInt16_LE(0); // Sc_DefenceCount
				packet->WriteUInt16_LE(0); // Sc_MVPCount
				packet->WriteUInt32_LE(0); // Sc_AccumGoalCount
				packet->WriteUInt32_LE(0); // Sc_AccumAssistCount
				packet->WriteUInt32_LE(0); // Sc_AccumDefenceCount
				packet->WriteUInt32_LE(0); // Sc_AccumMVPCount
				break;
			case 18: // PVPVE
				packet->WriteFloat_LE(0.0f); // Pvpve_ClearTime
				packet->WriteUInt16_LE(0);    // Pvpve_HumanKill
				packet->WriteUInt16_LE(0);    // Pvpve_BotKill
				packet->WriteUInt16_LE(0);    // Pvpve_ConquestCount
				break;
			case 21: // Bazooka (轟炸擂台)
				packet->WriteUInt16_LE(gmu->GetBkAssist()); // bk_Assist ("CSO_bk_Assist")
				packet->WriteUInt8(gmu->GetBkMaxCombo());    // bk_MaxCombo ("CSO_bk_MaxCombo")
				break;
			case 23: // Casual / Light Original (悠遊模式)
				packet->WriteUInt8((unsigned char)gmu->GetBombDeactivate()); // BombDeactivate ("CSO_BomeBreak")
				packet->WriteUInt8((unsigned char)gmu->GetBombSuccess());    // BombSuccess ("CSO_TargetDestroy")
				packet->WriteUInt8((unsigned char)gmu->GetHostageRescue());   // HostageRescue ("CSO_RescueHostage")
				packet->WriteUInt8((unsigned char)gmu->GetVipEscape());       // VipEscape ("CSO_VIPEscape")
				packet->WriteUInt8((unsigned char)gmu->GetVipKill());         // VipKill ("CSO_VIPKill")
				packet->WriteUInt16_LE(gmu->GetLoAssist());                  // lo_Assist ("CSO_lo_Assist")
				break;
			case 25: // Battle Run / Rescue (即刻救援)
				packet->WriteUInt16_LE(gmu->GetLoAssist()); // lo_Assist ("CSO_lo_Assist")
				packet->WriteUInt32_LE(gmu->GetBrPoint());  // BR_Point ("CSO_BR_Point")
				break;
			case 26: // Shelter Survival (夜襲生存戰)
				packet->WriteUInt16_LE(gmu->GetZshPoint());     // ZSHPoint ("CSO_ZSHPoint")
				packet->WriteUInt8(gmu->GetRescuee());          // Rescuee ("CSO_Rescuee")
				packet->WriteUInt8(gmu->GetSurviveDays());      // SurviveDays ("CSO_SurviveDays")
				packet->WriteUInt8(gmu->GetMaxSurviveDays());   // MaximumSurviveDays ("CSO_MaximumSurviveDays")
				packet->WriteUInt8(gmu->GetAccumSurviveDays()); // AccumulateSurviveDays ("CSO_AccumulateSurviveDays")
				packet->WriteUInt8(gmu->GetAccumRescuee());     // AccumulateRescuee ("CSO_AccumulateRescuee")
				break;
			default:
				break;
		}
	}

	packet->Send();
	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] Sent Packet_Room GameResult - winTeam: {}, terWins: {}, ctWins: {}, players: {}, gameModeID: {}\n",
		winTeam, terWins, ctWins, playerCount, gameModeID));
}

void Packet_RoomManager::parsePacket_Room_CreateRoom(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	const UserNetwork& userNetwork = user->GetUserNetwork();
	if (userNetwork.networkSet != 0x3) {
		packet_UMsgManager.SendPacket_UMsg_ServerMessage(connection, Packet_UMsgType::WarningMessage, "ROOM_OPEN_FAILED_INVALID_GAME_IP");
		return;
	}

	if (user->GetUserStatus() == UserStatus::InRoom) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room CreateRoom, but it's already in a room!\n", user->GetUserLogName()));
		return;
	}

	unsigned short roomID = roomManager.GetFreeRoomID();
	if (!roomID) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room CreateRoom, but there's no available roomID!\n", user->GetUserLogName()));
		return;
	}

	Room* newRoom = new Room(roomID, user);
	if (newRoom == NULL) {
		packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
		return;
	}

	const UserCharacterResult& userCharacterResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (!userCharacterResult.result) {
		packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);

		delete newRoom;
		newRoom = NULL;
		return;
	}

	RoomSettings roomSettings = newRoom->GetRoomSettings();

	roomSettings.roomName = packet->ReadString();
	unsigned char unk2 = packet->ReadUInt8();
	roomSettings.maxPlayers = packet->ReadUInt8();
	roomSettings.gameModeID = packet->ReadUInt8();
	roomSettings.mapID = packet->ReadUInt8();
	roomSettings.winLimit = packet->ReadUInt8();
	roomSettings.killLimit = packet->ReadUInt16_LE();
	roomSettings.timeLimit = packet->ReadUInt8();
	roomSettings.roundTime = packet->ReadUInt8();
	roomSettings.password = packet->ReadString();
	unsigned char unk11 = packet->ReadUInt8();
	unsigned char unk12 = packet->ReadUInt8();
	bool fastStart = packet->ReadBool();
	unsigned char unk14 = packet->ReadUInt8();
	unsigned char unk15 = packet->ReadUInt8();
	unsigned char unk16 = packet->ReadUInt32_LE();

	roomSettings.hostagePenalty = 3;

	switch (roomSettings.gameModeID) {
		case GameMode::Original:
		case GameMode::Bot:
		case GameMode::Official:
		case GameMode::Official_TieBreak: {
			roomSettings.freezeTime = 6;
			break;
		}
	}

	roomSettings.buyTime = 90;
	roomSettings.nickNameDisplay = 1;

	if (roomSettings.gameModeID == GameMode::DeathMatch || roomSettings.gameModeID == GameMode::BotDM) {
		roomSettings.friendlyFire = true;
	}

	roomSettings.flashLight = true;
	roomSettings.footSteps = true;
	roomSettings.voiceChat = true;

	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room CreateRoom - roomName: {}, unk2: {}, maxPlayers: {}, gameModeID: {}, mapID: {}, winLimit: {}, killLimit: {}, timeLimit: {}, roundTime: {}, password: {}, unk11: {}, unk12: {}, fastStart: {}, unk14: {}, unk15: {}, unk16: {}\n", user->GetUserLogName(), Cp950ToUtf8(roomSettings.roomName), unk2, roomSettings.maxPlayers, roomSettings.gameModeID, roomSettings.mapID, roomSettings.winLimit, roomSettings.killLimit, roomSettings.timeLimit, roomSettings.roundTime, Cp950ToUtf8(roomSettings.password), unk11, unk12, fastStart, unk14, unk15, unk16));

	unsigned char modeSubFlag = calculateModeSubFlag(user, roomSettings.gameModeID, false);
	roomSettings.highFlag |= ROOMSETTINGS_HFLAG_UNK1000;
	roomSettings.unkh1000 = modeSubFlag;

	newRoom->SetRoomSettings(roomSettings);

	newRoom->AddRoomUser(user);
	userManager.SendRemoveUserPacketToAll(user);

	roomManager.AddRoom(newRoom);
	roomManager.SendAddRoomPacketToAll(newRoom, ROOMLIST_FLAG_ALL);

	sendPacket_Room_ReplyCreateAndJoin(connection, newRoom, { { user, userCharacterResult.userCharacter } });

	if (fastStart) {
		packet_HostManager.SendPacket_Host_HostStart(user);
	}
}

void Packet_RoomManager::parsePacket_Room_JoinRoom(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	const UserNetwork& userNetwork = user->GetUserNetwork();
	if (userNetwork.networkSet != 0x3) {
		packet_UMsgManager.SendPacket_UMsg_ServerMessage(connection, Packet_UMsgType::WarningMessage, "ROOM_JOIN_FAILED_INVALID_GAME_IP");
		return;
	}

	if (user->GetUserStatus() == UserStatus::InRoom) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room JoinRoom, but it's already in a room!\n", user->GetUserLogName()));
		return;
	}

	unsigned short roomID = packet->ReadUInt16_LE();
	const string& password = packet->ReadString();

	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room JoinRoom - roomID: {}, password: {}\n", user->GetUserLogName(), roomID, password));

	Room* room = roomManager.GetRoomByRoomID(roomID);
	if (room == NULL) {
		packet_UMsgManager.SendPacket_UMsg_ServerMessage(connection, Packet_UMsgType::WarningMessage, "ROOM_JOIN_FAILED_CLOSED");
		return;
	}

	const RoomSettings& roomSettings = room->GetRoomSettings();
	if (!roomSettings.password.empty() && password != roomSettings.password) {
		packet_UMsgManager.SendPacket_UMsg_ServerMessage(connection, Packet_UMsgType::WarningMessage, "ROOM_JOIN_FAILED_INVALID_PASSWD");
		return;
	}

	if (room->GetRoomUsers().size() >= roomSettings.maxPlayers) {
		packet_UMsgManager.SendPacket_UMsg_ServerMessage(connection, Packet_UMsgType::WarningMessage, "ROOM_JOIN_FAILED_FULL");
		return;
	}

	const UserCharacterResult& userCharacterResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (!userCharacterResult.result) {
		packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
		return;
	}

	if (!room->AddRoomUser(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room JoinRoom, but it's already in the room!\n", user->GetUserLogName()));
		return;
	}

	userManager.SendRemoveUserPacketToAll(user);
	roomManager.SendUpdateRoomPacketToAll(room, ROOMLIST_FLAG_CURRENTPLAYERS);

	vector<User*> roomUsers = room->GetRoomUsers();

	roomUsers.erase(remove_if(roomUsers.begin(), roomUsers.end(), [user](User* u) {
		return (u == NULL || u == user || u->GetConnection() == NULL);
	}), roomUsers.end());

	vector<GameUser> gameUsers;
	UserCharacterResult uCharacterResult;
	unsigned long userID = user->GetUserID();
	RoomUser* ru = room->GetRoomUser(user);
	unsigned char joinTeam = ru ? ru->team : RoomTeam::Terrorist;
	unsigned char joinStatus = ru ? ru->status : 0;
	unsigned char joinSlot = ru ? ru->slot : 0;

	for (auto& u : roomUsers) {
		uCharacterResult = u->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		if (uCharacterResult.result) {
			gameUsers.push_back({ u, uCharacterResult.userCharacter });
		}

		sendPacket_Room_UserJoin(u->GetConnection(), { user, userCharacterResult.userCharacter }, joinTeam, joinStatus, joinSlot);
		sendPacket_Room_UserReady(u->GetConnection(), userID, joinStatus);
	}

	gameUsers.push_back({ user, userCharacterResult.userCharacter });
	sendPacket_Room_ReplyCreateAndJoin(connection, room, gameUsers);
	sendPacket_Room_UserReady(connection, userID, joinStatus);
}

void Packet_RoomManager::parsePacket_Room_LeaveRoom(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InRoom && user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room LeaveRoom, but it's not in a room or in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room LeaveRoom, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	unsigned char leaveOption = packet->ReadUInt8();

	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room LeaveRoom - leaveOption: {}\n", user->GetUserLogName(), leaveOption));

	string warningKey = "";

	if (user->GetUserStatus() == UserStatus::InGame) {
		GameMatch* gameMatch = room->GetGameMatch();
		GameMatchUser* gameMatchUser = gameMatch ? gameMatch->GetGameMatchUserByUser(user) : nullptr;
		unsigned char gameModeID = room->GetRoomSettings().gameModeID;
		bool isBotMode = ((gameModeID >= 3 && gameModeID <= 5) || gameModeID == 24);
		bool isCompetitiveMode = (gameModeID == 0 || gameModeID == 1 || gameModeID == 2 || 
		                          gameModeID == 6 || gameModeID == 7 || 
		                          gameModeID == 22 || gameModeID == 23);

		if (gameMatch && gameMatchUser && !isBotMode && leaveOption != 0) {
			unsigned long userID = user->GetUserID();
			unsigned short kills = gameMatchUser->GetKills();

			if (leaveOption == 1) { // 離開進行中的遊戲，無道具
				databaseManager.RecordUserLeavePenalty(userID, true, 50, isCompetitiveMode);
				warningKey = "ROOM_LEAVE_PENALTY";
			}
			else if (leaveOption == 2) { // 使用道具 106 離場優惠券-經驗值,Point保留
				unsigned short slotID = 0;
				bool deleted = false;
				if (databaseManager.ConsumeUserInventoryItem(userID, 106, 1, slotID, deleted)) {
					if (deleted) {
						packetManager.SendPacket_InventoryDelta(connection, {}, { slotID });
					}
					else {
						const auto inv = databaseManager.GetUserInventory(userID);
						for (const auto& it : inv) {
							if (it.slotID == slotID) {
								packetManager.SendPacket_InventoryDelta(connection, { it }, {});
								break;
							}
						}
					}

					unsigned short earnedExp = 100 + kills * 10;
					unsigned short earnedPoints = 200 + kills * 15;
					BattleBonusResult bonusResult = battleBonusManager.CalculateBonus(user, gameMatchUser, gameModeID, earnedExp, earnedPoints);
					unsigned short totalEarnedExp = earnedExp + bonusResult.bonusExp;
					unsigned short totalEarnedPoints = earnedPoints + bonusResult.bonusPoints;

					databaseManager.SaveUserLeaveExpAndPoints(userID, totalEarnedExp, totalEarnedPoints, isCompetitiveMode);
					warningKey = "ROOM_LEAVE_NOPENALTY_ITEM_OK";
				}
				else {
					databaseManager.RecordUserLeavePenalty(userID, true, 50, isCompetitiveMode);
					warningKey = "ROOM_LEAVE_NOPENALTY_ITEM_FAIL";
				}
			}
			else if (leaveOption == 3) { // 使用道具 105 離場優惠券-勝敗不計
				unsigned short slotID = 0;
				bool deleted = false;
				if (databaseManager.ConsumeUserInventoryItem(userID, 105, 1, slotID, deleted)) {
					if (deleted) {
						packetManager.SendPacket_InventoryDelta(connection, {}, { slotID });
					}
					else {
						const auto inv = databaseManager.GetUserInventory(userID);
						for (const auto& it : inv) {
							if (it.slotID == slotID) {
								packetManager.SendPacket_InventoryDelta(connection, { it }, {});
								break;
							}
						}
					}

					databaseManager.RecordUserLeavePenalty(userID, true, 50, false);
					warningKey = "ROOM_LEAVE_NODEFEAT_ITEM_OK";
				}
				else {
					databaseManager.RecordUserLeavePenalty(userID, true, 50, isCompetitiveMode);
					warningKey = "ROOM_LEAVE_NODEFEAT_ITEM_FAIL";
				}
			}
			else if (leaveOption == 4) { // 使用道具 107 離場全優惠券
				unsigned short slotID = 0;
				bool deleted = false;
				if (databaseManager.ConsumeUserInventoryItem(userID, 107, 1, slotID, deleted)) {
					if (deleted) {
						packetManager.SendPacket_InventoryDelta(connection, {}, { slotID });
					}
					else {
						const auto inv = databaseManager.GetUserInventory(userID);
						for (const auto& it : inv) {
							if (it.slotID == slotID) {
								packetManager.SendPacket_InventoryDelta(connection, { it }, {});
								break;
							}
						}
					}

					unsigned short earnedExp = 100 + kills * 10;
					unsigned short earnedPoints = 200 + kills * 15;
					BattleBonusResult bonusResult = battleBonusManager.CalculateBonus(user, gameMatchUser, gameModeID, earnedExp, earnedPoints);
					unsigned short totalEarnedExp = earnedExp + bonusResult.bonusExp;
					unsigned short totalEarnedPoints = earnedPoints + bonusResult.bonusPoints;

					databaseManager.SaveUserLeaveExpAndPoints(userID, totalEarnedExp, totalEarnedPoints, false);
					warningKey = "ROOM_LEAVE_NOPENALTYDEFEAT_ITEM_OK";
				}
				else {
					databaseManager.RecordUserLeavePenalty(userID, true, 50, isCompetitiveMode);
					warningKey = "ROOM_LEAVE_NOPENALTYDEFEAT_ITEM_FAIL";
				}
			}
		}
	}

	room->RemoveRoomUser(user);
	userManager.SendAddUserPacketToAll(user);

	sendPacket_Room_LeaveRoomInGame(connection);

	userManager.SendFullUserListPacket(connection);
	roomManager.SendFullRoomListPacket(connection);

	const UserCharacterResult& ucr = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (ucr.result) {
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, ucr.userCharacter });
	}

	if (!warningKey.empty()) {
		packet_UMsgManager.SendPacket_UMsg_ServerMessage(connection, Packet_UMsgType::WarningMessage, warningKey);
	}

	userManager.CheckAndProcessUserExpiredItems(user, false);
}

void Packet_RoomManager::parsePacket_Room_StartGame(User* user) {
	if (user == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InRoom) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room StartGame, but it's not in a room!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room StartGame, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	User* roomHostUser = room->GetRoomHostUser();
	if (roomHostUser == NULL) {
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room StartGame\n", user->GetUserLogName()));

	if (roomHostUser == user) {
		RoomSettings roomSettings = room->GetRoomSettings();
		roomSettings.roomState = RoomState::Playing;
		room->SetRoomSettings(roomSettings);
		packet_HostManager.SendPacket_Host_HostStart(user);
	}
	else {
		packet_HostManager.SendPacket_Host_HostJoin(connection, roomHostUser->GetUserID());
	}
}

void Packet_RoomManager::parsePacket_Room_UpdateRoomSettings(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InRoom) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room UpdateRoomSettings, but it's not in a room!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room UpdateRoomSettings, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	if (room->GetRoomHostUser() != user) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room UpdateRoomSettings, but it's not the room host!\n", user->GetUserLogName()));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room UpdateRoomSettings\n", user->GetUserLogName()));

	RoomSettings roomSettings;
	roomSettings.lowFlag = packet->ReadUInt32_LE();
	roomSettings.highFlag = packet->ReadUInt32_LE();

	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] UpdateRoomSettings - lowFlag: 0x{:X}, highFlag: 0x{:X}\n", roomSettings.lowFlag, roomSettings.highFlag));

	unsigned char roomListFlag = 0;

	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_ROOMNAME) {
		roomSettings.roomName = packet->ReadString();
		roomListFlag |= ROOMLIST_FLAG_ROOMNAME;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK2) {
		roomSettings.unk2 = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK4) {
		roomSettings.unk4_1 = packet->ReadUInt8();
		roomSettings.unk4_2 = packet->ReadUInt8();
		roomSettings.unk4_3 = packet->ReadUInt8();
		roomSettings.unk4_4 = packet->ReadUInt32_LE();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_PASSWORD) {
		roomSettings.password = packet->ReadString();
		roomListFlag |= ROOMLIST_FLAG_ISPASSWORDED;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_LEVELLIMIT) {
		roomSettings.levelLimit = packet->ReadUInt8();
		roomListFlag |= ROOMLIST_FLAG_LEVELLIMIT;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK20) {
		roomSettings.unk20 = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_GAMEMODEID) {
		roomSettings.gameModeID = packet->ReadUInt8();
		roomListFlag |= ROOMLIST_FLAG_GAMEMODEID;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_MAPID) {
		roomSettings.mapID = packet->ReadUInt8();
		roomListFlag |= ROOMLIST_FLAG_MAPID;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_MAXPLAYERS) {
		roomSettings.maxPlayers = packet->ReadUInt8();
		roomListFlag |= ROOMLIST_FLAG_MAXPLAYERS;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_WINLIMIT) {
		roomSettings.winLimit = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_KILLLIMIT) {
		roomSettings.killLimit = packet->ReadUInt16_LE();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_TIMELIMIT) {
		roomSettings.timeLimit = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_ROUNDTIME) {
		roomSettings.roundTime = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_WEAPONLIMIT) {
		roomSettings.weaponLimit = packet->ReadUInt8();
		roomListFlag |= ROOMLIST_FLAG_WEAPONLIMIT;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_HOSTAGEPENALTY) {
		roomSettings.hostagePenalty = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FREEZETIME) {
		roomSettings.freezeTime = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_BUYTIME) {
		roomSettings.buyTime = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_NICKNAMEDISPLAY) {
		roomSettings.nickNameDisplay = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK40000) {
		roomSettings.unk40000 = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK80000) {
		roomSettings.unk80000 = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FRIENDLYFIRE) {
		roomSettings.friendlyFire = packet->ReadBool();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FLASHLIGHT) {
		roomSettings.flashLight = packet->ReadBool();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FOOTSTEPS) {
		roomSettings.footSteps = packet->ReadBool();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK800000) {
		roomSettings.unk800000 = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK1000000) {
		roomSettings.unk1000000 = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK2000000) {
		roomSettings.unk2000000 = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK4000000) {
		roomSettings.unk4000000 = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK8000000) {
		roomSettings.unk8000000 = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_DEATHCAMERATYPE) {
		roomSettings.deathCameraType = packet->ReadUInt8();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_VOICECHAT) {
		roomSettings.voiceChat = packet->ReadBool();
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_ROOMSTATE) {
		roomSettings.roomState = (RoomState)packet->ReadUInt8();
		roomListFlag |= ROOMLIST_FLAG_ROOMSTATE;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK80000000) {
		roomSettings.unk80000000_1 = packet->ReadUInt8();
		for (unsigned char i = 0; i < 2; i++) {
			roomSettings.unk80000000_2[i].unk8000000_vec_1 = packet->ReadUInt32_LE();
			roomSettings.unk80000000_2[i].unk8000000_vec_2 = packet->ReadUInt32_LE();
			roomSettings.unk80000000_2[i].unk8000000_vec_3 = packet->ReadUInt8();
			roomSettings.unk80000000_2[i].unk8000000_vec_4 = packet->ReadUInt8();
			roomSettings.unk80000000_2[i].unk8000000_vec_5 = packet->ReadUInt8();
			roomSettings.unk80000000_2[i].unk8000000_vec_6 = packet->ReadUInt8();
			roomSettings.unk80000000_2[i].unk8000000_vec_7 = packet->ReadUInt16_LE();
			roomSettings.unk80000000_2[i].unk8000000_vec_8 = packet->ReadUInt8();
			roomSettings.unk80000000_2[i].unk8000000_vec_9 = packet->ReadUInt8();
		}
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK1) {
		roomSettings.unkh1_1 = packet->ReadUInt32_LE();
		roomSettings.unkh1_2 = packet->ReadString();
		roomSettings.unkh1_3 = packet->ReadUInt8();
		roomSettings.unkh1_4 = packet->ReadUInt8();
		roomSettings.unkh1_5 = packet->ReadUInt8();
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK2) {
		roomSettings.unkh2 = packet->ReadUInt8();
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK4) {
		roomSettings.unkh4_1 = packet->ReadUInt8();
		roomSettings.unkh4_2 = packet->ReadUInt8();
		roomSettings.unkh4_3 = packet->ReadUInt8();
		roomSettings.unkh4_4 = packet->ReadUInt8();
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK8) {
		roomSettings.unkh8 = packet->ReadUInt8();
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK10) {
		roomSettings.unkh10 = packet->ReadUInt16_LE();
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK20) {
		roomSettings.unk20 = packet->ReadUInt8();
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK40) {
		roomSettings.unkh40 = packet->ReadUInt8();
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK80) {
		roomSettings.unkh80 = packet->ReadUInt8();
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK100) {
		roomSettings.unkh100 = packet->ReadUInt8();
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK200) {
		unsigned char count = packet->ReadUInt8();
		for (unsigned char i = 0; i < count; i++) {
			unsigned char a = packet->ReadUInt8();
			unsigned char b = packet->ReadUInt8();
			roomSettings.unkh200.push_back({ a, b });
		}
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK400) {
		roomSettings.unkh400 = packet->ReadUInt8();
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK800) {
		roomSettings.unkh800 = packet->ReadUInt8();
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK1000) {
		roomSettings.unkh1000 = packet->ReadUInt8();
	}

	unsigned char currentMode = (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_GAMEMODEID) ? roomSettings.gameModeID : room->GetRoomSettings().gameModeID;
	if (currentMode != 15 && currentMode != 17) {
		room->SetBigHeadDisabled(false);
	}
	unsigned char modeSubFlag = calculateModeSubFlag(user, currentMode, room->IsBigHeadDisabled());

	// BigHead (item 439): keep the flag in sync for BigHead-compatible modes.
	//
	// Official SGP server (sub_591050, case `a3 & 0x1000`) computes
	//   unkh1000 = (host has item 439) && (gameModeID == 15)
	// and serialises it (sub_579990) only when highFlag & 0x1000 is present.
	//
	// Our client UI (GameUI) never sets 0x1000 on its own in the UpdateRoomSettings
	// request, so gating on `clientSentBigHeadFlag` means the flag is (almost) never
	// echoed back. The engine (hw.dll) needs it in order to call
	// GameOptionManager::SetBigHead and scale the "Bip01 Head" bone in-game.
	// Therefore we (re)assert it whenever the room mode is BigHead-compatible,
	// in addition to honouring an explicit client request.
	bool clientSentBigHeadFlag = (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK1000) != 0;
	bool sendBigHeadFlag = clientSentBigHeadFlag || IsBigHeadCompatibleMode(currentMode);

	if (sendBigHeadFlag) {
		roomSettings.highFlag |= ROOMSETTINGS_HFLAG_UNK1000;
		roomSettings.unkh1000 = modeSubFlag;
		roomListFlag |= ROOMLIST_FLAG_UNK2000;
		serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] BigHead flag INCLUDED in reply - modeSubFlag: {}, mode: {}\n", modeSubFlag, currentMode));
	} else {
		// Still update the room's internal BigHead state (without sending it in this delta reply)
		RoomSettings internalUpdate;
		internalUpdate.highFlag = ROOMSETTINGS_HFLAG_UNK1000;
		internalUpdate.unkh1000 = modeSubFlag;
		room->UpdateRoomSettings(internalUpdate);
	}

	room->UpdateRoomSettings(roomSettings);

	roomManager.SendUpdateRoomPacketToAll(room, roomListFlag);

	vector<User*> roomUsers = room->GetRoomUsers();

	roomUsers.erase(remove_if(roomUsers.begin(), roomUsers.end(), [](User* u) {
		return (u == NULL || u->GetConnection() == NULL);
	}), roomUsers.end());

	for (auto& u : roomUsers) {
		SendPacket_Room_ReplyUpdateRoomSettings(u->GetConnection(), roomSettings);
	}
}

void Packet_RoomManager::parsePacket_Room_ResultConfirm(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InRoom && user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room ResultConfirm, but it's not in a room!\n", user->GetUserLogName()));
		return;
	}

	user->SetUserStatus(UserStatus::InRoom);

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room ResultConfirm, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	unsigned char unk = packet->ReadUInt8();

	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room ResultConfirm - unk: {}\n", user->GetUserLogName(), unk));

	packet_HostManager.SendPacket_Host_ResultClose(connection);
	SendPacket_Room_ReplyUpdateRoomSettings(connection, room->GetRoomSettings());

	const UserCharacterResult& ucr = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (ucr.result) {
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, ucr.userCharacter });
	}
}

void Packet_RoomManager::parsePacket_Room_ReturnToRoom(User* user) {
	if (user == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InRoom && user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room ReturnToRoom, but it's not in a room or in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room ReturnToRoom, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		user->SetUserStatus(UserStatus::InRoom);
		serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room ReturnToRoom (room match already ended)\n", user->GetUserLogName()));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room ReturnToRoom\n", user->GetUserLogName()));

	GameMatchUser* gameMatchUser = gameMatch->GetGameMatchUserByUser(user);
	if (gameMatchUser) {
		gameMatch->RemoveGameMatchUser(gameMatchUser);
	}

	if (user == room->GetRoomHostUser()) {
		vector<GameMatchUser*> gameMatchUsers = gameMatch->GetGameMatchUsers();

		gameMatchUsers.erase(remove_if(gameMatchUsers.begin(), gameMatchUsers.end(), [](GameMatchUser* gameMatchUser) {
			return (gameMatchUser->GetUser() == NULL || gameMatchUser->GetUser()->GetConnection() == NULL);
		}), gameMatchUsers.end());

		if (gameMatchUsers.empty()) {
			delete gameMatch;
			room->SetGameMatch(NULL);

			RoomSettings roomSettings;
			roomSettings.lowFlag = ROOMSETTINGS_LFLAG_ROOMSTATE;
			roomSettings.roomState = RoomState::Waiting;
			room->UpdateRoomSettings(roomSettings);

			roomManager.SendUpdateRoomPacketToAll(room, ROOMLIST_FLAG_ROOMSTATE);

			vector<User*> roomUsers = room->GetRoomUsers();

			roomUsers.erase(remove_if(roomUsers.begin(), roomUsers.end(), [](User* u) {
				return (u == NULL || u->GetConnection() == NULL);
			}), roomUsers.end());

			for (auto& u : roomUsers) {
				packet_RoomManager.SendPacket_Room_ReplyUpdateRoomSettings(u->GetConnection(), roomSettings);
				roomManager.SendUpdateRoomPacket(u->GetConnection(), room, ROOMLIST_FLAG_ROOMSTATE);
			}
		}
		else {
			User* newRoomHostUser = gameMatchUsers[0]->GetUser();
			room->UpdateRoomHostUser(newRoomHostUser);

			gameMatchUsers.erase(remove_if(gameMatchUsers.begin(), gameMatchUsers.end(), [newRoomHostUser](GameMatchUser* gameMatchUser) {
				return (gameMatchUser->GetUser() == newRoomHostUser);
			}), gameMatchUsers.end());

			unsigned long newRoomHostUserID = newRoomHostUser->GetUserID();
			const vector<unsigned char>& saveData = gameMatch->GetSaveData();

			packet_HostManager.SendPacket_Host_HostRestart(newRoomHostUser->GetConnection(), true, newRoomHostUserID, gameMatchUsers, saveData);

			for (auto& gameMatchUser : gameMatchUsers) {
				packet_HostManager.SendPacket_Host_HostRestart(gameMatchUser->GetUser()->GetConnection(), false, newRoomHostUserID);
				packet_HostManager.SendPacket_Host_HostJoin(gameMatchUser->GetUser()->GetConnection(), newRoomHostUserID);
			}

			if (saveData.empty()) {
				delete gameMatch;
				room->SetGameMatch(NULL);
			}
		}
	}

	user->SetUserStatus(UserStatus::InRoom);
}

void Packet_RoomManager::parsePacket_Room_UserReady(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InRoom) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room UserReady, but it's not in a room!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room UserReady, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	unsigned char ready = packet->ReadUInt8();
	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room UserReady: {}\n", user->GetUserLogName(), ready));

	room->SetUserReady(user, ready);

	unsigned long userID = user->GetUserID();
	vector<User*> roomUsers = room->GetRoomUsers();
	for (auto& u : roomUsers) {
		if (u && u->GetConnection()) {
			SendPacket_Room_UserReady(u->GetConnection(), userID, ready);
		}
	}
}

void Packet_RoomManager::parsePacket_Room_ChangeTeam(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InRoom) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room ChangeTeam, but it's not in a room!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room ChangeTeam, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	unsigned char targetTeam = packet->ReadUInt8();
	serverConsole.Print(PrefixType::Info, format("[ Packet_RoomManager ] User ({}) has sent Packet_Room ChangeTeam to {}\n", user->GetUserLogName(), targetTeam));

	if (IsScenarioMode(room->GetRoomSettings().gameModeID)) {
		targetTeam = RoomTeam::CounterTerrorist;
	}
	else if (targetTeam != RoomTeam::Terrorist && targetTeam != RoomTeam::CounterTerrorist && targetTeam != RoomTeam::Spectator) {
		targetTeam = RoomTeam::Terrorist;
	}

	room->SetUserTeam(user, targetTeam);

	unsigned long userID = user->GetUserID();
	vector<User*> roomUsers = room->GetRoomUsers();
	for (auto& u : roomUsers) {
		if (u && u->GetConnection()) {
			SendPacket_Room_TeamChange(u->GetConnection(), userID, targetTeam);
		}
	}
}

void Packet_RoomManager::buildRoomInfo(TCPConnection::Packet::pointer packet, Room* room, unsigned short flag) {
	if (packet == NULL || room == NULL) {
		return;
	}

	packet->WriteUInt16_LE(room->GetRoomID());
	packet->WriteUInt16_LE(flag);

	const RoomSettings& roomSettings = room->GetRoomSettings();
	const UserNetwork& userNetwork = room->GetRoomHostUser()->GetUserNetwork();

	if (flag & ROOMLIST_FLAG_ROOMNAME) {
		packet->WriteString(roomSettings.roomName);
	}
	if (flag & ROOMLIST_FLAG_UNK2) {
		packet->WriteUInt8(0); // unk
	}
	if (flag & ROOMLIST_FLAG_ISPASSWORDED) {
		packet->WriteBool(!roomSettings.password.empty());
	}
	if (flag & ROOMLIST_FLAG_LEVELLIMIT) {
		packet->WriteUInt8(roomSettings.levelLimit);
	}
	if (flag & ROOMLIST_FLAG_GAMEMODEID) {
		packet->WriteUInt8(roomSettings.gameModeID);
	}
	if (flag & ROOMLIST_FLAG_MAPID) {
		packet->WriteUInt8(roomSettings.mapID);
	}
	if (flag & ROOMLIST_FLAG_CURRENTPLAYERS) {
		packet->WriteUInt8((unsigned char)room->GetRoomUsers().size());
	}
	if (flag & ROOMLIST_FLAG_MAXPLAYERS) {
		packet->WriteUInt8(roomSettings.maxPlayers);
	}
	if (flag & ROOMLIST_FLAG_WEAPONLIMIT) {
		packet->WriteUInt8(roomSettings.weaponLimit);
	}
	if (flag & ROOMLIST_FLAG_ROOMHOST) {
		packet->WriteUInt32_LE(room->GetRoomHostUser()->GetUserID());
		packet->WriteUInt8(0); // unk
	}
	if (flag & ROOMLIST_FLAG_UNK400) {
		packet->WriteUInt8(0); // unk
	}
	if (flag & ROOMLIST_FLAG_ROOMHOSTNETWORK) {
		packet->WriteUInt32_LE(userNetwork.localIP);
		packet->WriteUInt16_LE(userNetwork.localHostPort);
		packet->WriteUInt32_LE(serverConfig.ip);
		packet->WriteUInt16_LE(serverConfig.port);
		packet->WriteUInt8(0); // unk
	}
	if (flag & ROOMLIST_FLAG_UNK1000) {
		packet->WriteUInt8(0); // unk
	}
	if (flag & ROOMLIST_FLAG_UNK2000) {
		packet->WriteUInt8(roomSettings.unkh1000);
	}
	if (flag & ROOMLIST_FLAG_ROOMSTATE) {
		packet->WriteUInt8((unsigned char)roomSettings.roomState);
	}
}

void Packet_RoomManager::buildRoomSettings(TCPConnection::Packet::pointer packet, const RoomSettings& roomSettings) {
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt32_LE(roomSettings.lowFlag);
	packet->WriteUInt32_LE(roomSettings.highFlag);

	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_ROOMNAME) {
		packet->WriteString(roomSettings.roomName);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK2) {
		packet->WriteUInt8(roomSettings.unk2); // unk
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK4) {
		packet->WriteUInt8(roomSettings.unk4_1); // unk
		packet->WriteUInt8(roomSettings.unk4_2); // unk
		packet->WriteUInt8(roomSettings.unk4_3); // unk
		packet->WriteUInt32_LE(roomSettings.unk4_4); // unk
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_PASSWORD) {
		packet->WriteString(roomSettings.password);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_LEVELLIMIT) {
		packet->WriteUInt8(roomSettings.levelLimit);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK20) {
		packet->WriteUInt8(roomSettings.unk20); // unk
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_GAMEMODEID) {
		packet->WriteUInt8(roomSettings.gameModeID);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_MAPID) {
		packet->WriteUInt8(roomSettings.mapID);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_MAXPLAYERS) {
		packet->WriteUInt8(roomSettings.maxPlayers);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_WINLIMIT) {
		packet->WriteUInt8(roomSettings.winLimit);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_KILLLIMIT) {
		packet->WriteUInt16_LE(roomSettings.killLimit);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_TIMELIMIT) {
		packet->WriteUInt8(roomSettings.timeLimit);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_ROUNDTIME) {
		packet->WriteUInt8(roomSettings.roundTime);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_WEAPONLIMIT) {
		packet->WriteUInt8(roomSettings.weaponLimit);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_HOSTAGEPENALTY) {
		packet->WriteUInt8(roomSettings.hostagePenalty);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FREEZETIME) {
		packet->WriteUInt8(roomSettings.freezeTime);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_BUYTIME) {
		packet->WriteUInt8(roomSettings.buyTime);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_NICKNAMEDISPLAY) {
		packet->WriteUInt8(roomSettings.nickNameDisplay);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK40000) {
		packet->WriteUInt8(roomSettings.unk40000); // unk
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK80000) {
		packet->WriteUInt8(roomSettings.unk80000); // unk
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FRIENDLYFIRE) {
		packet->WriteBool(roomSettings.friendlyFire);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FLASHLIGHT) {
		packet->WriteBool(roomSettings.flashLight);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FOOTSTEPS) {
		packet->WriteBool(roomSettings.footSteps);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK800000) {
		packet->WriteUInt8(roomSettings.unk800000); // unk
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK1000000) {
		packet->WriteUInt8(roomSettings.unk1000000); // unk
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK2000000) {
		packet->WriteUInt8(roomSettings.unk2000000); // unk
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK4000000) {
		packet->WriteUInt8(roomSettings.unk4000000); // unk
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK8000000) {
		packet->WriteUInt8(roomSettings.unk8000000); // unk
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_DEATHCAMERATYPE) {
		packet->WriteUInt8(roomSettings.deathCameraType);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_VOICECHAT) {
		packet->WriteBool(roomSettings.voiceChat);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_ROOMSTATE) {
		packet->WriteUInt8((unsigned char)roomSettings.roomState);
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK80000000) {
		packet->WriteUInt8(roomSettings.unk80000000_1); // unk
		for (unsigned char i = 0; i < 2; i++) {
			packet->WriteUInt32_LE(roomSettings.unk80000000_2[i].unk8000000_vec_1); // unk
			packet->WriteUInt32_LE(roomSettings.unk80000000_2[i].unk8000000_vec_2); // unk
			packet->WriteUInt8(roomSettings.unk80000000_2[i].unk8000000_vec_3); // unk
			packet->WriteUInt8(roomSettings.unk80000000_2[i].unk8000000_vec_4); // unk
			packet->WriteUInt8(roomSettings.unk80000000_2[i].unk8000000_vec_5); // unk
			packet->WriteUInt8(roomSettings.unk80000000_2[i].unk8000000_vec_6); // unk
			packet->WriteUInt16_LE(roomSettings.unk80000000_2[i].unk8000000_vec_7); // unk
			packet->WriteUInt8(roomSettings.unk80000000_2[i].unk8000000_vec_8); // unk
			packet->WriteUInt8(roomSettings.unk80000000_2[i].unk8000000_vec_9); // unk
		}
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK1) {
		packet->WriteUInt32_LE(roomSettings.unkh1_1); // unk
		packet->WriteString(roomSettings.unkh1_2); // unk
		packet->WriteUInt8(roomSettings.unkh1_3); // unk
		packet->WriteUInt8(roomSettings.unkh1_4); // unk
		packet->WriteUInt8(roomSettings.unkh1_5); // unk
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK2) {
		packet->WriteUInt8(roomSettings.unkh2); // unk
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK4) {
		packet->WriteUInt8(roomSettings.unkh4_1);
		packet->WriteUInt8(roomSettings.unkh4_2);
		packet->WriteUInt8(roomSettings.unkh4_3);
		packet->WriteUInt8(roomSettings.unkh4_4);
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK8) {
		packet->WriteUInt8(roomSettings.unkh8);
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK10) {
		packet->WriteUInt16_LE(roomSettings.unkh10);
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK20) {
		packet->WriteUInt8(roomSettings.unkh20);
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK40) {
		packet->WriteUInt8(roomSettings.unkh40);
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK80) {
		packet->WriteUInt8(roomSettings.unkh80);
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK100) {
		packet->WriteUInt8(roomSettings.unkh100);
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK200) {
		packet->WriteUInt8((unsigned char)roomSettings.unkh200.size());
		for (const auto& item : roomSettings.unkh200) {
			packet->WriteUInt8(item.first);
			packet->WriteUInt8(item.second);
		}
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK400) {
		packet->WriteUInt8(roomSettings.unkh400);
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK800) {
		packet->WriteUInt8(roomSettings.unkh800);
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK1000) {
		packet->WriteUInt8(roomSettings.unkh1000);
	}
}

void Packet_RoomManager::buildRoomUserInfo(TCPConnection::Packet::pointer packet, const GameUser& gameUser, unsigned char team, unsigned char status, unsigned char slot) {
	if (packet == NULL || gameUser.user == NULL) {
		return;
	}

	packet->WriteUInt32_LE(gameUser.user->GetUserID());
	packet->WriteUInt8(team); // team: 1 = TR, 2 = CT, 3 = Spectator
	packet->WriteUInt8(status); // status: 0 = not ready, 2 = ready
	packet->WriteUInt8(slot); // slot (0..31)

	const UserNetwork& userNetwork = gameUser.user->GetUserNetwork();

	packet->WriteUInt32_LE(userNetwork.externalIP);
	packet->WriteUInt16_LE(userNetwork.externalHostPort);
	packet->WriteUInt16_LE(userNetwork.externalGuestPort);
	packet->WriteUInt32_LE(userNetwork.localIP);
	packet->WriteUInt16_LE(userNetwork.localHostPort);
	packet->WriteUInt16_LE(userNetwork.localGuestPort);

	packetManager.BuildUserCharacter(packet, gameUser.userCharacter);
}

void Packet_RoomManager::sendPacket_Room_ReplyCreateAndJoin(TCPConnection::pointer connection, Room* room, const vector<GameUser>& gameUsers) {
	if (connection == NULL || room == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Room });
	if (packet == NULL) {
		return;
	}

	User* roomHostUser = room->GetRoomHostUser();
	if (roomHostUser == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_RoomType::ReplyCreateAndJoin);
	packet->WriteUInt32_LE(roomHostUser->GetUserID());
	packet->WriteUInt16_LE(room->GetRoomID());
	packet->WriteUInt8(0); // ping, 0~1 - Worst, 2 - Bad, 3 - Normal, >=4 - Good

	buildRoomSettings(packet, room->GetRoomSettings());

	packet->WriteUInt8((unsigned char)gameUsers.size());

	for (auto& gameUser : gameUsers) {
		RoomUser* ru = room->GetRoomUser(gameUser.user);
		unsigned char team = ru ? ru->team : RoomTeam::Terrorist;
		unsigned char status = ru ? ru->status : 0;
		unsigned char slot = ru ? ru->slot : 0;
		buildRoomUserInfo(packet, gameUser, team, status, slot);
	}

	packet->Send();
}

void Packet_RoomManager::sendPacket_Room_UserJoin(TCPConnection::pointer connection, const GameUser& gameUser, unsigned char team, unsigned char status, unsigned char slot) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Room });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_RoomType::UserJoin);
	buildRoomUserInfo(packet, gameUser, team, status, slot);

	packet->Send();
}

void Packet_RoomManager::SendPacket_Room_UserReady(TCPConnection::pointer connection, unsigned long userID, unsigned char ready) {
	sendPacket_Room_UserReady(connection, userID, ready);
}

void Packet_RoomManager::sendPacket_Room_UserReady(TCPConnection::pointer connection, unsigned long userID, unsigned char ready) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Room });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_RoomType::UserReady);
	packet->WriteUInt32_LE(userID);
	packet->WriteUInt8(ready);

	packet->Send();
}

void Packet_RoomManager::SendPacket_Room_TeamChange(TCPConnection::pointer connection, unsigned long userID, unsigned char team) {
	sendPacket_Room_TeamChange(connection, userID, team);
}

void Packet_RoomManager::sendPacket_Room_TeamChange(TCPConnection::pointer connection, unsigned long userID, unsigned char team) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Room });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_RoomType::TeamChange);
	packet->WriteUInt32_LE(userID);
	packet->WriteUInt8(team);

	packet->Send();
}

void Packet_RoomManager::sendPacket_Room_LeaveRoomInGame(TCPConnection::pointer connection) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Room });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_RoomType::LeaveRoomInGame);

	packet->Send();
}