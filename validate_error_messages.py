#!/usr/bin/env python3

import os
import re

re_msgid_all      = r'(?P<id>(?P<val>[I0-9][0-9]{3,4})[EWID])'
re_msgid_all2     = r'"LTFS(?P<id>(?P<val>[0-9]{4,5})[EWID])'

re_msgid_bundle   = r'(^|[^0-9])(?P<id>(?P<val>[0-9]{4,5})[EWID]):string'

msg_used = set()
msg_unref = set()

def check_line(path, linenum, x):
	m1 = re.search(re_msgid_all, x)
	m2 = re.search(re_msgid_all2, x)
	if m2 and not m1:
		m1 = m2
	if m1:
		mid = m1.group('id')

		if m1.group('val')[0] != 'I':
			mval = int(m1.group('val'))
		else:
			return None

		if re.search(r'/\* ltfsresult', line):
			return None
		if re.search(r'[^EWID],$', mid):
			print('Bad output level in message ID at %s:%d' % (path, linenum))
			print(line.rstrip())
			return None
		if re.search(r'ltfsmsg\(', line):
			if mid[0] == '0':
				print('Leading zero(s) in message ID at %s:%d' % (path, linenum))
				print(line.rstrip())
				return None
			if re.search(r'LTFS_ERR', line):
				if mid[-1] != 'E':
					print('Output level mismatch at %s:%d' % (path, linenum))
					print(line.rstrip())
					return None
			elif re.search(r'LTFS_WARN', line):
				if mid[-1] != 'W':
					print('Output level mismatch at %s:%d' % (path, linenum))
					print(line.rstrip())
					return None
			elif re.search(r'LTFS_INFO', line):
				if mid[-1] != 'I':
					print('Output level mismatch at %s:%d' % (path, linenum))
					print(line.rstrip())
					return None
			elif re.search(r'LTFS_DEBUG', line):
				if mid[-1] != 'D':
					print('Output level mismatch at %s:%d' % (path, linenum))
					print(line.rstrip())
					return None
			else:
				print('Unknown output level at %s:%d' % (path, linenum))
				print(line.rstrip())
				return None
			return mid
		elif re.search(r'ltfsresult\(', line):
			return mid
		elif re.search(r'fprintf\(', line):
			return mid
		elif re.search(r'_slext_trace', line):
			return mid
		elif re.search(r'check_err\(', line):
			return mid
		elif re.search(r' [0-9]{4,5}[EWID],', line):
			return mid
		elif re.search(r'\s+[0-9]{4,5}[EWID],', line):
			return mid
		#return mid
	return None

# List the messages present in the source
for d, dirs, files in os.walk('src'):
	for f in files:
		if re.search(r'\.[ch]$', f) or re.search(r'\.cpp$', f):
			with open(os.path.join(d, f), 'r') as fd:
				linenum = 1
				for line in fd:
					msgid = check_line(os.path.join(d, f), linenum, line)
					if msgid:
						msg_used.add(msgid)
						msg_unref.add(msgid)
					linenum += 1

msg_ids = dict()

# List the messages defined in the message bundles
for d, dirs, files in os.walk('messages'):
	if d == 'messages':
		continue
	if d == "messages/internal_error":
		continue
	msg_list = set()
	for f in files:
		start_id = 0
		end_id = 1000000
		if re.search(r'\.txt$', f):
			with open(os.path.join(d, f), 'r') as fd:
				linenum = 1
				for line in fd:
					m = re.search(r'start_id:int\s*{\s*(?P<val>[0-9]+)\s*}', line)
					if m is not None:
						start_id = int(m.group('val'))
					m = re.search(r'end_id:int\s*{\s*(?P<val>[0-9]+)\s*}', line)
					if m is not None:
						end_id = int(m.group('val'))
						if end_id < start_id:
							print('Warning: strange message ID range (%d-%d) in %s' % (
								start_id, end_id, os.path.join(d, f)))

					m = re.search(r'^(?P<val>.*)//', line)
					if m is not None:
						m = re.search(re_msgid_bundle, m.group('val'))
					else:
						m = re.search(re_msgid_bundle, line)
					if m is not None:
						val = int(m.group('val'))
						if val < start_id or val > end_id:
							print('Message ID %s out of range (%d-%d) at %s:%d' % (
								m.group('id'), start_id, end_id, os.path.join(d, f), linenum))
						else:
							msg_list.add(m.group('id'))
					linenum += 1
	if len(msg_list) > 0:
		msg_ids[os.path.basename(d)] = msg_list

# Find unused and nonexistent message IDs
for module in msg_ids:
	msg_unref = msg_unref - msg_ids[module]
	diff = msg_ids[module] - msg_used
	if len(diff) > 0:
		print("Found %d unused message IDs in message bundle '%s':" % (len(diff), module))
		diff_list = [i for i in diff]
		diff_list.sort()
		for i in diff_list:
			print('\t%s' % (i,))
if len(msg_unref) > 0:
	print("Found %d undefined message IDs in the source:" % (len(msg_unref),))
	unref_list = [i for i in msg_unref]
	unref_list.sort()
	for i in unref_list:
		print('\t%s' % (i,))
