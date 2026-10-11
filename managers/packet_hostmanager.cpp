#include "packet_hostmanager.h"
#include "usermanager.h"
#include "roommanager.h"
#include "packet_roommanager.h"
#include "serverconsole.h"
#include "serverconfig.h"
#include "databasemanager.h"
#include "packetmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_itemmanager.h"
#include "questmanager.h"
#include <set>
#include <map>
#include <algorithm>

Packet_HostManager packet_HostManager;

void Packet_HostManager::ParsePacket_Host(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (!userManager.IsUserLoggedIn(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] Client ({}) has sent Packet_Host, but it's not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] Parsing Packet_Host from user ({})\n", user->GetUserLogName()));

	unsigned char type = packet->ReadUInt8();

	switch (type) {
		case Packet_HostType::ServerIsActivated: {
			parsePacket_Host_ServerIsActivated(user);
			break;
		}
		case Packet_HostType::SaveData: {
			parsePacket_Host_SaveData(user, packet);
			break;
		}
		case Packet_HostType::SendUserStatus: {
			parsePacket_Host_SendUserStatus(user, packet);
			break;
		}
		case Packet_HostType::PlayerKilled: {
			parsePacket_Host_PlayerKilled(user, packet);
			break;
		}
		case Packet_HostType::EndGame: {
			parsePacket_Host_EndGame(user, packet);
			break;
		}
		case Packet_HostType::RejoinFail: {
			parsePacket_Host_RejoinFail(user, packet);
			break;
		}
		case Packet_HostType::AddKill: {
			parsePacket_Host_AddKill(user, packet);
			break;
		}
		case Packet_HostType::AddDeath: {
			parsePacket_Host_AddDeath(user, packet);
			break;
		}
		case Packet_HostType::RoundResult: {
			parsePacket_Host_RoundResult(user, packet);
			break;
		}
		case Packet_HostType::SelectTeam: {
			parsePacket_Host_SelectTeam(user, packet);
			break;
		}
		case Packet_HostType::UserWeapons: {
			parsePacket_Host_UserWeapons(user, packet);
			break;
		}
		case Packet_HostType::ItemConsumed: {
			parsePacket_Host_ItemConsumed(user, packet);
			break;
		}
		case Packet_HostType::PlayerPosition: {
			packet->ReadUInt32_LE();
			packet->ReadBytes<float>();
			packet->ReadBytes<float>();
			packet->ReadBytes<float>();
			if (packet->GetBuffer().size() >= (size_t)packet->GetReadOffset() + 4) {
				packet->ReadBytes<float>();
			}
			break;
		}
		case Packet_HostType::PlayerBuyItem: {
			unsigned long userID = packet->ReadUInt32_LE();
			unsigned short itemID = packet->ReadUInt16_LE();
			unsigned char count = packet->ReadUInt8();
			serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) PlayerBuyItem - userID: {}, itemID: {}, count: {}\n", user->GetUserLogName(), userID, itemID, (int)count));
			break;
		}
		case Packet_HostType::FlyerFlock: {
			string name = packet->ReadString();
			serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) requested FlyerFlock - name: {}\n", user->GetUserLogName(), name));
			SendPacket_Host_FlyerFlock(connection, name);
			break;
		}
		case Packet_HostType::GameEvent: {
			// Telemetry / event telemetry sent from client (e.g. eventID: 33, userID), consume silently
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent unregistered Packet_Host type {}!\n", user->GetUserLogName(), type));
			break;
		}
	}
}

void Packet_HostManager::SendPacket_Host_HostStart(User* user) {
	if (user == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Host });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_HostType::HostStart);
	packet->WriteUInt32_LE(user->GetUserID());

	packet->Send();
}

void Packet_HostManager::SendPacket_Host_HostJoin(TCPConnection::pointer connection, unsigned long userID) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Host });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_HostType::HostJoin);
	packet->WriteUInt32_LE(userID);

	packet->Send();
}

