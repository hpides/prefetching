import matplotlib.pyplot as plt
import matplotlib.font_manager as fm
import seaborn as sns
import colorsys

plt.rcParams["font.family"] = ["Latin Modern Roman"]

plt.rcParams["axes.titlesize"] = 20  # Title font size
plt.rcParams["axes.labelsize"] = 16  # X and Y label font size
plt.rcParams["xtick.labelsize"] = 14  # X tick label font size
plt.rcParams["ytick.labelsize"] = 14  # Y tick label font size

GROUPS = [["AMD1", "AMD2", "INTEL1", "INTEL2", "INTEL3"], ["ARM1", "ARM2"]]
GROUPS_FLATTENED = [
    (item, group_id) for group_id, group in enumerate(GROUPS) for item in group
]
UNIQUE_IDS = [item for group in GROUPS for item in group]
GROUPS_MEM_TYPE = ["DDR", "HBM / LPDDR"]
GROUPS_SPACING_SEPARATOR = 0.07

COLORS = list(sns.color_palette())

# === START ===
# taken from https://github.com/CMU-SAFARI/BreakHammer/tree/master
base_colors = sns.color_palette("pastel", 15)


def darken_color(color, amount=0.25):
    normalized_color = [x / 255.0 if x > 1 else x for x in color]
    c = colorsys.rgb_to_hls(*normalized_color)
    new_lightness = max(0, min(1, amount * c[1]))
    darkened_color = colorsys.hls_to_rgb(c[0], new_lightness, c[2])
    darkened_color = [max(0, min(1, x)) for x in darkened_color]
    return darkened_color


dark_colors = [darken_color(color, 0.65) for color in base_colors]

# === END ===

NODEID_TO_LABEL = {
    "INTEL1": "Xeon-E5",
    "INTEL2": "Xeon-2",
    "INTEL3": "Xeon-3",
    "AMD1": "EPYC-2",
    "AMD2": "EPYC-3",
    "ARM1": "A64FX",
    "ARM2": "Grace",
}

NODE_TO_ARCH = {
    "gx21": "Broadwell",
    "cx28": "Zen 2",
    "cx04": "Cascade Lake",
    "cx08": "Cascade Lake",
    "nx06": "Ice Lake",
    "ca06": "ARMv8",
}

NODE_TO_CPU = {
    "gx21": "Intel Xeon E5-2689",
    "cx28": "AMD EPYC 7742",
    "cx04": "Intel Xeon Gold 5220S",
    "cx08": "Intel Xeon Gold 5220S",
    "nx06": "Intel Xeon Platinum 8352Y",
    "ca06": "Fujitsu A64FX",
}

NODE_TO_MT_ID = {
    "gx21": "IntelXX",
    "cx28": "AMD1",
    "cx04": "Intel1",
    "cx08": "Intel1",
    "nx06": "Intel2",
    "ca06": "ARM1",
}


NODEID_TO_ARCH = {
    "INTEL1": "Broadwell",
    "AMD1": "Zen 2",
    "AMD2": "Zen 3",
    "INTEL2": "Cascade Lake",
    "INTEL3": "Ice Lake",
    "ARM1": "ARMv8",
    "ARM2": "ARMv9",
}

NODEID_TO_CPU = {
    "INTEL1": "Intel Xeon E5-2689",
    "AMD1": "AMD EPYC 7742",
    "AMD2": "AMD EPYC 7413",
    "INTEL2": "Intel Xeon Gold 5220S",
    "INTEL3": "Intel Xeon Platinum 8352Y",
    "ARM1": "Fujitsu A64FX",
    "ARM2": "NVIDIA Grace",
}

NODEID_TO_RELIABILITY = {
    "AMD1": "Weak",
    "AMD2": "Weak",
    "ARM1": "Strong (depends)",
    "ARM2": "Weak",
    "INTEL1": "Strong",
    "INTEL2": "Strong",
    "INTEL3": "Strong",
}
