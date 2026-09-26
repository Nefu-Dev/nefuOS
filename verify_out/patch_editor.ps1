$p = "D:\mycppos1\nefuOS\core\apps\editor.cpp"
$t = [System.IO.File]::ReadAllText($p)
$old = '    // Click to move cursor (clears any Shift selection)
    if (pressed && my > 35 && my < w->content_h - 20) {
        int line = (my - 35) / 16 + st->scroll_y;
        int col = (mx - 40 + st->scroll_x) / 8;
        if (line >= 0) st->cursor_y = line;
        if (col >= 0) st->cursor_x = col;
        st->has_sel = false;
    }'
$new = '    // Click to move cursor (clears any Shift selection). Clamp to the actual
    // text bounds: clicking far below the last line used to set cursor_y past
    // the line list, and the next keystroke read/wrote out of bounds -> crash.
    if (pressed && my > 35 && my < w->content_h - 20) {
        List<String> lines = split_lines(st->content);
        int line = (my - 35) / 16 + st->scroll_y;
        int col = (mx - 40 + st->scroll_x) / 8;
        if (line < 0) line = 0;
        if (line >= lines.size()) line = lines.size() - 1;
        if (col < 0) col = 0;
        if (col > lines[line].len()) col = lines[line].len();
        st->cursor_y = line;
        st->cursor_x = col;
        st->has_sel = false;
    }'
if ($t.Contains($old)) { $t = $t.Replace($old, $new) } else { Write-Output "PATTERN1 NOT FOUND" }

# defensive clamp in editor_insert
$old2 = '    st->undo_content = st->content;
    List<String> lines = split_lines(st->content);
    int x1, y1, x2, y2;'
$new2 = '    st->undo_content = st->content;
    List<String> lines = split_lines(st->content);
    if (st->cursor_y < 0) st->cursor_y = 0;
    if (st->cursor_y >= lines.size()) st->cursor_y = lines.size() - 1;
    if (st->cursor_x < 0) st->cursor_x = 0;
    if (st->cursor_x > lines[st->cursor_y].len()) st->cursor_x = lines[st->cursor_y].len();
    int x1, y1, x2, y2;'
if ($t.Contains($old2)) { $t = $t.Replace($old2, $new2) } else { Write-Output "PATTERN2 NOT FOUND" }

[System.IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding $false))
Write-Output "done"
