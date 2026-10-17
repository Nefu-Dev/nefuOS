# nefuOS `.ncode` Scripting Language Reference

`.ncode` is nefuOS's self-developed scripting language: no STL, a self-developed AST-based interpreter (`core/apps/ncode.cpp`). It directly integrates the entire set of system APIs from `sysapi.h` into the language, and provides the `start_r_os()` runtime windowed desktop.

- **Identification**: a file that starts with `#nefu`, or one with the `.ncode` extension.
- **Entry points**: terminal `run file.ncode`, `npkg start <pkg>`, or the terminal's exec branch recognizes and executes it directly.
- **Error output**: `ncode: line N: <message>`. The `<message>` text is emitted in English.

---

## 1. Lexical structure

### 1.1 Comments
Both `#` and `//` are **line comments** (they extend to the end of the line). The `#nefu` on the first line is consumed by the comment rule and serves as the magic marker.

```ncode
# this is a comment
var x = 1;  // this is also a comment
```

### 1.2 Identifiers
Composed of letters, digits, and underscores, and **cannot start with a digit** (`n_is_id_char`: `a-z A-Z 0-9 _`).

### 1.3 Keywords
```
var  let  func  if  else  while  for
break  continue  return  true  false  null  import  main
```

### 1.4 Literals
| Kind | Example | Description |
|---|---|---|
| Decimal integer | `42` `-7` | 64-bit integer |
| Hex integer | `0x3A6EA5` `0xFF` | starts with `0x` / `0X` (often used for colors) |
| Float | `3.14` `0.5` | a literal containing a decimal point |
| String | `"hello"` | double-quoted string |
| Boolean | `true` `false` | |
| Null | `null` | |

### 1.5 Operator tokens
- Arithmetic: `+  -  *  /  %`
- Comparison: `==  !=  <  >  <=  >=`
- Logical: `&&  ||  !`
- Assignment: `=`
- Increment/decrement: `++  --` (postfix and unary prefix)
- Other: `[ ]  ( )  { }  ,  ;  .`

---

## 2. Types and values

| Type | Description |
|---|---|
| `int` | 64-bit integer (including hex) |
| `float` | double-precision float |
| `str` | string; member methods see §8.1 |
| `bool` | `true` / `false` |
| `null` | null value |
| array | `[a, b, c]`; member methods see §8.2 |

Value semantics + variable binding: array `push/pop` directly mutate the array bound to the variable.

---

## 3. Importing libraries: `import<>`

`.ncode` uses `import<name>` to explicitly import a library; calling a function from a library that was not imported reports **`unknown function`** (gating).

```ncode
import<std>          # standard library: print/len/math/time/start_r_os etc.
import<sysapi>       # system library: the full sysapi.h API set (files/process/network/display/power/settings/time/notifications/apps)
```

- `import<std>`: see §9 for standard functions, plus string/array member methods (§8 requires it).
- `import<sysapi>`: see §10 for the full system API set.
- Both libraries can be imported at the same time.

---

## 4. Program structure

Top-level statements execute **before** `main()` (global variables / top-level logic take effect), and then `main()` is called.

```ncode
#nefu
import<std>
import<sysapi>

var g = 10;                  # top-level global variable

func add(a, b) { return a + b; }   # function definition

main() {                     # program entry point
  println("g=" + g + " sum=" + add(3, 5));
}
```

- `main()` is the only entry point; if it is missing, compilation errors out.
- Functions must be defined first as `func name(params) { statements }`; recursion is supported.

---

## 5. Statements

### 5.1 Variable declaration
```ncode
var a = 1;      # declare and initialize
var b;          # declare (initially null)
let c = 2.5;    # let is the same as var
```

### 5.2 Assignment
```ncode
a = 10;
```

### 5.3 Branching `if / else`
```ncode
if (x > 0) { println("pos"); }
else if (x == 0) { println("zero"); }
else { println("neg"); }
```

### 5.4 Loop `while`
```ncode
var i = 0;
while (i < 3) { println(i); i = i + 1; }
```

### 5.5 Loop `for(init; cond; post)`
```ncode
for (var i = 0; i < 5; i++) { println(i); }   # three-clause form
for (;;) { break; }                            # clauses may be omitted
```

### 5.6 `break / continue / return`
```ncode
while (true) { if (x) break; if (y) continue; }
func f() { return 7; }        # return may carry a value
```

### 5.7 Expression statements and blocks
Expressions terminated by `;`; `{ ... }` forms a block.

---

## 6. Expressions and operators

Precedence (low → high):

