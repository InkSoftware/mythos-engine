# Credits

mythos-engine is MIT-licensed (see LICENSE). Third-party code:

- `third_party/stb/stb_truetype.h`: [stb_truetype](https://github.com/nothings/stb) by Sean Barrett,
  public domain (or MIT).
- `third_party/stb/stb_image.h`: [stb_image](https://github.com/nothings/stb) by Sean Barrett, public domain (or MIT).
- [nlohmann/json](https://github.com/nlohmann/json) (MIT) and [GLM](https://github.com/g-truc/glm) (MIT) are fetched
  at configure time when no installed copy is found.
- SDL3 is fetched at configure time when no installed copy is found: [libsdl.org](https://libsdl.org),
  zlib licence.

## Assets (`assets/`)

Shared by the games built on the engine; a game's own `assets/` is laid over these. Every file here is
CC0 or under the SIL Open Font License. Credit is not required for CC0, and is given with thanks. Each
source below was checked against its page (author, licence and the file used).

### Textures (`assets/*.bmp`)

From [ambientCG](https://ambientcg.com), whose assets are all under
[CC0 1.0](https://docs.ambientcg.com/license/). Each is the colour map multiplied by the ambient-occlusion map,
baked to 256x256, 24-bit BMP.

| File | Material |
| --- | --- |
| `brick.bmp` | [Bricks066](https://ambientcg.com/view?id=Bricks066) |
| `stone.bmp` | [Bricks097](https://ambientcg.com/view?id=Bricks097) |
| `wood.bmp` | [WoodFloor051](https://ambientcg.com/view?id=WoodFloor051) |
| `tile.bmp` | [Tiles107](https://ambientcg.com/view?id=Tiles107) |
| `carpet.bmp` | [Carpet012](https://ambientcg.com/view?id=Carpet012) |
| `ceiling.bmp` | [Plaster001](https://ambientcg.com/view?id=Plaster001) |
| `metal.bmp` | [Metal032](https://ambientcg.com/view?id=Metal032) |
| `grass.bmp` | [Grass004](https://ambientcg.com/view?id=Grass004) |

### Sounds (`assets/sounds/*.wav`)

Mono, 16-bit, 44.1 kHz, trimmed and normalised.

| File(s) | Source | Author | Licence |
| --- | --- | --- | --- |
| `step_wood1-3.wav`, `step_grass1-2.wav`, `step_stone1.wav` | [Different steps on wood, stone, leaves, gravel and mud](https://opengameart.org/content/different-steps-on-wood-stone-leaves-gravel-and-mud) | TinyWorlds | CC0 |
| `door_open.wav`, `door_close.wav` | [Door open, door close](https://opengameart.org/content/door-open-door-close) (cut from its recordings) | Iwan Gabovitch (submitted by qubodup) | CC0 |
| `wind.wav` | [Wind](https://opengameart.org/content/wind) (made loopable) | IgnasD | CC0 |
| `torch.wav`, `battery.wav`, `tick.wav`, `static.wav` | synthesized for the project | the project | CC0 |

### Fonts (`assets/fonts/`)

SIL Open Font License 1.1; the licence texts are beside the fonts. Obtained from
[Google Fonts](https://github.com/google/fonts).

| File | Font | Copyright (as in its licence text) |
| --- | --- | --- |
| `AlegreyaSC-Bold.ttf` | [Alegreya SC](https://github.com/huertatipografica/Alegreya) Bold | 2011 The Alegreya Project Authors (designed by Juan Pablo del Peral, Huerta Tipográfica) |
| `Alegreya.ttf` | [Alegreya](https://github.com/huertatipografica/Alegreya) (variable, regular weight) | 2011 The Alegreya Project Authors (designed by Juan Pablo del Peral, Huerta Tipográfica) |
| `FiraMono-Medium.ttf` | [Fira Mono](https://github.com/mozilla/Fira) Medium | 2012-2013 The Mozilla Corporation and Telefonica S.A. (designed by Carrois Apostrophe) |
