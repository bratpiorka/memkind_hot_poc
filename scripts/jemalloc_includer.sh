#!/bin/bash
#  Copyright (C) 2015 Intel Corporation.
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#  1. Redistributions of source code must retain the above copyright notice(s),
#     this list of conditions and the following disclaimer.
#  2. Redistributions in binary form must reproduce the above copyright
#  notice(s),
#     this list of conditions and the following disclaimer in the documentation
#     and/or other materials provided with the distribution.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
#  EXPRESS
#  OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
#  OF
#  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO
#  EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
#  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
#  OR
#  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
#  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
#  NEGLIGENCE
#  OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
#  ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

############################################################################
#
#Overview:
#
# This script provides semi-automatic solution for including jemalloc repo
# into memkind.
#
# Usage:
#
# ./jemalloc_includer.sh commit0 commit1
#
# Requirements:
#
# - jemalloc directory should be deleted (except cbc mode)
# - jemalloc repo where the commits come from shoud be added to remotes and
# fetched
#
#############################################################################

if [ $# != 2 ] && [ $# != 3 ]; then
    echo "Usage $0: commit0 commit1 <optionals>"
    echo "Optionals: --cbc-only"
    exit 0
fi

first_commit="$1"
last_commit="$2"

commit_list=$(git rev-list $first_commit..$last_commit | tac)

prefix="jemalloc"
commit_prefix="[jemalloc_integration] original commit:"

#integrate repo state from first commit (original jemalloc)
if [ $3 != "--cbc-only" ]; then
	original_message=$(git show --quiet $first_commit)
	integration_message=`echo -e "$commit_prefix\n$original_message"`

	git read-tree --prefix=jemalloc -u $first_commit
	git commit -m "$integration_message"
fi
#apply change by change
old_commit="$first_commit"
for commit in $commit_list; do

	original_message=$(git show --quiet $commit | tail -n +2)
	integration_message=`echo -e "$commit_prefix\n$original_message"`

	git read-tree --prefix=jemalloc -u $old_commit $commit
	git commit -m "$integration_message"

	old_commit="$commit"
done
