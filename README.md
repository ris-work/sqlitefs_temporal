# sqlite-fs

## About

sqlite-temporal-fs allows Linux, FreeBSD, NetBSD and MacOS to mount a sqlite database file as a temporal filesystem that you can reverse to any time in the past.

## Requirements

- Latest Rust Programming Language (≥ 1.38)
- libfuse(Linux) or osxfuse(MacOS) is requied by [fuse-rs](https://github.com/zargony/fuse-rs)

## Usage
### Mount a filesystem

```
$ sqlite-fs <mount_point> [<db_path>]
```

If a database file doesn't exist, sqlite-fs create db file and tables.

If a database file name isn't specified, sqlite-fs use in-memory-db instead of a file.
All data will be deleted when the filesystem is closed.

### Unmount a filesystem

- Linux

```
$ fusermount -u <mount_point>
```

- Mac

```
$ umount <mount_point>
```

## example
```
$ sqlite-fs ~/mount ~/filesystem.sqlite &
$ echo "Hello world\!" > ~/mount/hello.txt
$ cat ~/mount/hello.txt
Hello world!
```

## functions

- [x] Create/Read/Delete directories
- [x] Create/Read/Write/Delete files
- [x] Change attributions
- [x] Copy/Move files
- [x] Create Hard Link and Symbolic Link
- [x] Read/Write extended attributes
- [] File lock operations
- [] Strict error handling

### Tuneables

- [x] WAL mode
- [x] Syncing mode
- [x] No atime
- [x] MEMORY

### Temporal features  

- [x] Mount as at a particular time in the past  
- [] Re-genesis (garbage-collect from the past)  
- [x] Optional delta-diff or compression  

### Platforms  

- [x] Linux support  
- [x] FreeBSD support    
- [x] NetBSD support (if it doesn't work, manually patch the Rust FUSE client)  
- [] OpenBSD support
- [] Microsoft Windows (R) support
