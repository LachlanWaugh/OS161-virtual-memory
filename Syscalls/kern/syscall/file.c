#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <kern/limits.h>
#include <kern/stat.h>
#include <kern/seek.h>
#include <lib.h>
#include <uio.h>
#include <thread.h>
#include <current.h>
#include <synch.h>
#include <vfs.h>
#include <vnode.h>
#include <file.h>
#include <syscall.h>
#include <copyinout.h>
#include <proc.h>
#include <device.h>

/*
* Explanations of the functions are provided in file.h
*/

/* Open system call */
int sys_open(userptr_t filename, int flags, mode_t mode, int *retval) {
    struct vnode *vn;
    int result, fd_index = -1, of_index = -1;

    /* Check the flags are valid */
    if (((flags & O_ACCMODE) != flags) &&
        /* O_RDONLY == 0, so the normal check doesn't work */
        !(flags & O_ACCMODE) != O_RDONLY) {
         return EINVAL;
    }

    /* If the function is called by another function, there is no need to
    copy the string to user space, so first attempt to open the node at
    filename, if that fails attempt to copy the filename to userspace and
    use that as the filename, if this also fails return the error */
    result = vfs_open((char *) filename, flags, mode, &vn);
    if (result) {
        char file_name[PATH_MAX];
        /* copy the file name from user space to kernel space */
        result = copyinstr(filename, file_name, PATH_MAX, NULL);
        if (result) {
            return result;
        }

        /* open the vnode */
        result = vfs_open(file_name, flags, mode, &vn);
        if (result) {
            return result;
        }
    }

    /* acquire the lock for the global open file table */
    lock_acquire(of_table->lock);

    /* get the process' fd table, and find the next available fd */
    struct fd_table_t *fd_table = curproc->fd_table;
    for (int i = 0; i < OPEN_MAX; i++) {
        if (fd_table->of_index[i] == FILE_UNUSED) {
            fd_index = i;
            break;
        }
    }

    /* find the next available spot in global open file table */
    for (int i = 0; i < OPEN_MAX; i++) {
        if (of_table->of_entries[i] == NULL) {
            of_index = i;
            break;
        }
    }

    /* check that both tables provided valid entries */
    if (fd_index == -1 || of_index == -1) {
        vfs_close(vn);
        lock_release(of_table->lock);
        return EMFILE;
    }

    /* The file descriptor table stores the index of the open files entry
    in the open files table (rather than a pointer) */
    fd_table->of_index[fd_index] = of_index;

    /* Create the open file entry */
    struct of_entry_t *of_entry = kmalloc(sizeof(struct of_entry_t));
    if (of_entry == NULL) {
        vfs_close(vn);
        lock_release(of_table->lock);
        return ENOMEM;
    }

    /* Set up the attributes of the file */
    of_entry->vn = vn;         /* The vnode for the file                      */
    of_entry->offset = 0;      /* The location of the pointer within the file */
    of_entry->mode = flags;    /* The access mode of the file                 */
    of_entry->ref_count = 1;   /* The number of process' accessing this file  */

    /* Add the file to the global open files table */
    of_table->of_entries[of_index] = of_entry;

    /* Release the lock for the open file table */
    lock_release(of_table->lock);

    /* Set the return value to the file descriptor */
    *retval = fd_index;

    /* Indicates no error */
    return 0;
}

/* Close system call */
int sys_close(int fd) {
    /* close() can be called from dup2(), so this indicates if it's a system
    call or called by a function to determine if the lock should be released */
    int function_call = 1;

    /* Check if the provided fd is valid */
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    /* Grab the index of the file/entry in the open file table*/
    int of_index = curproc->fd_table->of_index[fd];

    /* Check if the current process' file table contains the file */
    if (of_index < 0 || of_index >= OPEN_MAX) {
        return EBADF;
    }

    /* Check if the of_table lock is held, if so this is called by dup2 */
    if (!lock_do_i_hold(of_table->lock)) {
        function_call = 0;
        /* Acquire lock for exclusive access to the open file table */
        lock_acquire(of_table->lock);
    }

    /* Grab the file connected to the file descriptor */
    struct of_entry_t *of_entry = of_table->of_entries[of_index];
    if (of_entry == NULL) {
        lock_release(of_table->lock);
        return EBADF;
    }

    /* Remove the file from this process' fd table */
    curproc->fd_table->of_index[fd] = FILE_UNUSED;

    /* Check whether other process' are using the file */
    if (of_entry->ref_count > 1) {
        of_entry->ref_count--;
    }

    /* If this is the only reference, completely close the file */
    else {
        vfs_close(of_entry->vn);
        of_table->of_entries[of_index] = NULL;
        kfree(of_entry);
    }

    /* Release the lock (only if called by syscall) */
    if (!function_call) {
        /* Release lock for open file table */
        lock_release(of_table->lock);
    }

    return 0;
}

