/*
 * kunzite-agent: a small HTTP/1.0 service for the iOS port, reached over the
 * USB Ethernet link (phone 192.168.7.2, Mac 192.168.7.1; kernel patch 0909).
 * Stock iOS has no shell, so the agent itself provides what bring-up needs:
 *
 *   GET  /                      this help
 *   GET  /ping                  version, pid, uid, uptime
 *   GET  /ls?p=DIR              type mode uid:gid size mtime name [-> target]
 *   GET  /stat?p=PATH           the same line for one path
 *   GET  /get?p=FILE            file contents
 *   PUT  /put?p=FILE[&m=644][&mk=1]
 *                               body -> FILE (FILE.agent-tmp, then rename);
 *                               m = octal mode, mk=1 creates parent dirs
 *   GET  /rm?p=PATH             unlink, or rmdir for a directory
 *   GET  /mv?p=FROM&to=TO       rename
 *   GET  /mkdir?p=DIR[&m=755]   mkdir -p
 *   GET  /ps                    pid ppid uid comm path
 *   GET  /kill?pid=N[&sig=15]
 *   GET  /run?a=PATH&a=ARG...[&t=30]
 *                               posix_spawn PATH with the args; stdout and
 *                               stderr stream back as they come, then
 *                               "[exit N]", "[signal N]" or "[timeout]"
 *                               (t seconds, 0 = none)
 *   GET  /exit                  exit; launchd (KeepAlive) starts the binary
 *                               again - after a PUT over it, the new one
 *
 * Mac: curl -s 'http://192.168.7.2:8022/ls?p=/var/mobile/Library/Logs'
 *
 * Access: TCP 8022 on all addresses (the agent can start before the USB
 * link is up), but only peers in 192.168.7.0/24 and 127.0.0.1 are served.
 * No authentication: a bring-up test device on a cable. Requests are logged
 * to stderr (launchd sends it to /dev/console, i.e. the live USB log).
 *
 * Build: agent/build.sh (arm64 iOS 16, ad-hoc signed with ldid and
 * agent/entitlements.plist; also a macOS build for local tests).
 */
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define AGENT_VERSION   "kunzite-agent 1"
#define AGENT_PORT      8022
#define HDR_MAX         16384
#define MAX_PARAMS      64
#define MAX_ARGS        48
/* A stalled or vanished peer must never wedge the single-threaded loop */
#ifndef AGENT_RCV_TIMEOUT_S
#define AGENT_RCV_TIMEOUT_S     60
#endif
#ifndef AGENT_SND_TIMEOUT_S
#define AGENT_SND_TIMEOUT_S     300
#endif

extern char **environ;
extern int proc_pidpath(int pid, void *buffer, uint32_t buffersize);

struct req {
	int fd;
	char method[8];
	char path[64];
	int nparams;
	char *key[MAX_PARAMS], *val[MAX_PARAMS];
	long content_length;
	char *body;             /* body bytes already read with the header */
	size_t body_have;
	char peer[INET_ADDRSTRLEN];
};

