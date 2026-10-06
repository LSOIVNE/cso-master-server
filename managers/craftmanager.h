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
	unsigned long normalCraftFee = 500;
	unsigned long advancedCraftFee = 1000;
	unsigned long disassembleFeePerItem = 50;
	std::string permanentDisplayMode = "Permanent"; // "Permanent" (displays 永久) or "Count" (displays 1個)
	int singleRecycleBlueprintWeight = 50; // 1個再生工法返還設計圖的權重
	int singleRecycleDurabilityWeight = 50; // 1個再生工法返還耐久材料的權重
	int longTermPermanentChance = 10; // 長期耐久材料 (5106) 製作出永久的機率百分比 (0~100)
};

struct DisassembleRewardEntry {
	unsigned short itemID = 0;
	std::string name;
	int weight = 10;
};

struct DisassembleCategoryConfig {
	bool enabled = true;
	int dropChance = 50; // 觸發掉落機率 % (0~100)
	int minAmount = 1;
	int maxAmount = 1;
	std::vector<DisassembleRewardEntry> items;
};

struct DisassembleScalingConfig {
	int permanentShares = 10;       // 永久武器基礎獎勵份額
	int daysPerShare = 3;           // 期間型道具每幾天增加1份額
	int maxPeriodShares = 30;       // 期間型道具最大獎勵份額上限
};

struct DisassembleConfig {
	DisassembleScalingConfig scaling;
	DisassembleCategoryConfig durability;  // 耐久材料列表
	DisassembleCategoryConfig blueprint;   // 圖紙列表
};

struct DurabilityConfig {
	int minDays = 1;
	int maxDays = 1;
	int permChance = 0; // 0~100 percentage
};

struct MaterialCombineSettings {
	unsigned long feePoint = 100;
	int successRate2Items = 55;
	int successRate3Items = 80;
	int failLowGradeChance = 80;
};

class CraftManager {
public:
	bool Init();
	bool Reload();

	bool QueryCraftPreview(User* user, unsigned short bpSlotID, unsigned short durSlotID, unsigned short secSlotID1, unsigned short secSlotID2);
	bool CraftWeapon(User* user, unsigned short bpSlotID, unsigned short durSlotID, unsigned short secSlotID1, unsigned short secSlotID2);
	bool DisassembleItems(User* user, const std::vector<unsigned short>& weaponSlots);
	bool AssembleMaterials(User* user, const std::vector<unsigned short>& slots);

	const BlueprintConfig* GetBlueprintConfig(unsigned short bpID) const;
	int RollDurabilityDays(unsigned short durID) const;
	unsigned long GetBlueprintFee(unsigned short bpID) const;

private:
	void sendCraftPreview(TCPConnection::pointer connection, unsigned char successLevel, unsigned long feePoint);
	void sendCraftResultSuccess(TCPConnection::pointer connection, unsigned short itemID, unsigned short durationDays, unsigned short count = 1);
	void sendCraftResultFailure(TCPConnection::pointer connection, const std::vector<unsigned short>& returnedItemIDs);
	void sendDisassembleResult(TCPConnection::pointer connection, unsigned short itemID, unsigned short count);
	void sendDisassembleFailure(TCPConnection::pointer connection, const std::string& failReason);
	void sendMaterialAssemResult(TCPConnection::pointer connection, unsigned char resultFlag, unsigned short itemID, unsigned short count);

private:
	CraftSettings _settings;
	MaterialCombineSettings _combineSettings;
	DisassembleConfig _disassembleCfg;
	std::unordered_map<unsigned short, DurabilityConfig> _durabilityMapping;
	std::unordered_map<unsigned short, BlueprintConfig> _blueprints;
};

extern CraftManager craftManager;
