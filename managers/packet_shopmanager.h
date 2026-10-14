#pragma once
#include "user.h"
#include "databasemanager.h"
#include <unordered_map>
#include <vector>
#include <string>

enum Packet_ShopType {
	RequestShopList = 0,
	ShopList = 0,
	Buy = 1,
	BuyReply = 1,
	Gift = 2,
	GiftReply = 2,
	RequestGiftList = 3,
	GiftList = 3,
	ClaimGift = 4,
	ClaimGiftReply = 4,
	GiftPush = 5,
	ViewGift = 5
};

struct ShopPackageReward {
	unsigned short itemID = 0;
	unsigned short duration = 0; // 0 = permanent (if duration item), or days
	unsigned short count = 1;    // quantity (if count-based consumable)
};

struct ShopOffer {
	unsigned int subProductID = 0;        // Sequential offer ID sent to client
	unsigned short duration = 0;           // Days (3, 10, 30, 0=permanent)
	unsigned char isCount = 0;             // 1 if count-based (decoder, ticket, part), 0 if duration
	unsigned short count = 1;              // Quantity (1, 5, 10, 30)
	unsigned int price = 0;                // Price in Points or Cash
	unsigned int additionalPoints = 0;     // 0
	unsigned int additionalClanPoints = 0; // 0
	unsigned char adType = 0;              // 0=None, 1=Event, 2=New, 4=Hot, 7=Hot Event
	bool canGift = true;                   // 1=Giftable (Bit 5 / 0x20 in adType), 0=Non-giftable

	// Parent product context for purchase logic
	unsigned int productID = 0;
	std::string productName = "";
	bool isPoints = true;
	std::vector<ShopPackageReward> packageItems;
};

struct ShopProduct {
	unsigned int productID = 0;
	std::string name = "";
	bool isPoints = true; // 1 = Points, 0 = Cash
	bool canGift = true;
	std::vector<ShopPackageReward> packageItems;
	std::vector<ShopOffer> offers;
};

class Packet_ShopManager {
public:
	bool Init(const std::string& configPath = "configs/shop.json");
	bool LoadShopConfig(const std::string& configPath = "configs/shop.json");
	void ParsePacket_Shop(TCPConnection::Packet::pointer packet);

	size_t GetProductCount() const { return _products.size(); }
	size_t GetOfferCount() const { return _offersBySubId.size(); }
	const ShopProduct* GetProductByID(unsigned int productID) const;
	void SendPacket_Shop_ShopList(TCPConnection::pointer connection);
	void SendPacket_Shop_GiftList(TCPConnection::pointer connection, User* user);
	void BroadcastShopList();

private:
	void parsePacket_Shop_RequestShopList(User* user);
	void parsePacket_Shop_RequestGiftList(User* user);
	void parsePacket_Shop_Buy(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Shop_Gift(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Shop_ClaimGift(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_Shop_ViewGift(User* user, TCPConnection::Packet::pointer packet);
	void sendPacket_Shop_BuyReply(TCPConnection::pointer connection, unsigned char status, const std::string& message = "", const std::vector<unsigned short>& itemIDs = {});
	void sendPacket_Shop_GiftReply(TCPConnection::pointer connection, unsigned char status, const std::string& message = "");
	void sendPacket_Shop_GiftPush(TCPConnection::pointer connection, const UserGift& gift);
	void sendPacket_Shop_ClaimGiftReply(TCPConnection::pointer connection, unsigned char status, unsigned int giftID_low, unsigned int giftID_high = 0, unsigned short claimedCount = 0, const std::vector<unsigned short>& itemIDs = {}, const std::string& message = "");
	bool grantProductRewardToUser(unsigned long userID, unsigned int productID, unsigned short duration, unsigned char isCount, unsigned short count, std::vector<unsigned short>& outAwardedItemIDs, std::vector<InventoryItem>* outDeltaItems = nullptr);

private:
	std::vector<ShopProduct> _products;
	std::unordered_map<unsigned int, ShopOffer> _offersBySubId;
};

extern Packet_ShopManager packet_ShopManager;