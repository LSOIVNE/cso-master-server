#pragma once
#include "gamematchuser.h"
#include "user.h"
#include <vector>

using namespace std;

class GameMatch {
public:
	GameMatch(unsigned short roomID);
	~GameMatch();

	const vector<unsigned char>& GetSaveData() {
		return _saveData;
	}

	void SetSaveData(const vector<unsigned char> saveData) {
		_saveData = saveData;
	}

	const vector<GameMatchUser*>& GetGameMatchUsers() {
		return _gameMatchUsers;
	}

	bool AddGameMatchUser(GameMatchUser* gameMatchUser);
	void RemoveGameMatchUser(GameMatchUser* gameMatchUser);
	GameMatchUser* GetGameMatchUserByUser(User* user);

	unsigned char GetWinTeam() const noexcept {
		return _winTeam;
	}
	void SetWinTeam(unsigned char winTeam) noexcept {
		_winTeam = winTeam;
	}

	unsigned char GetTerWins() const noexcept {
		return _terWins;
	}
	void SetTerWins(unsigned char terWins) noexcept {
		_terWins = terWins;
	}

	unsigned char GetCtWins() const noexcept {
		return _ctWins;
	}
	void SetCtWins(unsigned char ctWins) noexcept {
		_ctWins = ctWins;
	}

private:
	unsigned short _roomID;
	vector<unsigned char> _saveData;
	vector<GameMatchUser*> _gameMatchUsers;
	unsigned char _winTeam = 0;
	unsigned char _terWins = 0;
	unsigned char _ctWins = 0;
};