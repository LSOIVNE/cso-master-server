#pragma once
#include "user.h"
#include <string>
#include <vector>
#include <map>

struct TitleConfig {
	unsigned short id = 0;
	string key = "";
	string name = "";
	string desc = "";
	unsigned short honorPoints = 0;
};

class TitleManager {
public:
	bool Init();
	bool Reload();

	const TitleConfig* GetTitle(unsigned short id) const;
	const map<unsigned short, TitleConfig>& GetAllTitles() const;

	bool UnlockTitle(User* user, unsigned short titleID, bool syncToClient = true);
	bool UnlockAllTitles(User* user, bool syncToClient = true);

	bool EquipTitle(User* user, unsigned short prefixID);
	bool EquipBadge(User* user, unsigned char slot, unsigned char titleID);
	bool UnequipBadge(User* user, unsigned char slot);

	void SyncTitleUpdate(User* user);

private:
	bool loadConfigs();
	map<unsigned short, TitleConfig> _titles;
};

extern TitleManager titleManager;
