#include <stdint.h>
#include "../git-compat-util.h"

unsigned int _CRT_fmode = _O_BINARY;

static inline time_t filetime_to_time_t(const FILETIME *ft)
{
	long long winTime = ((long long)ft->dwHighDateTime << 32) + ft->dwLowDateTime;
	winTime -= 116444736000000000LL; /* Windows to Unix Epoch conversion */
	winTime /= 10000000;		 /* Nano to seconds resolution */
	return (time_t)winTime;
}

static inline size_t size_to_blocks(size_t s)
{
	return (s+511)/512;
}

extern int _getdrive( void );
/* We keep the do_lstat code in a separate function to avoid recursion.
 * When a path ends with a slash, the stat will fail with ENOENT. In
 * this case, we strip the trailing slashes and stat again.
 */
static int do_lstat(const char *file_name, struct stat *buf)
{
	WIN32_FILE_ATTRIBUTE_DATA fdata;

	if (GetFileAttributesExA(file_name, GetFileExInfoStandard, &fdata)) {
		int fMode = S_IREAD;
		if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			fMode |= S_IFDIR;
		else
			fMode |= S_IFREG;
		if (!(fdata.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
			fMode |= S_IWRITE;

		buf->st_ino = 0;
		buf->st_gid = 0;
		buf->st_uid = 0;
		buf->st_mode = fMode;
		buf->st_size = fdata.nFileSizeLow; /* Can't use nFileSizeHigh, since it's not a stat64 */
		buf->st_blocks = size_to_blocks(buf->st_size);
		buf->st_dev = _getdrive() - 1;
		buf->st_atime = filetime_to_time_t(&(fdata.ftLastAccessTime));
		buf->st_mtime = filetime_to_time_t(&(fdata.ftLastWriteTime));
		buf->st_ctime = filetime_to_time_t(&(fdata.ftCreationTime));
		errno = 0;
		return 0;
	}

	switch (GetLastError()) {
	case ERROR_ACCESS_DENIED:
	case ERROR_SHARING_VIOLATION:
	case ERROR_LOCK_VIOLATION:
	case ERROR_SHARING_BUFFER_EXCEEDED:
		errno = EACCES;
		break;
	case ERROR_BUFFER_OVERFLOW:
		errno = ENAMETOOLONG;
		break;
	case ERROR_NOT_ENOUGH_MEMORY:
		errno = ENOMEM;
		break;
	default:
		errno = ENOENT;
		break;
	}
	return -1;
}

/* We provide our own lstat/fstat functions, since the provided
 * lstat/fstat functions are so slow. These stat functions are
 * tailored for Git's usage (read: fast), and are not meant to be
 * complete. Note that Git stat()s are redirected to mingw_lstat()
 * too, since Windows doesn't really handle symlinks that well.
 */
int mingw_lstat(const char *file_name, struct mingw_stat *buf)
{
	int namelen;
	static char alt_name[PATH_MAX];

	if (!do_lstat(file_name, buf))
		return 0;

	/* if file_name ended in a '/', Windows returned ENOENT;
	 * try again without trailing slashes
	 */
	if (errno != ENOENT)
		return -1;

	namelen = strlen(file_name);
	if (namelen && file_name[namelen-1] != '/')
		return -1;
	while (namelen && file_name[namelen-1] == '/')
		--namelen;
	if (!namelen || namelen >= PATH_MAX)
		return -1;

	memcpy(alt_name, file_name, namelen);
	alt_name[namelen] = 0;
	return do_lstat(alt_name, buf);
}

#undef fstat
#undef stat
int mingw_fstat(int fd, struct mingw_stat *buf)
{
	HANDLE fh = (HANDLE)_get_osfhandle(fd);
	BY_HANDLE_FILE_INFORMATION fdata;

	if (fh == INVALID_HANDLE_VALUE) {
		errno = EBADF;
		return -1;
	}
	/* direct non-file handles to MS's fstat() */
	if (GetFileType(fh) != FILE_TYPE_DISK) {
		struct stat st;
		if (fstat(fd, &st))
			return -1;
		buf->st_ino = st.st_ino;
		buf->st_gid = st.st_gid;
		buf->st_uid = st.st_uid;
		buf->st_mode = st.st_mode;
		buf->st_size = st.st_size;
		buf->st_blocks = size_to_blocks(buf->st_size);
		buf->st_dev = st.st_dev;
		buf->st_atime = st.st_atime;
		buf->st_mtime = st.st_mtime;
		buf->st_ctime = st.st_ctime;
		return 0;
	}

	if (GetFileInformationByHandle(fh, &fdata)) {
		int fMode = S_IREAD;
		if (fdata.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			fMode |= S_IFDIR;
		else
			fMode |= S_IFREG;
		if (!(fdata.dwFileAttributes & FILE_ATTRIBUTE_READONLY))
			fMode |= S_IWRITE;

		buf->st_ino = 0;
		buf->st_gid = 0;
		buf->st_uid = 0;
		buf->st_mode = fMode;
		buf->st_size = fdata.nFileSizeLow; /* Can't use nFileSizeHigh, since it's not a stat64 */
		buf->st_blocks = size_to_blocks(buf->st_size);
		buf->st_dev = _getdrive() - 1;
		buf->st_atime = filetime_to_time_t(&(fdata.ftLastAccessTime));
		buf->st_mtime = filetime_to_time_t(&(fdata.ftLastWriteTime));
		buf->st_ctime = filetime_to_time_t(&(fdata.ftCreationTime));
		return 0;
	}
	errno = EBADF;
	return -1;
}

unsigned int sleep (unsigned int __seconds)
{
	Sleep(__seconds*1000);
	return 0;
}
const char *inet_ntop(int af, const void *src,
                             char *dst, size_t cnt)
{
	return NULL;
}
int mkstemp (char *__template)
{
	char *filename = mktemp(__template);
	if (filename == NULL)
		return -1;
	return open(filename, O_RDWR | O_CREAT, 0600);
}

int gettimeofday(struct timeval *tv, void *tz)
{
	extern time_t my_mktime(struct tm *tm);
	SYSTEMTIME st;
	struct tm tm;
	GetSystemTime(&st);
	tm.tm_year = st.wYear-1900;
	tm.tm_mon = st.wMonth-1;
	tm.tm_mday = st.wDay;
	tm.tm_hour = st.wHour;
	tm.tm_min = st.wMinute;
	tm.tm_sec = st.wSecond;
	tv->tv_sec = my_mktime(&tm);
	if (tv->tv_sec < 0)
		return -1;
	tv->tv_usec = st.wMilliseconds*1000;
	return 0;
}

int pipe(int filedes[2])
{
	int fd;
	HANDLE h[2], parent;

	if (_pipe(filedes, 8192, 0) < 0)
		return -1;

	parent = GetCurrentProcess();

	if (!DuplicateHandle (parent, (HANDLE)_get_osfhandle(filedes[0]),
			parent, &h[0], 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		close(filedes[0]);
		close(filedes[1]);
		return -1;
	}
	if (!DuplicateHandle (parent, (HANDLE)_get_osfhandle(filedes[1]),
			parent, &h[1], 0, FALSE, DUPLICATE_SAME_ACCESS)) {
		close(filedes[0]);
		close(filedes[1]);
		CloseHandle(h[0]);
		return -1;
	}
	fd = _open_osfhandle(h[0], O_NOINHERIT);
	if (fd < 0) {
		close(filedes[0]);
		close(filedes[1]);
		CloseHandle(h[0]);
		CloseHandle(h[1]);
		return -1;
	}
	close(filedes[0]);
	filedes[0] = fd;
	fd = _open_osfhandle(h[1], O_NOINHERIT);
	if (fd < 0) {
		close(filedes[0]);
		close(filedes[1]);
		CloseHandle(h[1]);
		return -1;
	}
	close(filedes[1]);
	filedes[1] = fd;
	return 0;
}

int poll(struct pollfd *ufds, unsigned int nfds, int timeout)
{
	return -1;
}

#include <time.h>

struct tm *gmtime_r(const time_t *timep, struct tm *result)
{
	memcpy(result, gmtime(timep), sizeof(struct tm));
	return result;
}

struct tm *localtime_r(const time_t *timep, struct tm *result)
{
	memcpy(result, localtime(timep), sizeof(struct tm));
	return result;
}

#undef getcwd
char *mingw_getcwd(char *pointer, int len)
{
	char *ret = getcwd(pointer, len);
	if (!ret)
		return ret;
	if (pointer[0] != 0 && pointer[1] == ':') {
		int i;
		for (i = 2; pointer[i]; i++)
			/* Thanks, Bill. You'll burn in hell for that. */
			if (pointer[i] == '\\')
				pointer[i] = '/';
	}
	return ret;
}

void openlog(const char *ident, int option, int facility)
{
}

/* See http://msdn2.microsoft.com/en-us/library/17w5ykft(vs.71).aspx (Parsing C++ Command-Line Arguments */
static const char *quote_arg(const char *arg)
{
	/* count chars to quote */
	int len = 0, n = 0;
	int force_quotes = 0;
	char *q, *d;
	const char *p = arg;
	if (!*p) force_quotes = 1;
	while (*p) {
		if (isspace(*p) || *p == '*' || *p == '?')
			force_quotes = 1;
		else if (*p == '"')
			n++;
		else if (*p == '\\') {
			int count = 0;
			while (*p == '\\') {
				count++;
				p++;
				len++;
			}
			if (*p == '"')
				n += count*2 + 1;
			continue;
		}
		len++;
		p++;
	}
	if (!force_quotes && n == 0)
		return arg;

	/* insert \ where necessary */
	d = q = xmalloc(len+n+3);
	*d++ = '"';
	while (*arg) {
		if (*arg == '"')
			*d++ = '\\';
		else if (*arg == '\\') {
			int count = 0;
			while (*arg == '\\') {
				count++;
				*d++ = *arg++;
			}
			if (*arg == '"') {
				while (count-- > 0)
					*d++ = '\\';
				*d++ = '\\';
			}
		}
		*d++ = *arg++;
	}
	*d++ = '"';
	*d++ = 0;
	return q;
}

void quote_argv(const char **dst, const char **src)
{
	while (*src)
		*dst++ = quote_arg(*src++);
	*dst = NULL;
}

const char *parse_interpreter(const char *cmd)
{
	static char buf[100];
	char *p, *opt;
	int n, fd;

	/* don't even try a .exe */
	n = strlen(cmd);
	if (n >= 4 && !strcasecmp(cmd+n-4, ".exe"))
		return NULL;

	fd = open(cmd, O_RDONLY);
	if (fd < 0)
		return NULL;
	n = read(fd, buf, sizeof(buf)-1);
	close(fd);
	if (n < 4)	/* at least '#!/x' and not error */
		return NULL;

	if (buf[0] != '#' || buf[1] != '!')
		return NULL;
	buf[n] = '\0';
	p = strchr(buf, '\n');
	if (!p)
		return NULL;

	*p = '\0';
	if (!(p = strrchr(buf+2, '/')) && !(p = strrchr(buf+2, '\\')))
		return NULL;
	/* strip options */
	if ((opt = strchr(p+1, ' ')))
		*opt = '\0';
	return p+1;
}

static int try_shell_exec(const char *cmd, const char **argv, const char **env)
{
	const char **sh_argv;
	int n;
	const char *interpr = parse_interpreter(cmd);
	if (!interpr)
		return 0;

	/*
	 * expand
	 *    git-foo args...
	 * into
	 *    sh git-foo args...
	 */
	for (n = 0; argv[n];) n++;
	sh_argv = xmalloc((n+2)*sizeof(char*));
	sh_argv[0] = interpr;
	sh_argv[1] = quote_arg(cmd);
	quote_argv(&sh_argv[2], &argv[1]);
	n = spawnvpe(_P_WAIT, interpr, sh_argv, env);
	if (n == -1)
		return 1;	/* indicate that we tried but failed */
	exit(n);
}

void mingw_execve(const char *cmd, const char **argv, const char **env)
{
	/* check if git_command is a shell script */
	if (!try_shell_exec(cmd, argv, env)) {
		const char **qargv;
		int n;
		for (n = 0; argv[n];) n++;
		qargv = xmalloc((n+1)*sizeof(char*));
		quote_argv(qargv, argv);
		int ret = spawnve(_P_WAIT, cmd, qargv, env);
		if (ret != -1)
			exit(ret);
	}
}

static char *lookup_prog(const char *dir, const char *cmd, int tryexe)
{
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s/%s.exe", dir, cmd);

	if (tryexe && access(path, 0) == 0)
		return xstrdup(path);
	path[strlen(path)-4] = '\0';
	if (access(path, 0) == 0)
		return xstrdup(path);
	return NULL;
}

/*
 * Determines the absolute path of cmd using the the split path in path.
 * If cmd contains a slash or backslash, no lookup is performed.
 */
char *mingw_path_lookup(const char *cmd, char **path)
{
	char **p = path;
	char *prog = NULL;
	int len = strlen(cmd);
	int tryexe = len < 4 || strcasecmp(cmd+len-4, ".exe");

	if (strchr(cmd, '/') || strchr(cmd, '\\'))
		prog = xstrdup(cmd);

	while (!prog && *p) {
		prog = lookup_prog(*p++, cmd, tryexe);
	}
	if (!prog) {
		prog = lookup_prog(".", cmd, tryexe);
		if (!prog)
			prog = xstrdup(cmd);
	}
	return prog;
}

/*
 * Splits the PATH into parts.
 */
char **mingw_get_path_split(void)
{
	char *p, **path, *envpath = getenv("PATH");
	int i, n = 0;

	if (!envpath || !*envpath)
		return NULL;

	envpath = xstrdup(envpath);
	p = envpath;
	while (p) {
		char *dir = p;
		p = strchr(p, ';');
		if (p) *p++ = '\0';
		if (*dir) {	/* not earlier, catches series of ; */
			++n;
		}
	}
	if (!n)
		return NULL;

	path = xmalloc((n+1)*sizeof(char*));
	p = envpath;
	i = 0;
	do {
		if (*p)
			path[i++] = xstrdup(p);
		p = p+strlen(p)+1;
	} while (i < n);
	path[i] = NULL;

	free(envpath);

	return path;
}

void mingw_free_path_split(char **path)
{
	if (!path)
		return;

	char **p = path;
	while (*p)
		free(*p++);
	free(path);
}

void mingw_execvp(const char *cmd, const char **argv)
{
	char **path = mingw_get_path_split();
	char *prog = mingw_path_lookup(cmd, path);

	if (prog) {
		mingw_execve(prog, argv, (const char **) environ);
		free(prog);
	} else
		errno = ENOENT;

	mingw_free_path_split(path);
}

int mingw_socket(int domain, int type, int protocol)
{
	SOCKET s = WSASocket(domain, type, protocol, NULL, 0, 0);
	if (s == INVALID_SOCKET) {
		/*
		 * WSAGetLastError() values are regular BSD error codes
		 * biased by WSABASEERR.
		 * However, strerror() does not know about networking
		 * specific errors, which are values beginning at 38 or so.
		 * Therefore, we choose to leave the biased error code
		 * in errno so that _if_ someone looks up the code somewhere,
		 * then it is at least the number that are usually listed.
		 */
		errno = WSAGetLastError();
		return -1;
	}
	return s;
}

#undef rename
int mingw_rename(const char *pold, const char *pnew)
{
	/*
	 * Try native rename() first to get errno right.
	 * It is based on MoveFile(), which cannot overwrite existing files.
	 */
	if (!rename(pold, pnew))
		return 0;
	if (errno != EEXIST)
		return -1;
	if (MoveFileEx(pold, pnew, MOVEFILE_REPLACE_EXISTING))
		return 0;
	/* TODO: translate more errors */
	if (GetLastError() == ERROR_ACCESS_DENIED) {
		DWORD attrs = GetFileAttributes(pnew);
		if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
			errno = EISDIR;
			return -1;
		}
	}
	errno = EACCES;
	return -1;
}

#undef vsnprintf
/* Note that the size parameter specifies the available space, i.e.
 * includes the trailing NUL byte; but Windows's vsnprintf expects the
 * number of characters to write without the trailing NUL.
 */

/* This is out of line because it uses alloca() behind the scenes,
 * which must not be called in a loop (alloca() reclaims the allocations
 * only at function exit).
 */
static int try_vsnprintf(size_t size, const char *fmt, va_list args)
{
	char buf[size];	/* gcc-ism */
	return vsnprintf(buf, size-1, fmt, args);
}

int mingw_vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
	int len;
	if (size > 0) {
		len = vsnprintf(buf, size-1, fmt, args);
		if (len >= 0)
			return len;
	}
	/* ouch, buffer too small; need to compute the size */
	if (size < 250)
		size = 250;
	do {
		size *= 4;
		len = try_vsnprintf(size, fmt, args);
	} while (len < 0);
	return len;
}

struct passwd *mingw_getpwuid(int uid)
{
	static char user_name[100];
	static struct passwd p;

	DWORD len = sizeof(user_name);
	if (!GetUserName(user_name, &len))
		return NULL;
	p.pw_name = user_name;
	p.pw_gecos = "unknown";
	p.pw_dir = NULL;
	return &p;
}


static HANDLE timer_event;
static HANDLE timer_thread;
static int timer_interval;
static int one_shot;
static sig_handler_t timer_fn = SIG_DFL;

/* The timer works like this:
 * The thread, ticktack(), is basically a trivial routine that most of the
 * time only waits to receive the signal to terminate. The main thread
 * tells the thread to terminate by setting the timer_event to the signalled
 * state.
 * But ticktack() does not wait indefinitely; instead, it interrupts the
 * wait state every now and then, namely exactly after timer's interval
 * length. At these opportunities it calls the signal handler.
 */
 
static __stdcall unsigned ticktack(void *dummy)
{
	while (WaitForSingleObject(timer_event, timer_interval) == WAIT_TIMEOUT) {
		if (timer_fn == SIG_DFL)
			die("Alarm");
		if (timer_fn != SIG_IGN)
			timer_fn(SIGALRM);
		if (one_shot)
			break;
	}
	return 0;
}

static int start_timer_thread(void)
{
	timer_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (timer_event) {
		timer_thread = (HANDLE) _beginthreadex(NULL, 0, ticktack, NULL, 0, NULL);
		if (!timer_thread )
			return errno = ENOMEM,
				error("cannot create progress indicator");
	} else
		return errno = ENOMEM,
			error("cannot allocate resources for progress indicator");
	return 0;
}

static void stop_timer_thread(void)
{
	if (timer_event)
		SetEvent(timer_event);	/* tell thread to terminate */
	if (timer_thread) {
		int rc = WaitForSingleObject(timer_thread, 1000);
		if (rc == WAIT_TIMEOUT)
			error("timer thread did not terminate timely");
		else if (rc != WAIT_OBJECT_0)
			error("waiting for timer thread failed: %lu",
			      GetLastError());
		CloseHandle(timer_thread);
	}
	if (timer_event)
		CloseHandle(timer_event);
	timer_event = NULL;
	timer_thread = NULL;
}

static inline int is_timeval_eq(const struct timeval *i1, const struct timeval *i2)
{
	return i1->tv_sec == i2->tv_sec && i1->tv_usec == i2->tv_usec;
}

int setitimer(int type, struct itimerval *in, struct itimerval *out)
{
	static const struct timeval zero;
	static int atexit_done;

	if (out != NULL)
		return errno = EINVAL,
			error("setitmer param 3 != NULL not implemented");
	if (!is_timeval_eq(&in->it_interval, &zero) &&
	    !is_timeval_eq(&in->it_interval, &in->it_value))
		return errno = EINVAL,
			error("setitmer: it_interval must be zero or eq it_value");

	if (timer_thread)
		stop_timer_thread();

	if (is_timeval_eq(&in->it_value, &zero) &&
	    is_timeval_eq(&in->it_interval, &zero))
		return 0;

	timer_interval = in->it_interval.tv_sec * 1000 + in->it_interval.tv_usec / 1000;
	one_shot = is_timeval_eq(&in->it_value, &zero);
	if (!atexit_done) {
		atexit(stop_timer_thread);
		atexit_done = 1;
	}
	return start_timer_thread();
}

int sigaction(int sig, struct sigaction *in, struct sigaction *out)
{
	if (sig != SIGALRM)
		return errno = EINVAL,
			error("sigaction only implemented for SIGALRM");
	if (out != NULL)
		return errno = EINVAL,
			error("sigaction: param 3 != NULL not implemented");

	timer_fn = in->sa_handler;
	return 0;
}

#undef signal
sig_handler_t mingw_signal(int sig, sig_handler_t handler)
{
	if (sig != SIGALRM)
		return signal(sig, handler);
	sig_handler_t old = timer_fn;
	timer_fn = handler;
	return old;
}
