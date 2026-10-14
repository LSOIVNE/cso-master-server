#include "gamematchuser.h"
#include <cstddef>

GameMatchUser::GameMatchUser(User* user) : _user(user), _team(Team::None), _kills(0), _deaths(0), _exp(100), _points(200), _levelUp(0), _rank(1),
	_headshots(0), _grenadeKills(0), _knifeKills(0),
	_bombDeactivate(0), _bombSuccess(0), _hostageRescue(0), _vipEscape(0), _vipKill(0),
	_surviveCount(0), _zombieKill(0), _zombieMotherKill(0), _humanKill(0),
	_hdHiddenKill(0), _hdHumanKill(0),
	_bkAssist(0), _bkMaxCombo(0), _currentCombo(0),
	_loAssist(0), _brPoint(0),
	_zshPoint(0), _rescuee(0), _surviveDays(0), _maxSurviveDays(0), _accumSurviveDays(0), _accumRescuee(0) {
	if (user == NULL) {
		delete this;
		return;
	}
}