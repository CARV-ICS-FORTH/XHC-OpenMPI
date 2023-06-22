#!/usr/bin/python3

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

from decimal import Decimal
import os, sys, tempfile

# -----------------------

def cmd_df(cmd):
	tmp = tempfile.NamedTemporaryFile('w', delete=False)
	
	tmp.write('#!/bin/bash\n')
	tmp.write(cmd)
	tmp.close()
	
	os.chmod(tmp.name, 0o755)
	
	csv = os.popen(tmp.name)
	df = pd.read_csv(csv, delim_whitespace=True)
	os.remove(tmp.name)
	
	return df

# -----------------------

opname = {
	'bcast': 'Broadcast',
	'barrier': 'Barrier',
	'allreduce': 'Allreduce',
	'reduce': 'Reduce',
	'latency': 'Latency',
}

opbench = {
	'bcast': 'osu_bcast_dv',
	'barrier': 'osu_barrier_xb',
	'allreduce': 'osu_allreduce_dv',
	'reduce': 'osu_reduce_dv',
	'latency': 'osu_latency_dv',
}

def plt_init(config, figsize=None):
	if config == 'detailed':
		plt.rcParams.update({'font.size': 11.5})
		fig, ax = plt.subplots(figsize=((14, 10) if not figsize else figsize))
	if config == 'presentation':
		plt.rcParams.update({'font.size': 20})
		fig, ax = plt.subplots(figsize=((12, 9) if not figsize else figsize))
	if config == 'small':
		plt.rcParams.update({'font.size': 30})
		fig, ax = plt.subplots(figsize=((10, 8) if not figsize else figsize))
	
	return fig, ax

def plt_title(config, name, benchmark, host):
	if config == 'detailed':
		title = name + '%s' % (' (%s)' % benchmark if benchmark else '')
		
		if host:
			title += '\nNode: %s' % host
		
		plt.title(title)

def plt_title_osu(config, desc, op, host, info=None):
	if  config == 'detailed':
		title = '%s%s %s%s' % (
			('%s ' % desc if desc else ''),
			('%s%s' % (('MPI ' if config == 'presentation' else ''), opname[op])
				if op else ''),
			('(%s) ' % info if info else ''),
			('(%s)' % opbench[op] if op else '')
		)
		
		if host:
			title += '\nNode: %s' % host
		
		plt.title(title)

def plt_legend(config, title='', handles=None, loc=None):
	if config == 'small':
		l = plt.legend(labelspacing=0.1, prop={"size": 26},
			title=title, handles=handles, loc=loc)
	else:
		l = plt.legend(title=title, handles=handles, loc=loc)
	
	plt.gca().add_artist(l)

def plt_grid(config):
	if config == 'detailed':
		plt.grid()

# -----------------------

def slice_ticks(ticks, interval):
	if not isinstance(ticks, list):
		ticks = list(ticks)
	
	new_ticks = list(range(len(ticks)))[::interval]
	labels = ticks[::interval]
	
	if not ticks[0] in labels:
		labels = [ticks[0]] + labels
		new_ticks = [0] + new_ticks
	if not ticks[-1] in labels:
		labels = labels + [ticks[-1]]
		new_ticks = new_ticks + [len(ticks)-1]
	
	return new_ticks, labels

# -----------------------

def pow2SI(val):
	if not isinstance(val, int):
		val = int(val)
	
	if val >= 1024*1024*1024*1024:
		msval = '%sT' % (val/1024/1024/1024/1024)
	elif val >= 1024*1024*1024:
		msval = '%sG' % (val/1024/1024/1024)
	elif val >= 1024*1024:
		msval = '%sM' % (val/1024/1024)
	elif val >= 1024:
		msval = '%sK' % (val/1024)
	else:
		msval = '%s' % val
		return msval # below processing not necessary
	
	mod = msval[-1]
	return str(Decimal(msval[:-1]).normalize()) + mod

def pow2SI_int(strval):
	bits = {'T': 40, 'G': 30, 'M': 20, 'K': 10}
	
	strval = str(strval)
	
	if strval[-1] in bits:
		strnum = strval[:-1]
		bit_mod = bits[strval[-1]]
	else:
		strnum = strval
		bit_mod = 0
	
	return int(strnum) << bit_mod

# -----------------------
