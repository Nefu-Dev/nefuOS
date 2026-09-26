$p = "D:\mycppos1\nefuOS\backends\win32\win32.cpp"
$t = [System.IO.File]::ReadAllText($p)
$old = '        if (s_savefs) {
            platform_fs_save();
            printf("fs saved\n");
        }'
$new = '        if (s_savefs) {
            // Serialize the in-memory VFS into the NVFS image and persist it,
            // so test runs can verify files created/edited inside the OS.
            nefuos_shutdown();
            printf("fs saved\n");
        }'
if ($t.Contains($old)) { $t = $t.Replace($old, $new) } else { Write-Output "PATTERN NOT FOUND" }
[System.IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding $false))
Write-Output "done"
