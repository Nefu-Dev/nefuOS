$p = "D:\mycppos1\nefuOS\backends\win32\win32.cpp"
$t = [System.IO.File]::ReadAllText($p)
$old = '                printf("keys injected: %s\n", s_key_str);
                s_key_str = 0;
            }
            // mirror the WM_TIMER handler: tick (window animations, media
            // frame grabs) then frame (paint)
            nefuos_tick();
            nefuos_frame(); Sleep(16);
            }
        save_bmp(s_shot_path);'
$new = '                printf("keys injected: %s\n", s_key_str);
                s_key_str = 0;
            }
            if (s_click2_x >= 0 && platform_tick_ms() - t0 > 3000) {
                // optional second click (e.g. click "Save" after typing)
                int cx = s_click2_x, cy = s_click2_y;
                nefuos_handle_mouse(cx, cy, 0x01);
                for (int f = 0; f < 4; f++) { nefuos_tick(); nefuos_frame(); Sleep(16); }
                nefuos_handle_mouse(cx, cy, 0);
                s_click2_x = -1;
                printf("clicked2 %d,%d\n", cx, cy);
            }
            // mirror the WM_TIMER handler: tick (window animations, media
            // frame grabs) then frame (paint)
            nefuos_tick();
            nefuos_frame(); Sleep(16);
            }
        if (s_savefs) {
            platform_fs_save();
            printf("fs saved\n");
        }
        save_bmp(s_shot_path);'
if ($t.Contains($old)) { $t = $t.Replace($old, $new) } else { Write-Output "PATTERN NOT FOUND" }
[System.IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding $false))
Write-Output "done"
