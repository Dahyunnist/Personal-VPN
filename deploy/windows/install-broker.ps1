[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Install', 'Uninstall')]
    [string]$Action,

    [Parameter()]
    [string]$PackageDirectory,

    [Parameter()]
    [string]$PolicyFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$serviceName = 'PersonalVPNBroker'
$programDirectory = Join-Path $env:ProgramFiles 'PersonalVPN'
$binaryDirectory = Join-Path $programDirectory 'bin'
$dataDirectory = Join-Path $env:ProgramData 'PersonalVPN'
$installedPolicy = Join-Path $dataDirectory 'broker-policy.json'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Set-PrivateDirectoryAcl {
    param([Parameter(Mandatory = $true)][string]$Path, [switch]$AllowUsersRead)

    $acl = Get-Acl -LiteralPath $Path
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($rule in @($acl.Access)) {
        [void]$acl.RemoveAccessRuleAll($rule)
    }
    $inheritance = [Security.AccessControl.InheritanceFlags]::ContainerInherit -bor
        [Security.AccessControl.InheritanceFlags]::ObjectInherit
    $propagation = [Security.AccessControl.PropagationFlags]::None
    $allow = [Security.AccessControl.AccessControlType]::Allow
    foreach ($entry in @(
        @{ Sid = 'S-1-5-18'; Rights = [Security.AccessControl.FileSystemRights]::FullControl },
        @{ Sid = 'S-1-5-32-544'; Rights = [Security.AccessControl.FileSystemRights]::FullControl }
    )) {
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]::new($entry.Sid),
            $entry.Rights, $inheritance, $propagation, $allow)
        [void]$acl.AddAccessRule($rule)
    }
    if ($AllowUsersRead) {
        $usersRule = [Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]::new('S-1-5-32-545'),
            [Security.AccessControl.FileSystemRights]::ReadAndExecute,
            $inheritance, $propagation, $allow)
        [void]$acl.AddAccessRule($usersRule)
    }
    Set-Acl -LiteralPath $Path -AclObject $acl
}

function Set-TrustedOwner {
    param([Parameter(Mandatory = $true)][string]$Path)

    $acl = Get-Acl -LiteralPath $Path
    $acl.SetOwner([Security.Principal.SecurityIdentifier]::new('S-1-5-32-544'))
    Set-Acl -LiteralPath $Path -AclObject $acl
}

function Set-PrivateFileAcl {
    param([Parameter(Mandatory = $true)][string]$Path)

    $acl = Get-Acl -LiteralPath $Path
    $acl.SetAccessRuleProtection($true, $false)
    foreach ($rule in @($acl.Access)) {
        [void]$acl.RemoveAccessRuleAll($rule)
    }
    foreach ($sid in @('S-1-5-18', 'S-1-5-32-544')) {
        $rule = [Security.AccessControl.FileSystemAccessRule]::new(
            [Security.Principal.SecurityIdentifier]::new($sid),
            [Security.AccessControl.FileSystemRights]::FullControl,
            [Security.AccessControl.AccessControlType]::Allow)
        [void]$acl.AddAccessRule($rule)
    }
    Set-Acl -LiteralPath $Path -AclObject $acl
    Set-TrustedOwner -Path $Path
}

function Assert-AdministratorProtectedPath {
    param([Parameter(Mandatory = $true)][string]$Path)

    $trustedSids = @(
        'S-1-5-18',
        'S-1-5-32-544',
        'S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464'
    )
    $writeRights = [Security.AccessControl.FileSystemRights]::Write -bor
        [Security.AccessControl.FileSystemRights]::Modify -bor
        [Security.AccessControl.FileSystemRights]::Delete -bor
        [Security.AccessControl.FileSystemRights]::ChangePermissions -bor
        [Security.AccessControl.FileSystemRights]::TakeOwnership -bor
        [Security.AccessControl.FileSystemRights]::FullControl -bor
        [Security.AccessControl.FileSystemRights]::CreateFiles -bor
        [Security.AccessControl.FileSystemRights]::CreateDirectories -bor
        [Security.AccessControl.FileSystemRights]::DeleteSubdirectoriesAndFiles
    $current = (Resolve-Path -LiteralPath $Path).Path
    while ($true) {
        $item = Get-Item -LiteralPath $current -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Trusted installation source contains a reparse point: $current"
        }
        $acl = Get-Acl -LiteralPath $current
        $ownerSid = $acl.GetOwner([Security.Principal.SecurityIdentifier]).Value
        if ($ownerSid -notin $trustedSids) {
            throw "Trusted installation source has an untrusted owner: $current"
        }
        foreach ($rule in $acl.GetAccessRules($true, $true,
                [Security.Principal.SecurityIdentifier])) {
            if ($rule.AccessControlType -eq [Security.AccessControl.AccessControlType]::Allow -and
                $rule.IdentityReference.Value -notin $trustedSids -and
                (($rule.FileSystemRights -band $writeRights) -ne 0)) {
                throw "Trusted installation source grants write access outside SYSTEM/Administrators: $current"
            }
        }
        $parent = Split-Path -Path $current -Parent
        if ([string]::IsNullOrEmpty($parent) -or $parent -eq $current) { break }
        $current = $parent
    }
}

