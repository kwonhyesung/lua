$key = "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\msw.exe"
New-Item -Path $key -Force | Out-Null
New-ItemProperty -Path $key -Name "DumpFolder" -Value "C:\Users\kwon\Desktop\luahook\out\crashdumps" -PropertyType ExpandString -Force | Out-Null
New-ItemProperty -Path $key -Name "DumpType" -Value 2 -PropertyType DWord -Force | Out-Null
New-ItemProperty -Path $key -Name "DumpCount" -Value 5 -PropertyType DWord -Force | Out-Null
Write-Host "설정 완료:"
Get-ItemProperty -Path $key | Select-Object DumpFolder, DumpType, DumpCount
