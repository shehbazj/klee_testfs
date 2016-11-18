#include "testfs.h"
#include "super.h"
#include "inode.h"
#include "block.h"
#include "dir.h"
#include "tx.h"

#define KLEE

#ifdef KLEE
#include "klee/klee.h"
#endif

int namelen;

// reads the directory entry in a directory inode dir.
// updates the inode offset to point to the next directory entry
// in the inode.

/* reads next dirent, updates offset to next dirent in directory */
/* allocates memory, caller should free */
// offset is 0 initially.
struct dirent *
testfs_next_dirent(struct inode *dir, int *offset) 
{
	int ret;
	struct dirent d;
	struct dirent *dp;

	assert(dir);
	assert(testfs_inode_get_type(dir) == I_DIR);

	// check size of the directory with offset
	if (*offset >= testfs_inode_get_size(dir))
		return NULL;

	/* Make sure a struct dirent entry does not span multiple blocks. */
	if((((*offset) + sizeof(struct dirent)) / BLOCK_SIZE) > ((*offset) / BLOCK_SIZE))
		(*offset) = (((*offset) + sizeof(struct dirent)) / BLOCK_SIZE) * BLOCK_SIZE;

	// read data from dir into buffer "d" at offset-offset of size struct dirent
	// dirent contains inode number and name length value
	ret = testfs_read_data(dir, *offset, (char *) &d, sizeof(struct dirent));
	if (ret < 0)
		return NULL;

	// assert(d.d_name_len > 0);
	if(d.d_name_len == 0) {
		/* The next entry is located inside the next block allocated for this directory. */
		(*offset) = (((*offset) / BLOCK_SIZE) + 1) * BLOCK_SIZE;

		ret = testfs_read_data(dir, *offset, (char *) &d, sizeof(struct dirent));
		if (ret < 0)
			return NULL;
	}

	dp = malloc(sizeof(struct dirent) + d.d_name_len);
	if (!dp)
		return NULL;

	*dp = d;
	// increment offset as we have already read dirent
	*offset += sizeof(struct dirent);
	// since d_dname is stored at the end of every dirent, we need to read that many
	// bytes of data
	ret = testfs_read_data(dir, *offset, D_NAME(dp), d.d_name_len);
	if (ret < 0) {
		free(dp);
		return NULL;
	}
	*offset += d.d_name_len;
	return dp;
}

/* returns dirent associated with inode_nr in dir.
 * returns NULL on error.
 * allocates memory, caller should free. */
static struct dirent *
testfs_find_dirent(struct inode *dir, int inode_nr) 
{
	struct dirent *d;
	int offset = 0;

	assert(dir);
	assert(testfs_inode_get_type(dir) == I_DIR);
	assert(inode_nr >= 0);

	// go in a linear order searching from current directories inode
	// to all other inodes by comparing inode numbers
	// after every iteration, the offset is updated to the next dirent
	// node.
	for (; (d = testfs_next_dirent(dir, &offset)); free(d)) {
		if (d->d_inode_nr == inode_nr)
			return d;
	}
	return NULL;
}

/* return 0 on success.
 * return negative value on error. 
 * dir is the directory in which we need to write file or directory name
 * corresponding to touch or mkdir.
 * name is the name of the file or directory. 
 * len - length of the filename/directoryname. 
 * inode_nr - number of newly created inode.
 * offset - physical offset on the directory file. 
 */

static int testfs_write_dirent(struct inode *dir, char *name, int len,
		int inode_nr, int offset) 
{
	int ret;
	int total_bytes = sizeof(struct dirent) + len;
	struct dirent *d = malloc(total_bytes);
	if (!d)
		return -ENOMEM;

	assert(inode_nr >= 0);
	d->d_name_len = len;
	d->d_inode_nr = inode_nr;
	strcpy(D_NAME(d), name);

	/* Make sure a struct dirent entry does not span multiple blocks. In case the new dirent
	 * does not fit inside the current block, we fill the current block with zeroes and update
	 * the offset to point at the next available offset. */
	if(((offset + total_bytes) / BLOCK_SIZE) > (offset / BLOCK_SIZE)) {
		int next_offset = ((offset + total_bytes) / BLOCK_SIZE) * BLOCK_SIZE;
		int total = next_offset - offset;
		char *buf = malloc(total);

		if(!buf)
			return -ENOMEM;

		memset(buf, 0, total);
		ret = testfs_write_data(dir, offset, buf, total);
		free(buf);

		if(ret < 0)
			return ret;

		/* Update the current offset. */
		offset = next_offset;
	}

	ret = testfs_write_data(dir, offset, (char *) d, total_bytes);
	free(d);
	return ret;
}

