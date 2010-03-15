/*
 * Copyright © 1999 Keith Packard
 * Copyright © 2010 Mikhail Gusarov <dottedmag@dottedmag.net>
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
#include <sys/ioctl.h>
#include <linux/apm_bios.h>

#include "apm.h"

#ifdef FNONBLOCK
#define NOBLOCK FNONBLOCK
#else
#define NOBLOCK FNDELAY
#endif

static int LinuxApmFd = -1;
static Bool LinuxApmRunning;

static void
LinuxApmBlock (pointer blockData, OSTimePtr pTimeout, pointer pReadmask)
{
}

static void
LinuxApmWakeup (pointer blockData, int result, pointer pReadmask)
{
    fd_set  *readmask = (fd_set *) pReadmask;

    if (result > 0 && LinuxApmFd >= 0 && FD_ISSET (LinuxApmFd, readmask))
    {
	apm_event_t event;
	Bool	    running = LinuxApmRunning;
	int	    cmd = APM_IOC_SUSPEND;

	while (read (LinuxApmFd, &event, sizeof (event)) == sizeof (event))
	{
	    switch (event) {
	    case APM_SYS_STANDBY:
	    case APM_USER_STANDBY:
		running = FALSE;
		cmd = APM_IOC_STANDBY;
		break;
	    case APM_SYS_SUSPEND:
	    case APM_USER_SUSPEND:
	    case APM_CRITICAL_SUSPEND:
		running = FALSE;
		cmd = APM_IOC_SUSPEND;
		break;
	    case APM_NORMAL_RESUME:
	    case APM_CRITICAL_RESUME:
	    case APM_STANDBY_RESUME:
		running = TRUE;
		break;
	    }
	}
	if (running && !LinuxApmRunning)
	{
	    KdResume ();
	    LinuxApmRunning = TRUE;
	}
	else if (!running && LinuxApmRunning)
	{
	    KdSuspend ();
	    LinuxApmRunning = FALSE;
	    ioctl (LinuxApmFd, cmd, 0);
	}
    }
}

void
LinuxApmOpen(void)
{
    LinuxApmFd = open ("/dev/apm_bios", 2);
    if (LinuxApmFd < 0 && errno == ENOENT)
	LinuxApmFd = open ("/dev/misc/apm_bios", 2);
    if (LinuxApmFd >= 0)
    {
	LinuxApmRunning = TRUE;
	fcntl (LinuxApmFd, F_SETFL, fcntl (LinuxApmFd, F_GETFL) | NOBLOCK);
	RegisterBlockAndWakeupHandlers (LinuxApmBlock, LinuxApmWakeup, 0);
	AddEnabledDevice (LinuxApmFd);
    }
}


void
LinuxApmClose(void)
{
    if (LinuxApmFd >= 0)
    {
	RemoveBlockAndWakeupHandlers (LinuxApmBlock, LinuxApmWakeup, 0);
	RemoveEnabledDevice (LinuxApmFd);
	close (LinuxApmFd);
	LinuxApmFd = -1;
    }
}
