#pragma once
#include "user.h"
#include "room.h"
#include <string>
#include <vector>
#include <map>

struct KillDropItem {
	unsigned short itemID = 0;
	std::string name = "";
	unsigned short count = 1;
	unsigned short duration = 0; // 0 = count item / permanent
	unsigned int weight = 10;
	unsigned char noticeType = 12; // 10 = C-Box (Gachapon), 11 = Giftbox, 12 = FabEndu (耐久材料)
};

struct KillDropSettings {
	bool enabled = true;
	float dropRate = 0.01f; // default drop rate per kill
	std::map<unsigned char, float> modeDropRates; // gamemodeID -> dropRate (e.g. 15: 0.002f)
	bool enableInAllModes = true;
	std::vector<unsigned char> allowedGameModes;
	bool showRightHudIcon = true;
	bool showChatNotice = true;
};

class KillDropManager {
public:
	void Init(const std::string& configPath = "configs/kill_drop.json");
	void Reload();

	// Called on player kill count increment
	void OnPlayerKill(User* killer, Room* room, unsigned short killCount = 1);

private:
	bool LoadConfig(const std::string& path);
	bool TriggerDrop(User* killer, Room* room);
	void GiveDropItem(User* killer, Room* room, const KillDropItem& dropItem);

	KillDropSettings _settings;
	std::vector<KillDropItem> _dropItems;
	unsigned int _totalWeight = 0;
	std::string _configPath = "configs/kill_drop.json";
};

extern KillDropManager killDropManager;
