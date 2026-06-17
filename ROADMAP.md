# ROADMAP — RcloneBrowserNG

> Community continuation of [kapitainsky/RcloneBrowser](https://github.com/kapitainsky/RcloneBrowser) (itself a fork of [mmozeiko/RcloneBrowser](https://github.com/mmozeiko/RcloneBrowser)).  
> Both upstream repos are abandoned (last activity: Dec 2020 / 2018 respectively).  
> This roadmap consolidates **every** open issue, unmerged PR, and community request from the entire upstream/fork ecosystem.
>
> Items blocked on credentials, external submissions, unimplemented dependencies, or hardware → `Roadmap_Blocked.md`.

## Competitive Landscape

For context — active alternatives as of June 2026:

| Project | Stars | Stack | Status | Key Differentiator |
|---------|-------|-------|--------|--------------------|
| [rclone-ui](https://github.com/rclone-ui/rclone-ui) | 2,100 | TypeScript/Tauri | Active (v3.6.0) | Cron scheduling, dual-panel Commander, 7 langs, WinGet/Homebrew/Flathub. **Caution: #218 dry-run may execute real sync** |
| [RClone Manager](https://github.com/Zarestia-Dev/rclone-manager) | 933 | Angular/Tauri | Active (v0.2.7) | Fastest-growing. FS watchers, alert actions (toast/webhook/email/Telegram/script), Docker headless, Crowdin i18n, performance presets |
| [Celeste](https://github.com/hwittenborn/celeste) | 1,616 | Rust/GTK4 | **Archived Nov 2025** | Was Linux-only bisync — lane now open |
| [rclone-webui-react](https://github.com/rclone/rclone-webui-react) | 1,562 | JavaScript | Stale (5yr no release) | Superseded by `rclone gui` (v1.74.0) |
| [rem](https://github.com/liriliri/rem) | 603 | Electron | Active (v1.4.0, Dec 2025) | "Open source RcloneView" — file preview, multi-window, media preview |
| [rclone-webui-angular](https://github.com/yuudi/rclone-webui-angular) | 359 | Angular | Slowing (10mo no release) | Web alternative, GPG-signed releases |
| [H4R1B0/rclone-gui](https://github.com/H4R1B0/rclone-gui) | Low | Swift | Active (v1.6.0, 521 tests) | macOS-native. Cloud trash (10 providers), bookmarks, bulk rename, back/forward nav. Zero visibility |
| [yet-another-rclone-dashboard](https://github.com/outlook84/yet-another-rclone-dashboard) | 125 | TypeScript | Active (v0.4.4) | New entrant. Lightweight web RC GUI, multiple themes, mobile-responsive, PWA |
| [RcloneShuttle](https://github.com/pieterdd/RcloneShuttle) | 150 | Rust/GTK4 | Active (v0.1.9) | Linux-only via Flathub, upload-only, anti-AI policy |
| [rclone-rc-web-gui](https://github.com/retifrav/rclone-rc-web-gui) | 125 | TypeScript | Active (v2026.1.2) | Lightweight RC web GUI, no sync by design |

RcloneBrowserNG's niche: **lightweight native Qt desktop app** — faster startup, lower memory, no Electron/Tauri runtime, no browser tab. The value proposition is a well-maintained, modern C++/Qt6 app that "just works" with current rclone on all desktop platforms. Community signal: strong anti-paywall and anti-AI-vibe-coded sentiment favors a free, well-engineered native GUI.

---

## Source Cross-Reference

All items trace back to public GitHub issues/PRs:

- `kapitainsky #NNN` = [kapitainsky/RcloneBrowser](https://github.com/kapitainsky/RcloneBrowser/issues)
- `mmozeiko #NNN` = [mmozeiko/RcloneBrowser](https://github.com/mmozeiko/RcloneBrowser/issues)
- `docker #NNN` = [romancin/rclonebrowser-docker](https://github.com/romancin/rclonebrowser-docker/issues)
- `PR#NNN` = pull request (repo indicated in context)
- `rclone forum` = [forum.rclone.org](https://forum.rclone.org) discussion threads
