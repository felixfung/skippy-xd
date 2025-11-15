/* Skippy-xd
 *
 * Copyright (C) 2004 Hyriand <hyriand@thegraveyard.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "skippy.h"
#include <errno.h>
#include <locale.h>
#include <getopt.h>
#include <strings.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <libgen.h>
#include <dirent.h>
#include <regex.h>

bool debuglog = false;

enum pipe_cmd_t {
	PIPECMD_EXIT_DAEMON = 1,
	PIPECMD_SWITCH = 4,
	PIPECMD_EXPOSE = 8,
	PIPECMD_PAGING = 16,
	PIPECMD_PREV = 32,
	PIPECMD_NEXT = 64,
	PIPECMD_MULTI_BYTE = 128,
};

enum pipe_param_t {
	PIPEPRM_RELOAD_CONFIG_PATH = 1,
	PIPEPRM_RELOAD_CONFIG = 2,
	PIPEPRM_MULTI_SELECT = 4,
	PIPEPRM_WM_CLASS = 8,
	PIPEPRM_WM_TITLE = 16,
	PIPEPRM_WM_STATUS = 32,
	PIPEPRM_DESKTOPS = 64,
	PIPEPRM_PIVOTING = 128,
};

session_t *ps_g = NULL;

/**
 * @brief Parse a string representation of enum cliop.
 */
static bool
parse_cliop(session_t *ps, const char *str, enum cliop *dest) {
	static const char * const STRS_CLIENTOP[] = {
		[	CLIENTOP_NO					] = "no",
		[	CLIENTOP_FOCUS				] = "focus",
		[	CLIENTOP_ICONIFY			] = "iconify",
		[	CLIENTOP_SHADE_EWMH			] = "shade-ewmh",
		[	CLIENTOP_CLOSE_ICCCM		] = "close-icccm",
		[	CLIENTOP_CLOSE_EWMH			] = "close-ewmh",
		[	CLIENTOP_DESTROY			] = "destroy",
		[	CLIENTOP_PREV				] = "keysPrev",
		[	CLIENTOP_NEXT				] = "keysNext",
	};
	for (int i = 0; i < sizeof(STRS_CLIENTOP) / sizeof(STRS_CLIENTOP[0]); ++i)
		if (!strcmp(STRS_CLIENTOP[i], str)) {
			*dest = i;
			return true;
		}

	printfef(true, "() (\"%s\"): Unrecognized operation.", str);
	return false;
}

/**
 * @brief Parse a string representation of enum align.
 */
static int
parse_align(session_t *ps, const char *str, enum align *dest) {
	static const char * const STRS_ALIGN[] = {
		[ ALIGN_LEFT  ] = "left",
		[ ALIGN_MID   ] = "mid",
		[ ALIGN_RIGHT ] = "right",
	};
	for (int i = 0; i < CARR_LEN(STRS_ALIGN); ++i)
		if (str_startswithword(str, STRS_ALIGN[i])) {
			*dest = i;
			return strlen(STRS_ALIGN[i]);
		}

	printfef(true, "() (\"%s\"): Unrecognized operation.", str);
	return 0;
}

static inline bool
parse_align_full(session_t *ps, const char *str, enum align *dest) {
	int r = parse_align(ps, str, dest);
	if (r && str[r]) r = 0;
	return r;
}

/**
 * @brief Parse a string representation of picture positioning mode.
 */
static int
parse_pict_posp_mode(session_t *ps, const char *str, enum pict_posp_mode *dest) {
	static const char * const STRS_PICTPOSP[] = {
		[ PICTPOSP_ORIG			] = "orig",
		[ PICTPOSP_SCALE		] = "scale",
		[ PICTPOSP_SCALEK		] = "scalek",
		[ PICTPOSP_SCALEE		] = "scalee",
		[ PICTPOSP_SCALEEK		] = "scaleek",
		[ PICTPOSP_TILE	 		] = "tile",
	};
	for (int i = 0; i < CARR_LEN(STRS_PICTPOSP); ++i)
		if (str_startswithword(str, STRS_PICTPOSP[i])) {
			*dest = i;
			return strlen(STRS_PICTPOSP[i]);
		}

	printfef(true, "() (\"%s\"): Unrecognized operation.", str);
	return 0;
}

static inline int
parse_color_sub(const char *s, unsigned short *dest) {
	static const int SEG = 2;

	char *endptr = NULL;
	long v = 0L;
	char *s2 = mstrncpy(s, SEG);
	v = strtol(s2, &endptr, 16);
	int ret = 0;
	if (endptr && s2 + strlen(s2) == endptr)
		ret = endptr - s2;
	free(s2);
	if (!ret) return ret;
	*dest = (double) v / 0xff * 0xffff;
	return ret;
}

/**
 * @brief Parse an option string into XRenderColor.
 */
static int
parse_color(session_t *ps, const char *s, XRenderColor *pc) {
	const char * const sorig = s;
	static const struct {
		const char *name;
		XRenderColor c;
	} PREDEF_COLORS[] = {
		{ "black", { 0x0000, 0x0000, 0x0000, 0xFFFF } },
		{ "red", { 0xffff, 0x0000, 0x0000, 0xFFFF } },
	};

	// Predefined color names
	for (int i = 0; i < CARR_LEN(PREDEF_COLORS); ++i)
		if (str_startswithwordi(s, PREDEF_COLORS[i].name)) {
			*pc = PREDEF_COLORS[i].c;
			return strlen(PREDEF_COLORS[i].name);
		}

	// RRGGBBAA color
	if ('#' == s[0]) {
		++s;
		int next = 0;
		if (!((next = parse_color_sub(s, &pc->red))
					&& (next = parse_color_sub((s += next), &pc->green))
					&& (next = parse_color_sub((s += next), &pc->blue)))) {
			printfef(true, "() (\"%s\"): Failed to read color segment.", s);
			return 0;
		}
		if (!(next = parse_color_sub((s += next), &pc->alpha)))
			pc->alpha = 0xffff;
		s += next;
		return s - sorig;
	}

	printfef(true, "(\"%s\"): Unrecognized color format.", s);
	return 0;
}

/**
 * @brief Parse a size string.
 */
static int
parse_size(const char *s, int *px, int *py) {
	const char * const sorig = s;
	long val = 0L;
	char *endptr = NULL;
	bool hasdata = false;

#define T_NEXTFIELD() do { \
	hasdata = true; \
	if (isspace0(*s)) goto parse_size_end; \
} while(0)

	// Parse width
	// Must be base 10, because "0x0..." may appear
	val = strtol(s, &endptr, 10);
	if (endptr && s != endptr) {
		*px = val;
		assert(*px >= 0);
		s = endptr;
		T_NEXTFIELD();
	}

	// Parse height
	if ('x' == *s) {
		++s;
		val = strtol(s, &endptr, 10);
		if (endptr && s != endptr) {
			*py = val;
			if (*py < 0) {
				printfef(true, "() (\"%s\"): Invalid height.", s);
				return 0;
			}
			s = endptr;
		}
		T_NEXTFIELD();
	}

#undef T_NEXTFIELD

	if (!hasdata)
		return 0;

	if (!isspace0(*s)) {
		printfef(true, "() (\"%s\"): Trailing characters.", s);
		return 0;
	}

parse_size_end:
	return s - sorig;
}

/**
 * @brief Parse an image specification.
 */
static bool
parse_pictspec(session_t *ps, const char *s, pictspec_t *dest) {
#define T_NEXTFIELD() do { \
	s += next; \
	while (isspace(*s)) ++s; \
	if (!*s) goto parse_pictspec_end; \
} while (0)

	int next = 0;
	T_NEXTFIELD();
	if (!(next = parse_size(s, &dest->twidth, &dest->theight)))
		dest->twidth = dest->theight = 0;
	T_NEXTFIELD();
	if (!(next = parse_pict_posp_mode(ps, s, &dest->mode)))
		dest->mode = PICTPOSP_ORIG;
	T_NEXTFIELD();
	if (!(next = parse_align(ps, s, &dest->alg)))
		dest->alg = ALIGN_MID;
	T_NEXTFIELD();
	if (!(next && (next = parse_align(ps, s, &dest->valg))))
		dest->valg = ALIGN_MID;
	T_NEXTFIELD();
	next = parse_color(ps, s, &dest->c);
	T_NEXTFIELD();
	if (*s)
		dest->path = mstrdup(s);
#undef T_NEXTFIELD

parse_pictspec_end:
	return true;
}

static inline const char *
ev_dumpstr_type(const XEvent *ev) {
	switch (ev->type) {
		CASESTRRET(KeyPress);
		CASESTRRET(KeyRelease);
		CASESTRRET(ButtonPress);
		CASESTRRET(ButtonRelease);
		CASESTRRET(MotionNotify);
		CASESTRRET(EnterNotify);
		CASESTRRET(LeaveNotify);
		CASESTRRET(FocusIn);
		CASESTRRET(FocusOut);
		CASESTRRET(KeymapNotify);
		CASESTRRET(Expose);
		CASESTRRET(GraphicsExpose);
		CASESTRRET(NoExpose);
		CASESTRRET(CirculateRequest);
		CASESTRRET(ConfigureRequest);
		CASESTRRET(MapRequest);
		CASESTRRET(ResizeRequest);
		CASESTRRET(CirculateNotify);
		CASESTRRET(ConfigureNotify);
		CASESTRRET(CreateNotify);
		CASESTRRET(DestroyNotify);
		CASESTRRET(GravityNotify);
		CASESTRRET(MapNotify);
		CASESTRRET(MappingNotify);
		CASESTRRET(ReparentNotify);
		CASESTRRET(UnmapNotify);
		CASESTRRET(VisibilityNotify);
		CASESTRRET(ColormapNotify);
		CASESTRRET(ClientMessage);
		CASESTRRET(PropertyNotify);
		CASESTRRET(SelectionClear);
		CASESTRRET(SelectionNotify);
		CASESTRRET(SelectionRequest);
	}

	return "Unknown";
}

static inline Window
ev_window(session_t *ps, const XEvent *ev) {
#define T_SETWID(type, ele) case type: return ev->ele.window
	switch (ev->type) {
		case KeyPress:
		T_SETWID(KeyRelease, xkey);
		case ButtonPress:
		T_SETWID(ButtonRelease, xbutton);
		T_SETWID(MotionNotify, xmotion);
		case EnterNotify:
		T_SETWID(LeaveNotify, xcrossing);
		case FocusIn:
		T_SETWID(FocusOut, xfocus);
		T_SETWID(KeymapNotify, xkeymap);
		T_SETWID(Expose, xexpose);
		case GraphicsExpose: return ev->xgraphicsexpose.drawable;
		case NoExpose: return ev->xnoexpose.drawable;
		T_SETWID(CirculateNotify, xcirculate);
		T_SETWID(ConfigureNotify, xconfigure);
		T_SETWID(CreateNotify, xcreatewindow);
		T_SETWID(DestroyNotify, xdestroywindow);
		T_SETWID(GravityNotify, xgravity);
		T_SETWID(MapNotify, xmap);
		T_SETWID(MappingNotify, xmapping);
		T_SETWID(ReparentNotify, xreparent);
		T_SETWID(UnmapNotify, xunmap);
		T_SETWID(VisibilityNotify, xvisibility);
		T_SETWID(ColormapNotify, xcolormap);
		T_SETWID(ClientMessage, xclient);
		T_SETWID(PropertyNotify, xproperty);
		T_SETWID(SelectionClear, xselectionclear);
		case SelectionNotify: return ev->xselection.requestor;
	}
#undef T_SETWID
	if (ps->xinfo.damage_ev_base + XDamageNotify == ev->type)
	  return ((XDamageNotifyEvent *) ev)->drawable;

	printfef(false, "(): Failed to find window for event type %d. Troubles ahead.",
			ev->type);

	return ev->xany.window;
}

static inline void
ev_dump(session_t *ps, const MainWin *mw, const XEvent *ev) {
	if (!ev || (ps->xinfo.damage_ev_base + XDamageNotify) == ev->type) return;
	// if (MotionNotify == ev->type) return;

	const char *name = ev_dumpstr_type(ev);

	Window wid = ev_window(ps, ev);
	const char *wextra = "";
	if (ps->root == wid) wextra = "(Root)";
	if (mw && mw->window == wid) wextra = "(Main)";

	print_timestamp(ps);
	printfdf(false, "(): Event %-13.13s wid %#010lx %s", name, wid, wextra);
}

static char *
DaemonToClientPipeName(session_t *ps, pid_t pid) {
	int pipeStrLen = strlen(ps->o.pipePath2) + 1 +10 + 1;
	char* daemon2client_pipe = malloc(pipeStrLen);
	sprintf(daemon2client_pipe, "%s-%010i", ps->o.pipePath2, pid);
	return daemon2client_pipe;
}

static void returnToClient(session_t *ps, pid_t pid, char *pipe_return)
{
	char *daemon2clientpipe = DaemonToClientPipeName(ps, pid);
	int fd = open(daemon2clientpipe, O_WRONLY | O_NONBLOCK);
	int bytes_written = write(fd, pipe_return, strlen(pipe_return));
	if (bytes_written < strlen(pipe_return)) {
		printfef(true, "(): daemon-to-client packet incomplete!");
	}
	close(fd);
	free(daemon2clientpipe);
}

static inline bool
open_pipe(session_t *ps, struct pollfd *r_fd) {
	if (ps->fd_pipe >= 0) {
		close(ps->fd_pipe);
		ps->fd_pipe = -1;
		if (r_fd)
			r_fd[1].fd = ps->fd_pipe;
	}
	ps->fd_pipe = open(ps->o.pipePath, O_RDONLY | O_NONBLOCK);
	if (ps->fd_pipe >= 0) {
		if (r_fd)
			r_fd[1].fd = ps->fd_pipe;
		return true;
	}
	else {
		printfef(true, "(): Failed to open pipe \"%s\": %d", ps->o.pipePath, errno);
		perror("open");
	}

	return false;
}

