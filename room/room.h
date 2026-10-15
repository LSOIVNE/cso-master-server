#pragma once
#include "user.h"
#include "gamematch.h"

constexpr auto ROOMSETTINGS_LFLAG_ROOMNAME =		0x1;
constexpr auto ROOMSETTINGS_LFLAG_UNK2 =			0x2;
constexpr auto ROOMSETTINGS_LFLAG_UNK4 =			0x4;
constexpr auto ROOMSETTINGS_LFLAG_PASSWORD =		0x8;
constexpr auto ROOMSETTINGS_LFLAG_LEVELLIMIT =		0x10;
constexpr auto ROOMSETTINGS_LFLAG_UNK20 =			0x20;
constexpr auto ROOMSETTINGS_LFLAG_GAMEMODEID =		0x40;
constexpr auto ROOMSETTINGS_LFLAG_MAPID =			0x80;
constexpr auto ROOMSETTINGS_LFLAG_MAXPLAYERS =		0x100;
constexpr auto ROOMSETTINGS_LFLAG_WINLIMIT =		0x200;
constexpr auto ROOMSETTINGS_LFLAG_KILLLIMIT =		0x400;
constexpr auto ROOMSETTINGS_LFLAG_TIMELIMIT =		0x800;
constexpr auto ROOMSETTINGS_LFLAG_ROUNDTIME =		0x1000;
constexpr auto ROOMSETTINGS_LFLAG_WEAPONLIMIT =		0x2000;
constexpr auto ROOMSETTINGS_LFLAG_HOSTAGEPENALTY =	0x4000;
constexpr auto ROOMSETTINGS_LFLAG_FREEZETIME =		0x8000;
constexpr auto ROOMSETTINGS_LFLAG_BUYTIME =			0x10000;
constexpr auto ROOMSETTINGS_LFLAG_NICKNAMEDISPLAY = 0x20000;
constexpr auto ROOMSETTINGS_LFLAG_UNK40000 =		0x40000;
constexpr auto ROOMSETTINGS_LFLAG_UNK80000 =		0x80000;
constexpr auto ROOMSETTINGS_LFLAG_FRIENDLYFIRE =	0x100000;
constexpr auto ROOMSETTINGS_LFLAG_FLASHLIGHT =		0x200000;
constexpr auto ROOMSETTINGS_LFLAG_FOOTSTEPS =		0x400000;
constexpr auto ROOMSETTINGS_LFLAG_UNK800000 =		0x800000;
constexpr auto ROOMSETTINGS_LFLAG_UNK1000000 =		0x1000000;
constexpr auto ROOMSETTINGS_LFLAG_UNK2000000 =		0x2000000;
constexpr auto ROOMSETTINGS_LFLAG_UNK4000000 =		0x4000000;
constexpr auto ROOMSETTINGS_LFLAG_UNK8000000 =		0x8000000;
constexpr auto ROOMSETTINGS_LFLAG_DEATHCAMERATYPE = 0x10000000;
constexpr auto ROOMSETTINGS_LFLAG_VOICECHAT =		0x20000000;
constexpr auto ROOMSETTINGS_LFLAG_ROOMSTATE =		0x40000000;
constexpr auto ROOMSETTINGS_LFLAG_UNK80000000 =		0x80000000;
constexpr auto ROOMSETTINGS_LFLAG_ALL =				0xFFFFFFFF;

constexpr auto ROOMSETTINGS_HFLAG_UNK1 =			0x1;
constexpr auto ROOMSETTINGS_HFLAG_UNK2 =			0x2;
constexpr auto ROOMSETTINGS_HFLAG_UNK4 =			0x4;
constexpr auto ROOMSETTINGS_HFLAG_UNK8 =			0x8;
constexpr auto ROOMSETTINGS_HFLAG_UNK10 =			0x10;
constexpr auto ROOMSETTINGS_HFLAG_UNK20 =			0x20;
constexpr auto ROOMSETTINGS_HFLAG_UNK40 =			0x40;
constexpr auto ROOMSETTINGS_HFLAG_UNK80 =			0x80;
constexpr auto ROOMSETTINGS_HFLAG_UNK100 =			0x100;
constexpr auto ROOMSETTINGS_HFLAG_UNK200 =			0x200;
constexpr auto ROOMSETTINGS_HFLAG_UNK400 =			0x400;
constexpr auto ROOMSETTINGS_HFLAG_UNK800 =			0x800;
constexpr auto ROOMSETTINGS_HFLAG_UNK1000 =			0x1000;
constexpr auto ROOMSETTINGS_HFLAG_ALL =				ROOMSETTINGS_HFLAG_UNK1000;

enum class RoomState {
	Waiting = 0,
	Playing = 2
};

inline bool IsBigHeadCompatibleMode(unsigned char gameModeID) noexcept {
	// 官方 2014 SGP sub_591050 逆向證實：大頭製造器僅支援模式 15 (災厄之章) 與模式 17 (戰神之章)
	return (gameModeID == 15 || gameModeID == 17);
}

inline bool IsScenarioMode(unsigned char gameModeID) noexcept {
	return (gameModeID == 15 || gameModeID == 17);
}

inline unsigned char CalculateRoomModeSubFlag(const User* hostUser, unsigned char gameModeID, bool bigheadDisabled = false) noexcept {
	if (!hostUser) return 0;
	if (IsBigHeadCompatibleMode(gameModeID)) {
		if (!bigheadDisabled && hostUser->HasItem(439)) {
			return 1;
		}
	}
	else if (gameModeID == 0) {
		if (hostUser->HasItem(537)) {
			return 2;
		}
	}
	return 0;
}

