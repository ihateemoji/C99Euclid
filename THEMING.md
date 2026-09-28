# Theming C99Euclid

All colours used by the X11 GUI live in `src/euclid.h` as simple
`#define`s.  Change the RGB values, recompile, and you have a new theme.


## Colour constants

| Constant          | Default RGB     | Used for                                      |
|-------------------|-----------------|-----------------------------------------------|
| `EU_BG_*`         | 24, 26, 30      | Main window background                        |
| `EU_SURF_*`       | 38, 41, 48      | Panel / control surface backgrounds           |
| `EU_FG_*`         | 240, 242, 248   | Title text and general foreground             |
| `EU_MUT_*`        | 95, 100, 110    | Muted tracks / inactive step dots             |
| `EU_ACC_*`        | 85, 145, 235    | Accents, borders, hit dots                    |
| `EU_GRID_*`       | 52, 56, 64      | Track row backgrounds                         |
| `EU_CYA_*`        | 75, 195, 225    | Active track labels and rings                 |
| `EU_GRN_*`        | 95, 205, 145    | Currently-firing step highlight               |
| `EU_NOTE_BG_*`    | 48, 52, 60      | Note-name box background                      |

All values are 8-bit (0–255).  They are packed into X11 pixel values by
the helper `eu_col()` in `gui_x11.c`.

## Themes

Themes live in the `themes/` directory as unified diff patches
against the default colour block in `src/euclid.h`.  Apply one before
building:

```bash
patch -p0 < themes/amber-crt.patch
make clean && make
```

To return to the default theme:

```bash
git checkout -- src/euclid.h
```

Other user-contributed themes are always welcome — open a PR that adds a
new `.patch` file under `themes/` (and a screenshot under `imgs/` with an
appropriate change to `THEMING.md`).

### Amber CRT

![screenshot](imgs/C99Euclid_AmberCRT.jpeg?raw=true)

```bash
patch -p0 < themes/amber-crt.patch
```

### Nord

![screenshot](imgs/C99Euclid_Nord.jpeg?raw=true)

```bash
patch -p0 < themes/nord.patch
```

### Vintage Green

![screenshot](imgs/C99Euclid_VintageGreen.jpeg?raw=true)

```bash
patch -p0 < themes/vintage-green.patch
```

### Gruvbox Dark

![screenshot](imgs/C99Euclid_GruvboxDark.jpeg?raw=true)

```bash
patch -p0 < themes/gruvbox-dark.patch
```

### Dracula

![screenshot](imgs/C99Euclid_Dracula.jpeg?raw=true)

```bash
patch -p0 < themes/dracula.patch
```

## How to create a new theme

1. Edit the `#define`s in `src/euclid.h`.
2. Generate a patch:

   ```bash
   git diff src/euclid.h > themes/my-theme.patch
   ```

3. Rebuild to test:

   ```bash
   make clean && make
   ```

4. Replace your old `C99Euclid.clap` binary with the one you just built and
   rescan the plug-ins in your DAW.

5. Add a screenshot under `imgs/` and document the theme here.

6. Open a PR to contribute your theme to this repository.
