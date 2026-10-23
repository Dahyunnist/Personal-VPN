[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BrokerExecutable,
    [Parameter(Mandatory = $true)]
    [string]$ClientExecutable,
    [Parameter(Mandatory = $true)]
    [string]$WintunDll,
    [Parameter(Mandatory = $true)]
    [string]$FailureServiceExecutable,
    [Parameter()]
    [string]$PolicyTemplate = (Join-Path $PSScriptRoot '..\configs\broker-policy.example.json'),
    [Parameter()]
    [string]$InstallerScript = (Join-Path $PSScriptRoot '..\deploy\windows\install-broker.ps1')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$serviceName = 'PersonalVPNBroker'
$programDirectory = Join-Path $env:ProgramFiles 'PersonalVPN'
$dataDirectory = Join-Path $env:ProgramData 'PersonalVPN'
$installedPolicy = Join-Path $dataDirectory 'broker-policy.json'
$stage = Join-Path $env:ProgramFiles ('PersonalVPN-Installer-Test-' + [guid]::NewGuid().ToString('N'))
$rollbackStage = Join-Path $env:ProgramFiles ('PersonalVPN-Rollback-Test-' + [guid]::NewGuid().ToString('N'))
$untrustedStage = Join-Path $env:TEMP ('PersonalVPN-Installer-Test-' + [guid]::NewGuid().ToString('N'))
$installationSucceeded = $false
$testPolicyHash = $null

function Set-TestSourceAcl {
    param([Parameter(Mandatory = $true)][string]$Path)

    & icacls.exe $Path /inheritance:r /T /C | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Unable to protect test source: $Path" }
    & icacls.exe $Path /grant:r '*S-1-5-18:(OI)(CI)F' '*S-1-5-32-544:(OI)(CI)F' /T /C | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Unable to set test source ACL: $Path" }
    & icacls.exe $Path /setowner '*S-1-5-32-544' /T /C | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Unable to set test source owner: $Path" }
}

function Invoke-InstallerProcess {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet('Install', 'Uninstall')]
        [string]$Action,
        [string]$PackageDirectory,
        [string]$PolicyFile,
        [switch]$WhatIf
    )

    if ([string]::IsNullOrWhiteSpace($PackageDirectory)) { $PackageDirectory = $stage }
    if ([string]::IsNullOrWhiteSpace($PolicyFile)) {
        $PolicyFile = Join-Path $PackageDirectory 'broker-policy.json'
    }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = (Get-Process -Id $PID).Path
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $InstallerScript,
        '-Action', $Action, '-PackageDirectory', $PackageDirectory,
        '-PolicyFile', $PolicyFile
    )) {
        $start.ArgumentList.Add($argument)
    }
    if ($WhatIf) { $start.ArgumentList.Add('-WhatIf') }

    $process = [Diagnostics.Process]::Start($start)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [pscustomobject]@{ ExitCode = $process.ExitCode; Output = $stdout + $stderr }
}

