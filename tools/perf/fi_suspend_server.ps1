param([int]$LeadSec = 6, [int]$SuspendSec = 4, [int]$TailSec = 8, [string]$Etl)
Add-Type -Namespace Nt -Name P -MemberDefinition @"
[System.Runtime.InteropServices.DllImport("ntdll.dll")] public static extern int NtSuspendProcess(System.IntPtr h);
[System.Runtime.InteropServices.DllImport("ntdll.dll")] public static extern int NtResumeProcess(System.IntPtr h);
"@
$srv = Get-Process WeaselServer | Select -First 1
logman stop lw -ets 2>&1 | Out-Null
Remove-Item $Etl -ErrorAction SilentlyContinue
logman create trace lw -p "{5974258d-005e-4490-adef-9e265b26abf5}" 0xffffffffffffffff 0xff -o $Etl -ets | Out-Null
"{0:HH:mm:ss.fff} capture started, typing expected now" -f (Get-Date)
Start-Sleep $LeadSec
$h = $srv.Handle
$t0 = Get-Date
[void][Nt.P]::NtSuspendProcess($h)
"{0:HH:mm:ss.fff} SERVER SUSPENDED" -f (Get-Date)
Start-Sleep $SuspendSec
[void][Nt.P]::NtResumeProcess($h)
"{0:HH:mm:ss.fff} SERVER RESUMED (suspended for {1:N2} s)" -f (Get-Date), ((Get-Date)-$t0).TotalSeconds
Start-Sleep $TailSec
logman stop lw -ets | Select -First 1
"{0:HH:mm:ss.fff} capture stopped" -f (Get-Date)
