all: profile

profile: profile.c lib_perf.h lib_numa.h header.h
	gcc -o profile profile.c -lnuma

clean:
	@rm -f profile