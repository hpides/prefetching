import matplotlib.pyplot as plt
import seaborn as sns
from import_helper import *


def addlabels(x, y):
    for i in range(len(x)):
        if y[i] > 4:
            plt.text(i, y[i] // 2, f"{y[i]}%", fontdict={"fontsize": 23}, ha="center")
        else:
            plt.text(i, y[i] + 1, f"{y[i]}%", fontdict={"fontsize": 23}, ha="center")


plt.rcParams["xtick.labelsize"] = 25  # X tick label font size
plt.rcParams["ytick.labelsize"] = 25  # Y tick label font size
plt.rcParams["axes.labelsize"] = 13  # X and Y label font size
data = [57, 17, 13, 9, 4]
labels = ["Default\n(T0)", "T0", "NTA", "T1", "T2"]

colors = sns.color_palette("pastel")[0:5]
plt.gca().spines["top"].set_visible(False)
plt.gca().spines["right"].set_visible(False)
plt.gca().spines["left"].set_visible(False)
plt.grid(True, axis="y")
# create pie chart

for i, (y, x) in enumerate(zip(data, labels)):
    plt.bar(
        i,
        y,
        color=colors[i],
        label=x,
        linewidth=0.9,
        edgecolor="black",
        zorder=2,
    )
addlabels(range(0, 5), data)
plt.tight_layout(rect=[0, 0.1, 1, 1])
plt.xticks(range(0, 5), labels)
plt.savefig(f"{MT_FINAL_FIGURES_DIR}/prefetching_usage.pdf")
plt.show()
