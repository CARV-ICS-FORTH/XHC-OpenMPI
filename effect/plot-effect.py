#!/usr/bin/python

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

cmd_pipe = os.popen('echo file size latency; \
	PROC=proc-df-full.awk ../utils/cproc-df.sh data/%s' % host)
df = pd.read_csv(cmd_pipe, delim_whitespace=True)

def adjust(row):
	return int(re.findall(r'(?:[a-zA-Z0-9-]+)_xhc_flat_(?:[0-9]+[KM]?)_(\d+)', row['file'])[0])

df['n_ranks'] = df.apply(adjust, axis=1)
df = df.drop('file', axis=1)
df = df.sort_values(by = ['n_ranks', 'size'])

sizes = ['4', '1K', '16K', '64K', '256K', '1M']
df = df[df['size'].isin([util.pow2SI_int(x) for x in sizes])]

df['size'] = df['size'].map(util.pow2SI)
max_ranks = df['n_ranks'].max()

ax = sns.lineplot(data=df, x='n_ranks', y='latency',
	hue='size', errorbar='sd', palette='tab10')

xl = df['n_ranks'].unique()[::2]

ax.set(xlabel='Ranks (#)', ylabel='Latency (us)', xticks=xl)
ax.axvline(x=max_ranks/2, color='grey', lw=2, ls=':', zorder=0.5)
ax.set_axisbelow(True)
ax.get_legend().remove()

ax.axvline(x=max_ranks/2, color='grey', lw=2, ls='-', zorder=0.5)
legend = ax.legend(loc='upper left', title='Message size')

plt.yscale('log')
plt.grid(which='both', axis='both', ls=':')

if save_fig:
	plt.savefig('%s/effect_%s.%s' % (dest_dir, host,
		gfmt), bbox_inches='tight', dpi=150)
else:
	plt.show()
