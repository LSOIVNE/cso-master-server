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
#include "scenariorewardmanager.h"
#include "killdropmanager.h"
#include "itemdefinitionmanager.h"
#include "battlebonusmanager.h"
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

			if (itemID > 0) {
				const ItemDefinition* itemDef = itemDefinitionManager.GetItem(itemID);
				bool isClass = (itemDef && itemDef->IsClass()) || battleBonusManager.HasClassBonus(itemID);
				if (isClass) {
					Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
					if (room) {
						GameMatch* gameMatch = room->GetGameMatch();
						if (gameMatch) {
							User* targetUser = userManager.GetUserByUserID(userID);
							if (targetUser) {
								GameMatchUser* gmu = gameMatch->GetGameMatchUserByUser(targetUser);
								if (gmu == NULL) {
									gmu = new GameMatchUser(targetUser);
									RoomUser* ru = room->GetRoomUser(targetUser);
									if (ru) {
										gmu->SetMapSequence(ru->mapSequence);
									}
									gameMatch->AddGameMatchUser(gmu);
								}
								gmu->SetClassItemID(itemID);
								std::string defName = itemDef ? itemDef->name : std::string();
								serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] Set in-game class for User ({}) to itemID: {} ({})\n",
									targetUser->GetUserLogName(), itemID, defName));
							}
						}
					}
				}
			}
			break;
		}
		case Packet_HostType::FlyerFlock: {
			string name = packet->ReadString();
			serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) requested FlyerFlock - name: {}\n", user->GetUserLogName(), name));
			SendPacket_Host_FlyerFlock(connection, name);
			break;
		}
		case Packet_HostType::GameEvent: {
			parsePacket_Host_GameEvent(user, packet);
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
				gmu->SetMapSequence(ru->mapSequence);
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
	unsigned short attackerWeapon = packet->ReadUInt16_LE();
	unsigned char attackerTeam = packet->ReadUInt8();
	unsigned long victimUserID = packet->ReadUInt32_LE();
	unsigned char victimTeam = packet->ReadUInt8();
	unsigned short killFlags = packet->ReadUInt16_LE();

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

	bool isHeadshot = (killFlags & 0x01) != 0;
	unsigned char realWeaponID = (unsigned char)(attackerWeapon & 0xFF);
	bool isKnife = (killFlags & 0x02) != 0 || realWeaponID == 29;
	bool isGrenade = (killFlags & 0x10) != 0 || realWeaponID == 4;
	bool isMotherZombieKill = (killFlags & 0x04) != 0;

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] User ({}) PlayerKilled - attID: {} (team: {}), weapon: {} (HS: {}, knife: {}, nade: {}, flags: 0x{:04X}), vicID: {} (team: {}), dist: {}\n",
		user->GetUserLogName(), attackerUserID, (int)attackerTeam, (int)realWeaponID, isHeadshot ? 1 : 0, isKnife ? 1 : 0, isGrenade ? 1 : 0, killFlags, victimUserID, (int)victimTeam, distance));

	User* attackerUser = attackerUserID ? userManager.GetUserByUserID(attackerUserID) : NULL;
	User* victimUser = victimUserID ? userManager.GetUserByUserID(victimUserID) : NULL;

	GameMatchUser* attackerGmu = attackerUser ? gameMatch->GetGameMatchUserByUser(attackerUser) : NULL;
	GameMatchUser* victimGmu = victimUser ? gameMatch->GetGameMatchUserByUser(victimUser) : NULL;

	unsigned char gameModeID = room ? room->GetRoomSettings().gameModeID : 0;
	bool isZombieMode = (gameModeID == 8 || gameModeID == 9 || gameModeID == 14 || gameModeID == 20 || gameModeID == 24);

	if (attackerGmu) {
		if (isZombieMode) {
			unsigned char aTeam = attackerTeam ? attackerTeam : (unsigned char)attackerGmu->GetTeam();
			unsigned char vTeam = victimTeam ? victimTeam : (victimGmu ? (unsigned char)victimGmu->GetTeam() : 1);
			if (aTeam == (unsigned char)Team::CounterTerrorist && vTeam == (unsigned char)Team::Terrorist) {
				attackerGmu->AddZombieKill(1);
				if (isMotherZombieKill) {
					attackerGmu->AddZombieMotherKill(1);
				}
				if (isHeadshot) attackerGmu->AddHeadshots(1);
				if (isGrenade) attackerGmu->AddGrenadeKills(1);
				if (isKnife) attackerGmu->AddKnifeKills(1);
			}
			else if (aTeam == (unsigned char)Team::Terrorist && vTeam == (unsigned char)Team::CounterTerrorist) {
				attackerGmu->AddHumanKill(1);
			}
		}
		else if (gameModeID == 10) { // Phantom / Hidden Mode (魔幻空間 / 幽靈模式)
			unsigned char aTeam = attackerTeam ? attackerTeam : (unsigned char)attackerGmu->GetTeam();
			unsigned char vTeam = victimTeam ? victimTeam : (victimGmu ? (unsigned char)victimGmu->GetTeam() : 0);
			if (aTeam == (unsigned char)Team::CounterTerrorist && vTeam == (unsigned char)Team::Terrorist) {
				attackerGmu->AddHdHiddenKill(1);
			}
			else if (aTeam == (unsigned char)Team::Terrorist && vTeam == (unsigned char)Team::CounterTerrorist) {
				attackerGmu->AddHdHumanKill(1);
			}
			if (isHeadshot) attackerGmu->AddHeadshots(1);
			if (isGrenade) attackerGmu->AddGrenadeKills(1);
			if (isKnife) attackerGmu->AddKnifeKills(1);
		}
		else {
			if (attackerUser != victimUser) {
				unsigned char aTeam = attackerTeam ? attackerTeam : (unsigned char)attackerGmu->GetTeam();
				unsigned char vTeam = victimTeam ? victimTeam : (victimGmu ? (unsigned char)victimGmu->GetTeam() : 0);
				if (vTeam == 0 || aTeam != vTeam || gameModeID == 2) {
					if (isHeadshot) attackerGmu->AddHeadshots(1);
					if (isGrenade) attackerGmu->AddGrenadeKills(1);
					if (isKnife) attackerGmu->AddKnifeKills(1);
				}
			}
		}

		if (gameModeID == 21) { // Bazooka (轟炸擂台) combo tracking
			attackerGmu->AddCombo(1);
		}
	}

	if (victimGmu) {
		if (isZombieMode) {
			unsigned char aTeam = attackerTeam ? attackerTeam : (attackerGmu ? (unsigned char)attackerGmu->GetTeam() : 2);
			if (aTeam == (unsigned char)Team::Terrorist) {
				// Human victim was infected by zombie, victim becomes Zombie
				victimGmu->SetTeam(Team::Terrorist);
			}
		}
		if (gameModeID == 21) { // Bazooka (轟炸擂台) reset combo on death
			victimGmu->ResetCombo();
		}
		if (gameModeID == 26) { // Shelter Survival (夜襲生存戰) reset consecutive survival days on death
			victimGmu->SetAccumSurviveDays(0);
		}
	}

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

	if (room->GetGameMatch() == NULL) {
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
	float matchDuration = packet->ReadBytes<float>();

	ProcessEndGame(room, winTeam, terWins, ctWins, matchDuration);
}