static void
logf_(const char *fmt, ...)
{
	char line[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	fprintf(stderr, "kunzite-agent: %s\n", line);
}

static int
write_all(int fd, const void *buf, size_t len)
{
	const char *p = buf;
	while (len > 0) {
		ssize_t n = write(fd, p, len);
		if (n < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		p += n;
		len -= (size_t)n;
	}
	return 0;
}

#define BODY_MAX       (256u << 20)

static void
out(struct req *r, const char *fmt, ...)
{
	char buf[4096];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n > 0) {
		write_all(r->fd, buf, (size_t)n);
	}
}

static void
respond(struct req *r, int status, const char *ctype)
{
	char h[256];
	int n = snprintf(h, sizeof(h),
	    "HTTP/1.0 %d %s\r\nContent-Type: %s\r\nConnection: close\r\n\r\n",
	    status, status == 200 ? "OK" : "Error", ctype);
	if (n > 0) {
		write_all(r->fd, h, (size_t)n);
	}
}

/* --- request parsing --- */

static int
hexval(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

static void
url_decode(char *s)
{
	char *o = s;
	while (*s) {
		if (s[0] == '%' && hexval(s[1]) >= 0 && hexval(s[2]) >= 0) {
			*o++ = (char)(hexval(s[1]) * 16 + hexval(s[2]));
			s += 3;
		} else {
			*o++ = *s++;
		}
	}
	*o = 0;
}

static const char *
param(struct req *r, const char *key)
{
	for (int i = 0; i < r->nparams; i++) {
		if (strcmp(r->key[i], key) == 0) {
			return r->val[i];
		}
	}
	return NULL;
}

static long
param_num(struct req *r, const char *key, long dflt)
{
	const char *v = param(r, key);
	return v ? strtol(v, NULL, 0) : dflt;
}

/* m=600 means octal 0600, as ls and chmod print it */
static mode_t
param_mode(struct req *r, const char *key, mode_t dflt)
{
	const char *v = param(r, key);
	return v ? (mode_t)strtol(v, NULL, 8) : dflt;
}

static char *
header_value(const char *headers, const char *name)
{
	size_t nlen = strlen(name);
	const char *l = headers;
	while (l && *l) {
		const char *eol = strstr(l, "\r\n");
		size_t ll = eol ? (size_t)(eol - l) : strlen(l);
		if (ll > nlen && strncasecmp(l, name, nlen) == 0 && l[nlen] == ':') {
			const char *v = l + nlen + 1;
			while (*v == ' ' || *v == '\t') {
				v++;
			}
			return (char *)v;      /* inside the header buffer */
		}
		l = eol ? eol + 2 : NULL;
	}
	return NULL;
}

static void
free_req(struct req *r)
{
	for (int i = 0; i < r->nparams; i++) {
		free(r->key[i]);
		free(r->val[i]);
	}
	free(r->body);
}

/* 0 = ok, -1 = malformed / truncated. Fills method, path, params, body. */

/* Buffered socket reader for chunked bodies: seeded with the bytes that
 * arrived together with the headers, then the socket. */
struct breader {
	int fd;
	unsigned char buf[HDR_MAX];
	size_t off, n;
};

static int
br_getc(struct breader *b)
{
	if (b->off >= b->n) {
		ssize_t k = read(b->fd, b->buf, sizeof(b->buf));
		if (k < 0 && errno == EINTR) {
			return br_getc(b);
		}
		if (k <= 0) {
			return -1;
		}
		b->off = 0;
		b->n = (size_t)k;
	}
	return b->buf[b->off++];
}

static int
br_read_exact(struct breader *b, void *dst, size_t n)
{
	unsigned char *d = dst;
	while (n > 0) {
		if (b->off < b->n) {
			size_t c = b->n - b->off;
			if (c > n) {
				c = n;
			}
			memcpy(d, b->buf + b->off, c);
			b->off += c;
			d += c;
			n -= c;
		} else {
			int ch = br_getc(b);
			if (ch < 0) {
				return -1;
			}
			*d++ = (unsigned char)ch;
			n--;
		}
	}
	return 0;
}

/* One CRLF-terminated line (without the CRLF); -1 on EOF/oversize */
static int
br_line(struct breader *b, char *line, size_t max)
{
	size_t l = 0;
	int c;
	while ((c = br_getc(b)) >= 0) {
		if (c == '\n') {
			if (l > 0 && line[l - 1] == '\r') {
				l--;
			}
			line[l] = 0;
			return 0;
		}
		if (l + 1 >= max) {
			return -1;
		}
		line[l++] = (char)c;
	}
	return -1;
}

static int
read_chunked(struct req *r, const char *leftover, size_t nleft)
{
	struct breader br;
	memset(&br, 0, sizeof(br));
	br.fd = r->fd;
	if (nleft > sizeof(br.buf)) {
		return -1;
	}
	memcpy(br.buf, leftover, nleft);
	br.n = nleft;

	size_t cap = 65536, len = 0;
	r->body = malloc(cap);
	if (!r->body) {
		return -1;
	}
	char line[128];
	for (;;) {
		if (br_line(&br, line, sizeof(line)) != 0) {
			return -1;
		}
		unsigned long sz = strtoul(line, NULL, 16);
		if (sz == 0) {
			break;              /* trailers until the empty line */
		}
		if (len + sz > BODY_MAX) {
			return -1;
		}
		while (cap < len + sz) {
			cap *= 2;
		}
		char *nb = realloc(r->body, cap);
		if (!nb) {
			return -1;
		}
		r->body = nb;
		if (br_read_exact(&br, r->body + len, sz) != 0) {
			return -1;
		}
		len += sz;
		char crlf[2];
		if (br_read_exact(&br, crlf, 2) != 0) {
			return -1;
		}
	}
	while (br_line(&br, line, sizeof(line)) == 0 && line[0] != 0) {
	}
	r->body_have = len;
	r->content_length = (long)len;
	return 0;
}

static int
read_request(struct req *r)
{
	char *buf = malloc(HDR_MAX);
	if (!buf) {
		return -1;
	}
	size_t have = 0;
	char *hdr_end = NULL;
	while (!hdr_end) {
		if (have >= HDR_MAX - 1) {
			goto bad;
		}
		ssize_t n = read(r->fd, buf + have, HDR_MAX - 1 - have);
		if (n < 0 && errno == EINTR) {
			continue;
		}
		if (n <= 0) {
			goto bad;
		}
		have += (size_t)n;
		buf[have] = 0;
		hdr_end = strstr(buf, "\r\n\r\n");
	}
	*hdr_end = 0;                       /* headers end here; body follows at +4 */

	/* "METHOD /path?params HTTP/1.x" - parse from a copy: the header block
	 * must keep its CRLFs intact for header_value() below */
	char *rl_end = strstr(buf, "\r\n");
	if (!rl_end) {
		goto bad;
	}
	char rl[256];
	size_t rl_len = (size_t)(rl_end - buf);
	if (rl_len >= sizeof(rl)) {
		goto bad;
	}
	memcpy(rl, buf, rl_len);
	rl[rl_len] = 0;
	char *sp1 = strchr(rl, ' ');
	if (!sp1) {
		goto bad;
	}
	char *sp2 = strchr(sp1 + 1, ' ');
	if (!sp2) {
		goto bad;
	}
	size_t ml = MIN((size_t)(sp1 - rl), sizeof(r->method) - 1);
	memcpy(r->method, rl, ml);
	r->method[ml] = 0;
	char *target = sp1 + 1;
	*sp2 = 0;
	char *q = strchr(target, '?');
	if (q) {
		*q++ = 0;
	}
	size_t pl = strlen(target);
	if (pl >= sizeof(r->path)) {
		goto bad;
	}
	memcpy(r->path, target, pl + 1);

	/* params: a=b pairs, both percent-decoded, duplicates kept */
	if (q) {
		char *seg = q;
		while (seg && *seg && r->nparams < MAX_PARAMS) {
			char *amp = strchr(seg, '&');
			if (amp) {
				*amp = 0;
			}
			char *eq = strchr(seg, '=');
			char *k, *v;
			if (eq) {
				*eq = 0;
				k = seg;
				v = eq + 1;
			} else {
				k = seg;
				v = seg + strlen(seg);       /* mutable empty string */
			}
			url_decode(k);
			url_decode(v);
			r->key[r->nparams] = strdup(k);
			r->val[r->nparams] = strdup(v);
			if (!r->key[r->nparams] || !r->val[r->nparams]) {
				goto bad;
			}
			r->nparams++;
			seg = amp ? amp + 1 : NULL;
		}
	}

	char *cl = header_value(buf, "Content-Length");
	char *te = header_value(buf, "Transfer-Encoding");
	bool chunked = te && strncasecmp(te, "chunked", 7) == 0;
	r->content_length = cl ? strtol(cl, NULL, 10) : 0;
	if (r->content_length < 0 || r->content_length > (long)BODY_MAX) {
		goto bad;
	}
	size_t head = (size_t)(hdr_end - buf) + 4;      /* past \r\n\r\n */
	if (chunked) {
		/* curl -T - streams stdin as chunked; decode it */
		if (read_chunked(r, buf + head, have - head) != 0) {
			goto bad;
		}
	} else if (r->content_length > 0) {
		r->body = malloc((size_t)r->content_length);
		if (!r->body) {
			goto bad;
		}
		size_t rem = have - head;
		if (rem > (size_t)r->content_length) {
			rem = (size_t)r->content_length;
		}
		memcpy(r->body, buf + head, rem);
		r->body_have = rem;
		while (r->body_have < (size_t)r->content_length) {
			ssize_t n = read(r->fd, r->body + r->body_have,
			    (size_t)r->content_length - r->body_have);
			if (n < 0 && errno == EINTR) {
				continue;
			}
			if (n <= 0) {
				goto bad;
			}
			r->body_have += (size_t)n;
		}
	}
	free(buf);
	return 0;
bad:
	free(buf);
	free_req(r);
	return -1;
}

/* --- filesystem --- */

static void
stat_line(struct req *r, const char *path, const char *name)
{
	struct stat st;
	if (lstat(path, &st) != 0) {
		out(r, "ERR lstat %s: %s\n", name, strerror(errno));
		return;
	}
	char ts[24] = "?";
	struct tm tm;
	if (localtime_r(&st.st_mtimespec.tv_sec, &tm) != NULL) {
		strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M", &tm);
	}
	char t = S_ISDIR(st.st_mode) ? 'd' : S_ISLNK(st.st_mode) ? 'l' :
	    S_ISREG(st.st_mode) ? 'f' : '?';
	if (t == 'l') {
		char tgt[PATH_MAX];
		ssize_t m = readlink(path, tgt, sizeof(tgt) - 1);
		tgt[m > 0 ? m : 0] = 0;
		out(r, "%c %04o %u:%u %lld %s %s -> %s\n", t, st.st_mode & 07777u,
		    st.st_uid, st.st_gid, (long long)st.st_size, ts, name, tgt);
	} else {
		out(r, "%c %04o %u:%u %lld %s %s\n", t, st.st_mode & 07777u,
		    st.st_uid, st.st_gid, (long long)st.st_size, ts, name);
	}
}

static void
h_ls(struct req *r)
{
	const char *p = param(r, "p");
	if (!p) {
		out(r, "ERR no p\n");
		return;
	}
	DIR *d = opendir(p);
	if (!d) {
		out(r, "ERR opendir %s: %s\n", p, strerror(errno));
		return;
	}
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		char full[PATH_MAX];
		if (snprintf(full, sizeof(full), "%s/%s", p, e->d_name) < (int)sizeof(full)) {
			stat_line(r, full, e->d_name);
		}
	}
	closedir(d);
}

static void
h_stat(struct req *r)
{
	const char *p = param(r, "p");
	if (!p) {
		out(r, "ERR no p\n");
		return;
	}
	stat_line(r, p, p);
}

static void
h_get(struct req *r)
{
	const char *p = param(r, "p");
	if (!p) {
		out(r, "ERR no p\n");
		return;
	}
	int fd = open(p, O_RDONLY);
	if (fd < 0) {
		out(r, "ERR open %s: %s\n", p, strerror(errno));
		return;
	}
	char buf[65536];
	ssize_t n;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		if (write_all(r->fd, buf, (size_t)n) != 0) {
			break;      /* client went away */
		}
	}
	if (n < 0) {
		out(r, "\nERR read: %s\n", strerror(errno));
	}
	close(fd);
}

