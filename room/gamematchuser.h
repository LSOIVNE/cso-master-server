#pragma once
#include "user.h"
#include <array>
#include <cstdint>

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

	unsigned short GetBonusExp() const noexcept { return _bonusExp; }
	void SetBonusExp(unsigned short v) noexcept { _bonusExp = v; }

	unsigned short GetBonusPoints() const noexcept { return _bonusPoints; }
	void SetBonusPoints(unsigned short v) noexcept { _bonusPoints = v; }

	const std::array<uint16_t, 14>& GetBonusPercentages() const noexcept { return _bonusPercentages; }
	void SetBonusPercentages(const std::array<uint16_t, 14>& v) noexcept { _bonusPercentages = v; }

	unsigned short GetClassItemID() const noexcept { return _classItemID; }
	void SetClassItemID(unsigned short v) noexcept { _classItemID = v; }

	unsigned short GetMapSequence() const noexcept { return _mapSequence; }
	void SetMapSequence(unsigned short v) noexcept { _mapSequence = v; }

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

	// Mode 10: Phantom / Hidden (魔幻空間 / 幽靈模式)
	unsigned short GetHdHiddenKill() const noexcept { return _hdHiddenKill; }
	void SetHdHiddenKill(unsigned short v) noexcept { _hdHiddenKill = v; }
	void AddHdHiddenKill(unsigned short v) noexcept { _hdHiddenKill += v; }

	unsigned short GetHdHumanKill() const noexcept { return _hdHumanKill; }
	void SetHdHumanKill(unsigned short v) noexcept { _hdHumanKill = v; }
	void AddHdHumanKill(unsigned short v) noexcept { _hdHumanKill += v; }

	// Mode 21: Bazooka (轟炸擂台)
	unsigned short GetBkAssist() const noexcept { return _bkAssist; }
	void SetBkAssist(unsigned short v) noexcept { _bkAssist = v; }
	void AddBkAssist(unsigned short v) noexcept { _bkAssist += v; }

	unsigned char GetBkMaxCombo() const noexcept { return _bkMaxCombo; }
	void SetBkMaxCombo(unsigned char v) noexcept { _bkMaxCombo = v; }
	void AddCombo(unsigned char v = 1) noexcept {
		_currentCombo += v;
		if (_currentCombo > _bkMaxCombo) {
			_bkMaxCombo = _currentCombo;
		}
	}
	void ResetCombo() noexcept { _currentCombo = 0; }

	// Mode 23 (悠遊模式) & Mode 25 (即刻救援)
	unsigned short GetLoAssist() const noexcept { return _loAssist; }
	void SetLoAssist(unsigned short v) noexcept { _loAssist = v; }
	void AddLoAssist(unsigned short v) noexcept { _loAssist += v; }

	unsigned int GetBrPoint() const noexcept { return _brPoint; }
	void SetBrPoint(unsigned int v) noexcept { _brPoint = v; }
	void AddBrPoint(unsigned int v) noexcept { _brPoint += v; }

	// Mode 26: Shelter Survival (夜襲生存戰)
	unsigned short GetZshPoint() const noexcept { return _zshPoint; }
	void SetZshPoint(unsigned short v) noexcept { _zshPoint = v; }
	void AddZshPoint(unsigned short v) noexcept { _zshPoint += v; }

	unsigned char GetRescuee() const noexcept { return _rescuee; }
	void SetRescuee(unsigned char v) noexcept { _rescuee = v; }
	void AddRescuee(unsigned char v) noexcept { _rescuee += v; }

	unsigned char GetSurviveDays() const noexcept { return _surviveDays; }
	void SetSurviveDays(unsigned char v) noexcept { _surviveDays = v; }
	void AddSurviveDays(unsigned char v = 1) noexcept { _surviveDays += v; }

	unsigned char GetMaxSurviveDays() const noexcept { return _maxSurviveDays; }
	void SetMaxSurviveDays(unsigned char v) noexcept { _maxSurviveDays = v; }

	unsigned char GetAccumSurviveDays() const noexcept { return _accumSurviveDays; }
	void SetAccumSurviveDays(unsigned char v) noexcept { _accumSurviveDays = v; }
	void AddAccumSurviveDays(unsigned char v) noexcept { _accumSurviveDays += v; }

	unsigned char GetAccumRescuee() const noexcept { return _accumRescuee; }
	void SetAccumRescuee(unsigned char v) noexcept { _accumRescuee = v; }
	void AddAccumRescuee(unsigned char v) noexcept { _accumRescuee += v; }

private:
	User* _user;
	Team _team;
	unsigned short _kills;
	unsigned short _deaths;
	unsigned short _exp;
	unsigned short _points;
	unsigned char _levelUp;
	unsigned char _rank;
	unsigned short _bonusExp;
	unsigned short _bonusPoints;
	std::array<uint16_t, 14> _bonusPercentages;
	unsigned short _classItemID;
	unsigned short _mapSequence;

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

	// Mode 10 Stats
	unsigned short _hdHiddenKill;
	unsigned short _hdHumanKill;

	// Mode 21 Stats
	unsigned short _bkAssist;
	unsigned char _bkMaxCombo;
	unsigned char _currentCombo;

	// Mode 23 & 25 Stats
	unsigned short _loAssist;
	unsigned int _brPoint;

	// Mode 26 Stats
	unsigned short _zshPoint;
	unsigned char _rescuee;
	unsigned char _surviveDays;
	unsigned char _maxSurviveDays;
	unsigned char _accumSurviveDays;
	unsigned char _accumRescuee;
};