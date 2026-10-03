#pragma once
#include "user.h"
#include <unordered_map>
#include <vector>
#include <string>

enum Packet_ShopType {
	RequestShopList = 0,
	ShopList = 0,
	Buy = 1,
	BuyReply = 1
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

	// Parent product context for purchase logic
	unsigned int productID = 0;
	std::string productName = "";
	bool isPoints = true;
};

struct ShopProduct {
	unsigned int productID = 0;
	std::string name = "";
	bool isPoints = true; // 1 = Points, 0 = Cash
	std::vector<ShopOffer> offers;
};

class Packet_ShopManager {
public:
	bool Init(const std::string& configPath = "configs/shop.json");
	bool LoadShopConfig(const std::string& configPath = "configs/shop.json");
	void ParsePacket_Shop(TCPConnection::Packet::pointer packet);

	size_t GetProductCount() const { return _products.size(); }
	size_t GetOfferCount() const { return _offersBySubId.size(); }
	void SendPacket_Shop_ShopList(TCPConnection::pointer connection);
	void BroadcastShopList();

private:
	void parsePacket_Shop_RequestShopList(User* user);
	void parsePacket_Shop_Buy(User* user, TCPConnection::Packet::pointer packet);
	void sendPacket_Shop_BuyReply(TCPConnection::pointer connection, unsigned char status, const std::string& message = "");

private:
	std::vector<ShopProduct> _products;
	std::unordered_map<unsigned int, ShopOffer> _offersBySubId;
};

extern Packet_ShopManager packet_ShopManager;