/* mkdir -p for one path (dirs up to the last component) */
static int
mkdir_p(const char *path, mode_t mode)
{
	char tmp[PATH_MAX];
	size_t l = strlen(path);
	if (l == 0 || l >= sizeof(tmp)) {
		errno = EINVAL;
		return -1;
	}
	memcpy(tmp, path, l + 1);
	while (l > 1 && tmp[l - 1] == '/') {
		tmp[--l] = 0;
	}
	for (char *c = tmp + 1; *c; c++) {
		if (*c != '/' || !c[1]) {
			continue;
		}
		*c = 0;
		if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
			return -1;
		}
		*c = '/';
	}
	return 0;
}

static void
h_put(struct req *r)
{
	const char *p = param(r, "p");
	if (!p) {
		out(r, "ERR no p\n");
		return;
	}
	mode_t mode = param_mode(r, "m", 0644);
	char tmp[PATH_MAX + 16];
	if (snprintf(tmp, sizeof(tmp), "%s.agent-tmp", p) >= (int)sizeof(tmp)) {
		out(r, "ERR path too long\n");
		return;
	}
	if (param(r, "mk") && mkdir_p(tmp, 0755) != 0) {
		out(r, "ERR mkdir parents: %s\n", strerror(errno));
		return;
	}
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (fd < 0) {
		out(r, "ERR open %s: %s\n", tmp, strerror(errno));
		return;
	}
	int bad = write_all(fd, r->body, r->body_have) != 0;
	if (close(fd) != 0) {
		bad = 1;
	}
	if (bad) {
		unlink(tmp);
		out(r, "ERR write %s: %s\n", tmp, strerror(errno));
		return;
	}
	/* open() applied the umask: set the requested mode exactly */
	if (chmod(tmp, mode) != 0 || rename(tmp, p) != 0) {
		unlink(tmp);
		out(r, "ERR rename: %s\n", strerror(errno));
		return;
	}
	out(r, "OK %zu bytes -> %s mode %04o\n", r->body_have, p, mode);
}

