#!/usr/bin/env python3
"""
ADR Architecture Compliance Checker
====================================
Scans the repository against rules defined in adr_rules.yaml.
Reports violations with rule ID, file, line, matched pattern, and reason.
Exits with code 0 if all rules pass, non-zero on any violation.
"""

import sys
import os
import argparse
import glob
import yaml


def load_rules(rules_path):
    """Load compliance rules from YAML file."""
    with open(rules_path, 'r', encoding='utf-8') as f:
        return yaml.safe_load(f)['rules']


def expand_globs(patterns, repo_root):
    """Expand glob patterns to a set of absolute file paths."""
    files = set()
    for pattern in patterns:
        full_pattern = os.path.join(repo_root, pattern)
        matched = glob.glob(full_pattern, recursive=True)
        for f in matched:
            if os.path.isfile(f):
                files.add(f)
    return files


def check_rule(rule, repo_root):
    """
    Check a single rule against all matching files.
    Returns a list of violation dicts.
    """
    violations = []
    include_patterns = rule['include']
    forbidden = rule['forbidden_patterns']
    rule_id = rule['id']
    description = rule['description']

    files = expand_globs(include_patterns, repo_root)

    for filepath in sorted(files):
        try:
            with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
                lines = f.readlines()
        except (IOError, OSError) as e:
            violations.append({
                'rule_id': rule_id,
                'file': os.path.relpath(filepath, repo_root),
                'line': 0,
                'pattern': '',
                'reason': f'Cannot read file: {e}'
            })
            continue

        for line_num, line in enumerate(lines, start=1):
            # Skip comment lines (C-style // and #)
            stripped = line.strip()
            if stripped.startswith('//') or stripped.startswith('#'):
                continue

            for pattern in forbidden:
                if pattern in line:
                    violations.append({
                        'rule_id': rule_id,
                        'file': os.path.relpath(filepath, repo_root),
                        'line': line_num,
                        'pattern': pattern,
                        'reason': description
                    })

    return violations


def main():
    parser = argparse.ArgumentParser(description='ADR Architecture Compliance Checker')
    parser.add_argument(
        '--rules', '-r',
        default=os.path.join(os.path.dirname(__file__), 'adr_rules.yaml'),
        help='Path to adr_rules.yaml (default: same directory as this script)'
    )
    parser.add_argument(
        '--repo-root',
        default=os.path.join(os.path.dirname(__file__), '..', '..'),
        help='Repository root directory (default: ../../ from script location)'
    )
    args = parser.parse_args()

    rules_path = os.path.abspath(args.rules)
    repo_root = os.path.abspath(args.repo_root)

    if not os.path.isfile(rules_path):
        print(f'ERROR: Rules file not found: {rules_path}', file=sys.stderr)
        sys.exit(2)

    if not os.path.isdir(repo_root):
        print(f'ERROR: Repository root not found: {repo_root}', file=sys.stderr)
        sys.exit(2)

    rules = load_rules(rules_path)
    all_violations = []

    print(f'ADR Compliance Checker')
    print(f'Rules: {rules_path}')
    print(f'Repo:  {repo_root}')
    print(f'Rules loaded: {len(rules)}')
    print()

    for rule in rules:
        rule_id = rule['id']
        violations = check_rule(rule, repo_root)
        all_violations.extend(violations)

        if violations:
            print(f'FAIL [{rule_id}]: {rule["description"]}')
            for v in violations:
                print(f'  {v["file"]}:{v["line"]}  pattern="{v["pattern"]}"')
            print()
        else:
            print(f'PASS [{rule_id}]: {rule["description"]}')

    print()
    print('-' * 60)

    if all_violations:
        print(f'RESULT: {len(all_violations)} violation(s) found')
        for v in all_violations:
            print(f'  [{v["rule_id"]}] {v["file"]}:{v["line"]}  "{v["pattern"]}"')
        sys.exit(1)
    else:
        print('RESULT: All rules passed')
        sys.exit(0)


if __name__ == '__main__':
    main()