/* Read system call */
int sys_read(int fd, userptr_t buf, size_t buflen, int *retval) {
    int result;

    /* Check if the fd provided is valid */
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    /* Grab the index of the file/entry in the open file table */
    int of_index = curproc->fd_table->of_index[fd];
    /* Check if the current process' file table contains the file */
    if (of_index < 0 || of_index >= OPEN_MAX) {
        return EBADF;
    }

    /* Lock the open file table */
    lock_acquire(of_table->lock);

    /* Grab the file connected to the file descriptor */
    struct of_entry_t *of_entry = of_table->of_entries[of_index];
    if (of_entry == NULL) {
        lock_release(of_table->lock);
        return EBADF;
    }

    /* Check the access mode of the file is readable */
    if ((of_entry->mode & O_ACCMODE) == O_WRONLY) {
        lock_release(of_table->lock);
        return EBADF;
    }

    /* Initialize a uio to read, pointing at the buffer buf */
    struct iovec iov;
    struct uio u;
    uio_uinit(&iov, &u, buf, buflen, of_entry->offset, UIO_READ);

    /* Read from the vnode into the uio */
    result = VOP_READ(of_entry->vn, &u);
    if (result) {
        lock_release(of_table->lock);
        return result;
    }

    /* Set the return value to the number of bytes read */
    *retval = u.uio_offset - of_entry->offset;

    /* Update the pointer in the file */
    of_entry->offset = u.uio_offset;

    /* Release the lock from the oft */
    lock_release(of_table->lock);

    return 0;
}

/* Write system call */
int sys_write(int fd, userptr_t buf, size_t nbytes, int *retval) {
    int result;

    /* Check if the fd provided is valid */
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    /* Grab the index of the file/entry in the open file table */
    int of_index = curproc->fd_table->of_index[fd];
    /* Check if the current process' file table contains the file */
    if (of_index < 0 || of_index >= OPEN_MAX) {
        return EBADF;
    }

    /* Lock the open file table */
    lock_acquire(of_table->lock);

    /* Grab the file connected to the file descriptor */
    struct of_entry_t *of_entry = of_table->of_entries[of_index];
    if (of_entry == NULL) {
        lock_release(of_table->lock);
        return EBADF;
    }

    /* Check the access mode of the file is writable */
    if ((of_entry->mode & O_ACCMODE) == O_RDONLY) {
        lock_release(of_table->lock);
        return EBADF;
    }

    /* Initialize a uio to read, pointing at the buffer buf */
    struct iovec iov;
    struct uio u;
    uio_uinit(&iov, &u, buf, nbytes, of_entry->offset, UIO_WRITE);

    /* Write into the vnode from the uio */
    result = VOP_WRITE(of_entry->vn, &u);
    if (result) {
        lock_release(of_table->lock);
        return result;
    }

    /* Set the return value to the number of bytes written */
    *retval = u.uio_offset - of_entry->offset;

    /* Update the pointer in the file */
    of_entry->offset = u.uio_offset;

    /* Release the lock from the oft */
    lock_release(of_table->lock);

    return 0;
}

/* LSeek system call */
int sys_lseek(int fd, off_t pos, int whence, off_t *retval) {
    KASSERT(curthread != NULL);
    KASSERT(curproc != NULL);

    off_t new_offset = 0;
    struct stat st;
    int result;

    /* Check if the provided fd is valid */
    if (fd < 0 || fd >= OPEN_MAX) {
        return EBADF;
    }

    /* Grab the index of the file/entry in the open file table */
    int of_index = curproc->fd_table->of_index[fd];

    /* Check that the current process' file table contains the file */
    if (of_index < 0 || of_index >= OPEN_MAX) {
        return EBADF;
    }

    /* Acquire lock for exclusive access to the open file table */
    lock_acquire(of_table->lock);

    /* Grab the file connected to the file descriptor */
    struct of_entry_t *of_entry = of_table->of_entries[of_index];
    if (of_entry == NULL) {
        lock_release(of_table->lock);
        return EBADF;
    }

    /* Check if the file is a device (unseekable) */
    if (!VOP_ISSEEKABLE(of_entry->vn)) {
        /* fd refers to an object which does not support seeking. */
        lock_release(of_table->lock);
        return ESPIPE; /* Illegal seek */
    }

    switch(whence) {
        case SEEK_SET:
            new_offset = pos;
            break;

        case SEEK_CUR:
            new_offset = of_entry->offset + pos;
            break;

        case SEEK_END:
            result = VOP_STAT(of_entry->vn, &st);
            if (result) {
                lock_release(of_table->lock);
                return result;
            }
            new_offset = st.st_size + pos;
            break;

        default:
            /* whence is invalid. */
            lock_release(of_table->lock);
            return EINVAL; /* Invalid argument */
    }

    if (new_offset < 0) {
        /* The resulting seek position would be negative. */
        lock_release(of_table->lock);
        return EINVAL; /* Invalid argument */
    }

    /* update file offsets */
    of_entry->offset = new_offset;

    lock_release(of_table->lock);

    /* set return value to new offset */
    *retval = new_offset;

    return 0;
}

