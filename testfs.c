//#define KLEE

/*
	Modified earlier version of testfs command to accept input directly from command line.
	theres no prompt.
	preq:
	create filesystem at /tmp/file using mktestfs
	to run:
	./testfs_all touch abc
	
	KLEE related stuff will be done at block layer
*/

#define _GNU_SOURCE
#include <stdbool.h>
#include <getopt.h>
#include "testfs.h"
#include "super.h"
#include "inode.h"
#include "dir.h"
#include "tx.h"

#ifdef KLEE
#include <klee/klee.h>
#endif

static int cmd_help(struct super_block *, struct context *c);
static int cmd_quit(struct super_block *, struct context *c);
static bool can_quit = false;

#define PROMPT printf("%s", "% ")

static struct {
	const char *name;
	int (*func)(struct super_block *sb, struct context *c);
        int max_args;
} cmdtable[] = {
        /* menus */
        { "?",          cmd_help,       1, },
        { "cd",         cmd_cd,         2, },
        { "pwd",        cmd_pwd,        1, },
        { "ls",         cmd_ls,         2, },
        { "lsr",        cmd_lsr,        2, },
        { "touch",      cmd_create,     MAX_ARGS, },
        { "stat",       cmd_stat,       MAX_ARGS, },
        { "rm",         cmd_rm,         2, },
        { "mkdir",      cmd_mkdir,      2, },
        { "cat",        cmd_cat,        MAX_ARGS, },
		{ "catr",       cmd_catr,       2, },
        { "write",      cmd_write,      2, },
		{ "owrite",     cmd_owrite,		3, },
		{ "oread",      cmd_oread,		3, },
        { "checkfs",    cmd_checkfs,    1, },
        { "quit",    	cmd_quit,       1, },
        { NULL,         NULL}
};

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

static int cmd_help(struct super_block *sb, struct context *c) {
	int i = 0;

	printf("Commands:\n");
	for (; cmdtable[i].name; i++) {
		printf("%s\n", cmdtable[i].name);
	}
	return 0;
}

static int cmd_quit(struct super_block *sb, struct context *c) {
	printf("Bye!\n");
	can_quit = true;
	return 0;
}

// tokenize the command, place it in the form of cmd, arg1, arg2 ... in
// context c->cmd[] structure. pass c and superblock sb to cmdtable->func

static void handle_command(struct super_block *sb, struct context *c,
		char * name, char * args) {
	int i, j =0; 
	if (name == NULL)
		return;

	/// c->cmd[0] contains the command's name. This statement must be executed
	 // at every invocation. Otherwise, if the command does not exist, c->cmd[0]
	 // will either contain the last successful executed command, or it will be
	 // undefined possibly resulting in a Segmentation fault. 
	c->cmd[j++] = name;

	for (i = 0; cmdtable[i].name; i++) {
		if (strcmp(name, cmdtable[i].name) == 0) {
			char * token = args;
			assert(cmdtable[i].func);

			// context->cmd contains the command's arguments, starting from index 1.
			while (j < cmdtable[i].max_args
					&& (c->cmd[j] = strtok(token, " \t\n")) != NULL) {
				j++;
				token = NULL;
			}
			if ((c->cmd[j] = strtok(token, "\n")) != NULL) {
				j++;
			}
			for (c->nargs = j++; j <= MAX_ARGS; j++) {
				c->cmd[j] = NULL;
			}
			
	//		for ( k = 1; k < cmdtable[i].max_args ; k++){
	//			klee_make_symbolic(c->cmd[k], sizeof(c->cmd[k]), "arg");
	//		}
			errno = cmdtable[i].func(sb, c);
			if (errno < 0) {
				errno = -errno;
				WARN(c->cmd[0]);
			}
			return;
		}
	}
	printf("%s: command not found: type ? for help...\n", c->cmd[0]);
}

static void usage(const char * progname) {
	fprintf(stdout, "Usage: %s [-ch][--help] rawfile\n", progname);
	exit(1);
}

struct args {
	const char * disk;  // name of disk
	int corrupt;        // to corrupt or not
};

#ifndef KLEE
static struct args *
parse_arguments(int argc, char * const argv[]) {
	static struct args args = { 0 };
// struct options -
// name of the option. 
// has arg {no_argument, required_argument, optional_argument}
// flag ptr - 0 means val is the value that identifies this option
// flag ptr - non null - address of int variable which is flag for the option
// val - c or h
	static struct option long_options[] = { { "corrupt", no_argument, 0, 'c' },
			{ "help", no_argument, 0, 'h' }, { 0, 0, 0, 0 }, };
	int running = 1;

	while (running) {
		int option_index = 0;
		// getopt_long - decode options from argv.
		// getopt_long (int argc, char *const *argv, const char *shortopts, const struct option *longopts, int *indexptr)
		//
		int c = getopt_long(argc, argv, "ch", long_options, &option_index);
		switch (c) {
		case -1:
			running = 0;
			break;
		case 0:
			break;
		case 'c':
			args.corrupt = 1;
			break;
		case 'h':
			usage(argv[0]);
			break;
		case '?':
			usage(argv[0]);
			break;
		default:
			abort();
		}
	}
	// optind - index of next variable to be processed in argv.    
	if (argc < 2)
		usage(argv[0]);

	args.disk = argv[optind];
	return &args;
}
#endif

