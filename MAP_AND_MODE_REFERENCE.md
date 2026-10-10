# CSO 模式、地圖與遊戲事件類型全覽對照表

此文件由伺服端根據 `configs/gamemodelist.csv`、`configs/maplist.csv`、`cstrike_tw/resource/cso_tw.txt` 與任務規則引擎自動彙整，方便在 `configs/quest.json` 中配置任務判定條件。

---

## 1. 遊戲事件類型對照表 (Events)

在 `quest.json` 的 `conditions` 中，透過 `"event": "事件名稱"` 指定該任務條件要監聽的遊戲事件：

| 事件名稱 (`event`) | 事件說明 | 觸發時機 / 底層封包 | 支援的附加過濾參數 | 典型任務範例 |
| :--- | :--- | :--- | :--- | :--- |
| **`kill`** | 擊殺目標（預設） | 玩家擊殺任何目標 (`Packet_Host PlayerKilled`) | `require_human`, `require_bot`, `require_zombie`, `require_headshot`, `weapons`, `weapon_category`, `consecutive`, `combo_in_life`, `streak_in_life` | 殺手條件 (爆頭)、以一擋百 (連殺)、手榴彈專家、近身搏鬥 |
| **`round_win`** | 回合勝利 | 回合勝負揭曉 (`Packet_Host RoundEnd`) | `allowed_modes`, `allowed_maps`, `min_players` | 每日1 榮耀的勝利 (取得3回合勝利)、每日10 英雄勝利 |
| **`match_win`** | 整場比賽獲勝 | 比賽結算且本陣營勝出 (`GameMatch EndMatch`) | `allowed_modes`, `allowed_maps`, `min_players` | 每週501 團結致勝 (一般軍團戰7勝)、每週502 團死5勝、榮譽2026 血戰王者 |
| **`match_end`** | 比賽完成/結束 | 比賽結算 (不論輸贏) | `allowed_modes`, `allowed_maps`, `min_players` | 災厄之章通關任務、完成對局任務 |
| **`c4_any`** | C4 安裝或拆除 | 成功裝彈或拆彈皆可 | `allowed_maps: ["de_"]`, `min_players` | 每日4 爆破專家 (安裝或拆除3次C4) |
| **`c4_plant`** | 安裝 C4 炸彈 | 恐怖份子成功安裝 C4 | `allowed_maps: ["de_"]`, `min_players` | 爆破專用特化任務 |
| **`c4_defuse`** | 拆除 C4 炸彈 | 反恐小組成功拆除 C4 | `allowed_maps: ["de_"]`, `min_players` | 拆彈專家任務 |
| **`hostage_rescue`** | 營救人質 | 成功引導人質脫離危險 | `allowed_maps: ["cs_"]`, `min_players` | 每日5 人質危機 (拯救5名人質) |
| **`morale_boost`** | 士氣提升滿級 | 殭屍英雄士氣達到 LV.10 | `allowed_modes: [14]`, `min_players` | 每日11 士氣提升 (將士氣提升至LV.10 5次) |
| **`supply_box`** | 拾取空投補給箱 | 在戰局中拾獲空投補給箱 | `allowed_modes: [9, 14]`, `min_players` | 每日14 奪回補給箱 (領取補給箱10次) |
| **`wall_upgrade`** | 升級防線/防禦牆 | 防守模式花費資金升級防護牆 | `allowed_modes: [15]`, `allowed_maps: ["zs_defense"]` | 每日17 銅牆鐵壁 (完成防禦牆升級5次) |
| **`boss_kill`** | 擊殺特定 Boss | 災厄/戰神之章擊敗首領怪 | `bosses: ["starman", "titan", "phobos", "oberon", "angra"]` | 每日16 殘暴達叔、每日18 獵殺泰坦、榮譽BOSS毀滅者 |
| **`skill_kill`** | 特殊技能擊殺 | 啟用特殊武裝技能取得擊殺 | `allowed_modes: [17]` | 每日19 空中轟炸 (以空中轟炸技能擊殺50人) |
| **`point_capture`** | 佔領據點 | 機甲爭霸/佔領模式奪取據點 | `allowed_modes: [18]` | 每日21 占領據點、榮譽2031 征服者 |
| **`play_time`** | 累計遊戲時長 | 戰局結算時結算遊玩小時數 | `allowed_modes`, `min_players` | 每週503 殭屍模式遊玩5小時、每週504 災厄遊玩5小時 |
| **`soccer_goal`** | 足球射門得分 | 足球模式射門進球 | `allowed_modes: [11]`, `allowed_maps: ["sc_"]` | 每日20 全能戰士 (射門得分3次) |
| **`soccer_assist`** | 足球傳球助攻 | 足球模式傳球助攻得分 | `allowed_modes: [11]`, `allowed_maps: ["sc_"]` | 每日20 全能戰士 (助攻3次) |
| **`soccer_save`** | 守門防守成功 | 門前阻擋對方射門得分 | `allowed_modes: [11]`, `allowed_maps: ["sc_"]` | 每日20 全能戰士 (防守射門3次) |
| **`any`** | 任何遊戲事件 | 只要有任何合法事件即匹配 | 任意條件 | 自訂特殊事件 |

---

## 2. 遊戲模式對照表 (Game Modes)

可填寫在 `"allowed_modes"` 或 `"disallowed_modes"` 中：

