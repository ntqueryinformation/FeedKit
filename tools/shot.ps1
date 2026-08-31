Add-Type -AssemblyName System.Drawing
Add-Type -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
public struct RECT { public int Left, Top, Right, Bottom; }
'@ -Name W32 -Namespace Native

[Native.W32]::SetProcessDPIAware() | Out-Null

$p = Get-Process FeedKit -ErrorAction Stop
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { throw "no main window" }
$r = New-Object Native.W32+RECT
[Native.W32]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.Right - $r.Left
$ht = $r.Bottom - $r.Top
$bmp = New-Object System.Drawing.Bitmap($w, $ht)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
# PW_RENDERFULLCONTENT (2): captures DWM/D3D content even if the window is occluded
[Native.W32]::PrintWindow($h, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
New-Item -ItemType Directory -Force -Path 'C:\Users\Kyle\Documents\GitHub\FeedKit\docs' | Out-Null
$bmp.Save('C:\Users\Kyle\Documents\GitHub\FeedKit\docs\screenshot.png', [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "saved ${w}x${ht}"
