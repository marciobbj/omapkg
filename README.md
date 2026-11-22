# Omapkg

Omarchy menu bar update indicator (`marciobbj.omapkg`) and native Qt Quick package & application manager for **Omarchy 4.5** — inspect disk usage, browse installed apps and tools across **Omarchy**, **Pacman**, **AUR**, **Flatpak**, **Omarchy Plugins**, and **Mise**, apply updates, remove packages, and clean unused orphans, all following your active Omarchy theme and language live.

![Omapkg](preview.png)

## Features

- **Omarchy menu bar widget (`marciobbj.omapkg`)** — checks installed sources for waiting updates and security alerts, lights up with an accent badge for updates or urgent red for security advisories, sends deduplicated desktop notifications (`notify-send`) when new vulnerabilities or compromised/retired plugins appear, and opens or focuses Omapkg on click.
- **Security advisories & CVE alerts** — checks installed **Omarchy/Pacman** packages against the **Arch Linux Security Tracker**, **Omarchy Plugins** against the **Omarchy Plugin Marketplace** registry (retired/revoked plugins and failed security baselines), and **Mise** packages against **OSV.dev** (cached locally with `ETag` revalidation), surfacing alerts in a dedicated **Security** tab with clickable advisory links.
- **Unified package sources** — Omarchy (`[omarchy]`), Pacman (`core`/`extra`/`multilib`/custom repos), AUR (`yay`), Flatpak, **Omarchy Plugins** (`~/.config/omarchy/plugins`), and **Mise** (`mise` dev tools) — automatically detected.
- **Disk usage analytics** — stacked breakdown bar per source, per-app disk usage readout, sort by largest packages, and orphan disk reclaim summary.
- **One-click maintenance** — check updates concurrently (`checkupdates`, `yay -Qua`, `flatpak remote-ls --updates`, `git ls-remote` for Omarchy Plugins, `mise outdated --json`), route Omarchy and Pacman system updates through Omarchy's full `omarchy-update` flow (avoiding partial upgrades), open Omarchy Plugin updates interactively in a floating terminal (`omarchy-plugin-update`) so you can review the `git diff` before applying changes, update Flatpak apps, Mise tools (`mise upgrade`), and up-to-date AUR packages, inspect `Depends On` / `Required By`, launch desktop apps, remove packages safely with dependency warnings, and clean unused orphans.
- **Live Omarchy theme & Hyprland rounding** — watches `~/.local/state/omarchy/current/theme/colors.toml` and `hyprctl decoration:rounding` live.
- **All 50 Omarchy 4.5 languages** — watches `~/.config/omarchy/language` live with CLDR plurals, localized disk sizes/dates/numbers, and RTL layout mirroring.
- **Headless CLI** — scriptable `list`, `updates`, `security`, and `summary` commands with `--json` output.

## Installation

### 1. Install the Omarchy bar plugin

```sh
omarchy plugin add https://github.com/marciobbj/omapkg --enable
```

The bar widget starts working immediately — it checks for waiting updates across Pacman, AUR, and Flatpak right away.

### 2. Build and install the Omapkg desktop app

On first click, if `omapkg` is not installed yet, the bar widget opens a floating terminal asking whether you want to build and install it. You can also build and install it manually at any time:

```sh
cd ~/.config/omarchy/plugins/marciobbj.omapkg
./bin/install
```

For local development without installing the Arch package:

```sh
./bin/build      # Compile with qmake6 + make into build/omapkg
./bin/test       # Build and run the Qt Test suite offscreen
```

## Removal

To cleanly remove both the bar widget and the desktop app:

```sh
# Remove the Omarchy bar plugin
omarchy plugin remove marciobbj.omapkg

# Remove the desktop app Arch package (if installed via ./bin/install)
sudo pacman -R omapkg
```

If you installed the desktop app into `~/.local` manually instead of via `pacman`, remove `~/.local/bin/omapkg`, `~/.local/share/applications/omapkg.desktop`, and `~/.local/share/icons/hicolor/scalable/apps/omapkg.svg`.

## Dependencies

### Required (for the desktop app & build)

- `qt6-base`, `qt6-declarative`, `qt6-svg`, `qt6-wayland`
- `xdg-desktop-portal`
- `expac`, `pacman-contrib` (`checkupdates`)
- `polkit` (`pkexec` for privileged package updates/removals)
- `gcc`, `make` (build-time only)

### Optional

- `yay` — AUR package updates and management
- `flatpak` — Flatpak application updates and management
- `mise` — Mise runtime & dev tool updates and management
- `git` — Git-managed Omarchy Plugin update checks

## Configuration & Permissions

- Omapkg **never modifies or overwrites user configuration files** without explicit consent.
- Update checks (`checkupdates`, `yay -Qua`, `flatpak remote-ls --updates`, `git ls-remote`, `mise outdated --json`) are read-only and run unprivileged.
- Package installation, updates, removals, and orphan cleanup are initiated only by explicit user action and authenticate through standard Polkit (`pkexec`), `omarchy-update`, or `makepkg` prompts.
- Omarchy Plugin updates open interactively in Omarchy's floating terminal (`omarchy-plugin-update`) so you always inspect the `git diff` and confirm before merging third-party shell code.
- **Security advisory network requests (read-only, cached locally under `~/.cache/omapkg/`):**
  - `https://security.archlinux.org/json` (GET with `ETag`, 1h TTL) — matches installed Omarchy/Pacman packages locally; no package list is uploaded.
  - `https://raw.githubusercontent.com/omacom/omarchy-plugin-marketplace/main/registry.json` (GET with `ETag`, 6h TTL) — checks installed Omarchy Plugins locally for retired/revoked status or security baseline findings; no plugin list is uploaded.
  - `https://api.osv.dev/v1/querybatch` and `/v1/vulns/<id>` (1h / 24h cache) — queries known vulnerabilities for installed Mise tools (sends only mapped Mise tool names and installed versions).
  - Deduplication state for `notify-send` alerts is stored locally at `~/.local/state/omapkg/notified-advisories.json`. Security advisories are informational and are not a substitute for security audits or zero-day detection.

## Controls & Shortcuts

### Bar Widget

- **Left-click** — Open or focus Omapkg (or offer to build/install it on first run)
- **IPC refresh** — `omarchy-shell marciobbj.omapkg refresh` (called automatically by Omapkg after update checks and package actions)

### Desktop App

| Shortcut | Action |
| --- | --- |
| `Ctrl+F` or `/` | Focus search |
| `↑` / `↓` or `J` / `K` | Move selection |
| `Enter` / `Space` | Update selected package (or check for updates) |
| `Ctrl+U` | Update all outdated packages |
| `Ctrl+R` / `F5` | Refresh packages and check for updates |
| `Delete` / `Backspace` | Remove selected package |
| `Ctrl+Shift+O` | Clean unused orphan packages |
| `1` – `9` | Switch source tabs (All, Updates, Omarchy, Pacman, AUR, Flatpak, Omarchy Plugins, Mise, Orphans) |
| `S` | Show security alerts |
| `L` | Toggle activity log drawer |
| `?` | Show or hide keyboard shortcuts |
| `Esc` | Close overlay or clear search |
| `Q` / `Ctrl+Q` | Quit |

## Headless CLI

```sh
omapkg summary --json
omapkg updates --json
omapkg security --json
omapkg list --json
```

## License

MIT — see [LICENSE](LICENSE).
