#!/usr/bin/python3

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import seaborn as sns
import sys, os, argparse
import math, re

sys.path.append('../utils')
import graph_utils as util

pd.set_option('display.max_rows', None)
pd.set_option('display.max_columns', None)
pd.set_option('display.width', 0)

plt.rcParams['lines.linewidth'] = 5
plt.rcParams['lines.markersize'] = 10

# -----------------------

GCONFIG = 'detailed'
# GCONFIG = 'presentation'
# GCONFIG = 'small'

# -----------------------

hosts = os.listdir('data')

gfmt = 'svg'
# gfmt = 'png'

parser = argparse.ArgumentParser(add_help=False)

parser.add_argument('-d', '--dest', help='destination directory for plots', default='.')
parser.add_argument('-s', help='save figure to file?', action='store_true', dest='save_fig')

args = parser.parse_args()

dest_dir, save_fig = args.dest, args.save_fig

# -----------------------

fig, ax = util.plt_init(GCONFIG)
util.plt_title_osu(GCONFIG, None, 'bcast', None)

dfs = []

for host in hosts:
	cmd_pipe = os.popen('echo file rank size latency; \
		PROC=proc-df-full-lat-all.awk ../utils/cproc-df.sh data/%s' % host)
	df = pd.read_csv(cmd_pipe, delim_whitespace=True)
	dfs.append(df)

df = pd.concat(dfs)

# -----------------------

def adjust(row):
	groups = re.findall(r'([a-zA-Z0-9-]+)_xhc_flat_(?:[0-9]+[KM]?)_(\d+)', row['file'])[0]
	return groups[0], int(groups[1])

df['host'], df['n_ranks'] = zip(*df.apply(adjust, axis=1))
df = df.drop('file', axis=1)

max_ranks = df['n_ranks'].max()

# -----------------------

df = df.sort_values(by = ['host', 'n_ranks', 'rank', 'size'])
df = df[df['rank'] != 0]

# -----------------------

for host in df['host'].unique():
	max_ranks = df[df['host'] == host]['n_ranks'].max()
	df = df[(df['host'] != host) | \
		(df['n_ranks'].isin([max_ranks/2, max_ranks]) \
		& (df['rank'] < max_ranks/2))]

df = df.drop('rank', axis=1)

df = df.groupby(['host', 'n_ranks', 'size'], as_index=False).mean()

for host in df['host'].unique():
	for size in df['size'].unique():
		n_locals = df[df['host'] == host]['n_ranks'].min()
		
		sel = ((df['host'] == host) & (df['size'] == size))
		pivot = ((df['n_ranks'] == n_locals))
		
		base = df[sel & pivot]['latency'].iloc[0]
		df.loc[sel, 'latency'] = df[sel]['latency'].div(base)

for host in df['host'].unique():
	max_ranks = df[df['host'] == host]['n_ranks'].max()
	df = df[(df['host'] != host) | (df['n_ranks'] == max_ranks)]

df['size'] = df['size'].map(util.pow2SI)

ax = sns.lineplot(data=df, x='size', y='latency',
	hue='host', style='host', markers=True)

xt, xl = util.slice_ticks(df['size'].unique(), 4)
ax.set(xlabel='Message size', ylabel='Slowdown (x)', xticks=xt, xticklabels=xl)

ax.legend().set_title(None)
sns.move_legend(ax, 'upper right', frameon=True)
ax.axhline(y=1, color='black', lw=2, ls='-')
# plt.ylim(bottom=0.8, top=5)

plt.grid(which='both', axis='both', ls=':')

if save_fig:
	plt.savefig('%s/effect_locality_slowdown_all.%s' % (dest_dir, gfmt),
		bbox_inches='tight', dpi=150)
else:
	plt.show()
