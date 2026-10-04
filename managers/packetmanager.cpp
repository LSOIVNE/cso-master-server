#include "packetmanager.h"
#include "packet_versionmanager.h"
#include "packet_transfermanager.h"
#include "packet_loginmanager.h"
#include "packet_serverlistmanager.h"
#include "packet_charactermanager.h"
#include "packet_cryptmanager.h"
#include "packet_roommanager.h"
#include "packet_clientcheckmanager.h"
#include "packet_umsgmanager.h"
#include "packet_hostmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_udpmanager.h"
#include "packet_shopmanager.h"
#include "packet_optionmanager.h"
#include "packet_favoritemanager.h"
#include "packet_itemmanager.h"
#include "packet_userstartmanager.h"
#include "packet_messengermanager.h"
#include "packet_minigamemanager.h"
#include "usermanager.h"
#include "serverconsole.h"

PacketManager packetManager;

PacketManager::PacketManager() {
	_running = false;
}

PacketManager::~PacketManager() {
	shutdown();
}

void PacketManager::Start() {
	if (_packetThread.joinable()) {
		serverConsole.Print(PrefixType::Warn, "[ PacketManager ] Thread is already running!\n");
		return;
	}

	serverConsole.Print(PrefixType::Info, "[ PacketManager ] Starting!\n");

	_packetThread = thread(&PacketManager::run, this);
}

void PacketManager::Stop() {
	if (!_packetThread.joinable()) {
		serverConsole.Print(PrefixType::Warn, "[ PacketManager ] Thread is already shut down!\n");
		return;
	}

	shutdown();
}

void PacketManager::QueuePacket(TCPConnection::Packet::pointer packet) {
	if (!_running) {
		return;
	}

	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ PacketManager ] Queueing packet from client ({})\n", connection->GetLogEndpoint()));

	{
		lock_guard<mutex> lock(_queueMutex);
		_packetQueue.push_back(packet);
	}
	_cv.notify_one();
}

void PacketManager::SendPacket_Reply(TCPConnection::pointer connection, Packet_ReplyType type, const vector<string>& additionalText) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Reply });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(type);
	packet->WriteString(""); // "S_REPLY_OK"? TODO: research this
	packet->WriteUInt8((unsigned char)additionalText.size());
	for (auto& text : additionalText) {
		packet->WriteString(text);
	}

	packet->Send();
}

void PacketManager::SendPacket_Inventory(TCPConnection::pointer connection, const vector<InventoryItem>& userInventory, const vector<unsigned short>& deletedSlots) {
	if (connection == NULL) {
		return;
	}

	vector<InventoryItem> itemsToSend;
	vector<unsigned short> slotsToDelete = deletedSlots;
	unsigned short cboxCount = 0;

	for (const auto& item : userInventory) {
		if (item.itemID == 166) {
			cboxCount += item.count;
			// Ensure any existing slot on client side for item 166 is removed
			slotsToDelete.push_back(item.slotID);
		}
		else {
			itemsToSend.push_back(item);
		}
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Inventory });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt16_LE((unsigned short)(itemsToSend.size() + slotsToDelete.size()));

	for (unsigned short slotID : slotsToDelete) {
		packet->WriteUInt16_LE(slotID);
		packet->WriteUInt8(0); // status (0 = removed / deleted from client inventory)
	}

	for (auto& item : itemsToSend) {
		packet->WriteUInt16_LE(item.slotID);
		packet->WriteUInt8(1); // status (1 = present)
		packet->WriteUInt16_LE(item.itemID);
		packet->WriteUInt16_LE(item.count);
		packet->WriteUInt8(item.activeFlag == 0 ? 1 : item.activeFlag);
		packet->WriteUInt8(item.inUse ? 1 : 0);
		packet->WriteUInt32_LE(item.obtainTime);
		packet->WriteUInt32_LE(item.expirationTime); // 0 = permanent
		packet->WriteUInt32_LE(item.enhanceBits);
	}

	packet->Send();

	// Also sync CBox count to client HUD / CGamePlayer
	User* user = userManager.GetUserByConnection(connection);
	if (user != NULL) {
		packet_UpdateInfoManager.SendPacket_UserUpdateInfo_CBoxCount(connection, user->GetUserID(), cboxCount);
	}
}

