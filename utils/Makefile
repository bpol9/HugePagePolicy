all: break_hugepages test_rhp

break_hugepages: break_hugepages.c
	gcc -o break_hugepages break_hugepages.c

test_rhp: test_rhp.c
	gcc -o test_rhp test_rhp.c

clean:
	@rm -f test_rhp break_hugepages
