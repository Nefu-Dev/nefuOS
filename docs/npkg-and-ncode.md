# nefuOS npkg Package Manager and the .ncode Scripting Language

## 1. The npkg package manager

npkg is nefuOS's command-line package management system, integrated through terminal extension commands (`term_ext`).

### Data source
- Fixed server: `http://npkg.site.je` (**cannot be overridden**; the `npkg update <url>` syntax has been removed, so the source is always the default).
- The server root provides `package.json`, in the format:
  ```json
  {
    "app": {
      "nefud": {
        "Introduction": "a text for nefuOS NPKG",
        "version": "1.0.0",
        "server_in": "/htdocs/nefud",
        "deps": ["sysapi.h", "std/*/**"]
      }
    }
  }
  ```
- `server_in` holds the **full path** on the server; npkg takes the part **after the last `htdocs/`** as the package's relative content directory (i.e. the package's relative path under the server's document root).
  Example: `server_in: "/htdocs/nefud"` → relative directory `nefud` → the entry file is fetched from `http://npkg.site.je/nefud/main.ncode`.
- An optional `manifest.json` (in the same directory as the entry file) lists, via its `files[]`, additional files beyond the entry, which are downloaded and installed together.

### Commands (all run in the nefuOS terminal)
```
npkg update                   refresh the registry (download the latest package.json, reconnect limit 3 times x 400ms)
npkg install <pkg>            install a package to /pkg/<pkg>/ according to the registry
npkg remove <pkg>             recursively delete /pkg/<pkg>
npkg start <pkg>[/<script>] [extra args...]   run the package's entry .ncode (default main.ncode)
npkg list                     list installed packages
```

- Installed artifacts: `/pkg/package.json` (registry cache), `/pkg/<pkg>/...` (package files).
- Entry fallback: look for `main.ncode` first; if that fails, look for `<pkg>.ncode`.
- Reconnect limit: `NPKG_MAX_RETRY = 3`, with `400ms` between attempts; reaching the limit reports `reconnect limit reached`.
- **NVFS persistence**: after `npkg update / install / remove` succeeds, the current VFS tree is immediately written to the NVFS image and flushed to disk (`nvfs::vfs_to_nvfs()` + `platform_fs_save`); the command output shows `npkg: persisted to NVFS (... bytes)`. Thus even if the system later crashes or is killed (without a normal shutdown), the installed/removed packages have already been truly written to the persistent filesystem `nefuos.fs`, and remain in effect on the next boot.

### Automatic bypass of the InfinityFree JS challenge
Free hosts like `npkg.site.je` first respond to **every path** with a JS anti-bot challenge page (containing `aes.js` / `slowAES` / `toNumbers`) instead of the real file. npkg automatically bypasses this in two steps at the `npkg_fetch` / `npkg_fetch_url` layer:
1. The first GET returns the challenge → parse `a=toNumbers("…"), b=toNumbers("…"), c=toNumbers("…")` (a is the AES key, b is the IV, c is the ciphertext).
2. The built-in `core/klib/aes128.{h,cpp}` (AES-128-CBC decryption with no STL, verified against NIST vectors) decrypts c to its 16-byte plaintext, which is lowercased to hex and becomes the `__test` cookie.
3. Resend the same request with `Cookie: __test=<hex>` (`platform_http_request`) → get the real file (package.json / main.ncode / manifest...).

The cookie changes per request, so it is recomputed on the fly for every fetch and is not cached. For local / non-challenge servers it is skipped automatically (content without `slowAES` is treated as a normal file).

### Direct-install syntax: `npkg install <pkg>:<server-path>`
`install` supports **direct install** without going through the registry: after `<pkg>`, write the package's entry path on the server directly after a colon; npkg downloads and parses that `main.ncode` and stores it as `/pkg/<pkg>/main.ncode`.
```
npkg install draw:npkg.site.je/draw/main.ncode
```
- The path after the colon is auto-prefixed with a protocol: if there is no `http://`/`https://` prefix, `http://` is added. The example above actually downloads `http://npkg.site.je/draw/main.ncode`.
- After install, it also tries to fetch the `files[]` additional files from `manifest.json` in the entry's directory (same as registry install).
- Suited to cases where the entry URL is known and no registry metadata (version/deps) is needed; afterwards you can run it with `npkg start draw`.

