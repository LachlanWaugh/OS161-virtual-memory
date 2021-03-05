/* Declarations for file handle and file table management. */

#ifndef _FILE_H_
#define _FILE_H_

/* Contains some file-related maximum length constants */
#include <limits.h>

//------------------------ Data types -----------------------//

/* global open file table entry */
struct of_entry_t {
    struct vnode *vn;           /* the vnode for the file          */
    off_t offset;               /* the current offset in the file  */
    int mode;                   /* the access mode of the file     */
    int ref_count;              /* the reference count of the file */
};

/* global open file table */
struct of_table_t {
    struct lock *lock;                         /* lock for the file table */
    struct of_entry_t *of_entries[OPEN_MAX];   /* an array of open files  */
};

/* per-process file descriptor array/table */
struct fd_table_t {
    int of_index[OPEN_MAX];  /* array of indices into the open file table */
};

/* Global open file table */
struct of_table_t *of_table;

/* All unused fds will be set to this value */
#define FILE_UNUSED -1

//-------------------- Syscall Functions --------------------//

/*
* Open system call
*/
int sys_open(userptr_t filename, int flags, mode_t mode, int *retval);

/*
* Open system call
*/
int sys_close(int fd);

/*
* Open system call
*/
int sys_read(int fd, userptr_t buf, size_t buflen, int *retval);

/*
* Open system call
*/
int sys_write(int fd, userptr_t buf, size_t nbytes, int *retval);

/*
* Open system call
*/
int sys_lseek(int fd, off_t pos, int whence, off_t *retval);

/*
* Open system call
*/
int sys_dup2(int oldfd, int newfd, int *retval);

//-----------------------------------------------------------//

//--------------------- Helper Functions --------------------//

/*
* This function is called whenever a new process is started.
*
* Create a file descriptor table which stores all of the files a process
* has open. These files are stored as references to entries in the global
* open file table. Each fd will store a index into the table.
*
* When initialized, this function will create entries for stdout and stderr
* in file descriptors in indices 1 and 2.
*
* When this process is run, it checks whether there is a current global
* open file table, if there is none, it will call create_of_table.
*/
int create_fd_table(void);

/*
* Close all the file descriptors for a process, and free the memory
* allocated for the table.
*/
void close_fd_table(void);

/*
* Initialize the global open file table, each entry in the table will
* correspond to a file which has been opened by a running process.
*/
int create_of_table(void);

/*
* Closes the open file table, free all memory allocated for the table
*/
void close_of_table(void);

//-----------------------------------------------------------//

#endif /* _FILE_H_ */
