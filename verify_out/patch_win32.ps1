$p = "D:\mycppos1\nefuOS\backends\win32\win32.cpp"
$t = [System.IO.File]::ReadAllText($p)
$old = '        if (strcmp(argv[i], "--click") == 0 && i + 2 < argc) {
            s_click_x = atoi(argv[i + 1]);
            s_click_y = atoi(argv[i + 2]);
            }'
$new = '        if (strcmp(argv[i], "--click") == 0 && i + 2 < argc) {
            s_click_x = atoi(argv[i + 1]);
            s_click_y = atoi(argv[i + 2]);
            }
        if (strcmp(argv[i], "--click2") == 0 && i + 2 < argc) {
            s_click2_x = atoi(argv[i + 1]);
            s_click2_y = atoi(argv[i + 2]);
            }
        if (strcmp(argv[i], "--savefs") == 0) s_savefs = true;'
if ($t.Contains($old)) { $t = $t.Replace($old, $new) } else { Write-Output "PATTERN NOT FOUND" }
[System.IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding $false))
Write-Output "done"
