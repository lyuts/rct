#include "MemoryMappedFile.h"

#include "Log.h"

#ifdef _WIN32
#  include <Windows.h>
#else
#  include "Rct.h"
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

MemoryMappedFile::MemoryMappedFile()
    : mpMapped(nullptr),
#ifdef _WIN32
      mhFile(INVALID_HANDLE_VALUE), mhFileMapping(INVALID_HANDLE_VALUE),
      mFileSize(0)
#else
      mFd(-1), mFileSize(0)
#endif
{
}

MemoryMappedFile::MemoryMappedFile(const Path &f_file, AccessType f_access,
                                   LockType f_lock)
    : mpMapped(nullptr),
#ifdef _WIN32
      mhFile(INVALID_HANDLE_VALUE), mhFileMapping(INVALID_HANDLE_VALUE),
      mFileSize(0)
#else
      mFd(-1), mFileSize(0)
#endif
{
    open(f_file, f_access, f_lock);
}

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& f_other)
{
    *this = std::move(f_other);
}

MemoryMappedFile &MemoryMappedFile::operator=(MemoryMappedFile &&f_other)
{
    mpMapped = f_other.mpMapped;
    f_other.mpMapped = nullptr;

    mFilename = f_other.mFilename;
    f_other.mFilename.clear();

#ifdef _WIN32
    mhFile = f_other.mhFile;
    f_other.mhFile = INVALID_HANDLE_VALUE;

    mhFileMapping = f_other.mhFileMapping;
    f_other.mhFileMapping = INVALID_HANDLE_VALUE;

    mFileSize = f_other.mFileSize;
    f_other.mFileSize = 0;
#else
    mFd = f_other.mFd;
    f_other.mFd = -1;

    mFileSize = f_other.mFileSize;
    f_other.mFileSize = 0;
#endif

    return *this;
}

MemoryMappedFile::~MemoryMappedFile()
{
    close();
}

std::size_t MemoryMappedFile::size() const
{
    return mFileSize;
}

bool MemoryMappedFile::open(const Path &f_filename, AccessType f_access,
                            LockType f_lock)
{
    if(isOpen()) close();
#ifdef _WIN32

    const DWORD access = (f_access == READ_ONLY) ?
        GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    const DWORD share = (f_lock == DO_LOCK) ?
        0 : (FILE_SHARE_READ | FILE_SHARE_WRITE);
    const DWORD protect = (f_access == READ_ONLY) ?
        PAGE_READONLY : PAGE_READWRITE;
    const DWORD desiredAccess = (f_access == READ_ONLY) ?
        FILE_MAP_READ : FILE_MAP_WRITE; // FILE_MAP_WRITE includes read access

    // first, we need to open the file:
    mhFile = CreateFile(f_filename.nullTerminated(),
                        access,
                        share,
                        NULL,
                        OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL,
                        NULL);

    if(mhFile == INVALID_HANDLE_VALUE)
    {
        error() << "Can't open file " << f_filename << ". "
                << "GetLastError(): " << GetLastError();
        return false;
    }

    mFileSize = GetFileSize(mhFile, NULL);

    // now, we can set up a file mapping:
    mhFileMapping = CreateFileMapping(mhFile,   // file to map
                                      NULL,     // security attrs
                                      protect,
                                      0, 0,     // use full file size
                                      NULL);    // name

    if(mhFileMapping == NULL)
    {
        error() << "Can't map file " << f_filename << ". "
                << "GetLastError(): " << GetLastError();
        close();
        return false;
    }

    // now we need to open a so-called "view" into the file mapping:
    mpMapped = MapViewOfFile(mhFileMapping, desiredAccess,
                             0,0,  // offset high and low
                             0);   // size


    if(mpMapped == NULL)
    {
        error() << "Can't map view of file " << f_filename << ". "
                << "GetLastError(): " << GetLastError();
        close();
        return false;
    }

    // everything worked out! We're done.
    mFilename = f_filename;
    return true;

#else
    // try to open the file
    const int openFlags = (f_access == READ_ONLY) ?
        O_RDONLY : O_RDWR;
    const int protFlags = (f_access == READ_ONLY) ?
        PROT_READ : (PROT_READ & PROT_WRITE);

    mFd = ::open(f_filename.nullTerminated(), openFlags);

    if(mFd == -1)
    {
        error() << "Could not open file " << f_filename
                << ". errno=" << errno;
        return false;
    }

    // TODO lock according to f_lock
    (void) f_lock;

    // get file size
    struct stat st;
    if(fstat(mFd, &st) != 0)
    {
        error() << "Could not stat file " << f_filename
                << ". errno=" << errno;
        close();
        return false;
    }

    mFileSize = st.st_size;   // size in byte

    // now, we can actually map the file
    mpMapped = mmap(
            NULL,       // destination hint
            mFileSize,
            protFlags,  // mmu page protection
            MAP_SHARED,
            mFd,
            0           // offset
        );

    if(mpMapped == MAP_FAILED)
    {
        mpMapped = nullptr;
        close();
        error() << "Could not map file " << f_filename
                << ". errno=" << errno;
        return false;
    }

    mFilename = f_filename;
    return true;
#endif
}

void MemoryMappedFile::close()
{
#ifdef _WIN32
    if(mpMapped && !UnmapViewOfFile(mpMapped))
    {
        error() << "Could not UnmapViewOfFile(). GetLastError()="
                << GetLastError();
    }
    closeHandleIfValid(mhFileMapping);
    closeHandleIfValid(mhFile);

#else  // ifdef _WIN32
    if(mpMapped != nullptr && munmap(mpMapped, mFileSize) != 0)
    {
        error() << "Could not unmap " << mFilename
                << ". errno=" << errno;
    }

    if(mFd != -1)
    {
        int ret;
        eintrwrap(ret, ::close(mFd));

        if(ret == -1)
        {
            error() << "Could not close file " << mFilename
                    << ". errno=" << errno;
        }
    }

#endif

    mFileSize = 0;
    mFilename.clear();
    mpMapped = nullptr;
}

#ifdef _WIN32
/* static */ void MemoryMappedFile::closeHandleIfValid(HANDLE &f_hdl)
{
    if(f_hdl == INVALID_HANDLE_VALUE) return;

    if(!CloseHandle(f_hdl))
    {
        error() << "Could not close handle! GetLastError()=" << GetLastError();
    }
    f_hdl = INVALID_HANDLE_VALUE;
}
#endif
