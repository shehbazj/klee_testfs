CFLAGS = -g -c -emit-llvm -Wall -Werror
COMMON_SOURCES := bitmap.c block.c super.c inode.c dir.c file.c tx.c csum.c
SOURCES:= testfs.c mktestfs.c $(COMMON_SOURCES)
COMMON_TARGETS := $(SOURCES:.c=.bc)
INCLUDE:= /home/klee/klee_src/include

TARGETS := bitmap block super inode dir file tx csum testfs mktestfs
CC=clang

all: testfs.bc mktestfs.bc $(COMMON_TARGETS) testfsAll

exec:
	clang -o testfs_all bitmap.bc block.bc super.bc inode.bc dir.bc file.bc tx.bc csum.bc testfs.bc -I$(INCLUDE)

bitmap.bc: bitmap.c
	$(CC) -o $@ $(CFLAGS) $^ -I$(INCLUDE)  
block.bc: block.c
	$(CC) -o $@ $(CFLAGS) $^ -I$(INCLUDE)
super.bc: super.c
	$(CC) -o $@ $(CFLAGS) $^ -I$(INCLUDE)
inode.bc: inode.c
	$(CC) -o $@ $(CFLAGS) $^ -I$(INCLUDE)
dir.bc: dir.c
	$(CC) -o $@ $(CFLAGS) $^ -I$(INCLUDE)
file.bc: file.c
	$(CC) -o $@ $(CFLAGS) $^ -I$(INCLUDE)
tx.bc: tx.c
	$(CC) -o $@ $(CFLAGS) $^ -I$(INCLUDE)
csum.bc: csum.c
	$(CC) -o $@ $(CFLAGS) $^ -I$(INCLUDE)
testfs.bc: testfs.c
	$(CC) -o $@ $(CFLAGS) $^ -I$(INCLUDE)
mktestfs.bc: mktestfs.c
	$(CC) -o $@ $(CFLAGS) $^ -I$(INCLUDE)
testfsAll:
	llvm-link -o testfs_all.bc bitmap.bc block.bc super.bc inode.bc dir.bc file.bc tx.bc csum.bc testfs.bc

clean:
	rm -rf *.bc
