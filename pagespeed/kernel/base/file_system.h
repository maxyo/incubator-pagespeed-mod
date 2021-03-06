/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef PAGESPEED_KERNEL_BASE_FILE_SYSTEM_H_
#define PAGESPEED_KERNEL_BASE_FILE_SYSTEM_H_

#include <vector>

#include "pagespeed/kernel/base/basictypes.h"
#include "pagespeed/kernel/base/string.h"
#include "pagespeed/kernel/base/string_util.h"

namespace net_instaweb {

// Three-way return type for distinguishing Errors from boolean answer.
//
// This is physically just an enum, but is wrapped in a class to prevent
// accidental usage in an if- or ternary-condition without explicitly indicating
// whether you are looking for true, false, or error.
class BoolOrError {
  enum Choice { kIsFalse, kIsTrue, kIsError };

 public:
  BoolOrError() : choice_(kIsError) {}
  explicit BoolOrError(bool t_or_f) : choice_(t_or_f ? kIsTrue : kIsFalse) {}

  // Intended to be passed by value; explicitly support copy & assign
  BoolOrError(const BoolOrError& src) : choice_(src.choice_) {}
  BoolOrError& operator=(const BoolOrError& src) {
    if (&src != this) {
      choice_ = src.choice_;
    }
    return *this;
  }

  bool is_false() const { return choice_ == kIsFalse; }
  bool is_true() const { return choice_ == kIsTrue; }
  bool is_error() const { return choice_ == kIsError; }
  void set_error() { choice_ = kIsError; }
  void set(bool t_or_f) { choice_ = t_or_f ? kIsTrue : kIsFalse; }

 private:
  Choice choice_;
};

class MessageHandler;
class Timer;
class Writer;

// Provides abstract file system interface.  This isolation layer helps us:
//   - write unit tests that don't test the physical filesystem via a
//     MemFileSystem.
//   - Eases integration with Apache, which has its own file system interface,
//     and this class can help serve as the glue.
//   - provides a speculative conduit to a database so we can store resources
//     in a place where multiple Apache servers can see them.
class FileSystem {
 public:
  // This is documented as -1 in user-facing documentation, so don't change it.
  static const int64 kUnlimitedSize = -1;

  virtual ~FileSystem();

  class File {
   public:
    virtual ~File();

    // Gets the name of the file.
    virtual const char* filename() = 0;

   protected:
    // Use public interface provided by FileSystem::Close.
    friend class FileSystem;
    virtual bool Close(MessageHandler* handler) = 0;
  };

  class InputFile : public File {
   public:
    // Note: This returns num bytes read, NOT a success bool.
    virtual int Read(char* buf, int size, MessageHandler* handler) = 0;

    // Reads entire file into buf, returning true if successful.  Calling this
    // with max_file_size=kUnlimitedSize doesn't limit the read size, but it's
    // dangerous, since we can OOM if the file somehow ended up being much
    // larger than expected, so you should set a reasonable limit.
    virtual bool ReadFile(GoogleString* buf, int64 max_file_size,
                          MessageHandler* handler) = 0;

   protected:
    friend class FileSystem;
    ~InputFile() override;
  };

  class OutputFile : public File {
   public:
    // Note: Write is not atomic. If Write fails, there is no indication of how
    // much data has already been written to the file.
    virtual bool Write(const StringPiece& buf, MessageHandler* handler) = 0;
    virtual bool Flush(MessageHandler* handler) = 0;
    virtual bool SetWorldReadable(MessageHandler* handler) = 0;

   protected:
    friend class FileSystem;
    ~OutputFile() override;
  };

  class ProgressNotifier {
   public:
    virtual void Notify() = 0;
    virtual ~ProgressNotifier() {}
  };

  class NullProgressNotifier : public ProgressNotifier {
   public:
    void Notify() override {}
  };

  struct FileInfo {
    FileInfo(int64 size_bytes, int64 atime_sec, const GoogleString& name)
        : size_bytes(size_bytes), atime_sec(atime_sec), name(name) {}

    int64 size_bytes;
    int64 atime_sec;
    GoogleString name;
  };

  struct DirInfo {
    DirInfo() : size_bytes(0), inode_count(0) {}

    std::vector<FileInfo> files;
    StringVector empty_dirs;
    int64 size_bytes;
    int64 inode_count;
  };

  // Returns the maximum possible length of a path in a given directory.
  // Note that this is the total, and there may be further constraints
  // on each level. It also depends on the base path.
  //
  // Default implementation defensively returns 8192.
  virtual int MaxPathLength(const StringPiece& base) const;

