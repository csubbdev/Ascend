# colors

Color and style text in the terminal.

```
apkg install colors
```

```
colors := import("colors")

print(colors.green("passed"), colors.red("failed"))
print(colors.style("Warning:", "bold yellow on_black"), "disk almost full")
print(colors.hex("orange", "#ff8800"), colors.rgb("teal", 0, 128, 128))
```

| Function | What it does |
|----------|--------------|
| `red(text)`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`, `gray`, `black` | Colors the text. |
| `bold(text)`, `dim`, `italic`, `underline`, `inverse`, `strike` | Styles the text. |
| `style(text, styles)` | Applies several styles named in one string, like `"bold red on_white"`. `on_<color>` sets the background, and `bright_<color>` picks the bright version. |
| `rgb(text, r, g, b)`, `on_rgb(text, r, g, b)` | Any foreground / background color (each part 0-255). |
| `hex(text, color)` | A color written as `"#ff8800"` or `"f80"`. |
| `strip(text)` | Removes color codes, e.g. to measure how wide colored text is. |
| `names()` | The style names `style()` accepts. |
| `enable(on)`, `enabled()` | Turns colors on or off. |

Colors are off when the `NO_COLOR` environment variable is set
([no-color.org](https://no-color.org)).
