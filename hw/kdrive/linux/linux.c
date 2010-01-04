/*
 * Copyright © 1999 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Keith Packard not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Keith Packard makes no
 * representations about the suitability of this software for any purpose.  It
 * is provided "as is" without express or implied warranty.
 *
 * KEITH PACKARD DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL KEITH PACKARD BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <kdrive-config.h>
#endif
#include "kdrive.h"
#include <errno.h>
#include <signal.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <X11/keysym.h>

static int  vtno;
int  LinuxConsoleFd;
static int  activeVT;
static Bool enabled;

static void
LinuxVTRequest (int sig)
{
    kdSwitchPending = TRUE;
}

/* Check before chowning -- this avoids touching the file system */
static void
LinuxCheckChown (char *file)
{
    struct stat	    st;
    __uid_t	    u;
    __gid_t	    g;

    if (stat (file, &st) < 0)
	return;
    u = getuid ();
    g = getgid ();
    if (st.st_uid != u || st.st_gid != g)
	chown (file, u, g);
}

static int
LinuxInit (void)
{
    int fd = -1;
    char vtname[11];
    struct vt_stat vts;

    LinuxConsoleFd = -1;
    /* check if we're run with euid==0 */
    if (geteuid() != 0)
    {
	FatalError("LinuxInit: Server must be suid root\n");
    }

    if (kdVirtualTerminal >= 0)
	vtno = kdVirtualTerminal;
    else
    {
	if ((fd = open("/dev/tty0",O_WRONLY,0)) < 0)
	{
	    FatalError(
		       "LinuxInit: Cannot open /dev/tty0 (%s)\n",
		       strerror(errno));
	}
	if ((ioctl(fd, VT_OPENQRY, &vtno) < 0) ||
	    (vtno == -1))
	{
	    FatalError("xf86OpenConsole: Cannot find a free VT\n");
	}
	close(fd);
    }

    sprintf(vtname,"/dev/tty%d",vtno); /* /dev/tty1-64 */

    if ((LinuxConsoleFd = open(vtname, O_RDWR|O_NDELAY, 0)) < 0)
    {
	FatalError("LinuxInit: Cannot open %s (%s)\n",
		   vtname, strerror(errno));
    }

    /* change ownership of the vt */
    LinuxCheckChown (vtname);

    /*
     * the current VT device we're running on is not "console", we want
     * to grab all consoles too
     *
     * Why is this needed?
     */
    LinuxCheckChown ("/dev/tty0");
    /*
     * Linux doesn't switch to an active vt after the last close of a vt,
     * so we do this ourselves by remembering which is active now.
     */
    memset (&vts, '\0', sizeof (vts));	/* valgrind */
    if (ioctl(LinuxConsoleFd, VT_GETSTATE, &vts) == 0)
    {
	activeVT = vts.v_active;
    }

    return 1;
}

#ifdef FNONBLOCK
#define NOBLOCK FNONBLOCK
#else
#define NOBLOCK FNDELAY
#endif

static void
LinuxEnable (void)
{
    if (enabled)
	return;
    if (kdSwitchPending)
    {
	kdSwitchPending = FALSE;
	ioctl (LinuxConsoleFd, VT_RELDISP, VT_ACKACQ);
    }

    /*
     * now get the VT
     */
    if (ioctl(LinuxConsoleFd, VT_ACTIVATE, vtno) != 0)
    {
	FatalError("LinuxInit: VT_ACTIVATE failed\n");
    }
    if (ioctl(LinuxConsoleFd, VT_WAITACTIVE, vtno) != 0)
    {
	FatalError("LinuxInit: VT_WAITACTIVE failed\n");
    }
    if (ioctl(LinuxConsoleFd, KDSETMODE, KD_GRAPHICS) < 0)
    {
	FatalError("LinuxInit: KDSETMODE KD_GRAPHICS failed\n");
    }
    enabled = TRUE;
}

static void
LinuxDisable (void)
{
    ioctl(LinuxConsoleFd, KDSETMODE, KD_TEXT);  /* Back to text mode ... */
    if (kdSwitchPending)
    {
	kdSwitchPending = FALSE;
	ioctl (LinuxConsoleFd, VT_RELDISP, 1);
    }
    enabled = FALSE;
}

static void
LinuxFini (void)
{
    struct vt_mode   VT;
    struct vt_stat  vts;
    int		    fd;

    if (LinuxConsoleFd < 0)
	return;

    if (ioctl(LinuxConsoleFd, VT_GETMODE, &VT) != -1)
    {
	VT.mode = VT_AUTO;
	ioctl(LinuxConsoleFd, VT_SETMODE, &VT); /* set dflt vt handling */
    }
    memset (&vts, '\0', sizeof (vts));	/* valgrind */
    ioctl (LinuxConsoleFd, VT_GETSTATE, &vts);
    if (vtno == vts.v_active)
    {
	/*
	 * Find a legal VT to switch to, either the one we started from
	 * or the lowest active one that isn't ours
	 */
	if (activeVT < 0 ||
	    activeVT == vts.v_active ||
	    !(vts.v_state & (1 << activeVT)))
	{
	    for (activeVT = 1; activeVT < 16; activeVT++)
		if (activeVT != vtno && (vts.v_state & (1 << activeVT)))
		    break;
	    if (activeVT == 16)
		activeVT = -1;
	}
	/*
	 * Perform a switch back to the active VT when we were started
	 */
	if (activeVT >= -1)
	{
	    ioctl (LinuxConsoleFd, VT_ACTIVATE, activeVT);
	    ioctl (LinuxConsoleFd, VT_WAITACTIVE, activeVT);
	    activeVT = -1;
	}
    }
    close(LinuxConsoleFd);                /* make the vt-manager happy */
    LinuxConsoleFd = -1;
    fd = open ("/dev/tty0", O_RDWR|O_NDELAY, 0);
    if (fd >= 0)
    {
	memset (&vts, '\0', sizeof (vts));	/* valgrind */
	ioctl (fd, VT_GETSTATE, &vts);
	if (ioctl (fd, VT_DISALLOCATE, vtno) < 0)
	    fprintf (stderr, "Can't deallocate console %d %s\n", vtno, strerror(errno));
	close (fd);
    }
    return;
}

void
KdOsAddInputDrivers (void)
{
    KdAddPointerDriver(&LinuxMouseDriver);
    KdAddPointerDriver(&MsMouseDriver);
    KdAddPointerDriver(&Ps2MouseDriver);
#ifdef TSLIB
    KdAddPointerDriver(&TsDriver);
#endif
    KdAddPointerDriver(&LinuxEvdevMouseDriver);
    KdAddKeyboardDriver(&LinuxKeyboardDriver);
    KdAddKeyboardDriver(&LinuxEvdevKeyboardDriver);
}

static void
LinuxBell(int volume, int pitch, int duration)
{
    if (volume && pitch)
        ioctl(LinuxConsoleFd, KDMKTONE, ((1193190 / pitch) & 0xffff) |
              (((unsigned long)duration * volume / 50) << 16));
}

KdOsFuncs   LinuxFuncs = {
    .Init = LinuxInit,
    .Enable = LinuxEnable,
    .Disable = LinuxDisable,
    .Fini = LinuxFini,
    .Bell = LinuxBell,
};

void
OsVendorInit (void)
{
    KdOsInit (&LinuxFuncs);
}