struct unk80000000_vec {
	unsigned long unk8000000_vec_1 = 0;
	unsigned long unk8000000_vec_2 = 0;
	unsigned char unk8000000_vec_3 = 0;
	unsigned char unk8000000_vec_4 = 0;
	unsigned char unk8000000_vec_5 = 0;
	unsigned char unk8000000_vec_6 = 0;
	unsigned short unk8000000_vec_7 = 0;
	unsigned char unk8000000_vec_8 = 0;
	unsigned char unk8000000_vec_9 = 0;
};

struct RoomSettings {
	unsigned long lowFlag = 0;
	unsigned long highFlag = 0;
	string roomName = "";
	unsigned char unk2 = 0;
	unsigned char unk4_1 = 0;
	unsigned char unk4_2 = 0;
	unsigned char unk4_3 = 0;
	unsigned long unk4_4 = 0;
	string password = "";
	unsigned char levelLimit = 0;
	unsigned char unk20 = 0;
	unsigned char gameModeID = 0;
	unsigned char mapID = 0;
	unsigned char maxPlayers = 0;
	unsigned char winLimit = 0;
	unsigned short killLimit = 0;
	unsigned char timeLimit = 0;
	unsigned char roundTime = 0;
	unsigned char weaponLimit = 0;
	unsigned char hostagePenalty = 0;
	unsigned char freezeTime = 0;
	unsigned char buyTime = 0;
	unsigned char nickNameDisplay = 0;
	unsigned char unk40000 = 0;
	unsigned char unk80000 = 0;
	bool friendlyFire = false;
	bool flashLight = false;
	bool footSteps = false;
	unsigned char unk800000 = 0;
	unsigned char unk1000000 = 0;
	unsigned char unk2000000 = 0;
	unsigned char unk4000000 = 0;
	unsigned char unk8000000 = 0;
	unsigned char deathCameraType = 0;
	bool voiceChat = false;
	RoomState roomState = RoomState::Waiting;
	unsigned char unk80000000_1 = 0;
	unk80000000_vec unk80000000_2[2];
	unsigned long unkh1_1 = 0;
	string unkh1_2 = "";
	unsigned char unkh1_3 = 0;
	unsigned char unkh1_4 = 0;
	unsigned char unkh1_5 = 0;
	unsigned char unkh2 = 0;
	unsigned char unkh4_1 = 0;
	unsigned char unkh4_2 = 0;
	unsigned char unkh4_3 = 0;
	unsigned char unkh4_4 = 0;
	unsigned char unkh8 = 0;
	unsigned short unkh10 = 0;
	unsigned char unkh20 = 0;
	unsigned char unkh40 = 0;
	unsigned char unkh80 = 0;
	unsigned char unkh100 = 0;
	vector<pair<unsigned char, unsigned char>> unkh200;
	unsigned char unkh400 = 0;
	unsigned char unkh800 = 0;
	unsigned char unkh1000 = 0;
};

namespace RoomTeam {
	enum : unsigned char {
		None = 0,
		Terrorist = 1,
		CounterTerrorist = 2,
		Spectator = 3
	};
}

struct RoomUser {
	User* user = nullptr;
	unsigned char team = RoomTeam::Terrorist;
	unsigned char status = 0; // 0 = Waiting, 2 = Ready
	unsigned char slot = 0;
	unsigned short mapSequence = 1; // 連續地圖場次計數 (第1張地圖為1)
};

class Room {
public:
	Room(unsigned short roomID, User* roomHostUser);

	unsigned short GetRoomID() const noexcept {
		return _roomID;
	}

	User* GetRoomHostUser() const noexcept {
		return _roomHostUser;
	}

	const RoomSettings& GetRoomSettings() const noexcept {
		return _roomSettings;
	}

	void SetRoomSettings(const RoomSettings& roomSettings) noexcept {
		_roomSettings = roomSettings;
	}

	const vector<RoomUser>& GetRoomUsersData() const noexcept {
		return _roomUsers;
	}

	vector<RoomUser>& GetRoomUsersData() noexcept {
		return _roomUsers;
	}

	vector<User*> GetRoomUsers() const {
		vector<User*> users;
		users.reserve(_roomUsers.size());
		for (const auto& ru : _roomUsers) {
			if (ru.user) users.push_back(ru.user);
		}
		return users;
	}

	GameMatch* GetGameMatch() const noexcept {
		return _gameMatch;
	}

	void SetGameMatch(GameMatch* gameMatch) noexcept {
		_gameMatch = gameMatch;
	}

	char AddRoomUser(User* user, unsigned char team = 0);
	void RemoveRoomUser(User* user);
	void UpdateRoomHostUser(User* user);
	void UpdateRoomSettings(const RoomSettings& roomSettings);

	RoomUser* GetRoomUser(User* user);
	RoomUser* GetRoomUserByUserID(unsigned long userID);
	bool SetUserTeam(User* user, unsigned char team);
	bool SetUserReady(User* user, unsigned char ready);

	bool IsBigHeadDisabled() const noexcept {
		return _bigheadDisabled;
	}

	void SetBigHeadDisabled(bool disabled) noexcept {
		_bigheadDisabled = disabled;
	}

private:
	unsigned short _roomID;
	User* _roomHostUser;
	RoomSettings _roomSettings;
	vector<RoomUser> _roomUsers;
	GameMatch* _gameMatch;
	bool _bigheadDisabled = false;
};