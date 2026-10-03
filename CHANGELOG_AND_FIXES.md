# CSOTW (CSO TW CB2) 服务端与客户端修复与适配全记录

本文档汇总记录了本项目（`cso-master-server` 服务端与 CSO TW CB2 客户端）截至目前的所有逆向分析、协议适配、Bug 修复及底层架构改进。

---

## 目录
1. [项目架构与核心组件](#1-项目架构与核心组件)
2. [大厅与用户数据展示适配](#2-大厅与用户数据展示适配)
3. [仓库道具与装备组合（Favorite）存储修复](#3-仓库道具与装备组合favorite存储修复)
4. [创建房间模式/地图切换防闪退修复](#4-创建房间模式地图切换防闪退修复)
5. [游戏加载中 `#256256M1887[????]_p.BMP` 弹窗根因与修复](#5-游戏加载中-256256m1887_pbmp-弹窗根因与修复)
6. [房间等待室队伍列表与局内选人出生修复](#6-房间等待室队伍列表与局内选人出生修复)
7. [房间内自由换队（TR / CT / 观战者）协议适配](#7-房间内自由换队tr--ct--观战者协议适配)
8. [聊天系统与全服广播（ServerYell）协议适配与字体颜色逆向](#8-聊天系统与全服广播serveryell协议适配与字体颜色逆向)
9. [官方繁中物品全集（631项）与动态加载校验器（ItemDefinitionManager）](#9-官方繁中物品全集631项与动态加载校验器itemdefinitionmanager)
10. [管理员聊天指令体系（/additem, /addallitem, 分类过滤与黑名单）](#10-管理员聊天指令体系additem-addallitem-分类过滤与黑名单)
11. [仓库一键重置脚本与客户端原生即时刷新（Delta Purge / status=0）](#11-仓库一键重置脚本与客户端原生即时刷新delta-purge--status0)
12. [客户端崩溃（COpenglDetect / CrashReport.dmp）根因定位与协议防下溢修复](#12-客户端崩溃copengldetect--crashreportdmp根因定位与协议防下溢修复)
13. [修改文件清单与影响范围](#13-修改文件清单与影响范围)

---

## 1. 项目架构与核心组件

* **服务端**：`d:\agy\CounterStrike\CSOTW\cso-master-server`
  * 采用 C++20（Visual Studio 2022 v143 工具集，x64）。
  * 基于 Boost.Asio 实现 TCP / UDP 双协议通信（TCP 8001 主信令，UDP 8001 P2P 打洞与局域网路由）。
  * 数据库：MariaDB / MySQL 5.7+ (`csodatabase`)，存储用户、背包物品、收藏组合。
* **客户端**：`d:\agy\CounterStrike\CSOTW\Counter-Strike Online`
  * 核心二进制：`Bin\cstrike-online.exe`、`hw.dll`（GoldSrc 引擎核心与网络解析）、`GameUI.dll`（VGUI 大厅与房间界面）。
  * 协议版本：LauncherVersion `67`，ClientVersion `26`，客户端 Build 日期：`2013-06-04`。

---

## 2. 大厅与用户数据展示适配

### 2.1 问题表现
* 玩家登录大厅后，左上角角色等级、昵称、金币/积分/经验值显示空白或异常。

### 2.2 逆向定位与修复
* **逆向分析**：
  * 客户端 `hw.dll` 与 `GameUI.dll` 在接收登录与角色数据包（`PacketID::Character` 0x14 / `PacketID::ServerList`）时，对角色基础属性的字节偏移和位标志（Bitmask）有严格校验。
* **修复内容**：
  * 在 `packetmanager.cpp` 中调整 `BuildUserCharacter` 打包逻辑，严格按照客户端期望的序列化布局填充：
    * 角色等级（Level）、经验值（EXP）、金币（Points / Cash）；
    * 角色装备的角色模型 ID（CT / TR 默认模型）；
  * 完善了数据库中的默认用户初始化数据，使新注册或已有账号在大厅左上角能即时正确渲染角色名、军衔图标与资产数据。

---

## 3. 仓库道具与装备组合（Favorite）存储修复

### 3.1 问题表现
* 仓库只有一把小刀是使用中，点另一把刀会顶替或显示过期。
* 编辑组合（A/B/C 背包预设）中的武器/角色道具，退出登录再重新登录后全部重置丢失。

### 3.2 逆向定位与修复
* **逆向分析**：
  * 客户端在仓库中切换武器时，会向服务端发送 `PacketID::Item`（0x4D）与 `PacketID::Favorite`。
  * 原服务端在处理背包物品状态变更时，未区分“各槽位独立装备（主武器、副武器、近战、投掷、角色）”，导致装备新道具时粗暴重置了其他槽位。
  * 客户端点击“保存编辑组合”时发送的数据包未持久化写入数据库 `user_favorites` 表。
* **修复内容**：
  * **`packet_itemmanager.cpp`**：按装备类型（Slot Type）划分互斥逻辑，装备主武器仅顶替上一把主武器，不再影响副武器、近战武器及角色。
  * **`packet_favoritemanager.cpp`**：完善 `UserItemSet` 数据包解析与持久化存储，将每个组合 Slot 对应的 ItemID 写入数据库；在玩家登录时读取并下发，保证退出登录再登录后组合道具完整保留。

---

## 4. 创建房间模式/地图切换防闪退修复

### 4.1 问题表现
* 创建房间时切换特定游戏模式或地图，会自动跳转到“连续地图”列表并导致客户端崩溃闪退。

### 4.2 逆向定位与修复
* **逆向分析**：
  * 客户端在修改房间设置时发送 `PacketID::Room`（0x41）Subtype 5（`UpdateRoomSettings`），附带 `lowFlag` 与 `highFlag`。
  * 当客户端选择特定模式（如虚拟对手/生化/团队死斗）时，某些标志位（如 `ROOMSETTINGS_LFLAG_UNK80000000` 与 `highFlag` 中的结构体）如果服务端未正确解析或广播了错误的默认值，客户端 VGUI 列表控件解析溢出，触发空指针解引用或模式非法回退。
* **修复内容**：
  * **`packet_roommanager.cpp`**：重构了 `UpdateRoomSettings` 与 `buildRoomSettings`，完整对齐了客户端 32 位低标志与 13 位高标志的全部字段（包括武器限制、冻结时间、友军伤害、语音等），防止因下发畸形房间属性引起客户端逻辑混乱。

---

## 5. 游戏加载中 `#256256M1887[????]_p.BMP` 弹窗根因与修复

### 5.1 问题表现
* 创建房间开始游戏后，加载进度条反复弹出关于 `#256256M1887[????]_p.BMP` 的对话框，必须手动连续点击数次“确定”后才能进入游戏。

### 5.2 逆向与底层根因剖析
1. **Windows 全局代码页冲突**：
   * 宿主 Windows 开启了“Beta: 使用 Unicode UTF-8 提供全球语言支持（ACP: 65001）”。
2. **模型内外部贴图引用格式**：
   * M1887 散弹枪模型文件（`p_m1887.mdl`、`w_m1887.mdl`、`shop\s_weapon\s_m1887.mdl`）内部引用了外部贴图名：
     `b'#256256M1887[\xbc\xa6\xb0\xc7]_p.BMP'`
   * 其中字节 `\xbc\xa6\xb0\xc7` 原为韩文 CP949 编码的 `[샷건]`（Shotgun）。
3. **ANSI API 转换非法字符**：
   * 游戏为 ANSI 32位程序，调用 `fopen` / `CreateFileA` 时，系统将 `\xbc\xa6\xb0\xc7` 转换为非法 UTF-8 字符 `?`。
   * Windows 文件系统对文件名有硬性规范，**严禁包含字符 `?`**。调用 `CreateFileA` 必定返回 `ERROR_INVALID_NAME`（123）。
   * 引擎贴图加载器失败后，直接调用 Windows `MessageBoxA` 弹出缺失贴图通知，由于预载了 `p_`、`w_`、`s_` 三个模型，因此恰好连续弹窗 3 次。
4. **外部贴图资源未就绪**：
   * 客户端实际解包后的武器外置贴图存放在 `cstrike_tmp\models\texture`，而引擎读取路径为 `cstrike\models\texture`。

### 5.3 修复措施
1. **资源部署**：
   * 创建 `cstrike\models\texture` 与 `cstrike_tw\models\texture` 目录，将所有武器贴图复制就位。
2. **MDL 二进制热补丁**：
   * 通过脚本扫描所有模型，对 `p_m1887.mdl`、`w_m1887.mdl`、`s_m1887.mdl` 内部贴图名称进行二进制 Patch：
     * 原字符：`#256256M1887[\xbc\xa6\xb0\xc7]_p.BMP`（24 字节）
     * 替换为：`#256256m1887_p_6.bmp\x00\x00\x00\x00`（24 字节纯 ASCII）
   * 彻底消除路径转换中的非法 `?` 字符，加载过程中不再有任何弹窗。

---

## 6. 房间等待室队伍列表与局内选人出生修复

### 6.1 问题表现
* 玩家创建房间后，等待室右上角仅显示总人数 `(1/10)`；
* “反恐小組 (0)”、“恐怖份子 (0)”、“觀戰者 (0)”三个列表全部为空；
* 进入游戏后，没有阵营，无法弹出角色选择菜单，无法在地图出生。

### 6.2 逆向定位
* **`hw.dll` 与 `GameUI.dll` 逻辑**：
  * 在 `hw.dll` 的 `sub_372ea810`（Case 0 `ReplyCreateAndJoin`）中，循环调用 `sub_372EA670` 解析房间内每个玩家：
    * `userID`（4 字节）
    * `team`（1 字节） -> 写入 `*(user + 376)`
    * `status`（1 字节） -> 写入 `*(user + 372)`
    * `slot`（1 字节） -> 写入 `*(user + 360)`
  * `GameUI.dll` 的 `CGamePlayerManagerDlg::OnUserAdded`（`sub_100A9960`）通过虚表读取 `user->GetTeam()`：
    * `team == 1`：将玩家加入 `TRListPanel`（恐怖份子）；
    * `team == 2`：将玩家加入 `CTListPanel`（反恐小組）；
    * `team == 3`：将玩家加入 `SPListPanel`（觀戰者）；
    * **`team == 0`：完全静默丢弃！** 不会加入任何 UI 列表，导致所有队伍计数均为 `(0)`。
  * 进图时，由于等待室分配的阵营为 `0`，游戏逻辑找不到有效阵营，导致角色选择菜单无法激活。

### 6.3 修复措施
1. **`room/room.h`**：
   * 定义独立命名空间枚举，避免与 `Team` 冲突：
     ```cpp
     namespace RoomTeam {
         enum : unsigned char {
             None = 0,
             Terrorist = 1,
             CounterTerrorist = 2,
             Spectator = 3
         };
     }
     ```
   * 引入结构体 `RoomUser`，管理每个玩家的 `user`、`team`、`status`、`slot`。
2. **`room/room.cpp`**：
   * 在 `AddRoomUser` 中，若玩家未指定阵营：
     * 房主默认分配为 `RoomTeam::Terrorist`（1）；
     * 后续加入的玩家按当前 TR / CT 人数自动平衡分配；
     * 自动从 0~31 分配首个空闲槽位 `slot`。
3. **`managers/packet_roommanager.cpp`**：
   * `buildRoomUserInfo` 正确打包 `team`、`status`、`slot`。
   * `ReplyCreateAndJoin` 与 `UserJoin` 广播玩家真实的队伍与槽位编号。
4. **`managers/packet_hostmanager.cpp`**：
   * 进图建立 `GameMatchUser` 时同步设置 `ru->team`，使玩家在局内拥有合法阵营，角色选择与复活出生恢复正常。

---

## 7. 房间内自由换队（TR / CT / 观战者）协议适配

### 7.1 问题表现
* 玩家在等待室内能正常显示在“恐怖份子”，但点击“隊伍變更”按钮或点击 CT / TR / 觀戰者 上方的切换箭头时，完全没有反应，无法换队。

### 7.2 逆向定位与数据包抓取
1. **服务端日志抓取真实包数据**：
   ```
   [ Info ][ PacketManager ] Client sent packet: 55 15 03 00 41 11 02
   [ Info ][ Packet_RoomManager ] Parsing Packet_Room from user (1, test)
   [ Warn ][ Packet_RoomManager ] User (1, test) has sent unregistered Packet_Room type 17!
   ```
   * `0x41` = `PacketID::Room`（65）
   * **`0x11` = 17**（客户端请求换队的真实 Subtype）
   * `0x02` = 目标队伍（2 为反恐小組，1 为恐怖份子，3 为觀戰者）
2. **原代码失配原因**：
   * 服务端之前将 `ChangeTeam` 声明为 `9`，因此客户端发送 `17` 时触发了 `default` 分支直接丢弃，服务端从未发送广播。
3. **服务端下发广播协议（`TeamChange`）**：
   * 客户端 `hw.dll` 在 `sub_372ea810` 的 **Case 13** 中监听换队广播：
     * 读取 `userID`（4 字节 UInt32 LE）
     * 读取 `newTeam`（1 字节 UInt8）
     * 调用 `sub_372A9580` 通知 `GameUI` 控件，将该玩家即时移动至目标阵营面板。

### 7.3 修复措施
1. **`managers/packet_roommanager.h`**：
   * 正确将 `ChangeTeam` 设为 `17`，保留 `ChangeTeam_Old = 9`；
   * 定义 `TeamChange = 13`。
2. **`managers/packet_roommanager.cpp`**：
   * 在 `ParsePacket_Room` 的 switch 中增加 `case Packet_RoomType::ChangeTeam`（17）。
   * `parsePacket_Room_ChangeTeam`：读取 `targetTeam`，调用 `room->SetUserTeam(user, targetTeam)`。
   * 向房间内所有成员广播 `TeamChange`（Subtype 13）：
     ```cpp
     packet->WriteUInt8(Packet_RoomType::TeamChange); // 13
     packet->WriteUInt32_LE(userID);
     packet->WriteUInt8(team);
     ```
3. **`managers/packet_hostmanager.cpp`**：
   * 放宽 `parsePacket_Host_SelectTeam` 的玩家状态判断（支持 `InRoom` 与 `InGame`），避免刚进图选队请求被拒绝。

---

## 8. 聊天系统与全服广播（ServerYell）协议适配与字体颜色逆向

### 8.1 问题表现与需求
* 玩家在聊天框发送管理命令后，输入的文本被静默拦截，缺乏命令回显；
* 系统反馈文字（`SendNotice`）颜色在某些界面显示偏蓝或白，用户希望能显示为更醒目的黄色/金色；
* 客户端大厅曾出现 `#CSO_ServerYe` 未格式化字串提示。

### 8.2 逆向定位（hw.dll sub_37225E80）
* 通过 IDA 对 `hw.dll` 的聊天渲染函数 `sub_37225E80` 及颜色表进行逆向：
  * **ChannelChat（Type 1，大厅频道聊天）**：硬编码颜色为 `0xFFE7E7E7`（浅灰白色），无前缀图标；
  * **RoomChat（Type 2，等待室/局内聊天）**：硬编码颜色为 `0xFFF0BC`（淡米黄色）；
  * **ServerYell（Type 6，全服大喇叭广播）**：硬编码颜色为 `0xFF6C38`（亮金色/橘黄色），客户端原生支持全服悬浮广播与醒目气泡展示。
* 原服务端仅支持普通单播提示，缺少对广播协议（Packet 67, Type 6）的支持。

### 8.3 修复措施
1. **`packet_umsgmanager.h`**：
   * 定义 `Packet_UMsgType::ServerYell = 6`。
2. **`packet_umsgmanager.cpp`**：
   * 增加 `parsePacket_UMsg_ServerYell`，解析客户端发送的大喇叭包并全服广播；
   * 命令回显：管理指令（如 `/additem`, `/addallitem`, `/resetinv`）在被拦截后，服务端先回显用户所发的原始消息，再下发执行反馈；
   * 修复了字串前缀，规范化使用官方本地化词条。

---

## 9. 官方繁中物品全集（631项）与动态加载校验器（ItemDefinitionManager）

### 9.1 需求与挑战
* 客户端拥有 600+ 武器、角色与道具，服务端缺乏完整的物品定义与分类映射；
* 批量添加命令若盲目遍历 ID，会误加无效或废弃物品（如 `6008`, `6009` Invalid Item Table Record，导致客户端点击崩溃）；
* 需要确保服务端使用的 `item.csv` 与目标 2013 客服端（Exe build: Jun 4 2013）完全匹配。

### 9.2 实现与修复
1. **全集文档 `ITEM_LIST.md` 生成**：
   * 结合 `cstrike_tw\resource\cso_tw.txt` 官方繁中本地化文件与 `item.csv`；
   * 完整提取 11 大分类、共 631 件物品道具的官方繁中名称、代码、Class 类别、期限/数量属性，并提供快捷添加示例命令。
2. **`ItemDefinitionManager` 模组实装**：
   * 新增 `managers/itemdefinitionmanager.h` 与 `.cpp`；
   * 支持服务端启动时动态读取 `configs/item.csv`；
   * **SHA-256 硬编码哈希校验**：
     * 哈希值：`b21036850d713889e6bb39ac4b9580f5ac291a53e6b965700bdf47cb8ecfc711`；
     * 启动时自动比对校验，若文件被篡改或版本不匹配，控制台输出清晰警告。
3. **分类白名单与黑名单过滤**：
   * 提供分类 ID 映射（1: 手枪, 2: 霰彈槍, 3: 衝鋒槍, 4: 突擊步槍, 5: 狙擊槍, 6: 機關槍, 7: 恐怖份子, 8: 反恐小組, 9: 裝飾, 10: 裝備/服飾, 11: 近戰武器）；
   * 支持配置文件排除未开放或特殊道具（如自动排除 6008、6009、9997~9999 纹身等）。

---

## 10. 管理员聊天指令体系（/additem, /addallitem, 分类过滤与黑名单）

### 10.1 功能实现
1. **`/additem <id> [天数/数量]`**：
   * 若未指定数量/天数：消耗品默认赠送 1 个；期限装备默认赠送永久；
   * 若已拥有该期限武器且追加天数，自动在原到期时间上顺延累加，已有永久则自动跳过；
   * 支持在 `serverconfig.json` 中配置 `BlockedItemIDs` 黑名单，防止误刷特定道具。
2. **`/addallitem [分类ID] [天数]`**：
   * 支持按分类（1~11）批量添加，省略分类时默认添加全部；
   * 期限武器支持批量天数叠加与升级为永久；
   * 支持配置 `AddAllItemAllowedCategories` 与 `AddAllItemExcludedCategories`；
   * 反馈信息清晰汇总：新增数量、延长数量、升级为永久数量及已跳过数量。
3. **服饰与装备（Class 58~149）适配**：
   * 在 `packet_itemmanager.cpp` 中适配头饰/服饰槽位，保证装备与卸除状态能正确持久化与同步。

---

## 11. 仓库一键重置脚本与客户端原生即时刷新（Delta Purge / status=0）

### 11.1 问题表现
* 玩家使用 `/resetinv` 或 `/resetitem` 重置个人仓库后，必须重新登录游戏，仓库界面才会恢复成 107 件初始装备；若不重新登录，先前添加的多余武器依然残留在界面上。

### 11.2 逆向定位（hw.dll sub_372D33B0 与 IInventory[26]）
* 深入逆向分析客户端 `Packet_Inventory`（Packet 154）解析函数 `sub_372D33B0`：
  * Packet 154 是一个**增量更新（Delta Update）**协议；
  * 每个槽位拥有状态字节 `status`：
    * `status == 1`：客户端读取 18 字节道具属性并插入/更新槽位；
    * `status == 0`：客户端仅读取 `slotID` 和 `status`，**不读取后续字节**，并直接触发 `IInventory::RemoveItem(slotID)`，将该道具从本地内存释放！
  * 循环结束后，统一触发 `IInventory::NotifyListeners()`，通知 `CInventoryDlgVer2` 重绘仓库 UI；
  * **原根因**：原服务端执行 `/resetinv` 时，仅向客户端发送了 107 件初始物品（`status = 1`）。由于没有对旧的 108~460+ 槽位发送 `status = 0`，客户端内存中依然保留着这些对象，只有在重新登录调用 `IInventory::Clear()` 时才被清理。

### 11.3 修复措施
1. **`managers/packetmanager.h` / `.cpp`**：
   * 扩展 `SendPacket_Inventory`，支持传入 `deletedSlots` 列表；
   * 将需要移除的槽位打包为 `WriteUInt16_LE(slotID)` + `WriteUInt8(0)` 发送。
2. **`managers/packet_umsgmanager.cpp`**：
   * 在执行 `/resetinv` 重置前，记录用户先前拥有的全部槽位；
   * 重置后，自动比对将所有多余的槽位（以及最大槽位范围内的空闲槽位）标记为 `status = 0` 一并下发。
3. **一键重置测试脚本**：
   * 编写 `reset_inventory.ps1` 与 `reset_inventory.bat`，可随时一键将指定账号（默认 test / userID 1）还原为官方 107 件初始装备，方便反复测试。
4. **效果**：
   * 在游戏内聊天框输入 `/resetinv` 后，**仓库内的非预设物品即时消失，界面瞬间刷新恢复官方预设，完全无需重登游戏**！

---

## 12. 客户端崩溃（COpenglDetect / CrashReport.dmp）根因定位与协议防下溢修复

### 12.1 崩溃现象
* 游戏进入大厅或房间运行约 1~2 分钟后莫名其妙闪退，生成 `Bin\CrashReport.dmp`。

### 12.2 崩溃转储分析与逆向排查
* **异常信息**：`0xC0000005`（Access Violation），地址 `hw.dll + 0xBEAEF`（指令 `mov eax, [eax]`，读取地址 `0x0000003C`，即 `NULL + 0x3C`）。
* **崩溃线程**：Thread 3（TID 7004），属于 `COpenglDetect`（继承自 `Cdisasm` 内建反汇编引擎）。
* **根本诱因**：
  * CSO 2013 引擎包含一个后台防透视扫描线程 `COpenglDetect`，定期反汇编内存中的 OpenGL 函数以检测 Hook。
  * 用户系统配有 NVIDIA 独立显卡，自动加载了 `nvspcap.dll`（**NVIDIA GeForce Experience 游戏内覆盖 / ShadowPlay**）；
  * `nvspcap.dll` 对 OpenGL 进行了挂钩，而 2013 年的老旧 `Cdisasm` 引擎无法识别现代 NVIDIA 驱动生成的跳板（Trampoline）指令，导致反汇编计算出空指针 `0x3C`，从而引发闪退；
  * **解决方案**：在 GeForce Experience 设置中关闭**「遊戲內重疊 (In-Game Overlay)」**或关闭第三方录屏/监控工具（如 Discord 覆盖、RTSS），即可彻底杜绝该崩溃。

### 12.3 协议防下溢修复（Packet 66 ClientCheck）
* 在客户端 `Error.log` 中捕获到：`PacketReader Error - curpacketID : 66[51], len : 8`。
* 逆向 `sub_372CCF30`（`Packet_ClientCheck::Read`）发现客户端强制要求读取 8 字节；原服务端仅发送了 0 字节空包，导致读取下溢。
* 在 `managers/packet_clientcheckmanager.cpp` 中增加 `packet->WriteUInt64_LE(0)`，补齐 8 字节校验数据，消除了该底层协议报错。

---

## 13. 修改文件清单与影响范围

| 文件相对路径 | 修改模块 | 主要改动说明 |
| :--- | :--- | :--- |
| `managers/packetmanager.h` / `.cpp` | 基础网络协议 | `SendPacket_Inventory` 增加 `deletedSlots` 支持（`status = 0` 原生槽位删除），实现实时仓库更新；调整 `BuildUserCharacter` 打包。 |
| `managers/packet_umsgmanager.h` / `.cpp` | 聊天与管理指令 | 增加 `ServerYell`（6）全服大喇叭广播；实装 `/additem`、`/addallitem`、`/resetinv`；指令回显与精准反馈。 |
| `managers/itemdefinitionmanager.h` / `.cpp` | 道具定义管理 | 动态解析 `item.csv`，SHA-256 哈希硬编码校验，分类过滤与黑名单控制。 |
| `managers/packet_clientcheckmanager.cpp` | 客户端心跳校验 | `SendPacket_ClientCheck` 发送 8 字节数据，修复客户端读取下溢与 Error.log 报错。 |
| `managers/databasemanager.h` / `.cpp` | 数据库交互 | 增加 `ResetUserInventory`，实现原子化清空与官方 107 件初始装备重建；角色战绩保存。 |
| `serverconfig.h` / `.cpp` | 配置文件 | 扩展物品黑名单与分类白名单/排除列表配置项解析。 |
| `room/room.h` / `room/room.cpp` | 房间模型与逻辑 | 引入 `RoomTeam` 命名空间枚举，定义 `RoomUser` 结构，补充换队/准备等接口与阵营自动分配。 |
| `managers/packet_roommanager.h` / `.cpp` | 房间通信 | 打包真实阵营与槽位，处理 `ChangeTeam`（17）与 `UserReady`（3），广播 `TeamChange`（13）。 |
| `managers/packet_hostmanager.h` / `.cpp` | 主机/局内通信 | 继承房间阵营至 `GameMatchUser`，激活主机时广播 `HostJoin`，放宽 `SelectTeam` 状态检查。 |
| `managers/packet_itemmanager.h` / `.cpp` | 道具背包 | 修复各槽位武器装备独立性，防止覆盖失效；适配服饰/头饰槽位。 |
| `managers/packet_favoritemanager.h` / `.cpp` | 预设组合 | 持久化玩家武器组合（A/B/C）到数据库，解决重新登录组合丢失问题。 |
| `configs/item.csv` / `bin/configs/item.csv` | 游戏数据 | 官方 631 件物品属性数据表，供服务端动态解析与校准。 |
| `ITEM_LIST.md` | 物品全集文档 | 包含 11 大类、631 项道具官方繁中对照与快速添加指令表。 |
| `reset_inventory.ps1` / `.bat` | 测试辅助工具 | 一键重置测试账号仓库为官方 107 件初始装备脚本。 |
| `CHANGELOG_AND_FIXES.md` | 核心工程文档 | 全面记录项目架构、逆向分析、BUG 修复与运维排错指南。 |

---
*文档更新时间：2026-10-03*  
*对应版本：cso-master-server (Release/x64) - CSO TW 2013 / CB2 深度适配版*
