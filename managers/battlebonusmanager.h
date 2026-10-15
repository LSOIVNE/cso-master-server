#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <cstdint>
#include <ctime>

class User;
class GameMatchUser;

struct PassItemBonus {
	unsigned short itemID = 0;
	std::string name;
	unsigned short pointPercent = 0;
	unsigned short expPercent = 0;
	std::string exclusiveGroup;
	std::vector<unsigned char> validModes; // empty = all game modes
};

struct ClassBonus {
	unsigned short itemID = 0;
	std::string name;
	unsigned short pointPercent = 0;
	unsigned short expPercent = 0;
};

struct DecorationBonus {
	unsigned short itemID = 0;
	std::string name;
	unsigned short pointPercent = 0;
	unsigned short expPercent = 0;
};

struct GlobalBonusConfig {
	unsigned short pointPercent = 0;
	unsigned short expPercent = 0;
};

struct ScheduledEventBonus {
	std::string name;
	unsigned short pointPercent = 0;
	unsigned short expPercent = 0;
	std::string date;        // 某個日期: "YYYY-MM-DD"
	std::string dateRange;   // 某個日期範圍: "YYYY-MM-DD~YYYY-MM-DD"
	std::string timeRange;   // 某個時間段: "HH:MM-HH:MM"
};

struct BattleBonusResult {
	uint16_t pcBangPoint = 0;
	uint16_t itemPoint = 0;
	uint16_t classPoint = 0;
	uint16_t eventPoint = 0;
	uint16_t decoPoint = 0;
	uint16_t teamPoint = 0;
	uint16_t mapSeqPoint = 0;

	uint16_t pcBangExp = 0;
	uint16_t itemExp = 0;
	uint16_t classExp = 0;
	uint16_t eventExp = 0;
	uint16_t decoExp = 0;
	uint16_t teamExp = 0;
	uint16_t mapSeqExp = 0;

	uint16_t totalPointPct = 0;
	uint16_t totalExpPct = 0;

	unsigned short bonusPoints = 0;
	unsigned short bonusExp = 0;

	std::array<uint16_t, 14> ToPacketArray() const {
		return {
			pcBangPoint, itemPoint, classPoint, eventPoint, decoPoint, teamPoint, mapSeqPoint,
			pcBangExp,   itemExp,   classExp,   eventExp,   decoExp,   teamExp,   mapSeqExp
		};
	}
};

struct RewardRule {
	unsigned short base = 20;
	float perKill = 3.0f;
	unsigned short maxKillBonus = 300;
	unsigned short winBonus = 15;
};

struct ModeRewardConfig {
	std::string name;
	std::vector<unsigned char> modeIDs;
	RewardRule exp;
	RewardRule points;
};

struct BaseRewardsConfig {
	RewardRule defaultExp{ 20, 3.0f, 300, 15 };
	RewardRule defaultPoints{ 50, 5.0f, 500, 30 };
	std::vector<ModeRewardConfig> modeOverrides;
	float botModeMultiplier = 0.5f;
};

struct BaseRewardResult {
	unsigned short exp = 0;
	unsigned short points = 0;
};

class BattleBonusManager {
public:
	BattleBonusManager();
	~BattleBonusManager() = default;

	bool Init();
	bool LoadConfig(const std::string& configPath);
	bool Reload(const std::string& configPath = "configs/battle_bonus.json") { return LoadConfig(configPath); }

	BaseRewardResult CalculateBaseRewards(unsigned char gameModeID, unsigned short kills, bool isWin) const;
	BattleBonusResult CalculateBonus(User* user, GameMatchUser* gmu, unsigned char gameModeID, unsigned short baseExp, unsigned short basePoints);
	bool HasClassBonus(unsigned short itemID) const;

private:
	static bool IsEventActive(const ScheduledEventBonus& evt, const tm& localTm);

	BaseRewardsConfig _baseRewards;
	bool _stackGeneralAndZombie = false;
	std::unordered_map<unsigned short, PassItemBonus> _passItems;
	std::unordered_map<unsigned short, ClassBonus> _classes;
	std::unordered_map<unsigned short, DecorationBonus> _decorations;
	GlobalBonusConfig _pcBangBonus;
	GlobalBonusConfig _baseEventBonus;
	std::vector<ScheduledEventBonus> _scheduledEvents;

	bool _mapSeqBonusEnabled = true;
	unsigned short _mapSeqStartMap = 2;
	unsigned short _mapSeqStartPercent = 4;
	unsigned short _mapSeqStepPercent = 2;
	unsigned short _mapSeqMaxPercent = 10;
};

extern BattleBonusManager battleBonusManager;
