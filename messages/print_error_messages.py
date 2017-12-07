#!/usr/bin/env python2.7

import os
import re

re_msgid_bundle   = r'(^|[^0-9])(?P<id>(?P<val>[0-9]{4,5})[^0-9]):string { (?P<msg>".*")'

msg_ids = dict()

# List the messages defined in the message bundles
for d, dirs, files in os.walk('.'):
	if d == 'messages':
		continue
	if d == "messages/internal_error":
		continue
	msg_list = dict()
	for f in files:
		start_id = 0
		end_id = 1000000
		if re.search(r'\.txt$', f):
			with file(os.path.join(d, f), 'r') as fd:
				linenum = 1
				for line in fd:
					m = re.search(r'start_id:int\s*{\s*(?P<val>[0-9]+)\s*}', line)
					if m is not None:
						start_id = int(m.group('val'))
					m = re.search(r'end_id:int\s*{\s*(?P<val>[0-9]+)\s*}', line)
					if m is not None:
						end_id = int(m.group('val'))
						if end_id < start_id:
							print 'Warning: strange message ID range (%d-%d) in %s' % (
								start_id, end_id, os.path.join(d, f))

					m = re.search(r'^(?P<val>.*)//', line)
					if m is not None:
						m = re.search(re_msgid_bundle, m.group('val'))
					else:
						m = re.search(re_msgid_bundle, line)
					if m is not None:
						val = int(m.group('val'))
						if val < start_id or val > end_id:
							print 'Message ID %s out of range (%d-%d) at %s:%d' % (
								m.group('id'), start_id, end_id, os.path.join(d, f), linenum)
						else:
							msg_list[m.group('id')] = m.group('msg')
					linenum += 1
	if len(msg_list) > 0:
		msg_ids[os.path.basename(d)] = msg_list

# Find unused and nonexistent message IDs
for module in msg_ids:
    for id in msg_ids[module]:
        print "#define LTFS%s %s" % (id, msg_ids[module][id])
