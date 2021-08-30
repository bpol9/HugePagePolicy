# RemoteHugePages

A new NUMA memory policy, implemented for Linux as a simple userspace library. The goal of the policy is to map all process memory with huge pages.
The library works at allocation time, by binding parts of the memory request to nodes with available huge pages. Huge pages of the local node are of course searched first
, with the key point being that huge pages of remote nodes are prefered over local small pages.
