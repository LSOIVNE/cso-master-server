#include "roommanager.h"
#include "packet_roommanager.h"
#include "packet_hostmanager.h"
#include "serverconsole.h"

Room::Room(unsigned short roomID, User* roomHostUser) : _roomID(roomID), _roomHostUser(roomHostUser) {
	if (_roomHostUser == NULL) {
		delete this;
		return;
	}

	_roomSettings.lowFlag = ROOMSETTINGS_LFLAG_ALL;
	_roomSettings.highFlag = ROOMSETTINGS_HFLAG_ALL;
	SetGameMatch(NULL);
}

char Room::AddRoomUser(User* user, unsigned char team) {
	if (user == NULL) {
		return -1;
	}

	auto it = find_if(_roomUsers.begin(), _roomUsers.end(), [user](const RoomUser& ru) { return ru.user == user; });
	if (it != _roomUsers.end()) {
		return 0;
	}

	if (team == 0) {
		if (user == _roomHostUser) {
			team = RoomTeam::Terrorist;
		}
		else {
			int trCount = 0, ctCount = 0;
			for (const auto& ru : _roomUsers) {
				if (ru.team == RoomTeam::Terrorist) trCount++;
				else if (ru.team == RoomTeam::CounterTerrorist) ctCount++;
			}
			team = (trCount <= ctCount) ? RoomTeam::Terrorist : RoomTeam::CounterTerrorist;
		}
	}

	unsigned char slot = 0;
	while (slot < 32) {
		bool used = false;
		for (const auto& ru : _roomUsers) {
			if (ru.slot == slot) {
				used = true;
				break;
			}
		}
		if (!used) break;
		slot++;
	}

	RoomUser ru;
	ru.user = user;
	ru.team = team;
	ru.status = 0;
	ru.slot = slot;
	_roomUsers.push_back(ru);

	user->SetUserStatus(UserStatus::InRoom);
	user->SetCurrentRoomID(_roomID);

	return 1;
}

void Room::RemoveRoomUser(User* user) {
	if (user == NULL) {
		return;
	}

	if (_gameMatch) {
		GameMatchUser* gameMatchUser = _gameMatch->GetGameMatchUserByUser(user);
		if (gameMatchUser) {
			_gameMatch->RemoveGameMatchUser(gameMatchUser);
		}
	}

	auto it = find_if(_roomUsers.begin(), _roomUsers.end(), [user](const RoomUser& ru) { return ru.user == user; });
	if (it != _roomUsers.end()) {
		_roomUsers.erase(it);
	}

	roomManager.SendUpdateRoomPacketToAll(this, ROOMLIST_FLAG_CURRENTPLAYERS);

	user->SetUserStatus(UserStatus::InLobby);
	user->SetCurrentRoomID(NULL);

	if (_roomUsers.empty()) {
		roomManager.RemoveRoom(this);
	}
	else {
		vector<User*> roomUsers = GetRoomUsers();

		roomUsers.erase(remove_if(roomUsers.begin(), roomUsers.end(), [](User* u) {
			return (u == NULL || u->GetConnection() == NULL);
		}), roomUsers.end());

		unsigned long userID = user->GetUserID();

		for (auto& u : roomUsers) {
			roomManager.SendRoomUserLeavePacket(u->GetConnection(), userID);
		}

		if (_roomHostUser == user) {
			if (_gameMatch) {
				vector<GameMatchUser*> gameMatchUsers = _gameMatch->GetGameMatchUsers();

				gameMatchUsers.erase(remove_if(gameMatchUsers.begin(), gameMatchUsers.end(), [](GameMatchUser* gameMatchUser) {
					return (gameMatchUser->GetUser() == NULL || gameMatchUser->GetUser()->GetConnection() == NULL);
				}), gameMatchUsers.end());

				if (gameMatchUsers.empty()) {
					delete _gameMatch;
					SetGameMatch(NULL);

					RoomSettings roomSettings;
					roomSettings.lowFlag = ROOMSETTINGS_LFLAG_ROOMSTATE;
					roomSettings.roomState = RoomState::Waiting;
					UpdateRoomSettings(roomSettings);

					roomManager.SendUpdateRoomPacketToAll(this, ROOMLIST_FLAG_ROOMSTATE);

					for (auto& u : roomUsers) {
						packet_RoomManager.SendPacket_Room_ReplyUpdateRoomSettings(u->GetConnection(), roomSettings);
						roomManager.SendUpdateRoomPacket(u->GetConnection(), this, ROOMLIST_FLAG_ROOMSTATE);
					}

					UpdateRoomHostUser(_roomUsers[0].user);
				}
				else {
					User* newRoomHostUser = gameMatchUsers[0]->GetUser();
					UpdateRoomHostUser(newRoomHostUser);

					gameMatchUsers.erase(remove_if(gameMatchUsers.begin(), gameMatchUsers.end(), [newRoomHostUser](GameMatchUser* gameMatchUser) {
						return (gameMatchUser->GetUser() == newRoomHostUser);
					}), gameMatchUsers.end());

					unsigned long newRoomHostUserID = newRoomHostUser->GetUserID();
					const vector<unsigned char>& saveData = _gameMatch->GetSaveData();

					packet_HostManager.SendPacket_Host_HostRestart(newRoomHostUser->GetConnection(), true, newRoomHostUserID, gameMatchUsers, saveData);

					for (auto& gameMatchUser : gameMatchUsers) {
						packet_HostManager.SendPacket_Host_HostRestart(gameMatchUser->GetUser()->GetConnection(), false, newRoomHostUserID);
						packet_HostManager.SendPacket_Host_HostJoin(gameMatchUser->GetUser()->GetConnection(), newRoomHostUserID);
					}

					if (saveData.empty()) {
						delete _gameMatch;
						SetGameMatch(NULL);
					}
				}
			}
			else {
				UpdateRoomHostUser(_roomUsers[0].user);
			}
		}
	}
}