static inline int
read_pipe(session_t *ps, struct pollfd *r_fd, char *piped_input) {
	int read_ret = read(ps->fd_pipe, piped_input, BUF_LEN);
	if (0 == read_ret) {
		printfef(true, "(): EOF reached on pipe \"%s\".", ps->o.pipePath);
		open_pipe(ps, r_fd);
	}
	else if (-1 == read_ret) {
		if (EAGAIN != errno)
			printfef(true, "(): Reading pipe \"%s\" failed: %d", ps->o.pipePath, errno);
		//exit(1);
	}

	return read_ret;
}

static void
flush_clients(session_t *ps) {
	// most if not all function calls in this function is POSIX only,
	// which is fair enough

	char *fullname = strdup(ps->o.pipePath2);
	char *dirpath = dirname(fullname);
	printfdf(false, "(): looking for client pipes in %s", dirpath);

	DIR *dirp = opendir(dirpath);
	if (dirp == NULL) {
		printfef(false, "Unable to read %s", dirpath);
		return;
	}

	regex_t regex;
	regcomp(&regex, ps->o.pipePath2, REG_EXTENDED);

	struct dirent* dr;
	while ((dr = readdir(dirp)))
	{
		if (dr->d_type == DT_FIFO) {
			char pipePath[strlen(dirpath) + 1 + strlen(dr->d_name) + 1];
			sprintf(pipePath, "%s/%s", dirpath, dr->d_name);
			if (regexec(&regex, pipePath, 0, NULL, 0) == 0) {
				printfdf(false, "(): flushing client pipe %s", pipePath);
				int fd = open(pipePath, O_WRONLY | O_NONBLOCK);
				int pipe_return = -1;
				int bytes_written = write(fd, &pipe_return, sizeof(int));
				if (bytes_written < sizeof(int)) {
					printfef(true, "(): cannot flush client pipe %s", pipePath);
				}
				close(fd);
			}
		}
	}
	closedir(dirp);
	regfree(&regex);
	free(fullname);
}

static void
send_string_command_to_daemon_via_fifo(
		const char *pipePath, char* command) {
	{
		int access_ret = 0;
		if ((access_ret = access(pipePath, W_OK))) {
			printfef(true, "(): Failed to access() pipe \"%s\": %d", pipePath, access_ret);
			perror("access");
			exit(1);
		}
	}

	// reserve space for first char and null terminator
	int command_len = strlen(command) + 2 + 10;
	if (command_len > BUF_LEN) {
		printfef(true, "(): attempting to send %d character commands, exceeding %d limit",
				command_len, BUF_LEN);
		exit(1);
	}
	printfdf(false, "(): sending string command to pipe of length %d", command_len);

	char final_cmd[command_len];
	sprintf(final_cmd, "%010i%c%s", getpid(), (char)strlen(command), command);
	printfdf(false, "(): string command: %s", final_cmd);

	int fp = open(pipePath, O_WRONLY);
	int bytes_written = write(fp, final_cmd, command_len);
	if (bytes_written < strlen(command))
		printfef(true, "(): incomplete command sent!");
	close(fp);
}

static void
send_command_to_daemon_via_fifo(char command, const char *pipePath) {
	// single character command is NOT NULL terminated
	char str[2];
	sprintf(str, "%c", command);
	send_string_command_to_daemon_via_fifo(pipePath, str);
}

static char*
receive_string_in_daemon_via_fifo(session_t *ps, struct pollfd *r_fd,
		int *pcmdlen) {
	char buffer[BUF_LEN];
	int ret_read = read_pipe(ps, r_fd, &buffer[0]);

	char cmdlen = 0;
	if (ret_read < 1 || (cmdlen = buffer[10]+2+10) > ret_read) {
		printfef(true, "(): stubbed command received");
		*pcmdlen = 0;
		return NULL;
	}

	*pcmdlen = ret_read;
	char *pbuffer = malloc(ret_read);
	memcpy(pbuffer, buffer, ret_read);
	return pbuffer;
}

static char
read_fifo_command(char *buffer, int cmdlen, int *increment,
		pid_t *pid, char *nparams, char **param, char ***str) {
	char pidbuffer[11];
	memcpy(pidbuffer, buffer, 10);
	pidbuffer[10] = '\0';
	*pid = atoi(pidbuffer);

	char *pbuffer = &buffer[11];
	char master_command = pbuffer[0];

	if ((master_command & PIPECMD_MULTI_BYTE) == 0) {
		printfdf(false, "(): received single byte command %d", master_command);
		*nparams = 0;
		param = NULL;
		str = NULL;
		*increment = 11 + 2;
	}
	else {
		if (cmdlen <= 1) {
			printfef(true, "(): multi-byte command stubbed");
			exit(1);
		}

		*nparams = pbuffer[1];
		printfdf(false, "(): received multi-byte command %d of %d parameters",
				master_command, *nparams);
		*param = malloc(*nparams * sizeof(char*));
		*str = malloc(*nparams * sizeof(char*));

		int k=2;
		*increment = 12 + 2;
		for (int i=0; i < *nparams; i++) {
			(*param)[i] = pbuffer[k];
			k++;
			(*increment)++;

			char nchar = pbuffer[k];
			printfdf(false, "(): parameter %d takes %d bytes...",
					(*param)[i], nchar);
			k++;
			(*increment)++;

			(*str)[i] = malloc(nchar+1);
			strncpy((*str)[i], pbuffer + k, nchar);
			(*str)[i][(int)nchar] = '\0';
			k += nchar;
			(*increment) += nchar;

			printfdf(false, "(): received parameter %d%s", (*param)[i], (*str)[i]);
		}
	}

	return master_command;
}

static inline void
exit_daemon(const char *pipePath) {
	printfdf(false, "(): Killing daemon...");
	send_command_to_daemon_via_fifo(PIPECMD_EXIT_DAEMON, pipePath);
}

static void
activate_via_fifo(session_t *ps, const char *pipePath) {
	char master_command = 0;
	if (ps->o.mode == PROGMODE_SWITCH)
		master_command |= PIPECMD_SWITCH;
	if (ps->o.mode == PROGMODE_EXPOSE)
		master_command |= PIPECMD_EXPOSE;
	if (ps->o.mode == PROGMODE_PAGING)
		master_command |= PIPECMD_PAGING;

	if (ps->o.focus_initial > 0)
		master_command |= PIPECMD_NEXT;
	else if (ps->o.focus_initial < 0)
		master_command |= PIPECMD_PREV;

	char command[BUF_LEN*2];
	char nparams = ps->o.multiselect
		+ (ps->o.wm_class != NULL) + (ps->o.wm_title != NULL)
		+ (ps->o.wm_status_count > 0)
		+ (ps->o.desktops != NULL)
		+ (ps->o.pivotkey != 0);
	if (ps->o.config_reload_path || ps->o.config_reload)
		nparams++;

	int cmd_len = 1;
	if (nparams > 0) {
		master_command |= PIPECMD_MULTI_BYTE;
		sprintf(command, "%c%c", master_command, nparams);
		cmd_len++;
	}
	else
		sprintf(command, "%c", master_command);

	if (ps->o.config_reload_path) {
		printfef(true, "(): loading new config file path \"%s\"", ps->o.config_path);
		cmd_len += 1+1+strlen(ps->o.config_path)+1;
		char cfg_cmd[1+1+strlen(ps->o.config_path)+1];
		sprintf(cfg_cmd, "%c%c%s",
				PIPEPRM_RELOAD_CONFIG_PATH,
				(char)strlen(ps->o.config_path), ps->o.config_path);
		strcat(command, cfg_cmd);
	}
	else if (ps->o.config_reload) {
		printfef(true, "(): reloading existing config file");
		cmd_len += 2;
		char cfg_cmd[2];
		sprintf(cfg_cmd, "%c", PIPEPRM_RELOAD_CONFIG);
		strcat(command, cfg_cmd);
	}

	if (ps->o.multiselect) {
		cmd_len += 2;
		char pivot_cmd[2];
		sprintf(pivot_cmd, "%c", PIPEPRM_MULTI_SELECT);
		strcat(command, pivot_cmd);
	}

	if (ps->o.wm_class) {
		cmd_len += 1+1+strlen(ps->o.wm_class)+1;
		char wm_cmd[1+1+strlen(ps->o.wm_class)+1];
		sprintf(wm_cmd, "%c%c%s",
				PIPEPRM_WM_CLASS, (char)strlen(ps->o.wm_class), ps->o.wm_class);
		strcat(command, wm_cmd);
	}

	if (ps->o.wm_title) {
		cmd_len += 1+1+strlen(ps->o.wm_title)+1;
		char wm_cmd[1+1+strlen(ps->o.wm_title)+1];
		sprintf(wm_cmd, "%c%c%s",
				PIPEPRM_WM_TITLE, (char)strlen(ps->o.wm_title), ps->o.wm_title);
		strcat(command, wm_cmd);
	}

	if (ps->o.wm_status) {
		cmd_len += 1+1+ps->o.wm_status_count+1;
		char status[1+1+ps->o.wm_status_count+1];
		sprintf(status, "%c%c%s",
				PIPEPRM_WM_STATUS, (char)ps->o.wm_status_count, ps->o.wm_status_str);
		strcat(command, status);
	}

	if (ps->o.desktops) {
		cmd_len += 1+1+strlen(ps->o.desktops)+1;
		char wm_cmd[1+1+strlen(ps->o.desktops)+1];
		sprintf(wm_cmd, "%c%c%s",
				PIPEPRM_DESKTOPS, (char)strlen(ps->o.desktops), ps->o.desktops);
		strcat(command, wm_cmd);
	}

	if (ps->o.pivotkey) {
		char pivot_cmd[4];
		sprintf(pivot_cmd, "%c%c%c", PIPEPRM_PIVOTING, 1, ps->o.pivotkey);
		strcat(command, pivot_cmd);
		cmd_len += 4;
	}

	if (cmd_len > BUF_LEN) {
		printfef(true, "(): attempting to send %d character commands, exceeding %d limit",
				cmd_len, BUF_LEN);
		exit(1);
	}
	send_string_command_to_daemon_via_fifo(pipePath, command);
}

static void
panel_map(ClientWin *cw)
{
	int border = 0;
	XSetWindowBorderWidth(cw->mainwin->ps->dpy, cw->mini.window, border);

	cw->mini.x = cw->src.x;
	cw->mini.y = cw->src.y;
	cw->mini.width = cw->src.width;
	cw->mini.height = cw->src.height;

	XMoveResizeWindow(cw->mainwin->ps->dpy, cw->mini.window, cw->mini.x - border, cw->mini.y - border, cw->mini.width, cw->mini.height);

	if(cw->pixmap)
		XFreePixmap(cw->mainwin->ps->dpy, cw->pixmap);

	if(cw->destination)
		XRenderFreePicture(cw->mainwin->ps->dpy, cw->destination);

	cw->pixmap = XCreatePixmap(cw->mainwin->ps->dpy, cw->mini.window, cw->mini.width, cw->mini.height, cw->mainwin->depth);
	XSetWindowBackgroundPixmap(cw->mainwin->ps->dpy, cw->mini.window, cw->pixmap);

	cw->destination = XRenderCreatePicture(cw->mainwin->ps->dpy, cw->pixmap, cw->mini.format, 0, 0);
}

static void
anime(
	MainWin *mw,
	dlist *clients,
	float timeslice
)
{
	float multiplier = 1.0 + timeslice * (mw->multiplier - 1.0);
	mainwin_transform(mw, multiplier);

	foreach_dlist (mw->clientondesktop) {
		ClientWin *cw = (ClientWin *) iter->data;
		clientwin_move(cw, multiplier, mw->xoff, mw->yoff, timeslice);
		clientwin_update2(cw);
		clientwin_map(cw);
	}
}

static void
count_clients(MainWin *mw)
{
	// Update the list of windows with correct z-ordering
	dlist *stack = dlist_first(wm_get_stack(mw->ps));
	mw->clients = dlist_first(mw->clients);

	// Terminate mw->clients that are no longer managed
	for (dlist *iter = mw->clients; iter; ) {
		ClientWin *cw = (ClientWin *) iter->data;
		if (dlist_find_data(stack, (void *) cw->wid_client)) {
			iter = iter->next;
		}
		else {
			dlist *tmp = iter->next;
			clientwin_destroy((ClientWin *) iter->data, True);
			mw->clients = dlist_remove(iter);
			iter = tmp;
		}
	}
	XFlush(mw->ps->dpy);

	// Add new mw->clients
	// This algorithm preserves correct z-order:
	// stack is ordered by correct z-order
	// and we re-order existing or new ClientWin to match that in stack
	// yes, it is O(n^2) complexity
	dlist *new_clients = NULL;

	foreach_dlist (stack) {
		dlist *insert_point = dlist_find(mw->clients, clientwin_cmp_func, iter->data);
		if (!insert_point && ((Window) iter->data) != mw->window) {
			ClientWin *cw = clientwin_create(mw, (Window)iter->data);
			if (!cw) continue;
			new_clients = dlist_add(new_clients, cw);
		}
		else {
			ClientWin *cw = (ClientWin *) insert_point->data;
			new_clients = dlist_add(new_clients, cw);
		}
	}

	dlist_free(stack);
	dlist_free(mw->clients);
	mw->clients = dlist_first(new_clients);
}

