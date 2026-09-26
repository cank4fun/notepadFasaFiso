# notepadFasaFiso

i made ts because opening a text file should not feel like launching a whole browser 😭

notepadFasaFiso is a native C++23 text editor + file viewer for Windows and Linux.  
the general idea is basically **"VLC but for text/files"** — handle a lot of stuff, stay fast, dont turn into an IDE, dont eat your pc alive

normal files open like a normal editor. huge / long / cursed files can use View Mode so the app doesnt just inhale the entire file into RAM and pray

## ok but what does it do

- normal text editing
- large-file viewing without loading the whole thing into memory
- tabs + split views
- encoding / BOM / newline preservation
- session restore + crash recovery
- file search/sidebar stuff, opt-in because i also dont want random background scanning lol
- web + email links
- portable Windows build
- Linux AppImage
- a bunch of smaller text/file tools that would make this README 9 years long if i listed all of them

the goal was never "replace vscode" or become some 800 button productivity cockpit  
its supposed to open files, deal with weird ones, and get out of your way

## things it does *not* do

- no Electron
- no telemetry
- no analytics
- no cloud sync
- no update checker phoning home
- no automatic diagnostics upload
- no random file-association registry writes

your files are your files gang

if you want the less yappy version of the privacy stuff, read [PRIVACY.md](PRIVACY.md)

## downloads

### Windows

grab the Windows x64 portable zip from Releases, extract the whole folder and run:

`notepadFasaFiso.exe`

no installer needed

"portable" here means installer-free btw, settings/session/recovery still use the normal per-user Windows locations. im not doing fake USB-portable magic and then hiding config files next to your exe lmao

### Linux

grab:

`notepadFasaFiso-x86_64.AppImage`

the AppImage is released together with:

- `notepadFasaFiso-appimage-corresponding-source.tar.gz`
- `notepadFasaFiso-appimage-runtime-source.tar.gz`

yes those source tarballs are intentional, licenses exist and we're not doing the classic "surely nobody checks this" strat

## building it yourself for some reason

respect tbh

check [BUILDING.md](BUILDING.md)

release/dependency nerd stuff is in:

- [RELEASING.md](RELEASING.md)
- [DEPENDENCIES.md](DEPENDENCIES.md)
- [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)

## project state

this is the long-lived release version

feature development is frozen. unless something breaks, turns out unsafe, or needs maintenance, im not adding 47 random features every month until the app becomes a spaceship dashboard

hotfixes / maintenance are fine  
feature creep can stay outside

## license

notepadFasaFiso itself is MIT licensed, see [LICENSE](LICENSE)

third-party components keep their own licenses/notices under `third_party/licenses/` and in [THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt)

the Linux release also carries the exact Debian package/source inventories and the source material needed for the stuff bundled into the AppImage

basically i made ts for actually using it, not for making the github page look corporate 👍