/* return 0 on success.
 * return negative value on error. */
/*
 called whenever a new file or a directory is created.
 when new entity is created, we add "name" to "dir" directory
 the new file or directories inode is dir.
 */

static int testfs_add_dirent(struct inode *dir, char *name, int inode_nr) 
{
	struct dirent *d;
	int p_offset = 0, offset = 0;
	int found = 0;
	int ret = 0;
	int len = strlen(name) + 1;
	// newly created file and directories will have there
	// name recorded.

	assert(dir);
	assert(testfs_inode_get_type(dir) == I_DIR);
	assert(name);
	for (; ret == 0 && found == 0; free(d)) {
		p_offset = offset;
		// goes through each directory/file entry insode dir.
		// updates offset to point to next file/directory entry.
		if ((d = testfs_next_dirent(dir, &offset)) == NULL)
			// reached last directory/file in the inode
			break;

		if ((d->d_inode_nr >= 0) && (strcmp(D_NAME(d), name) == 0)) {
			// d->d_inode_nr >=0 means we found an inode 
			// strcmp ==0 means the file or directory already exists
			ret = -EEXIST;
			continue;
		}

		if ((d->d_inode_nr >= 0) || (d->d_name_len != len))
			continue;

		found = 1;
	}
	if (ret < 0)
		return ret;

	assert(found || (p_offset == testfs_inode_get_size(dir)));
	// writes directory information to file dir. enters name, length
	// p_offset contains the offset where to write (usually at the end)
	// dir = name of parent directory. name = name of newly created file/directory
	// len = length of the string name + 1. inode_nr number of newly created
	// file or directory.
	// XXX why do we need to go to the end of the file to write directory
	// entry?
	return testfs_write_dirent(dir, name, len, inode_nr, p_offset);
}

/* returns negative value if name within dir is not empty */
static int testfs_remove_dirent_allowed(struct super_block *sb, int inode_nr) 
{
	struct inode *dir;
	int offset = 0;
	struct dirent *d;
	int ret = 0;

	// get inode will retrieve the inode from memory (hash table)
	// increment the reference count. If the in memory inode does 
	// not already exist, it will create a new one.
	dir = testfs_get_inode(sb, inode_nr);

	// if it is only a file that you need to delete, remove the
	// in-memory inode
	if (testfs_inode_get_type(dir) != I_DIR)
		goto out;

	// iterate through the directory entries; if there is any entry
	// other than . or .., or with d_inode_nr < 0, return that there
	// exists some directory inside the directory (return -ENOEMPTY)
	for (; ret == 0 && (d = testfs_next_dirent(dir, &offset)); free(d)) {
		if ((d->d_inode_nr < 0) || (strcmp(D_NAME(d), ".") == 0)
				|| (strcmp(D_NAME(d), "..") == 0))
			continue;
		ret = -ENOTEMPTY;
	}

	out:
	// decrement inode count by 1, remove from hash.
	testfs_put_inode(dir);
	return ret;
}

/* 
 this does not implement garbage collection. Only
 the inode_nr corresponding to the file or directory to be
 deleted are set to -1.
 returns inode_nr of dirent removed
 returns negative value if name is not found */
static int testfs_remove_dirent(struct super_block *sb, struct inode *dir, char *name) 
{
	struct dirent *d;
	int p_offset;
	int offset = 0;
	int inode_nr = -1;
	int ret = -ENOENT;

	assert(dir);
	assert(name);
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
		return -EINVAL;
	}
	for (; inode_nr == -1; free(d)) {
		p_offset = offset;
		// reached last element in the dirent file, return
		// with -ENOENT error
		if ((d = testfs_next_dirent(dir, &offset)) == NULL)
			break;

		// XXX in what scenario will d_inode_nr be 0?
		// if we read a valid directory entry, and it does
		// not correpond to the directory that we are looking
		// for, continue.
		if ((d->d_inode_nr < 0) || (strcmp(D_NAME(d), name) != 0))
			continue;

		/* found the dirent */
		inode_nr = d->d_inode_nr;

		// check if there are no children directories or subdirectories
		// in the directory to delete. also, remove the inode from
		// hash table, and delete the in memory inode
		if ((ret = testfs_remove_dirent_allowed(sb, inode_nr)) < 0)
			continue; /* this will break out of the loop */

		// set inode_nr to -1
		d->d_inode_nr = -1;
		ret = testfs_write_data(dir, p_offset, (char *) d, sizeof(struct dirent) + d->d_name_len);
		if (ret >= 0)
			ret = inode_nr;
	}
	return ret;
}

