/* Copyright (C) 1991, 1993, 1995 Free Software Foundation, Inc.
This file is part of the GNU C Library.

The GNU C Library is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The GNU C Library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with the GNU C Library; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 675 Mass Ave,
Cambridge, MA 02139, USA.  */

#include <errno.h>
#include <stddef.h>
#include <termios.h>
#include <sys/ioctl.h>

#undef	B0
#undef	B50
#undef	B75
#undef	B110
#undef	B134
#undef	B150
#undef	B200
#undef	B300
#undef	B600
#undef	B1200
#undef	B1800
#undef	B2400
#undef	B4800
#undef	B9600
#undef	B19200
#undef	B38400
#undef	EXTA
#undef	EXTB
#undef	ECHO
#undef	TOSTOP
#undef	NOFLSH
#undef	MDMBUF
#undef	FLUSHO
#undef	PENDIN
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS
#undef	CERASE
#undef	CKILL
#undef	CINTR
#undef	CQUIT
#undef	CSTART
#undef	CSTOP
#undef	CEOF
#undef	CEOT
#undef	CBRK
#undef	CSUSP
#undef	CDSUSP
#undef	CRPRNT
#undef	CFLUSH
#undef	CWERASE
#undef	CLNEXT
#undef	CSTATUS

#define IOCPARM_MASK 0x7f
#define IOC_OUT 0x40000000
#define IOC_IN 0x80000000
#define _IOR(x,y,t) (IOC_OUT|((sizeof(t)&IOCPARM_MASK)<<16)|(x<<8)|y)
#define _IOW(x,y,t) (IOC_IN|((sizeof(t)&IOCPARM_MASK)<<16)|(x<<8)|y)
#define FIONREAD _IOR('f', 127, int)
#define FIOASYNC _IOW('f', 125, int)
#define TIOCGETP _IOR('t', 8, struct sgttyb)
#define TIOCLGET _IOR('t', 124, int)
#define TIOCLSET _IOW('t', 125, int)
#define TIOCSETN _IOW('t', 10, struct sgttyb)
#define TIOCSETP        _IOW('t', 9,struct sgttyb)/* set parameters -- stty */
#define TIOCFLUSH       _IOW('t', 16, int)      /* flush buffers */
#define TIOCSETC        _IOW('t',17,struct tchars)/* set special characters */
#define TIOCGETC        _IOR('t',18,struct tchars)/* get special characters */
#define         TANDEM          0x00000001      /* send stopc on out q full */
#define         CBREAK          0x00000002      /* half-cooked mode */
#define         LCASE           0x00000004      /* simulate lower case */
#define         ECHO            0x00000008      /* echo input */
#define         CRMOD           0x00000010      /* map \r to \r\n on output */
#define         RAW             0x00000020      /* no i/o processing */
#define         ODDP            0x00000040      /* get/send odd parity */
#define         EVENP           0x00000080      /* get/send even parity */
#define         ANYP            0x000000c0      /* get any parity/send none */
#define         PRTERA          0x00020000      /* \ ... / erase */
#define         CRTERA          0x00040000      /* " \b " to wipe out char */
#define         TILDE           0x00080000      /* hazeltine tilde kludge */
#define         MDMBUF          0x00100000      /* start/stop output on carrier intr */
#define         LITOUT          0x00200000      /* literal output */
#define         TOSTOP          0x00400000      /* SIGSTOP on background output */
#define         FLUSHO          0x00800000      /* flush output to terminal */
#define         NOHANG          0x01000000      /* no SIGHUP on carrier drop */
#define         L001000         0x02000000
#define         CRTKIL          0x04000000      /* kill line with " \b " */
#define         PASS8           0x08000000
#define         CTLECH          0x10000000      /* echo control chars as ^X */
#define         PENDIN          0x20000000      /* tp->t_rawq needs reread */
#define         DECCTQ          0x40000000      /* only ^Q starts after ^S */
#define         NOFLSH          0x80000000      /* no output flush on signal */
#define TIOCLSET        _IOW('t', 125, int)     /* set entire local mode word */
#define TIOCLGET        _IOR('t', 124, int)     /* get local modes */
#define         LCRTBS          (CRTBS>>16)
#define         LPRTERA         (PRTERA>>16)
#define         LCRTERA         (CRTERA>>16)
#define         LTILDE          (TILDE>>16)
#define         LMDMBUF         (MDMBUF>>16)
#define         LLITOUT         (LITOUT>>16)
#define         LTOSTOP         (TOSTOP>>16)
#define         LFLUSHO         (FLUSHO>>16)
#define         LNOHANG         (NOHANG>>16)
#define         LCRTKIL         (CRTKIL>>16)
#define         LPASS8          (PASS8>>16)
#define         LCTLECH         (CTLECH>>16)
#define         LPENDIN         (PENDIN>>16)
#define         LDECCTQ         (DECCTQ>>16)
#define         LNOFLSH         (NOFLSH>>16)
#define TIOCSLTC        _IOW('t',117,struct ltchars)/* set local special chars */
#define TIOCGLTC        _IOR('t',116,struct ltchars)/* get local special chars */