static void
count_and_filter_clients(MainWin *mw)
{
	count_clients(mw);

	foreach_dlist (mw->clients) {
		ClientWin *cw = iter->data;
		clientwin_update(cw);
	}

	// update mw->clientondesktop
	long desktop = wm_get_current_desktop(mw->ps);

	// given the client table, update the clientondesktop
	// the difference between mw->clients and mw->clientondesktop
	// is that mw->clients is all the client windows 
	// while mw->clientondesktop is only those in current virtual desktop
	// if that option is user supplied
	if (mw->clientondesktop) {
		dlist_free(mw->clientondesktop);
		mw->clientondesktop = NULL;
	}
	{
		dlist *tmp = dlist_first(dlist_find_all(mw->clients,
				(dlist_match_func) clientwin_filter_func, &desktop));
		mw->clientondesktop = tmp;
	}

	// update window panel list
	if (mw->panels) {
		dlist_free(mw->panels);
		mw->panels = NULL;
	}
	{
		dlist *tmp = dlist_first(dlist_find_all(mw->clients,
				(dlist_match_func) clientwin_validate_panel, &desktop));
		mw->panels = tmp;
	}

	return;
}

static void
init_focus(MainWin *mw, enum layoutmode layout, Window leader) {
	session_t *ps = mw->ps;

	// ordering of client windows list
	// is important for prev/next window selection
	mw->focuslist = dlist_dup(mw->clientondesktop);

	if (layout == LAYOUTMODE_EXPOSE && ps->o.exposeLayout != LAYOUT_XD)
		dlist_sort(mw->focuslist, sort_cw_by_column, 0);
	else
		dlist_reverse(mw->focuslist);

	dlist *iter = dlist_find(mw->focuslist, clientwin_cmp_func, (void *) leader);

	if (iter) {
		mw->client_to_focus_on_cancel = (ClientWin *) iter->data;
		mw->focuslist = dlist_cycle(mw->focuslist,
				dlist_index_of(mw->focuslist, iter));
		if (ps->o.focus_initial != 0 && iter)
		{
			if (ps->o.focus_initial < 0)
				ps->o.focus_initial = ps->o.focus_initial % dlist_len(mw->focuslist);

			mw->focuslist = dlist_cycle(mw->focuslist, ps->o.focus_initial);
		}
	}
	else {
		mw->client_to_focus_on_cancel = NULL;
	}

	dlist *first = dlist_first(mw->focuslist);
	if (first) {
		mw->client_to_focus = first->data;
		mw->client_to_focus->focused = 1;
		if (iter && !mw->mapped &&
				(ps->o.switchCycleDuringWait || ps->o.switchWaitDuration == 0)) {
			Window wid = mw->client_to_focus->wid_client;
			XRaiseWindow(ps->dpy, wid);
			XSetInputFocus(ps->dpy, wid, RevertToParent, CurrentTime);
			XFlush(ps->dpy);
		}
	}

	if (layout == LAYOUTMODE_SWITCH && ps->o.switchLayout == LAYOUT_COSMOS)
		dlist_sort(mw->focuslist, sort_cw_by_column, 0);
}

static void
calculatePanelBorders(MainWin *mw,
		int *x1, int *y1, int *x2, int *y2) {
	if (!mw->ps->o.panel_reserveSpace)
		return;

	// use heuristics to find panel borders
	// e.g. a panel on the bottom
	*x1 = 0;
	*y1 = 0;
	*x2 = mw->x + mw->width;
	*y2 = mw->y + mw->height;

	foreach_dlist(mw->panels) {
		ClientWin *cw = iter->data;
		if (cw->paneltype != WINTYPE_PANEL)
			continue;
		// assumed horizontal panel
		if (cw->src.width >= cw->src.height) {
			// assumed top panel
			if (cw->src.y < mw->y + mw->height / 2.0) {
				*y1 = MAX(*y1, cw->src.y + cw->src.height);
			}
			// assumed bottom panel
			else {
				*y2 = MIN(*y2, cw->src.y);
			}
		}
		// assumed vertical panel
		else {
			// assumed left panel
			if (cw->src.x < mw->x + mw->width / 2.0) {
				*x1 = MAX(*x1, cw->src.x + cw->src.width);
			}
			// assumed right panel
			else {
				*x2 = MIN(*x2, cw->src.x);
			}
		}
	}

	*x2 = mw->x + mw->width - *x2;
	*y2 = mw->y + mw->height - *y2;

	printfdf(false,"() panel framing calculations: (%d,%d) (%d,%d)", *x1, *y1, *x2, *y2);
}

static bool
init_layout(MainWin *mw, enum layoutmode layout, Window leader)
{
	unsigned int newwidth = 100, newheight = 100;
	if (mw->clientondesktop)
		layout_run(mw, mw->clientondesktop, &newwidth, &newheight, layout);

	int x1=0, y1=0, x2=0, y2=0;
	calculatePanelBorders(mw, &x1, &y1, &x2, &y2);
	newwidth += x1 + x2;
	newheight += y1 + y2;

	float multiplier = (float) (mw->width - 2 * mw->distance
			- x1 - x2) / newwidth;
	if (multiplier * newheight > mw->height - 2 * mw->distance)
		multiplier = (float) (mw->height - 2 * mw->distance
				- y1 - y2) / newheight;
	if (!mw->ps->o.allowUpscale)
		multiplier = MIN(multiplier, 1.0f);

	int xoff = (mw->width - x1 - x2 - (float)(newwidth
				- x1 - x2) * multiplier) / 2;
	int yoff = (mw->height - y1 - y2 - (float)(newheight
				- y1 - y2) * multiplier) / 2;

	mw->multiplier = multiplier;
	mw->xoff = xoff + x1;
	mw->yoff = yoff + y1;

	init_focus(mw, layout, leader);

	return true;
}

static bool
init_paging_layout(MainWin *mw, enum layoutmode layout, Window leader)
{
	int screencount = wm_get_desktops(mw->ps);
	if (screencount == -1)
		screencount = 1;
	int desktop_dim = ceil(sqrt(screencount));

	int desktop_width = mw->width;
	int desktop_height = mw->height;

#ifdef CFG_XINERAMA
	printfdf(false,"(): detecting %d screens and %d virtual desktops",
			mw->xin_screens, screencount);

	int minx = INT_MAX;
	int miny = INT_MAX;
	int maxx = INT_MIN;
	int maxy = INT_MIN;

	{
		XineramaScreenInfo *iter = mw->xin_info;
		for (int i = 0; i < mw->xin_screens; ++i)
		{
			minx = MIN(minx, iter->x_org);
			miny = MIN(miny, iter->y_org);
			maxx = MAX(maxx, iter->x_org + iter->width);
			maxy = MAX(maxy,  iter->y_org +iter->height);

			iter++;
		}
	}

	desktop_width = maxx - minx;
	desktop_height = maxy - miny;
#endif /* CFG_XINERAMA */

	// the paging layout is rectangular
	// such that screenwidth == ceil(sqrt(screencount))
	// and the screenheight == ceil(screencount / screenwidth)
	int screenwidth = desktop_dim;
	int screenheight = ceil((float)screencount / (float)screenwidth);

	foreach_dlist (mw->clients) {
		ClientWin *cw = (ClientWin *) iter->data;
		int win_desktop = wm_get_window_desktop(mw->ps, cw->wid_client);
		int current_desktop = wm_get_current_desktop(mw->ps);
		if (win_desktop == -1)
			win_desktop = current_desktop;

		int win_desktop_x = win_desktop % screenwidth;
		int win_desktop_y = win_desktop / screenwidth;

		int current_desktop_x = current_desktop % screenwidth;
		int current_desktop_y = current_desktop / screenwidth;

		cw->x = cw->src.x + win_desktop_x * (desktop_width + mw->distance);
		cw->y = cw->src.y + win_desktop_y * (desktop_height + mw->distance);

		cw->src.x += (win_desktop_x - current_desktop_x) * (desktop_width + mw->distance);
		cw->src.y += (win_desktop_y - current_desktop_y) * (desktop_height + mw->distance);
	}

    {
		int x1=0, y1=0, x2=0, y2=0;
		calculatePanelBorders(mw, &x1, &y1, &x2, &y2);
		unsigned int totalwidth = screenwidth * (desktop_width + mw->distance) - mw->distance;
		unsigned int totalheight = screenheight * (desktop_height + mw->distance) - mw->distance;
		totalwidth += x1 + x2;
		totalheight += y1 + y2;
		float multiplier = (float) (mw->width - 1 * mw->distance - x1 - x2)
			/ (float) totalwidth;
		if (multiplier * totalheight > mw->height - 1 * mw->distance - y1 - y2)
			multiplier = (float) (mw->height - 1 * mw->distance - y1 - y2)
				/ (float) totalheight;

		int xoff = (mw->width - x1 - x2 - (float)(totalwidth
					- x1 - x2)* multiplier) / 2;
		int yoff = (mw->height - y1 - y2 - (float)(totalheight
					- y1 - y2) * multiplier) / 2;

		mw->multiplier = multiplier;
		mw->xoff = xoff + x1;
		mw->yoff = yoff + y1;

		mw->desktoptransform.matrix[0][0] = 1.0;
		mw->desktoptransform.matrix[0][1] = 0.0;
		mw->desktoptransform.matrix[0][2] = xoff + x1;
		mw->desktoptransform.matrix[1][0] = 0.0;
		mw->desktoptransform.matrix[1][1] = 1.0;
		mw->desktoptransform.matrix[1][2] = yoff + y1;
		mw->desktoptransform.matrix[2][0] = 0.0;
		mw->desktoptransform.matrix[2][1] = 0.0;
		mw->desktoptransform.matrix[2][2] = 1.0;
	}

	// create windows which represent each virtual desktop
	int current_desktop = wm_get_current_desktop(mw->ps);
	for (int j=0, k=0; j<screenheight; j++) {
		for (int i=0; i<screenwidth && k<screencount; i++) {
			int desktop_idx = screenwidth * j + i;
			XSetWindowAttributes sattr = {
				.border_pixel = 0,
				.background_pixel = 0,
				.colormap = mw->colormap,
				.event_mask = ButtonPressMask | ButtonReleaseMask | KeyPressMask
					| KeyReleaseMask | PointerMotionMask | FocusChangeMask,
				.override_redirect = false,
                // exclude window frame
			};
			Window desktopwin = XCreateWindow(mw->ps->dpy,
					mw->window,
					0, 0, 0, 0,
					0, mw->depth, InputOnly, mw->visual,
					CWColormap | CWBackPixel | CWBorderPixel | CWEventMask | CWOverrideRedirect, &sattr);
			if (!desktopwin) return false;

			if (!mw->desktopwins)
				mw->desktopwins = dlist_add(NULL, &desktopwin);
			else
				mw->desktopwins = dlist_add(mw->desktopwins, &desktopwin);

			ClientWin *cw = clientwin_create(mw, desktopwin);
			if (!cw) return false;

			cw->slots = desktop_idx;
			cw->mode = CLIDISP_DESKTOP;

			{
				unsigned char *str = wm_get_desktop_name(mw->ps, desktop_idx);
				char *str1 = "skippy-xd page ";
				char *str2 = malloc(sizeof(char)
						* (strlen(str1) + strlen((char*) str) + 1));
				strcpy(str2, str1);
				strcat(str2, (char*) str);
				wm_wid_set_info(cw->mainwin->ps, cw->mini.window, (char *) str2, None);
				free(str);
				free(str2);
			}

			cw->zombie = false;

			cw->x = cw->src.x = (i * (desktop_width + mw->distance)) * mw->multiplier;
			cw->y = cw->src.y = (j * (desktop_height + mw->distance)) * mw->multiplier;
			cw->src.width = desktop_width;
			cw->src.height = desktop_height;

			if (!cw->redirected) {
				XCompositeRedirectWindow(mw->ps->dpy, cw->src.window,
						CompositeRedirectAutomatic);
				cw->redirected = true;
			}

			clientwin_prepmove(cw);
			clientwin_move(cw, mw->multiplier, mw->xoff, mw->yoff, 1);

			if (mw->ps->o.tooltip_show) {
				if (cw->tooltip)
					tooltip_destroy(cw->tooltip);
				cw->tooltip = tooltip_create(cw->mainwin);
			}

			if (!mw->dminis)
				mw->dminis = dlist_add(NULL, cw);
			else
				dlist_add(mw->dminis, cw);

			XRaiseWindow(mw->ps->dpy, cw->mini.window);

			if (cw->slots == current_desktop) {
				mw->client_to_focus = cw;
				mw->client_to_focus->focused = 1;

				{
					dlist *iter = dlist_find(mw->clientondesktop, clientwin_cmp_func, (void *) leader);
					if (!iter) {
						mw->client_to_focus_on_cancel = NULL;
					}
					else {
						mw->client_to_focus_on_cancel = (ClientWin *) iter->data;
					}
				}
			}
			k++;
		}
	}

	mw->focuslist = dlist_dup(mw->dminis);

	return true;
}

static void
desktopwin_map(ClientWin *cw)
{
	MainWin *mw = cw->mainwin;
	session_t *ps = mw->ps;

	free_damage(ps, &cw->damage);
	free_pixmap(ps, &cw->pixmap);

	if (ps->o.pseudoTrans)
		XUnmapWindow(ps->dpy, cw->mini.window);

	if (cw->origin)
		free_picture(ps, &cw->origin);
	if (ps->o.pseudoTrans) {
		XRenderPictureAttributes pa = { };
		cw->origin = XRenderCreatePicture(ps->dpy,
				mw->window, mw->format, CPSubwindowMode, &pa);
	}
	else {
		cw->origin = cw->pict_filled->pict;
	}
	XRenderSetPictureFilter(ps->dpy, cw->origin, FilterBest, 0, 0);

	if (ps->o.pseudoTrans)
	{
		mw->desktoptransform.matrix[0][2] += cw->x;
		mw->desktoptransform.matrix[1][2] += cw->y;
		XRenderSetPictureTransform(ps->dpy, cw->origin, &mw->desktoptransform);
		mw->desktoptransform.matrix[0][2] -= cw->x;
		mw->desktoptransform.matrix[1][2] -= cw->y;
	}

	cw->focused = cw == mw->client_to_focus;
	
	clientwin_render(cw);

	XMapWindow(ps->dpy, cw->mini.window);
	XRaiseWindow(ps->dpy, cw->mini.window);

	if (ps->o.tooltip_show) {
		clientwin_tooltip(cw);
		tooltip_handle(cw->tooltip, cw->focused);
	}
}

