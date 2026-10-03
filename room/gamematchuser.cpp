#include "gamematchuser.h"
#include <cstddef>

GameMatchUser::GameMatchUser(User* user) : _user(user), _team(Team::None), _kills(0), _deaths(0), _exp(100), _points(200), _levelUp(0), _rank(1) {
	if (user == NULL) {
		delete this;
		return;
	}
}