  // High level support to read/write entire files in one shot.  The input_file
  // versions accept a NULL input_file, in which case they report failure.  If
  // the file is larget than max_file_size, return false.  All routines close
  // the file.
  virtual bool ReadFile(const char* filename, int64 max_file_size,
                        Writer* writer, MessageHandler* handler);
  virtual bool ReadFile(InputFile* input_file, int64 max_file_size,
                        Writer* writer, MessageHandler* handler);
  virtual bool ReadFile(const char* filename, int64 max_file_size,
                        GoogleString* buffer, MessageHandler* handler);
  virtual bool ReadFile(InputFile* input_file, int64 max_file_size,
                        GoogleString* buffer, MessageHandler* handler);
  // Deprecated versions of ReadFile, because they can OOM if the file they're
  // trying to read happens to be surprisingly large.  Instead, call ReadFile
  // with a limit.  If you can guarantee that you'll never encounter a large
  // file with this call, perhaps because you're reading a file you created,
  // then call ReadFile with an explicit limit of kUnlimitedSize.
  virtual bool ReadFile(const char* filename, GoogleString* buffer,
                        MessageHandler* handler);
  virtual bool ReadFile(InputFile* input_file, GoogleString* buffer,
                        MessageHandler* handler);
  virtual bool ReadFile(const char* filename, Writer* writer,
                        MessageHandler* handler);
  virtual bool ReadFile(InputFile* input_file, Writer* writer,
                        MessageHandler* handler);
  // Non-atomic. Use WriteFileAtomic() for atomic version.
  virtual bool WriteFile(const char* filename, const StringPiece& buffer,
                         MessageHandler* handler);
  // Writes given data to a temp file in one shot, storing the filename
  // in filename on success.  Returns false and clears filename on failure.
  virtual bool WriteTempFile(const StringPiece& prefix_name,
                             const StringPiece& buffer, GoogleString* filename,
                             MessageHandler* handler);

  // Write a temp file first and then copy to filename so that the file
  // cannot be read after being partially written.
  // Temp file name is based on filename.
  bool WriteFileAtomic(const StringPiece& filename, const StringPiece& buffer,
                       MessageHandler* handler);

  virtual InputFile* OpenInputFile(const char* filename,
                                   MessageHandler* handler) = 0;
  // Automatically creates sub-directories to filename.
  OutputFile* OpenOutputFile(const char* filename, MessageHandler* handler) {
    SetupFileDir(filename, handler);
    return OpenOutputFileHelper(filename, false, handler);
  }
  // Open a file to append to it.
  // Automatically creates sub-directories to filename.
  OutputFile* OpenOutputFileForAppend(const char* filename,
                                      MessageHandler* handler) {
    SetupFileDir(filename, handler);
    return OpenOutputFileHelper(filename, true, handler);
  }
  // Opens a temporary file to write, with the specified prefix.
  // If successful, the filename can be obtained from File::filename().
  // Automatically creates sub-directories to filename.
  //
  // NULL is returned on failure.
  OutputFile* OpenTempFile(const StringPiece& prefix_name,
                           MessageHandler* handler) {
    SetupFileDir(prefix_name, handler);
    return OpenTempFileHelper(prefix_name, handler);
  }

  // Closes the File and cleans up memory.
  virtual bool Close(File* file, MessageHandler* handler);

  // Like POSIX 'rm'.
  virtual bool RemoveFile(const char* filename, MessageHandler* handler) = 0;

  // Like POSIX 'mv', except it automatically creates sub-directories for
  // new_filename.
  bool RenameFile(const char* old_filename, const char* new_filename,
                  MessageHandler* handler) {
    SetupFileDir(new_filename, handler);
    return RenameFileHelper(old_filename, new_filename, handler);
  }

  // Like POSIX 'mkdir', makes a directory only if parent directory exists.
  // Fails if directory_name already exists or parent directory doesn't exist.
  virtual bool MakeDir(const char* directory_path, MessageHandler* handler) = 0;

  // Like POSIX 'rmdir', remove a directory only if it is empty.
  virtual bool RemoveDir(const char* directory_path,
                         MessageHandler* handler) = 0;

  // Like POSIX 'test -e', checks if path exists (is a file, directory, etc.).
  virtual BoolOrError Exists(const char* path, MessageHandler* handler) = 0;

  // Like POSIX 'test -d', checks if path exists and refers to a directory.
  virtual BoolOrError IsDir(const char* path, MessageHandler* handler) = 0;

  // Like POSIX 'mkdir -p', makes all directories up to this one recursively.
  // Fails if we do not have permission to make any directory in chain.
  virtual bool RecursivelyMakeDir(const StringPiece& directory_path,
                                  MessageHandler* handler);

  // Like POSIX 'ls -a', lists all files and directories under the given
  // directory (but omits "." and "..").  Full paths (not just filenames) will
  // be pushed onto the back of the supplied vector (without clearing it).
  // Returns true on success (even if the dir was empty), false on error (even
  // if some files were pushed onto the vector).  This is generally not
  // threadsafe!  Use a mutex.
  virtual bool ListContents(const StringPiece& dir, StringVector* files,
                            MessageHandler* handler) = 0;

