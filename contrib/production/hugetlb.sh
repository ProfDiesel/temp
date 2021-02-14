#!/usr/bin/env bash
LD_PRELOAD=libhugetlbfs.so:$LD_PRELOAD HUGETLB_MORECORE=8M exec $*
