# Q62: show a Windows toast with one action button.
#
# The tray calls this with environment variables (avoids quoting problems with
# Chinese text and file paths):
#   DROPBRIDGE_TOAST_TITLE     e.g. 收到照片
#   DROPBRIDGE_TOAST_MESSAGE   e.g. 点击下方按钮把图片复制到剪贴板
#   DROPBRIDGE_TOAST_BUTTON    e.g. 复制到剪贴板
#   DROPBRIDGE_TOAST_ARGS      dropbridge://copy?file=...
#   DROPBRIDGE_TOAST_IMAGE     optional absolute path of a picture
#   DROPBRIDGE_TOAST_APPID     AUMID, defaults to DropBridge.Tray
#
# Windows PowerShell 5.1 is used because it can load the WinRT toast types
# in-box; no Windows SDK and nothing outside this folder is needed.
# Exit codes: 0 shown, 2 WinRT unavailable, 3 missing input, 1 anything else.
$ErrorActionPreference = 'Stop'

$appId = $env:DROPBRIDGE_TOAST_APPID
if ([string]::IsNullOrWhiteSpace($appId)) { $appId = 'DropBridge.Tray' }
$title = $env:DROPBRIDGE_TOAST_TITLE
$message = $env:DROPBRIDGE_TOAST_MESSAGE
$button = $env:DROPBRIDGE_TOAST_BUTTON
$arguments = $env:DROPBRIDGE_TOAST_ARGS
$image = $env:DROPBRIDGE_TOAST_IMAGE
if ([string]::IsNullOrWhiteSpace($button)) { $button = '复制到剪贴板' }

if ([string]::IsNullOrWhiteSpace($title) -or [string]::IsNullOrWhiteSpace($arguments)) {
    Write-Error 'DROPBRIDGE_TOAST_TITLE and DROPBRIDGE_TOAST_ARGS are required'
    exit 3
}

function ConvertTo-XmlText([string]$text) {
    if ($null -eq $text) { return '' }
    return $text.Replace('&', '&amp;').Replace('<', '&lt;').Replace('>', '&gt;').Replace('"', '&quot;')
}

try {
    [Windows.UI.Notifications.ToastNotificationManager, Windows.UI.Notifications, ContentType = WindowsRuntime] | Out-Null
    [Windows.Data.Xml.Dom.XmlDocument, Windows.Data.Xml.Dom.XmlDocument, ContentType = WindowsRuntime] | Out-Null
}
catch {
    Write-Error "WinRT toast types unavailable: $($_.Exception.Message)"
    exit 2
}

$text = '<text>' + (ConvertTo-XmlText $title) + '</text>'
if (-not [string]::IsNullOrWhiteSpace($message)) {
    $text += '<text>' + (ConvertTo-XmlText $message) + '</text>'
}
$hero = ''
if (-not [string]::IsNullOrWhiteSpace($image) -and (Test-Path -LiteralPath $image)) {
    $uri = ([System.Uri]$image).AbsoluteUri
    $hero = '<image placement="hero" src="' + $uri + '" />'
}

$xmlText = @"
<toast activationType="protocol" launch="$(ConvertTo-XmlText $arguments)">
  <visual>
    <binding template="ToastGeneric">
      $text
      $hero
    </binding>
  </visual>
  <actions>
    <action content="$(ConvertTo-XmlText $button)" activationType="protocol" arguments="$(ConvertTo-XmlText $arguments)" />
  </actions>
</toast>
"@

try {
    $xml = New-Object Windows.Data.Xml.Dom.XmlDocument
    $xml.LoadXml($xmlText)
    $toast = New-Object Windows.UI.Notifications.ToastNotification $xml
    [Windows.UI.Notifications.ToastNotificationManager]::CreateToastNotifier($appId).Show($toast)
    exit 0
}
catch {
    Write-Error "toast failed: $($_.Exception.Message)"
    exit 1
}