void Packet_HostManager::SendPacket_Host_HostRestart(TCPConnection::pointer connection, bool isRoomHost, unsigned long roomHostUserID, const vector<GameMatchUser*>& gameMatchUsers, const vector<unsigned char>& saveData) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Host });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_HostType::HostRestart);
	packet->WriteUInt32_LE(roomHostUserID);

	if (isRoomHost) {
		packet->WriteUInt16_LE((unsigned short)saveData.size());

		if (!saveData.empty()) {
			packet->WriteArray_UInt8(saveData);
			packet->WriteUInt8((unsigned char)gameMatchUsers.size());

			for (auto& gameMatchUser : gameMatchUsers) {
				packet->WriteUInt32_LE(gameMatchUser->GetUser()->GetUserID());
			}
		}
	}
	else {
		packet->WriteUInt8(1); // crashFlag
	}

	packet->Send();
}

void Packet_HostManager::SendPacket_Host_ResultClose(TCPConnection::pointer connection) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Host });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_HostType::ResultClose);

	packet->Send();
}

void Packet_HostManager::SendPacket_Host_FlyerFlock(TCPConnection::pointer connection, const string& name) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Host });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_HostType::FlyerFlock);
	packet->WriteString(name);
	packet->WriteUInt8(1);

	packet->Send();
}

void Packet_HostManager::parsePacket_Host_ServerIsActivated(User* user) {
	if (user == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InRoom && user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host ServerIsActivated, but it's not in a room or in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host ServerIsActivated, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	if (room->GetRoomHostUser() != user) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host ServerIsActivated, but it's not the room host!\n", user->GetUserLogName()));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) has sent Packet_Host ServerIsActivated\n", user->GetUserLogName()));

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		GameMatch* newGameMatch = new GameMatch(room->GetRoomID());
		if (newGameMatch == NULL) {
			return;
		}

		for (auto& u : room->GetRoomUsers()) {
			if (u == NULL) continue;
			GameMatchUser* gmu = new GameMatchUser(u);
			RoomUser* ru = room->GetRoomUser(u);
			if (ru) {
				gmu->SetTeam((Team)ru->team);
			}
			newGameMatch->AddGameMatchUser(gmu);
		}

		room->SetGameMatch(newGameMatch);

		RoomSettings roomSettings;
		roomSettings.lowFlag = ROOMSETTINGS_LFLAG_ROOMSTATE;
		roomSettings.roomState = RoomState::Playing;
		roomSettings.highFlag = ROOMSETTINGS_HFLAG_UNK80;
		roomSettings.unkh80 = 3;

		// BigHead (item 439) re-assert at match start.
		// The engine (hw.dll) only learns the BigHead state from a room-settings
		// packet that carries highFlag & 0x1000 (GameOptionManager::SetBigHead via
		// sub_372A7C30). The lobby-side reply is sent while the client is still in
		// the room, so we must repeat the flag together with the "room state = Playing"
		// delta, otherwise the in-game engine session keeps BigHead = 0 and the
		// "Bip01 Head" bone is never scaled (zs_trap etc.).
		const RoomSettings& currentRoomSettings = room->GetRoomSettings();
		if (IsBigHeadCompatibleMode(currentRoomSettings.gameModeID)) {
			roomSettings.highFlag |= ROOMSETTINGS_HFLAG_UNK1000;
			roomSettings.unkh1000 = CalculateRoomModeSubFlag(room->GetRoomHostUser(), currentRoomSettings.gameModeID, room->IsBigHeadDisabled());
		}

		room->UpdateRoomSettings(roomSettings);

		roomManager.SendUpdateRoomPacketToAll(room, ROOMLIST_FLAG_ROOMSTATE);

		vector<User*> roomUsers = room->GetRoomUsers();

		roomUsers.erase(remove_if(roomUsers.begin(), roomUsers.end(), [](User* u) {
			return (u == NULL || u->GetConnection() == NULL);
		}), roomUsers.end());

		for (auto& u : roomUsers) {
			packet_RoomManager.SendPacket_Room_ReplyUpdateRoomSettings(u->GetConnection(), roomSettings);
			if (u != user) {
				SendPacket_Host_HostJoin(u->GetConnection(), user->GetUserID());
			}
		}
	}
}

