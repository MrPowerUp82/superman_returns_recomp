# Texture modding guide

Skins and other texture mods for the native renderer (Direct3D 12 and Vulkan). No game file is changed: replacement PNGs are loaded while the game runs. Nothing here ships game art; you work from textures exported from **your** copy.

## 1. Export the game's textures

In the launcher, set **Texture mods** to **Export textures (textures_dump)**, play until the thing you want to change shows up on screen, then quit. Without the launcher, add this to the end of the line in `run.cmd` / `run_keyboard.cmd`:

```
--sr_texture_dump_dir=textures_dump
```

The `textures_dump` folder next to the executable now has, for each texture the game loaded:

| File | What it is |
| --- | --- |
| `<KEY>.png` | The texture as the game stores it, with alpha. |
| `<KEY>.color.png` | Only for textures with alpha: the same colors, fully opaque. **Paint this one.** |
| `<KEY>.alpha.png` | Only for textures with alpha: the alpha as a gray image, for reference. |
| `index.csv` | One line per texture: `key,width,height,format`. |

`<KEY>` is 16 hex digits computed from the texture content. It is the same on every run, PC and backend, so mods are shareable.

## 2. Find which texture is which

Set **Texture mods** to **Identify textures (shows the key)**, or start with:

```
--sr_texture_tag=all
```

Every texture is painted a solid color with its key written on it. Walk up to the part you want and read the key off it. Each line starts with an arrow pointing right; if the arrow points left, that surface is mirrored, so read the digits right to left. To narrow it down, pass key prefixes instead of `all`: `--sr_texture_tag=7D58,D25F`.

Known Superman suit keys (Superman Returns suit):

| Key | Part |
| --- | --- |
| `D25F525A7A0BBE06` | Body (blue) with the chest shield |
| `7D58BD0FC5DA272E` | Cape (outer side), trunks, boots, belt and buckle |
| `81E985D2CD3E08A9` | Head, hair, ears, hands, eye |
| `F85175BDDD7D50AA`, `AB2A145BE8A0C636`, `8C7EB1FB14E93170` | Normal maps (lighting relief, not color) |

More in [the suit texture map](kingdom-come-texture-map-2026-10-28.md).

- **Inner side of the cape** (the side facing his back): its dark red does not come from a texture that can be replaced. Painting `7D58BD0FC5DA272E` changes only the outer side, and tag mode leaves the inner side plain dark red with no key on it (tested 2026-11-11). Changing it needs work in the renderer, not a PNG.
- **Hair:** on `81E985D2CD3E08A9` the alpha (white = hair) makes the hair dark, whatever color is under it. To recolor the hair, paint it in `81E985D2CD3E08A9.color.png` **and** add a `81E985D2CD3E08A9.alpha.png` that is darker over the hair (black = your color shows fully).

## 3. Paint and install

1. Copy the file you want to change from `textures_dump` to a folder named `textures` next to the executable. Subfolders are fine: `textures\My Skin\...`.
2. Edit it. **Keep the same width and height**, otherwise the game ignores it and writes a warning to `logs\game.log`.
3. Start the game normally (**Texture mods: Normal**).

Which file to copy:

- **The texture has a `.color.png` (head, body, cape...): copy and paint `<KEY>.color.png`.** The game keeps its original alpha, so whatever your editor does with transparency cannot break it.
- To change the alpha too, add `<KEY>.alpha.png` (gray: black = 0, white = 255).
- `<KEY>.png` replaces color and alpha together. That's for textures without alpha, or when you really mean to change both.

### Why the head turned black (alpha is a mask, not transparency)

Superman's textures use the alpha channel as a mask: on `81E985D2CD3E08A9` it marks the hair (white) versus skin (black). The skin pixels have alpha 0 but the game still draws their color. Image editors treat alpha 0 as "invisible" and save black there, so the skin and hands came out black. Painting `<KEY>.color.png` avoids this. Since this version the game also repairs an old `<KEY>.png` mod: it puts the original color back where the editor wrote black under alpha 0, and it keeps the original alpha if the PNG was saved without alpha. The log says when it did either.

## 4. Share a skin

Zip your folder (`My Skin\` with the PNGs inside). Users extract it into `textures\`. If two skins have a file for the same key, the first by path name wins and the log names the one that was ignored.

## Options

| Option | Default | What it does |
| --- | --- | --- |
| `--sr_texture_dump_dir=<folder>` | empty (off) | Export textures. Relative paths are relative to the executable. |
| `--sr_texture_replace_dir=<folder>` | `textures` | Where replacements are read from (subfolders included). |
| `--sr_texture_tag=all\|<prefixes>` | empty (off) | Paint textures with their key. Replacements are off for tagged textures. |
| `--sr_texture_replace=false` | `true` | Turn off replacements and built-in skins. |

`logs\game.log` lists each replaced texture once (`texture mods: replaced <KEY> (...)`) and each file that was skipped, with the reason.

Limits: only plain 2D textures are exported or replaced (no cube maps or volumes), and a few formats cannot be exported; the log names them (`textures in format N cannot be dumped`).
