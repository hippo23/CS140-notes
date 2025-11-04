# Interlude: Files and Directories

- We add one more piece to the virtualisation trinity: **persistent storage**
- These store information permanently, and remain so even after power loss

## 1 Files and Directories

- Two abstractions we need to take note of:
  - **File** -> A linear array of bytes (although in POSIX standard, even devices can be files, but this definition should suffice for now)
    - For example, in xv6, we have its `inode` number. An `inode` doesn't contain the information itself, but instead, the `inode` has the address of blocks (directly and indirectly) and it is there that you will find all the blocks that belong to it.
  - **Directory** -> Think about how each `inode` has links, some are `T_DIR`, some are `T_FILE`. That is how we abstract into a directory. When finding a directory, we notice that the root process has a root directory of `/`. All other directors created by the user will be found here.
    - This kind of structure, wherein `T_DIR` and `T_FILE` are both `inodes`, is what allows us to create the abstraction of the filesystem.

## 2 The File System Interface

- Let's start with the creating, accessing, and deleting of files, such as with system calls like `unlink()`

## 3 Creating Files

- This can be done using the `open` system call and passing the `O_CREAT` flag.

```c
int fd = open("foo", O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR)
```

- We return a file descriptor (that is some integer in the `OFILE` per pocess in xv6 that corresponds to the current file I believe? I could be wrong). This is private per process.
- You can then use the file descriptor to make calls like `read()` and `write()`

```c
struct proc
{
  ...
  struct file *ofile[NOFILE];
  ...
}
```

## 5 Reading and Writing, But Not Sequentially

- All access so far has been **sequential**, but sometimes it is more useful to write to a specific location in the file.
- To accomplish this, we will use the `off_t lseek(int fildes, off_t offset, int whence)`
  - The first argument is just a file descriptor
  - The second argument is the offset within that file
  - The third argument, `whence`, determines how the seek is performed.
    - if `SEEK_SET`, the offset is set to offset bytes
    - if `SEEK_CUR`, the offset is set to its current location plus offset bytes
    - if `whence` is `SEEK_END`, the offset is set to the size of the file plus offset bytes.

```c
struct file
{
  int ref;
  char readable;
  char writable;
  struct inode *ip;
  uint off;
}
```

- The OS can use the abstraction or the `struct` of a file to see its permissions, its offset, what type it is, its corresponding `inode` or `dev` number, etc.
- All the currently open files are kept in the `ftable`

```c
struct
{
  struct spinlock lock;
  struct file file[NFILE];
} ftable;
```

## 6 Shared File Table Entries: `fork()` and `dup()`

- A lot of times, the file-descriptor to file mapping is one-to-one. However, there are some times where we want to have multiple access to that same file.
- More than just those two use cases, there are times when we want to share the reading or writing of a file (think the offset). This is one step further compared to simply having two independent channels that we use to transfer info through.
- You will notice that the behaviour of a parent and forked child follow this.
- When a file table entry is shared, its reference count is incremented (so we know when it is free).
  - Note that it is automatically shared because, in xv6, we do not copy the struct value, only the address, in the `OFILE` of the process. If the `child` process wants its own copy, it has to call `open` explicitly.
- We also have the `dup()` system call, which just copies the file under a new file descriptor
  - This is useful for shell programs needing to redirect output (**WHY?**)
  - **ANSWER**: It can assign a file to a given file descriptor, it basically shortcuts closing one file descriptor and opening a new one.

  ```c
  int main(int argc, char *argv[])
  {
    int fd = open("README", O_RDONLY);
    assert(fd >= 0);
    int fd2 = dup(fd);
    return 0;
  }
  ```

## 7 Writing Immediately With `fsync()`

- Typically (and as you should know if you have gone through the source code), we have a `log` buffer that buffers writes to persistent storage until some condition is met (in xv6, this is when there are no more active `log_<something here>` system calls open).
- However, some applications require more than this eventual guarantee. Sometimes, a correct recovery protocol needs the ability to force writes to disk from time to time.
- To support this, some file systems provide additional control APIs. In the UNIX case, this is known as `fsync()`

```c
int fd = open("foo", O_CREATE | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR)

assert(fd > -1);
int rc = write(fd, buffer, size);
assert(rc == size);
rc = fsync(fd);
assert(rc == 0);
```

- I imagine the `flush` system call works similar to this? This ensures that all the writes are forced to disk. `rc`, after the second assert, returns how much bytes were unable to be written?
- If we are newly creating the file, you often will need to run `fsync` both on the file and on the directory, to make sure that the file actually exists.

## 8 Renaming Files