### Local test server (for verification)
The repository ships with a set of example packages; you can start an HTTP service to test end-to-end:
- Directory: `tools\npkg_test_server\`
- `package.json` (3 example packages: hello / nefud / draw)
- `hello\` (contains main.ncode + manifest.json + greeting.txt, demonstrating files[] and extra arguments)
- `nefud\` (spec example package, demonstrating htdocs/ truncation)
- `draw\` (demonstrates the `start_r_os()` window)

Start it: under `tools\npkg_test_server`, run `python -m http.server 8000`,
then in the terminal run `npkg install hello` and `npkg start hello alpha beta`.
(Note: the source is fixed to `npkg.site.je`; for local testing you must use the direct-install syntax `npkg install <pkg>:<localURL>`.)

## 2. The .ncode scripting language

`.ncode` is nefuOS's self-developed scripting language (no STL, AST interpreter, located in `core\apps\ncode.cpp`). Identification: a file starting with `#nefu` or with the `.ncode` extension.

### Syntax highlights
- Optional `#nefu` on the first line (magic marker).
- `import<std>` / `import<sysapi>`: import libraries. std provides print/println/len/str/int/float/abs/min/max/sqrt/pow/rand/time_ms/sleep/args/type/start_r_os; sysapi provides the full system API set (see below).
- `func name(params) { … }` defines a function; `main() { … }` is the program entry.
- `var`/`let` declarations; `if else`, `while`, `for(init;cond;post)`, `break/continue/return`.
- Types: int, float, str, bool, null, array `[a,b,c]`; integers support hex literals `0x…` (e.g. `0x3A6EA5`).
- Array methods `.len/.push/.pop/.get/.join`; string methods `.len/.upper/.lower/.substr/.find/.contains/.split/.trim`.
- Unary `-`/`!`, `&&`/`||` short-circuit, postfix `++`/`--`.
- Top-level statements execute before `main()` (global variables take effect).

### The full sysapi.h API set in the language (`import<sysapi>`)
Files: `file_open(file,flags)` / `file_close(fd)` / `file_read(fd,n)` / `file_write(fd,data)` /
`file_seek(fd,off,whence)` / `file_size(fd)` / `file_exists(path)` / `file_read_all(path)` /
`file_write_all(path,data)` / `dir_mkdir(path)` / `dir_list(path)` / `file_remove(path)` / `file_rename(old,new)`
Process: `process_pid()` / `process_exit(code)`
Network: `http_get(url)` / `network_available()`
Display: `screen_width()` / `screen_height()` / `screen_bpp()`
Power: `power_shutdown()` / `power_reboot()` / `power_reboot_to_bios()` / `power_suspend()`
Settings: `settings_get(key)` / `settings_set(key,val)` / `settings_save()`
Time: `time_now()` / `time_uptime()`
Notifications: `notify(title,msg)` / `notification_count()` / `notification_mark_all_read()` / `notification_clear()`
Apps: `app_launch(id or app name)` / `app_list()` / `app_installed(id or app name)`

### start_r_os() (runtime windowed desktop)
After `import<std>`, call `start_r_os()` to return an "OS object" and launch an **empty desktop window** (customizable by the developer):

```
var os = start_r_os();
os.title("My Desktop");
os.background(0x1F2430);        // background color
os.fill(x,y,w,h,color);         // filled rectangle
os.rect(x,y,w,h,color);         // stroked rectangle
os.line(x1,y1,x2,y2,color);
os.circle(cx,cy,r,color);
os.pixel(x,y,color);
os.text(x,y,color,"...");       // text (TTF)
os.save();                       // save the desktop immediately
os.load();                       // restore the last saved desktop
os.list();                       // list sandbox files
os.remove("filename");           // delete a sandbox file
os.on_key(func);                 // keyboard callback
os.on_mouse(func);               // mouse callback
os.set_interval(ms, func);       // periodic callback
os.width / os.height;            // desktop size
os.close();                      // close the desktop
```

**Auto-save**: the desktop is serialized and saved automatically every **0.5 seconds** (written to the sandbox `desktop.save`), and can be restored after reboot with `os.load()`/`n_os_restore`.

**Sandbox restrictions**: `save/load/remove` accept only bare filenames (they reject `/`, `\`, `..`, and subdirectories), and reads/writes are limited to the sandbox directory
`/home/user/ncode/<package-name-or-filename-minus-extension>/` — only the user's local files can be read/written, and other system directories cannot be accessed.

### Entry points
- Terminal `run file.ncode` or `npkg start <pkg>`
- `.ncode` is already recognized and executed by the terminal's `term_nefud_run` / exec branch.

### Example
```ncode
#nefu
import<std>
import<sysapi>
func fib(n) { if (n < 2) return n; return fib(n-1) + fib(n-2); }
main() {
  println("fib(10) = " + fib(10));
  file_write_all("/home/user/ncode/demo/out.txt", "hello nefuOS\n");
  var os = start_r_os();
  os.title("demo");
  os.fill(10, 10, 200, 100, 0x3A6EA5);
  os.text(20, 30, 0xFFFFFF, "nefuOS .ncode demo");
}
```
