# Q62: per-user registration that lets a toast button call DropBridge back.
#
# Nothing is installed: this only writes the current user's registry and one
# Start-menu shortcut, so Windows knows which program owns "DropBridge.Tray"
# and where "dropbridge://" points. Re-runnable; -Remove undoes it.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File register.ps1 -ExePath "C:\...\DropBridge.exe"
#   powershell -NoProfile -ExecutionPolicy Bypass -File register.ps1 -Remove
#
# Exit codes: 0 ok, 1 failure.
param(
    [string]$ExePath = $env:DROPBRIDGE_EXE,
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'
$appId = 'DropBridge.Tray'
$protocol = 'dropbridge'
$startMenu = Join-Path $env:APPDATA 'Microsoft\Windows\Start Menu\Programs\DropBridge.lnk'
$appKey = "HKCU:\Software\Classes\AppUserModelId\$appId"
$protoKey = "HKCU:\Software\Classes\$protocol"

if ($Remove) {
    foreach ($key in @($protoKey, $appKey)) {
        if (Test-Path $key) { Remove-Item $key -Recurse -Force }
    }
    if (Test-Path $startMenu) { Remove-Item $startMenu -Force }
    'removed'
    exit 0
}

if ([string]::IsNullOrWhiteSpace($ExePath)) {
    Write-Error 'DROPBRIDGE_EXE / -ExePath is required'
    exit 1
}
if (-not (Test-Path -LiteralPath $ExePath)) {
    Write-Error "exe not found: $ExePath"
    exit 1
}
$ExePath = (Resolve-Path -LiteralPath $ExePath).Path
$exeDir = Split-Path $ExePath -Parent
$icon = Join-Path $exeDir 'DropBridge.ico'
if (-not (Test-Path $icon)) { $icon = $ExePath }

# 1. App identity: name and icon for notifications.
New-Item -Path $appKey -Force | Out-Null
Set-ItemProperty -Path $appKey -Name 'DisplayName' -Value 'DropBridge'
Set-ItemProperty -Path $appKey -Name 'IconUri' -Value $icon

# 2. Protocol: the toast button launches "<exe> dropbridge://copy?file=...".
New-Item -Path $protoKey -Force | Out-Null
Set-ItemProperty -Path $protoKey -Name '(Default)' -Value 'URL:DropBridge copy action'
Set-ItemProperty -Path $protoKey -Name 'URL Protocol' -Value ''
New-Item -Path "$protoKey\shell\open\command" -Force | Out-Null
Set-ItemProperty -Path "$protoKey\shell\open\command" -Name '(Default)' -Value ('"{0}" "%1"' -f $ExePath)

# 3. Start-menu shortcut carrying the AppUserModelID. Windows uses it to resolve
#    the app identity; without it toasts from an unpackaged exe are dropped.
if (Test-Path $startMenu) { Remove-Item $startMenu -Force }
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($startMenu)
$shortcut.TargetPath = $ExePath
$shortcut.WorkingDirectory = $exeDir
$shortcut.IconLocation = "$ExePath,0"
$shortcut.Description = 'DropBridge'
$shortcut.Save()
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($shortcut) | Out-Null
[System.Runtime.InteropServices.Marshal]::ReleaseComObject($shell) | Out-Null
[GC]::Collect()
[GC]::WaitForPendingFinalizers()
Start-Sleep -Milliseconds 200

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class ShortcutAumid
{
    [ComImport, InterfaceType(ComInterfaceType.InterfaceIsIUnknown), Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99")]
    public interface IPropertyStore
    {
        void GetCount(out uint cProps);
        void GetAt(uint iProp, out PropertyKey pkey);
        void GetValue(ref PropertyKey key, out PropVariant pv);
        void SetValue(ref PropertyKey key, ref PropVariant pv);
        void Commit();
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PropertyKey { public Guid FormatId; public uint PropertyId; }

    [StructLayout(LayoutKind.Sequential)]
    public struct PropVariant
    {
        public ushort VarType;
        public ushort Reserved1;
        public ushort Reserved2;
        public ushort Reserved3;
        public IntPtr Pointer;
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    static extern int SHGetPropertyStoreFromParsingName(string path, IntPtr bindCtx, uint flags, ref Guid riid, out IPropertyStore store);

    public static bool Set(string path, string value)
    {
        Guid iid = new Guid("886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99");
        IPropertyStore store;
        if (SHGetPropertyStoreFromParsingName(path, IntPtr.Zero, 2, ref iid, out store) != 0) return false;
        try
        {
            var key = new PropertyKey { FormatId = new Guid("9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3"), PropertyId = 5 };
            var pv = new PropVariant { VarType = 31, Pointer = Marshal.StringToCoTaskMemUni(value) };
            try { store.SetValue(ref key, ref pv); store.Commit(); }
            finally { Marshal.FreeCoTaskMem(pv.Pointer); }
            return true;
        }
        finally { Marshal.ReleaseComObject(store); }
    }
}
"@

if (-not [ShortcutAumid]::Set($startMenu, $appId)) {
    Write-Error 'could not stamp the AppUserModelID on the shortcut'
    exit 1
}

# Verify what is actually on disk, not what the API claimed.
$bytes = [IO.File]::ReadAllBytes($startMenu)
if (-not ([Text.Encoding]::Unicode.GetString($bytes)).Contains($appId)) {
    Write-Error 'shortcut does not carry the AppUserModelID'
    exit 1
}

"registered exe=$ExePath shortcut=$startMenu"
exit 0
