import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec  # Импортируем нужный модуль
import matplotlib.patches as mpatches
import numpy as np

import os
all_data = []

scene = 'conveyor'

rr = 'result_full'

sipp_files = [f for f in os.listdir(f"./{rr}/stats_{scene}/") if f.startswith("sipp_") and f.endswith(".json")]
rrt_files = [f for f in os.listdir(f"./{rr}/stats_{scene}/") if f.startswith("rrt_") and f.endswith(".json")]

print("SIPP-файлы:", sipp_files)
print("RRT-файлы:", rrt_files)

s_total_planning_time = []
s_total_trajectory_length = []
s_total_movement_time = []
s_total_planning_time_init = []

for file in sipp_files:
    results = process_file_s(f"./{rr}/stats_{scene}/" + file)
    s_total_planning_time.append(sum([results[robot]['total_planning_time'] for robot in results.keys()]))
    s_total_planning_time_init.append(sum([results[robot]['total_init_time'] for robot in results.keys()]))
    s_total_trajectory_length.append(sum([results[robot]['total_trajectory_length'] for robot in results.keys()]))
    s_total_movement_time.append(sum([results[robot]['total_movement_time'] for robot in results.keys()]))

r_total_planning_time = []
r_total_trajectory_length = []
r_total_movement_time = []

for file in rrt_files:
    results = process_file_r(f"./{rr}/stats_{scene}/" + file)
    r_total_planning_time.append(sum([results[robot]['total_planning_time'] for robot in results.keys()]))
    r_total_trajectory_length.append(sum([results[robot]['total_trajectory_length'] for robot in results.keys()]))
    r_total_movement_time.append(sum([results[robot]['total_movement_time'] for robot in results.keys()]))

r_total_planning_time = [i for i in r_total_planning_time if i > 0.1]
r_total_trajectory_length = [i for i in r_total_trajectory_length if i > 0.1]
r_total_movement_time = [i for i in r_total_movement_time if i > 0.1]



s_total_planning_time = [s_total_planning_time[i] - s_total_planning_time_init[i] for i in range(len(s_total_planning_time))]

s_total_planning_time = [i for i in s_total_planning_time if i > 0.1]
s_total_trajectory_length = [i for i in s_total_trajectory_length if i > 0.1]
s_total_movement_time = [i for i in s_total_movement_time if i > 0.1]

all_data.append({ 'planning': [s_total_planning_time, r_total_planning_time], 'trajectory': [s_total_trajectory_length, r_total_trajectory_length], 'movement': [s_total_movement_time, r_total_movement_time]})


scene = 'random'
sipp_files = [f for f in os.listdir(f"./{rr}/stats_{scene}/") if f.startswith("sipp_") and f.endswith(".json")]
rrt_files = [f for f in os.listdir(f"./{rr}/stats_{scene}/") if f.startswith("rrt_") and f.endswith(".json")]

print("SIPP-файлы:", sipp_files)
print("RRT-файлы:", rrt_files)

s_total_planning_time = []
s_total_trajectory_length = []
s_total_movement_time = []
s_total_planning_time_init = []

for file in sipp_files:
    print(f"./{rr}/stats_{scene}/" + file)
    results = process_file_s(f"./{rr}/stats_{scene}/" + file)
    s_total_planning_time.append(sum([results[robot]['total_planning_time'] for robot in results.keys()]))
    s_total_planning_time_init.append(sum([results[robot]['total_init_time'] for robot in results.keys()]))
    s_total_trajectory_length.append(sum([results[robot]['total_trajectory_length'] for robot in results.keys()]))
    s_total_movement_time.append(sum([results[robot]['total_movement_time'] for robot in results.keys()]))

r_total_planning_time = []
r_total_trajectory_length = []
r_total_movement_time = []

for file in rrt_files:
    results = process_file_r(f"./{rr}/stats_{scene}/" + file)
    r_total_planning_time.append(sum([results[robot]['total_planning_time'] for robot in results.keys()]))
    r_total_trajectory_length.append(sum([results[robot]['total_trajectory_length'] for robot in results.keys()]))
    r_total_movement_time.append(sum([results[robot]['total_movement_time'] for robot in results.keys()]))

r_total_planning_time = [i for i in r_total_planning_time if i > 0.1]
r_total_trajectory_length = [i for i in r_total_trajectory_length if i > 0.1]
r_total_movement_time = [i for i in r_total_movement_time if i > 0.1]



