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
	bool broadcast = false;
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
	std::string boxName = "";
	unsigned short requireBoxID = 0; // 0 if none, e.g. 166 for CBOX
	ItemBoxRates rates;
	std::vector<ItemBoxReward> items;
};

struct ItemBoxSettings {
	bool broadcastGoldGrade = true;
	std::string broadcastFormat = "[伺服器訊息] ***** 恭喜[{PlayerName}]使用[{DecoderName}]獲得[{GradeName}][{ItemName}] *****";
	std::string goldGradeName = "金獎勳";
	std::string silverGradeName = "銀獎勳";
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
	const ItemBoxReward* RollReward(const ItemBoxConfig& boxDef) const;

	ItemBoxSettings _settings;
	std::unordered_map<unsigned short, ItemBoxConfig> _boxes;
};

extern ItemBoxManager itemBoxManager;