static bool
skippy_activate(MainWin *mw, enum layoutmode layout, Window leader)
{
	// Update the main window's geometry (and Xinerama info if applicable)
	mainwin_update(mw);

	mw->client_to_focus = NULL;

	count_and_filter_clients(mw);
	foreach_dlist(mw->clients) {
		clientwin_update((ClientWin *) iter->data);
		clientwin_update3((ClientWin *) iter->data);
		clientwin_update2((ClientWin *) iter->data);
	}

	if (layout == LAYOUTMODE_PAGING) {
		if (!init_paging_layout(mw, layout, leader)) {
			printfef(false, "(): failed.");
			return false;
		}
	}
	else {
		if (!init_layout(mw, layout, leader)) {
			printfef(false, "(): failed.");
			return false;
		}
	}

	foreach_dlist(mw->clients) {
		ClientWin *cw = iter->data;
		cw->x *= mw->multiplier;
		cw->y *= mw->multiplier;
		cw->paneltype = WINTYPE_WINDOW;
	}

	foreach_dlist(mw->panels) {
		ClientWin *cw = iter->data;
		cw->factor = 1;
		cw->paneltype = wm_identify_panel(mw->ps, cw->wid_client);
		clientwin_update(cw);
		clientwin_update3(cw);
		if (cw->paneltype == WINTYPE_DESKTOP)
			clientwin_move(cw, 1, cw->src.x, cw->src.y, 1);
		clientwin_update2(cw);
	}

	return true;
}