if (-not (Test-Administrator)) {
    throw 'Run this script from an elevated PowerShell session.'
}

if ($Action -eq 'Uninstall') {
    $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
    if ($null -eq $service) {
        Write-Output 'PersonalVPN broker service is not installed.'
        exit 0
    }
    if ($PSCmdlet.ShouldProcess($serviceName, 'Stop and remove Windows service')) {
        if ($service.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Stopped) {
            Stop-Service -Name $serviceName -Force
            (Get-Service -Name $serviceName).WaitForStatus(
                [System.ServiceProcess.ServiceControllerStatus]::Stopped,
                [TimeSpan]::FromSeconds(30))
        }
        & sc.exe delete $serviceName | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "sc.exe delete failed: $LASTEXITCODE" }
        for ($attempt = 0; $attempt -lt 30; $attempt++) {
            if (-not (Get-Service -Name $serviceName -ErrorAction SilentlyContinue)) { break }
            Start-Sleep -Seconds 1
        }
        if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) {
            throw 'Service removal is pending. Retry after the Service Control Manager releases it.'
        }
        if (Test-Path -LiteralPath $programDirectory) {
            Remove-Item -LiteralPath $programDirectory -Recurse -Force
        }
        Write-Output "Removed service and binaries. Preserved policy at $installedPolicy"
    }
    exit 0
}

