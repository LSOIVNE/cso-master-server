#pragma once
#include "user.h"

enum Team {
	None = 0,
	Terrorist = 1,
	CounterTerrorist = 2
};

class GameMatchUser {
public:
	GameMatchUser(User* user);

	User* GetUser() const noexcept {
		return _user;
	}

	Team GetTeam() const noexcept {
		return _team;
	}

	void SetTeam(Team team) noexcept {
		_team = team;
	}

	unsigned short GetKills() const noexcept {
		return _kills;
	}

	void SetKills(unsigned short kills) noexcept {
		_kills = kills;
	}

	unsigned short GetDeaths() const noexcept {
		return _deaths;
	}

	void SetDeaths(unsigned short deaths) noexcept {
		_deaths = deaths;
	}

	unsigned short GetExp() const noexcept {
		return _exp;
	}

	void SetExp(unsigned short exp) noexcept {
		_exp = exp;
	}

	unsigned short GetPoints() const noexcept {
		return _points;
	}

	void SetPoints(unsigned short points) noexcept {
		_points = points;
	}

	unsigned char GetLevelUp() const noexcept {
		return _levelUp;
	}

	void SetLevelUp(unsigned char levelUp) noexcept {
		_levelUp = levelUp;
	}

	unsigned char GetRank() const noexcept {
		return _rank;
	}

	void SetRank(unsigned char rank) noexcept {
		_rank = rank;
	}

private:
	User* _user;
	Team _team;
	unsigned short _kills;
	unsigned short _deaths;
	unsigned short _exp;
	unsigned short _points;
	unsigned char _levelUp;
	unsigned char _rank;
};