void Packet_HostManager::parsePacket_Host_SaveData(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SaveData, but it's not in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SaveData, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SaveData, but its room doesn't have a gameMatch!\n", user->GetUserLogName()));
		return;
	}

	if (room->GetRoomHostUser() != user) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SaveData, but it's not the room host!\n", user->GetUserLogName()));
		return;
	}

	unsigned short saveSize = packet->ReadUInt16_LE();

	if (saveSize == 0 || saveSize > SAVEDATA_MAX_SIZE) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SaveData, but its saveSize is invalid: {}!\n", user->GetUserLogName(), saveSize));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SaveData - saveSize: {}\n", user->GetUserLogName(), saveSize));

	const vector<unsigned char>& saveData = packet->ReadArray_UInt8(saveSize);
	gameMatch->SetSaveData(saveData);
}

void Packet_HostManager::parsePacket_Host_SendUserStatus(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InRoom && user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SendUserStatus, but it's not in a room or in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SendUserStatus, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SendUserStatus, but its room doesn't have a gameMatch!\n", user->GetUserLogName()));
		return;
	}

	if (room->GetRoomHostUser() != user) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SendUserStatus, but it's not the room host!\n", user->GetUserLogName()));
		return;
	}

	unsigned long userID = packet->ReadUInt32_LE();

	User* targetUser = userManager.GetUserByUserID(userID);
	if (!targetUser) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SendUserStatus, but targetUser is not logged in!\n", user->GetUserLogName()));
		return;
	}

	if (targetUser->GetUserStatus() != UserStatus::InRoom && targetUser->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SendUserStatus, but targetUser is not in a room or in game!\n", user->GetUserLogName()));
		return;
	}

	if (roomManager.GetRoomByRoomID(targetUser->GetCurrentRoomID()) != room) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SendUserStatus, but targetUser is not in the same room!\n", user->GetUserLogName()));
		return;
	}

	unsigned char status = packet->ReadUInt8();

	if (status != 1) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SendUserStatus, but status is invalid: {}!\n", user->GetUserLogName(), status));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SendUserStatus - userID: {}, status: {}\n", user->GetUserLogName(), userID, status));

	GameMatchUser* gameMatchUser = gameMatch->GetGameMatchUserByUser(targetUser);
	if (!gameMatchUser) {
		gameMatchUser = new GameMatchUser(targetUser);
		RoomUser* ru = room->GetRoomUser(targetUser);
		if (ru) {
			gameMatchUser->SetTeam((Team)ru->team);
		}
		if (!gameMatch->AddGameMatchUser(gameMatchUser)) {
			return;
		}
	}

	targetUser->SetUserStatus(UserStatus::InGame);
}