int main(int argc, char * const argv[]) {
/*
	struct super_block *sb;
	#ifndef KLEE
	int it;
	#endif
	int ret;
	struct context c;
	// context contains command line arguments/parameters,
	// inode of directory from which cmd was issued, and no of args.

	//#ifndef KLEE
	//struct args * args = parse_arguments(argc, argv);
	//#endif
	
//	if(argc < 3){
//		usage(argv[0]);
//	}

	// args->disk contains the name of the disk file. 
	// initializes the in memory structure sb with data that is 
	// read from the disk. after successful execution, we have 
	// sb initialized to dsuper_block read from disk.
//	printf("args->disk = %s, args->corrupt = %d\n", args->disk, args->corrupt);
	ret = testfs_init_super_block("/tmp/file", 0 , &sb);
	printf("testfs initialized\n");
	if (ret) {
		EXIT("testfs_init_super_block");
	}
	// if the inode does not exist in the inode_hash_map (which
	// is an inmemory map of all inode blocks, create a new inode by
	// allocating memory to it. read the dinode from disk into that
	// memory inode
	// 
	c.cur_dir = testfs_get_inode(sb, 0); // root dir 
		char name[50];
		char arguments[10];

		strcpy(name, argv[1]);
	
	printf("testfs_get_inode done\n");
	//void klee_make_symbolic_range(void* addr, size_t offset, size_t nbytes, const char* name) {
	#ifdef KLEE
//		klee_make_symbolic_range(arguments, 10, 10, "filename");
		klee_make_symbolic(arguments, sizeof arguments, "filename");
	#else
		int prev_len = 0;
		printf("argc = %d, argv = %s\n", argc, argv[1]);
		for (it = 1; it < argc ; it++){
			printf("begin loop, it = %d, argc = %d\n", it, argc);
			strcpy(arguments + prev_len, argv[it]);
			printf("strcpy done %s\n", arguments);
			prev_len += strlen(argv[it]);
			printf("prev len updated = %d\n", prev_len);
			arguments[prev_len] = ' ';
			printf("args updated\n");
			prev_len++; 
		}
		arguments[prev_len] = '\0';
		printf("name = %s, arguments = %s\n", name, arguments);
	#endif
		printf("arguments set\n");
		handle_command(sb, &c, name, arguments);
		printf("handle_command\n");
		if (can_quit) {
			return 1;
		}
	// decrement inode count by 1. remove inode from in_memory hash map if
	// inode count has become 0.
	testfs_put_inode(c.cur_dir);
	testfs_close_super_block(sb);
	return 0;
*/

	struct super_block *sb;
        char line[1000];
        int ret;
        struct context c;
        // context contains command line arguments/parameters,
        // inode of directory from which cmd was issued, and no of args.

        struct args * args = parse_arguments(argc, argv);

        // args->disk contains the name of the disk file. 
       // initializes the in memory structure sb with data that is 
       // read from the disk. after successful execution, we have 
       // sb initialized to dsuper_block read from disk.
       ret = testfs_init_super_block("/tmp/file", args->corrupt, &sb);
       //fslice_clear();       
       if (ret) {
               EXIT("testfs_init_super_block");
       }
        /* if the inode does not exist in the inode_hash_map (which
         is an inmemory map of all inode blocks, create a new inode by
         allocating memory to it. read the dinode from disk into that
         memory inode
         */
        c.cur_dir = testfs_get_inode(sb, 0); /* root dir */
	int paramNo, offset = 0;
	printf("argc = %d\n", argc);
	for(paramNo = 1 ; paramNo < argc ; paramNo ++){
		printf("param = %d\n", paramNo);
		printf("offset = %d\n",offset);
		printf("argument being copied = %s\n", argv[paramNo]);
		strcpy(line + offset, argv[paramNo]);
		printf("line = %s\n",line);
		offset = strlen(line)+1;
		line[offset] = '\0';
		printf("new line length = %lu\n", strlen(line));
		printf("new offset = %d\n", offset);
		line[offset-1] = ' ';
	}
		line[offset-1] = '\0';
	printf("line  = %s\n", line);
	
                char * name;
                char * args1;

                printf("command: %s\n", line);
                name = strtok(line, " \t\n");
                args1 = strtok(NULL, "\n");
                handle_command(sb, &c, name, args1);

        // decrement inode count by 1. remove inode from in_memory hash map if
        // inode count has become 0.
        testfs_put_inode(c.cur_dir);
        testfs_close_super_block(sb);
        return 0;
}