void Packet_HostManager::ProcessEndGame(Room* room, unsigned char winTeam, unsigned short terWins, unsigned char ctWins, float matchDuration) {
	if (room == NULL) return;
	GameMatch* gameMatch = room->GetGameMatch();
	if (gameMatch == NULL) return;

	gameMatch->SetMatchDuration(matchDuration);
	gameMatch->SetTerWins(terWins);
	gameMatch->SetCtWins(ctWins);
	gameMatch->SetWinTeam(winTeam);

	unsigned char gameModeID = room ? room->GetRoomSettings().gameModeID : 0;

	if (IsScenarioMode(gameModeID)) {
		winTeam = 0;
	}
	else {
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
	}

	string hostName = room->GetRoomHostUser() ? room->GetRoomHostUser()->GetUserLogName() : "Unknown";
	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] ProcessEndGame in room (Host: {}) - winTeam: {}, terWins: {}, ctWins: {}, matchDuration: {:.2f}s\n",
		hostName, winTeam, terWins, ctWins, matchDuration));

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
		else if (IsScenarioMode(gameModeID)) {
			isWin = (terWins > ctWins);
		}
		else {
			isWin = (winTeam != 0 && team == winTeam);
		}

		// Mode-specific settlement stats calculations
		if (gameModeID == 26) { // 夜襲生存戰 (Shelter Survival)
			unsigned char matchDays = gmu->GetSurviveDays();
			unsigned char matchRescuee = gmu->GetRescuee();
			unsigned char consecutiveDays = gmu->GetAccumSurviveDays();
			bool playerSurvived = (consecutiveDays > 0 || gmu->GetDeaths() == 0 || isWin || matchRescuee > 0);

			unsigned int totalDays = 0, totalRescuee = 0;
			databaseManager.UpdateUserShelterStats(
				u->GetUserID(),
				matchDays,
				matchRescuee,
				totalDays,
				totalRescuee
			);

			if (gmu->GetZshPoint() == 0) {
				unsigned short pts = (unsigned short)(kills * 15 + matchDays * 100);
				gmu->SetZshPoint(pts);
			}

			// Scoreboard row "Rescuee" column:
			// Non-zero = "生存" (#CSO_zsh_Rescuee), 0 = "死亡" (#CSO_zsh_Dead)
			gmu->SetRescuee(playerSurvived ? 1 : 0);

			// Summary box fields:
			// "CSO_zsh_SurviveDays" (生存天數): continuously accumulated across matches in DB
			gmu->SetSurviveDays((unsigned char)min(totalDays, 255u));

			// "CSO_zsh_MaximumSurviveDays" (最高生存天數): current match total days
			gmu->SetMaxSurviveDays(matchDays);

			// "CSO_zsh_AccumulateSurviveDays" (連續生存天數): consecutive days survived without dying
			gmu->SetAccumSurviveDays(consecutiveDays);

			// "CSO_zsh_AccumulateRescuee" (使用無線電次數): total accumulated radio uses
			gmu->SetAccumRescuee((unsigned char)min(totalRescuee, 255u));
		}
		else if (gameModeID == 25) { // 即刻救援 (Battle Run)
			if (gmu->GetBrPoint() == 0) {
				unsigned int pts = (unsigned int)(kills * 100 + gmu->GetLoAssist() * 50);
				gmu->SetBrPoint(pts);
			}
		}

		BaseRewardResult baseRewards = battleBonusManager.CalculateBaseRewards(gameModeID, kills, isWin);
		unsigned short earnedExp = baseRewards.exp;
		unsigned short earnedPoints = baseRewards.points;

		// Check scenario custom clear rewards (Gamemode 15 & 17)
		const ScenarioMapReward* scenReward = nullptr;
		if (IsScenarioMode(gameModeID) && isWin) {
			unsigned char mapID = room ? room->GetRoomSettings().mapID : 0;
			scenReward = scenarioRewardManager.GetRewardForMap(mapID, gameModeID);
			if (scenReward) {
				earnedExp += scenReward->bonusExp;
				earnedPoints += scenReward->bonusPoints;
			}
		}

		gmu->SetExp(earnedExp);
		gmu->SetPoints(earnedPoints);
		gmu->SetRank((unsigned char)(i + 1));

		// Calculate BattleBonus (Pass items, Class, Decorations, PCBang, Event)
		BattleBonusResult bonusResult = battleBonusManager.CalculateBonus(u, gmu, gameModeID, earnedExp, earnedPoints);
		gmu->SetBonusExp(bonusResult.bonusExp);
		gmu->SetBonusPoints(bonusResult.bonusPoints);
		gmu->SetBonusPercentages(bonusResult.ToPacketArray());

		unsigned short totalEarnedExp = earnedExp + bonusResult.bonusExp;
		unsigned short totalEarnedPoints = earnedPoints + bonusResult.bonusPoints;

		// Fetch current character stats from database
		UserCharacterResult ucr = u->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		unsigned char oldLevel = ucr.userCharacter.level ? ucr.userCharacter.level : 1;
		unsigned long long newExp = ucr.userCharacter.exp + totalEarnedExp;
		unsigned long long newPoints = ucr.userCharacter.points + totalEarnedPoints;
		unsigned char newLevel = max(oldLevel, User::CalculateLevel(newExp));
		unsigned char levelUp = (newLevel > oldLevel) ? 1 : 0;

		gmu->SetLevelUp(levelUp);

		// Calculate zombie kills and survivals to accumulate into user personal stats (only Mode 8, 9, 14)
		unsigned int addedZombieKills = 0;
		unsigned int addedSurvivals = 0;
		if (gameModeID == 8 || gameModeID == 9 || gameModeID == 14) {
			addedZombieKills = gmu->GetZombieKill();
			addedSurvivals = gmu->GetSurviveCount();
		}

		// Save battle result to database (recordStats only for competitive modes)
		databaseManager.SaveUserBattleResult(u->GetUserID(), totalEarnedExp, totalEarnedPoints, kills, deaths, isWin, newLevel, recordStats, addedZombieKills, addedSurvivals);

		// Quest progression hook
		questManager.OnMatchEnd(u, isWin, kills, deaths, (unsigned int)matchDuration);

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
		packet_RoomManager.SendPacket_Room_GameResult(u->GetConnection(), room, gameMatch, winTeam, terWins, ctWins);

		// Scenario custom clear rewards (grant items, sync inventory, and send ItemAwardNotice dialog)
		if (IsScenarioMode(gameModeID) && (terWins > ctWins)) {
			unsigned char mapID = room ? room->GetRoomSettings().mapID : 0;
			const ScenarioMapReward* scenReward = scenarioRewardManager.GetRewardForMap(mapID, gameModeID);
			if (scenReward) {
				string mapNameForLog = questManager.GetMapFileName(mapID);
				scenarioRewardManager.GrantScenarioReward(u, scenReward, mapNameForLog);
			}
		}

		// Scenario 3: Pop up expired items notice on the settlement screen
		userManager.CheckAndProcessUserExpiredItems(u, true);
	}

	const auto& seqMaps = room->GetRoomSettings().unkh200;
	if (!seqMaps.empty()) {
		// Increment consecutive map sequence for all room users who completed this match
		for (auto& gameMatchUser : gameMatchUsers) {
			User* u = gameMatchUser->GetUser();
			if (u && room) {
				RoomUser* ru = room->GetRoomUser(u);
				if (ru) {
					ru->mapSequence++;
					serverConsole.Print(PrefixType::Info, format("[ Room ] User ({}) consecutive map sequence incremented to {}\n", u->GetUserLogName(), ru->mapSequence));
				}
			}
		}
	}

	delete gameMatch;
	room->SetGameMatch(NULL);

	RoomSettings roomSettings;
	roomSettings.lowFlag = ROOMSETTINGS_LFLAG_ROOMSTATE;
	roomSettings.roomState = RoomState::Waiting;

	unsigned char roomListFlag = ROOMLIST_FLAG_ROOMSTATE;

	if (!seqMaps.empty()) {
		size_t currentIndex = room->GetMapSequenceIndex();
		if (currentIndex == 0) {
			currentIndex = 1;
		}

		if (currentIndex < seqMaps.size()) {
			// Advance to next map in sequence
			size_t nextIndex = currentIndex + 1;
			room->SetMapSequenceIndex(nextIndex);

			unsigned char nextMapID = seqMaps[nextIndex - 1].second;
			roomSettings.mapID = nextMapID;
			roomSettings.unkh400 = (unsigned char)nextIndex;
			roomSettings.lowFlag |= ROOMSETTINGS_LFLAG_MAPID;
			roomSettings.highFlag |= ROOMSETTINGS_HFLAG_UNK400;
			roomListFlag |= ROOMLIST_FLAG_MAPID;

			serverConsole.Print(PrefixType::Info, format("[ Room ] Consecutive map advancing to map {}/{} (mapID: {})\n",
				nextIndex, seqMaps.size(), nextMapID));
		}
		else {
			// Sequence complete, reset to map 1
			room->SetMapSequenceIndex(1);

			unsigned char firstMapID = seqMaps[0].second;
			roomSettings.mapID = firstMapID;
			roomSettings.unkh400 = 1;
			roomSettings.lowFlag |= ROOMSETTINGS_LFLAG_MAPID;
			roomSettings.highFlag |= ROOMSETTINGS_HFLAG_UNK400;
			roomListFlag |= ROOMLIST_FLAG_MAPID;

			for (auto& ru : room->GetRoomUsersData()) {
				ru.mapSequence = 1;
			}

			serverConsole.Print(PrefixType::Info, format("[ Room ] Consecutive map sequence completed ({} maps). Reset to map 1 (mapID: {})\n",
				seqMaps.size(), firstMapID));
		}
	}

	room->UpdateRoomSettings(roomSettings);

	roomManager.SendUpdateRoomPacketToAll(room, roomListFlag);

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
	User* targetUser = NULL;
	if (userID) {
		targetUser = userManager.GetUserByUserID(userID);
		if (targetUser) {
			gameMatchUser = gameMatch->GetGameMatchUserByUser(targetUser);
		}
	}

	signed char point = packet->ReadUInt8();
	signed short frags = packet->ReadUInt16_LE();

	if (gameMatchUser && frags >= 0) {
		unsigned short oldKills = gameMatchUser->GetKills();
		gameMatchUser->SetKills((unsigned short)frags);
		if ((unsigned short)frags > oldKills && targetUser != NULL) {
			unsigned short killCount = (unsigned short)frags - oldKills;
			killDropManager.OnPlayerKill(targetUser, room, killCount);
		}
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
		if (room && room->GetRoomSettings().gameModeID == 26) {
			gameMatchUser->SetAccumSurviveDays(0);
		}
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

	if (room && IsScenarioMode(room->GetRoomSettings().gameModeID)) {
		winTeam = 0;
	}

	gameMatch->SetWinTeam(winTeam);
	gameMatch->SetTerWins(terWins);
	gameMatch->SetCtWins(ctWins);

	unsigned char gameModeID = room ? room->GetRoomSettings().gameModeID : 0;
	bool isZombieMode = (gameModeID == 8 || gameModeID == 9 || gameModeID == 14 || gameModeID == 20 || gameModeID == 24);

	if (isZombieMode) {
		if (winTeam == (unsigned char)Team::CounterTerrorist) {
			for (auto& gmu : gameMatch->GetGameMatchUsers()) {
				if (gmu && gmu->GetTeam() == Team::CounterTerrorist) {
					gmu->AddSurviveCount(1);
				}
			}
		}
	}

	for (auto& gmu : gameMatch->GetGameMatchUsers()) {
		if (gmu && gmu->GetUser()) {
			if ((unsigned char)gmu->GetTeam() == winTeam) {
				questManager.OnRoundWin(gmu->GetUser());
			}
		}
	}

	if (isZombieMode) {
		// Reset players back to CounterTerrorist (Human) for the start of the next round
		for (auto& gmu : gameMatch->GetGameMatchUsers()) {
			if (gmu) {
				gmu->SetTeam(Team::CounterTerrorist);
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
		RoomUser* ru = room->GetRoomUser(targetUser);
		if (ru) {
			gameMatchUser->SetMapSequence(ru->mapSequence);
		}
		gameMatch->AddGameMatchUser(gameMatchUser);
	}

	unsigned char team = packet->ReadUInt8();

	if (IsScenarioMode(room->GetRoomSettings().gameModeID)) {
		team = (unsigned char)Team::CounterTerrorist;
	}
	else if (team != Team::Terrorist && team != Team::CounterTerrorist) {
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

void Packet_HostManager::parsePacket_Host_GameEvent(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	if (packet->GetBuffer().size() < (size_t)packet->GetReadOffset() + 5) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_HostManager ] User ({}) sent GameEvent with insufficient data!\n", user->GetUserLogName()));
		return;
	}

	unsigned char eventType = packet->ReadUInt8();
	unsigned long targetUserID = packet->ReadUInt32_LE();

	unsigned long value = 0;
	size_t remaining = packet->GetBuffer().size() - packet->GetReadOffset();
	if (remaining >= 4) {
		value = packet->ReadUInt32_LE();
	}
	else if (remaining >= 2) {
		value = packet->ReadUInt16_LE();
	}
	else if (remaining >= 1) {
		value = packet->ReadUInt8();
	}

	User* targetUser = (targetUserID != 0) ? userManager.GetUserByUserID(targetUserID) : NULL;

	serverConsole.Print(PrefixType::Info, format("[ Packet_HostManager ] GameEvent: type {}, host: {}, targetUser: {} (ID: {}), value: {}\n",
		(int)eventType, user->GetUserLogName(), targetUser ? targetUser->GetUserLogName() : "None/Bot", targetUserID, value));

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	GameMatch* gameMatch = room ? room->GetGameMatch() : NULL;
	if (gameMatch != NULL) {
		GameMatchUser* gmu = targetUser ? gameMatch->GetGameMatchUserByUser(targetUser) : NULL;
		if (gmu == NULL && user != NULL) {
			gmu = gameMatch->GetGameMatchUserByUser(user);
		}
		if (gmu != NULL) {
			switch (eventType) {
			case 0: // C4 Exploded / Target Destroyed
				if (gmu->GetTeam() == Team::Terrorist) {
					gmu->AddBombSuccess(1);
				}
				break;
			case 1: // C4 Defused
				if (gmu->GetTeam() == Team::CounterTerrorist) {
					gmu->AddBombDeactivate(1);
				}
				break;
			case 2: // Hostage Rescued / Shelter Rescue
				if (gmu->GetTeam() == Team::CounterTerrorist) {
					gmu->AddHostageRescue(value > 0 ? (unsigned short)value : 1);
				}
				if (room && room->GetRoomSettings().gameModeID == 26) {
					gmu->AddRescuee(value > 0 ? (unsigned char)value : 1);
				}
				break;
			case 3: // Assist event
				gmu->AddLoAssist(1);
				gmu->AddBkAssist(1);
				break;
			case 6: // VIP Escaped
				if (gmu->GetTeam() == Team::CounterTerrorist) {
					gmu->AddVipEscape(1);
				}
				break;
			case 7: // VIP Killed
				if (gmu->GetTeam() == Team::Terrorist) {
					gmu->AddVipKill(1);
				}
				break;
			case 35: // Shelter points (Mode 26)
				gmu->AddZshPoint((unsigned short)value);
				break;
			case 64: // Shelter Survival Event (Mode 26)
				if (room && room->GetRoomSettings().gameModeID == 26) {
					if (value == 1 || value == 0) {
						gmu->AddSurviveDays(1);
						gmu->AddAccumSurviveDays(1);
					}
					else if (value == 5) {
						// Shelter radio / broadcast upgraded or activated
						gmu->AddRescuee(1);
					}
				}
				break;
			default:
				break;
			}
		}
	}

	if (eventType == 34) { // 0x22: Scenario drop box / point pickup
		User* boxUser = targetUser ? targetUser : user;
		scenarioRewardManager.HandleBoxPickup(boxUser, (unsigned int)value);
	}
}