s_total_planning_time = [s_total_planning_time[i] - s_total_planning_time_init[i] for i in range(len(s_total_planning_time))]

s_total_planning_time = [i for i in s_total_planning_time if 1e6>i > 0.1]
s_total_trajectory_length = [i for i in s_total_trajectory_length if i > 0.1]
s_total_movement_time = [i for i in s_total_movement_time if i > 0.1]

all_data.append({ 'planning': [s_total_planning_time, r_total_planning_time], 'trajectory': [s_total_trajectory_length, r_total_trajectory_length], 'movement': [s_total_movement_time, r_total_movement_time]})


scene = 'shelf'
sipp_files = [f for f in os.listdir(f"./{rr}/stats_{scene}/") if f.startswith("sipp_") and f.endswith(".json")]
rrt_files = [f for f in os.listdir(f"./{rr}/stats_{scene}/") if f.startswith("rrt_") and f.endswith(".json")]

print("SIPP-файлы:", sipp_files)
print("RRT-файлы:", rrt_files)

s_total_planning_time = []
s_total_trajectory_length = []
s_total_movement_time = []
s_total_planning_time_init = []

for file in sipp_files:
    results = process_file_s(f"./{rr}/stats_{scene}/" + file)
    s_total_planning_time.append(sum([results[robot]['total_planning_time'] for robot in results.keys()]))
    s_total_planning_time_init.append(sum([results[robot]['total_init_time'] for robot in results.keys()]))
    s_total_trajectory_length.append(sum([results[robot]['total_trajectory_length'] for robot in results.keys()]))
    s_total_movement_time.append(sum([results[robot]['total_movement_time'] for robot in results.keys()]))

r_total_planning_time = []
r_total_trajectory_length = []
r_total_movement_time = []

for file in rrt_files:
    results = process_file_r(f"./{rr}/stats_{scene}/" + file)
    r_total_planning_time.append(sum([results[robot]['total_planning_time'] for robot in results.keys()]))
    r_total_trajectory_length.append(sum([results[robot]['total_trajectory_length'] for robot in results.keys()]))
    r_total_movement_time.append(sum([results[robot]['total_movement_time'] for robot in results.keys()]))

r_total_planning_time = [i for i in r_total_planning_time if i > 0.1]
r_total_trajectory_length = [i for i in r_total_trajectory_length if i > 0.1]
r_total_movement_time = [i for i in r_total_movement_time if i > 0.1]



s_total_planning_time = [s_total_planning_time[i] - s_total_planning_time_init[i] for i in range(len(s_total_planning_time))]

s_total_planning_time = [i for i in s_total_planning_time if 1e6>i > 0.1]
s_total_trajectory_length = [i for i in s_total_trajectory_length if i > 0.1]
s_total_movement_time = [i for i in s_total_movement_time if i > 0.1]

all_data.append({ 'planning': [s_total_planning_time, r_total_planning_time], 'trajectory': [s_total_trajectory_length, r_total_trajectory_length], 'movement': [s_total_movement_time, r_total_movement_time]})


scene = 'husky'
sipp_files = [f for f in os.listdir(f"./{rr}/stats_{scene}/") if f.startswith("sipp_") and f.endswith(".json")]
rrt_files = [f for f in os.listdir(f"./{rr}/stats_{scene}/") if f.startswith("rrt_") and f.endswith(".json")]

print("SIPP-файлы:", sipp_files)
print("RRT-файлы:", rrt_files)

s_total_planning_time = []
s_total_trajectory_length = []
s_total_movement_time = []
s_total_planning_time_init = []

for file in sipp_files:
    results = process_file_s(f"./{rr}/stats_{scene}/" + file)
    s_total_planning_time.append(sum([results[robot]['total_planning_time'] for robot in results.keys()]))
    s_total_planning_time_init.append(sum([results[robot]['total_init_time'] for robot in results.keys()]))
    s_total_trajectory_length.append(sum([results[robot]['total_trajectory_length'] for robot in results.keys()]))
    s_total_movement_time.append(sum([results[robot]['total_movement_time'] for robot in results.keys()]))

r_total_planning_time = []
r_total_trajectory_length = []
r_total_movement_time = []

