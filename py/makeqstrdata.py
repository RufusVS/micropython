from __future__ import print_function

import argparse
import re
import sys

# codepoint2name is different in Python 2 to Python 3
import platform
if platform.python_version_tuple()[0] == '2':
    from htmlentitydefs import codepoint2name
elif platform.python_version_tuple()[0] == '3':
    from html.entities import codepoint2name
codepoint2name[ord('-')] = 'hyphen';

# add some custom names to map characters that aren't in HTML
codepoint2name[ord('.')] = 'dot'
codepoint2name[ord(':')] = 'colon'
codepoint2name[ord('/')] = 'slash'
codepoint2name[ord('%')] = 'percent'
codepoint2name[ord('#')] = 'hash'
codepoint2name[ord('{')] = 'brace_open'
codepoint2name[ord('}')] = 'brace_close'
codepoint2name[ord('*')] = 'star'

# this must match the equivalent function in qstr.c
def compute_hash(qstr):
    hash = 5381
    for char in qstr:
        hash = (hash * 33) ^ ord(char)
    return hash & 0xffff

# given a list of (name,regex) pairs, find the first one that matches the given line
def re_match_first(regexs, line):
    for name, regex in regexs:
        match = re.match(regex, line)
        if match:
            return name, match
    return None, None

# regexs to recognise lines that the CPP emits
# use a list so that matching order is honoured
cpp_regexs = [
    ('qstr', r'Q\((.+)\)$'),
    ('cdecl', r'(typedef|extern) [A-Za-z0-9_*(,\.) ]+;$')
]

def do_work(infiles):
    # read the qstrs in from the input files
    qstrs = {}
    for infile in infiles:
        with open(infile, 'rt') as f:
            line_number = 0
            for line in f:
                line_number += 1
                line = line.strip()

                # ignore blank lines, comments and preprocessor directives
                if len(line) == 0 or line.startswith('//') or line.startswith('#'):
                    continue

                # work out what kind of line it is
                match_kind, match = re_match_first(cpp_regexs, line)
                if match_kind is None:
                    # unknown line format
                    print('({}:{}) bad qstr format, got {}'.format(infile, line_number, line), file=sys.stderr)
                    return False
                elif match_kind != 'qstr':
                    # not a line with a qstr
                    continue

                # get the qstr value
                qstr = match.group(1)
                ident = re.sub(r'[^A-Za-z0-9_]', lambda s: "_" + codepoint2name[ord(s.group(0))] + "_", qstr)

                # don't add duplicates
                if ident in qstrs:
                    continue

                # add the qstr to the list, with order number to retain original order in file
                qstrs[ident] = (len(qstrs), ident, qstr)

    # process the qstrs, printing out the generated C header file
    print('// This file was automatically generated by makeqstrdata.py')
    print('')
    for order, ident, qstr in sorted(qstrs.values(), key=lambda x: x[0]):
        qhash = compute_hash(qstr)
        qlen = len(qstr)
        print('Q({}, (const byte*)"\\x{:02x}\\x{:02x}\\x{:02x}\\x{:02x}" "{}")'.format(ident, qhash & 0xff, (qhash >> 8) & 0xff, qlen & 0xff, (qlen >> 8) & 0xff, qstr))

    return True

def main():
    arg_parser = argparse.ArgumentParser(description='Process raw qstr file and output qstr data with length, hash and data bytes')
    arg_parser.add_argument('files', nargs='+', help='input file(s)')
    args = arg_parser.parse_args()

    result = do_work(args.files)
    if not result:
        print('exiting with error code', file=sys.stderr)
        exit(1)

if __name__ == "__main__":
    main()