static void
h_rm(struct req *r)
{
	const char *p = param(r, "p");
	if (!p) {
		out(r, "ERR no p\n");
		return;
	}
	struct stat st;
	if (lstat(p, &st) != 0) {
		out(r, "ERR lstat: %s\n", strerror(errno));
		return;
	}
	int e = S_ISDIR(st.st_mode) ? rmdir(p) : unlink(p);
	if (e != 0) {
		out(r, "ERR remove: %s\n", strerror(errno));
		return;
	}
	out(r, "OK removed %s\n", p);
}

static void
h_mv(struct req *r)
{
	const char *p = param(r, "p");
	const char *to = param(r, "to");
	if (!p || !to) {
		out(r, "ERR need p and to\n");
		return;
	}
	if (rename(p, to) != 0) {
		out(r, "ERR rename: %s\n", strerror(errno));
		return;
	}
	out(r, "OK %s -> %s\n", p, to);
}

static void
h_mkdir(struct req *r)
{
	const char *p = param(r, "p");
	if (!p) {
		out(r, "ERR no p\n");
		return;
	}
	mode_t mode = param_mode(r, "m", 0755);
	if (mkdir_p(p, mode) != 0 || mkdir(p, mode) != 0) {
		if (errno == EEXIST) {
			struct stat st;
			if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
				out(r, "OK dir %s exists\n", p);
				return;
			}
		}
		out(r, "ERR mkdir: %s\n", strerror(errno));
		return;
	}
	out(r, "OK dir %s mode %04o\n", p, mode);
}

