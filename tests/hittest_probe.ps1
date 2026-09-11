# Objective click-through probe: what window owns the pixel where the diagnostic square is?
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class Probe {
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int x; public int y; }

    [DllImport("user32.dll")]
    public static extern IntPtr WindowFromPoint(POINT p);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr GetParent(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern int GetWindowTextW(IntPtr hWnd, StringBuilder b, int n);

    [DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern int GetClassNameW(IntPtr hWnd, StringBuilder b, int n);

    [DllImport("user32.dll")]
    public static extern int GetSystemMetrics(int n);

    public static string Text(IntPtr h) {
        var sb = new StringBuilder(256);
        GetWindowTextW(h, sb, 256);
        var cb = new StringBuilder(256);
        GetClassNameW(h, cb, 256);
        return "hwnd=0x" + h.ToInt64().ToString("X") + " class='" + cb.ToString() + "' title='" + sb.ToString() + "'";
    }
}
"@

$SM_XVIRTUALSCREEN = 76
$SM_YVIRTUALSCREEN = 77
$vx = [Probe]::GetSystemMetrics($SM_XVIRTUALSCREEN)
$vy = [Probe]::GetSystemMetrics($SM_YVIRTUALSCREEN)

# Points to test, in virtual screen coordinates:
#  (20,20)+60  -> inside the RED square
#  (140,20)+40 -> inside the BLUE bar
#  center-ish empty area -> far from any drawn primitive
$points = @(
    @{ name = "RED square (vx+60, vy+60)";  x = $vx + 60;  y = $vy + 60 },
    @{ name = "BLUE bar   (vx+240, vy+60)"; x = $vx + 240; y = $vy + 60 },
    @{ name = "EMPTY      (vx+900, vy+700)"; x = $vx + 900; y = $vy + 700 }
)

"virtual screen origin: ($vx, $vy)"
""
foreach ($p in $points) {
    $pt = New-Object Probe+POINT
    $pt.x = $p.x
    $pt.y = $p.y
    $hwnd = [Probe]::WindowFromPoint($pt)
    "{0,-34} -> {1}" -f $p.name, ([Probe]::Text($hwnd))
}
