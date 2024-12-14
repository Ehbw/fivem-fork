<#
.Synopsis
Downloads the 32-bit CEF dependency to vendor/cef32/.
#>
$WorkDir = "$PSScriptRoot\..\..\..\"
$SaveDir = "$WorkDir\code\build\"
if (!(Test-Path $SaveDir)) { New-Item -ItemType Directory -Force $SaveDir | Out-Null }
$CefName = (Get-Content -Encoding ascii $WorkDir\vendor\cef32\cef_build_name.txt).Trim()
$uri = 'https://example.com/string with spaces'
[uri]::EscapeUriString($CefName)
if (!(Test-Path "$SaveDir\$CefName.tar.bz2")) {
	curl.exe --create-dirs -Lo "$SaveDir\$CefName.tar.bz2" "https://cef-builds.spotifycdn.com/$CefName.tar.bz2"
}
if (Test-Path $WorkDir\vendor\cef32) {
	Get-ChildItem -Directory $WorkDir\vendor\cef32 | Remove-Item -Recurse -Force
}
& $env:WINDIR\system32\tar.exe -C $WorkDir\vendor\cef32 -xf "$SaveDir\$CefName.tar.bz2"
Copy-Item -Force -Recurse $WorkDir\vendor\cef32\$CefName\* $WorkDir\vendor\cef32\
Remove-Item -Recurse $WorkDir\vendor\cef32\$CefName\