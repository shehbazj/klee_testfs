CFLAGS = -g -c -emit-llvm -Wall -Werror
COMMON_SOURCES := bitmap.c block.c super.c inode.c dir.c file.c tx.c csum.c
SOURCES:= testfs.c mktestfs.c $(COMMON_SOURCES)
COMMON_TARGETS := $(SOURCES:.c=.bc)
INCLUDE:= /home/klee/klee_src/include

TARGETS := bitmap block super inode dir file tx csum testfs mktestfs
CC=clang

all: testfs.bc mktestfs.bc $(COMMON_TARGETS) testfsAll

bitmap.bc: bitmap.c
	$(CC) -o $@ $(CFLAGS) $^ 
block.bc: block.c
	$(CC) -o $@ $(CFLAGS) $^
super.bc: super.c
	$(CC) -o $@ $(CFLAGS) $^
inode.bc: inode.c
	$(CC) -o $@ $(CFLAGS) $^
dir.bc: dir.c
	$(CC) -o $@ $(CFLAGS) $^
file.bc: file.c
	$(CC) -o $@ $(CFLAGS) $^
tx.bc: tx.c
	$(CC) -o $@ $(CFLAGS) $^
csum.bc: csum.c
	$(CC) -o $@ $(CFLAGS) $^
testfs.bc: testfs.c
	$(CC) -o $@ $(CFLAGS) $^ -I$(INCLUDE)
mktestfs.bc: mktestfs.c
	$(CC) -o $@ $(CFLAGS) $^
testfsAll:
	llvm-link -o testfs_all.bc bitmap.bc block.bc super.bc inode.bc dir.bc file.bc tx.bc csum.bc testfs.bc
	clang -o testfs_all bitmap.bc block.bc super.bc inode.bc dir.bc file.bc tx.bc csum.bc testfs.bc

clean:
	rm -rf *.bc