/* --- processes --- */

static void
h_ps(struct req *r)
{
	int mib[3] = { CTL_KERN, KERN_PROC, KERN_PROC_ALL };
	size_t len = 0;
	struct kinfo_proc *kp = NULL;
	int okp = 0;
	for (int tries = 0; tries < 4 && !okp; tries++) {
		len = 0;
		if (sysctl(mib, 3, NULL, &len, NULL, 0) != 0 || len < sizeof(struct kinfo_proc)) {
			break;
		}
		struct kinfo_proc *nk = realloc(kp, len);
		if (!nk) {
			break;
		}
		kp = nk;
		okp = sysctl(mib, 3, kp, &len, NULL, 0) == 0;
		if (!okp && errno != ENOMEM) {
			break;
		}
	}
	if (!okp) {
		out(r, "ERR sysctl: %s\n", strerror(errno));
		free(kp);
		return;
	}
	size_t n = len / sizeof(struct kinfo_proc);
	for (size_t i = 0; i < n; i++) {
		char path[PATH_MAX] = "";
		(void)proc_pidpath(kp[i].kp_proc.p_pid, path, sizeof(path));
		out(r, "%d %d %u %s %s\n", kp[i].kp_proc.p_pid, kp[i].kp_eproc.e_ppid,
		    kp[i].kp_eproc.e_ucred.cr_uid, kp[i].kp_proc.p_comm, path);
	}
	free(kp);
}

