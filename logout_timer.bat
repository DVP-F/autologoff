:: Powershell script
REM set "script=$IdleThresholdMinutes=5; $CheckIntervalSeconds=30; Add-Type 'using System; using System.Runtime.InteropServices; public static class IdleTime { [StructLayout(LayoutKind.Sequential)] struct LASTINPUTINFO { public uint cbSize; public uint dwTime; } [DllImport(\"user32.dll\")] static extern bool GetLastInputInfo(ref LASTINPUTINFO plii); public static uint GetIdleTime() { LASTINPUTINFO lastInputInfo = new LASTINPUTINFO(); lastInputInfo.cbSize = (uint)System.Runtime.InteropServices.Marshal.SizeOf(lastInputInfo); GetLastInputInfo(ref lastInputInfo); return ((uint)Environment.TickCount - lastInputInfo.dwTime); } }'; while ($true) { $idleTimeMs=[IdleTime]::GetIdleTime(); $idleMinutes=$idleTimeMs/1000/60; if ($idleMinutes -ge $IdleThresholdMinutes) { shutdown.exe /l; break }; Start-Sleep -Seconds $CheckIntervalSeconds }"
:: Stupid but this uses C to check inactivity
REM powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "%script%"

:: Options:

shutdown /l /f
logoff ID | REM get ID with `query user`
tsdiscon
taskkill /F /IM explorer.exe


