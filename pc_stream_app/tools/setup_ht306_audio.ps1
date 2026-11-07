# Setup & Auto-Rename LG HT306 Audio Endpoint
$ErrorActionPreference = 'SilentlyContinue'

$regPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render"
Get-ChildItem $regPath | ForEach-Object {
    $subKey = $_.PSPath
    $propKey = Join-Path $subKey "Properties"
    $props = Get-ItemProperty -Path $propKey -ErrorAction SilentlyContinue
    if ($props) {
        $desc = [string]$props.'{a45c254e-df1c-4efd-8020-67d146a850e0},2'
        $ifname = [string]$props.'{b3f8fa53-0004-438e-9003-51a46e139bfc},6'
        
        # 1. Disable CABLE in 16ch
        if ($desc -like "*16ch*" -or $ifname -like "*16ch*") {
            Set-ItemProperty -Path $subKey -Name "DeviceState" -Value 2 -Type DWord
        }
        # 2. Rename CABLE Input to LG HT306 Home Theater
        elseif ($desc -like "*CABLE*" -or $ifname -like "*VB-Audio*" -or $desc -like "*HT306*") {
            Set-ItemProperty -Path $propKey -Name '{a45c254e-df1c-4efd-8020-67d146a850e0},2' -Value "LG HT306 Home Theater"
            Set-ItemProperty -Path $propKey -Name '{b3f8fa53-0004-438e-9003-51a46e139bfc},6' -Value "LG HT306 Home Theater"
            Set-ItemProperty -Path $subKey -Name "DeviceState" -Value 1 -Type DWord
        }
    }
}

# Restart Windows AudioEndpointBuilder service to refresh Taskbar cache instantly
Restart-Service AudioEndpointBuilder -Force