static void
h_kill(struct req *r)
{
	long long pid = param_num(r, "pid", 0);
	long sig = param_num(r, "sig", SIGTERM);
	/* pid_t is 32-bit: a wrapped 2^32+k would reach kill() as k */
	if (pid <= 1 || pid > INT_MAX) {
		out(r, "ERR need 1 < pid <= %d\n", INT_MAX);
		return;
	}
	if (kill((pid_t)pid, (int)sig) != 0) {
		out(r, "ERR kill: %s\n", strerror(errno));
		return;
	}
	out(r, "OK signal %ld to %lld\n", sig, pid);
}

/*
 * /run?a=PROG&a=arg...[&t=30]: spawn with stdout+stderr streamed to the
 * client as they arrive; the last line says how it ended. stdin is a closed
 * pipe (immediate EOF for tools that read it).
 */
static void
h_run(struct req *r)
{
	char *argvs[MAX_ARGS + 2];
	int argc = 0;
	for (int i = 0; i < r->nparams && argc <= MAX_ARGS; i++) {
		if (strcmp(r->key[i], "a") == 0) {
			argvs[argc++] = r->val[i];
		}
	}
	if (argc == 0) {
		out(r, "ERR no a= program\n");
		return;
	}
	argvs[argc] = NULL;
	int t = (int)param_num(r, "t", 30);
	int inpipe[2] = { -1, -1 }, outp[2] = { -1, -1 }, errp[2] = { -1, -1 };
	if (pipe(inpipe) != 0 || pipe(outp) != 0 || pipe(errp) != 0) {
		out(r, "ERR pipe: %s\n", strerror(errno));
		goto pipes_done;
	}
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, inpipe[0], 0);
	posix_spawn_file_actions_adddup2(&fa, outp[1], 1);
	posix_spawn_file_actions_adddup2(&fa, errp[1], 2);
	/*
	 * CLOEXEC_DEFAULT: the child (and anything it forks) keeps only the
	 * dup2'd 0/1/2 - not our listening socket, the client connection, or
	 * spare pipe ends (the child's own copy of the stdin write end would
	 * otherwise keep stdin open forever). SIGPIPE is reset to default so a
	 * grandchild writing after we are gone dies instead of hanging.
	 */
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif
	sigset_t dfl;
	sigemptyset(&dfl);
	sigaddset(&dfl, SIGPIPE);
	posix_spawnattr_setsigdefault(&attr, &dfl);
	pid_t pid = -1;
	int err = posix_spawn(&pid, argvs[0], &fa, &attr, argvs, environ);
	posix_spawnattr_destroy(&attr);
	posix_spawn_file_actions_destroy(&fa);
	close(inpipe[0]);
	close(inpipe[1]);      /* the child sees EOF on stdin */
	close(outp[1]);
	close(errp[1]);
	if (err != 0) {
		out(r, "ERR spawn %s: %s\n", argvs[0], strerror(err));
		goto pipes_done;
	}
	logf_("run %s pid %d", argvs[0], pid);

	struct pollfd fds[2] = {
		{ .fd = outp[0], .events = POLLIN, .revents = 0 },
		{ .fd = errp[0], .events = POLLIN, .revents = 0 },
	};
	int open = 2;
	struct timespec start, now;
	clock_gettime(CLOCK_MONOTONIC, &start);
	int timed_out = 0, client_gone = 0;
	char buf[65536];
	while (open > 0) {
		int ms = -1;
		if (t > 0) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			long left = t * 1000L -
			    ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);
			if (left <= 0) {
				timed_out = 1;
				break;
			}
			ms = (int)(left > INT_MAX ? INT_MAX : left);
		}
		int rdy = poll(fds, 2, ms);
		if (rdy < 0 && errno == EINTR) {
			continue;
		}
		if (rdy < 0) {
			break;
		}
		if (rdy == 0) {
			timed_out = 1;
			break;
		}
		for (int i = 0; i < 2; i++) {
			if (fds[i].fd < 0 || !fds[i].revents) {
				continue;
			}
			ssize_t n = read(fds[i].fd, buf, sizeof(buf));
			if (n > 0) {
				if (write_all(r->fd, buf, (size_t)n) != 0) {
					client_gone = 1;     /* stop streaming */
					open = 0;
					break;
				}
			} else {
				close(fds[i].fd);
				fds[i].fd = -1;
				open--;
			}
		}
	}
	/*
	 * Close our pipe ends BEFORE reaping (a child blocked writing into a
	 * full pipe gets EPIPE/SIGPIPE), and never wait unbounded: t=0 still
	 * means one hour, and a client that walked away means SIGKILL now -
	 * this loop is the whole agent.
	 */
	for (int i = 0; i < 2; i++) {
		if (fds[i].fd >= 0) {
			close(fds[i].fd);
			fds[i].fd = -1;
		}
	}
	if (client_gone) {
		kill(pid, SIGKILL);        /* the client walked away: reap fast */
	}
	long budget_ms = (t > 0 ? (long)t : 3600L) * 1000L;
	int st = 0;
	for (;;) {
		if (waitpid(pid, &st, WNOHANG) == pid) {
			break;
		}
		if (!timed_out) {
			clock_gettime(CLOCK_MONOTONIC, &now);
			long el = (now.tv_sec - start.tv_sec) * 1000 +
			    (now.tv_nsec - start.tv_nsec) / 1000000;
			if (el >= budget_ms) {
				kill(pid, SIGKILL);
				timed_out = 1;
			}
		}
		usleep(20000);
	}
	if (client_gone) {
		/* nobody is listening; the [exit] line goes nowhere */
	} else if (timed_out) {
		out(r, "\n[timeout]\n");
	} else if (WIFEXITED(st)) {
		out(r, "\n[exit %d]\n", WEXITSTATUS(st));
	} else if (WIFSIGNALED(st)) {
		out(r, "\n[signal %d]\n", WTERMSIG(st));
	}
