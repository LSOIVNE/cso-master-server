#pragma once
#include "user.h"
#include <string>
#include <vector>
#include <unordered_map>

struct ItemBoxReward {
	unsigned short itemID = 0;
	std::string name = "";
	int duration = -1; // -1 = permanent, >0 = days, 0 = count
	unsigned short count = 1;
	int grade = 1; // 1 = Normal, 2 = Silver, 3 = Gold, 4 = Top Gold
	int broadcast = -1; // -1 = default by grade (grade >= broadcastMinGrade), 0 = suppress, 1 = force broadcast
};

struct ItemBoxRates {
	double grade1 = 68.0;
	double grade2 = 20.0;
	double grade3 = 9.0;
	double grade4 = 3.0;
};

struct ItemBoxConfig {
	unsigned short decoderID = 0;
	std::string name = "";
	ItemBoxRates rates;
	std::vector<ItemBoxReward> items;
};

struct ItemBoxSettings {
	int broadcastMinGrade = 3; // Minimum grade to broadcast announcement (3 = Silver and Gold, 4 = Gold only)
	bool skipOwnedPermanent = true; // If true, exclude items that the user already owns permanently
	bool skipPeriodIfPermanentOwned = true; // If true, exclude period items for weapons the user already owns permanently
};

class ItemBoxManager {
public:
	bool Init(const std::string& configPath = "configs/itembox.json");
	bool LoadConfig(const std::string& configPath = "configs/itembox.json");

	bool HasBoxConfig(unsigned short decoderID) const;
	const ItemBoxConfig* GetBoxConfig(unsigned short decoderID) const;
	unsigned short ResolveDecoderID(unsigned short id) const;

	bool OpenBox(User* user, unsigned short decoderSlotID, unsigned short boxSlotID, unsigned char flag);

	void SendPacket_Item_OpenBoxResult(TCPConnection::pointer connection, unsigned char result, unsigned char grade = 1, unsigned int itemID = 0, short duration = 0, unsigned short count = 0);

private:
	const ItemBoxReward* RollReward(const ItemBoxConfig& boxDef, const std::vector<InventoryItem>* userInventory = nullptr) const;

	ItemBoxSettings _settings;
	std::unordered_map<unsigned short, ItemBoxConfig> _boxes;
};

extern ItemBoxManager itemBoxManager;