static void
mainloop(session_t *ps, bool activate_on_start) {
	MainWin *mw = NULL;
	bool die = false;
	bool activate = activate_on_start;
	bool pending_damage = false;
	long last_rendered = 0L;
	long last_animated = 0L;
	enum layoutmode layout = LAYOUTMODE_EXPOSE;
	bool toggling = !ps->o.pivotkey;
	bool animating = activate;
	long first_animated = 0L;
	bool first_animating = false;
	pid_t trigger_client = 0;
	bool focus_stolen = false;
	Window leader = 0;
	bool switchdesktop = false;

	switch (ps->o.mode) {
		case PROGMODE_SWITCH:
			layout = LAYOUTMODE_SWITCH;
		case PROGMODE_EXPOSE:
			layout = LAYOUTMODE_EXPOSE;
			break;
		case PROGMODE_PAGING:
			layout = LAYOUTMODE_PAGING;
			break;
		default:
			ps->o.mode = PROGMODE_EXPOSE;
			layout = LAYOUTMODE_EXPOSE;
			break;
	}

	struct pollfd r_fd[2] = {
		{
			.fd = ConnectionNumber(ps->dpy),
			.events = POLLIN,
		},
		{
			.fd = ps->fd_pipe,
			.events = POLLIN,
		},
	};

	count_and_filter_clients(ps->mainwin);

	foreach_dlist(ps->mainwin->clients) {
		clientwin_update((ClientWin *) iter->data);
		clientwin_update3((ClientWin *) iter->data);
		clientwin_update2((ClientWin *) iter->data);
	}

	while (true) {
		// Clear revents in pollfd
		for (int i = 0; i < CARR_LEN(r_fd); ++i)
			r_fd[i].revents = 0;

		// Activation goes first, so that it won't be delayed by poll()
		if (!mw && activate) {
			assert(ps->mainwin);
			activate = false;
			leader = wm_get_focused(ps);

			if (skippy_activate(ps->mainwin, layout, leader)) {
				last_animated = last_rendered = time_in_millis();
				mw = ps->mainwin;
				pending_damage = false;
				first_animated = time_in_millis();
				first_animating = true;
			}
		}
		if (mw)
			activate = false;

		// Main window destruction, before poll()
		if (mw && die) {
			printfdf(false,"(): selecting/canceling and returning to background");

			animating = false;

			// Unmap the main window and all clients, to make sure focus doesn't fall out
			// when we start setting focus on client window
			mainwin_unmap(mw);
			foreach_dlist(mw->clientondesktop) { clientwin_unmap((ClientWin *) iter->data); }
			XSync(ps->dpy, False);

			// Focus the client window only after the main window get unmapped and
			// keyboard gets ungrabbed.

			int selected = -1;
			if (mw->client_to_focus && layout != LAYOUTMODE_PAGING) {
				if (!mw->refocus) {
					dlist *iter = dlist_find(ps->mainwin->clients,
							clientwin_cmp_func,
							(void *) mw->client_to_focus);
					if (iter) {
						childwin_focus(mw->client_to_focus);
						selected = mw->client_to_focus->wid_client;
					}
				}
				else {
					dlist *iter = dlist_find(ps->mainwin->clients,
							clientwin_cmp_func,
							(void *) mw->client_to_focus_on_cancel);
					if (iter) {
						childwin_focus(mw->client_to_focus_on_cancel);
						selected = mw->client_to_focus_on_cancel->wid_client;
					}
				}
			}

			if (mw->client_to_focus && layout == LAYOUTMODE_PAGING ) {
				if (!mw->refocus &&
						mw->client_to_focus->slots
						!= wm_get_current_desktop(ps)) {
					wm_set_desktop_ewmh(ps, mw->client_to_focus->slots);
					selected = mw->client_to_focus->slots;
				}
				else {
					if (mw->client_to_focus_on_cancel){
						childwin_focus(mw->client_to_focus_on_cancel);
					}
					else {
						// this trick does not work
						// when there is only one virtual desktop
						wm_set_desktop_ewmh(ps,
								(wm_get_current_desktop(ps)+1)
								% wm_get_desktops(mw->ps));
						wm_set_desktop_ewmh(ps, wm_get_current_desktop(ps));
					}
					selected = wm_get_current_desktop(ps);
				}
			}

			char pipe_return[1024];
			sprintf(pipe_return, "%i", selected);

			if (ps->o.multiselect)
			{
				pipe_return[0] = '\0';
				bool firstprint = true;
				dlist *iter = mw->clientondesktop;
				if (layout == LAYOUTMODE_PAGING)
					iter = mw->dminis;
				for (; iter; iter = iter->next) {
					ClientWin *cw = iter->data;
					unsigned long client = cw->wid_client;
					if (layout == LAYOUTMODE_PAGING)
						client = cw->slots;
					if (cw->multiselect) {
						char wid[1024];
						if (firstprint) {
							sprintf(pipe_return, "%lu", client);
							firstprint = false;
						}
						else {
							sprintf(wid, " %lu", client);
							strcat(pipe_return, wid);
						}
					}
				}
			}

			if (trigger_client != 0)
				returnToClient(ps, trigger_client, pipe_return);
			else
				printf("%s\n", pipe_return);

			ps->o.multiselect = false;
			mw->refocus = false;
			mw->client_to_focus = NULL;
			pending_damage = false;

			// Cleanup
			foreach_dlist (mw->clientondesktop) {
				ClientWin *cw = iter->data;
				cw->multiselect = false;
			}
			foreach_dlist (mw->dminis) {
				ClientWin *cw = iter->data;
				cw->multiselect = false;
			}

			dlist_free(mw->clientondesktop);
			mw->clientondesktop = 0;
			dlist_free(mw->focuslist);

			// free all mini desktop representations
			dlist_free_with_func(mw->dminis, (dlist_free_func) clientwin_destroy);
			mw->dminis = NULL;

			foreach_dlist (mw->desktopwins) {
				XDestroyWindow(ps->dpy, (Window) (iter->data));
			}
			dlist_free(mw->desktopwins);
			mw->desktopwins = NULL;

			foreach_dlist(mw->panels) {
				clientwin_unmap(iter->data);
			}

			// Catch all errors, but remove all events
			XSync(ps->dpy, False);
			XSync(ps->dpy, True);

			if (switchdesktop) {
				wm_set_desktop_ewmh(ps,
						(wm_get_current_desktop(ps) + ps->o.focus_initial)
						% wm_get_desktops(mw->ps));
				animating = activate = true;
				switchdesktop = false;
			}

			mw = NULL;
		}
		if (!mw)
			die = false;
		if (activate_on_start && !mw)
			return;

		// poll whether pivoting key is being pressed
		// if not, then die
		// the placement of this code allows MainWin not to map
		// so that previews may not show for switch
		// when the pivot key is held for only short time
		if (mw && !toggling)
		{
			bool pivotTerminate = false;
			char keys[32];
			XQueryKeymap(ps->dpy, keys);
			int slot = ps->o.pivotkey / 8;
			char mask = 1 << (ps->o.pivotkey % 8);
			pivotTerminate = !(keys[slot] & mask);

			if (pivotTerminate)
				die = true;
		}

		// animation!
		if (mw && animating) {
			int timeslice = time_in_millis() - first_animated;
			int starttime = last_animated + (1000.0 / ps->o.animationRefresh) - first_animated;
			int stabletime = ps->o.animationDuration;
			if (layout == LAYOUTMODE_SWITCH) {
				if (ps->o.switchWaitDuration == 0) {
					starttime = stabletime = timeslice + 1;
				}
				else if (ps->o.switchLayout == LAYOUT_XD) {
					starttime = ps->o.switchWaitDuration + 1;
					stabletime = ps->o.switchWaitDuration;
				}
				else if (ps->o.switchLayout == LAYOUT_COSMOS) {
					starttime += ps->o.switchWaitDuration;
					stabletime += ps->o.switchWaitDuration;
				}
			}
			if (starttime < timeslice && timeslice < stabletime) {
				if (!mw->mapped)
					mainwin_map(mw);

				if (first_animating) {
					foreach_dlist (mw->clientondesktop) {
						ClientWin *cw = iter->data;
						clientwin_prepmove(cw);
					}
					foreach_dlist (mw->panels) {
						ClientWin *cw = iter->data;
						panel_map(cw);
						clientwin_map(cw);
					}

					first_animating = false;
				}

				if (layout == LAYOUTMODE_SWITCH
				&& ps->o.switchLayout == LAYOUT_COSMOS)
					timeslice -= ps->o.switchWaitDuration;

				anime(ps->mainwin, ps->mainwin->clients,
					((float)timeslice)/(float)ps->o.animationDuration);
				last_animated = last_rendered = time_in_millis();

				if (layout == LAYOUTMODE_SWITCH
				&& ps->o.switchLayout == LAYOUT_COSMOS)
					last_animated = last_rendered -= ps->o.switchWaitDuration;

				XFlush(ps->dpy);
			}
			else if (timeslice >= stabletime) {
				if (!mw->mapped)
					mainwin_map(mw);

				if (first_animating) {
					foreach_dlist (mw->clientondesktop) {
						ClientWin *cw = iter->data;
						clientwin_prepmove(cw);
					}
					foreach_dlist (mw->panels) {
						ClientWin *cw = iter->data;
						panel_map(cw);
						clientwin_map(cw);
					}

					first_animating = false;
				}

				if (layout == LAYOUTMODE_PAGING && mw->ps->o.preservePages) {
					foreach_dlist (mw->dminis) {
						ClientWin *cw = (ClientWin *) iter->data;
						XRenderComposite(mw->ps->dpy,
								PictOpSrc, mw->ps->o.from,
								None, mw->background,
								cw->x + mw->xoff + mw->x, cw->y + mw->yoff + mw->y,
								0, 0,
								cw->x + mw->xoff, cw->y + mw->yoff,
								cw->src.width * mw->multiplier,
								cw->src.height * mw->multiplier);
						XClearWindow(ps->dpy, mw->window);
					}
				}

				anime(ps->mainwin, ps->mainwin->clients, 1);
				animating = false;
				last_animated = last_rendered = time_in_millis();

				if (layout == LAYOUTMODE_PAGING) {
					foreach_dlist (mw->dminis) {
						clientwin_update2(iter->data);
						desktopwin_map(((ClientWin *) iter->data));
					}
				}

				XFlush(ps->dpy);

				focus_miniw_adv(ps, mw->client_to_focus,
						ps->o.moveMouse);
			}

			if (layout != LAYOUTMODE_SWITCH ||
					!(ps->o.switchCycleDuringWait || ps->o.switchWaitDuration == 0))
				continue; // while animating, do not allow user actions
		}

		if (layout != LAYOUTMODE_SWITCH
				&& !toggling && ps->o.pivotLockingTime > 0
				&& time_in_millis() >= first_animated + ps->o.pivotLockingTime) {
			printfdf(false, "(): pivot locking at %d", ps->o.pivotLockingTime);
			toggling = true;
		}

		// Process X events
		int num_events = 0;
		XEvent ev = { };
		while ((num_events = XEventsQueued(ps->dpy, QueuedAfterReading)))
		{
			XNextEvent(ps->dpy, &ev);

#ifdef DEBUG_EVENTS
			ev_dump(ps, mw, &ev);
#endif
			Window wid = ev_window(ps, &ev);

			if (mw && MotionNotify == ev.type)
			{
				// when mouse move within a client window, focus on it
				if (wid) {
					dlist *iter = mw->clientondesktop;
					if (layout == LAYOUTMODE_PAGING)
						iter = mw->dminis;
					for (; iter; iter = iter->next) {
						ClientWin *cw = (ClientWin *) iter->data;
						if (cw->mini.window == wid) {
							if (!(POLLIN & r_fd[1].revents)) {
								die = clientwin_handle(cw, &ev);
							}
						}
					}
				}

				// Speed up responsiveness when the user is moving the mouse around
				// The queue gets filled up with consquetive MotionNotify events
				// discard all except the last MotionNotify event in a contiguous block of MotionNotify events

				num_events--;
				XEvent ev_next = { };
				while(num_events > 0)
				{
					XPeekEvent(ps->dpy, &ev_next);

					if(ev_next.type != MotionNotify)
						break;

					XNextEvent(ps->dpy, &ev);
					wid = ev_window(ps, &ev);

					num_events--;
				}
			}
			else if (mw && ev.type == DestroyNotify) {
				printfdf(false, "(): else if (ev.type == DestroyNotify) {");
				count_and_filter_clients(ps->mainwin);
				if (!mw->clientondesktop) {
					printfdf(false, "(): Last client window destroyed/unmapped, "
							"exiting.");
					die = true;
					mw->client_to_focus = NULL;
				}
			}
			else if (!mw && (ev.type == ConfigureNotify || ev.type == PropertyNotify)) {
				printfdf(false,
						"(): else if (ev.type == ConfigureNotify || ev.type == PropertyNotify) {");
				dlist *iter = (wid ? dlist_find(ps->mainwin->clients, clientwin_cmp_func, (void *) wid): NULL);
				ClientWin *cw = NULL;
				if (iter)
					cw = (ClientWin *) iter->data;
				if (cw) {
					clientwin_update(cw);
					clientwin_update3(cw);
					clientwin_update2(cw);
				}
            }
			else if (ev.type == CreateNotify || ev.type == MapNotify || ev.type == UnmapNotify) {
				printfdf(false, "(): else if (ev.type == CreateNotify || ev.type == MapNotify || ev.type == UnmapNotify) {");
				count_and_filter_clients(ps->mainwin);
				dlist *iter = (wid ? dlist_find(ps->mainwin->clients, clientwin_cmp_func, (void *) wid): NULL);
				if (iter) {
					ClientWin *cw = (ClientWin *) iter->data;
					clientwin_update(cw);
					clientwin_update3(cw);
					clientwin_update2(cw);
				}
				num_events--;

				{
					int ev_prev = ev.type;
					XEvent ev_next = { };
					while(num_events > 0)
					{
						XPeekEvent(ps->dpy, &ev_next);

						if(ev_next.type != ev_prev)
							break;

						XNextEvent(ps->dpy, &ev);
						wid = ev_window(ps, &ev);

						num_events--;
						dlist *iter = (wid ? dlist_find(ps->mainwin->clients,
								clientwin_cmp_func, (void *) wid): NULL);
						if (iter) {
							ClientWin *cw = (ClientWin *) iter->data;
							clientwin_update(cw);
							clientwin_update3(cw);
							clientwin_update2(cw);
						}
					}
				}
			}
			else if (mw && (ps->xinfo.damage_ev_base + XDamageNotify == ev.type)) {
				//printfdf(false, "(): else if (ev.type == XDamageNotify) {");
				pending_damage = true;
				dlist *iter = dlist_find(ps->mainwin->clients,
						clientwin_cmp_func, (void *) wid);
				if (iter) {
					((ClientWin *)iter->data)->damaged = true;
				}
				num_events--;

				{
					int ev_prev = ev.type;
					XEvent ev_next = { };
					while (num_events > 0)
					{
						XPeekEvent(ps->dpy, &ev_next);
						Window wid2 = ev_window(ps, &ev_next);

						if(ev_next.type != ev_prev || wid2 != wid)
							break;

						XNextEvent(ps->dpy, &ev);
						num_events--;
					}
				}
			}
			else if (mw && wid == mw->window && !die) {
				if (ev.type == FocusOut)
					focus_stolen = true;
				if (ev.type == FocusIn)
					focus_stolen = false;
				die = mainwin_handle(mw, &ev);
			}
			else if (mw && wid) {
				if (ev.type == FocusOut)
					focus_stolen = true;
				if (ev.type == FocusIn)
					focus_stolen = false;

				bool processing = true;
				dlist *iter = mw->clientondesktop;
				if (layout == LAYOUTMODE_PAGING)
					iter = mw->dminis;
				for (; iter && processing; iter = iter->next) {
					ClientWin *cw = (ClientWin *) iter->data;
					if (cw->mini.window == wid) {
						if (!(POLLIN & r_fd[1].revents)
								&& ((layout != LAYOUTMODE_PAGING)
								// do not process these excessive paging events
								|| (ev.type != Expose
								 && ev.type != GraphicsExpose
								 && ev.type != MotionNotify
								 && ev.type != EnterNotify
								 && ev.type != LeaveNotify
								 && ev.type != CreateNotify
								 && ev.type != CirculateNotify
								 && ev.type != ConfigureNotify
								 && ev.type != GravityNotify
								 && ev.type != ReparentNotify
								))) {

							die = clientwin_handle(cw, &ev);
							if (layout == LAYOUTMODE_PAGING) {
								cw->damaged = true;
								pending_damage = true;
							}
						}
						processing = false;
					}
				}
				for (iter = mw->panels; iter && processing; iter = iter->next) {
					ClientWin *cw = (ClientWin *) iter->data;
					if (cw->mini.window == wid) {
						die = mainwin_handle(mw, &ev);
						processing = false;
					}
				}
			}
		}

		// prevent focus stealing by newly mapped window
		// by checking for a FocusOut/FocusIn event pair
		if (mw && ps->o.enforceFocus && focus_stolen) {
			printfdf(false,"(): skippy-xd focus stolen... take back focus");
			XSetInputFocus(ps->dpy, mw->window,
					RevertToParent, CurrentTime);
			if (mw->client_to_focus) {
				mw->client_to_focus->focused = true;
				clientwin_render(mw->client_to_focus);
			}
			focus_stolen = false;
		}

		// Do delayed painting if it's active
		if (mw && pending_damage && !die) {
			//printfdf(false, "(): delayed painting");
			pending_damage = false;
			foreach_dlist(mw->clientondesktop) {
				if (((ClientWin *) iter->data)->damaged)
					clientwin_repair(iter->data);
			}

			foreach_dlist(mw->panels) {
				clientwin_repair((ClientWin *) iter->data);
			}

			if (layout == LAYOUTMODE_PAGING) {
				foreach_dlist (mw->dminis) {
					ClientWin *cw = (ClientWin *) iter->data;
					// with pseudo-transparency,
					// some desktops never receive refresh events
					// so we need to refresh all desktops
					if (cw->damaged || ps->o.pseudoTrans) {
						clientwin_update2(cw);
						desktopwin_map(cw);
						cw->damaged = false;
					}
				}
			}
			last_rendered = time_in_millis();
		}

		// Discards all events so that poll() won't constantly hit data to read
		//XSync(ps->dpy, True);
		//assert(!XEventsQueued(ps->dpy, QueuedAfterReading));

		last_rendered = time_in_millis();
		XFlush(ps->dpy);

		// Poll for events
		int timeout = -1;
		if (mw && (!toggling || mw->pressed_key || mw->pressed_mouse)) {
			timeout = (1.0 / 60.0) * 1000.0 + time_in_millis() - last_rendered;
			if (timeout < 0)
				timeout = 0;
		}
		if (animating)
			timeout = 0;
		poll(r_fd, (r_fd[1].fd >= 0 ? 2: 1), timeout);

		// Handle daemon commands
		if (POLLIN & r_fd[1].revents) {
			int cmdlen = 0, increment = 0;
			char *pipestr = receive_string_in_daemon_via_fifo(ps, r_fd, &cmdlen);
			char *pipestr2 = pipestr;

			while (cmdlen > 0) {
				char nparams=0, *param=0, **str=0;
				pid_t pid = 0;
				char piped_input = read_fifo_command(pipestr2, cmdlen, &increment,
						&pid, &nparams, &param, &str);
				printfdf(false, "(): Received pipe command: %d from %010i",
						piped_input, pid);

				if (piped_input & PIPECMD_EXIT_DAEMON) {
					printfdf(true, "(): Exit command received, killing daemon...");
					unlink(ps->o.pipePath);

					returnToClient(ps, pid, "-1");

					return;
				}

				for (int i=0; i<nparams; i++) {
					if (param[i] == PIPEPRM_RELOAD_CONFIG_PATH) {
						if (ps->o.config_path)
							free(ps->o.config_path);
						ps->o.config_path = mstrdup(str[i]);
						load_config_file(ps);
						mainwin_reload(ps, ps->mainwin);
					}
					if (param[i] == PIPEPRM_RELOAD_CONFIG) {
						load_config_file(ps);
						mainwin_reload(ps, ps->mainwin);
					}
				}

				ps->o.focus_initial = -((piped_input & PIPECMD_PREV) > 0)
					+ ((piped_input & PIPECMD_NEXT) > 0);

				if (!mw /*|| !mw->mapped*/)
				{
					bool forget_activating = false;
					if (piped_input & PIPECMD_SWITCH) {
						ps->o.mode = PROGMODE_SWITCH;
						layout = LAYOUTMODE_SWITCH;
					}
					else if (piped_input & PIPECMD_EXPOSE) {
						ps->o.mode = PROGMODE_EXPOSE;
						layout = LAYOUTMODE_EXPOSE;
					}
					else if (piped_input & PIPECMD_PAGING) {
						ps->o.mode = PROGMODE_PAGING;
						layout = LAYOUTMODE_PAGING;
					}
					else
						forget_activating = true;

					if (!forget_activating) {
						if (ps->o.wm_class) {
							free(ps->o.wm_class);
							ps->o.wm_class = NULL;
						}
						if (ps->o.wm_title) {
							free(ps->o.wm_title);
							ps->o.wm_title = NULL;
						}
						if (ps->o.wm_status) {
							ps->o.wm_status_count = 0;
							free(ps->o.wm_status);
							ps->o.wm_status = NULL;
							free(ps->o.wm_status_str);
							ps->o.wm_status_str = NULL;
						}
						if (ps->o.desktops) {
							free(ps->o.desktops);
							ps->o.desktops = NULL;
						}

						animating = activate = true;

						toggling = true;
						for (int i=0; i<nparams; i++) {
							if (param[i] == PIPEPRM_MULTI_SELECT) {
								printfdf(false,"(): multi-select mode");
								ps->o.multiselect = true;
							}

							if (param[i] & PIPEPRM_WM_CLASS) {
								if (ps->o.wm_class)
									free(ps->o.wm_class);
								ps->o.wm_class = mstrdup(str[i]);
								printfdf(false, "(): receiving new wm_class=%s",
										ps->o.wm_class);
							}

							if (param[i] & PIPEPRM_WM_TITLE) {
								if (ps->o.wm_title)
									free(ps->o.wm_title);
								ps->o.wm_title = mstrdup(str[i]);
								printfdf(false, "(): receiving new wm_title=%s",
										ps->o.wm_title);
							}

							if (param[i] & PIPEPRM_PIVOTING) {
								ps->o.pivotkey = str[i][0];
								printfdf(false, "(): receiving new pivot key=%d",ps->o.pivotkey);
								toggling = false;
							}

							if (param[i] & PIPEPRM_WM_STATUS) {
								if (ps->o.wm_status) {
									free(ps->o.wm_status);
									free(ps->o.wm_status_str);
								}
								ps->o.wm_status_str = mstrdup(str[i]);
								ps->o.wm_status_count = strlen(ps->o.wm_status_str);
								ps->o.wm_status = malloc(ps->o.wm_status_count * sizeof(int));
								for (int j=0; j<ps->o.wm_status_count; j++)
									ps->o.wm_status[j] = ps->o.wm_status_str[j];
							}

							if (param[i] & PIPEPRM_DESKTOPS) {
								if (ps->o.desktops)
									free(ps->o.desktops);
								ps->o.desktops = mstrdup(str[i]);
								printfdf(false, "(): receiving new desktops=%s",
										ps->o.desktops);
							}
						}

						trigger_client = pid;
						printfdf(false, "(): skippy activating: metaphor=%d", layout);
					}
				}
				// parameter == 0, toggle
				// otherwise shift window focus
				else if (mw && ps->o.focus_initial == 0) {
					if (toggling) {
						printfdf(false, "(): toggling skippy off");
						mw->refocus = die = true;
					}
				}
				else if (mw /*&& mw->mapped*/)
				{
					printfdf(false, "(): cycling window");
					fflush(stdout);fflush(stderr);

					if ((layout == LAYOUTMODE_SWITCH && ps->o.switchCycleDesktops)
					 || (layout == LAYOUTMODE_EXPOSE && ps->o.exposeCycleDesktops))
					{
						int focusindex = 0;
						if (mw->client_to_focus) {
							dlist *search = dlist_first(mw->focuslist);
							ClientWin *searchdata = search->data;
							while (searchdata != mw->client_to_focus) {
								search = search->next;
								searchdata = search->data;
								focusindex++;
							}
						}
						if (0 > focusindex + ps->o.focus_initial
						|| focusindex + ps->o.focus_initial >= dlist_len(mw->focuslist)) {
							die = true;
							switchdesktop = true;
						}
					}

					int oldfocus = ps->o.focus_initial;
					if (ps->o.focus_initial < 0)
						ps->o.focus_initial = dlist_len(mw->focuslist) + ps->o.focus_initial;

					while (ps->o.focus_initial > 0 && mw->client_to_focus) {
						focus_miniw_next(ps, mw->client_to_focus);
						if (!mw->mapped &&
								(ps->o.switchCycleDuringWait || ps->o.switchWaitDuration == 0))
							childwin_focus(mw->client_to_focus);
						ps->o.focus_initial--;
					}
					ps->o.focus_initial = oldfocus;

					if (mw->client_to_focus)
						clientwin_render(mw->client_to_focus);
				}

				// if the client did not trigger activation, return to it immediately
				if (mw) {
					returnToClient(ps, pid, "-1");
				}

				// free receive_string_in_daemon_via_fifo() paramters
				if (param)
					free(param);
				for (int i=0; i<nparams; i++)
					if (str[i])
						free(str[i]);
				if (str)
					free(str);

				pipestr2 += increment;
				cmdlen -= increment;
			}
			if (pipestr)
				free(pipestr);
		}

		if (POLLHUP & r_fd[1].revents) {
			printfdf(false, "(): PIPEHUP on pipe \"%s\".", ps->o.pipePath);
			open_pipe(ps, r_fd);
		}
	}
}