static int testfs_create_empty_dir(struct super_block *sb, int p_inode_nr, struct inode *cdir) 
{
	int ret;

	assert(testfs_inode_get_type(cdir) == I_DIR);
	ret = testfs_add_dirent(cdir, ".", testfs_inode_get_nr(cdir));
	if (ret < 0)
		return ret;

	ret = testfs_add_dirent(cdir, "..", p_inode_nr);
	if (ret < 0) {
		testfs_remove_dirent(sb, cdir, ".");
		return ret;
	}
	return 0;
}

/*
 dir is the inode corresponding to current directory.
 */

static int testfs_create_file_or_dir(struct super_block *sb, struct context *c, inode_type type, char *name)
{
	int name_offset;
	int ret;
	struct inode *in;
	int inode_nr;
	char *name_to_create = name;	// KLEE - need to keep track of name_to_create
	int current_inode;

	int sym_namelen = strlen(name);
	int const_namelen = strlen(name);

	klee_make_symbolic(&sym_namelen,sizeof(sym_namelen) , "namelen");

	if(name != NULL) {
		if(!strcmp(name, "/"))
			return -EEXIST;

		/* Search for the first occurrence of '/', starting from the end of the specified name. */
		for(name_offset = const_namelen - 1; name_offset >= 0; --name_offset)	// KLEE - generate
			if(name[name_offset] == '/')			// only different name_offset values
				break;

		if(name_offset >= 0) {		// KLEE - check for name_offset > 0 and less than 0.
			/* In case the specified path is an absolute path to a file or directory
			 * inside the root directory, increase the offset by 1, in order to set
			 * the path variable equal to "/". */
			if(name_offset == 0)	// KLEE - check for name_offset ==0, and name_offset !=0
						// KLEE - can optimise since path already has constraint
				++name_offset;

			/* The specified path represents either an absolute or a relative path. */
			char *path = malloc(name_offset + 1);
			if(path == NULL)
				return -ENOMEM;

			/* Copy the path until the last occurrence of '/', store the current inode
			 * number and finally, change directory. */
			strncpy(path, name, name_offset);	// KLEE- keep track of path
			path[name_offset] = '\0';

			current_inode = testfs_inode_get_nr(c->cur_dir);
			c->cmd[1] = path;		// KLEE - keep track of c->cmd[1]
			ret = cmd_cd(sb, c);		// KLEE*** - check for cmd_cd() call
			free(path);			// KLEE - remove c->cmd[] tracking

			if(ret < 0)
				return ret;

			/* Update the offset to the start of the new name. */
			name_to_create = name + name_offset + 1;	// KLEE - update name_to_create
		}
	}

	/* Make sure that the specified name does not exceed the size of one block. */
	if(name_to_create != NULL)				// KLEE - TODO -generate name such that both
								// name_to_create != and == NULL are tested
		if(namelen + 1 > BLOCK_SIZE - sizeof(struct dirent))	// KLEE GENERATE different namelens
			return -EINVAL;

	testfs_tx_start(sb, TX_CREATE);
	if (c) {
		// Check if the specified name exists inside the current directory.
		inode_nr = testfs_dir_name_to_inode_nr(sb, &c->cur_dir, name_to_create);
		if (inode_nr >= 0) {
			ret = -EEXIST;
			goto fail;
		}
	}

	/* first create inode */
	/*
	 * allocates a new inode number in inode freemap.
	 * allocates new inode (using calloc). assigns in to
	 * newly created inode
	 */
	ret = testfs_create_inode(sb, type, &in);
	if (ret < 0) {
		goto fail;
	}
	inode_nr = testfs_inode_get_nr(in);

	if (type == I_DIR) { /* create directory */
		int p_inode_nr = c ? testfs_inode_get_nr(c->cur_dir) : inode_nr;
		ret = testfs_create_empty_dir(sb, p_inode_nr, in);
		if (ret < 0)
			goto out;
	}
	/* then add directory entry */
	// inode_nr is the number of the newly created inode
	// dir - name of parent directory. name- name of new file/directory
	if (c) {	// KLEE*** - check for name_to_create
		if ((ret = testfs_add_dirent(c->cur_dir, name_to_create, inode_nr)) < 0)
			goto out;
		testfs_sync_inode(c->cur_dir);
	}
	testfs_sync_inode(in);
	testfs_put_inode(in);
	testfs_tx_commit(sb, TX_CREATE);

	/* Restore the current directory to its previous path, before the invocation of this function. */
	if(name != NULL && name_offset > 0) {	// KLEE - generate different name_offset lengths
		testfs_put_inode(c->cur_dir);
		c->cur_dir = testfs_get_inode(sb, current_inode);
	}
	return 0;
	printf("error in creation\n");

	out: testfs_remove_inode(in);
	fail: testfs_tx_commit(sb, TX_CREATE);

	/* Restore the current directory to its previous path, before the invocation of this function. */
	if(name != NULL && name_offset > 0) {	// KLEE - generate different name_offset lengths
		testfs_put_inode(c->cur_dir);
		c->cur_dir = testfs_get_inode(sb, current_inode);
	}
	return ret;
}