| Level | Operators |
|---|---|
| 1 (lowest) | `||` |
| 2 | `&&` |
| 3 | `==  !=  <  >  <=  >=` |
| 4 | `+  -` |
| 5 | `*  /  %` |
| 6 (unary) | `-` (negate), `!` (not), prefix `++` / `--` |
| 7 (postfix, highest) | postfix `++`/`--`, index `a[i]`, member `obj.x`, call `f(...)` |

```ncode
var n = 2 + 3 * 4;        # 14
var ok = (a > 0) && !b;   # short-circuit
var k = 0;
k++; k--;                 # postfix inc/dec, returns the old value
```

---

## 7. Indexing / calls / members

```ncode
var a = [10, 20, 30];
println(a[1]);            # 20 (0-based)

func hi(w) { println("hi " + w); }
hi("nefuOS");             # function call

var s = "AbC";
println(s.len);           # member access (methods in §8)
```

---

## 8. Member methods (require `import<std>`)

### 8.1 String methods
| Method | Description |
|---|---|
| `.len` | length (int) |
| `.upper()` | uppercase |
| `.lower()` | lowercase |
| `.substr(start, len)` | substring (`start` is 0-based; negatives are treated as 0) |
| `.find(sub)` | find a substring, returns the start index (int, 0-based; negative if not found) |
| `.contains(sub)` | whether it contains the substring (bool) |
| `.split(sep)` | split by the separator, returns an array |
| `.trim()` | trim leading/trailing whitespace (space / `\t` / newline) |

### 8.2 Array methods
| Method | Description |
|---|---|
| `.len` | element count (int) |
| `.push(v)` | append to the end (mutates the array bound to the variable) |
| `.pop()` | pop from the end |
| `.get(i)` | get the i-th element |
| `.join(sep)` | join into a string with the separator |

---

## 9. Standard library functions (`import<std>`)

| Function | Description |
|---|---|
| `print(x, ...)` / `println(x, ...)` | output; `println` appends a newline |
| `len(x)` | string/array length |
| `str(x)` | convert to string |
| `int(x)` | convert to integer |
| `float(x)` | convert to float |
| `abs(x)` | absolute value |
| `min(a, b)` / `max(a, b)` | min / max |
| `sqrt(x)` | square root |
| `pow(x, y)` | x raised to the y-th power |
| `rand()` | random number |
| `time_ms()` | current time in milliseconds |
| `sleep(ms)` | sleep |
| `args()` | extra command-line arguments (array) |
| `type(x)` | return the type name (int/float/str/bool/arr/func/os/null...) |
| `start_r_os()` | launch the runtime windowed desktop, see §11 |

---

## 10. System library API (`import<sysapi>`, the full set from `sysapi.h`)

### File system
| Function | Description |
|---|---|
| `file_open(path, flags)` | open a file, return its fd (flags bit 0=read, 1=write; creates the file if it is missing when writing); returns `-1` on failure |
| `file_close(fd)` | close |
| `file_read(fd, n)` | read n bytes, returns a string |
| `file_write(fd, data)` | write, returns the number of bytes written |
| `file_seek(fd, off, whence)` | move the position (0=start, 1=current, 2=end) |
| `file_size(fd)` | file size |
| `file_exists(path)` | whether it exists |
| `file_read_all(path)` | read the whole file at once |
| `file_write_all(path, data)` | write the whole file at once |
| `dir_mkdir(path)` | create a directory |
| `dir_list(path)` | list a directory |
| `file_remove(path)` | remove a file/directory |
| `file_rename(old, new)` | rename |

### Process / network / display
| Function | Description |
|---|---|
| `process_pid()` | current process ID |
| `process_exit(code)` | exit the current script |
| `http_get(url)` | HTTP GET (returns the body; fails when there is no network) |
| `network_available()` | whether the network is available |
| `screen_width()` / `screen_height()` / `screen_bpp()` | screen size / color depth |

### Power / settings / time / notifications / apps
| Function | Description |
|---|---|
| `power_shutdown()` / `power_reboot()` / `power_reboot_to_bios()` / `power_suspend()` | power management |
| `settings_get(key)` / `settings_set(key, val)` / `settings_save()` | settings |
| `time_now()` / `time_uptime()` | Unix time / uptime |
| `notify(title, msg)` / `notification_count()` / `notification_mark_all_read()` / `notification_clear()` | notification center |
| `app_launch(id or app name)` / `app_list()` / `app_installed(id or app name)` | app management |

> Note: the file APIs are implemented directly on the VFS (including a global fd table) and do not go through the sysapi stub.