if ([string]::IsNullOrWhiteSpace($PackageDirectory) -or
    [string]::IsNullOrWhiteSpace($PolicyFile)) {
    throw 'Install requires -PackageDirectory and -PolicyFile.'
}
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$policySource = (Resolve-Path -LiteralPath $PolicyFile).Path
if ($package.StartsWith('\\') -or $policySource.StartsWith('\\')) {
    throw 'Package and policy must come from local, administrator-protected paths.'
}
Assert-AdministratorProtectedPath -Path $package
Assert-AdministratorProtectedPath -Path $policySource
$requiredFiles = @(
    'personal-vpn-broker-service.exe',
    'personal-vpn-client.exe',
    'wintun.dll'
)
foreach ($name in $requiredFiles) {
    $binary = Join-Path $package "bin\$name"
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw "Package is missing bin\$name"
    }
    Assert-AdministratorProtectedPath -Path $binary
}
$policy = Get-Content -LiteralPath $policySource -Raw | ConvertFrom-Json
$policyLength = (Get-Item -LiteralPath $policySource).Length
if ($policyLength -le 0 -or $policyLength -gt 65536) {
    throw 'Policy file must be between 1 and 65536 bytes.'
}
if ($policy.schema_version -ne 1 -or $null -eq $policy.network -or
    $null -eq $policy.network.allowed_routes -or
    $policy.network.allow_default_route -isnot [bool]) {
    throw 'Policy must contain schema_version 1 and a network policy.'
}
$reuseInstalledPolicy = $false
if (Test-Path -LiteralPath $dataDirectory) {
    $dataItem = Get-Item -LiteralPath $dataDirectory -Force
    if (($dataItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Existing PersonalVPN data directory must not be a reparse point.'
    }
    if (-not (Test-Path -LiteralPath $installedPolicy -PathType Leaf)) {
        throw 'PersonalVPN data directory exists without a policy file. Inspect it before installing.'
    }
    $policyItem = Get-Item -LiteralPath $installedPolicy -Force
    if (($policyItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw 'Existing policy must not be a reparse point.'
    }
    $existingPolicy = Get-Content -LiteralPath $installedPolicy -Raw | ConvertFrom-Json
    $existingPolicyLength = (Get-Item -LiteralPath $installedPolicy).Length
    if ($existingPolicyLength -le 0 -or $existingPolicyLength -gt 65536) {
        throw 'Existing policy file must be between 1 and 65536 bytes.'
    }
    if ($existingPolicy.schema_version -ne 1 -or $null -eq $existingPolicy.network -or
        $null -eq $existingPolicy.network.allowed_routes -or
        $existingPolicy.network.allow_default_route -isnot [bool]) {
        throw 'Existing policy has an invalid schema.'
    }
    $sourceHash = (Get-FileHash -LiteralPath $policySource -Algorithm SHA256).Hash
    $installedHash = (Get-FileHash -LiteralPath $installedPolicy -Algorithm SHA256).Hash
    if ($sourceHash -ne $installedHash) {
        throw 'Existing policy differs from -PolicyFile. Back it up and resolve the difference before reinstalling.'
    }
    $reuseInstalledPolicy = $true
}

$existingService = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
if ($null -ne $existingService) {
    throw 'Service already exists. Uninstall it first; this installer does not overwrite an existing installation.'
}
if ((Test-Path -LiteralPath $programDirectory) -or
    ((Test-Path -LiteralPath $dataDirectory) -and -not $reuseInstalledPolicy) -or
    ((Test-Path -LiteralPath $installedPolicy) -and -not $reuseInstalledPolicy)) {
    throw 'An installation or PersonalVPN data directory already exists. Inspect it before installing.'
}
if (-not $PSCmdlet.ShouldProcess($programDirectory, 'Install PersonalVPN broker service')) {
    exit 0
}

try {
    New-Item -ItemType Directory -Path $binaryDirectory -Force | Out-Null
    New-Item -ItemType Directory -Path $dataDirectory -Force | Out-Null
    Set-PrivateDirectoryAcl -Path $programDirectory -AllowUsersRead
    Set-PrivateDirectoryAcl -Path $binaryDirectory -AllowUsersRead
    Set-PrivateDirectoryAcl -Path $dataDirectory
    Set-TrustedOwner -Path $programDirectory
    Set-TrustedOwner -Path $binaryDirectory
    Set-TrustedOwner -Path $dataDirectory
    foreach ($name in $requiredFiles) {
        Copy-Item -LiteralPath (Join-Path $package "bin\$name") `
            -Destination (Join-Path $binaryDirectory $name)
    }
    if (-not $reuseInstalledPolicy) {
        Copy-Item -LiteralPath $policySource -Destination $installedPolicy
    }
    Set-PrivateDirectoryAcl -Path $dataDirectory
    Set-PrivateFileAcl -Path $installedPolicy

    $serviceExecutable = Join-Path $binaryDirectory 'personal-vpn-broker-service.exe'
    New-Service -Name $serviceName -DisplayName 'PersonalVPN Broker' `
        -Description 'Runs the authenticated PersonalVPN tunnel broker.' `
        -BinaryPathName ('"{0}"' -f $serviceExecutable) -StartupType Automatic | Out-Null
    & sc.exe failure $serviceName 'reset=' '86400' `
        'actions=' 'restart/60000/restart/60000/restart/60000' | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to configure service recovery: $LASTEXITCODE"
    }
    & sc.exe failureflag $serviceName '1' | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to configure service failure actions: $LASTEXITCODE"
    }
    Start-Service -Name $serviceName
} catch {
    $failure = $_
    try {
        $service = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
        if ($null -ne $service) {
            if ($service.Status -ne [System.ServiceProcess.ServiceControllerStatus]::Stopped) {
                Stop-Service -Name $serviceName -Force -ErrorAction SilentlyContinue
            }
            & sc.exe delete $serviceName | Out-Null
            if ($LASTEXITCODE -ne 0) { throw "sc.exe delete failed during rollback: $LASTEXITCODE" }
            for ($attempt = 0; $attempt -lt 30; $attempt++) {
                if (-not (Get-Service -Name $serviceName -ErrorAction SilentlyContinue)) { break }
                Start-Sleep -Seconds 1
            }
            if (Get-Service -Name $serviceName -ErrorAction SilentlyContinue) {
                throw 'Service removal is pending during rollback; binaries were preserved.'
            }
        }
        if (Test-Path -LiteralPath $programDirectory) {
            Remove-Item -LiteralPath $programDirectory -Recurse -Force
        }
        if (-not $reuseInstalledPolicy -and (Test-Path -LiteralPath $dataDirectory)) {
            Remove-Item -LiteralPath $dataDirectory -Recurse -Force
        }
    } catch {
        Write-Warning "Install rollback was incomplete: $($_.Exception.Message)"
    }
    throw $failure
}
Write-Output "Installed PersonalVPN broker. Policy: $installedPolicy"
