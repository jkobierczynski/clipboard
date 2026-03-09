# Clipboard History Manager

A lightweight Windows system tray application written in C that maintains a stack of clipboard entries, lets you re-copy any past entry from a context menu, and supports a pop-and-paste hotkey for rapid sequential pasting.

## Features

- Automatically captures every `Ctrl+C` copy into a history stack (up to 50 entries)
- Re-copy any past entry via the tray icon context menu
- Pop the most recent entry and paste it directly with `Ctrl+Shift+V`
- Deduplication: copying the same text twice moves it to the top instead of duplicating it
- History is persisted to disk and restored on next launch
- Runs silently with no visible window — only a system tray icon

## Usage

### Hotkeys

| Hotkey | Action |
|---|---|
| `Ctrl+C` | Copy selected text as normal — also automatically pushed to the history stack |
| `Ctrl+Shift+V` | Pop the most recent history entry, put it on the clipboard, and paste it into the active window |

> `Ctrl+Shift+V` is destructive: the entry is removed from the stack after pasting. Press it repeatedly to paste entries in reverse-copy order.

### Tray icon

Left-click or right-click the tray icon to open the history menu.

- Each entry is shown truncated to 60 characters.
- Clicking an entry copies it back to the clipboard (non-destructive — the entry stays in the stack) and shows a brief balloon notification.
- **Exit** at the bottom of the menu closes the application.

## Building

Open `clipboard.slnx` in Visual Studio and build, or compile directly with MSVC:

```
cl /W4 /O2 /subsystem:windows /entry:wWinMainCRTStartup clipboard.c
```

No third-party dependencies are required. The Windows SDK libraries (`user32.lib`, `shell32.lib`) are pulled in automatically via `#pragma comment` directives in the source.

## Files

| File | Description |
|---|---|
| `clipboard.c` | Full source — single translation unit |
| `clipboard.vcxproj` | Visual Studio MSBuild project |
| `clipboard.slnx` | Visual Studio solution |
| `clipboard_history.txt` | Persisted history (UTF-8, created automatically on first run) |

## Requirements

- Windows Vista or later (uses `AddClipboardFormatListener`, available since Vista)
- MSVC or any compiler targeting the Windows SDK