void PacketManager::BuildUserCharacter(TCPConnection::Packet::pointer packet, const UserCharacter& userCharacter) {
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt32_LE(userCharacter.flag);

	if (userCharacter.flag & USERCHARACTER_FLAG_UNK1) {
		packet->WriteUInt8(userCharacter.unk1);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_NICKNAME) {
		packet->WriteString(userCharacter.nickName);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_UNK4) {
		packet->WriteString(userCharacter.unk4_1);
		packet->WriteUInt8(userCharacter.unk4_2);
		packet->WriteUInt8(userCharacter.unk4_3);
		packet->WriteUInt8(userCharacter.unk4_4);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_LEVEL) {
		packet->WriteUInt8(userCharacter.level);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_UNK10) {
		packet->WriteUInt8(userCharacter.unk10);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_EXP) {
		packet->WriteUInt64_LE(userCharacter.exp);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_CASH) {
		packet->WriteUInt64_LE(userCharacter.cash);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_POINTS) {
		packet->WriteUInt64_LE(userCharacter.points);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_BATTLESTATS) {
		packet->WriteUInt32_LE(userCharacter.battles);
		packet->WriteUInt32_LE(userCharacter.wins);
		packet->WriteUInt32_LE(userCharacter.frags);
		packet->WriteUInt32_LE(userCharacter.deaths);
		packet->WriteUInt32_LE(userCharacter.battleStats4);
		packet->WriteUInt32_LE(userCharacter.battleStats5);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_LOCATION) {
		packet->WriteString(""); // pcBang
		packet->WriteUInt16_LE(userCharacter.city); // province/city (도시) index
		packet->WriteUInt16_LE(userCharacter.county); // district/county (구군) index
		packet->WriteUInt16_LE(userCharacter.neighborhood); // neighborhood (동) index
		packet->WriteString(userCharacter.unk200_5);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_UNK400) {
		unsigned long cashUint32 = (userCharacter.unk400 != 0) ? userCharacter.unk400 : (unsigned long)userCharacter.cash;
		packet->WriteUInt32_LE(cashUint32);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_UNK800) {
		packet->WriteUInt8(userCharacter.unk800);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_CLAN) {
		packet->WriteUInt32_LE(userCharacter.clanID);
		packet->WriteUInt32_LE(userCharacter.clanMarkID);
		packet->WriteString(userCharacter.clanName);
		packet->WriteUInt8(userCharacter.clanUnk4);
		packet->WriteUInt8(userCharacter.clanUnk5);
		packet->WriteUInt8(userCharacter.clanUnk6);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_CBOXCOUNT) {
		packet->WriteUInt16_LE(userCharacter.cboxCount);
	}
	if (userCharacter.flag & USERCHARACTER_FLAG_CHATCOUNT) {
		packet->WriteUInt8(userCharacter.chatRemainCount);
		packet->WriteUInt32_LE(0); // expiration / timeout (0 = never expires)
	}
}

int PacketManager::run() {
	serverConsole.Print(PrefixType::Info, "[ PacketManager ] Thread starting!\n");

	try {
		_running = true;
		while (_running) {
			vector<TCPConnection::Packet::pointer> packets;
			{
				unique_lock<mutex> lock(_queueMutex);
				_cv.wait(lock, [this] { return !_packetQueue.empty() || !_running; });

				if (!_running) {
					break;
				}

				while (!_packetQueue.empty()) {
					packets.push_back(_packetQueue.front());
					_packetQueue.pop_front();
				}
			}

			for (auto& packet : packets) {
				parsePacket(packet);
			}
		}
	}
	catch (exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ PacketManager ] Error on run: {}\n", e.what()));
		return -1;
	}

	serverConsole.Print(PrefixType::Info, "[ PacketManager ] Thread shutting down!\n");
	return 0;
}

int PacketManager::shutdown() {
	try {
		if (_packetThread.joinable()) {
			serverConsole.Print(PrefixType::Info, "[ PacketManager ] Shutting down!\n");

			_running = false;
			_packetQueue.clear();
			_packetThread.detach();
		}
	}
	catch (exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ PacketManager ] Error on shutdown: {}\n", e.what()));
		return -1;
	}

	return 0;
}

void PacketManager::parsePacket(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ PacketManager ] Client ({}) sent packet ({} bytes): {}\n", connection->GetLogEndpoint(), packet->GetBuffer().size(), packet->ByteStr(true)));

	unsigned char packetID = packet->ReadUInt8();

	switch ((PacketID)packetID) {
		case PacketID::Version: {
			packet_VersionManager.ParsePacket_Version(packet);
			break;
		}
		case PacketID::RecvCharacter: {
			packet_CharacterManager.ParsePacket_RecvCharacter(packet);
			break;
		}
		case PacketID::Login: {
			packet_LoginManager.ParsePacket_Login(packet);
			break;
		}
		case PacketID::TransferLogin: {
			packet_TransferManager.ParsePacket_TransferLogin(packet);
			break;
		}
		case PacketID::RequestTransfer: {
			packet_TransferManager.ParsePacket_RequestTransfer(packet);
			break;
		}
		case PacketID::RequestServerList: {
			packet_ServerListManager.ParsePacket_RequestServerList(packet);
			break;
		}
		case PacketID::RecvCrypt: {
			packet_CryptManager.ParsePacket_RecvCrypt(packet);
			break;
		}
		case PacketID::Room: {
			packet_RoomManager.ParsePacket_Room(packet);
			break;
		}
		case PacketID::ClientCheck: {
			packet_ClientCheckManager.ParsePacket_ClientCheck(packet);
			break;
		}
		case PacketID::UMsg: {
			packet_UMsgManager.ParsePacket_UMsg(packet);
			break;
		}
		case PacketID::Host: {
			packet_HostManager.ParsePacket_Host(packet);
			break;
		}
		case PacketID::UpdateInfo: {
			packet_UpdateInfoManager.ParsePacket_UpdateInfo(packet);
			break;
		}
		case PacketID::Udp: {
			packet_UdpManager.ParsePacket_Udp(packet);
			break;
		}
		case PacketID::Shop: {
			packet_ShopManager.ParsePacket_Shop(packet);
			break;
		}
		case PacketID::Option: {
			packet_OptionManager.ParsePacket_Option(packet);
			break;
		}
		case PacketID::Favorite: {
			packet_FavoriteManager.ParsePacket_Favorite(packet);
			break;
		}
		case PacketID::Item: {
			packet_ItemManager.ParsePacket_Item(packet);
			break;
		}
		case PacketID::UserStart: {
			packet_UserStartManager.ParsePacket_UserStart(packet);
			break;
		}
		case PacketID::Messenger: {
			packet_MessengerManager.ParsePacket_Messenger(packet);
			break;
		}
		case PacketID::Minigame: {
			packet_MiniGameManager.ParsePacket_MiniGame(packet);
			break;
		}
		case (PacketID)90: {
			// Client telemetry/ping packet
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ PacketManager ] Client ({}) has sent unregistered packet ID {}!\n", connection->GetLogEndpoint(), packetID));
			break;
		}
	}
}
