#!/usr/bin/env bash
LD_PRELOAD=libhugetlbfs.so:i$LD_PRELOAD HUGETLB_MORECORE=8M exec $*
