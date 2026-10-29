param([string]$Ndk = 'C:/Users/tsuka/AppData/Local/Android/Sdk/ndk/28.2.13676358')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'artifacts/android-task-stop-check'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$toolset=Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC' -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
$sdk=Get-ChildItem 'C:/Program Files (x86)/Windows Kits/10/Include' -Directory |
    Where-Object { Test-Path (Join-Path $_.FullName 'um/Windows.h') } |
    Sort-Object Name -Descending | Select-Object -First 1
if(!$toolset -or !$sdk) { throw 'MSVC/Windows SDK is required for the host-only console tests' }
function Run-Tool([string]$exe,[string]$arguments,[string]$directory) {
    $start=[Diagnostics.ProcessStartInfo]::new($exe,$arguments)
    $start.WorkingDirectory=$directory
    $start.UseShellExecute=$false
    $start.CreateNoWindow=$true
    $start.RedirectStandardOutput=$true
    $start.RedirectStandardError=$true
    # 子プロセスの環境だけを変更し、競合する PATH/Path を作らない。
    foreach($key in @($start.Environment.Keys)) { if($key -ieq 'path') { $start.Environment.Remove($key) | Out-Null } }
    $start.Environment['INCLUDE']="$($toolset.FullName)/include;$($sdk.FullName)/ucrt;$($sdk.FullName)/shared;$($sdk.FullName)/um"
    $sdkLib=Join-Path 'C:/Program Files (x86)/Windows Kits/10/Lib' $sdk.Name
    $start.Environment['LIB']="$($toolset.FullName)/lib/x64;$sdkLib/ucrt/x64;$sdkLib/um/x64"
    $start.Environment['VSLANG']='1033'
    $process=[Diagnostics.Process]::Start($start)
    $stdout=$process.StandardOutput.ReadToEndAsync()
    $stderr=$process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    Write-Output $stdout.Result
    Write-Output $stderr.Result
    if($process.ExitCode -ne 0) { throw "$exe failed: $($process.ExitCode)" }
}
$cl=Join-Path $toolset.FullName 'bin/Hostx64/x64/cl.exe'
$link=Join-Path $toolset.FullName 'bin/Hostx64/x64/link.exe'
foreach($file in @('cwxeditor_src/lib/editor_task_group.cpp','tests/editor_task_group_tests.cpp')) {
    $base=[IO.Path]::GetFileNameWithoutExtension($file)
    Run-Tool $cl ('/nologo /c /std:c++20 /EHsc /MT /O2 /utf-8 /W4 /DNDEBUG /I"{0}/src" /Fo"{1}/{2}.obj" "{0}/src/{3}"' -f $root,$output,$base,$file) $output
}
$testExe=Join-Path $output 'editor_task_group_tests.exe'
Run-Tool $link ('/nologo /SUBSYSTEM:CONSOLE /OUT:"{0}" "{1}/editor_task_group.obj" "{1}/editor_task_group_tests.obj"' -f $testExe,$output) $output
# Windows のコンソール検査だけをリンク・実行し、Android の製品や GUI は起動しない。
for($iteration=0;$iteration -lt 10;++$iteration) { Run-Tool $testExe '' $output }
Write-Output 'TaskGroup non-GUI contracts passed (10 runs).'
$clang=Join-Path $Ndk 'toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe'
foreach($abi in @('arm64-v8a','x86_64')) {
    $dbPath=Join-Path $root "src/platform/android/app/.cxx/tools/allRelease/$abi/compile_commands.json"
    $db=Get-Content -LiteralPath $dbPath -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach($name in @('editor_task_group.cpp','portable_runtime.cpp')) {
        $entry=@($db | Where-Object { [IO.Path]::GetFileName($_.file) -eq $name })
        # 生成済みの ABI データベースが TaskGroup 導入前の場合がある。同
        # じ cwxeditor_lib のコンパイルフラグを再利用し、別作業の Gradle/CMake
        # ツリーを再生成したり、異なる ABI を流用したりしない。
        if($entry.Count -eq 0 -and $name -eq 'editor_task_group.cpp') {
            $entry=@($db | Where-Object { [IO.Path]::GetFileName($_.file) -eq 'portable_runtime.cpp' })
        }
        if($entry.Count -ne 1) { throw "Expected one ALL compile command for $abi/$name" }
        $command=$entry[0].command
        $argumentStart=$command.IndexOf(' --target=')
        if($argumentStart -lt 0 -or $command -notmatch [regex]::Escape('28.2.13676358')) {
            throw "Unexpected Android compiler command: $abi/$name"
        }
        $arguments=$command.Substring($argumentStart+1)
        $arguments=$arguments -replace '-c\s+.*$', ('-c "{0}/src/cwxeditor_src/lib/{1}"' -f $root,$name)
        $object=Join-Path $output "$abi-$name.o"
        $arguments=$arguments -replace '-o\s+(?:"[^"]+"|\S+)', ('-o "{0}"' -f $object)
        if($arguments -notmatch ' -c ') { throw 'Android validation must compile only, without linking' }
        Run-Tool $clang $arguments $entry[0].directory
        if(!(Test-Path -LiteralPath $object)) { throw "Missing Android object $object" }
        Write-Output "Android ALL $abi compiled $name (no link)."
        if($name -eq 'editor_task_group.cpp') {
            $arguments=$arguments -replace '-o\s+(?:"[^"]+"|\S+)', ('-o "{0}/{1}-editor_task_group_tests.o"' -f $output,$abi)
            $arguments=$arguments -replace '-c\s+.*$', ('-c "{0}/src/tests/editor_task_group_tests.cpp"' -f $root)
            Run-Tool $clang $arguments $entry[0].directory
            Write-Output "Android ALL $abi compiled task group contracts (no link/run)."
        }
    }
}
