#!/usr/bin/env pwsh

param(
    [string]$Compiler = "gcc"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function ConvertTo-InvokableCxxCompiler() {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InputName
    )

    switch($InputName) {
        "gcc" { return "g++" }
        "g++" { return "g++" }
        "clang" { return "clang++" }
        "clang++" { return "clang++" }
        "vc" { return "cl" }
        "msvc" { return "cl" }
        "cl" { return "cl" }
        "vs" { return "cl" }
        default { throw "Unsupported compiler." }
    }
}

function ConvertTo-InvokableCCompiler() {
    param(
        [Parameter(Mandatory = $true)]
        [string]$InputName
    )

    switch($InputName) {
        "gcc" { return "gcc" }
        "g++" { return "gcc" }
        "clang" { return "clang" }
        "clang++" { return "clang" }
        "vc" { return "cl" }
        "msvc" { return "cl" }
        "cl" { return "cl" }
        "vs" { return "cl" }
        default { throw "Unsupported compiler." }
    }
}

function Test-LastExitCode() {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if($LASTEXITCODE -ne 0) {
        throw "Latest invocation failed: code is $LASTEXITCODE, msg is $Message"
    }
}

function Remove-DirectoryIfExists() {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if(Test-Path $Path) {
        if($IsWindows) {
            cmd /c "rmdir /s /q `"$Path`"" | Out-Null
        } else {
            [System.IO.Directory]::Delete((Resolve-Path $Path).Path, $true)
        }
    }
}

function Get-NormalizedText() {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if(!(Test-Path $Path)) {
        throw "Expected file does not exist: $Path"
    }

    $raw = Get-Content $Path -Raw
    return $raw.Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd([char[]]"`n")
}

function Assert-EmptyFile() {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    if((Get-Item $Path).Length -ne 0) {
        throw "Assertion failed for $Label. Expected empty file: $Path"
    }
}

function Resolve-PythonCommand() {
    if(Get-Command python -ErrorAction SilentlyContinue) {
        return "python"
    }
    if(Get-Command python3 -ErrorAction SilentlyContinue) {
        return "python3"
    }
    if(Get-Command py -ErrorAction SilentlyContinue) {
        return "py -3"
    }
    throw "No usable Python interpreter found."
}

function New-RenderedCase() {
    param(
        [Parameter(Mandatory = $true)]
        [string]$TemplatePath,
        [Parameter(Mandatory = $true)]
        [string]$OutputPath,
        [Parameter(Mandatory = $true)]
        [string]$PythonCommand
    )

    $template = Get-Content $TemplatePath -Raw
    $rendered = $template.Replace("@PYTHON@", $PythonCommand)
    Set-Content -Path $OutputPath -Value $rendered -Encoding utf8NoBOM
}

function Assert-FileContent() {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExpectedPath,
        [Parameter(Mandatory = $true)]
        [string]$ActualPath,
        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    $expected = Get-NormalizedText -Path $ExpectedPath
    $actual = Get-NormalizedText -Path $ActualPath
    if($expected -ne $actual) {
        throw "Assertion failed for $Label.`nExpected: $ExpectedPath`nActual: $ActualPath"
    }
}

function Invoke-ShellCase() {
    param(
        [Parameter(Mandatory = $true)]
        [string]$BinaryPath,
        [Parameter(Mandatory = $true)]
        [string]$CasePath,
        [Parameter(Mandatory = $true)]
        [string]$ActualStdoutPath,
        [string]$ExpectedStdoutPath = "",
        [Parameter(Mandatory = $true)]
        [string]$Label
    )

    & $BinaryPath $CasePath | Out-File -FilePath $ActualStdoutPath -Encoding utf8NoBOM
    Test-LastExitCode -Message "test case $Label"
    if($ExpectedStdoutPath -ne "") {
        Assert-FileContent -ExpectedPath $ExpectedStdoutPath -ActualPath $ActualStdoutPath -Label $Label
    }
}

$invokableCCompiler = ConvertTo-InvokableCCompiler -InputName $Compiler
$invokableCxxCompiler = ConvertTo-InvokableCxxCompiler -InputName $Compiler

Push-Location
try {
    Set-Location "$PSScriptRoot/.."

    Write-Output "[lumine2024 shell] configuration..."
    Remove-DirectoryIfExists -Path "build"
    New-Item -ItemType Directory -Path "build" | Out-Null
    cmake -S . -B "build" -G Ninja -D CMAKE_C_COMPILER=$invokableCCompiler -D CMAKE_CXX_COMPILER=$invokableCxxCompiler
    Test-LastExitCode -Message "configure"

    Write-Output "[lumine2024 shell] build..."
    cmake --build "build"
    Test-LastExitCode -Message "build"

    Write-Output "[lumine2024 shell] test..."
    $binaryName = if($IsWindows) { "lumine2024_shell.exe" } else { "lumine2024_shell" }
    $binaryPath = Join-Path "build/src" $binaryName
    $artifactRoot = "build/test-artifacts"
    $caseArtifactDir = Join-Path $artifactRoot "cases"
    $redirectArtifactDir = Join-Path $artifactRoot "redirect"
    $pythonCommand = Resolve-PythonCommand

    Remove-DirectoryIfExists -Path $artifactRoot
    New-Item -ItemType Directory -Path $caseArtifactDir -Force | Out-Null
    New-Item -ItemType Directory -Path $redirectArtifactDir -Force | Out-Null

    New-RenderedCase -TemplatePath "tests/cases/basic.lsh.in" -OutputPath (Join-Path $caseArtifactDir "basic.lsh") -PythonCommand $pythonCommand
    New-RenderedCase -TemplatePath "tests/cases/pipeline.lsh.in" -OutputPath (Join-Path $caseArtifactDir "pipeline.lsh") -PythonCommand $pythonCommand
    New-RenderedCase -TemplatePath "tests/cases/redirect_io.lsh.in" -OutputPath (Join-Path $caseArtifactDir "redirect_io.lsh") -PythonCommand $pythonCommand

    Invoke-ShellCase `
        -BinaryPath $binaryPath `
        -CasePath (Join-Path $caseArtifactDir "basic.lsh") `
        -ActualStdoutPath (Join-Path $artifactRoot "basic.stdout") `
        -ExpectedStdoutPath "tests/expected/basic.stdout" `
        -Label "basic"

    Invoke-ShellCase `
        -BinaryPath $binaryPath `
        -CasePath (Join-Path $caseArtifactDir "pipeline.lsh") `
        -ActualStdoutPath (Join-Path $artifactRoot "pipeline.stdout") `
        -ExpectedStdoutPath "tests/expected/pipeline.stdout" `
        -Label "pipeline"

    Invoke-ShellCase `
        -BinaryPath $binaryPath `
        -CasePath (Join-Path $caseArtifactDir "redirect_io.lsh") `
        -ActualStdoutPath (Join-Path $artifactRoot "redirect.stdout") `
        -Label "redirect"

    Assert-EmptyFile -Path (Join-Path $artifactRoot "redirect.stdout") -Label "redirect stdout stream"

    Assert-FileContent `
        -ExpectedPath "tests/expected/redirect/stdout.txt" `
        -ActualPath (Join-Path $redirectArtifactDir "stdout.txt") `
        -Label "redirect stdout"

    Assert-FileContent `
        -ExpectedPath "tests/expected/redirect/stderr.txt" `
        -ActualPath (Join-Path $redirectArtifactDir "stderr.txt") `
        -Label "redirect stderr"

    Assert-FileContent `
        -ExpectedPath "tests/expected/redirect/combined.txt" `
        -ActualPath (Join-Path $redirectArtifactDir "combined.txt") `
        -Label "redirect combined"

    Write-Output "[lumine2024 shell] Success!"
} finally {
    Pop-Location
}