void Packet_HostManager::parsePacket_Host_PlayerKilled(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host PlayerKilled, but it's not in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host PlayerKilled, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host PlayerKilled, but its room doesn't have a gameMatch!\n", user->GetUserLogName()));
		return;
	}

	if (room->GetRoomHostUser() != user) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host PlayerKilled, but it's not the room host!\n", user->GetUserLogName()));
		return;
	}

	unsigned long attackerUserID = packet->ReadUInt32_LE();

	if (attackerUserID) {
		User* targetUser = userManager.GetUserByUserID(attackerUserID);
		if (!targetUser) {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host PlayerKilled, but targetUser is not logged in!\n", user->GetUserLogName()));
			return;
		}

		GameMatchUser* gameMatchUser = gameMatch->GetGameMatchUserByUser(targetUser);
		if (gameMatchUser == NULL) {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host PlayerKilled, but gameMatchUser is NULL!\n", user->GetUserLogName()));
			return;
		}
	}

	unsigned char attackerWeaponID = packet->ReadUInt8();
	signed char attackerArmor = packet->ReadUInt8();
	signed char attackerHealth = packet->ReadUInt8();
	unsigned long victimUserID = packet->ReadUInt32_LE();
	unsigned char victimWeaponID = packet->ReadUInt8();
	signed char victimArmor = packet->ReadUInt8();
	signed char victimHealth = packet->ReadUInt8();

	float attX = 0.0f, attY = 0.0f, attZ = 0.0f, vicX = 0.0f, vicY = 0.0f, vicZ = 0.0f;
	if (packet->GetBuffer().size() >= (size_t)(packet->GetReadOffset() + 24)) {
		attX = packet->ReadBytes<float>();
		attY = packet->ReadBytes<float>();
		attZ = packet->ReadBytes<float>();
		vicX = packet->ReadBytes<float>();
		vicY = packet->ReadBytes<float>();
		vicZ = packet->ReadBytes<float>();
	}

	float dx = attX - vicX;
	float dy = attY - vicY;
	float dz = attZ - vicZ;
	unsigned short distance = (unsigned short)sqrt(dx * dx + dy * dy + dz * dz);

	bool isHeadshot = (attackerWeaponID & 0x80) != 0;
	unsigned char realWeaponID = attackerWeaponID & 0x7F;

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) has sent Packet_Host PlayerKilled - attackerUserID: {}, weapon: {} (HS: {}), attackerArmor: {}, attackerHealth: {}, victimUserID: {}, victimWeaponID: {}, victimArmor: {}, victimHealth: {}, distance: {}\n",
		user->GetUserLogName(), attackerUserID, (int)realWeaponID, isHeadshot ? 1 : 0, attackerArmor, attackerHealth, victimUserID, victimWeaponID, victimArmor, victimHealth, distance));

	User* attackerUser = attackerUserID ? userManager.GetUserByUserID(attackerUserID) : NULL;
	User* victimUser = victimUserID ? userManager.GetUserByUserID(victimUserID) : NULL;

	if (attackerUser) {
		questManager.OnPlayerKilled(attackerUser, victimUser, realWeaponID, isHeadshot, distance);
	}
	if (victimUser) {
		questManager.OnPlayerDied(victimUser);
	}
}

