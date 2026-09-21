param(
  [string]$Root = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
$rootPath = (Resolve-Path -LiteralPath $Root).Path
$utf8 = [System.Text.Encoding]::UTF8
$violations = [System.Collections.Generic.List[string]]::new()

$tracked = & git -C $rootPath -c core.quotepath=false ls-files
foreach ($relativePath in $tracked) {
  $normalized = $relativePath.Replace('\', '/')
  if ($normalized.StartsWith('vendor/')) { continue }
  $extension = [System.IO.Path]::GetExtension($normalized).ToLowerInvariant()
  $isCmake = ([System.IO.Path]::GetFileName($normalized) -eq 'CMakeLists.txt') -or
             ($extension -eq '.cmake')
  $isSource = $extension -in @('.h', '.hpp', '.cpp')
  $isResource = $extension -eq '.rc'
  if (-not ($isCmake -or $isSource -or $isResource)) { continue }

  $path = Join-Path $rootPath $relativePath
  $bytes = [System.IO.File]::ReadAllBytes($path)
  $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and
            $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
  $offset = if ($hasBom) { 3 } else { 0 }
  $text = $utf8.GetString($bytes, $offset, $bytes.Length - $offset)

  if ($isSource -and -not $hasBom) {
    $violations.Add("${normalized}: source file must use UTF-8 BOM")
  }
  if (($isCmake -or $isResource) -and $hasBom) {
    $violations.Add("${normalized}: CMake/RC file must use UTF-8 without BOM")
  }
  if ([regex]::IsMatch($text, "(?<!`r)`n")) {
    $violations.Add("${normalized}: line ending must be CRLF")
  }
}

if ($violations.Count -gt 0) {
  $violations | ForEach-Object { Write-Error $_ }
  exit 1
}

Write-Host 'Text encoding and CRLF checks passed.'