void Room::UpdateRoomHostUser(User* user) {
	if (user == NULL) {
		return;
	}

	_roomHostUser = user;

	unsigned char modeSubFlag = CalculateRoomModeSubFlag(user, _roomSettings.gameModeID, _bigheadDisabled);

	vector<User*> roomUsers = GetRoomUsers();

	roomUsers.erase(remove_if(roomUsers.begin(), roomUsers.end(), [](User* u) {
		return (u == NULL || u->GetConnection() == NULL);
	}), roomUsers.end());

	if (_roomSettings.unkh1000 != modeSubFlag) {
		_roomSettings.unkh1000 = modeSubFlag;
		_roomSettings.highFlag |= ROOMSETTINGS_HFLAG_UNK1000;
		RoomSettings delta;
		delta.highFlag = ROOMSETTINGS_HFLAG_UNK1000;
		delta.unkh1000 = modeSubFlag;
		for (auto& u : roomUsers) {
			packet_RoomManager.SendPacket_Room_ReplyUpdateRoomSettings(u->GetConnection(), delta);
		}
		roomManager.SendUpdateRoomPacketToAll(this, ROOMLIST_FLAG_UNK2000);
	}

	for (auto& u : roomUsers) {
		roomManager.SendUpdateRoomPacket(u->GetConnection(), this, ROOMLIST_FLAG_ROOMHOST);
	}
}