static int testfs_pwd(struct super_block *sb, struct inode *in)
{
	int p_inode_nr;
	struct inode *p_in;
	struct dirent *d;
	int ret;

	assert(in);
	assert(testfs_inode_get_nr(in) >= 0);
	p_inode_nr = testfs_dir_name_to_inode_nr(sb, &in, "..");
	assert(p_inode_nr >= 0);
	if (p_inode_nr == testfs_inode_get_nr(in)) {
		printf("/");
		return 1;
	}
	p_in = testfs_get_inode(sb, p_inode_nr);
	d = testfs_find_dirent(p_in, testfs_inode_get_nr(in));
	assert(d);
	ret = testfs_pwd(sb, p_in);	// recursion, keep
	testfs_put_inode(p_in);		// looping till root directory
	// is reached.
	printf("%s%s", ret == 1 ? "" : "/", D_NAME(d));
	free(d);
	return 0;
}

int testfs_dir_name_to_inode_nr_rec(struct super_block *sb, struct inode **dir, char *name)
{
	struct inode *p_in;
	struct dirent *d;
	int i;
	int offset = 0;
	int name_offset = -1;
	int ret = -ENOENT;
	char *entry_name;
	char *name_to_search = name;	// KLEE track name_to_search

	assert(*dir);
	assert(name);
	assert(testfs_inode_get_type(*dir) == I_DIR);

	if(!strcmp(name, "/")) {
		/* Special case where the specified directory equals to root. */
		return 0;
	}
	else {
		/* Search if the contains a path. */
		for(i = 0; i < strlen(name); ++i) {	// KLEE generate 2 states
			if(name[i] == '/') {
				name_offset = i;	// KLEE name_offset symbolic
				break;
			}
		}

		if(name_offset == 0) {			// KLEE generate 2 name_offsets
			/* The specified path is absolute. After fetching the inode associated
			 * with the root directory, the function proceeds recursively. */
			p_in = testfs_get_inode(sb, 0);
			testfs_put_inode(*dir);
			(*dir) = p_in;

			return testfs_dir_name_to_inode_nr_rec(sb, dir, name + 1); // KLEE track name+1
		}
		else if(name_offset == (strlen(name) - 1))	// KLEE track name_offset
			/* No entry name is terminated with the '/' character. */
			return ret;
		else {
			if(name_offset != -1) {	// KLEE track name_offsets
				/* The specified name represents a relative path; thus, the name of the
				 * first entry must be extracted. Otherwise, by default, the name to search
				 * for is equal to the specified name. */
				entry_name = malloc(name_offset + 1);
				if (!entry_name)
					return -ENOMEM;

				strncpy(entry_name, name, name_offset);	// KLEE track entry_name
				entry_name[name_offset] = '\0';
				name_to_search = entry_name;	// KLEE track name_to_search
			}

			for (; ret < 0 && (d = testfs_next_dirent(*dir, &offset)); free(d)) {
				if ((d->d_inode_nr < 0) || (strcmp(D_NAME(d), name_to_search) != 0))
					// KLEE TODO name compare 
					continue;

				ret = d->d_inode_nr;
			}

			if(name_offset != -1) {	// KLEE check
				/* The specified name represents a relative path; the function continues
				 * its recursion. */
				if(ret < 0)
					return ret;

				/* Replace the old inode with the newly read one. */
				p_in = testfs_get_inode(sb, ret);
				testfs_put_inode(*dir);
				(*dir) = p_in;

				free(name_to_search);
				return testfs_dir_name_to_inode_nr_rec(sb, dir, name + name_offset + 1); // KLEE
							//check name + name_offset + 1
			}
			else {
				/* The specified name does not contain any paths. */
				return ret;
			}
		}
	}
}