void Packet_HostManager::parsePacket_Host_EndGame(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host EndGame, but it's not in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host EndGame, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host EndGame, but its room doesn't have a gameMatch!\n", user->GetUserLogName()));
		return;
	}

	if (room->GetRoomHostUser() != user) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host EndGame, but it's not the room host!\n", user->GetUserLogName()));
		return;
	}

	unsigned char winTeam = packet->ReadUInt8();
	unsigned short terWins = packet->ReadUInt16_LE();
	unsigned char ctWins = packet->ReadUInt8();
	unsigned long matchDuration = packet->ReadUInt32_LE();

	if (winTeam == 0 && gameMatch->GetWinTeam() != 0) {
		winTeam = gameMatch->GetWinTeam();
	}
	if (terWins == 0 && ctWins == 0) {
		terWins = gameMatch->GetTerWins();
		ctWins = gameMatch->GetCtWins();
	}
	if (winTeam == 0) {
		if (terWins > ctWins) winTeam = 1;
		else if (ctWins > terWins) winTeam = 2;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) has sent Packet_Host EndGame - winTeam: {}, terWins: {}, ctWins: {}, matchDuration: {}\n",
		user->GetUserLogName(), winTeam, terWins, ctWins, matchDuration));

	vector<GameMatchUser*> gameMatchUsers = gameMatch->GetGameMatchUsers();

	gameMatchUsers.erase(remove_if(gameMatchUsers.begin(), gameMatchUsers.end(), [](GameMatchUser* gameMatchUser) {
		return (gameMatchUser->GetUser() == NULL || gameMatchUser->GetUser()->GetConnection() == NULL);
	}), gameMatchUsers.end());

	// Sort by kills descending for ranking
	sort(gameMatchUsers.begin(), gameMatchUsers.end(), [](GameMatchUser* a, GameMatchUser* b) {
		if (a->GetKills() != b->GetKills()) {
			return a->GetKills() > b->GetKills();
		}
		return a->GetDeaths() < b->GetDeaths();
	});

	unsigned char gameModeID = room ? room->GetRoomSettings().gameModeID : 0;
	// Only competitive PVP modes record battles/wins/frags/deaths to user personal stats:
	// 0: Original, 1: DeathMatch, 2: TeamDeathMatch, 6: Official, 7: Official_TieBreak, 22: GunDeath, 23: LightOri
	bool recordStats = (gameModeID == 0 || gameModeID == 1 || gameModeID == 2 || 
	                    gameModeID == 6 || gameModeID == 7 || 
	                    gameModeID == 22 || gameModeID == 23);

	// Calculate and persist match results for each player
	for (size_t i = 0; i < gameMatchUsers.size(); ++i) {
		GameMatchUser* gmu = gameMatchUsers[i];
		User* u = gmu->GetUser();

		unsigned char team = (unsigned char)gmu->GetTeam();
		if (team != 1 && team != 2) {
			RoomUser* ru = room ? room->GetRoomUser(u) : NULL;
			team = ru ? ru->team : 1;
		}

		unsigned short kills = gmu->GetKills();
		unsigned short deaths = gmu->GetDeaths();
		bool isWin = false;
		if (gameModeID == 1 || gameModeID == 22) {
			isWin = (gmu->GetRank() == 1);
		}
		else {
			isWin = (winTeam != 0 && team == winTeam);
		}

		unsigned short earnedExp = 100 + kills * 10 + (isWin ? 50 : 0);
		unsigned short earnedPoints = 200 + kills * 15 + (isWin ? 100 : 0);

		gmu->SetExp(earnedExp);
		gmu->SetPoints(earnedPoints);
		gmu->SetRank((unsigned char)(i + 1));

		// Fetch current character stats from database
		UserCharacterResult ucr = u->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		unsigned char oldLevel = ucr.userCharacter.level ? ucr.userCharacter.level : 1;
		unsigned long long newExp = ucr.userCharacter.exp + earnedExp;
		unsigned long long newPoints = ucr.userCharacter.points + earnedPoints;
		unsigned char newLevel = max(oldLevel, User::CalculateLevel(newExp));
		unsigned char levelUp = (newLevel > oldLevel) ? 1 : 0;

		gmu->SetLevelUp(levelUp);

		// Save battle result to database (recordStats only for competitive modes)
		databaseManager.SaveUserBattleResult(u->GetUserID(), earnedExp, earnedPoints, kills, deaths, isWin, newLevel, recordStats);

		// Quest progression hook
		questManager.OnMatchEnd(u, isWin, kills, deaths, matchDuration);

		// Note: DO NOT send UpdateInfo here!
		// The client settlement dialog (CResultCurrentPlayerInfoPanel) expects localPlayer in memory
		// to still hold the pre-match level and EXP, adding earnedExp itself.
		// Sending UpdateInfo before GameResult causes the client to double-add earnedExp,
		// displaying wrong percentage and level.
		// UpdateInfo is sent when the user confirms the settlement dialog (ResultConfirm).
	}

	for (auto& gameMatchUser : gameMatchUsers) {
		User* u = gameMatchUser->GetUser();

		u->SetUserStatus(UserStatus::InRoom);
		SendPacket_Host_HostEndSession(u->GetConnection());
		packet_RoomManager.SendPacket_Room_GameResult(u->GetConnection(), room, gameMatch, winTeam, (unsigned char)terWins, ctWins);

		// Scenario 3: Pop up expired items notice on the settlement screen
		userManager.CheckAndProcessUserExpiredItems(u, true);
	}

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

		if (u != room->GetRoomHostUser()) {
			room->SetUserReady(u, 0);
			for (auto& dest : roomUsers) {
				packet_RoomManager.SendPacket_Room_UserReady(dest->GetConnection(), u->GetUserID(), 0);
			}
		}
	}
}

