param(
  [Parameter(Mandatory = $true)]
  [string]$ExtensionId
)

$hostExe = Join-Path $PSScriptRoot '..\qingying_chrome_native_host.exe'
if (-not (Test-Path -LiteralPath $hostExe)) {
  $hostExe = Join-Path $PSScriptRoot '..\..\build\bin\Release\qingying_chrome_native_host.exe'
}
$hostExe = [System.IO.Path]::GetFullPath($hostExe)
if (-not (Test-Path -LiteralPath $hostExe)) {
  throw "Native host not found: $hostExe. Build the Release target first."
}
$manifestDir = Join-Path (Split-Path -Parent $hostExe) 'chrome-native-host'
New-Item -ItemType Directory -Force -Path $manifestDir | Out-Null
$manifestPath = Join-Path $manifestDir 'net.qingying.chrome_longshot.json'
$manifest = @{
  name = 'net.qingying.chrome_longshot'
  description = 'QingYing Chrome long-shot bridge'
  path = $hostExe
  type = 'stdio'
  allowed_origins = @("chrome-extension://$ExtensionId/")
} | ConvertTo-Json -Depth 3
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($manifestPath, $manifest, $utf8NoBom)
$registryPath = 'HKCU:\Software\Google\Chrome\NativeMessagingHosts\net.qingying.chrome_longshot'
New-Item -Force -Path $registryPath | Out-Null
Set-ItemProperty -Path $registryPath -Name '(Default)' -Value $manifestPath
Write-Host "Installed QingYing native host for extension $ExtensionId"
