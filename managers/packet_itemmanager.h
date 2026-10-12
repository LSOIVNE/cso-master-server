#pragma once
#include "user.h"

struct ItemDefinition;

enum Packet_ItemType {
	EquipItem = 0,
	SelectClass = 1,
	BuyWeapon = 2,
	OpenBox = 4,
	Craft = 5,
	MaterialAssem = 6,
	Disassemble = 7,
	Roulette = 8,
	Tattoo = 9,
	GachaponNotice = 10,
	GiftboxNotice = 11,
	FabEnduNotice = 12,
	Enhance = 14
};

bool IsTattooItem(unsigned short itemID);

class Packet_ItemManager {
public:
	void ParsePacket_Item(TCPConnection::Packet::pointer packet);
	void SendPacket_Item_Notice(TCPConnection::pointer connection, unsigned char noticeType);

private:
	void parsePacket_Item_EquipItem(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Item_UseRewardItem(User* user, const InventoryItem& item, const ItemDefinition& itemDef);
	void parsePacket_Item_SelectClass(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Item_BuyWeapon(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Item_OpenBox(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Item_Craft(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Item_MaterialAssem(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Item_Disassemble(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Item_Roulette(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Item_Tattoo(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Item_Enhance(User* user, TCPConnection::Packet::pointer packet);
};

extern Packet_ItemManager packet_ItemManager;