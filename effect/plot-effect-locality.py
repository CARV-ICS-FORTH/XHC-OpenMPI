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

plt.rcParams['lines.linewidth'] = 4
plt.rcParams['lines.markersize'] = 12

# -----------------------

GCONFIG = 'detailed'
# GCONFIG = 'presentation'
# GCONFIG = 'small'

# -----------------------

gfmt = 'svg'
# gfmt = 'png'

parser = argparse.ArgumentParser(add_help=False)

parser.add_argument('-h', '--host', help='benchmark host', required=True)
parser.add_argument('-d', '--dest', help='destination directory for plots', default='.')
parser.add_argument('-s', help='save figure to file?', action='store_true', dest='save_fig')

args = parser.parse_args()

host, dest_dir, save_fig = args.host, args.dest, args.save_fig

# -----------------------

fig, ax = util.plt_init(GCONFIG)
util.plt_title_osu(GCONFIG, None, 'bcast', host)

cmd_pipe = os.popen('echo file rank size latency; \
	PROC=proc-df-full-lat-all.awk ../utils/cproc-df.sh data/%s' % host)
df = pd.read_csv(cmd_pipe, delim_whitespace=True)

def adjust(row):
	return int(re.findall(r'(?:[a-zA-Z0-9-]+)_xhc_flat_(?:[0-9]+[KM]?)_(\d+)', row['file'])[0])

df['n_ranks'] = df.apply(adjust, axis=1)
df = df.drop('file', axis=1)

max_ranks = df['n_ranks'].max()

# -----------------------

df = df.sort_values(by = ['n_ranks', 'rank', 'size'])
df = df[df['rank'] != 0]

df['locality'] = df.apply(lambda x: ('local'
	if x['rank'] < max_ranks/2 else 'remote'), axis=1)
df = df.drop('rank', axis=1)

df = df.groupby(['n_ranks', 'locality', 'size'], as_index=False).mean()

df['size'] = df['size'].map(util.pow2SI)
df = df[df['size'].isin(['4', '1K', '16K', '64K', '256K', '1M'])]

# Fake plot, to get legend only for locality
# ax = sns.lineplot(data=df, x='n_ranks', y='latency', style='locality', errorbar=None)
# ax.legend().set_title(None)
# for l in ax.get_lines(): l.remove()	
# sns.move_legend(ax, 'upper left', frameon=True)

ax = sns.lineplot(data=df, x='n_ranks', y='latency',
	hue='size', style='locality', palette='tab10')
	# errorbar=None, # removed cause t'was buggy with old versions of matplotlib/seaborn (I hate old software)

xl = df['n_ranks'].unique()[::2]

ax.set(xlabel='Ranks (#)', ylabel='Latency (us)', xticks=xl)
ax.axvline(x=max_ranks/2, color='grey', lw=2, ls=':', zorder=0.5)
ax.set_axisbelow(True)

plt.yscale('log')
# plt.ylim(top=3000)
plt.grid(which='both', axis='both', ls=':')

if save_fig:
	plt.savefig('%s/effect_locality_%s.%s' % (dest_dir, host,
		gfmt), bbox_inches='tight', dpi=150)
else:
	plt.show()