/* Dup2 system call */
int sys_dup2(int oldfd, int newfd, int *retval) {
    KASSERT(curthread != NULL);
    KASSERT(curproc != NULL);

    int result;

    /* ensure that both file descriptors are valid */
    if (oldfd < 0 || newfd < 0 || oldfd >= OPEN_MAX || newfd >= OPEN_MAX){
        return EBADF;
    }

    /* Check that both fds are different */
    if (oldfd == newfd) {
        *retval = newfd;
        return 0;
    }

    /* Grab the open file table indices from the fd table */
    int old_index = curproc->fd_table->of_index[oldfd];
    int new_index = curproc->fd_table->of_index[newfd];

    /* Check that the fd being duped is valid */
    if (old_index < 0 || old_index >= OPEN_MAX) {
        return EBADF;
    }

    /* Grab the lock for the global open file table */
    lock_acquire(of_table->lock);

    /* Close newfd (if it is currently open) */
    if (new_index != FILE_UNUSED) {
        result = sys_close(newfd);
        if (result) {
            lock_release(of_table->lock);
            return result;
        }
    }

    /* Set file descriptor entry to point to the same open file entry */
    curproc->fd_table->of_index[newfd] = curproc->fd_table->of_index[oldfd];

    /* Increment file entry reference count and vnode reference count */
    of_table->of_entries[old_index]->ref_count++;
    VOP_INCREF(of_table->of_entries[old_index]->vn);

    /* Release the of_table lock */
    lock_release(of_table->lock);

    *retval = newfd;    /* Set return value to new file descriptor*/
    return 0;   /* indicates no error */
}

//--------------------------------------------------------------------//

/* Create a table of file descriptors for each process */
int create_fd_table(void) {
    /* ensures that the global of table is initialized */
    create_of_table();

    int result, fd[2];


    /* Allocate memory for the fd table */
    curproc->fd_table = kmalloc(sizeof(struct fd_table_t));
    if (curproc->fd_table == NULL) {
        return ENOMEM;
    }

    /* Initialize all of the entries of the fd table */
    for (int i = 0; i < OPEN_MAX; i++) {
        curproc->fd_table->of_index[i] = FILE_UNUSED;
    }

    char fp0[] = "con:", fp1[] = "con:", fp2[] = "con:";
    /* Set up stdin file descriptor */
    /* stdin ends up being closed at the end as it was never intended to
    be left open, but I found this was the easiest way to have fd1 and fd2 to
    be set while leaving fd0 open (I was considering passing in the desired
    fd into the function, but the actual open syscall doesn't offer this) */
    result = sys_open((userptr_t) fp0, O_WRONLY, 0, &fd[0]);
    if (result) {
        kfree(curproc->fd_table);
        return result;
    }

    /* Set up stdout file descriptor */
    result = sys_open((userptr_t) fp1, O_WRONLY, 0, &fd[0]);
    if (result) {
        kfree(curproc->fd_table);
        return result;
    }

    /* Set up stderr file descriptor */
    result = sys_open((userptr_t) fp2, O_WRONLY, 0, &fd[1]);
    if (result) {
        kfree(curproc->fd_table);
        return result;
    }

    /* Close stdin (as above) */
    sys_close(0);

    return 0;
}

/* Closes all entries in a file descriptor table */
void close_fd_table(void) {
    for (int i = 0; i < OPEN_MAX; i++) {
        if (curproc->fd_table->of_index[i] != FILE_UNUSED) {
            sys_close(i);
        }
    }

    kfree(curproc->fd_table);
}

/* Create a global table of open file entries */
int create_of_table(void) {
    if (of_table != NULL) {
        return 0;
    }

    of_table = kmalloc(sizeof(struct of_table_t));
    if (of_table == NULL) {
        return ENOMEM;
    }

    /* Initialize all of the entries */
    for (int i = 0; i < OPEN_MAX; i++) {
        of_table->of_entries[i] = NULL;
    }

    /* Create the mutex lock for the open file table */
    struct lock *oft_lock = lock_create("table_lock_mutex");
    if (oft_lock == NULL) {
        return ENOMEM;
    }

    /* Assign the lock to the table */
    of_table->lock = oft_lock;

    return 0;
}

/* Destroy the global open file table  */
void close_of_table(void) {
    if (of_table != NULL) {
        kfree(of_table);
    }
}
