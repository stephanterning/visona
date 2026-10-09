# App icon

The Visona icon is the website's favicon motif (`site/static/favicon.svg`), redrawn at 1024×1024 (D-112): a dark rounded square, three faint bar lines and a teal waveform.

| File | Use |
| --- | --- |
| `visona-icon.svg` | The master. Apple's macOS grid: an 824×824 body with corner radius 185, 100 px in from each edge, with a soft shadow in the margin. |
| `visona-icon-1024.png` | Rendered from the master and committed, so the build needs no SVG tool. `ICON_BIG` of the app in `app/CMakeLists.txt`, from which JUCE makes the `.icns`. |
| `visona-icon-square.svg` | The same motif full bleed, with no margin, shadow or corners, for platforms that apply their own mask: iPadOS, App Store artwork and an Icon Composer `.icon`. |

Colours: body `#1a1d23` to `#0b0c0f` top to bottom, bar lines `#2c2f37`, waveform `#22b8a0` with a `#2fd4b8` highlight in the middle. The favicon stays a separate drawing, since at 16–32 px it needs its thicker strokes.

## Re-render

After changing `visona-icon.svg`:

```sh
brew install librsvg   # once
scripts/render-icon.sh
```

and commit the PNG with the SVG.

## Later: Mac App Store and iPad

A Mac App Store build, and an iPad app, need the icon in an Xcode asset catalog, or as an Icon Composer `.icon` file for the Liquid Glass icons of macOS and iPadOS 26 and later. JUCE's CMake makes neither. Make them from `visona-icon-square.svg`, with the waveform and the bar lines as separate layers in Icon Composer.