  // Stores in *timestamp_sec the timestamp (in seconds since the
  // epoch) of the last time the file was accessed (through one of our
  // Read methods, or by someone else accessing the filesystem
  // directly).  Returns true on success, false on failure.
  // TODO(abliss): replace this with a single Stat() function.
  virtual bool Atime(const StringPiece& path, int64* timestamp_sec,
                     MessageHandler* handler) = 0;

  // Modified time. Time the file contents were modified.
  virtual bool Mtime(const StringPiece& path, int64* timestamp_sec,
                     MessageHandler* handler) = 0;

  // Given a directory path, list the files in the directory and all
  // subdirectories along with total size, inode count, and list of empty
  // directories (useful for cache cleaning). The files/directories in the
  // 'files' and 'empty_dirs' members of dirinfo will have the 'path' input
  // parameter prepended to them. We assume no circular links. If the files or
  // directories are modified while we traverse, we are not guaranteed to
  // represent their final state. The path name should NOT end in a "/".
  // TODO(abliss): unify all slash-ending assumptions
  void GetDirInfo(const StringPiece& path, DirInfo* dirinfo,
                  MessageHandler* handler);

  // Like GetDirInfo, but notifier->Notify() is called repeatedly as long as
  // GetDirInfo is making progress.
  virtual void GetDirInfoWithProgress(const StringPiece& path, DirInfo* dirinfo,
                                      ProgressNotifier* notifier,
                                      MessageHandler* handler);

  // Given a file, computes its size in bytes and store it in *size.  Returns
  // true on success, false on failure.  Behavior is undefined if path refers to
  // a directory.
  // This function has different behavior depending on the underlying
  // implementation. Memory-based implementations will report the size of the
  // file, while disk-based implementations should return the actual allocated
  // size on disk.
  // TODO(abliss): replace this with a single Stat() function.
  virtual bool Size(const StringPiece& path, int64* size,
                    MessageHandler* handler) const = 0;

  // Attempts to obtain a global (cross-process, cross-thread) lock of the given
  // name (which should be a valid filename, not otherwise used, in an extant
  // directory).  If someone else has this lock, returns False immediately.  If
  // anything goes wrong, returns Error.  On success, returns True: then you
  // must call Unlock when you are done.
  virtual BoolOrError TryLock(const StringPiece& lock_name,
                              MessageHandler* handler) = 0;

  // Like TryLock, but may attempt to break stale locks, though the default
  // implementation never actually breaks any.  A lock is stale if it was taken
  // (or last bumped) more than timeout_millis ms ago.
  //
  // If you obtain a lock through this method, there are no hard guarantees that
  // nobody else has it too.
  // <blink> If you use this function, your lock becomes "best-effort". </blink>
  //
  // If you override this function, you need to override BumpLockTimeout as
  // well.
  virtual BoolOrError TryLockWithTimeout(const StringPiece& lock_name,
                                         int64 timeout_millis,
                                         const Timer* timer,
                                         MessageHandler* handler) {
    return TryLock(lock_name, handler);
  }

  // If you're holding a lock for a long running task you want to avoid someone
  // else receiving the lock if they request it with TryLockWithTimeout because
  // you've been working for longer than the timeout, you should bump it often
  // enough that it doesn't expire.
  virtual bool BumpLockTimeout(const StringPiece& lock_name,
                               MessageHandler* handler) {
    // Default implementation does nothing, since the default implementation of
    // TryLockWithTimeout doesn't do anything either.
    return true;
  }

  // Attempts to release a lock previously obtained through TryLock.  If your
  // thread did not previously obtain the lock, the behavior is undefined.
  // Returns true if we successfully release the lock.  Returns false if we were
  // unable to release the lock (e.g. somebody came along and write-protected
  // the lockfile).  You might try again, or start using a different lock name.
  virtual bool Unlock(const StringPiece& lock_name,
                      MessageHandler* handler) = 0;

 protected:
  // These interfaces must be defined by implementers of FileSystem.
  // They may assume the directory already exists.
  virtual OutputFile* OpenOutputFileHelper(const char* filename, bool append,
                                           MessageHandler* handler) = 0;
  virtual OutputFile* OpenTempFileHelper(const StringPiece& filename,
                                         MessageHandler* handler) = 0;
  virtual bool RenameFileHelper(const char* old_filename,
                                const char* new_filename,
                                MessageHandler* handler) = 0;

 private:
  // RecursiveMakeDir the directory needed for filename.
  void SetupFileDir(const StringPiece& filename, MessageHandler* handler);
};

}  // namespace net_instaweb

#endif  // PAGESPEED_KERNEL_BASE_FILE_SYSTEM_H_
