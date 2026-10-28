# Z-order probe: asserts ProTrailOverlay sits at the top of the topmost band
# even after a foreign process raised its own topmost window.
# Usage: topmost_probe.ps1 <path-to-protrail.exe> <seconds-between-checks>
param([string]$Exe, [int]$SettleMs = 4000)

Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class Z {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] public static extern int GetWindowLong(IntPtr h, int i);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  public struct W { public IntPtr H; public bool Top; public bool Vis; public string T; public string C; }
  public static List<W> All() {
    var list = new List<W>();
    EnumWindows((h,p) => {
      var sb = new StringBuilder(256); GetWindowTextW(h, sb, 256);
      var cb = new StringBuilder(256); GetClassNameW(h, cb, 256);
      list.Add(new W { H=h, Top=(GetWindowLong(h,-20)&0x8)!=0, Vis=IsWindowVisible(h), T=sb.ToString(), C=cb.ToString() });
      return true;
    }, IntPtr.Zero);
    return list;
  }
  public static string Report() {
    var all = All();
    int idx = -1;
    for (int i=0;i<all.Count;i++) if (all[i].C=="ProTrailOverlay" && all[i].Vis) { idx = i; break; }
    if (idx < 0) return "FAIL: no visible ProTrailOverlay window found";
    var sb = new StringBuilder();
    int above = 0;
    for (int i=0;i<idx;i++) {
      if (!all[i].Vis) continue;                 // invisible windows never occlude
      above++;
      if (above <= 6) sb.Append("  above#" + above + " topmost=" + all[i].Top + " class=" + all[i].C + " title='" + all[i].T + "'\n");
    }
    sb.Append(above==0 ? "  VISIBLE windows above overlay: 0\n" : "  VISIBLE windows above overlay: " + above + "\n");
    int tot=0; foreach (var w in all) if (w.C=="ProTrailOverlay" && w.Vis) tot++;
    sb.Append("  overlays_visible=" + tot + "\n");
    // Surface any error dialog the app itself popped -- it sits above the
    // overlay too and would silently corrupt this measurement.
    foreach (var w in all) if (w.Vis && w.C=="#32770") sb.Append("  ERROR-DIALOG: '" + w.T + "'\n");
    return sb.ToString();
  }
}
"@

$app = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Milliseconds $SettleMs

Write-Host "=== phase 1: baseline ==="
Write-Host ([Z]::Report())

Write-Host "=== phase 2: foreign process raises a topmost window ==="
$rival = Start-Process powershell -ArgumentList '-NoProfile','-Command',
  'Add-Type -AssemblyName System.Windows.Forms; $f=New-Object System.Windows.Forms.Form; $f.Text="RivalTopMost"; $f.TopMost=$true; $f.Width=300; $f.Height=200; $f.Show(); Start-Sleep -Seconds 25' -PassThru
Start-Sleep -Milliseconds 2500
Write-Host ([Z]::Report())

Write-Host "=== phase 3: 2s later (watchdog should have restored the band) ==="
Start-Sleep -Milliseconds 2000
Write-Host ([Z]::Report())

Write-Host "=== phase 4: rival closed, 1s later ==="
Stop-Process -Id $rival.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 1500
Write-Host ([Z]::Report())

Write-Host "=== phase 5: rival raised again, checked 4x over 4s ==="
$rival2 = Start-Process powershell -ArgumentList '-NoProfile','-Command',
  'Add-Type -AssemblyName System.Windows.Forms; $f=New-Object System.Windows.Forms.Form; $f.Text="RivalTopMost2"; $f.TopMost=$true; $f.Width=300; $f.Height=200; $f.Show(); Start-Sleep -Seconds 25' -PassThru
foreach ($i in 1..4) { Start-Sleep -Milliseconds 1000; Write-Host "  t+${i}s:"; Write-Host ([Z]::Report()) }

Stop-Process -Id $rival2.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $app.Id -Force -ErrorAction SilentlyContinue
