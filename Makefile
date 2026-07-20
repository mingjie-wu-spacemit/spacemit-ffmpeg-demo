# Top-level Makefile: builds both decode and encode demos

all:
	$(MAKE) -C decode
	$(MAKE) -C encode

clean:
	$(MAKE) -C decode clean
	$(MAKE) -C encode clean

.PHONY: all clean
