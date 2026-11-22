# Credits

mythos-engine is MIT-licensed (see LICENSE). Third-party code:

- `third_party/stb/stb_truetype.h`: [stb_truetype](https://github.com/nothings/stb) by Sean Barrett,
  public domain (or MIT).
- `third_party/stb/stb_image.h`: [stb_image](https://github.com/nothings/stb) by Sean Barrett, public domain (or MIT).
- [nlohmann/json](https://github.com/nlohmann/json) 3.12.0 by Niels Lohmann, MIT licence: fetched at configure time
  when no installed copy is found.
- [GLM](https://github.com/g-truc/glm) 1.0.3 by G-Truc Creation, MIT licence (the Happy Bunny licence is an
  alternative): fetched at configure time when no installed copy is found.
- [Dear ImGui](https://github.com/ocornut/imgui) 1.92 (docking branch) by Omar Cornut, MIT licence: fetched at configure
  time, only for the level editor (`MYTHOS_BUILD_EDITOR`); the game does not link it.
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

<!-- cc0-models:begin -->

## 3D models — [Poly Haven](https://polyhaven.com) (CC0) and others

Converted to OBJ and BMP by `tools/fetch_cc0_models.py` (textures in `assets/modeltex/`).

| Model | Source model | Author | Licence | Used for |
| --- | --- | --- | --- | --- |
| `barrel.obj` | [Wine Barrel 01](https://polyhaven.com/a/wine_barrel_01) | James Ray Cock | CC0 1.0 | wine barrels |
| `bed.obj` | [Old Bed Frame](https://polyhaven.com/a/old_bed_frame) | Luca B | CC0 1.0 | beds |
| `bookshelf.obj` | [Wooden Bookshelf Worn](https://polyhaven.com/a/wooden_bookshelf_worn) | Ulan Cabanilla | CC0 1.0 | bookshelves |
| `boombox.obj` | [Boombox](https://polyhaven.com/a/boombox) | Thomas Paul Mouilleron | CC0 1.0 | a boombox |
| `bowl.obj` | [Wooden Bowl 01](https://polyhaven.com/a/wooden_bowl_01) | Oliver Harries | CC0 1.0 | wooden bowls |
| `broom.obj` | [Wooden Broom](https://polyhaven.com/a/wooden_broom) | Balen | CC0 1.0 | brooms |
| `bucket.obj` | [Wooden Bucket 01](https://polyhaven.com/a/wooden_bucket_01) | James Ray Cock | CC0 1.0 | buckets |
| `cardbox.obj` | [Cardboard Box 01](https://polyhaven.com/a/cardboard_box_01) | Rahul Chaudhary | CC0 1.0 | cardboard boxes |
| `chair.obj` | [Dining Chair 02](https://polyhaven.com/a/dining_chair_02) | James Ray Cock | CC0 1.0 | chairs |
| `dustpan.obj` | [Dustpan](https://polyhaven.com/a/dustpan) | Balen | CC0 1.0 | a dustpan |
| `frame_fancy.obj` | [Fancy Picture Frame 01](https://polyhaven.com/a/fancy_picture_frame_01) | Rob Tuytel, Rico Cilliers | CC0 1.0 | pictures on the walls |
| `frame_stand.obj` | [Standing Picture Frame 01](https://polyhaven.com/a/standing_picture_frame_01) | James Ray Cock | CC0 1.0 | photographs on the tables |
| `frame_tall.obj` | [Hanging Picture Frame 01](https://polyhaven.com/a/hanging_picture_frame_01) | James Ray Cock | CC0 1.0 | pictures on the walls |
| `frame_wide.obj` | [Hanging Picture Frame 02](https://polyhaven.com/a/hanging_picture_frame_02) | James Ray Cock | CC0 1.0 | pictures on the walls |
| `gamepad.obj` | [Gamepad](https://polyhaven.com/a/gamepad) | Josh Dean | CC0 1.0 | a game controller |
| `jug.obj` | [Jug 01](https://polyhaven.com/a/jug_01) | Kuutti Siitonen | CC0 1.0 | jugs |
| `kettle.obj` | [Vintage Electric Kettle](https://polyhaven.com/a/vintage_electric_kettle) | SV Garip | CC0 1.0 | kettles |
| `laptop.obj` | [Classic Laptop](https://polyhaven.com/a/classic_laptop) | Arrangemonk | CC0 1.0 | a laptop |
| `lighter.obj` | [Vintage Lighter](https://polyhaven.com/a/vintage_lighter) | Slinc | CC0 1.0 | a lighter |
| `nightstand.obj` | [Painted Wooden Nightstand](https://polyhaven.com/a/painted_wooden_nightstand) | Kirill Sannikov | CC0 1.0 | bedside tables |
| `oven.obj` | [Electric Stove](https://polyhaven.com/a/electric_stove) | Kuutti Siitonen | CC0 1.0 | ovens |
| `sofa.obj` | [Arm Chair 01](https://polyhaven.com/a/ArmChair_01) | Kirill Sannikov | CC0 1.0 | armchairs and sofas |
| `table.obj` | [Round Wooden Table 02](https://polyhaven.com/a/round_wooden_table_02) | Ulan Cabanilla | CC0 1.0 | tables |
| `toolbox.obj` | [Metal Toolbox](https://polyhaven.com/a/metal_toolbox) | Mateusz Sadek | CC0 1.0 | toolboxes |
| `torch.obj` | [Small Plastic Torch](https://polyhaven.com/a/small_plastic_torch) | Abiyyu Panggalih (Abiyyu_xyz) | CC0 1.0 | a torch |
| `trashbag.obj` | [Trashbag](https://polyhaven.com/a/trashbag) | Benny Weimer | CC0 1.0 | rubbish |
| `vase.obj` | [Ceramic Vase 01](https://polyhaven.com/a/ceramic_vase_01) | James Ray Cock | CC0 1.0 | vases |
| `wardrobe.obj` | [Drawer Cabinet](https://polyhaven.com/a/drawer_cabinet) | Ulan Cabanilla | CC0 1.0 | wardrobes and chests of drawers |
| `cabinet.obj` | [Painted Wooden Cabinet](https://polyhaven.com/a/painted_wooden_cabinet) | Kirill Sannikov | CC0 1.0 | cupboards |
| `crate.obj` | [Wooden Crate 01](https://polyhaven.com/a/wooden_crate_01) | James Ray Cock | CC0 1.0 | crates |
| `deskamp.obj` | [Desk Lamp Arm 01](https://polyhaven.com/a/desk_lamp_arm_01) | Yann Kervran, Kuutti Siitonen | CC0 1.0 | desk lamps |
| `dresser.obj` | [Drawer Cabinet](https://polyhaven.com/a/drawer_cabinet) | Ulan Cabanilla | CC0 1.0 | chests of drawers |
| `grandfather.obj` | [Vintage Grandfather Clock 01](https://polyhaven.com/a/vintage_grandfather_clock_01) | Yann Kervran, James Ray Cock | CC0 1.0 | tall clocks |
| `ladder.obj` | [Wooden Ladder](https://polyhaven.com/a/wooden_ladder) | Miroslav Turura | CC0 1.0 | ladders |
| `pillows.obj` | [Throw Pillows 01](https://polyhaven.com/a/throw_pillows_01) | Serhii Khromov | CC0 1.0 | cushions and pillows |
| `schooldesk.obj` | [School Desk 01](https://polyhaven.com/a/SchoolDesk_01) | Ethan Place | CC0 1.0 | school desks |
| `stool.obj` | [Wooden Stool 01](https://polyhaven.com/a/wooden_stool_01) | Kuutti Siitonen | CC0 1.0 | stools |
| `toolchest.obj` | [Metal Tool Chest](https://polyhaven.com/a/metal_tool_chest) | Yann Kervran, John Hutcheson | CC0 1.0 | tool chests |
| `winebottles.obj` | [Wine Bottles 01](https://polyhaven.com/a/wine_bottles_01) | Rico Cilliers, Jurita Burger | CC0 1.0 | wine bottles |
| `oillamp.obj` | [Vintage Oil Lamp](https://polyhaven.com/a/vintage_oil_lamp) | Monsta3D | CC0 1.0 | oil lamps on the tables of the old house |
| `handplane.obj` | [Hand Plane No4](https://polyhaven.com/a/hand_plane_no4) | Satyaki Mandal | CC0 1.0 | the mason's plane |
| `hammer.obj` | [Wooden Hammer 01](https://polyhaven.com/a/wooden_hammer_01) | James Ray Cock | CC0 1.0 | the mason's mallet |
| `tape.obj` | [Measuring Tape 01](https://polyhaven.com/a/measuring_tape_01) | Kuutti Siitonen | CC0 1.0 | the mason's measuring tape |
| `duck.obj` | [Rubber Duck Toy](https://polyhaven.com/a/rubber_duck_toy) | Plat251 | CC0 1.0 | the rubber duck in the bathroom |
| `cake.obj` | [Carrot Cake](https://polyhaven.com/a/carrot_cake) | Greg Zaal, James Ray Cock | CC0 1.0 | the name-day cake |
| `lamppost.obj` | [Street Lamp 02](https://polyhaven.com/a/street_lamp_02) | Josh Dean | CC0 1.0 | the lamp posts of the village lane |
| `plant.obj` | [Potted Plant 04](https://polyhaven.com/a/potted_plant_04) | James Ray Cock | CC0 1.0 | the house plants in the corners of the rooms |
| `shoes.obj` | [Rubber Boots](https://polyhaven.com/a/rubber_boots) | L | CC0 1.0 | the boots by the front door |
| `hat.obj` | [Fishermans Hat](https://polyhaven.com/a/fishermans_hat) | PierreB3D | CC0 1.0 | the hat on the coat rack |

<!-- cc0-models:end -->