/**
 * @brief Xlib error handler function.
 */
static int
xerror(Display *dpy, XErrorEvent *ev) {
	session_t * const ps = ps_g;

	int o;
	const char *name = "Unknown";

#define CASESTRRET2(s)	 case s: name = #s; break

	o = ev->error_code - ps->xinfo.fixes_err_base;
	switch (o) {
		CASESTRRET2(BadRegion);
	}

	o = ev->error_code - ps->xinfo.damage_err_base;
	switch (o) {
		CASESTRRET2(BadDamage);
	}

	o = ev->error_code - ps->xinfo.render_err_base;
	switch (o) {
		CASESTRRET2(BadPictFormat);
		CASESTRRET2(BadPicture);
		CASESTRRET2(BadPictOp);
		CASESTRRET2(BadGlyphSet);
		CASESTRRET2(BadGlyph);
	}

	switch (ev->error_code) {
		CASESTRRET2(BadAccess);
		CASESTRRET2(BadAlloc);
		CASESTRRET2(BadAtom);
		CASESTRRET2(BadColor);
		CASESTRRET2(BadCursor);
		CASESTRRET2(BadDrawable);
		CASESTRRET2(BadFont);
		CASESTRRET2(BadGC);
		CASESTRRET2(BadIDChoice);
		CASESTRRET2(BadImplementation);
		CASESTRRET2(BadLength);
		CASESTRRET2(BadMatch);
		CASESTRRET2(BadName);
		CASESTRRET2(BadPixmap);
		CASESTRRET2(BadRequest);
		CASESTRRET2(BadValue);
		CASESTRRET2(BadWindow);
	}

#undef CASESTRRET2

	print_timestamp(ps);
	{
		char buf[128] = "";
		XGetErrorText(ps->dpy, ev->error_code, buf, 128);
		printfef(false, "(): error %d (%s) request %d minor %d serial %lu (\"%s\")",
				ev->error_code, name, ev->request_code,
				ev->minor_code, ev->serial, buf);
	}

	return 0;
}

#ifndef SKIPPYXD_VERSION
#define SKIPPYXD_VERSION "unknown"
#endif

static void
show_help() {
	fputs("skippy-xd " SKIPPYXD_VERSION "\n"
			"Usage: skippy-xd [command]\n\n"
			"The available commands are:\n"
			"\n"
			"  [no command]        - activate expose once without daemon.\n"
			"\n"
			"  --help              - show this message.\n"
			"  --debug-log         - enable debugging logs.\n"
			"\n"
			"  --config            - load/reload configuration file from specified path.\n"
			"  --config-reload     - reload configuration file without changing path.\n"
			"\n"
			"  --start-daemon      - run as daemon mode.\n"
			"  --stop-daemon       - terminate skippy-xd daemon.\n"
			"\n"
			"  --switch            - connect to daemon and activate switch.\n"
			"  --expose            - connect to daemon and activate expose.\n"
			"  --paging            - connect to daemon and activate paging.\n"
			"\n"
			"  --multi-select      - select multiple windows and return all IDs.\n"
			"\n"
			"  --wm-class          - display only windows of specific class.\n"
			"  --wm-title          - display only windows of specific title.\n"
			"  --wm-status         - display only windows with specified status:\n"
			"                          sticky, shaded, minimized, float,\n"
			"                          maximized_vert, maximized_horz, maximized\n"
			"  --desktop           - display only windows on specific virtual desktops.\n"
			"                          use '-1' to show windows from all desktops.\n"
			"\n"
			"  --toggle            - activate via toggle mode.\n"
			"  --pivot             - activate via pivot mode with specified pivot key.\n"
			"\n"
			"  --prev              - focus on the previous window.\n"
			"  --next              - focus on the next window.\n"
			"\n"
			, stdout);
#ifdef CFG_LIBPNG
	spng_about(stdout);
#endif
}

static inline bool
init_xexts(session_t *ps) {
	Display * const dpy = ps->dpy;
#ifdef CFG_XINERAMA
	ps->xinfo.xinerama_exist = XineramaQueryExtension(dpy,
			&ps->xinfo.xinerama_ev_base, &ps->xinfo.xinerama_err_base);
	if (ps->o.runAsDaemon)
		printfef(true, "(): Xinerama extension: %s",
			(ps->xinfo.xinerama_exist ? "yes": "no"));
#endif /* CFG_XINERAMA */

	if(!XDamageQueryExtension(dpy,
				&ps->xinfo.damage_ev_base, &ps->xinfo.damage_err_base)) {
		printfef(true, "(): FATAL: XDamage extension not found.");
		return false;
	}

	if(!XCompositeQueryExtension(dpy, &ps->xinfo.composite_ev_base,
				&ps->xinfo.composite_err_base)) {
		printfef(true, "(): FATAL: XComposite extension not found.");
		return false;
	}

	if(!XRenderQueryExtension(dpy,
				&ps->xinfo.render_ev_base, &ps->xinfo.render_err_base)) {
		printfef(true, "(): FATAL: XRender extension not found.");
		return false;
	}

	if(!XFixesQueryExtension(dpy,
				&ps->xinfo.fixes_ev_base, &ps->xinfo.fixes_err_base)) {
		printfef(true, "(): FATAL: XFixes extension not found.");
		return false;
	}

	return true;
}

/**
 * @brief Check if a file exists.
 *
 * access() may not actually be reliable as according to glibc manual it uses
 * real user ID instead of effective user ID, but stat() is just too costly
 * for this purpose.
 */
static inline bool
fexists(const char *path) {
	return !access(path, F_OK);
}

/**
 * @brief Find path to configuration file.
 */
static inline char *
get_cfg_path(void) {
	static const char *PATH_CONFIG_HOME_SUFFIX = "/skippy-xd/skippy-xd.rc";
	static const char *PATH_CONFIG_HOME = "/.config";
	static const char *PATH_CONFIG_SYS_SUFFIX = "/skippy-xd.rc";
	static const char *PATH_CONFIG_SYS = "/etc/xdg";

	char *path = NULL;
	const char *dir = NULL;

	// Check $XDG_CONFIG_HOME
	if ((dir = getenv("XDG_CONFIG_HOME")) && strlen(dir)) {
		path = mstrjoin(dir, PATH_CONFIG_HOME_SUFFIX);
		if (fexists(path))
			goto get_cfg_path_found;
		free(path);
		path = NULL;
	}
	// Check ~/.config
	if ((dir = getenv("HOME")) && strlen(dir)) {
		path = mstrjoin3(dir, PATH_CONFIG_HOME, PATH_CONFIG_HOME_SUFFIX);
		if (fexists(path))
			goto get_cfg_path_found;
		free(path);
		path = NULL;
	}

	// Look in env $XDG_CONFIG_DIRS
	if ((dir = getenv("XDG_CONFIG_DIRS")))
	{
		char *dir_free = mstrdup(dir);
		char *part = strtok(dir_free, ":");
		while (part) {
			path = mstrjoin(part, PATH_CONFIG_SYS_SUFFIX);
			if (fexists(path))
			{
				free(dir_free);
				goto get_cfg_path_found;
			}
			free(path);
			path = NULL;
			part = strtok(NULL, ":");
		}
		free(dir_free);
	}

	// Use the default location if env var not set
	{
		dir = PATH_CONFIG_SYS;
		path = mstrjoin(dir, PATH_CONFIG_SYS_SUFFIX);
		if (fexists(path))
			goto get_cfg_path_found;
		free(path);
		path = NULL;
	}

	return NULL;

get_cfg_path_found:
	return path;
}

static void
parse_args(session_t *ps, int argc, char **argv, bool first_pass) {
	enum options {
		OPT_CONFIG,
		OPT_CONFIG_RELOAD,
		OPT_DEBUGLOG,
		OPT_ACTV_SWITCH,
		OPT_ACTV_EXPOSE,
		OPT_ACTV_PAGING,
		OPT_DM_START,
		OPT_DM_STOP,
		OPT_MULTI_SELECT,
		OPT_WM_CLASS,
		OPT_WM_TITLE,
		OPT_WM_STATUS,
		OPT_DESKTOP,
		OPT_TOGGLE,
		OPT_PIVOTING,
		OPT_PREV,
		OPT_NEXT,
	};
	static const char * opts_short = "h";
	static const struct option opts_long[] = {
		{ "help",                     no_argument,       NULL, 'h' },
		{ "debug-log",                no_argument,       NULL, OPT_DEBUGLOG },
		{ "config",                   required_argument, NULL, OPT_CONFIG },
		{ "config-reload",            no_argument,       NULL, OPT_CONFIG_RELOAD },
		{ "switch",                   no_argument,       NULL, OPT_ACTV_SWITCH },
		{ "expose",                   no_argument,       NULL, OPT_ACTV_EXPOSE },
		{ "paging",                   no_argument,       NULL, OPT_ACTV_PAGING },
		{ "start-daemon",             no_argument,       NULL, OPT_DM_START },
		{ "stop-daemon",              no_argument,       NULL, OPT_DM_STOP },
		{ "multi-select",             no_argument,       NULL, OPT_MULTI_SELECT },
		{ "wm-class",                 required_argument, NULL, OPT_WM_CLASS },
		{ "wm-title",                 required_argument, NULL, OPT_WM_TITLE },
		{ "wm-status",                required_argument, NULL, OPT_WM_STATUS },
		{ "desktop",                  required_argument, NULL, OPT_DESKTOP },
		{ "toggle",                   no_argument,       NULL, OPT_TOGGLE },
		{ "pivot",                    required_argument, NULL, OPT_PIVOTING },
		{ "prev",                     no_argument,       NULL, OPT_PREV },
		{ "next",                     no_argument,       NULL, OPT_NEXT },
		// { "test",                     no_argument,       NULL, 't' },
		{ NULL, no_argument, NULL, 0 }
	};

	int o = 0;
	optind = 1;
	bool custom_config = false;
	bool config_reload = false;
	bool user_specified_toggle_pivot = false;

	// Only parse --config in first pass
	if (first_pass) {
		while ((o = getopt_long(argc, argv, opts_short, opts_long, NULL)) >= 0) {
			switch (o) {
				case OPT_CONFIG:
					if (ps->o.config_path)
						free(ps->o.config_path);
					ps->o.config_path = realpath(optarg, NULL);
					if (ps->o.config_path)
						custom_config = true;
					else
						ps->o.config_blank = true;
					if (ps->o.config_path &&
							strlen(ps->o.config_path) + 3 + 1 + 1 > BUF_LEN)
						printfef(true, "(): config file path exceeds %d character limit",
								BUF_LEN - 3 - 1);
					break;
				case OPT_CONFIG_RELOAD:
					config_reload = true;
					break;
				case OPT_DEBUGLOG:
					debuglog = true;
					break;
				case OPT_DM_START:
					ps->o.runAsDaemon = true;
					break;
				// case 't':
				// 	developer_tests();
				// 	exit('t' == o ? RET_SUCCESS: RET_BADARG);
				case '?':
				case 'h':
					show_help();
					// Return a non-zero value on unrecognized option
					exit('h' == o ? RET_SUCCESS: RET_BADARG);
				default:
					break;
			}
		}
		return;
	}

	while ((o = getopt_long(argc, argv, opts_short, opts_long, NULL)) >= 0) {
		switch (o) {
			case OPT_DEBUGLOG: break;
			case OPT_CONFIG:
				if (realpath(optarg, NULL))
					custom_config = true;
				else
					printfef(true, "(): config path \"%s\" not found, ignored",
							optarg);
				break;
			case OPT_CONFIG_RELOAD:
				config_reload = true;
				break;
			case OPT_ACTV_SWITCH:
				ps->o.mode = PROGMODE_SWITCH;
				break;
			case OPT_ACTV_EXPOSE:
				ps->o.mode = PROGMODE_EXPOSE;
				break;
			case OPT_ACTV_PAGING:
				ps->o.mode = PROGMODE_PAGING;
				break;
			case OPT_DM_STOP:
				ps->o.mode = PROGMODE_DM_STOP;
				break;
			case OPT_MULTI_SELECT:
				ps->o.multiselect = true;
				break;
			case OPT_WM_CLASS:
				if (ps->o.wm_class == NULL)
					ps->o.wm_class = mstrdup(optarg);
				else {
					char* newclass = malloc(
							(strlen(ps->o.wm_class) + strlen(optarg) + 3)*sizeof(char));
					newclass[0] = '('; newclass[1] = '\0';
					strcat(newclass, ps->o.wm_class);
					strcat(newclass, "|");
					strcat(newclass, optarg);
					strcat(newclass, ")");
					free(ps->o.wm_class);
					ps->o.wm_class = newclass;
				}
				printfdf(false, "(): --wm-class=%s", ps->o.wm_class);
				break;
			case OPT_WM_TITLE:
				if (ps->o.wm_title == NULL)
					ps->o.wm_title = mstrdup(optarg);
				else {
					char* newtitle = malloc(
							(strlen(ps->o.wm_title) + strlen(optarg) + 3)*sizeof(char));
					newtitle[0] = '('; newtitle[1] = '\0';
					strcat(newtitle, ps->o.wm_title);
					strcat(newtitle, "|");
					strcat(newtitle, optarg);
					strcat(newtitle, ")");
					free(ps->o.wm_title);
					ps->o.wm_title = newtitle;
				}
				printfdf(false, "(): --wm-title=%s", ps->o.wm_title);
				break;
			case OPT_WM_STATUS:
			{
				int anchor = 0;
				for (int i=0; i<strlen(optarg) + 1; i++)
					if (optarg[i] == ',' || optarg[i] == '\0') {
						char *status = malloc(i - anchor);
						for (int j=anchor; j<i; j++)
							status[j-anchor] = optarg[j];
						status[i-anchor] = '\0';
						anchor = i + 1;

						int wm_status = wm_get_status(status);
						if (wm_status == 0) {
							printfef(true,
								"(): window status %s not recognized",
								status);
							exit(1);
						}
						free(status);

						int count = ps->o.wm_status_count;

						int *newptr = malloc(count+1);
						for (int j=0; j<count; j++)
							newptr[j] = ps->o.wm_status[j];
						newptr[count] = wm_status;

						ps->o.wm_status_count++;
						free(ps->o.wm_status);

						ps->o.wm_status = newptr;
					}

				ps->o.wm_status_str = malloc(ps->o.wm_status_count+1);
				for (int i=0; i<ps->o.wm_status_count; i++)
					ps->o.wm_status_str[i] = ps->o.wm_status[i];
				ps->o.wm_status_str[ps->o.wm_status_count] = '\0';
			}

				break;
			case OPT_DESKTOP:
				for (int i=0; i<strlen(optarg); i++)
					if (optarg[i] != '-' && optarg[i] != ','
							&& !('0'<=optarg[i] && optarg[i]<='9')) {
						printfef(true,
							"(): --desktop argument accepts only numerals and comma");
						exit(1);
					}

				if (ps->o.desktops)
					free(ps->o.desktops);
				ps->o.desktops = strdup(optarg);
				break;
			case OPT_TOGGLE:
				user_specified_toggle_pivot = true;
				ps->o.pivotkey = 0;
				break;
			case OPT_PIVOTING:
				user_specified_toggle_pivot = true;
				KeySym keysym = XStringToKeysym(optarg);
				if (keysym == 0) {
					printfef(true, "(): \"%s\" was not recognized as a valid KeySym. Run the program 'xev' to find the correct value.", optarg);
					exit(1);
				}

				ps->o.pivotkey = XKeysymToKeycode(ps->dpy, keysym);
				break;
			case OPT_PREV:
				ps->o.focus_initial--;
				break;
			case OPT_NEXT:
				ps->o.focus_initial++;
				break;
			case OPT_DM_START:
				ps->o.runAsDaemon = true;
				break;
			default:
				printfef(true, "(0): Unimplemented option %d.", o);
				exit(RET_UNKNOWN);
		}
	}

	if (!user_specified_toggle_pivot) {
		if (ps->o.mode == PROGMODE_SWITCH) {
			ps->o.pivotkey = 64; // switch defaults to pivot with Alt_L
		}
		if (ps->o.mode == PROGMODE_EXPOSE
				|| ps->o.mode == PROGMODE_PAGING) {
			ps->o.pivotkey = 0; // expose/paging defaults to toggle
		}
	}
	if (custom_config && !ps->o.runAsDaemon)
		ps->o.config_reload_path = true;

	if (config_reload && !ps->o.runAsDaemon)
		ps->o.config_reload = true;
}