void Room::UpdateRoomSettings(const RoomSettings& roomSettings) {
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_ROOMNAME) {
		_roomSettings.roomName = roomSettings.roomName;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK2) {
		_roomSettings.unk2 = roomSettings.unk2;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK4) {
		_roomSettings.unk4_1 = roomSettings.unk4_1;
		_roomSettings.unk4_2 = roomSettings.unk4_2;
		_roomSettings.unk4_3 = roomSettings.unk4_3;
		_roomSettings.unk4_4 = roomSettings.unk4_4;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_PASSWORD) {
		_roomSettings.password = roomSettings.password;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_LEVELLIMIT) {
		_roomSettings.levelLimit = roomSettings.levelLimit;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK20) {
		_roomSettings.unk20 = roomSettings.unk20;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_GAMEMODEID) {
		_roomSettings.gameModeID = roomSettings.gameModeID;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_MAPID) {
		_roomSettings.mapID = roomSettings.mapID;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_MAXPLAYERS) {
		_roomSettings.maxPlayers = roomSettings.maxPlayers;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_WINLIMIT) {
		_roomSettings.winLimit = roomSettings.winLimit;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_KILLLIMIT) {
		_roomSettings.killLimit = roomSettings.killLimit;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_TIMELIMIT) {
		_roomSettings.timeLimit = roomSettings.timeLimit;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_ROUNDTIME) {
		_roomSettings.roundTime = roomSettings.roundTime;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_WEAPONLIMIT) {
		_roomSettings.weaponLimit = roomSettings.weaponLimit;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_HOSTAGEPENALTY) {
		_roomSettings.hostagePenalty = roomSettings.hostagePenalty;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FREEZETIME) {
		_roomSettings.freezeTime = roomSettings.freezeTime;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_BUYTIME) {
		_roomSettings.buyTime = roomSettings.buyTime;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_NICKNAMEDISPLAY) {
		_roomSettings.nickNameDisplay = roomSettings.nickNameDisplay;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK40000) {
		_roomSettings.unk40000 = roomSettings.unk40000;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK80000) {
		_roomSettings.unk80000 = roomSettings.unk80000;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FRIENDLYFIRE) {
		_roomSettings.friendlyFire = roomSettings.friendlyFire;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FLASHLIGHT) {
		_roomSettings.flashLight = roomSettings.flashLight;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_FOOTSTEPS) {
		_roomSettings.footSteps = roomSettings.footSteps;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK800000) {
		_roomSettings.unk800000 = roomSettings.unk800000;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK1000000) {
		_roomSettings.unk1000000 = roomSettings.unk1000000;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK2000000) {
		_roomSettings.unk2000000 = roomSettings.unk2000000;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK4000000) {
		_roomSettings.unk4000000 = roomSettings.unk4000000;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK8000000) {
		_roomSettings.unk8000000 = roomSettings.unk8000000;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_DEATHCAMERATYPE) {
		_roomSettings.deathCameraType = roomSettings.deathCameraType;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_VOICECHAT) {
		_roomSettings.voiceChat = roomSettings.voiceChat;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_ROOMSTATE) {
		_roomSettings.roomState = roomSettings.roomState;
	}
	if (roomSettings.lowFlag & ROOMSETTINGS_LFLAG_UNK80000000) {
		_roomSettings.unk80000000_1 = roomSettings.unk80000000_1;
		for (unsigned char i = 0; i < 2; i++) {
			_roomSettings.unk80000000_2[i].unk8000000_vec_1 = roomSettings.unk80000000_2[i].unk8000000_vec_1;
			_roomSettings.unk80000000_2[i].unk8000000_vec_2 = roomSettings.unk80000000_2[i].unk8000000_vec_2;
			_roomSettings.unk80000000_2[i].unk8000000_vec_3 = roomSettings.unk80000000_2[i].unk8000000_vec_3;
			_roomSettings.unk80000000_2[i].unk8000000_vec_4 = roomSettings.unk80000000_2[i].unk8000000_vec_4;
			_roomSettings.unk80000000_2[i].unk8000000_vec_5 = roomSettings.unk80000000_2[i].unk8000000_vec_5;
			_roomSettings.unk80000000_2[i].unk8000000_vec_6 = roomSettings.unk80000000_2[i].unk8000000_vec_6;
			_roomSettings.unk80000000_2[i].unk8000000_vec_7 = roomSettings.unk80000000_2[i].unk8000000_vec_7;
			_roomSettings.unk80000000_2[i].unk8000000_vec_8 = roomSettings.unk80000000_2[i].unk8000000_vec_8;
			_roomSettings.unk80000000_2[i].unk8000000_vec_9 = roomSettings.unk80000000_2[i].unk8000000_vec_9;
		}
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK1) {
		_roomSettings.unkh1_1 = roomSettings.unkh1_1;
		_roomSettings.unkh1_2 = roomSettings.unkh1_2;
		_roomSettings.unkh1_3 = roomSettings.unkh1_3;
		_roomSettings.unkh1_4 = roomSettings.unkh1_4;
		_roomSettings.unkh1_5 = roomSettings.unkh1_5;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK2) {
		_roomSettings.unkh2 = roomSettings.unkh2;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK4) {
		_roomSettings.unkh4_1 = roomSettings.unkh4_1;
		_roomSettings.unkh4_2 = roomSettings.unkh4_2;
		_roomSettings.unkh4_3 = roomSettings.unkh4_3;
		_roomSettings.unkh4_4 = roomSettings.unkh4_4;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK8) {
		_roomSettings.unkh8 = roomSettings.unkh8;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK10) {
		_roomSettings.unkh10 = roomSettings.unkh10;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK20) {
		_roomSettings.unk20 = roomSettings.unk20;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK40) {
		_roomSettings.unkh40 = roomSettings.unkh40;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK80) {
		_roomSettings.unkh80 = roomSettings.unkh80;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK100) {
		_roomSettings.unkh100 = roomSettings.unkh100;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK200) {
		_roomSettings.unkh200 = roomSettings.unkh200;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK400) {
		_roomSettings.unkh400 = roomSettings.unkh400;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK800) {
		_roomSettings.unkh800 = roomSettings.unkh800;
	}
	if (roomSettings.highFlag & ROOMSETTINGS_HFLAG_UNK1000) {
		_roomSettings.unkh1000 = roomSettings.unkh1000;
	}
	_roomSettings.highFlag |= (roomSettings.highFlag & 0x1FFF);
}

RoomUser* Room::GetRoomUser(User* user) {
	if (!user) return nullptr;
	for (auto& ru : _roomUsers) {
		if (ru.user == user) return &ru;
	}
	return nullptr;
}

RoomUser* Room::GetRoomUserByUserID(unsigned long userID) {
	for (auto& ru : _roomUsers) {
		if (ru.user && ru.user->GetUserID() == userID) return &ru;
	}
	return nullptr;
}

bool Room::SetUserTeam(User* user, unsigned char team) {
	RoomUser* ru = GetRoomUser(user);
	if (!ru) return false;
	ru->team = team;
	return true;
}

bool Room::SetUserReady(User* user, unsigned char ready) {
	RoomUser* ru = GetRoomUser(user);
	if (!ru) return false;
	ru->status = ready;
	return true;
}