---

## 11. The `start_r_os()` runtime windowed desktop

After `import<std>`, call `start_r_os()` to launch an **empty desktop window** (customizable by the developer), returning an OS object.

```ncode
var os = start_r_os();
os.title("My Desktop");
os.background(0x1F2430);
os.fill(10, 10, 200, 100, 0x3A6EA5);      # filled rectangle
os.rect(10, 10, 200, 100, 0x4A90D9);       # stroked rectangle
os.line(20, 200, 300, 200, 0x6B7BA3);
os.circle(160, 120, 30, 0xE8A33D);
os.pixel(160, 120, 0xFFFFFF);
os.text(30, 40, 0xFFFFFF, "nefuOS desktop");
```

### OS object methods
| Method | Description |
|---|---|
| `title("...")` | set the window title |
| `background(color)` | set the background color |
| `fill(x, y, w, h, color)` | filled rectangle |
| `rect(x, y, w, h, color)` | stroked rectangle |
| `line(x1, y1, x2, y2, color)` | line |
| `circle(cx, cy, r, color)` | circle |
| `pixel(x, y, color)` | single pixel |
| `text(x, y, color, "str")` | text (TTF) |
| `save()` | save the desktop immediately |
| `load()` | restore the last saved desktop |
| `list()` | list sandbox files |
| `remove("filename")` | delete a sandbox file (bare filename only) |
| `on_key("func")` | bind a keyboard callback (3 args: `keycode, ascii, down`) |
| `on_mouse("func")` | bind a mouse callback (3 args: `x, y, buttons`) |
| `set_interval(ms, "func")` | periodic callback (0 args) |
| `width` / `height` | desktop size |
| `close()` | close the desktop |
| `clear()` | clear drawing operations |

### Auto-save and sandbox
- **Auto-save**: the desktop is serialized and saved automatically every **0.5 seconds** (written to the sandbox `desktop.save`), and can be restored with `load()` after a reboot.
- **Sandbox restrictions**: `save/load/remove` accept only bare filenames (they reject `/`, `\`, `..`, and subdirectories); reads and writes are limited to the sandbox directory `/home/user/ncode/<package-name-or-filename-minus-extension>/` — only the user's local files can be read/written, and other system directories cannot be accessed.
- Callbacks run in the global environment and can access global state such as the top-level `var os;`.

```ncode
import<std>
var os;                 # global, for callbacks to use
var frame = 0;

func tick() {           # set_interval callback: 0 arguments
  frame = frame + 1;
  os.fill(16, 16, 200, 40, 0x1F2A3E);
  os.text(28, 26, 0xFFFFFF, "frame: " + frame);
}

func onKey(k, a, down) {          # on_key callback: 3 arguments
  if (down == 0) return;
  if (a == 98) os.background(0x0A1428);   # 'b'
}

func onMouse(x, y, b) {           # on_mouse callback: 3 arguments
  if (b != 0) os.pixel(x, y, 0xFFFFFF);
}

main() {
  os = start_r_os();
  os.title("demo");
  os.background(0x10131C);
  os.set_interval(120, "tick");
  os.on_key("onKey");
  os.on_mouse("onMouse");
  os.load();                       # restore the last desktop
  println("window ready: " + os.width + "x" + os.height);
}
```

---

## 12. Complete example

```ncode
#nefu
import<std>
import<sysapi>

func fib(n) {
  if (n < 2) return n;
  return fib(n-1) + fib(n-2);
}

main() {
  println("fib(10) = " + fib(10));                       # 55

  file_write_all("/home/user/ncode/demo/out.txt", "hello nefuOS\n");
  println(file_read_all("/home/user/ncode/demo/out.txt"));

  var s = "  Hello, nefuOS!  ";
  println(s.trim().upper());                              # HELLO, NEFUOS!
  var a = [1, 2, 3];
  a.push(9);
  println(a.join("-"));                                   # 1-2-3-9

  var os = start_r_os();
  os.title("demo");
  os.fill(10, 10, 200, 100, 0x3A6EA5);
  os.text(20, 30, 0xFFFFFF, "nefuOS .ncode demo");
}
```

---

## 13. Common errors
- `ncode: line N: unknown function: xxx` — called a function from a library that was not `import`ed.
- `ncode: line N: missing main()` / mentions `main` — the program entry point is missing.
- `ncode: line N: expected ')'` etc. — a syntax error; N is the line number where the error occurred.

> In all of the above diagnostics, the `<message>` portion after `line N:` is emitted in English.