try {
    if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) {
        throw "Refusing to test over existing service $serviceName."
    }
    if ((Test-Path -LiteralPath $programDirectory) -or
        (Test-Path -LiteralPath $dataDirectory)) {
        throw 'Refusing to test over existing PersonalVPN installation or policy data.'
    }
    foreach ($source in @($BrokerExecutable, $ClientExecutable, $WintunDll,
            $FailureServiceExecutable, $PolicyTemplate, $InstallerScript)) {
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Required installer test input is missing: $source"
        }
    }

    $stageBin = Join-Path $stage 'bin'
    New-Item -ItemType Directory -Path $stageBin -Force | Out-Null
    Copy-Item -LiteralPath $BrokerExecutable -Destination (Join-Path $stageBin 'personal-vpn-broker-service.exe')
    Copy-Item -LiteralPath $ClientExecutable -Destination (Join-Path $stageBin 'personal-vpn-client.exe')
    Copy-Item -LiteralPath $WintunDll -Destination (Join-Path $stageBin 'wintun.dll')
    Copy-Item -LiteralPath $PolicyTemplate -Destination (Join-Path $stage 'broker-policy.json')
    $testPolicyHash = (Get-FileHash -LiteralPath (Join-Path $stage 'broker-policy.json') -Algorithm SHA256).Hash
    Set-TestSourceAcl -Path $stage

    $dryRun = Invoke-InstallerProcess -Action Install -WhatIf
    if ($dryRun.ExitCode -ne 0) {
        throw "Installer -WhatIf preflight failed ($($dryRun.ExitCode)): $($dryRun.Output)"
    }
    if ((Test-Path -LiteralPath $programDirectory) -or (Test-Path -LiteralPath $dataDirectory)) {
        throw 'Installer -WhatIf changed the machine state.'
    }

    New-Item -ItemType Directory -Path (Join-Path $untrustedStage 'bin') -Force | Out-Null
    Copy-Item -LiteralPath $BrokerExecutable -Destination (Join-Path $untrustedStage 'bin\personal-vpn-broker-service.exe')
    Copy-Item -LiteralPath $ClientExecutable -Destination (Join-Path $untrustedStage 'bin\personal-vpn-client.exe')
    Copy-Item -LiteralPath $WintunDll -Destination (Join-Path $untrustedStage 'bin\wintun.dll')
    Copy-Item -LiteralPath $PolicyTemplate -Destination (Join-Path $untrustedStage 'broker-policy.json')
    $untrusted = Invoke-InstallerProcess -Action Install `
        -PackageDirectory $untrustedStage `
        -PolicyFile (Join-Path $untrustedStage 'broker-policy.json') `
        -WhatIf
    if ($untrusted.ExitCode -eq 0 -or
        $untrusted.Output -notmatch 'untrusted owner|grants write access') {
        throw "Installer did not reject user-writable source paths: $($untrusted.Output)"
    }

    $rollbackBin = Join-Path $rollbackStage 'bin'
    New-Item -ItemType Directory -Path $rollbackBin -Force | Out-Null
    Copy-Item -LiteralPath $FailureServiceExecutable `
        -Destination (Join-Path $rollbackBin 'personal-vpn-broker-service.exe')
    Copy-Item -LiteralPath $ClientExecutable -Destination (Join-Path $rollbackBin 'personal-vpn-client.exe')
    Copy-Item -LiteralPath $WintunDll -Destination (Join-Path $rollbackBin 'wintun.dll')
    Copy-Item -LiteralPath $PolicyTemplate -Destination (Join-Path $rollbackStage 'broker-policy.json')
    Set-TestSourceAcl -Path $rollbackStage
    $rollback = Invoke-InstallerProcess -Action Install -PackageDirectory $rollbackStage
    if ($rollback.ExitCode -eq 0) {
        throw 'Installer unexpectedly accepted a service executable that exits during startup.'
    }
    if ($rollback.Output -notmatch '(?i)Start-Service|cannot be started|failed to start') {
        throw "Installer failed before exercising service-start rollback: $($rollback.Output)"
    }
    for ($attempt = 0; $attempt -lt 30 -and
        (Get-Service -Name $serviceName -ErrorAction SilentlyContinue); $attempt++) {
        Start-Sleep -Seconds 1
    }
    if ((Get-Service -Name $serviceName -ErrorAction SilentlyContinue) -or
        (Test-Path -LiteralPath $programDirectory) -or
        (Test-Path -LiteralPath $dataDirectory)) {
        throw "Failed installation did not roll back cleanly: $($rollback.Output)"
    }

    $install = Invoke-InstallerProcess -Action Install
    if ($install.ExitCode -ne 0) {
        throw "Broker installation failed ($($install.ExitCode)): $($install.Output)"
    }
    $installationSucceeded = $true
    $service = Get-Service -Name $serviceName -ErrorAction Stop
    if ($service.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Running) {
        throw "Broker service did not reach Running state: $($service.Status)"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $programDirectory 'bin\personal-vpn-broker-service.exe')) -or
        -not (Test-Path -LiteralPath $installedPolicy -PathType Leaf)) {
        throw 'Installation omitted the broker executable or installed policy.'
    }
    $policyHash = (Get-FileHash -LiteralPath $installedPolicy -Algorithm SHA256).Hash
    $programOwner = (Get-Acl -LiteralPath $programDirectory).GetOwner(
        [Security.Principal.SecurityIdentifier]).Value
    $policyOwner = (Get-Acl -LiteralPath $installedPolicy).GetOwner(
        [Security.Principal.SecurityIdentifier]).Value
    if ($programOwner -ne 'S-1-5-32-544' -or $policyOwner -ne 'S-1-5-32-544') {
        throw 'Installed binaries or policy do not have the expected Administrators owner.'
    }
    $trustedSids = @('S-1-5-18', 'S-1-5-32-544')
    $writeRights = [int]([Security.AccessControl.FileSystemRights]::WriteData -bor
        [Security.AccessControl.FileSystemRights]::AppendData -bor
        [Security.AccessControl.FileSystemRights]::WriteExtendedAttributes -bor
        [Security.AccessControl.FileSystemRights]::WriteAttributes -bor
        [Security.AccessControl.FileSystemRights]::Delete -bor
        [Security.AccessControl.FileSystemRights]::ChangePermissions -bor
        [Security.AccessControl.FileSystemRights]::TakeOwnership -bor
        [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles)
    foreach ($path in @($programDirectory, (Join-Path $programDirectory 'bin'), $installedPolicy)) {
        $acl = Get-Acl -LiteralPath $path
        $untrustedWriter = $acl.GetAccessRules($true, $true,
            [Security.Principal.SecurityIdentifier]) | Where-Object {
                $_.AccessControlType -eq [Security.AccessControl.AccessControlType]::Allow -and
                $_.IdentityReference.Value -notin $trustedSids -and
                (([int]$_.FileSystemRights -band $writeRights) -ne 0)
            } | Select-Object -First 1
        if ($null -ne $untrustedWriter) {
            throw "Installed path grants an untrusted writer: $path ($($untrustedWriter.IdentityReference))"
        }
    }

    $uninstall = Invoke-InstallerProcess -Action Uninstall
    if ($uninstall.ExitCode -ne 0) {
        throw "Broker uninstall failed ($($uninstall.ExitCode)): $($uninstall.Output)"
    }
    if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) {
        throw 'Broker service remains after uninstall.'
    }
    if (Test-Path -LiteralPath $programDirectory) {
        throw 'Program files remain after uninstall.'
    }
    if (-not (Test-Path -LiteralPath $installedPolicy -PathType Leaf) -or
        (Get-FileHash -LiteralPath $installedPolicy -Algorithm SHA256).Hash -ne $policyHash) {
        throw 'Uninstall did not preserve the installed policy unchanged.'
    }

    Write-Output 'Windows broker installer integration passed: WhatIf, source ACL rejection, failed-start rollback, SCM install/start/uninstall, ACL ownership, and policy preservation.'
} finally {
    if ($installationSucceeded) {
        if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) {
            try { [void](Invoke-InstallerProcess -Action Uninstall) } catch { Write-Warning $_ }
        }
        if ($null -ne $testPolicyHash -and
            -not (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) -and
            -not (Test-Path -LiteralPath $programDirectory) -and
            (Test-Path -LiteralPath $installedPolicy -PathType Leaf) -and
            (Get-FileHash -LiteralPath $installedPolicy -Algorithm SHA256).Hash -eq $testPolicyHash) {
            Remove-Item -LiteralPath $installedPolicy -Force
            if (Test-Path -LiteralPath $dataDirectory) {
                try { Remove-Item -LiteralPath $dataDirectory -Force } catch { Write-Warning $_ }
            }
        }
    }
    if (Test-Path -LiteralPath $stage) {
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
    if (Test-Path -LiteralPath $rollbackStage) {
        Remove-Item -LiteralPath $rollbackStage -Recurse -Force
    }
    if (Test-Path -LiteralPath $untrustedStage) {
        Remove-Item -LiteralPath $untrustedStage -Recurse -Force
    }
}