pipes_done:
	for (int i = 0; i < 2; i++) {
		if (inpipe[i] >= 0) {
			close(inpipe[i]);
		}
		if (outp[i] >= 0) {
			close(outp[i]);
		}
		if (errp[i] >= 0) {
			close(errp[i]);
		}
	}
}


/* --- misc + dispatch --- */

static void
h_ping(struct req *r)
{
	struct timeval bt;
	size_t l = sizeof(bt);
	int mib[2] = { CTL_KERN, KERN_BOOTTIME };
	long up = -1;
	if (sysctl(mib, 2, &bt, &l, NULL, 0) == 0) {
		up = time(NULL) - bt.tv_sec;
	}
	out(r, "%s pid %d uid %d uptime %lds\n", AGENT_VERSION, getpid(), getuid(), up);
}

static const char HELP[] =
	AGENT_VERSION "\n"
	"  /                this help\n"
	"  /ping            version, pid, uid, uptime\n"
	"  /ls?p=DIR        type mode uid:gid size mtime name [-> target]\n"
	"  /stat?p=PATH     one line for one path\n"
	"  /get?p=FILE      file contents\n"
	"  /put?p=FILE [&m=644] [&mk=1]     body -> FILE (curl -T)\n"
	"  /rm?p=PATH       unlink / rmdir\n"
	"  /mv?p=FROM&to=TO rename\n"
	"  /mkdir?p=DIR[&m=755]\n"
	"  /ps              pid ppid uid comm path\n"
	"  /kill?pid=N[&sig=15]\n"
	"  /run?a=PROG&a=ARG...[&t=30]     spawn; stdout/stderr streamed\n"
	"  /exit            exit; launchd restarts the binary\n";

