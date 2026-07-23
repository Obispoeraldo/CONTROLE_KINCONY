# Compila o firmware usando a copia do worktree em caminho ASCII (C:\esp-build\CONTROLE_KINCONY_integration).
# Necessario porque o xtensa-esp-elf-gcc nao compila com "Area de Trabalho" (acento) no caminho do projeto.
# Uso: .\build-ascii.ps1 [args extras para idf.py, ex: flash monitor]

$origem = $PSScriptRoot
$destino = "C:\esp-build\CONTROLE_KINCONY_integration"

if (-not (Test-Path $destino)) {
    throw "Copia ASCII nao encontrada em $destino. Crie o worktree la antes de rodar este script."
}

Write-Host "Sincronizando main/ para $destino ..."
robocopy "$origem\main" "$destino\main" /MIR /NFL /NDL /NJH /NJS
if ($LASTEXITCODE -ge 8) { throw "robocopy falhou (codigo $LASTEXITCODE)" }

$env:PATH = "C:\Users\erald\AppData\Local\Programs\Python\Python312;C:\Users\erald\AppData\Local\Programs\Python\Python312\Scripts;" + $env:PATH
$env:PYTHONUTF8 = "1"
$env:PYTHONIOENCODING = "utf-8"
. "C:\esp\v6.0.1\esp-idf\export.ps1" *>$null

Set-Location $destino
if ($args.Count -eq 0) {
    idf.py build
} else {
    idf.py @args
}
