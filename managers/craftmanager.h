#pragma once

#include "user.h"
#include <vector>
#include <string>
#include <unordered_map>

struct CraftRewardItem {
	unsigned short itemID = 0;
	std::string name;
	int weight = 10;
};

struct BlueprintConfig {
	std::string name;
	std::string type;
	std::string grade;
	int successChance = 50;
	unsigned long craftFee = 0;
	std::vector<CraftRewardItem> items;
};

struct CraftSettings {
	int normalSuccessChance = 50;
	int advancedSuccessChance = 80;
	int luckyBonusChance = 20;
	int broadcastMinGrade = 3;
	unsigned long normalCraftFee = 500;
	unsigned long advancedCraftFee = 1000;
	unsigned long disassembleFeePerItem = 50;
};

struct RecycleItem {
	unsigned short itemID = 0;
	unsigned short count = 1;
};

class CraftManager {
public:
	bool Init();
	bool Reload();

	bool QueryCraftPreview(User* user, unsigned short bpSlotID, unsigned short durSlotID, unsigned short secSlotID1, unsigned short secSlotID2);
	bool CraftWeapon(User* user, unsigned short bpSlotID, unsigned short durSlotID, unsigned short secSlotID1, unsigned short secSlotID2);
	bool DisassembleItems(User* user, const std::vector<unsigned short>& weaponSlots);

	const BlueprintConfig* GetBlueprintConfig(unsigned short bpID) const;
	int GetDurabilityDays(unsigned short durID) const;
	unsigned long GetBlueprintFee(unsigned short bpID) const;

private:
	void sendCraftPreview(TCPConnection::pointer connection, unsigned char successLevel, unsigned long feePoint);
	void sendCraftResultSuccess(TCPConnection::pointer connection, unsigned short itemID, unsigned short durationDays);
	void sendCraftResultFailure(TCPConnection::pointer connection, const std::vector<unsigned short>& returnedItemIDs);

private:
	CraftSettings _settings;
	std::unordered_map<unsigned short, int> _durabilityMapping;
	std::vector<RecycleItem> _normalRecycleItems;
	std::vector<RecycleItem> _bonusRecycleItems;
	std::unordered_map<unsigned short, BlueprintConfig> _blueprints;
};

extern CraftManager craftManager;