static bool
update_and_flag(dlist *config,
		char *config_section, char *config_option, char *defaultvalue,
		char **ptr) {
	char *temp = mstrdup(config_get(config,
				config_section, config_option, defaultvalue));

	bool updated = true;
	if (*ptr && **ptr) {
		updated = strcmp(*ptr, temp) != 0;
		free(*ptr);
	}

	*ptr = temp;

	return updated;
}

int
load_config_file(session_t *ps)
{
    dlist *config = NULL;
    {
        bool user_specified_config = ps->o.config_path;
        if (!ps->o.config_path)
            ps->o.config_path = get_cfg_path();

		if (ps->o.runAsDaemon)
			printfef(true, "(): using \"%s\"", ps->o.config_path);

        if (ps->o.config_path) {
            config = config_load(ps->o.config_path);
		}
        else
            printfef(true, "(): WARNING: No configuration file found.");
        if (!config && user_specified_config)
            return 1;
    }

    char *lc_numeric_old = mstrdup(setlocale(LC_NUMERIC, NULL));
    setlocale(LC_NUMERIC, "C");

    // Read configuration into ps->o, because searching all the time is much
    // less efficient, may introduce inconsistent default value, and
    // occupies a lot more memory for non-string types.

	{
		// two -'s, the first digit of uid/xid and null terminator
		int pipeStrLen0 = 7;

		int uid = getuid();
		for (int num = uid; num >= 10; num /= 10)
			pipeStrLen0++;

		int xid = XConnectionNumber(ps->dpy);
		for (int num = xid; num >= 10; num /= 10)
			pipeStrLen0++;

		for (int num = ps->screen; num >= 10; num /= 10)
			pipeStrLen0++;

		const char * path = config_get(config, "system", "daemonPath", "/tmp/skippy-xd-fifo");
		int pipeStrLen1 = pipeStrLen0 + strlen(path);

		const char * path2 = config_get(config, "system", "clientPath", "/tmp/skippy-xd-fofi");
		int pipeStrLen2 = pipeStrLen0 + strlen(path);

		char * pipePath = malloc (pipeStrLen1 * sizeof(unsigned char));
		sprintf(pipePath, "%s-%i-%i-%i", path, uid, xid, ps->screen);

		char * pipePath2 = malloc (pipeStrLen2 * sizeof(unsigned char));
		sprintf(pipePath2, "%s-%i-%i-%i", path2, uid, xid, ps->screen);

		ps->o.pipePath = pipePath;
		ps->o.pipePath2 = pipePath2;
	}

	{
		ps->o.clientList = 0;
		const char *tmp = config_get(config, "system", "clientList", "_NET_CLIENT_LIST");
		if (tmp && strcmp(tmp, "XQueryTree") == 0)
			ps->o.clientList = 0;
		else if (tmp && strcmp(tmp, "_NET_CLIENT_LIST") == 0)
			ps->o.clientList = 1;
		else if (tmp && strcmp(tmp, "_WIN_CLIENT_LIST") == 0)
			ps->o.clientList = 2;
	}
    config_get_bool_wrap(config, "system", "pseudoTrans", &ps->o.pseudoTrans);

    config_get_bool_wrap(config, "multimonitor", "showOnlyCurrentMonitor", &ps->o.showOnlyCurrentMonitor);
    config_get_bool_wrap(config, "multimonitor", "showOnlyCurrentScreen", &ps->o.filterxscreen);

	{
		const char *s = config_get(config, "layout", "switchLayout", NULL);
		if (s) {
			if (strcmp(s,"cosmos") == 0) {
				ps->o.switchLayout = LAYOUT_COSMOS;
			}
			else if (strcmp(s,"xd") == 0) {
				ps->o.switchLayout = LAYOUT_XD;
			}
			else {
				printfef(true, "(): switchLayout \"%s\" not found. Valid switchLayout are:",
						s);
				printfef(true, "(): xd");
				printfef(true, "(): cosmos (default)");
				ps->o.switchLayout = LAYOUT_XD;
			}
		}
		else
			ps->o.switchLayout = LAYOUT_XD;
    }
	{
		const char *s = config_get(config, "layout", "exposeLayout", NULL);
		if (s) {
			if (strcmp(s,"cosmos") == 0) {
				ps->o.exposeLayout = LAYOUT_COSMOS;
			}
			else if (strcmp(s,"xd") == 0) {
				ps->o.exposeLayout = LAYOUT_XD;
			}
			else {
				printfef(true, "(): exposeLayout \"%s\" not found. Valid exposeLayout are:",
						s);
				printfef(true, "(): xd");
				printfef(true, "(): cosmos (default)");
				ps->o.exposeLayout = LAYOUT_COSMOS;
			}
		}
		else
			ps->o.exposeLayout = LAYOUT_COSMOS;
    }
    config_get_bool_wrap(config, "layout", "switchCycleDesktops", &ps->o.switchCycleDesktops);
    config_get_bool_wrap(config, "layout", "exposeCycleDesktops", &ps->o.exposeCycleDesktops);
    config_get_int_wrap(config, "layout", "switchWaitDuration", &ps->o.switchWaitDuration, 0, 2000);
    config_get_bool_wrap(config, "layout", "switchCycleDuringWait", &ps->o.switchCycleDuringWait);
    config_get_int_wrap(config, "layout", "distance", &ps->o.distance, 5, INT_MAX);
    config_get_bool_wrap(config, "layout", "allowUpscale", &ps->o.allowUpscale);

    config_get_int_wrap(config, "display", "animationDuration", &ps->o.animationDuration, 0, 2000);
    config_get_int_wrap(config, "display", "animationRefresh", &ps->o.animationRefresh, 1, 200);

    {
        const char *sspec = config_get(config, "display", "background", "#00000055");
		if (!sspec || strlen(sspec) == 0)
			sspec = "#00000055";
		char bg_spec[256] = "orig mid mid ";
		strcat(bg_spec, sspec);

		pictspec_t spec = PICTSPECT_INIT;
		if (strcmp("None", sspec) == 0) {
			ps->o.background = None;
		}
		else if (!parse_pictspec(ps, bg_spec, &spec)) {
			ps->o.background = None;
			return RET_BADARG;
		}
		free_pictspec(ps, &ps->o.bg_spec);
		ps->o.bg_spec = spec;
	}
	config_get_bool_wrap(config, "display", "preservePages", &ps->o.preservePages);
    config_get_bool_wrap(config, "bindings", "moveMouse", &ps->o.moveMouse);
    config_get_bool_wrap(config, "display", "includeFrame", &ps->o.includeFrame);
	config_get_int_wrap(config, "display", "cornerRadius", &ps->o.cornerRadius, 0, INT_MAX);
    {
        static client_disp_mode_t DEF_CLIDISPM[] = {
            CLIDISP_THUMBNAIL, CLIDISP_ZOMBIE, CLIDISP_ICON, CLIDISP_FILLED, CLIDISP_NONE
        };

        static client_disp_mode_t DEF_CLIDISPM_ICON[] = {
            CLIDISP_THUMBNAIL_ICON, CLIDISP_THUMBNAIL, CLIDISP_ZOMBIE_ICON,
            CLIDISP_ZOMBIE, CLIDISP_ICON, CLIDISP_FILLED, CLIDISP_NONE
        };

        bool thumbnail_icons = false;
        config_get_bool_wrap(config, "display", "icon", &thumbnail_icons);
        if (thumbnail_icons) {
            ps->o.clientDisplayModes = allocchk(malloc(sizeof(DEF_CLIDISPM_ICON)));
            memcpy(ps->o.clientDisplayModes, &DEF_CLIDISPM_ICON, sizeof(DEF_CLIDISPM_ICON));
        }
        else {
            ps->o.clientDisplayModes = allocchk(malloc(sizeof(DEF_CLIDISPM)));
            memcpy(ps->o.clientDisplayModes, &DEF_CLIDISPM, sizeof(DEF_CLIDISPM));
        }
    }
	{
		char defaultstr2[256] = "orig ";
		const char* sspec2 = config_get(config, "display", "iconPlace", "left left");
		strcat(defaultstr2, sspec2);
		const char space[] = " ";
		strcat(defaultstr2, space);
		char sspec[] = "#333333";
		strcat(defaultstr2, sspec);

		config_get_int_wrap(config, "display", "iconSize",
				&ps->o.iconSize, 1, INT_MAX);

		if (!parse_pictspec(ps, defaultstr2, &ps->o.iconSpec))
			return RET_BADARG;
		if (!simg_cachespec(ps, &ps->o.iconSpec))
			return RET_BADARG;

		if (ps->o.iconSpec.path
				&& !(ps->o.iconDefault = simg_load(ps, ps->o.iconSpec.path,
						PICTPOSP_SCALEK, ps->o.iconSize, ps->o.iconSize,
						ALIGN_MID, ALIGN_MID, NULL)))
			return RET_BADARG;
}
	{
		char defaultstr[256] = "orig mid mid ";
		const char* sspec = config_get(config, "filler", "tint", "#333333");
		strcat(defaultstr, sspec);
		if (!parse_pictspec(ps, defaultstr, &ps->o.fillSpec))
			return RET_BADARG;

		char defaultstr2[256] = "orig ";
		const char* sspec2 = config_get(config, "filler", "iconPlace", "mid mid");
		strcat(defaultstr2, sspec2);
		const char space[] = " ";
		strcat(defaultstr2, space);
		strcat(defaultstr2, sspec);

		config_get_int_wrap(config, "filler", "iconSize",
				&ps->o.fillerIconSize, 0, 256);

		if (!parse_pictspec(ps, defaultstr2, &ps->o.iconFillSpec))
			return RET_BADARG;
		if (!simg_cachespec(ps, &ps->o.iconFillSpec))
			return RET_BADARG;

		if (ps->o.iconFillSpec.path
				&& !(ps->o.iconFiller = simg_load(ps, ps->o.iconFillSpec.path,
						PICTPOSP_SCALEK, ps->o.fillerIconSize, ps->o.fillerIconSize,
						ALIGN_MID, ALIGN_MID, NULL)))
			return RET_BADARG;
	}

	ps->o.normal_tint = mstrdup(config_get(config, "normal", "tint", "black"));
    config_get_int_wrap(config, "normal", "tintOpacity", &ps->o.normal_tintOpacity, 0, 256);
    config_get_int_wrap(config, "normal", "opacity", &ps->o.normal_opacity, 0, 256);
	ps->o.highlight_tint = mstrdup(config_get(config, "highlight", "tint", "#444444"));
    config_get_int_wrap(config, "highlight", "tintOpacity", &ps->o.highlight_tintOpacity, 0, 256);
    config_get_int_wrap(config, "highlight", "opacity", &ps->o.highlight_opacity, 0, 256);
	ps->o.shadow_tint = mstrdup(config_get(config, "shadow", "tint", "#040404"));
    config_get_int_wrap(config, "shadow", "tintOpacity", &ps->o.shadow_tintOpacity, 0, 256);
    config_get_int_wrap(config, "shadow", "opacity", &ps->o.shadow_opacity, 0, 256);
	ps->o.multiselect_tint = mstrdup(config_get(config, "multiselect", "tint", "#3376BB"));
    config_get_int_wrap(config, "multiselect", "tintOpacity", &ps->o.multiselect_tintOpacity, 0, 256);
    config_get_int_wrap(config, "multiselect", "opacity", &ps->o.multiselect_opacity, 0, 256);

    config_get_bool_wrap(config, "panel", "show", &ps->o.panel_show);
    config_get_bool_wrap(config, "panel", "backgroundTinting", &ps->o.panel_tinting);
    config_get_bool_wrap(config, "panel", "reserveSpace", &ps->o.panel_reserveSpace);

    config_get_bool_wrap(config, "desktop", "show", &ps->o.panel_show_desktop);
    config_get_bool_wrap(config, "desktop", "backgroundTinting", &ps->o.desktopTinting);

	{
		ps->o.updatetooltip = false;
		ps->o.updatetooltip |= update_and_flag(config, "label", "border", "#0e0e0e", &ps->o.tooltip_border);
		ps->o.updatetooltip |= update_and_flag(config, "label", "background", "#202020", &ps->o.tooltip_background);
		ps->o.updatetooltip |= update_and_flag(config, "label", "backgroundHighlight", "#666666", &ps->o.tooltip_backgroundHighlight);
		ps->o.updatetooltip |= update_and_flag(config, "label", "text", "white", &ps->o.tooltip_text);
		ps->o.updatetooltip |= update_and_flag(config, "label", "textShadow", "black", &ps->o.tooltip_textShadow);
		ps->o.updatetooltip |= update_and_flag(config, "label", "font", "fixed-11:weight=bold", &ps->o.tooltip_font);
	}
    config_get_bool_wrap(config, "label", "show", &ps->o.tooltip_show);
	{
		const char* tooltipoption = config_get(config, "label", "option", "windowClass");
		if (strcmp(tooltipoption, "windowTitle") == 0)
			ps->o.tooltip_option = 0;
		else
			ps->o.tooltip_option = 1;
	}
    config_get_int_wrap(config, "label", "offsetX", &ps->o.tooltip_offsetX, INT_MIN, INT_MAX);
    config_get_int_wrap(config, "label", "offsetY", &ps->o.tooltip_offsetY, INT_MIN, INT_MAX);
    config_get_double_wrap(config, "label", "width", &ps->o.tooltip_width, 0.0, 1.0);
	{
		int old_value = ps->o.tooltip_opacity;
		config_get_int_wrap(config, "label", "opacity", &ps->o.tooltip_opacity, 0, 256);
		if (ps->o.tooltip_opacity != old_value)
			ps->o.updatetooltip = true;
	}

	config_get_bool_wrap(config, "bindings", "enforceFocus", &ps->o.enforceFocus);
    config_get_int_wrap(config, "bindings", "pivotLockingTime", &ps->o.pivotLockingTime, 0, 20000);

    // load keybindings settings
    ps->o.bindings_keysUp = mstrdup(config_get(config, "bindings", "keysUp", "Up"));
    ps->o.bindings_keysDown = mstrdup(config_get(config, "bindings", "keysDown", "Down"));
    ps->o.bindings_keysLeft = mstrdup(config_get(config, "bindings", "keysLeft", "Left"));
    ps->o.bindings_keysRight = mstrdup(config_get(config, "bindings", "keysRight", "Right"));
    ps->o.bindings_keysPrev = mstrdup(config_get(config, "bindings", "keysPrev", "p"));
    ps->o.bindings_keysNext = mstrdup(config_get(config, "bindings", "keysNext", "n"));
    ps->o.bindings_keysCancel = mstrdup(config_get(config, "bindings", "keysCancel", "Escape"));
    ps->o.bindings_keysSelect = mstrdup(config_get(config, "bindings", "keysSelect", "Return space"));
    ps->o.bindings_keysIconify = mstrdup(config_get(config, "bindings", "keysIconify", "1"));
    ps->o.bindings_keysShade = mstrdup(config_get(config, "bindings", "keysShade", "2"));
    ps->o.bindings_keysClose = mstrdup(config_get(config, "bindings", "keysClose", "3"));

    // print an error message for any key bindings that aren't recognized
    check_keysyms(ps->o.config_path, ": [bindings] keysUp =", ps->o.bindings_keysUp);
    check_keysyms(ps->o.config_path, ": [bindings] keysDown =", ps->o.bindings_keysDown);
    check_keysyms(ps->o.config_path, ": [bindings] keysLeft =", ps->o.bindings_keysLeft);
    check_keysyms(ps->o.config_path, ": [bindings] keysRight =", ps->o.bindings_keysRight);
    check_keysyms(ps->o.config_path, ": [bindings] keysPrev =", ps->o.bindings_keysPrev);
    check_keysyms(ps->o.config_path, ": [bindings] keysNext =", ps->o.bindings_keysNext);
    check_keysyms(ps->o.config_path, ": [bindings] keysCancel =", ps->o.bindings_keysCancel);
    check_keysyms(ps->o.config_path, ": [bindings] keysSelect =", ps->o.bindings_keysSelect);
    check_keysyms(ps->o.config_path, ": [bindings] keysIconify =", ps->o.bindings_keysIconify);
    check_keysyms(ps->o.config_path, ": [bindings] keysShade =", ps->o.bindings_keysShade);
    check_keysyms(ps->o.config_path, ": [bindings] keysClose =", ps->o.bindings_keysClose);

	if (!parse_cliop(ps, config_get(config, "bindings", "miwMouse1", "focus"), &ps->o.bindings_miwMouse[1])
			|| !parse_cliop(ps, config_get(config, "bindings", "miwMouse2", "close-ewmh"), &ps->o.bindings_miwMouse[2])
			|| !parse_cliop(ps, config_get(config, "bindings", "miwMouse3", "iconify"), &ps->o.bindings_miwMouse[3])
			|| !parse_cliop(ps, config_get(config, "bindings", "miwMouse4", "keysNext"), &ps->o.bindings_miwMouse[4])
			|| !parse_cliop(ps, config_get(config, "bindings", "miwMouse5", "keysPrev"), &ps->o.bindings_miwMouse[5])) {
		return RET_BADARG;
	}

    setlocale(LC_NUMERIC, lc_numeric_old);
    free(lc_numeric_old);
    config_free(config);

	return RET_SUCCESS;
}