#if	defined(TIOCGETC) || defined(TIOCSETC)
/* Type of ARG for TIOCGETC and TIOCSETC requests.  */
struct tchars
{
  char t_intrc;			/* Interrupt character.  */
  char t_quitc;			/* Quit character.  */
  char t_startc;		/* Start-output character.  */
  char t_stopc;			/* Stop-output character.  */
  char t_eofc;			/* End-of-file character.  */
  char t_brkc;			/* Input delimiter character.  */
};

#define	_IOT_tchars	/* Hurd ioctl type field.  */ \
  _IOT (_IOTS (char), 6, 0, 0, 0, 0)
#endif

#if	defined(TIOCGLTC) || defined(TIOCSLTC)
/* Type of ARG for TIOCGLTC and TIOCSLTC requests.  */
struct ltchars
{
  char t_suspc;			/* Suspend character.  */
  char t_dsuspc;		/* Delayed suspend character.  */
  char t_rprntc;		/* Reprint-line character.  */
  char t_flushc;		/* Flush-output character.  */
  char t_werasc;		/* Word-erase character.  */
  char t_lnextc;		/* Literal-next character.  */
};

#define	_IOT_ltchars	/* Hurd ioctl type field.  */ \
  _IOT (_IOTS (char), 6, 0, 0, 0, 0)
#endif

/* Type of ARG for TIOCGETP and TIOCSETP requests (and gtty and stty).  */
struct sgttyb
{
  char sg_ispeed;		/* Input speed.  */
  char sg_ospeed;		/* Output speed.  */
  char sg_erase;		/* Erase character.  */
  char sg_kill;			/* Kill character.  */
  short int sg_flags;		/* Mode flags.  */
};




const speed_t __bsd_speeds[] =
  {
    0,
    50,
    75,
    110,
    134,
    150,
    200,
    300,
    600,
    1200,
    1800,
    2400,
    4800,
    9600,
    19200,
    38400,
  };


/* Set the state of FD to *TERMIOS_P.  */
int
tcsetattr (int fd, int optional_actions, const struct termios *termios_p)
{
  struct sgttyb buf;
  struct tchars tchars;
  struct ltchars ltchars;
  int local;
#ifdef	TIOCGETX
  int extra;
#endif
  size_t i;

  if (ioctl(fd, TIOCGETP, &buf) < 0 ||
      ioctl(fd, TIOCGETC, &tchars) < 0 ||
      ioctl(fd, TIOCGLTC, &ltchars) < 0 ||
#ifdef	TIOCGETX
      ioctl(fd, TIOCGETX, &extra) < 0 ||
#endif
      ioctl(fd, TIOCLGET, &local) < 0)
    return -1;

  if (termios_p == NULL)
    {
      errno = EINVAL;
      return -1;
    }

  buf.sg_ispeed = buf.sg_ospeed = -1;
  for (i = 0; i <= sizeof (__bsd_speeds) / sizeof (__bsd_speeds[0]); ++i)
    {
      if (__bsd_speeds[i] == termios_p->__ispeed)
	buf.sg_ispeed = i;
      if (__bsd_speeds[i] == termios_p->__ospeed)
	buf.sg_ospeed = i;
    }
  if (buf.sg_ispeed == -1 || buf.sg_ospeed == -1)
    {
      errno = EINVAL;
      return -1;
    }

  buf.sg_flags &= ~(CBREAK|RAW);
  if (!(termios_p->c_lflag & ICANON))
    buf.sg_flags |= (termios_p->c_cflag & ISIG) ? CBREAK : RAW;
#ifdef	LPASS8
  if (termios_p->c_oflag & CS8)
    local |= LPASS8;
  else
    local &= ~LPASS8;
#endif
  if (termios_p->c_lflag & _NOFLSH)
    local |= LNOFLSH;
  else
    local &= ~LNOFLSH;
  if (termios_p->c_oflag & OPOST)
    local &= ~LLITOUT;
  else
    local |= LLITOUT;
#ifdef	TIOCGETX
  if (termios_p->c_lflag & ISIG)
    extra &= ~NOISIG;
  else
    extra |= NOISIG;
  if (termios_p->c_cflag & CSTOPB)
    extra |= STOPB;
  else
    extra &= ~STOPB;
#endif
  if (termios_p->c_iflag & ICRNL)
    buf.sg_flags |= CRMOD;
  else
    buf.sg_flags &= ~CRMOD;
  if (termios_p->c_iflag & IXOFF)
    buf.sg_flags |= TANDEM;
  else
    buf.sg_flags &= ~TANDEM;

  buf.sg_flags &= ~(ODDP|EVENP);
  if (!(termios_p->c_cflag & PARENB))
    buf.sg_flags |= ODDP | EVENP;
  else if (termios_p->c_cflag & PARODD)
    buf.sg_flags |= ODDP;
  else
    buf.sg_flags |= EVENP;

  if (termios_p->c_lflag & _ECHO)
    buf.sg_flags |= ECHO;
  else
    buf.sg_flags &= ~ECHO;
  if (termios_p->c_lflag & ECHOE)
    local |= LCRTERA;
  else
    local &= ~LCRTERA;
  if (termios_p->c_lflag & ECHOK)
    local |= LCRTKIL;
  else
    local &= ~LCRTKIL;
  if (termios_p->c_lflag & _TOSTOP)
    local |= LTOSTOP;
  else
    local &= ~LTOSTOP;

  buf.sg_erase = termios_p->c_cc[VERASE];
  buf.sg_kill = termios_p->c_cc[VKILL];
  tchars.t_eofc = termios_p->c_cc[VEOF];
  tchars.t_intrc = termios_p->c_cc[VINTR];
  tchars.t_quitc = termios_p->c_cc[VQUIT];
  ltchars.t_suspc = termios_p->c_cc[VSUSP];
  tchars.t_startc = termios_p->c_cc[VSTART];
  tchars.t_stopc = termios_p->c_cc[VSTOP];

  if (ioctl(fd, TIOCSETP, &buf) < 0 ||
      ioctl(fd, TIOCSETC, &tchars) < 0 ||
      ioctl(fd, TIOCSLTC, &ltchars) < 0 ||
#ifdef	TIOCGETX
      ioctl(fd, TIOCSETX, &extra) < 0 ||
#endif
      ioctl(fd, TIOCLSET, &local) < 0)
    return -1;
  return 0;
}


