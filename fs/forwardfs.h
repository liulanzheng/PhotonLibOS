#pragma once
#include "filesystem.h"

namespace FileSystem
{
    class ForwardFile : public IFile
    {
    protected:
        IFile* m_file;
        ForwardFile(IFile* file)
        {
            m_file = file;
        }
        virtual int close() override
        {
            return m_file->close();
        }
        virtual ssize_t pread(void *buf, size_t count, off_t offset) override
        {
            return m_file->pread(buf, count, offset);
        }
        virtual ssize_t pwrite(const void *buf, size_t count, off_t offset) override
        {
            return m_file->pwrite(buf, count, offset);
        }
        virtual ssize_t preadv(const struct iovec *iov, int iovcnt, off_t offset) override
        {
            return m_file->preadv(iov, iovcnt, offset);
        }
        virtual ssize_t preadv_mutable(struct iovec *iov, int iovcnt, off_t offset) override
        {
            return m_file->preadv_mutable(iov, iovcnt, offset);
        }
        virtual ssize_t preadv2(const struct iovec *iov, int iovcnt, off_t offset, int flags) override
        {
            return m_file->preadv2(iov, iovcnt, offset, flags);
        }
        virtual ssize_t preadv2_mutable(struct iovec *iov, int iovcnt, off_t offset, int flags) override
        {
            return m_file->preadv2_mutable(iov, iovcnt, offset, flags);
        }
        virtual ssize_t pwritev(const struct iovec *iov, int iovcnt, off_t offset) override
        {
            return m_file->pwritev(iov, iovcnt, offset);
        }
        virtual ssize_t pwritev_mutable(struct iovec *iov, int iovcnt, off_t offset) override
        {
            return m_file->pwritev_mutable(iov, iovcnt, offset);
        }
        virtual ssize_t pwritev2(const struct iovec *iov, int iovcnt, off_t offset, int flags) override
        {
            return m_file->pwritev2(iov, iovcnt, offset, flags);
        }
        virtual ssize_t pwritev2_mutable(struct iovec *iov, int iovcnt, off_t offset, int flags) override
        {
            return m_file->pwritev2_mutable(iov, iovcnt, offset, flags);
        }
        virtual ssize_t read(void *buf, size_t count) override
        {
            return m_file->read(buf, count);
        }
        virtual ssize_t readv(const struct iovec *iov, int iovcnt) override
        {
            return m_file->readv(iov, iovcnt);
        }
        virtual ssize_t readv_mutable(struct iovec *iov, int iovcnt) override
        {
            return m_file->readv_mutable(iov, iovcnt);
        }
        virtual ssize_t write(const void *buf, size_t count) override
        {
            return m_file->write(buf, count);
        }
        virtual ssize_t writev(const struct iovec *iov, int iovcnt) override
        {
            return m_file->writev(iov, iovcnt);
        }
        virtual ssize_t writev_mutable(struct iovec *iov, int iovcnt) override
        {
            return m_file->writev_mutable(iov, iovcnt);
        }
        virtual IFileSystem* filesystem() override
        {
            return m_file->filesystem();
        }
        virtual off_t lseek(off_t offset, int whence) override
        {
            return m_file->lseek(offset, whence);
        }
        virtual int fsync() override
        {
            return m_file->fsync();
        }
        virtual int fdatasync() override
        {
            return m_file->fdatasync();
        }
        virtual int fchmod(mode_t mode) override
        {
            return m_file->fchmod(mode);
        }
        virtual int fchown(uid_t owner, gid_t group) override
        {
            return m_file->fchown(owner, group);
        }
        virtual int fstat(struct stat *buf) override
        {
            return m_file->fstat(buf);
        }
        virtual int ftruncate(off_t length) override
        {
            return m_file->ftruncate(length);
        }
        virtual int sync_file_range(off_t offset, off_t nbytes, unsigned int flags) override
        {
            return m_file->sync_file_range(offset, nbytes, flags);
        }
        virtual ssize_t do_appendv(const struct iovec *iov, int iovcnt,
                                      off_t*  /*IN*/ offset = nullptr,
                                      off_t* /*OUT*/ position = nullptr) override
        {
            return m_file->do_appendv(iov, iovcnt, offset, position);
        }
        virtual int fallocate(int mode, off_t offset, off_t len) override
        {
            return m_file->fallocate(mode, offset, len);
        }
        virtual int fadvise(off_t offset, off_t len, int advice) override
        {
            return m_file->fadvise(offset, len, advice);
        }
        virtual int fiemap(struct fiemap* map) override
        {
            return m_file->fiemap(map);
        }
        virtual int vioctl(int request, va_list args) override
        {
            return m_file->vioctl(request, args);
        }
    };
    class ForwardFile_Ownership : public ForwardFile
    {
    protected:
        bool m_ownership;
        ForwardFile_Ownership(IFile* file, bool ownership) : ForwardFile(file)
        {
            m_ownership = ownership;
        }
        virtual ~ForwardFile_Ownership() override
        {
            if (m_ownership) delete m_file;
        }
        virtual int close() override
        {
            return m_ownership ? m_file->close() : 0;
        }
    };
    class ForwardFS : public IFileSystem
    {
    protected:
        IFileSystem* m_fs;
        ForwardFS(IFileSystem* fs)
        {
            m_fs = fs;
        }
        virtual IFile* open(const char *pathname, int flags) override
        {
            return m_fs->open(pathname, flags);
        }
        virtual IFile* open(const char *pathname, int flags, mode_t mode) override
        {
            return m_fs->open(pathname, flags, mode);
        }
        virtual IFile* creat(const char *pathname, mode_t mode) override
        {
            return m_fs->creat(pathname, mode);
        }
        virtual int mkdir (const char *pathname, mode_t mode) override
        {
            return m_fs->mkdir(pathname, mode);
        }
        virtual int rmdir(const char *pathname) override
        {
            return m_fs->rmdir(pathname);
        }
        virtual int symlink (const char *oldname, const char *newname) override
        {
            return m_fs->symlink(oldname, newname);
        }
        virtual ssize_t readlink(const char *pathname, char *buf, size_t bufsiz) override
        {
            return m_fs->readlink(pathname, buf, bufsiz);
        }
        virtual int link(const char *oldname, const char *newname) override
        {
            return m_fs->link(oldname, newname);
        }
        virtual int rename(const char *oldname, const char *newname) override
        {
            return m_fs->rename(oldname, newname);
        }
        virtual int unlink (const char *pathname) override
        {
            return m_fs->unlink(pathname);
        }
        virtual int chmod(const char *pathname, mode_t mode) override
        {
            return m_fs->chmod(pathname, mode);
        }
        virtual int chown(const char *pathname, uid_t owner, gid_t group) override
        {
            return m_fs->chown(pathname, owner, group);
        }
        virtual int lchown(const char *pathname, uid_t owner, gid_t group) override
        {
            return m_fs->lchown(pathname, owner, group);
        }
        virtual DIR* opendir(const char *pathname) override
        {
            return m_fs->opendir(pathname);
        }
        virtual int stat(const char *path, struct stat *buf) override
        {
            return m_fs->stat(path, buf);
        }
        virtual int lstat(const char *path, struct stat *buf) override
        {
            return m_fs->lstat(path, buf);
        }
        virtual int access(const char *path, int mode) override
        {
            return m_fs->access(path, mode);
        }
        virtual int truncate(const char *path, off_t length) override
        {
            return m_fs->truncate(path, length);
        }
        virtual int syncfs() override
        {
            return m_fs->syncfs();
        }
        virtual int statfs(const char *path, struct statfs *buf) override
        {
            return m_fs->statfs(path, buf);
        }
        virtual int statvfs(const char *path, struct statvfs *buf) override
        {
            return m_fs->statvfs(path, buf);
        }
        /*
        virtual ssize_t getxattr(const char *path, const char *name, void *value, size_t size) override
        {
            return m_fs->getxattr(path, name, value, size);
        }
        virtual ssize_t lgetxattr(const char *path, const char *name, void *value, size_t size) override
        {
            return m_fs->lgetxattr(path, name, value, size);
        }
        virtual ssize_t listxattr(const char *path, char *list, size_t size) override
        {
            return m_fs->listxattr(path, list, size);
        }
        virtual ssize_t llistxattr(const char *path, char *list, size_t size) override
        {
            return m_fs->llistxattr(path, list, size);
        }
        virtual int setxattr(const char *path, const char *name, const void *value, size_t size, int flags) override
        {
            return m_fs->setxattr(path, name, value, size, flags);
        }
        virtual int lsetxattr(const char *path, const char *name, const void *value, size_t size, int flags) override
        {
            return m_fs->lsetxattr(path, name, value, size, flags);
        }
        virtual int removexattr(const char *path, const char *name) override
        {
            return m_fs->removexattr(path, name);
        }
        virtual int lremovexattr(const char *path, const char *name) override
        {
            return m_fs->lremovexattr(path, name);
        }
        */
    };
    class ForwardFS_Ownership : public ForwardFS
    {
    protected:
        bool m_ownership;
        ForwardFS_Ownership(IFileSystem* fs, bool ownership) : ForwardFS(fs)
        {
            m_ownership = ownership;
        }
        virtual ~ForwardFS_Ownership() override
        {
            if (m_ownership) delete m_fs;
        }
    };
}
