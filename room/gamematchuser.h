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

	// General Battle Stats
	unsigned char GetHeadshots() const noexcept { return _headshots; }
	void SetHeadshots(unsigned char v) noexcept { _headshots = v; }
	void AddHeadshots(unsigned char v) noexcept { _headshots += v; }

	unsigned char GetGrenadeKills() const noexcept { return _grenadeKills; }
	void SetGrenadeKills(unsigned char v) noexcept { _grenadeKills = v; }
	void AddGrenadeKills(unsigned char v) noexcept { _grenadeKills += v; }

	unsigned char GetKnifeKills() const noexcept { return _knifeKills; }
	void SetKnifeKills(unsigned char v) noexcept { _knifeKills = v; }
	void AddKnifeKills(unsigned char v) noexcept { _knifeKills += v; }

	// Original Mode Stats (GameMode 0, 3, 6, 7, 23)
	unsigned char GetBombDeactivate() const noexcept { return _bombDeactivate; }
	void SetBombDeactivate(unsigned char v) noexcept { _bombDeactivate = v; }
	void AddBombDeactivate(unsigned char v) noexcept { _bombDeactivate += v; }

	unsigned char GetBombSuccess() const noexcept { return _bombSuccess; }
	void SetBombSuccess(unsigned char v) noexcept { _bombSuccess = v; }
	void AddBombSuccess(unsigned char v) noexcept { _bombSuccess += v; }

	unsigned char GetHostageRescue() const noexcept { return _hostageRescue; }
	void SetHostageRescue(unsigned char v) noexcept { _hostageRescue = v; }
	void AddHostageRescue(unsigned char v) noexcept { _hostageRescue += v; }

	unsigned char GetVipEscape() const noexcept { return _vipEscape; }
	void SetVipEscape(unsigned char v) noexcept { _vipEscape = v; }
	void AddVipEscape(unsigned char v) noexcept { _vipEscape += v; }

	unsigned char GetVipKill() const noexcept { return _vipKill; }
	void SetVipKill(unsigned char v) noexcept { _vipKill = v; }
	void AddVipKill(unsigned char v) noexcept { _vipKill += v; }

	// Zombie Mode Stats (GameMode 8, 9, 14, 20, 24)
	unsigned char GetSurviveCount() const noexcept { return _surviveCount; }
	void SetSurviveCount(unsigned char v) noexcept { _surviveCount = v; }
	void AddSurviveCount(unsigned char v) noexcept { _surviveCount += v; }

	unsigned char GetZombieKill() const noexcept { return _zombieKill; }
	void SetZombieKill(unsigned char v) noexcept { _zombieKill = v; }
	void AddZombieKill(unsigned char v) noexcept { _zombieKill += v; }

	unsigned char GetZombieMotherKill() const noexcept { return _zombieMotherKill; }
	void SetZombieMotherKill(unsigned char v) noexcept { _zombieMotherKill = v; }
	void AddZombieMotherKill(unsigned char v) noexcept { _zombieMotherKill += v; }

	unsigned char GetHumanKill() const noexcept { return _humanKill; }
	void SetHumanKill(unsigned char v) noexcept { _humanKill = v; }
	void AddHumanKill(unsigned char v) noexcept { _humanKill += v; }

private:
	User* _user;
	Team _team;
	unsigned short _kills;
	unsigned short _deaths;
	unsigned short _exp;
	unsigned short _points;
	unsigned char _levelUp;
	unsigned char _rank;

	// General Battle Stats
	unsigned char _headshots;
	unsigned char _grenadeKills;
	unsigned char _knifeKills;

	// Original Mode Stats
	unsigned char _bombDeactivate;
	unsigned char _bombSuccess;
	unsigned char _hostageRescue;
	unsigned char _vipEscape;
	unsigned char _vipKill;

	// Zombie Mode Stats
	unsigned char _surviveCount;
	unsigned char _zombieKill;
	unsigned char _zombieMotherKill;
	unsigned char _humanKill;
};