#undef tcgetattr

/* Put the state of FD into *TERMIOS_P.  */
int
tcgetattr (int fd, struct termios *termios_p)
{
  struct sgttyb buf;
  struct tchars tchars;
  struct ltchars ltchars;
  int local;
#ifdef	TIOCGETX
  int extra;
#endif

  if (termios_p == NULL)
    {
      errno = EINVAL;
      return -1;
    }

  if (ioctl(fd, TIOCGETP, &buf) < 0 ||
      ioctl(fd, TIOCGETC, &tchars) < 0 ||
      ioctl(fd, TIOCGLTC, &ltchars) < 0 ||
#ifdef	TIOCGETX
      ioctl(fd, TIOCGETX, &extra) < 0 ||
#endif
      ioctl(fd, TIOCLGET, &local) < 0)
    return -1;

  termios_p->__ispeed = __bsd_speeds[(unsigned char) buf.sg_ispeed];
  termios_p->__ospeed = __bsd_speeds[(unsigned char) buf.sg_ospeed];

  termios_p->c_iflag = 0;
  termios_p->c_oflag = 0;
  termios_p->c_cflag = 0;
  termios_p->c_lflag = 0;
  termios_p->c_oflag |= CREAD | HUPCL;
#ifdef	LPASS8
  if (local & LPASS8)
    termios_p->c_oflag |= CS8;
  else
#endif
    termios_p->c_oflag |= CS7;
  if (!(buf.sg_flags & RAW))
    {
      termios_p->c_iflag |= IXON;
      termios_p->c_cflag |= OPOST;
#ifndef	NOISIG
      termios_p->c_lflag |= ISIG;
#endif
    }
  if ((buf.sg_flags & (CBREAK|RAW)) == 0)
    termios_p->c_lflag |= ICANON;
  if (!(buf.sg_flags & RAW) && !(local & LLITOUT))
    termios_p->c_oflag |= OPOST;
  if (buf.sg_flags & CRMOD)
    termios_p->c_iflag |= ICRNL;
  if (buf.sg_flags & TANDEM)
    termios_p->c_iflag |= IXOFF;
#ifdef	TIOCGETX
  if (!(extra & NOISIG))
    termios_p->c_lflag |= ISIG;
  if (extra & STOPB)
    termios_p->c_cflag |= CSTOPB;
#endif

  switch (buf.sg_flags & (EVENP|ODDP))
    {
    case EVENP|ODDP:
      break;
    case ODDP:
      termios_p->c_cflag |= PARODD;
    default:
      termios_p->c_cflag |= PARENB;
      termios_p->c_iflag |= IGNPAR | INPCK;
      break;
    }
  if (buf.sg_flags & ECHO)
    termios_p->c_lflag |= _ECHO;
  if (local & LCRTERA)
    termios_p->c_lflag |= ECHOE;
  if (local & LCRTKIL)
    termios_p->c_lflag |= ECHOK;
  if (local & LTOSTOP)
    termios_p->c_lflag |= _TOSTOP;
  if (local & LNOFLSH)
    termios_p->c_lflag |= _NOFLSH;

  termios_p->c_cc[VEOF] = tchars.t_eofc;
  termios_p->c_cc[VEOL] = '\n';
  termios_p->c_cc[VERASE] = buf.sg_erase;
  termios_p->c_cc[VKILL] = buf.sg_kill;
  termios_p->c_cc[VINTR] = tchars.t_intrc;
  termios_p->c_cc[VQUIT] = tchars.t_quitc;
  termios_p->c_cc[VSTART] = tchars.t_startc;
  termios_p->c_cc[VSTOP] = tchars.t_stopc;
  termios_p->c_cc[VSUSP] = ltchars.t_suspc;
  termios_p->c_cc[VMIN] = -1;
  termios_p->c_cc[VTIME] = -1;

  return 0;
}
