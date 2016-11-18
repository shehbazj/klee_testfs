#include "testfs.h"
#include "block.h"
#include <assert.h>

//#define KLEE
#ifdef KLEE
#define NUM_SYMBOLS 1 
#include <klee/klee.h>
#endif

static char zero[BLOCK_SIZE] = { 0 };

/*
 * write buffer blocks to disk.
 */

void write_blocks(struct super_block *sb, char *blocks, int start, int nr) {
	long pos;

	if ((pos = ftell(sb->dev)) < 0) {
		EXIT("ftell");
	}
	if (fseek(sb->dev, start * BLOCK_SIZE, SEEK_SET) < 0) {
		EXIT("fseek");
	}
	if (fwrite(blocks, BLOCK_SIZE, nr, sb->dev) != nr) {
		EXIT("fwrite");
	}
	if (fseek(sb->dev, pos, SEEK_SET) < 0) {
		EXIT("fseek");
	}
}

void zero_blocks(struct super_block *sb, int start, int nr) {
	int i;

	for (i = 0; i < nr; i++) {
		write_blocks(sb, zero, start + i, 1);
	}
}

#ifdef KLEE
void klee_make_symbolic_range(void* addr, size_t offset, size_t nbytes, const char* name) {
	assert(addr != NULL && "Must pass a valid addr");
	assert(name != NULL && "Must pass a valid name");

	if(nbytes == 0)
		return;

	// this function can be made a primitive but it will require changes to the ktest file format
	void* start = addr + offset;
	klee_check_memory_access(start, nbytes);

	void* symbolic_data = malloc(nbytes);
	klee_make_symbolic(symbolic_data, nbytes, name);
	memcpy(start, symbolic_data, nbytes);
	free(symbolic_data);
}
#endif

/*
 read 'nr' number of blocks from start offset, place them in blocks.
 reset the sb->dev block pointer to the original position.
 you need sb only for the device handle name, stored in sb->dev
 */

void read_blocks(struct super_block *sb, char *blocks, int start, int nr) {
	long pos;

	if ((pos = ftell(sb->dev)) < 0) {
		EXIT("ftell");
	}
	if (fseek(sb->dev, start * BLOCK_SIZE, SEEK_SET) < 0) {
		EXIT("fseek");
	}
	if (fread(blocks, BLOCK_SIZE, nr, sb->dev) != nr) {
		EXIT("freed");
	}
	if (fseek(sb->dev, pos, SEEK_SET) < 0) {
		EXIT("fseek");
	}

#ifdef KLEE
	int blockNumber[NUM_SYMBOLS] = {64};
	int offset[NUM_SYMBOLS] = { 12};

	int i,j;

	for(i = 0 ; i < NUM_SYMBOLS ; i++){
	//	printf("start = %d,nr = %d checking block %d, offset %d\n", start ,nr , blockNumber[i], offset[i]);
		for(j = 1 ; j <= nr ; j++){
	//		printf("comparing blockNumber %d with number %d\n",blockNumber[i], start+nr-1);
			if(blockNumber[i] == start+j-1){
	//			printf("making block %d offset %d symbolic\n",start+nr-1 , offset[i]);
				klee_make_symbolic_range(blocks, offset[i], sizeof(blocks),"offset");
			}
		}
	}
#endif
}
