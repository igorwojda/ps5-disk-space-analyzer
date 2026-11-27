# Storage Space Analyzer for PS5

Find what takes space on a jailbroken PS5 and clean it up: **scan → select → clean up**.

`storage-analyzer.elf` is a payload that runs in the background and serves a web
UI. It adds a **Storage Space Analyzer** tile to the **Media** tab that opens the
UI in the PS5 browser. The same UI, with every feature, works from a phone or
PC on the same network: the bar at the bottom of the screen shows the app
version and the address to open (`http://<ps5-ip>:8833/`).

- Storage picker: whole volumes only: internal storage (`/user`), M.2 /
  extended storage, USB drives, the system partition. Folders on a volume
  are not offered. Shows used and free space for each.
- Folder list sorted by size next to a sunburst chart (4 levels deep). The
  focused item lights up in the chart.
- **Biggest Files** tab: the 500 largest files.
- Select files and folders, check the review screen (space you get back, free
  space afterwards), then **hold ✕** to delete.
- System locations are locked and cannot be selected.

## Install

1. Send `storage-analyzer.elf` to the console's ELF loader (port 9021):
   `make send PS5_HOST=<ip>`, or `nc -q0 <ip> 9021 < storage-analyzer.elf`.
2. A toast says "Storage Space Analyzer 1.0.0 is running" with the address.
3. Open **Media → Storage Space Analyzer**, or browse to the address from the
   toast (also shown in the bottom bar) on a computer or phone.

Send the ELF again to update it: the new copy replaces the running one. The
payload stops when the console reboots, so send it again after a restart (or
add it to your autoload). The tile stays installed.

## Controls

| Action | DualSense | Keyboard / mouse |
| --- | --- | --- |
| Move focus | D-pad | Arrow keys |
| Open folder / confirm | ✕ or D-pad → | Enter / → |
| Back / parent folder | ○ or D-pad ← | Esc, Backspace / ← |
| Select for cleanup | □ (or click the checkbox) | Space |
| Review & clean | △ (or the green button) | Tab / Delete |
| Switch tab | L1 / R1 | Q / E |
| Page up / down | L2 / R2 | PgUp / PgDn |
| Menu (rescan, clear selection, change storage, stop) | Options | M |
| Cursor | Left stick, ✕ clicks | Mouse |

With the cursor you can click rows, checkboxes, tabs, the path, the hints, and
chart segments. Clicking the centre of the chart goes up a level. Hovering
over a segment shows its size. ✕ acts on the item under the cursor if you
moved the cursor since your last D-pad press. Otherwise it acts on the
focused item. ○ goes back step by step. Press it twice on the storage list to
close the browser.

The PS5 browser sends △, □, L1, R1, L2 and R2 as the keys F1, F2 and F5–F8,
which the app handles. It may not pass Options to web pages; everything is
also clickable with the cursor.

## Safety

- You can never select: anything directly under `/`, `/system*`, `/preinst*`,
  `/update`, `/dev`, `/proc`, `/mnt/sandbox`, `/user/system`, `/user/license`,
  `/user/settings`, volume roots, or the top folders of the user partition
  (`/user/app`, `/user/home`, ...). These rows show a lock. The server checks
  this again before it deletes anything.
- Deleting asks for confirmation, and you must **hold ✕ for 1.2 s**.
- Deleting does not follow symlinks and does not cross into other volumes.
- The app warns you when a selection contains installed game data. It
  deletes the files but does not update the PS5 game library.
- A scan stays on one volume, and hard-linked files are counted once.

## Troubleshooting

- **Log file:** `/data/storage-analyzer/storage-analyzer.log`.
- **Tile missing:** the log says why (`launcher ...` lines). The UI still
  works at `http://<ps5-ip>:8833/`.
- **Port 8833 busy:** the payload tries the next ports. The toast and the
  tile use the real port.

## Build

Needs `cmake`, `clang` and `lld` (macOS: `brew install llvm lld cmake`).

```sh
make toolchain   # PS5 payload SDK into .toolchain (~700 MB, no sudo)
make package     # -> dist/storage-analyzer.elf
```

The version comes from `project(... VERSION x.y.z)` in `CMakeLists.txt`.

Development on macOS or Linux, using the same server code:

```sh
make run                                   # http://localhost:8833/, UI served from web/
./build-desktop/storage-analyzer ~/Downloads  # offer an extra folder to scan
```

## Layout

| File | Purpose |
| --- | --- |
| `src/main.cpp` | Payload start: single instance, port, tile, toast |
| `src/http.*` | Minimal HTTP server (thread per connection) |
| `src/api.*` | JSON API: version/address, storages, scan, folders, biggest files, delete |
| `src/tree.*` | File tree, background scanner, background deleter |
| `src/platform.*` | Storage list, free space, protected paths, warnings |
| `src/launcher.*` | Media tile (`/user/app/SSAN00001`, category 65536, `deeplinkUri`) |
| `src/system.*` | Log file and PS5 notifications |
| `web/` | The UI: `index.html`, `app.css`, `app.js` (compiled into the ELF) |

The tile uses the same mechanism as
[ps5-web-file-manager](https://github.com/owendswang/ps5-web-file-manager).
Font: Lato (SIL Open Font License).
