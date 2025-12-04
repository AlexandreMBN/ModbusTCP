# Script para remover emoticons de arquivos
$ErrorActionPreference = "Continue"

# Lista de emoticons comuns para remover
$emoticons = @(
    '📋', '📄', '🔧', '✅', '❌', '⚠️', '📂', '📡', '🚀', '📝', 
    '🔍', '🔄', '📊', '🟢', '🟡', '🔴', '📢', '🌐', '💾', '⚡', 
    '💻', '🎯', '✨', '🔑', '🛡️', '🔐', '📦', '🎨', '🏗️', '🔗',
    '📈', '📉', '🔀', '🔁', '🆕', '🆙', '🔒', '🔓', '⚙️', '🖥️',
    '📱', '🌍', '🌎', '🌏', '💡', '📶', '☁️', '⏱️', '⏳', '⏭️'
)

# Pastas e arquivos para processar
$folders = @(
    "src",
    "include",
    "readmes"
)

$extensions = @("*.c", "*.h", "*.md")

$stats = @{}
$totalRemoved = 0

foreach ($folder in $folders) {
    $path = Join-Path (Get-Location) $folder
    
    if (Test-Path $path) {
        Write-Host "`nProcessando pasta: $folder" -ForegroundColor Cyan
        
        foreach ($ext in $extensions) {
            $files = Get-ChildItem -Path $path -Filter $ext -Recurse -File
            
            foreach ($file in $files) {
                try {
                    $content = [System.IO.File]::ReadAllText($file.FullName, [System.Text.UTF8Encoding]::new($false))
                    $originalLength = $content.Length
                    $fileRemovedCount = 0
                    
                    foreach ($emoticon in $emoticons) {
                        $count = ([regex]::Matches($content, [regex]::Escape($emoticon))).Count
                        if ($count -gt 0) {
                            $fileRemovedCount += $count
                            $content = $content -replace [regex]::Escape($emoticon), ''
                        }
                    }
                    
                    if ($fileRemovedCount -gt 0) {
                        [System.IO.File]::WriteAllText($file.FullName, $content, [System.Text.UTF8Encoding]::new($false))
                        $stats[$file.Name] = $fileRemovedCount
                        $totalRemoved += $fileRemovedCount
                        Write-Host "  $($file.Name): $fileRemovedCount emoticons removidos" -ForegroundColor Green
                    }
                }
                catch {
                    Write-Host "  Erro ao processar $($file.Name): $_" -ForegroundColor Red
                }
            }
        }
    }
}

Write-Host "`n============================================" -ForegroundColor Yellow
Write-Host "RESUMO:" -ForegroundColor Yellow
Write-Host "Total de emoticons removidos: $totalRemoved" -ForegroundColor Green
Write-Host "Arquivos modificados: $($stats.Count)" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Yellow
