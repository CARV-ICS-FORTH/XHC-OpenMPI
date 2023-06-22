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
parser.add_argument('-r', '--remedy', help='remedy to plot', required=True)
parser.add_argument('-s', help='save figure to file?', action='store_true', dest='save_fig')

args = parser.parse_args()

host, remedy, dest_dir, save_fig = \
	args.host, args.remedy, args.dest, args.save_fig

# -----------------------

fig, ax = util.plt_init(GCONFIG)
util.plt_title_osu(GCONFIG, None, 'bcast', host)

cmd_pipe = os.popen('echo file size latency stddev; \
	../utils/cproc-df.sh data/%s' % host)
df = pd.read_csv(cmd_pipe, delim_whitespace=True)

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

df = df.drop(['file', 'host', 'topo'], axis=1)
df = df.sort_values(by = ['remedy', 'chunk', 'mod', 'size'])

# ---

df = df[df['remedy'].isin(['vanilla', remedy])]

palette = ['tab:blue', {
	'wait': 'tab:orange',
	'opt': 'tab:green',
	'dual': 'tab:brown',
	'clwb': 'tab:red',
}[remedy]]

# Configure which chunk size to plot
chunk = {
	# If plotting the 'wait' remedy
	'wait': {
		# Different chunk sizes depending on hostname
		'ICX-48': '2K',
		'SKX-24': '8K',
		'CSX-48': '16K',
	}.get(host, '8K'),
	# .get() does dictionary access by key (host), but if the
	# key is not present, the default value (8K) is used
	
	# For the 'opt' remedy (optimized version)
	'opt': {
		'ICX-48': '16K',
		'SKX-24': '4K',
		'CSX-48': '8K',
	}.get(host, '8K'),
	
	# For all other remedies, use the 16K chunk size. The chunk does not
	# actually matter for the them; 16K is the default XHC chunk size.
}.get(remedy, '16K')

# Configure the desired 'wait mode'
mod = {
	# In the 'wait' remedy, 0 is the scrict mode, 2 is
	# the more efficient one. This setting plots both.
	'wait': [0, 2],
	
	# In 'opt', mode -1 is with the remedy for the large messages
	# completely disabled, mode 2 is the efficient one (like above)
	'opt': [{
		'ICX-48': -1,
		'SKX-24': 2,
		'CSX-48': 2,
	}.get(host, 2)]
	# If not specified here, mode 2 is used
	
	# For other remedies, the wait mode is not applicable
}.get(remedy, [0])

yscale = {
	'wait': 'linear',
	'opt': 'log',
	'dual': 'linear',
	'clwb': 'linear',
}[remedy]

ti = {
	'wait': 2,
	'opt': 4,
	'dual': 4,
	'clwb': 3,
}[remedy]

df = df[(df['remedy'] == 'vanilla') | ((df['chunk'] == chunk) & (df['mod'].isin(mod)))]

# --

if remedy == 'wait':
	df = df[df['size'] >= 16384]
elif remedy == 'dual':
	df = df[df['size'] <= 2048]
elif remedy == 'clwb':
	df = df[df['size'] <= 512]

# --

df['remedy'] = df.apply(lambda x: ('wait_strong' if (x['remedy'] == 'wait' and x['mod'] == 0) else x['remedy']), axis=1)

df['remedy'] = df.apply(lambda x: {
	'vanilla': 'baseline',
	'wait': 'delay-flag',
	'wait_strong': 'delay-flag-strict',
	'dual': 'dual buffer',
	'clwb': 'clwb',
	'opt': 'optimized',
}[x['remedy']], axis=1)

df['size'] = df['size'].map(util.pow2SI)

# --

if yscale == 'log':
	plt.rcParams['lines.markersize'] = 6

remedies_in_df = df[df['remedy'] != 'baseline']['remedy'].unique()
order = ['baseline', remedies_in_df[0]]

if remedy == 'wait':
	order.append(remedies_in_df[1])
	palette.insert(1, 'tab:purple')

ax = sns.lineplot(data=df, x='size', y='latency', hue='remedy',
	style='remedy', palette=palette, markers=True, hue_order=order,
	style_order=order)

xt, xl = util.slice_ticks(df['size'].unique(), ti)
ax.set(xlabel='Message size', ylabel='Latency (us)', xticks=xt, xticklabels=xl)

ax.legend().set_title(None)
ax.set_axisbelow(True)

plt.yscale(yscale)
plt.grid(which='both', axis='both', ls=':')

if save_fig:
	plt.savefig('%s/remedy_%s_%s.%s' % (dest_dir, remedy,
		host, gfmt), bbox_inches='tight', dpi=150)
else:
	plt.show()