| 模式 ID (`mode_id`) | 內部 Token | 繁體中文名稱 | quest.json 支援別名 |
| :---: | :--- | :--- | :--- |
| **0** | `#CSO_GameMode_Original` | **一般** | `0, "original", "orig", "pvp"` |
| **1** | `#CSO_GameMode_DeathMatch` | **死鬥** | `1, "deathmatch", "dm", "pvp"` |
| **2** | `#CSO_GameMode_TeamDeathMatch` | **團隊死鬥** | `2, "teamdeathmatch", "tdm", "pvp"` |
| **3** | `#CSO_GameMode_Bot` | **一般 (虛擬對手)** | `3, "bot_original", "bot"` |
| **4** | `#CSO_GameMode_BotDM` | **死鬥 (虛擬對手)** | `4, "bot_deathmatch", "botdm", "bot"` |
| **5** | `#CSO_GameMode_BotTDM` | **團隊死鬥 (虛擬對手)** | `5, "bot_teamdeathmatch", "bottdm", "bot"` |
| **6** | `#CSO_GameMode_Official` | **正規賽事** | `6, "official"` |
| **7** | `#CSO_GameMode_Official_TieBreak`| **正規延長賽** | `7, "official_tiebreak"` |
| **8** | `#CSO_GameMode_Zombi` | **殭屍經典** | `8, "zombie", "zombi", "zombie_classic"` |
| **9** | `#CSO_GameMode_Zombi_Expand` | **殭屍異變** | `9, "zombie_mutation", "zombie"` |
| **10** | `#CSO_GameMode_Hidden` | **幽靈模式** | `10, "hidden"` |
| **11** | `#CSO_GameMode_Zombi_Team` | **大亂鬥** | `11, "fun"` |
| **12** | `#CSO_GameMode_Challenge` | **挑戰模式** | `12, "challenge"` |
| **13** | `#CSO_GameMode_Zombi_Team_Ann` | **大亂鬥殲滅戰** | `13` |
| **14** | `#CSO_GameMode_Zombi_3` | **殭屍英雄** | `14, "zombie_hero", "zombie"` |
| **15** | `#CSO_GameMode_Scenario` | **災厄之章** | `15, "scenario", "zombie_scenario"` |
| **16** | `#CSO_GameMode_Soccer` | **足球模式** | `16, "soccer"` |
| **17** | `#CSO_GameMode_Human_Scenario` | **戰神之章** | `17, "human_scenario"` |
| **18** | `#CSO_GameMode_PVPVE` | **機甲爭霸 / 佔領** | `18, "pvpve"` |
| **19** | `#CSO_GameMode_TDM_Item` | **團隊死鬥(道具)** | `19` |
| **20** | `#CSO_GameMode_Zombi_Escape` | **殭屍逃跑** | `20, "zombie_escape"` |
| **21** | `#CSO_GameMode_Bazooka` | **火箭筒模式** | `21, "bazooka"` |
| **22** | `#CSO_GameMode_GunDeath` | **槍王之王** | `22, "gundeath"` |
| **23** | `#CSO_GameMode_LightOri` | **輕量一般** | `23, "lightori"` |
| **24** | `#CSO_GameMode_BotZombie` | **虛擬對手(殭屍)** | `24, "botzombie"` |
| **25** | `#CSO_GameMode_BattleRush` | **戰鬥狂潮** | `25, "battlerush"` |
| **26** | `#CSO_GameMode_ZombieShelter` | **夜襲生存戰** | `26, "zombieshelter"` |

---

## 3. 常用地圖前綴與重點地圖對照

可填寫在 `"allowed_maps"` 或 `"disallowed_maps"` 中：

### 常用地圖類別前綴
* `["de_"]`：全體拆除炸彈地圖（例：`de_dust2`, `de_inferno`, `de_aztec`, `de_nuke`, `de_train` 等）
* `["cs_"]`：全體人質救援地圖（例：`cs_italy`, `cs_office`, `cs_assault`, `cs_militia` 等）
* `["zs_"]`：全體災厄之章地圖（例：`zs_lostcity`, `zs_defense`, `zs_trap`, `zs_culvert` 等）
* `["hs_"]`：全體戰神之章地圖（例：`hs_desertstorm`, `hs_hellfire`, `hs_blaze` 等）
* `["zm_"]` / `["ZM_"]`：殭屍專用地圖（例：`ZM_abyss`, `zm_requiem`, `zm_origin` 等）
* `["dm_"]` / `["DM_"]`：死鬥專用地圖（例：`dm_tunnel`, `DM_dust2`, `DM_gallery` 等）
* `["ze_"]`：殭屍逃跑地圖（例：`ze_bloodcastle`, `ze_skyline`, `ze_venice` 等）
* `["sc_"]`：足球地圖（例：`sc_soccer01`, `sc_cage` 等）

### 重點任務專用地圖 ID 與檔名
* **`5` (`de_dust2`)**：沙漠遺跡2(Dust2)
* **`7` (`de_inferno`)**：城市煉獄(Inferno)
* **`19` (`cs_italy`)**：義大利巷戰(Italy)
* **`22` (`cs_assault`)**：突擊安全庫(Assault)
* **`74` (`zs_lostcity`)** / **`79`** / **`86`**：失落的城市(Lost City) —— 每日16 殘暴達叔
* **`76` (`zs_trap`)**：沉默的陷阱(Trap) —— 每日18 異形泰坦
* **`112` (`zs_culvert`)**：地下水道(Culvert)
* **`126` (`zs_defense`)**：命運的關卡(Defense) —— 每日17 銅牆鐵壁