static void
handle(struct req *r)
{
	logf_("%s /%s from %s", r->method, r->path, r->peer);
	int is_get = strcmp(r->method, "GET") == 0 || strcmp(r->method, "HEAD") == 0;
	if (!is_get && strcmp(r->method, "PUT") != 0) {
		respond(r, 405, "text/plain");
		out(r, "ERR method %s\n", r->method);
		return;
	}
	if (strcmp(r->method, "HEAD") == 0) {
		/* headers only: cheap presence/status probe */
		respond(r, 200, "text/plain");
		return;
	}
	if (strcmp(r->path, "/put") == 0) {
		if (strcmp(r->method, "PUT") != 0) {
			respond(r, 405, "text/plain");
			out(r, "ERR /put needs PUT (curl -T)\n");
			return;
		}
		respond(r, 200, "text/plain");
		h_put(r);
		return;
	}
	if (!is_get) {
		respond(r, 405, "text/plain");
		out(r, "ERR PUT is for /put only\n");
		return;
	}
	if (strcmp(r->path, "/") == 0 || strcmp(r->path, "/help") == 0) {
		respond(r, 200, "text/plain");
		write_all(r->fd, HELP, sizeof(HELP) - 1);
	} else if (strcmp(r->path, "/ping") == 0) {
		respond(r, 200, "text/plain");
		h_ping(r);
	} else if (strcmp(r->path, "/ls") == 0) {
		respond(r, 200, "text/plain");
		h_ls(r);
	} else if (strcmp(r->path, "/stat") == 0) {
		respond(r, 200, "text/plain");
		h_stat(r);
	} else if (strcmp(r->path, "/get") == 0) {
		respond(r, 200, "application/octet-stream");
		h_get(r);
	} else if (strcmp(r->path, "/rm") == 0) {
		respond(r, 200, "text/plain");
		h_rm(r);
	} else if (strcmp(r->path, "/mv") == 0) {
		respond(r, 200, "text/plain");
		h_mv(r);
	} else if (strcmp(r->path, "/mkdir") == 0) {
		respond(r, 200, "text/plain");
		h_mkdir(r);
	} else if (strcmp(r->path, "/ps") == 0) {
		respond(r, 200, "text/plain");
		h_ps(r);
	} else if (strcmp(r->path, "/kill") == 0) {
		respond(r, 200, "text/plain");
		h_kill(r);
	} else if (strcmp(r->path, "/run") == 0) {
		respond(r, 200, "application/octet-stream");
		h_run(r);
	} else if (strcmp(r->path, "/exit") == 0) {
		respond(r, 200, "text/plain");
		out(r, "OK exiting\n");
		logf_("exit via /exit");
		_exit(0);
	} else {
		respond(r, 404, "text/plain");
		out(r, "ERR unknown endpoint %s\n", r->path);
		out(r, "%s", HELP);
	}
}

int
main(void)
{
	signal(SIGPIPE, SIG_IGN);

	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (lfd < 0) {
		logf_("socket: %s", strerror(errno));
		return 1;
	}
	int one = 1;
	(void)setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in a;
	memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	a.sin_port = htons(AGENT_PORT);
	if (bind(lfd, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(lfd, 8) != 0) {
		logf_("bind/listen %d: %s", AGENT_PORT, strerror(errno));
		return 1;
	}
	logf_("%s on tcp/%d (192.168.7.0/24 and loopback only)", AGENT_VERSION, AGENT_PORT);

	for (;;) {
		struct sockaddr_in pa;
		socklen_t pl = sizeof(pa);
		int c = accept(lfd, (struct sockaddr *)&pa, &pl);
		if (c < 0) {
			if (errno == EMFILE || errno == ENFILE) {
				usleep(100000);     /* no fds: do not spin */
			}
			continue;
		}
		/* A stalled or vanished peer must never wedge this loop */
		struct timeval tv = { .tv_sec = AGENT_RCV_TIMEOUT_S };
		(void)setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		tv.tv_sec = AGENT_SND_TIMEOUT_S;
		(void)setsockopt(c, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		uint32_t ip = ntohl(pa.sin_addr.s_addr);
		if ((ip & 0xffffff00u) != 0xc0a80700u && ip != 0x7f000001u) {
			close(c);      /* not the USB link, not us */
			continue;
		}
		struct req r;
		memset(&r, 0, sizeof(r));
		r.fd = c;
		inet_ntop(AF_INET, &pa.sin_addr, r.peer, sizeof(r.peer));
		if (read_request(&r) == 0) {
			handle(&r);
			free_req(&r);
		} else {
			respond(&r, 400, "text/plain");
			write_all(c, "ERR bad request\n", 17);
		}
		close(c);
	}
}