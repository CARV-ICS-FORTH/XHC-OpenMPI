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
parser.add_argument('-r', '--remedy', help='remedy to plot', required=True)
parser.add_argument('-s', help='save figure to file?', action='store_true', dest='save_fig')

args = parser.parse_args()

remedy, dest_dir, save_fig = \
	args.remedy, args.dest, args.save_fig

# -----------------------

fig, ax = util.plt_init(GCONFIG)
util.plt_title_osu(GCONFIG, None, 'bcast', None)

dfs = []

for host in hosts:
	cmd_pipe = os.popen('echo file size latency stddev; \
		../utils/cproc-df.sh data/%s' % host)
	df = pd.read_csv(cmd_pipe, delim_whitespace=True)
	dfs.append(df)

df = pd.concat(dfs)

# -----------------------

def adjust(row):
	groups = re.findall(r'([\w-]+)_xhc_([a-zA-Z0-9]+)_([^_]+)_([0-9]+[KM]?)(?:_([\d-]+))?',
		row['file'])[0]
	
	host = groups[0]
	
	remedy = groups[1]
	remedy = re.sub('^opt.*$', 'opt', remedy)
	
	topo = groups[2]
	chunk = groups[3]
	
	mod = (int(groups[4]) if remedy in ['wait', 'opt']  else 0)
	
	return host, remedy, topo, chunk, mod


df['host'], df['remedy'], df['topo'], df['chunk'], df['mod'] = zip(*df.apply(adjust, axis=1))

df = df.drop(['file', 'topo'], axis=1)
df = df.sort_values(by = ['host', 'remedy', 'chunk', 'mod', 'size'])

# ---

df = df[df['remedy'].isin(['vanilla', remedy])]

palette = 'tab10'
yscale = 'linear'
ti = 4

for host in hosts:
	chunk = {
		'wait': {
			'ICX-48': '2K',
			'SKX-24': '8K',
			'CSX-48': '16K',
		}.get(host, '8K'),
		
		'opt': {
			'ICX-48': '16K',
			'SKX-24': '4K',
			'CSX-48': '8K',
		}.get(host, '8K'),
	}.get(remedy, '16K')

	mod = {
		'wait': 2,
		'opt': {
			'ICX-48': -1,
			'SKX-24': 2,
			'CSX-48': 2,
		}.get(host, 2)
	}.get(remedy, 0)
	
	df = df[(df['remedy'] == 'vanilla') | (df['host'] != host) \
		| ((df['chunk'] == chunk) & (df['mod'] == mod))]

# --

df['remedy'] = df.apply(lambda x: {
	'vanilla': 'baseline',
	'wait': 'delay-flag',
	'dual': 'dual buffer',
	'clwb': 'clwb',
	'opt': 'optimized',
}[x['remedy']], axis=1)

df['size'] = df['size'].map(util.pow2SI)

# --

for hhost in df['host'].unique():
	for size in df['size'].unique():
		sel = ((df['host'] == hhost) & (df['size'] == size))
		pivot = (df['remedy'] == 'baseline')
		
		base = df[sel & pivot]['latency'].iloc[0]
		df.loc[sel, 'latency'] = df[sel]['latency'].rdiv(base)

df = df[df['remedy'] != 'baseline']

# --

ax = sns.lineplot(data=df, x='size', y='latency', hue='host',
	style='host', palette=palette, markers=True)

xt, xl = util.slice_ticks(df['size'].unique(), ti)
ax.set(xlabel='Message size', ylabel='Speedup (x)', xticks=xt, xticklabels=xl)

ax.axhline(y=1, color='black', lw=2, ls='-')
plt.ylim(bottom=0.8, top=1.8)

ax.legend().set_title(None)
ax.set_axisbelow(True)

plt.yscale(yscale)
plt.grid(which='both', axis='y', ls=':')

if save_fig:
	plt.savefig('%s/remedy_%s_speedup_all.%s' % (dest_dir, remedy,
		gfmt), bbox_inches='tight', dpi=150)
else:
	plt.show()
