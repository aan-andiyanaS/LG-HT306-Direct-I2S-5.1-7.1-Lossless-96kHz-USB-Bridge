@echo off
echo ============================================================
echo   LG HT306 Audio Bridge - Sinkronisasi Penamaan Taskbar
echo ============================================================
echo.
echo [1/2] Mendeteksi dan memperbarui registri audio Windows (KEY_SET_VALUE)...

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$base = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render', $false);" ^
  "if ($base) {" ^
  "  foreach ($sub in $base.GetSubKeyNames()) {" ^
  "    $pPath = 'SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\' + $sub + '\Properties';" ^
  "    $pRead = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($pPath, $false);" ^
  "    if ($pRead) {" ^
  "      $desc = [string]$pRead.GetValue('{a45c254e-df1c-4efd-8020-67d146a850e0},2');" ^
  "      $ifname = [string]$pRead.GetValue('{b3f8fa53-0004-438e-9003-51a46e139bfc},6');" ^
  "      $hwid = [string]$pRead.GetValue('{a8b865dd-2e3d-4094-ad97-e593a70c75d6},8');" ^
  "      $subtype = [string]$pRead.GetValue('{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},8');" ^
  "      $pRead.Close();" ^
  "      if ($hwid -like '*VBAudioVACWDM*' -or $desc -like '*CABLE*' -or $ifname -like '*VB-Audio*') {" ^
  "        if ($desc -like '*16ch*' -or $ifname -like '*16ch*' -or $subtype -like '*DFF21FE3*') {" ^
  "          $sWrite = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\' + $sub, [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree, [System.Security.AccessControl.RegistryRights]::SetValue);" ^
  "          if ($sWrite) { $sWrite.SetValue('DeviceState', 0x10000002, [Microsoft.Win32.RegistryValueKind]::DWord); $sWrite.Close(); Write-Host '  [-] Menonaktifkan CABLE In 16ch' -ForegroundColor Yellow }" ^
  "        } else {" ^
  "          $pWrite = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey($pPath, [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree, [System.Security.AccessControl.RegistryRights]::SetValue);" ^
  "          if ($pWrite) {" ^
  "            $pWrite.SetValue('{a45c254e-df1c-4efd-8020-67d146a850e0},2', 'LG HT306 Home Theater', [Microsoft.Win32.RegistryValueKind]::String);" ^
  "            $pWrite.SetValue('{b3f8fa53-0004-438e-9003-51a46e139bfc},6', '5.1 Surround', [Microsoft.Win32.RegistryValueKind]::String);" ^
  "            $pWrite.SetValue('{259abffc-50a7-47ce-af08-68c9a7d73366},12', '%windir%\system32\mmres.dll,-3010', [Microsoft.Win32.RegistryValueKind]::String);" ^
  "            $pWrite.Close();" ^
  "            Write-Host '  [+] Mengubah nama CABLE Input -> LG HT306 Home Theater (5.1 Surround)' -ForegroundColor Green;" ^
  "          }" ^
  "        }" ^
  "      }" ^
  "    }" ^
  "  }" ^
  "  $base.Close();" ^
  "}"

echo [2/2] Selesai! Perangkat 16ch telah disembunyikan dan nama Taskbar menjadi LG HT306 Home Theater (5.1 Surround).
echo.
timeout /t 3