/* returns negative value if name is not found */
/* takes current directory inode and the destination path
 to which we need to cd. returns inode number corresponding
 to the destination path.
 */

int testfs_dir_name_to_inode_nr(struct super_block *sb, struct inode **dir, char *name) {
	int ret;
	int current_inode_number = testfs_inode_get_nr(*dir);

	if(name[strlen(name) - 1] == '/' && strcmp(name, "/"))
		return -EINVAL;

	ret = testfs_dir_name_to_inode_nr_rec(sb, dir, name);	// KLEE*** continue exploring name
	if(testfs_inode_get_nr(*dir) != current_inode_number) {
		testfs_put_inode(*dir);
		(*dir) = testfs_get_inode(sb, current_inode_number);
	}
	return ret;
}

int testfs_make_root_dir(struct super_block *sb) 
{
	return testfs_create_file_or_dir(sb, NULL, I_DIR, NULL);
}

int cmd_cd(struct super_block *sb, struct context *c) 
{
	int inode_nr;
	struct inode *dir_inode;

	if (c->nargs != 2)
		return -EINVAL;

	// get destination directories inode number
	// KLEE*** - check function call
	inode_nr = testfs_dir_name_to_inode_nr(sb, &c->cur_dir, c->cmd[1]);
	if (inode_nr < 0)
		return inode_nr;

	// get inode from destination directories inode number
	dir_inode = testfs_get_inode(sb, inode_nr);
	if (testfs_inode_get_type(dir_inode) != I_DIR) {
		// check if inode number is non-zero. if it is 
		// zero, discard the inode from the hash table
		// otherwise decrement the inode_count but 
		// retain the inode. 
		// XXX where is inode count incremented?
		testfs_put_inode(dir_inode);
		return -ENOTDIR;
	}

	// same as the destination inode, do not retain original
	// current directory after use.
	testfs_put_inode(c->cur_dir);

	// change cur_dir to new destination inode.
	c->cur_dir = dir_inode;
	return 0;
}

int cmd_pwd(struct super_block *sb, struct context *c) 
{
	if (c->nargs != 1) {
		return -EINVAL;
	}
	// enters a recursive loop from current directory
	testfs_pwd(sb, c->cur_dir);
	printf("\n");
	return 0;
}

static int testfs_ls(struct inode *in, int recursive) 
{
	int offset = 0;
	struct dirent *d;

	// d gets the dirent stored in the inode.
	// a inode for a directory contains entries for all the constituent
	// directories and files. the directories have the structure dirent+dirname
	// dirent take the form of {dir_number, dir_name_size}. from the
	// dir_name_size, we get to know how far we need to read in inode to
	// get the name of the directory

	// XXX is there a limit to the number of subdirectories we can create 
	// within a directory? also is this limit variable? since each directory
	// can occupy dirent + dir_name_len space in inode file. so if we create
	// less files with large names, v/s more files with small names, does that
	// come out to the same? 
	for (; (d = testfs_next_dirent(in, &offset)); free(d)) {
		struct inode *cin;

		if (d->d_inode_nr < 0)
			continue;

		cin = testfs_get_inode(testfs_inode_get_sb(in), d->d_inode_nr);
		// name of a file is also located on the inode
		// D_NAME will print name of the file or the directory
		// depending on the inode type
		printf("%s%s\n", D_NAME(d), (testfs_inode_get_type(cin) == I_DIR) ? "/" : "");

		if (recursive && testfs_inode_get_type(cin) == I_DIR
				&& (strcmp(D_NAME(d), ".") != 0)
				&& (strcmp(D_NAME(d), "..") != 0)) {
			testfs_ls(cin, recursive);
		}
		testfs_put_inode(cin);
	}
	return 0;
}

