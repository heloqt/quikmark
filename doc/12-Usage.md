# QuikMark Usage Guide

## Installation

Windows:

- The installer places QuikMark in the current user's profile and does not
  require administrator privileges.
- A Start menu shortcut is always created.
- The installation directory and Start menu include this usage guide.
- Desktop shortcut and Markdown file association are enabled by default and
  can be disabled on the components page.
- Disabling the association or uninstalling restores the previous file
  association whenever possible.
- The finish page can launch QuikMark, but it is not selected by default.

Linux:

- Install the Debian package with:

  ```bash
  sudo apt install ./quikmark_1.1.0_amd64.deb
  ```

- Make the AppImage executable before running it:

  ```bash
  chmod +x QuikMark-1.1.0-x86_64.AppImage
  ./QuikMark-1.1.0-x86_64.AppImage
  ```

- If FUSE is unavailable:

  ```bash
  APPIMAGE_EXTRACT_AND_RUN=1 ./QuikMark-1.1.0-x86_64.AppImage
  ```

- AppImage does not change the default Markdown application automatically.
  Use **File > Register File Associations...** when desired.

## 1. Opening Documents

Open Markdown documents by:

- selecting **File > Open...**
- dragging one or more `.md` or `.markdown` files into the window
- opening an associated Markdown file from the system file manager

Documents opened in the same process appear as tabs. Opening a file that is
already open activates its existing tab.

## 2. Menus

### File

| Item | Description |
| --- | --- |
| Open... | Select one or more Markdown documents |
| Recent Files | Open up to five recently used documents |
| Reload | Read the current document again |
| Register File Associations... | Register QuikMark for `.md` and `.markdown` files |
| Exit | Close QuikMark |

### View

| Item | Description |
| --- | --- |
| Find... | Open the temporary find bar |
| Appearance | System, light, or dark theme |
| Language | System language, English, or Simplified Chinese |
| Outline | Show or completely hide the outline sidebar |

### Help

| Item | Description |
| --- | --- |
| Usage Guide | Open this guide in a QuikMark tab; shortcut `F1` |
| About QuikMark | Show the version, build type, and wxWidgets version |

The Edit menu and the Close/Previous/Next Tab menu items are hidden to keep the
interface compact, but their shortcuts remain available.

## 3. Keyboard and Mouse

### Files and Tabs

| Shortcut | Action |
| --- | --- |
| `Ctrl+O` | Open a document |
| `Ctrl+R` | Reload the current document |
| `Ctrl+W` | Close the current tab |
| `Ctrl+Tab` | Activate the next tab |
| `Ctrl+Shift+Tab` | Activate the previous tab |

### Selection and Copying

| Shortcut | Action |
| --- | --- |
| Mouse drag | Select text across one or more blocks |
| Mouse drag in a table | Select text in a cell or across cells |
| `Ctrl+A` | Select the whole document |
| `Ctrl+C` | Copy the selection as plain text |

Right-click a table and choose **复制整个表格** (Copy Table) to place both:

- an HTML table, which pastes into Word as a table
- TSV text, which pastes into Excel, WPS, or a plain-text editor

Copying the whole table does not replace the current text selection.

### Finding

| Key | Action |
| --- | --- |
| `Ctrl+F` | Open the find bar |
| `F1` | Open the usage guide tab |
| `Enter` | Go to the next match |
| `F3` | Go to the next match |
| `Shift+Enter` | Go to the previous match |
| `Shift+F3` | Go to the previous match |
| `Esc` | Close the find bar and clear highlights |

Search is case-insensitive for English and matches Chinese text literally.

### Display

| Action | Description |
| --- | --- |
| `Ctrl+mouse wheel` | Change document font size |
| Mouse wheel | Scroll the document |
| `Shift+mouse wheel` | Scroll a wide table horizontally |

## 4. Outline Sidebar

- The outline and splitter handle are hidden until the outline is enabled.
- Enable it with **View > Outline**.
- Click the arrow on the splitter to collapse it to a narrow strip, then click
  the opposite arrow to expand it again.
- Disabling **View > Outline** hides the outline completely.
- The expanded sidebar remains pinned and the document reflows into the
  remaining width.
- Selecting an outline item scrolls to that heading.
- The hidden, expanded, and collapsed states are stored with the workspace.

## 5. Search

- Search starts automatically after entering a query.
- All matches use a light highlight; the active match is emphasized.
- The find bar uses no document width when closed.
- A single query is limited to 10,000 results.

## 6. Reading and Display

- Supports headings, paragraphs, bold, italic, strikethrough, lists, task
  lists, blockquotes, code blocks, tables, images, links, and basic Mermaid
  flowcharts.
- Local images and HTTP/HTTPS images load asynchronously.
- `http`, `https`, and `mailto` links use the system default application.
- Unsupported Markdown or Mermaid content falls back to its source text.
- System, light, and dark themes are available under **View > Appearance**.
- Menu language is available under **View > Language** and is saved for the
  next launch.

## 7. File Changes and Reading State

- A non-modal banner appears when the active document changes on disk.
- Choose **Reload** or **Later**.
- Changes to inactive tabs are marked on the corresponding tab.
- QuikMark saves window placement, tab order, active tab, scroll position, and
  outline state when it closes.
- Existing documents are restored on the next launch.

## 8. Current Version

- QuikMark is a read-only Markdown reader without editing or saving.
- The first launch after upgrading from liteMark migrates workspace, recent
  documents, and theme settings.
- Only one main window is used; subsequent launches forward files to the
  existing instance.
- PDF export is not currently available.
- UTF-8 and UTF-8 BOM are supported; other encodings display an error.