void Packet_HostManager::parsePacket_Host_RejoinFail(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host RejoinFail, but it's not in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host RejoinFail, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host RejoinFail, but its room doesn't have a gameMatch!\n", user->GetUserLogName()));
		return;
	}

	if (room->GetRoomHostUser() != user) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host RejoinFail, but it's not the room host!\n", user->GetUserLogName()));
		return;
	}

	unsigned char rejoinFailUserCount = packet->ReadUInt8();

	string rejoinFailUserIDs;
	for (unsigned char i = 0; i < rejoinFailUserCount; i++) {
		unsigned long rejoinFailUserID = packet->ReadUInt32_LE();
		rejoinFailUserIDs += format(" {},", rejoinFailUserID);
	}
	rejoinFailUserIDs[rejoinFailUserIDs.size() - 1] = ' ';

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) has sent Packet_Host RejoinFail - rejoinFailUserCount: {}, rejoinFailUserIDs:{}\n", user->GetUserLogName(), rejoinFailUserCount, rejoinFailUserIDs));
}

void Packet_HostManager::parsePacket_Host_AddKill(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddKill, but it's not in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddKill, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddKill, but its room doesn't have a gameMatch!\n", user->GetUserLogName()));
		return;
	}

	if (room->GetRoomHostUser() != user) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddKill, but it's not the room host!\n", user->GetUserLogName()));
		return;
	}

	unsigned long userID = packet->ReadUInt32_LE();

	GameMatchUser* gameMatchUser = NULL;
	if (userID) {
		User* targetUser = userManager.GetUserByUserID(userID);
		if (targetUser) {
			gameMatchUser = gameMatch->GetGameMatchUserByUser(targetUser);
		}
	}

	signed char point = packet->ReadUInt8();
	signed short frags = packet->ReadUInt16_LE();

	if (gameMatchUser && frags >= 0) {
		gameMatchUser->SetKills((unsigned short)frags);
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddKill - userID: {}, point: {}, frags: {}\n", user->GetUserLogName(), userID, point, frags));
}

void Packet_HostManager::parsePacket_Host_AddDeath(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddDeath, but it's not in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddDeath, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddDeath, but its room doesn't have a gameMatch!\n", user->GetUserLogName()));
		return;
	}

	if (room->GetRoomHostUser() != user) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddDeath, but it's not the room host!\n", user->GetUserLogName()));
		return;
	}

	unsigned long userID = packet->ReadUInt32_LE();

	User* targetUser = userManager.GetUserByUserID(userID);
	if (!targetUser) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddDeath, but targetUser is not logged in!\n", user->GetUserLogName()));
		return;
	}

	GameMatchUser* gameMatchUser = gameMatch->GetGameMatchUserByUser(targetUser);
	if (gameMatchUser == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddDeath, but gameMatchUser is NULL!\n", user->GetUserLogName()));
		return;
	}

	signed char point = packet->ReadUInt8();
	signed short deaths = packet->ReadUInt16_LE();

	if (deaths >= 0) {
		gameMatchUser->SetDeaths((unsigned short)deaths);
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) has sent Packet_Host AddDeath - userID: {}, deathCount: {}, totalDeaths: {}\n", user->GetUserLogName(), userID, point, deaths));
}

void Packet_HostManager::parsePacket_Host_RoundResult(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host RoundResult, but it's not in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host RoundResult, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host RoundResult, but its room doesn't have a gameMatch!\n", user->GetUserLogName()));
		return;
	}

	if (room->GetRoomHostUser() != user) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host RoundResult, but it's not the room host!\n", user->GetUserLogName()));
		return;
	}

	unsigned char winTeam = packet->ReadUInt8();
	unsigned char terWins = packet->ReadUInt8();
	unsigned char ctWins = packet->ReadUInt8();

	gameMatch->SetWinTeam(winTeam);
	gameMatch->SetTerWins(terWins);
	gameMatch->SetCtWins(ctWins);

	for (auto& gmu : gameMatch->GetGameMatchUsers()) {
		if (gmu && gmu->GetUser()) {
			if ((unsigned char)gmu->GetTeam() == winTeam) {
				questManager.OnRoundWin(gmu->GetUser());
			}
		}
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) has sent Packet_Host RoundResult - winTeam: {}, terWins: {}, ctWins: {}\n", user->GetUserLogName(), winTeam, terWins, ctWins));
}

