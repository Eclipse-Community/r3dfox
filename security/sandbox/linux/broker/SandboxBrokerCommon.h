/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SandboxBrokerCommon_h
#define mozilla_SandboxBrokerCommon_h

#include <sys/types.h>
#include <stdint.h>

struct iovec;

// This file defines the protocol between the filesystem broker,
// described in SandboxBroker.h, and its client, described in
// ../SandboxBrokerClient.h; and it defines some utility functions
// used by both.
//
// In order to keep the client simple while allowing it to be thread
// safe and async signal safe, the main broker socket is used only for
// requests; responses arrive on a per-request socketpair sent with
// the request.  (This technique is also used by Chromium and Breakpad.)

namespace mozilla {

class SandboxBrokerCommon {
 public:
  enum class Operation : unsigned {
    SANDBOX_FILE_OPEN,
    SANDBOX_FILE_ACCESS,
    SANDBOX_FILE_STAT,
    SANDBOX_FILE_CHMOD,
    SANDBOX_FILE_LINK,
    SANDBOX_FILE_SYMLINK,
    SANDBOX_FILE_MKDIR,
    SANDBOX_FILE_RENAME,
    SANDBOX_FILE_RMDIR,
    SANDBOX_FILE_UNLINK,
    SANDBOX_FILE_READLINK,
    SANDBOX_SOCKET_CONNECT,
    SANDBOX_SOCKET_CONNECT_ABSTRACT,
    SANDBOX_OP_MAX_VALUE = SANDBOX_SOCKET_CONNECT_ABSTRACT
  };
#ifdef __cpp_using_enum
  using enum Operation;
#else
  // We can get rid of this once we desupport older compilers like GCC 10
  // (probably when we move to C++23: bug 1880762).
#  define USING_OP(NAME) static constexpr auto NAME = Operation::NAME
  USING_OP(SANDBOX_FILE_OPEN);
  USING_OP(SANDBOX_FILE_ACCESS);
  USING_OP(SANDBOX_FILE_STAT);
  USING_OP(SANDBOX_FILE_CHMOD);
  USING_OP(SANDBOX_FILE_LINK);
  USING_OP(SANDBOX_FILE_SYMLINK);
  USING_OP(SANDBOX_FILE_MKDIR);
  USING_OP(SANDBOX_FILE_RENAME);
  USING_OP(SANDBOX_FILE_RMDIR);
  USING_OP(SANDBOX_FILE_UNLINK);
  USING_OP(SANDBOX_FILE_READLINK);
  USING_OP(SANDBOX_SOCKET_CONNECT);
  USING_OP(SANDBOX_SOCKET_CONNECT_ABSTRACT);
  USING_OP(SANDBOX_OP_MAX_VALUE);
#  undef USING_OP
#endif

  static bool OperationIsValid(Operation aOp) {
    return static_cast<unsigned>(aOp) <=
           static_cast<unsigned>(SANDBOX_OP_MAX_VALUE);
  }

  static unsigned OperationToInt(Operation);
  static const char* OperationDescription(Operation);

  struct Request {
    Operation mOp;
    // For open, flags; for access, "mode"; for stat, O_NOFOLLOW for lstat.
    // For connect, the socket type.
    int mFlags;
    // ID to match child/parent requests in profiler
    uint64_t mId;
    // Size of return value buffer, if any
    size_t mBufSize;
    // The rest of the packet is the pathname.
    // SCM_RIGHTS for response socket attached.
  };

  struct Response {
    // Syscall result, -errno if failure, or 0 for no error
    int mError;
    // Followed by struct stat for stat/lstat.
    // SCM_RIGHTS attached for successful open.
  };

  // This doesn't need to be the system's maximum path length, just
  // the largest path that would be allowed by any policy.  (It's used
  // to size a stack-allocated buffer.)
  static const size_t kMaxPathLen = 4096;

  static ssize_t RecvWithFd(int aFd, const iovec* aIO, size_t aNumIO,
                            int* aPassedFdPtr);
  static ssize_t SendWithFd(int aFd, const iovec* aIO, size_t aNumIO,
                            int aPassedFd);
};

}  // namespace mozilla

#endif  // mozilla_SandboxBrokerCommon_h
