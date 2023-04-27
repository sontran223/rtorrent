// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2005-2011, Jari Sundell <jaris@ifi.uio.no>

#include <fcntl.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <torrent/utils/error_number.h>
#include <torrent/utils/path.h>

#include "thread_base.h"

#include "rpc/exec_file.h"
#include "rpc/parse.h"
#ifdef __linux__
# include <sys/syscall.h>
#endif 

namespace rpc {
    
#ifdef __linux__

struct linux_dirent
{
	unsigned long d_ino;
	unsigned long d_off;
	unsigned short d_reclen;
	char d_name[1];
};

#define DIR_BUF_SIZE 1024

void close_all_fds()
{
	int dir_fd;
	char dir_buf[DIR_BUF_SIZE];
	struct linux_dirent *dir_entry;
	int r, pos;
	const char *s;
	int x;

	dir_fd = open("/proc/self/fd", O_RDONLY | O_DIRECTORY);
	if(dir_fd != -1)
	{
		for (;;)
		{
			r = syscall(SYS_getdents, dir_fd, dir_buf, DIR_BUF_SIZE);
			if(r <= 0)
			{
				break;
			}
			for (pos = 0; pos < r; pos += dir_entry->d_reclen)
			{
				dir_entry = (struct linux_dirent *) (dir_buf + pos);
				s = dir_entry->d_name;
				if (*s < '0' || *s > '9')
					continue;
				/* atoi is not guaranteed to be async-signal-safe.  */
				for (x = 0; *s >= '0' && *s <= '9'; s++)
				x = x * 10 + (*s - '0');
				if(!*s && x > 2 && x != dir_fd)
				{
					::close(x);
				}
			}
		}
		::close(dir_fd);
      }
}

#else

void close_all_fds()
{
	for (int i = 3, last = sysconf(_SC_OPEN_MAX); i != last; i++)
		::close(i);	
}

#endif

const unsigned int ExecFile::max_args;
const unsigned int ExecFile::buffer_size;

const int ExecFile::flag_expand_tilde;
const int ExecFile::flag_throw;
const int ExecFile::flag_capture;
const int ExecFile::flag_background;

// Close m_logFd.

int
ExecFile::execute(const char* file, char* const* argv, int flags) {
  ssize_t __attribute__((unused)) result;

  // Write the execued command and its parameters to the log fd.
  if (m_logFd != -1) {
    for (char* const* itr = argv; *itr != nullptr; itr++) {
      if (itr == argv)
        result = write(m_logFd, "\n---\n", sizeof("\n---\n"));
      else
        result = write(m_logFd, " ", 1);

      result = write(m_logFd, *itr, std::strlen(*itr));
    }

    result = write(m_logFd, "\n---\n", sizeof("\n---\n"));
  }

  int pipeFd[2];

  if ((flags & flag_capture) && pipe(pipeFd))
    throw torrent::input_error("ExecFile::execute(...) Pipe creation failed.");

  pid_t childPid = fork();

  if (childPid == -1)
    throw torrent::input_error("ExecFile::execute(...) Fork failed.");

  if (childPid == 0) {
    if (flags & flag_background) {
      pid_t detached_pid = fork();

      if (detached_pid == -1)
        _exit(-1);

      if (detached_pid != 0) {
        if (m_logFd != -1)
          result = write(m_logFd,
                         "\n--- Background task ---\n",
                         sizeof("\n--- Background task ---\n"));

        _exit(0);
      }

      m_logFd = -1;
      flags &= ~flag_capture;
    }

    int devNull = open("/dev/null", O_RDWR);
    if (devNull != -1)
      dup2(devNull, 0);
    else
      ::close(0);

    if (flags & flag_capture)
      dup2(pipeFd[1], 1);
    else if (m_logFd != -1)
      dup2(m_logFd, 1);
    else if (devNull != -1)
      dup2(devNull, 1);
    else
      ::close(1);

    if (m_logFd != -1)
      dup2(m_logFd, 2);
    else if (devNull != -1)
      dup2(devNull, 2);
    else
      ::close(2);

    // Close all fd's.
    close_all_fds();

    _exit(execvp(file, argv));
  }

  // We yield the global lock when waiting for the executed command to
  // finish so that XMLRPC and other threads can continue working.
  if (!(flags & flag_capture))
  {
    ThreadBase::release_global_lock();
  }

  if (flags & flag_capture) {
    m_capture = std::string();
    ::close(pipeFd[1]);

    char    buffer[4096];
    ssize_t length;

    do {
      length = read(pipeFd[0], buffer, sizeof(buffer));

      if (length > 0)
        m_capture += std::string(buffer, length);
    } while (length > 0);

    ::close(pipeFd[0]);

    if (m_logFd != -1) {
      result =
        write(m_logFd, "Captured output:\n", sizeof("Captured output:\n"));
      result = write(m_logFd, m_capture.data(), m_capture.length());
    }
  }

  int status;
  int wpid;

  do {
    wpid = waitpid(childPid, &status, 0);
  } while (wpid == -1 && torrent::utils::error_number::current().value() ==
                           std::errc::interrupted);
  if (!(flags & flag_capture))
  {
    ThreadBase::acquire_global_lock();
  }
  if (wpid != childPid)
    throw torrent::internal_error("ExecFile::execute(...) waitpid failed.");

  // Check return value?
  if (m_logFd != -1) {
    if (status == 0)
      result =
        write(m_logFd, "\n--- Success ---\n", sizeof("\n--- Success ---\n"));
    else
      result = write(m_logFd, "\n--- Error ---\n", sizeof("\n--- Error ---\n"));
  }

  return status;
}

torrent::Object
ExecFile::execute_object(const torrent::Object& rawArgs, int flags) {
  char*  argsBuffer[max_args];
  char** argsCurrent = argsBuffer;

  // Size of value strings are less than 24.
  char  valueBuffer[buffer_size];
  char* valueCurrent = valueBuffer;

  if (rawArgs.is_list()) {
    const torrent::Object::list_type& args = rawArgs.as_list();

    if (args.empty())
      throw torrent::input_error("Too few arguments.");

    for (torrent::Object::list_const_iterator itr  = args.begin(),
                                              last = args.end();
         itr != last;
         itr++, argsCurrent++) {
      if (argsCurrent == argsBuffer + max_args - 1)
        throw torrent::input_error("Too many arguments.");

      if (itr->is_string() &&
          (!(flags & flag_expand_tilde) || *itr->as_string().c_str() != '~')) {
        *argsCurrent = const_cast<char*>(itr->as_string().c_str());

      } else {
        *argsCurrent = valueCurrent;
        valueCurrent =
          print_object(valueCurrent, valueBuffer + buffer_size, &*itr, flags) +
          1;

        if (valueCurrent >= valueBuffer + buffer_size)
          throw torrent::input_error("Overflowed execute arg buffer.");
      }
    }

  } else {
    const torrent::Object::string_type& args = rawArgs.as_string();

    if ((flags & flag_expand_tilde) && args.c_str()[0] == '~') {
      *argsCurrent = valueCurrent;
      valueCurrent =
        print_object(valueCurrent, valueBuffer + buffer_size, &rawArgs, flags) +
        1;
    } else {
      *argsCurrent = const_cast<char*>(args.c_str());
    }

    argsCurrent++;
  }

  *argsCurrent = nullptr;

  int status = execute(argsBuffer[0], argsBuffer, flags);

  if ((flags & flag_throw) && status != 0)
    throw torrent::input_error("Bad return code.");

  if (flags & flag_capture)
    return m_capture;

  return torrent::Object((int64_t)status);
}

}