void Packet_HostManager::parsePacket_Host_SelectTeam(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InRoom && user->GetUserStatus() != UserStatus::InGame) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SelectTeam, but it's not in a room or in game!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SelectTeam, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SelectTeam, but its room doesn't have a gameMatch!\n", user->GetUserLogName()));
		return;
	}

	if (room->GetRoomHostUser() != user) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SelectTeam, but it's not the room host!\n", user->GetUserLogName()));
		return;
	}

	unsigned long userID = packet->ReadUInt32_LE();

	User* targetUser = userManager.GetUserByUserID(userID);
	if (!targetUser) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SelectTeam, but targetUser is not logged in!\n", user->GetUserLogName()));
		return;
	}

	GameMatchUser* gameMatchUser = gameMatch->GetGameMatchUserByUser(targetUser);
	if (gameMatchUser == NULL) {
		gameMatchUser = new GameMatchUser(targetUser);
		gameMatch->AddGameMatchUser(gameMatchUser);
	}

	unsigned char team = packet->ReadUInt8();

	if (team != Team::Terrorist && team != Team::CounterTerrorist) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SelectTeam, but team is invalid: {}!\n", user->GetUserLogName(), team));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) has sent Packet_Host SelectTeam - userID: {}, team: {}\n", user->GetUserLogName(), userID, team));

	gameMatchUser->SetTeam((Team)team);
	RoomUser* ru = room->GetRoomUser(targetUser);
	if (ru) {
		ru->team = team;
	}
}

void Packet_HostManager::SendPacket_Host_HostEndSession(TCPConnection::pointer connection) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Host });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_HostType::HostEndSession);

	packet->Send();
}

void Packet_HostManager::parsePacket_Host_UserWeapons(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned long targetUserID = packet->ReadUInt32_LE();
	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) requested UserWeapons for userID {}\n", user->GetUserLogName(), targetUserID));

	SendPacket_Host_UserWeapons(connection, targetUserID);
}

