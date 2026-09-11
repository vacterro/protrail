# ProTrail input verification driver (T-007R / T-008 runtime checks).
# Synthesizes REAL mouse input via SendInput so the raw-input pipeline
# (RIDEV_INPUTSINK, message-only HWND, Qt-dispatched WM_INPUT) is exercised
# end-to-end while this script's console -- not ProTrail -- owns the
# foreground. No elevation, no hooks.
#
# Usage:
#   powershell -File tests\input_soak.ps1 -Mode burst   # ~5 s movement + one L/R/M burst
#   powershell -File tests\input_soak.ps1 -Mode mixed   # ~2 min mixed activity
#   powershell -File tests\input_soak.ps1 -Mode long    # ~5.5 min mixed activity (B10)

param(
    [Parameter(Mandatory = $false)]
    [ValidateSet('burst', 'mixed', 'long')]
    [string]$Mode = 'burst'
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class SoakInput {
    [StructLayout(LayoutKind.Sequential)]
    public struct INPUT { public uint type; public MOUSEINPUT mi; }
    [StructLayout(LayoutKind.Sequential)]
    public struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint SendInput(uint n, INPUT[] inputs, int size);
}
"@

function Send-Mouse([uint32]$flags, [int]$dx = 0, [int]$dy = 0) {
    $in = New-Object SoakInput+INPUT
    $in.type = 0
    $in.mi.dwFlags = $flags
    $in.mi.dx = $dx
    $in.mi.dy = $dy
    [SoakInput]::SendInput(1, @($in), [System.Runtime.InteropServices.Marshal]::SizeOf([type][SoakInput+INPUT])) | Out-Null
}

# Flags
$MOVE       = 0x0001
$L_DOWN     = 0x0002; $L_UP = 0x0004
$R_DOWN     = 0x0008; $R_UP = 0x0010
$M_DOWN     = 0x0020; $M_UP = 0x0040

function Burst {
    Write-Host "burst: ~5 s movement (this console stays foreground) + one L/R/M burst"
    for ($i = 0; $i -lt 250; $i++) {
        $dx = [int][Math]::Round(60 * [Math]::Sin($i / 3.1))
        $dy = [int][Math]::Round(45 * [Math]::Cos($i / 4.7))
        Send-Mouse $MOVE $dx $dy
        Start-Sleep -Milliseconds 18
    }
    Send-Mouse $L_DOWN; Start-Sleep -Milliseconds 70; Send-Mouse $L_UP
    Send-Mouse $R_DOWN; Start-Sleep -Milliseconds 70; Send-Mouse $R_UP
    Send-Mouse $M_DOWN; Start-Sleep -Milliseconds 70; Send-Mouse $M_UP
    Write-Host "burst done: 3 clicks (L/R/M) + sustained movement injected"
}

function MixedRun([double]$seconds) {
    Write-Host ("mixed run: {0:N0} s of varied movement + periodic clicks" -f $seconds)
    $deadline = [DateTime]::UtcNow.AddSeconds($seconds)
    $phase = 0
    while ([DateTime]::UtcNow -lt $deadline) {
        $t = [DateTime]::UtcNow.TimeOfDay.TotalSeconds
        switch ($phase % 5) {
            0 { $dx = [int](70 * [Math]::Sin(3 * $t)); $dy = [int](70 * [Math]::Cos(2 * $t)) }  # circles
            1 { $dx = 42; $dy = 0 }                                                              # straight fast
            2 { $dx = (Get-Random -Min -60 -Max 61); $dy = (Get-Random -Min -60 -Max 61) }       # zig-zag
            3 { $dx = 3; $dy = 2 }                                                               # very slow drift
            4 { $dx = 0; $dy = 0 }                                                               # stationary beat
        }
        Send-Mouse $MOVE $dx $dy
        Start-Sleep -Milliseconds 12
        if ($phase % 5 -eq 4) { Start-Sleep -Milliseconds 120 }
        $phase++
        if ($phase % 250 -eq 0) {
            Send-Mouse $L_DOWN; Start-Sleep -Milliseconds 60; Send-Mouse $L_UP
            Send-Mouse $R_DOWN; Start-Sleep -Milliseconds 60; Send-Mouse $R_UP
            Send-Mouse $M_DOWN; Start-Sleep -Milliseconds 60; Send-Mouse $M_UP
        }
        $phase++ | Out-Null
    }
    Write-Host ("mixed run done: {0} injection steps" -f $phase)
}

switch ($Mode) {
    'burst' { Burst }
    'mixed' { MixedRun 120 }
    'long'  { MixedRun 330 }
}