int main(int argc, char *argv[]) {
	session_t *ps = NULL;
	int ret = RET_SUCCESS;
	Display *dpy = NULL;

	/* Set program locale */
	setlocale (LC_ALL, "");

	// Initialize session structure
	{
		static const session_t SESSIONT_DEF = SESSIONT_INIT;
		ps_g = ps = allocchk(malloc(sizeof(session_t)));
		memcpy(ps, &SESSIONT_DEF, sizeof(session_t));
		gettimeofday(&ps->time_start, NULL);
	}

	// First pass
	parse_args(ps, argc, argv, true);

	// Open connection to X
	if (!(ps->dpy = dpy = XOpenDisplay(NULL))) {
		printfef(true, "(): FATAL: Couldn't connect to display.");
		ret = RET_XFAIL;
		goto main_end;
	}
	if (!init_xexts(ps)) {
		ret = RET_XFAIL;
		goto main_end;
	}
	if (debuglog)
		XSynchronize(ps->dpy, True);
	XSetErrorHandler(xerror);

	ps->screen = DefaultScreen(dpy);
	ps->root = RootWindow(dpy, ps->screen);
	printfdf(false, "(): Working on screen %d", ps->screen);

	wm_get_atoms(ps);

	int config_load_ret = load_config_file(ps);
	if (config_load_ret != 0)
		return config_load_ret;

	// Second pass
	parse_args(ps, argc, argv, false);

	printfdf(false, "(): after 2nd pass:  ps->o.focus_initial =  %i", ps->o.focus_initial);

	const char* pipePath = ps->o.pipePath;

	// Handle special modes
	switch (ps->o.mode) {
		case PROGMODE_NORMAL:
			if (!ps->o.runAsDaemon &&
					(ps->o.config_reload || ps->o.config_reload_path
					 || ps->o.config_blank)) {
				activate_via_fifo(ps, pipePath);
				goto main_end;
			}
			break;
		case PROGMODE_DM_STOP:
			exit_daemon(pipePath);
			goto main_end;
		default:
			// this is switch/expose/paging
			// potentially with flags of prev/next
			// or multi-byte pipe command

			// wait and read daemon-to-client pipe
			// then print result to stdout
			if (ps->fd_pipe2 >= 0) {
				close(ps->fd_pipe2);
				ps->fd_pipe2 = -1;
			}

			char* daemon2client_pipe = DaemonToClientPipeName(ps, getpid());
			{
				int result = mkfifo(daemon2client_pipe, S_IRUSR | S_IWUSR);
				if (result < 0  && EEXIST != errno) {
					printfef(true,
							"(): Failed to create named pipe \"%s\": %d",
							ps->o.pipePath2, result);
					perror("mkfifo");
					ret = 2;
					goto main_end;
				}
			}

			struct pollfd r_fd;
			r_fd.fd = ps->fd_pipe2 = open(daemon2client_pipe, O_RDONLY | O_NONBLOCK);
			r_fd.events = POLLIN;
			if (ps->fd_pipe2 < 0) {
				printfef(true, "(): Failed to open pipe \"%s\": %d", ps->o.pipePath2, errno);
				perror("open");
				goto main_end;
			}

			activate_via_fifo(ps, pipePath);

			poll(&r_fd, 1, -1);
			char buffer[1024];
			int read_ret = 0;
			read_ret = read(ps->fd_pipe2, &buffer, 1024);
			close(ps->fd_pipe2);
			unlink(daemon2client_pipe);
			free(daemon2client_pipe);

			if (read_ret == 0) {
				printfef(false,"(): pipe %i leak!", getpid());
			}
			else {
				printf("%s\n", buffer);
			}

			goto main_end;
	}

	if (!wm_check(ps)) {
		/* ret = 1;
		goto main_end; */
	}

	// Main branch
	MainWin *mw = mainwin_create(ps);
	if (!mw) {
		printfef(true, "(): FATAL: Couldn't create main window.");
		ret = 1;
		goto main_end;
	}
	ps->mainwin = mw;

	XSelectInput(ps->dpy, ps->root, SubstructureNotifyMask);

	// Daemon mode
	if (ps->o.runAsDaemon) {

		printfdf(false, "(): Running as daemon...");

		{
			int result = mkfifo(pipePath, S_IRUSR | S_IWUSR);
			if (result < 0  && EEXIST != errno) {
				printfef(true, "(): Failed to create named pipe \"%s\": %d", pipePath, result);
				perror("mkfifo");
				ret = 2;
				goto main_end;
			}
		}

		// Opening pipe
		if (!open_pipe(ps, NULL)) {
			ret = 2;
			goto main_end;
		}
		assert(ps->fd_pipe >= 0);

		{
			char *buf[BUF_LEN];
			while (read(ps->fd_pipe, buf, sizeof(buf)))
				continue;
			printfdf(false, "(): Finished flushing pipe \"%s\".", pipePath);
		}

		flush_clients(ps);

		mainloop(ps, false);
	}
	else {
		printfdf(false, "(): running once then quitting...");
		mainloop(ps, true);
	}

main_end:
	// Free session data
	if (ps) {
		// Free configuration strings
		{
			free(ps->o.config_path);
			free(ps->o.pipePath);
			free(ps->o.pipePath2);
			free(ps->o.clientDisplayModes);
			free(ps->o.normal_tint);
			free(ps->o.highlight_tint);
			free(ps->o.shadow_tint);
			free(ps->o.tooltip_border);
			free(ps->o.tooltip_background);
			free(ps->o.tooltip_text);
			free(ps->o.tooltip_textShadow);
			free(ps->o.tooltip_font);
			free_pictw(ps, &ps->o.background);
			free_pictspec(ps, &ps->o.bg_spec);
			free_pictw(ps, &ps->o.iconDefault);
			free_pictw(ps, &ps->o.iconFiller);
			free_pictspec(ps, &ps->o.fillSpec);
			free_pictspec(ps, &ps->o.iconFillSpec);
			free(ps->o.bindings_keysUp);
			free(ps->o.bindings_keysDown);
			free(ps->o.bindings_keysLeft);
			free(ps->o.bindings_keysRight);
			free(ps->o.bindings_keysPrev);
			free(ps->o.bindings_keysNext);
			free(ps->o.bindings_keysCancel);
			free(ps->o.bindings_keysSelect);
			free(ps->o.bindings_keysIconify);
			free(ps->o.bindings_keysShade);
			free(ps->o.bindings_keysClose);
		}

		if (ps->o.wm_class)
			free(ps->o.wm_class);
		if (ps->o.wm_title)
			free(ps->o.wm_title);
		if (ps->o.wm_status_count > 0)
			free(ps->o.wm_status);
		if (ps->o.wm_status_count > 0)
			free(ps->o.wm_status_str);
		if (ps->o.desktops)
			free(ps->o.desktops);

		if (ps->fd_pipe >= 0)
			close(ps->fd_pipe);

		if (ps->mainwin)
			mainwin_destroy(ps->mainwin);

		if (ps->dpy)
			XCloseDisplay(dpy);

		free(ps);
	}

	return ret;
}