- There is the `rename` system call, which, I imagine, just uses `namei` to retrieve the appropriate file or directory entry, and the abstraction `dirent` is what is important, we have to change the name there once we find it exists.
- `rename` is atomic with respect to system crashes, either it is fully renamed or it isn't at all.

```c
int fd = open("foo.txt.tmp", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
write(fd, buffer, size);
fsync(fd);
close(fd);
rename("foo.txt.tmp", "foo.text");
```

- This is how a file editor (like the one we are using right now) would function. It creates a temporary file to store the data, then writes or renames that file so that the original file has its information plus what you added.
- Because it is atomic, this guarantees that the file update itself is atomic (there is no copying over of actual texts, we delete the old file and save a new one).

## 9 Getting Information About Files

- To get the details of a file, we can use the `stat()` or `fstat()` system call.
- These take a pathname (or file descriptor) and fill in a `struct stat` as seen below.

```c
struct stat {
  dev_t st_dev; // ID of device containing file
  ino_t st_ino; // inode number
  mode_t st_mode; // protection
  nlink_t st_nlink; // number of hard links
  uid_t st_uid; // user ID of owner
  gid_t st_gid; // group ID of owner
  dev_t st_rdev; // device ID (if special file)
  off_t st_size; // total size, in bytes
  blksize_t st_blksize; // blocksize for filesystem I/O
  blkcnt_t st_blocks; // number of blocks allocated
  time_t st_atime; // time of last access
  time_t st_mtime; // time of last modification
  time_t st_ctime; // time of last status change
};
```

## 10 Removing Files

- We just use the `unlink` system call. The naming is because when we 'delete' a file, we don't actually delete it. Not yet at least.
- We delete the fact that its parent has it as its child or entry, and remove one reference to the file or directory that we are deleting. This is the `nlink` of an `inode`.
  - When we unlink a file, we actually remove a link from both the parent `dirent` and the child `dirent`. This is because the child contains a reference to the parent in the form of `..`, and the parent obviously contains the child.

## 11 Making Directories

- Note you can never write to a directory directly.
- Because the format of the directory is considered file system metadata, the file system considers itself responsible for the integrity of directory data
- Thus, you can only update a directory indirectly by, for example, creating files, directories, or other object types within it.

## 12 Reading Directories

```c
int main(int argc, char *argv[])
{
  // open the directory
  // in xv6, this would just be open
  // since both files and directories
  // are extracted into, well, a file
  DIR *dp = opendir(".");
  assert(dp != NULL);
  // dirent, like in xv6
  struct dirent *d;
  // reaerrerate through each directory entry, I assume
  // that readdir moves to the next directory entry
  while ((d = readdir(dp)) != NULL)
  {
    printf("%lu %s\n", (unsigned long) d->d_ino, d->d_name);
  }
  closedir(dp);
  return 0;
}
```

- The information below shows the information available with each `dirent`

```c
struct dirent {
  char d_name[256]; // filename
  ino_t d_ino; // inode number
  off_t d_off; // offset to the next dirent
  unsigned short d_reclen; // length of this record
  unsigned char d_type; // type of file
};
```

## 13 Deleting Directories

- Common sense

## 14 Hard Links

- We find out why a file is removed using `unlink()`, by understanding a new way to make an entry in the file system tree, through a system call known as `link()`
- Linking a new name to an old name is creating another way to reference the file
- We can unlink the file to remove that respective entry from the directory. Only when a file has no links to any directory will it be deleted

## 15 Symbolic Links

- Another type of link is the **symbolic** or **soft** link
- Hard links can't exist for a directory (because you can create a cycle in the directory by linking this directory to its parent), and also to an entry in a different persistent storage device, as `inode` numbers are only unique per-device
- **Symbolic links** are actually a file in and of itself, just one of a different type
  - The symbolic link will simply be a file containing a path to the actual file it is linked to
  - They leave the possibility for what is known as a **dangling reference**
  - Deleting a file without deleting the symbolic link means that the path in the symbolic link points to nothing

## 16 Permission Bits and Access Control Lists

- The abstraction of the persistent storage is different in some regards to that of the CPU and memory
- Files are commonly shared among different users and processes and are not (always) private. Thus, we need the use of **permission bits** to satisfy these new set of requirements

```sh
ls -l foo.txt
  -rw-r--r-- 1 remzi wheel 0 Aug 24 16:29 foo.txt
```

- The permissions are split into three groups, what the **owner** of the file can do, what someone in a **group** can do, and what anyone (the **others**) can do
- `chmod` is what is used to change these permissions
  - The execute bit, for files, determine if a program can actual run it or execute it.
  - The execute bit, for directories, actually tells us whether or not a user, group, or everyone can change directories into the given directory, and, in combination with the writable bit, create files therein