int cmd_ls(struct super_block *sb, struct context *c) 
{
	int inode_nr;
	struct inode *in;
	char *cdir = ".";

	if (c->nargs != 1 && c->nargs != 2)
		return -EINVAL;

	if (c->nargs == 2)
		cdir = c->cmd[1];

	// get inode number of directory path provided in cdir
	assert(c->cur_dir);
	inode_nr = testfs_dir_name_to_inode_nr(sb, &c->cur_dir, cdir);
	if (inode_nr < 0)
		return inode_nr;

	// get the inode corresponding to ls argument
	in = testfs_get_inode(sb, inode_nr);

	// do ls on inode corresponding to argument
	// second arg = whether recursive ls or not
	// testfs_ls(in,1) used for cmd_lsr
	testfs_ls(in, 0);

	// when we do get inode, the inode gets stored in the hash
	// table. we need to remove the inode from the hash table
	// this is why we call testfs_put
	testfs_put_inode(in);

	return 0;
}

int cmd_lsr(struct super_block *sb, struct context *c) 
{
	int inode_nr;
	struct inode *in;
	char *cdir = ".";

	if (c->nargs != 1 && c->nargs != 2)
		return -EINVAL;

	if (c->nargs == 2)
		cdir = c->cmd[1];

	assert(c->cur_dir);

	// get inode number from current directory name and 
	// destination directory name
	inode_nr = testfs_dir_name_to_inode_nr(sb, &c->cur_dir, cdir);
	if (inode_nr < 0)
		return inode_nr;

	// get inode corresponding to the inode number obtained
	// above.
	in = testfs_get_inode(sb, inode_nr);

	// you now have inode of destination, so do recursive ls. 
	testfs_ls(in, 1);

	// empty inode from hash table. it was inserted when you 
	// did get inode earlier.
	testfs_put_inode(in);
	return 0;
}

int cmd_create(struct super_block *sb, struct context *c) 
{
	int i;
	int ret;

	printf("cmd_create\n");
	if (c->nargs < 2) {
		return -EINVAL;
	}

	for (i = 1; i < c->nargs; i++) {
		ret = testfs_create_file_or_dir(sb, c, I_FILE, c->cmd[i]);
		if(ret < 0)
			return ret;
	}

	return 0;
}

int cmd_stat(struct super_block *sb, struct context *c) 
{
	int inode_nr;
	struct inode *in;
	int i;

	if (c->nargs < 2)
		return -EINVAL;

	for (i = 1; i < c->nargs; i++) {
		// get the inode corresponding to the file/directory argument
		inode_nr = testfs_dir_name_to_inode_nr(sb, &c->cur_dir, c->cmd[i]);
		if (inode_nr < 0)
			return inode_nr;

		// get inode / create inode corresponding to the file/directory
		// argument
		in = testfs_get_inode(sb, inode_nr);
		printf("%s: i_nr = %d, i_type = %d, i_size = %d\n", c->cmd[i],
				testfs_inode_get_nr(in), testfs_inode_get_type(in),
				testfs_inode_get_size(in));
		testfs_put_inode(in);
	}
	return 0;
}

int cmd_rm(struct super_block *sb, struct context *c) 
{
	int inode_nr;
	struct inode *in;

	if (c->nargs != 2)
		return -EINVAL;

	testfs_tx_start(sb, TX_RM);
	// check if dir entry can be removed or not.
	// also set the inode number to -1
	// returns the value of d_inode_nr before it was set to -1.
	inode_nr = testfs_remove_dirent(sb, c->cur_dir, c->cmd[1]);
	if (inode_nr < 0) {
		testfs_tx_commit(sb, TX_RM);
		return inode_nr;
	}
	in = testfs_get_inode(sb, inode_nr);
	// TODO check how garbage collection is done.
	testfs_remove_inode(in);
	testfs_sync_inode(c->cur_dir);
	testfs_tx_commit(sb, TX_RM);
	return 0;
}

int cmd_mkdir(struct super_block *sb, struct context *c) 
{
	if (c->nargs != 2) {
		return -EINVAL;
	}
//	char *command = c->cmd[1];
//	klee_make_symbolic(&command[0], sizeof(char) , "command");
	return testfs_create_file_or_dir(sb, c, I_DIR, c->cmd[1]);
}