for file in rrt_files:
    results = process_file_r(f"./{rr}/stats_{scene}/" + file)
    r_total_planning_time.append(sum([results[robot]['total_planning_time'] for robot in results.keys()]))
    r_total_trajectory_length.append(sum([results[robot]['total_trajectory_length'] for robot in results.keys()]))
    r_total_movement_time.append(sum([results[robot]['total_movement_time'] for robot in results.keys()]))

r_total_planning_time = [i for i in r_total_planning_time if i > 0.1]
r_total_trajectory_length = [i for i in r_total_trajectory_length if i > 0.1]
r_total_movement_time = [i for i in r_total_movement_time if i > 0.1]



s_total_planning_time = [s_total_planning_time[i] - s_total_planning_time_init[i] for i in range(len(s_total_planning_time))]

s_total_planning_time = [i for i in s_total_planning_time if 1e6>i > 0.1]
s_total_trajectory_length = [i for i in s_total_trajectory_length if i > 0.1]
s_total_movement_time = [i for i in s_total_movement_time if i > 0.1]

all_data.append({ 'planning': [s_total_planning_time, r_total_planning_time], 'trajectory': [s_total_trajectory_length, r_total_trajectory_length], 'movement': [s_total_movement_time, r_total_movement_time]})

plt.rcParams.update({
    'font.size': 16,
    'axes.titlesize': 20,
    'axes.labelsize': 18,
    'xtick.labelsize': 16,
    'ytick.labelsize': 16,
    'legend.fontsize': 16,
    'lines.linewidth': 2,
    'boxplot.boxprops.linewidth': 2,
    'boxplot.whiskerprops.linewidth': 2,
    'boxplot.capprops.linewidth': 2,
    'boxplot.medianprops.linewidth': 2,
    'boxplot.flierprops.markersize': 6,
})

labels = ['SI-RRT', 'ST-RRT']
colors = ['#66B2FF', "#D97D0C"]
titles = ['Conveyor', 'Random', 'Shelf', 'Husky']
metric_names = ['Planning Time', 'Trajectory Length', 'Movement Time']

fig = plt.figure(figsize=(32, 14))
gs_main = gridspec.GridSpec(2, 2, figure=fig, hspace=0.2, wspace=0.15)

def set_box_color(bp, color):
    plt.setp(bp['boxes'], color='black')
    plt.setp(bp['whiskers'], color='black')
    plt.setp(bp['caps'], color='black')
    plt.setp(bp['medians'], color='darkred')
    plt.setp(bp['fliers'], markeredgecolor='black')
    for patch, color in zip(bp['boxes'], color):
        patch.set_facecolor(color)

# Создаем все подграфики
for i, title in enumerate(titles):
    ax_main = fig.add_subplot(gs_main[i])
    ax_main.set_title(title, pad=20, fontsize=20)
    ax_main.axis('off')
    
    gs_sub = gridspec.GridSpecFromSubplotSpec(1, 3, subplot_spec=gs_main[i], wspace=0.3)
    
    for j, metric in enumerate(['planning', 'trajectory', 'movement']):
        ax = fig.add_subplot(gs_sub[j])
        data = all_data[i][metric]
        
        bp = ax.boxplot(data, positions=np.arange(len(data)), widths=0.6, patch_artist=True)
        set_box_color(bp, colors)
        
        ax.set_xticks([])
        ax.grid(True, linestyle='--', alpha=0.5)
        
        if j == 0:
            ax.set_ylabel('milliseconds', labelpad=10)
            ax.set_yscale('log')
        elif j == 1:
            ax.set_ylabel('radians', labelpad=10)
        else:
            ax.set_ylabel('frames', labelpad=10)
        
        ax.text(0.5, -0.06, metric_names[j], transform=ax.transAxes, 
                ha='center', va='center', fontsize=16)

# Создаем легенду
handles = [mpatches.Patch(color=colors[i], label=labels[i]) for i in range(len(labels))]
legend = fig.legend(handles=handles, loc='lower center', ncol=len(labels), 
                   bbox_to_anchor=(0.5, 0.02), frameon=True, 
                   borderpad=1, handlelength=1.5, handleheight=1.5)

frame = legend.get_frame()
frame.set_edgecolor('black')
frame.set_linewidth(0.5)

plt.tight_layout()
plt.subplots_adjust(bottom=0.12)
plt.show()