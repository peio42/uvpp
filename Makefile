.PHONY: run test clean

run: test
	test/main

test:
	make -C test

clean:
	make -C test clean
