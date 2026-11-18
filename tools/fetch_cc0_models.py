#!/usr/bin/env python3
"""Bakes the engine's basic models of houses and offices (furniture, household things, a torch) from Poly Haven.

They ship in the engine's assets (assets/models, assets/modeltex): a game lays its own assets over them, so a game
with a model of the same name replaces it. Run this after editing the list; commit the result. The conversion is
tools/mythos_models.py; see its notes for the options.

    python3 tools/fetch_cc0_models.py --pbr [--only name ...] [--size 512]
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mythos_models as mm  # noqa: E402
from mythos_models import Spec  # noqa: E402

# Life size (scale 1.0) unless a piece is too tall for a 2.6 m ceiling or too big for a room. Small things use small
# textures: they are seen from a metre or two.
MODELS = {
    "table": Spec("round_wooden_table_02", scale=1.04, what="tables"),  # 0.8 m round, top at 0.78 m where the props sit
    "chair": Spec("dining_chair_02", scale=1.0, what="chairs"),
    "bookshelf": Spec("wooden_bookshelf_worn", scale=0.8, what="bookshelves"),  # 1.65 m tall, 1.1 m wide
    "wardrobe": Spec("drawer_cabinet", scale=1.0, what="wardrobes and chests of drawers"),  # 1.9 m tall
    "oven": Spec("electric_stove", scale=1.0, what="ovens"),
    "bed": Spec("old_bed_frame", scale=1.0, yaw=0, what="beds"),
    "sofa": Spec("ArmChair_01", scale=1.0, what="armchairs and sofas"),
    "lighter": Spec("vintage_lighter", scale=0.8, roll=-90, yaw=-90, size=256, what="a lighter"),
    "torch": Spec("small_plastic_torch", scale=1.0, size=256, what="a torch"),
    "bucket": Spec("wooden_bucket_01", scale=1.0, size=256, what="buckets"),
    "jug": Spec("jug_01", scale=1.0, size=256, what="jugs"),
    "broom": Spec("wooden_broom", scale=1.0, size=256, what="brooms"),
    "bowl": Spec("wooden_bowl_01", scale=1.0, size=256, what="wooden bowls"),
    "barrel": Spec("wine_barrel_01", scale=1.0, size=256, what="wine barrels"),
    "toolbox": Spec("metal_toolbox", scale=1.0, size=256, what="toolboxes"),
    "frame_tall": Spec("hanging_picture_frame_01", scale=1.0, size=512, glass=False, art="painting", what="pictures on the walls"),
    "frame_wide": Spec("hanging_picture_frame_02", scale=1.0, size=512, glass=False, art="painting", what="pictures on the walls"),
    "frame_fancy": Spec("fancy_picture_frame_01", scale=1.0, size=512, what="pictures on the walls"),
    "frame_stand": Spec("standing_picture_frame_01", scale=1.0, yaw=-90, size=256, glass=False, art="painting", what="photographs on the tables"),
    "vase": Spec("ceramic_vase_01", scale=1.0, size=256, what="vases"),
    "kettle": Spec("vintage_electric_kettle", scale=1.0, size=256, what="kettles"),
    "boombox": Spec("boombox", scale=0.8, size=256, what="a boombox"),
    "laptop": Spec("classic_laptop", scale=0.6, size=256, what="a laptop"),
    "gamepad": Spec("gamepad", scale=0.45, size=256, what="a game controller"),
    "trashbag": Spec("trashbag", scale=1.0, size=256, what="rubbish"),
    "cardbox": Spec("cardboard_box_01", scale=1.0, size=256, what="cardboard boxes"),
    "pot": Spec("pot_enamel_01", scale=1.0, size=256, what="cooking pots"),
    "cuttingboard": Spec("wooden_cutting_board", scale=1.0, size=256, what="kitchen counters"),
    "nightstand": Spec("painted_wooden_nightstand", scale=1.0, size=256, what="bedside tables"),
    "cassette": Spec("cassette_player", scale=1.0, size=256, what="a cassette player"),
    "dustpan": Spec("dustpan", scale=1.0, size=256, what="a dustpan"),
}

if __name__ == "__main__":
    mm.run(MODELS, os.path.dirname(HERE), "tools/fetch_cc0_models.py")
