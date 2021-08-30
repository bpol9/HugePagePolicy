# RemoteHugePages (RHP)

A new NUMA memory policy, implemented for Linux as a simple userspace library. The goal of the policy is to map all process memory with huge pages.
The library works at allocation time, by binding parts of the memory request to nodes with available huge pages. Huge pages of the local node are of course used first
, with the key point being that huge pages of remote nodes are prefered over local small pages to map the request.


There are two ways to apply the policy to the execution of a program:

1. Replace calls in malloc in the source code with malloc_with_huge_pages of rhp.c and recompile linking with rhp.c
2. Compile rhp.c as a dynamically linked shared object library and set the LD_PRELOAD environment variable before running the program (more information  [here](http://www.cs.cmu.edu/afs/cs/academic/class/15213-s03/src/interposition/mymalloc.c)). No changes in source code or recompiling are needed.
```
export LD_PRELOAD="/usr/lib/x86_64-linux-gnu/libdl.so /path/to/rhp.so"
./program
```

# RHP Profiler

RHP profiler monitors processes that run with RHP and estimates whether their remote memory should be moved to the local node using small pages and eliminating remote accesses.
In order to estimate that, it needs some offline stats per application that are stored in the OFFLINE_PROFILER_FILE (defined in profiler/header.h). More precisely, each line in this file is consisted of 3 columns, (1) the executable name of the application, (2) the memory overhead when all memory is mapped locally and with huge pages and (3) the total memory footprint of the application. Currently, these statistics have to manually be computed and added to the file per application. Applications that have not been offline profiled and their statistics are not included in the file are ignored by the profiler.
