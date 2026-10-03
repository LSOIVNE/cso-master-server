# ==============================================================================
# CSO Master Server - 個人倉庫一鍵重置腳本
# 用法：
#   powershell -ExecutionPolicy Bypass -File .\reset_inventory.ps1
#   powershell -ExecutionPolicy Bypass -File .\reset_inventory.ps1 -UserName test
# ==============================================================================

param (
    [string]$UserName = "test"
)

$ErrorActionPreference = "Stop"

# 定位配置檔路徑
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$configPaths = @(
    (Join-Path $scriptDir "bin\configs\serverconfig.json"),
    (Join-Path $scriptDir "configs\serverconfig.json"),
    (Join-Path $scriptDir "cso-master-server\bin\configs\serverconfig.json"),
    (Join-Path $scriptDir "..\cso-master-server\bin\configs\serverconfig.json"),
    (Join-Path $scriptDir "serverconfig.json")
)

$configFile = $null
foreach ($path in $configPaths) {
    if (Test-Path $path) {
        $configFile = $path
        break
    }
}

if (-not $configFile) {
    Write-Host "[錯誤] 找不到 serverconfig.json 配置檔！" -ForegroundColor Red
    exit 1
}

Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "         CSO 個人倉庫一鍵重置工具 (Default Inventory)     " -ForegroundColor Cyan
Write-Host "==========================================================" -ForegroundColor Cyan

# 讀取 serverconfig.json
try {
    $config = Get-Content $configFile -Raw -Encoding utf8 | ConvertFrom-Json
}
catch {
    Write-Host "[錯誤] 解析 serverconfig.json 失敗：$($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# 取得資料庫配置
$sqlHost = if ($config.SQL.Server) { $config.SQL.Server } else { "localhost" }
$sqlUser = if ($config.SQL.User) { $config.SQL.User } else { "root" }
$sqlPass = if ($config.SQL.Password) { $config.SQL.Password } else { "CsoDev123!" }
$sqlDb   = if ($config.SQL.Database) { $config.SQL.Database } else { "csodatabase" }

$defaultItems = $config.DefaultInventory
if (-not $defaultItems -or $defaultItems.Count -eq 0) {
    Write-Host "[錯誤] serverconfig.json 中未配置 DefaultInventory 列表！" -ForegroundColor Red
    exit 1
}

Write-Host "[資訊] 目標用戶名稱 : $UserName" -ForegroundColor White
Write-Host "[資訊] 資料庫       : $sqlHost / $sqlDb (用戶: $sqlUser)" -ForegroundColor White
Write-Host "[資訊] 預設道具數量 : $($defaultItems.Count) 件" -ForegroundColor White

# 查詢用戶 ID
$mysqlCmd = "mysql"
$mysqlArgs = @(
    "-h", $sqlHost,
    "-u", $sqlUser,
    "-p$sqlPass",
    $sqlDb,
    "-N", "-s",
    "-e", "SELECT userID FROM users WHERE userName = '$UserName';"
)

try {
    $userIDStr = & $mysqlCmd $mysqlArgs
}
catch {
    Write-Host "[錯誤] 無法執行 mysql 命令，請確認已安裝 MySQL 並加入系統 PATH！" -ForegroundColor Red
    exit 1
}

if (-not $userIDStr -or $userIDStr.Trim() -eq "") {
    Write-Host "[錯誤] 資料庫中找不到帳號名為 [$UserName] 的玩家！" -ForegroundColor Red
    exit 1
}

$userID = [int]$userIDStr.Trim()
Write-Host "[資訊] 找到用戶 ID  : $userID" -ForegroundColor Green

# 生成重置 SQL
$now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$sql = "DELETE FROM user_inventory WHERE userID = $userID;`n"
$slotID = 1

foreach ($itemId in $defaultItems) {
    # 預設裝備角色與紋身
    $inUse = 0
    if ($itemId -eq 40 -or $itemId -eq 49 -or $itemId -eq 373) {
        $inUse = 1
    }
    $sql += "INSERT INTO user_inventory (userID, slotID, itemID, count, activeFlag, inUse, obtainTime, expirationTime, enhanceBits) VALUES ($userID, $slotID, $itemId, 1, 1, $inUse, $now, 0, 0);`n"
    $slotID++
}

# 執行 SQL 重置
$tempSql = Join-Path $scriptDir "temp_reset_inv.sql"
try {
    $sql | Set-Content -Path $tempSql -Encoding utf8
    & $mysqlCmd "-h" $sqlHost "-u" $sqlUser "-p$sqlPass" $sqlDb -e "source $tempSql"
}
finally {
    if (Test-Path $tempSql) {
        Remove-Item $tempSql -Force
    }
}

Write-Host "----------------------------------------------------------" -ForegroundColor DarkGray
Write-Host "[成功] 用戶 [$UserName] (ID: $userID) 的倉庫已成功還原為預設值！" -ForegroundColor Green
Write-Host "[成功] 共寫入 $($defaultItems.Count) 件初始角色與裝備。" -ForegroundColor Green
Write-Host "[提醒] 若您當前正在遊戲中，請重新登入或重進大廳以刷新倉庫。" -ForegroundColor Yellow
Write-Host "==========================================================" -ForegroundColor Cyan