void Packet_HostManager::SendPacket_Host_UserWeapons(TCPConnection::pointer connection, unsigned long targetUserID) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Host });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_HostType::UserWeapons);
	packet->WriteUInt32_LE(targetUserID);

	struct AllowedWeaponInfo {
		unsigned short itemID = 0;
		unsigned short count = 1;
		unsigned long enhanceBits = 0;
	};

	map<unsigned short, AllowedWeaponInfo> allowedWeapons;

	// 1. Standard CS weapons and equipment (1 to 31)
	for (unsigned short id = 1; id <= 31; ++id) {
		allowedWeapons[id] = { id, 1, 0 };
	}

	// 2. Default Buy Menu items from server config
	for (const auto& bm : serverConfig.defaultBuyMenus) {
		for (unsigned short id : bm.items) {
			if (id > 0) {
				allowedWeapons[id] = { id, 1, 0 };
			}
		}
	}

	// 3. Default Inventory items from server config
	for (unsigned short id : serverConfig.defaultInventory) {
		if (id > 0 && !IsTattooItem(id)) {
			allowedWeapons[id] = { id, 1, 0 };
		}
	}

	// 4. Target user's actual inventory items if online
	User* targetUser = userManager.GetUserByUserID(targetUserID);
	if (targetUser != NULL) {
		auto userInventory = targetUser->GetUserInventory();
		unsigned short equippedTattooID = 0;
		for (const auto& item : userInventory) {
			if (item.itemID > 0) {
				if (IsTattooItem(item.itemID)) {
					if (item.inUse) {
						equippedTattooID = item.itemID;
					}
				}
				else {
					unsigned short realCount = item.count > 0 ? item.count : 1;
					auto it = allowedWeapons.find(item.itemID);
					if (it != allowedWeapons.end()) {
						// For consumable tickets (258, 259, 260, 300, 334, etc.)
						// or if multiple stacks exist in inventory, sum count
						if (item.itemID == 258 || item.itemID == 259 || item.itemID == 260 || item.itemID == 300 || item.itemID == 334) {
							it->second.count += realCount;
						}
						else {
							it->second.count = max(it->second.count, realCount);
						}
						if (item.enhanceBits > 0) {
							it->second.enhanceBits = item.enhanceBits;
						}
					}
					else {
						allowedWeapons[item.itemID] = { item.itemID, realCount, item.enhanceBits };
					}
				}
			}
		}
		if (equippedTattooID > 0) {
			allowedWeapons[equippedTattooID] = { equippedTattooID, 1, 0 };
		}
	}

	packet->WriteUInt16_LE((unsigned short)allowedWeapons.size());
	for (const auto& [id, weapon] : allowedWeapons) {
		packet->WriteUInt16_LE(weapon.itemID);      // weaponID / itemID
		packet->WriteUInt16_LE(weapon.count);       // count (consumables match uses)
		packet->WriteUInt32_LE(weapon.enhanceBits); // weapon enhance value / flags
	}

	packet->Send();

	if (allowedWeapons.find(439) != allowedWeapons.end()) {
		serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] UserWeapons contains BigHeadEvent (item 439) for userID {}\n", targetUserID));
	}
	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] Sent Packet_Host UserWeapons for userID {} ({} weapons)\n", targetUserID, allowedWeapons.size()));
}

void Packet_HostManager::parsePacket_Host_ItemConsumed(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	unsigned long targetUserID = packet->ReadUInt32_LE();
	unsigned short itemID = packet->ReadUInt16_LE();
	unsigned short count = packet->ReadUInt16_LE();
	if (count == 0) {
		count = 1;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) consumed item ID {} (count: {}, targetUserID: {})\n",
		user->GetUserLogName(), itemID, count, targetUserID));

	unsigned short slotID = 0;
	bool deleted = false;
	bool consumed = databaseManager.ConsumeUserInventoryItem(user->GetUserID(), itemID, count, slotID, deleted);

	if (!consumed) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) attempted to consume item ID {}, but item was not found in inventory!\n",
			user->GetUserLogName(), itemID));
	}

	unsigned char resultFlag = consumed ? 1 : 0;
	unsigned long effectUserID = targetUserID > 0 ? targetUserID : user->GetUserID();

	// Broadcast Packet 68 Type 100 to all players in the room (including host and consumer)
	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room != NULL) {
		vector<User*> roomUsers = room->GetRoomUsers();
		for (auto& u : roomUsers) {
			if (u != NULL && u->GetConnection() != NULL && userManager.IsUserLoggedIn(u)) {
				SendPacket_Host_ItemConsumed(u->GetConnection(), effectUserID, itemID, resultFlag);
			}
		}
	}
	else {
		auto conn = user->GetConnection();
		if (conn != NULL) {
			SendPacket_Host_ItemConsumed(conn, effectUserID, itemID, resultFlag);
		}
	}
}

void Packet_HostManager::SendPacket_Host_ItemConsumed(TCPConnection::pointer connection, unsigned long targetUserID, unsigned short itemID, unsigned char result) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Host });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_HostType::ItemConsumed);
	packet->WriteUInt32_LE(targetUserID);
	packet->WriteUInt16_LE(itemID);
	packet->WriteUInt8(result);

	packet->Send();

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] Sent Packet_Host ItemConsumed - targetUserID: {}, itemID: {}, result: {}\n",
		targetUserID, itemID, (int)result));
}