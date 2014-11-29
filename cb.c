/*
  
  cbd.c
  
  "cbd.c" is the main code for the CB simulator.
  
  Copyright (c) 1992, Gary Grossman.  All rights reserved.
  Send comments and questions to: garyg@soda.berkeley.edu
  
  */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <arpa/telnet.h>

char   *malloc ();
char   *ctime ();

void add_recent ();

/* The default port the server is running on */

#define DEF_PORT 5492

/* Maximum screen width */

#define MAXWIDTH 132

/* Lengths of strings used in the program */

#define MAXINPUT 256		/* Maximum length of any input */
#define MAXID 15		/* Maximum length of a user ID */
#define MAXHANDLE 33		/* Maximum length of a handle */
#define MAXMSG 256		/* Maximum length of a message */
#define MAXFMT 64		/* Maximum length of a format string */
#define MAXHOST 64		/* Maximum length of a hostname */
#define MAXPW 8			/* Maximum length of a password */

/* Size of output and stopped outut buffers */

#define MAXSTOPQ 4096
#define MAXOUTQ 2048

/* Version string */

#define VERSION "Unix-CB 1.0 Beta 11/11/92 Revision 01"

/* Secret password to get @ access */

#define SECRET_PW "verminx"

/* Telnet control sequence processing */

#define TS_NONE	0
#define TS_IAC	1
#define TS_WILL	2
#define TS_WONT	3
#define TS_DO	4
#define TS_DONT	5

/* Character-oriented queue for output buffers.
   When it overflows, it "eats" the old data line by line. */

struct queue {
    char   *qbase,
           *qread,
           *qwrite;
    int     qoverflow,
            qsize;
};

/* Structure for user accounts. */

struct account {
    char    id[MAXID],
            handle[MAXHANDLE],
            pw[MAXPW];
    int     chan,
            width,
            level;
    char    nlchar;
unsigned    listed:             1,
            nostat:             1,
            newlines:           1,
            mon:                1;
    char    activefmt[MAXFMT],
            msgfmt[MAXFMT];
};

/* Definition of slot data structure.  Each connection gets a slot. */

struct slot {
    void (*readfunc) ();
    int     tsmode;
    struct account  acct;
    long    acct_pos;
            time_t login_time;
            fd_set squelch, reverse;
    int     inmax;
    char    wrap_base[MAXWIDTH + 1],
           *wrap_ptr;
    struct queue    outq,
                    stopq;
    char    in[MAXINPUT],
           *inp;
    char    pmail[MAXID];
            void (*dispid) ();
            void (*cleanup) ();
    char   *temp;
    char    hostname[MAXHOST];
            time_t last_typed;
unsigned    on:             1,	/* Set if user is logged in and active */
            wrap:           1,  /* Set if word wrap is on */
            lurk:           1,  /* Set if user is "lurking" */
            stopped:        1,  /* Set if incoming input is stopped */
            spy:            1,	/* Set if user can spy */
            gotcr:          1,  /* Set if CR was received */
            echo:           1,  /* Set if input echos back to user */
            wstop:          1;  /* Set if writing to stop queue */
};

/* slotbase is a pointer to the slot array, numslots is the number
   of elements in it.  slot is the currently executing slot. */

struct slot *slotbase,
           *slot;
int     numslots;

/* inet_port is the IP port the program is running on */

int     inet_port = DEF_PORT;

/* startup is the system time at program start up */

time_t startup;

/* login_count is the number of users that have logged in since startup */

int     login_count = 0;

/* used indicates which file descriptor (slots) are used */

fd_set used;

/* sock is the file descriptor for the socket */

int     sock;

/* squelched: returns nonzero if sp is squelching sq */

squelched (sp, sq)
struct slot *sp,
           *sq;
{
    return FD_ISSET (sq - slotbase, &sp -> squelch);
}

/* reversed: returns nonzero if sp is reverse squelching sq */

reversed (sp, sq)
struct slot *sp,
           *sq;
{
    return FD_ISSET (sq - slotbase, &sp -> reverse);
}

/* xatoi: converts an ASCII string to an integer, returns -1 on error */

int     xatoi (s)
char   *s;
{
    register int    i = 0;

    while (*s)
	if (isdigit (*s))
	    i = 10 * i + *s++ - '0';
	else
	    return (-1);
    return i;
}

/* slotname returns a pointer to the slot with user "id" in it, or
   NULL if none exists.  If a user is lurking he will not show up. */

struct slot *slotname (id)
char   *id;
{
    struct slot *sp;
    int     i;

    if (!id[0])
	return NULL;

    if ((i = xatoi (id)) != -1) {
	if (i < 0 || i >= numslots)
	    return NULL;
	sp = slotbase + i;
	if (sp -> on)
	    return (sp -> lurk && !slot -> spy) ? NULL : sp;
	else
	    return NULL;
    }

    for (sp = slotbase; sp < slotbase + numslots; sp++)
	if (sp -> on && !strcmp (sp -> acct.id, id))
	    return (sp -> lurk && !slot -> spy) ? NULL : sp;

    return NULL;
}

/* read_user: reads account with user-id "id" into "acct" */

long    read_user (id, acct)
char   *id;
struct account *acct;
{
    FILE * fp;
    long    pos;

    if ((fp = fopen ("cb.accounts", "rb")) == NULL)
	return - 1;

    pos = 0;

    while (fread (acct, sizeof (struct account), 1, fp)) {
	if (!strcmp (id, acct -> id)) {
	    fclose (fp);
	    return pos;
	}
	pos += sizeof (struct account);
    }

    fclose (fp);
    return - 1;
}

/* delete_user: deletes user with user-id "id" */

delete_user (id)
char   *id;
{
    FILE * fp;
    struct slot *sp;
    long    pos,
            newlen;
    struct account  acct;

    if ((pos = read_user (id, &acct)) < 0)
	return 0;

    if ((fp = fopen ("cb.accounts", "r+b")) == NULL)
	return 0;

    fseek (fp, -(long) sizeof (struct account) , 2);
    if ((newlen = ftell (fp)) != pos) {
	fread (&acct, sizeof acct, 1, fp);
	if ((sp = slotname (acct.id)) != NULL)
	    sp -> acct_pos = pos;
	fseek (fp, pos, 0);
	fwrite (&acct, sizeof acct, 1, fp);
    }
    ftruncate (fileno (fp), newlen);
    fclose (fp);
    return 1;
}

/* create_user: appends "acct" to the account file and returns offset */

long    create_user (acct)
struct account *acct;
{
    FILE * fp;
    long    pos;

    if ((fp = fopen ("cb.accounts", "ab")) == NULL)
	return - 1;
    fseek (fp, 0L, 2);
    pos = ftell (fp);
    fwrite (acct, sizeof (struct account)  , 1, fp);
    fclose (fp);
    return pos;
}

/* write_user: writes account "acct" at account file offset "pos" */

write_user (pos, acct)
long    pos;
struct account *acct;
{
    FILE * fp;

    if ((fp = fopen ("cb.accounts", "r+b")) == NULL)
	if ((fp = fopen ("cb.accounts", "wb")) == NULL)
	    return 0;
    fseek (fp, pos, 0);
    fwrite (acct, sizeof (struct account)  , 1, fp);
    fclose (fp);
    return 1;
}

/* qnext: return the incremented value of queue pointer "s" */

char   *qnext (q, s)
struct queue   *q;
char   *s;
{
    return (s == q -> qbase + q -> qsize - 1) ? q -> qbase : s + 1;
}

/* qprev: return the decremented value of queue pointer "s" */

char   *qprev (q, s)
struct queue   *q;
char   *s;
{
    return (s == q -> qbase) ? q -> qbase + q -> qsize - 1 : s - 1;
}

/* qnextline: return the line-incremented value of queue pointer */

char   *qnextline (q, s)
struct queue   *q;
char   *s;
{
    char    ch;

    for (;;) {
	ch = *s;
	s = qnext (q, s);
	if (ch == '\n')
	    return s;
    }
}

/* qinsert inserts a character into a queue */

void qinsert (q, ch)
struct queue   *q;
char    ch;
{
    *q -> qwrite = ch;
    q -> qwrite = qnext (q, q -> qwrite);
    if (q -> qwrite == q -> qread) {
	q -> qoverflow = 1;
	q -> qread = qnextline (q, q -> qread);
    }
}

/* qempty returns nonzero if a queue is empty, or zero otherwise. */

qempty (q)
struct queue   *q;
{
    return q -> qread == q -> qwrite;
}

/* qflush returns nonzero if a queue is empty, or zero otherwise. */

void qflush (q)
struct queue   *q;
{
    q -> qread = q -> qwrite = q -> qbase;
}

/* qdispose disposes of a queue. */

void qdispose (q)
struct queue   *q;
{
    if (q -> qbase != NULL)
	free (q -> qbase);
}

/* qcreate creates a queue. */

qcreate (q, size)
struct queue   *q;
{
    if ((q -> qbase = malloc (size)) == NULL)
	return 0;
    q -> qsize = size;
    q -> qoverflow = 0;
    q -> qread = q -> qwrite = q -> qbase;
    return 1;
}

/* qwrite transmits data in queue q to slot sp. */

void qwrite (sp, q)
struct slot *sp;
struct queue   *q;
{
    struct iovec    iov[2];

    if (!qempty (q))
	if (q -> qread < q -> qwrite)
	    q -> qread += write (sp - slotbase, q -> qread,
		    q -> qwrite - q -> qread);
	else {
	    iov[0].iov_base = q -> qread;
	    iov[0].iov_len = q -> qsize - (q -> qread - q -> qbase);
	    iov[1].iov_base = q -> qbase;
	    iov[1].iov_len = q -> qwrite - q -> qbase;
	    q -> qread += writev (sp - slotbase, iov, 2);
	    if (q -> qread >= q -> qbase + q -> qsize)
		q -> qread -= q -> qsize;
	}
}

/* qlength returns the current length of a queue */

qlength (q)
struct queue   *q;
{
    if (q -> qread > q -> qwrite)
	return (q -> qwrite - q -> qbase) + q -> qsize - (q -> qread - q -> qbase);
    else
	return (q -> qwrite - q -> qread);
}

/* insert inserts a character into sp's output queue if output is not
   stopped, or the stop queue if sp's output is stopped. */

void insert (sp, ch)
struct slot *sp;
char    ch;
{
    if (ch == '\n')
	insert (sp, '\r');
    qinsert (sp -> wstop ? &sp -> stopq : &sp -> outq, ch);
    if (qlength (&sp -> outq) > sp -> outq.qsize * 3 / 4)
	qwrite (sp, &sp -> outq);
}

/*
  
  Function: setread(func, max)
  
  Purpose:  Sets the "read" function for the current slot to "func".
  The largest line it will receive will be "max" chars long.
  
  */

void setread (func, max)
void (*func) ();
{
    slot -> readfunc = func;
    slot -> inmax = max;
}

/* select_stop selects the stop queue for writing ONLY IF IT IS NECESSARY */

void select_stop (sp)
struct slot *sp;
{
    if (sp -> stopped)
	sp -> wstop = 1;
}

/* clear_stop deselects the stop queue for writing, resuming normal output */

void clear_stop (sp)
struct slot *sp;
{
    sp -> wstop = 0;
}

/* select_wrap selects word wrap */

void select_wrap (sp)
struct slot *sp;
{
    sp -> wrap = 1;
}

/* clear_wrap deselects word wrap */

void clear_wrap (sp)
struct slot *sp;
{
    sp -> wrap = 0;
}

/* insert_buf is used by wrap_writech to write chunks into the output queue */

void insert_buf (sp, buf, n)
struct slot *sp;
char   *buf;
int     n;
{
    while (--n >= 0)
	insert (sp, *buf++);
}

/* wrap_writech is similar to writech but supports word wrap */

void wrap_writech (sp, ch)
struct slot *sp;
char    ch;
{
    char   *space,
            buf[MAXWIDTH + 1];

    if (ch == '\n') {
	insert_buf (sp, sp -> wrap_base, sp -> wrap_ptr - sp -> wrap_base);
	insert (sp, '\n');
	sp -> wrap_ptr = sp -> wrap_base;
    }
    else
	if (isprint (ch)) {
	    *sp -> wrap_ptr++ = ch;
	    if (sp -> wrap_ptr - sp -> wrap_base == sp -> acct.width - 1) {
		for (space = sp -> wrap_ptr; space != sp -> wrap_base; space--)
		    if (isspace (*space)) {
			insert_buf (sp, sp -> wrap_base,
				    space - sp -> wrap_base);
			insert (sp, '\n');
			*sp -> wrap_ptr = '\0';
			strcpy (buf, space + 1);
			strcpy (sp -> wrap_base, "  ");
			strcat (sp -> wrap_base, buf);
			sp -> wrap_ptr = sp -> wrap_base +
			                 strlen (sp -> wrap_base);
			return;
		    }
		insert_buf (sp, sp -> wrap_base,
			    sp -> wrap_ptr - sp -> wrap_base);
		insert (sp, '\n');
		sp -> wrap_ptr = sp -> wrap_base;
	    }
	}
}

/* writech writes a character to slot sp */

void writech (sp, ch)
struct slot *sp;
char    ch;
{
    if (sp -> wrap)
	wrap_writech (sp, ch);
    else
	insert (sp, ch);
}

/* writestr writes a string to slot sp */

void writestr (sp, s)
struct slot *sp;
char   *s;
{
    while (*s)
	writech (sp, *s++);
}

/* writeerr writes an error message before disconnection */

void writeerr (sp, s)
struct slot *sp;
char   *s;
{
    write (sp - slotbase, s, strlen (s));
}

/* writeint writes an integer value to slot sp */

void writeint (sp, i)
struct slot *sp;
int     i;
{
    if (i >= 10)
	writeint (sp, i / 10);
    writech (sp, i % 10 + '0');
}

/* writetwodig writes a two-digit value to slot sp */

void writetwodig (sp, i)
struct slot *sp;
{
    if (i >= 100)
	writeint (sp, i);
    else {
	writech (sp, i / 10 + '0');
	writech (sp, i % 10 + '0');
    }
}

void startqueue (sp)
struct slot *sp;
{
    sp -> stopped = 1;
}

/* clearqueue clears the queue flag and dumps what's in the queue
   in the user's lap, so to speak. */

void clearqueue (sp)
struct slot *sp;
{
    sp -> stopped = 0;
}

void sendpub ();

/* alloctemp allocates temporary space, or prints an error message if
   it cannot be allocated. */

alloctemp (sp, size)
struct slot *sp;
{
    if ((sp -> temp = malloc (size)) == NULL) {
	writestr (sp, "Insufficient memory.\n");
	return 0;
    }
    return 1;
}

/* freetemp frees a slot's temporary space (must exist!) */

void freetemp (sp)
struct slot *sp;
{
    free (sp -> temp);
    sp -> temp = NULL;
}

/* collapse closes a slot.  It disposes of its queue memory, takes it off the
   active list, closes its socket, gets rid of any squelch and reverse flags
   that apply to it, and whatever other cleanup I have to add in the future.
   It can be called to get rid of a person, or after a person is already
   gone. */

void collapse (sp)
struct slot *sp;
{
    struct slot *sq;
    FILE * fp;

    qdispose (&sp -> outq);
    qdispose (&sp -> stopq);

    if (sp -> on) {
	add_recent (sp);
	sp -> on = 0;
	for (sq = slotbase; sq < slotbase + numslots; sq++) {
	    FD_CLR (sp - slotbase, &sq -> squelch);
	    FD_CLR (sp - slotbase, &sq -> reverse);
	}
	if (sp -> acct_pos != -1)
	    write_user (sp -> acct_pos, &sp -> acct);
    }

    if (sp -> cleanup != NULL)
	sp -> cleanup ();
    if (sp -> temp != NULL)
	freetemp (slot);

    FD_CLR (sp - slotbase, &used);
    close (sp - slotbase);
}

void transmit (sp)
struct slot *sp;
{
    if (!qempty (&sp -> outq))
	qwrite (sp, &sp -> outq);

    if (qempty (&sp -> outq) && !qempty (&sp -> stopq) && !sp -> stopped)
	qwrite (sp, &sp -> stopq);
}

void readid2 (id)
char   *id;
{
    struct slot *sp;

    if (!id[0]) {
	setread (sendpub, MAXMSG);
	return;
    }

    if ((sp = slotname (id)) == NULL) {
	writestr (slot, "User not logged in.\n");
	setread (sendpub, MAXMSG);
	return;
    }

    slot -> dispid (sp);
}

void readid (dispatch)
void (*dispatch) ();
{
    slot -> dispid = dispatch;
    setread (readid2, MAXID);
}

char    levels[] = "#$%@";

#define MAXNL 20

shared_spec (con, about, ch)
struct slot *con,
           *about;
char    ch;
{
    static char lc1[] = "([{<";
    static char lc2[] = ")]}>";

    switch (ch) {
	case '%': 
	    writech (con, '%');
	    break;
	case 's': 
	    writetwodig (con, about - slotbase);
	    break;
	case 'S': 
	    writeint (con, about - slotbase);
	    break;
	case 'c': 
	    if (about -> acct.chan == con -> acct.chan || con -> spy ||
		    about -> acct.listed || about -> acct.chan == 1)
		writetwodig (con, about -> acct.chan);
	    else
		writestr (con, "**");
	    break;
	case 'C': 
	    if (about -> acct.chan == con -> acct.chan || con -> spy ||
		    about -> acct.listed || about -> acct.chan == 1)
		writeint (con, about -> acct.chan);
	    else
		writech (con, '*');
	    break;
	case '<': 
	    writech (con, lc1[about -> acct.level]);
	    break;
	case '>': 
	    writech (con, lc2[about -> acct.level]);
	    break;
	case 'u': 
	    writestr (con, about -> acct.id);
	    break;
	case 'h': 
	    writestr (con, about -> acct.handle);
	    break;
	case '$': 
	    writech (con, levels[about -> acct.level]);
	    break;
	case '@': 
	    writestr (con, about -> hostname);
	    break;
	default: 
	    return 0;
    }
    return 1;
}

void writemsg (sp, msg, typ)
struct slot *sp;
char   *msg;
int     typ;
{
    int     nlcount;
    char   *s,
           *t;

    select_stop (sp);
    select_wrap (sp);

    s = sp -> acct.msgfmt;
    while (*s) {
	if (*s == '%')
	    switch (*++s) {
		case '\\': 
		    writech (sp, '\\');
		    break;
		case '_': 
		    writech (sp, '_');
		    break;
		case 'm': 
		    nlcount = 0;
		    while (*msg) {
			if (*msg == slot -> acct.nlchar &&
				sp -> acct.newlines && nlcount < MAXNL) {
			    writestr (sp, "\n  ");
			    nlcount++;
			}
			else
			    writech (sp, *msg);
			msg++;
		    }
		    break;
		default: 
		    if (shared_spec (sp, slot, *s))
			break;
		    writech (sp, '%');
		    if (!*s)
			s--;
		    else
			writech (slot, *s);
	    }
	else
	    if (*s == '_') {
		t = index (++s, '_');
		if (typ == 2) {
		    if (t != NULL)
			*t = '\0';
		    writestr (sp, s);
		    if (t != NULL)
			*t = '_';
		}
		if (t == NULL)
		    break;
		else
		    s = t;
	    }
	    else
		if (*s == '\\') {
		    t = index (++s, '\\');
		    if (typ == 1) {
			if (t != NULL)
			    *t = '\0';
			writestr (sp, s);
			if (t != NULL)
			    *t = '\\';
		    }
		    if (t == NULL)
			break;
		    else
			s = t;
		}
		else
		    writech (sp, *s);
	s++;
    }
    writech (sp, '\n');
    clear_stop (sp);
    clear_wrap (sp);
}

/* Routines for the last users list */

#define MAX_RECENT 20

int     recent_head = 0,
        recent_tail = 0;

struct {
    char    id[MAXID];
            time_t login_time, logout_time;
}       recent_users[MAX_RECENT];

void add_recent (sp)
struct slot *sp;
{
    strcpy (recent_users[recent_tail].id, sp -> acct.id);
    recent_users[recent_tail].login_time = sp -> login_time;
    time (&recent_users[recent_tail].logout_time);
    if (++recent_tail == MAX_RECENT)
	recent_tail = 0;
    if (recent_tail == recent_head)
	if (++recent_head == MAX_RECENT)
	    recent_head = 0;
}

void list_recent () {
    int     i;
    char   *s;

    if ((i = recent_head) == recent_tail)
	writestr (slot, "Nobody has been online\n");
    else
	while (i != recent_tail) {
	    s = ctime (&recent_users[i].login_time);
	    s[24] = '\0';
	    writestr (slot, s);
	    writestr (slot, " to ");
	    s = ctime (&recent_users[i].logout_time);
	    s[24] = '\0';
	    writestr (slot, s);
	    writestr (slot, "   ");
	    writestr (slot, recent_users[i].id);
	    writech (slot, '\n');
	    if (++i == MAX_RECENT)
		i = 0;
	}
}

/* These are the implementations for the various commands. */

void active () {
    struct slot *sp;
    char   *s;
    int     first = 1;

    for (sp = slotbase; sp < slotbase + numslots; sp++)
	if (sp -> on) {
	    if (sp -> lurk && !slot -> spy)
		continue;
	    s = slot -> acct.activefmt;
	    while (*s) {
		if (*s == '%')
		    switch (*++s) {
			case 't': 
			    if (sp != slot && sp -> stopped)
				writestr (slot, " (typing)");
			    if (slot -> acct.level >= 2) {
				if (sp -> lurk)
				    writestr (slot, " {lurk}");
				if (sp -> spy)
				    writestr (slot, " <spy>");
			    }
			    break;
			default: 
			    if (shared_spec (slot, sp, *s))
				break;
			    writech (slot, '%');
			    if (!*s)
				s--;
			    else
				writech (slot, *s);
		    }
		else
		    writech (slot, *s);
		s++;
	    }
	    writech (slot, '\n');
	    first = 0;
	}

    if (first)
	writestr (slot, "Nobody currently online\n");
}

void typing () {
    struct slot *sp;
    int     first = 1;

    for (sp = slotbase; sp < slotbase + numslots; sp++)
	if (sp != slot && sp -> on && sp -> stopped) {
	    if (sp -> lurk && !slot -> spy)
		continue;
	    if (first) {
		first = 0;
		writestr (slot, "Users currently typing:\n");
	    }
	    writestr (slot, "  ");
	    writestr (slot, sp -> acct.id);
	    writech (slot, '/');
	    writestr (slot, sp -> acct.handle);
	    writech (slot, '\n');
	}
    if (first)
	writestr (slot, "Nobody currently typing\n");
}

void operact (from, to, msg)
struct slot *from,
           *to;
char   *msg;
{
    struct slot *sp;

    for (sp = slotbase; sp < slotbase + numslots; sp++)
	if (sp != from && sp -> on && sp -> acct.level >= 2) {
	    select_stop (sp);
	    select_wrap (sp);
	    writestr (sp, "* ");
	    writestr (sp, from -> acct.id);
	    writech (sp, '/');
	    writestr (sp, from -> acct.handle);
	    writech (sp, ' ');
	    writestr (sp, msg);
	    if (to != NULL) {
		writech (sp, ' ');
		writestr (sp, to -> acct.id);
		writech (sp, '/');
		writestr (sp, to -> acct.handle);
	    }
	    writech (sp, '\n');
	    clear_stop (sp);
	    clear_wrap (sp);
	}
}

void paabout (slot, msg, chan)
struct slot *slot;
char   *msg;
int     chan;
{
    struct slot *sp;

    for (sp = slotbase; sp < slotbase + numslots; sp++)
	if (sp -> on) {
	    if (chan && (sp == slot || sp -> acct.chan != slot -> acct.chan))
		continue;
	    select_stop (sp);
	    select_wrap (sp);
	    writestr (sp, "-- #");
	    writeint (sp, slot - slotbase);
	    writech (sp, ' ');
	    writestr (sp, msg);
	    writestr (sp, ": ");
	    writestr (sp, slot -> acct.id);
	    writech (sp, '/');
	    writestr (sp, slot -> acct.handle);
	    writech (sp, '\n');
	    clear_stop (sp);
	    clear_wrap (sp);
	}
}

void kickoff2 (sp)
struct slot *sp;
{
    operact (slot, sp, "kills");
    writestr (sp, "\nAdios, pal.\n");
    transmit (sp);
    collapse (sp);
    writestr (slot, sp -> acct.id);
    writestr (slot, " kicked off.\n");
    paabout (sp, "Kicked off", 0);
    setread (sendpub, MAXMSG);
}

void kickoff () {
    writestr (slot, "Who: ");
    readid (kickoff2);
}

int     user9x[10] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

void channel2 (text)
char   *text;
{
    int     chan;

    if (text[0]) {
	chan = atoi (text);
	if (chan >= 1 && chan <= 99) {
	    if (chan >= 91 && chan <= 99) {
		if (user9x[chan - 91] > chan - 91) {
		    writestr (slot, "Channel busy.\n");
		    setread (sendpub, MAXMSG);
		    return;
		}
		else
		    user9x[chan - 91]++;
	    }
	    if (slot -> acct.chan >= 91 && slot -> acct.chan <= 99)
		user9x[slot -> acct.chan - 91]--;
	    paabout (slot, "Left Channel", 1);
	    slot -> acct.chan = chan;
	    writestr (slot, "Channel changed to ");
	    writestr (slot, text);
	    writestr (slot, ".\n");
	    paabout (slot, "Joined Channel", 1);
	}
	else
	    writestr (slot, "Channel must be a number from 1 to 99.\n");
    }
    setread (sendpub, MAXMSG);
}

void channel () {
    writestr (slot, "Channel: ");
    setread (channel2, 3);
}

void station2 (msg)
char   *msg;
{
    struct slot *sp;

    if (msg[0])
	for (sp = slotbase; sp < slotbase + numslots; sp++)
	    if (sp -> on && !squelched (sp, slot) && !reversed (slot, sp) &&
		    !sp -> acct.nostat)
		writemsg (sp, msg, 2);

    setread (sendpub, MAXMSG);
}

void station () {
    writestr (slot, "Msg: ");
    setread (station2, MAXMSG);
}

void handle2 (handle)
char   *handle; {
    struct slot *sp;
    if (handle[0]) {
    /* Inform @'s and %'s of the handle change */
	for (sp = slotbase; sp < slotbase + numslots; sp++)
	    if (sp != slot && sp -> on && sp -> acct.level >= 2) {
		select_stop (sp);
		select_wrap (sp);
		writestr (sp, "* ");
		writestr (sp, slot -> acct.id);
		writech (sp, '/');
		writestr (sp, slot -> acct.handle);
		writestr (sp, " is now ");
		writestr (sp, slot -> acct.id);
		writech (sp, '/');
		writestr (sp, handle);
		writech (sp, '\n');
		clear_wrap (sp);
		clear_stop (sp);
	    }
	strcpy (slot -> acct.handle, handle);
	writestr (slot, "Handle changed to ");
	writestr (slot, handle);
	writestr (slot, ".\n");
    }
    setread (sendpub, MAXMSG);
}

void handle () {
    writestr (slot, "New handle: ");
    setread (handle2, MAXHANDLE);
}

void pmail3 (msg)
char   *msg;
{
    struct slot *sp,
               *ss;

    if ((sp = slotname (slot -> pmail)) == NULL)
	writestr (slot, "User no longer logged in.\n");
    else
	if (msg[0]) {
	    if (!squelched (sp, slot) && !reversed (slot, sp))
		writemsg (sp, msg, 1);
	/* Send p-mail msg to spying sysops */
	    for (ss = slotbase; ss < slotbase + numslots; ss++)
		if (ss -> on && ss -> spy && ss != sp && ss != slot) {
		    select_wrap (ss);
		    select_stop (ss);
		    writestr (ss, "* ");
		    writestr (ss, slot -> acct.id);
		    writestr (ss, " to ");
		    writestr (ss, sp -> acct.id);
		    writestr (ss, ": ");
		    writestr (ss, msg);
		    writech (ss, '\n');
		    clear_wrap (ss);
		    clear_stop (ss);
		}
	    writestr (slot, "Message sent.\n");
	}
    slot -> pmail[0] = '\0';
    setread (sendpub, MAXMSG);
}

void pmail2 (sp)
struct slot *sp;
{
    strcpy (slot -> pmail, sp -> acct.id);
    setread (pmail3, MAXMSG);
    writestr (slot, "Msg: ");
}

void pmail () {
    writestr (slot, "To: ");
    readid (pmail2);
}

void quit3 () {
    if (*slot -> temp != 'g')
	paabout (slot, "Logged off", 0);
    else
	operact (slot, NULL, "logged off silently");
    transmit (slot);
    collapse (slot);
}

void quit2 (yn)
char   *yn;
{
    if (yn[0] == 'y')
	quit3 ();
    else {
	setread (sendpub, MAXMSG);
	freetemp (slot);
    }
}

void do_quit (type)
char    type;
{
    struct slot *sp;

    if (!alloctemp (slot, sizeof (char))) {
	setread (sendpub, MAXMSG);
	return;
    }

    *slot -> temp = type;

    for (sp = slotbase; sp < slotbase + numslots; sp++)
	if (!strcmp (sp -> pmail, slot -> acct.id)) {
	    writestr (slot, "Someone is sending you a private message.\n");
	    writestr (slot, "Are you sure you want to quit? ");
	    setread (quit2, 2);
	}

    quit3 ();
}

void quit () {
    do_quit ('q');
}

void goodbye () {
    do_quit ('g');
}

void squelch2 (sp)
struct slot *sp;
{
    if (!FD_ISSET (sp - slotbase, &slot -> squelch)) {
	FD_SET (sp - slotbase, &slot -> squelch);
	writestr (slot, "User squelched.\n");
	operact (slot, sp, "squelches");
    }
    else {
	FD_CLR (sp - slotbase, &slot -> squelch);
	writestr (slot, "User unsquelched.\n");
	operact (slot, sp, "unsquelches");
    }
    setread (sendpub, MAXMSG);
}

void squelch () {
    writestr (slot, "Who: ");
    readid (squelch2);
}

void reverse2 (sp)
struct slot *sp;
{
    if (!FD_ISSET (sp - slotbase, &slot -> reverse)) {
	FD_SET (sp - slotbase, &slot -> reverse);
	writestr (slot, "User reversed.\n");
	operact (slot, sp, "reverses");
    }
    else {
	FD_CLR (sp - slotbase, &slot -> reverse);
	writestr (slot, "User unreversed.\n");
	operact (slot, sp, "unreverses");
    }
    setread (sendpub, MAXMSG);
}

void reverse () {
    writestr (slot, "Who: ");
    readid (reverse2);
}

void writetime (slot, t)
struct slot *slot;
long    t;
{
    writetwodig (slot, t / 3600);
    writech (slot, ':');
    writetwodig (slot, t / 60 % 60);
    writech (slot, ':');
    writetwodig (slot, t % 60);
}

void print_time () {
    time_t secs;

    time (&secs);
    writestr (slot, "Current time: ");
    writestr (slot, ctime (&secs));
    writestr (slot, "Time on: ");
    writetime (slot, secs - slot -> login_time);
    writech (slot, '\n');
}

void lurk () {
    slot -> lurk = !slot -> lurk;
    paabout (slot, slot -> lurk ? "Logged off" : "Logged on", 0);
}

void sharedfmthelp () {
    writestr (slot, "The following macros can be used in the format:\n");
    writestr (slot, "%u - User's loginid       %h - User's handle\n");
    writestr (slot, "%s - Slot number          %c - Channel number\n");
    writestr (slot, "%S - Slot w/no leading 0  %C - Channel w/no leading 0\n");
    writestr (slot, "%< - Level open bracket   %> - Level close bracket\n");
    writestr (slot, "%$ - User's level         %@ - User's net location\n");
}

void helpmsgfmt () {
    sharedfmthelp ();
    writestr (slot, "%m - Message text\n\n");
    writestr (slot, "Text enclosed in '\\' is only printed if the message\n");
    writestr (slot, "is a private message.  Similarly, text enclosed in\n");
    writestr (slot, "'_' is only printed if the message is a station\n");
    writestr (slot, "message.\n\n");
    writestr (slot, "Default format:\n");
    writestr (slot, "Slot %s Channel %c %u/%h\\ [Pvt]\\_ [Station]_: %m\n");
}

void helpactivefmt () {
    sharedfmthelp ();
    writestr (slot, "%t - (typing)\n\n");
    writestr (slot, "Default format:\n");
    writestr (slot, "Slot %s Channel %c (%$) %u/%h%t\n");
}

void change_fmt3 (fmt)
char   *fmt;
{
    if (!strcmp (fmt, "?"))
	helpmsgfmt ();
    else
	if (*fmt) {
	    strcpy (slot -> acct.msgfmt, fmt);
	    writestr (slot, "Message format changed.\n");
	}
    setread (sendpub, MAXMSG);
}

void change_fmt4 (fmt)
char   *fmt;
{
    if (!strcmp (fmt, "?"))
	helpactivefmt ();
    else
	if (*fmt) {
	    strcpy (slot -> acct.activefmt, fmt);
	    writestr (slot, "Active format changed.\n");
	}
    setread (sendpub, MAXMSG);
}

void change_fmt2 (maq)
char   *maq;
{
    switch (*maq) {
	case 'm': 
	    writestr (slot, "Your message format is:\n");
	    writestr (slot, slot -> acct.msgfmt);
	    writestr (slot, "\nEnter new format, or CR to keep. ? for help\n");
	    setread (change_fmt3, MAXFMT);
	    break;
	case 'a': 
	    writestr (slot, "Your active format is:\n");
	    writestr (slot, slot -> acct.activefmt);
	    writestr (slot, "\nEnter new format, or CR to keep. ? for help\n");
	    setread (change_fmt4, MAXFMT);
	    break;
	case 'q': 
	    setread (sendpub, MAXMSG);
	    break;
    }
}

void change_fmt () {
    writestr (slot, "M)essage, A)ctive, Q)uit: ");
    setread (change_fmt2, 2);
}

void togglestat () {
    slot -> acct.nostat = !slot -> acct.nostat;
    writestr (slot, slot -> acct.nostat ? "Station messages off.\n" :
	    "Station messages on.\n");
}

void setwidth2 (buf)
char   *buf;
{
    int     i;

    if ((i = atoi (buf)) >= 20 && i <= MAXWIDTH) {
	slot -> acct.width = i;
	writestr (slot, "Screen width changed.\n");
    }
    else
	writestr (slot, "Value must be between 20 and 132.\n");
    setread (sendpub, MAXMSG);
}

void setwidth () {
    writestr (slot, "Current screen width is ");
    writeint (slot, slot -> acct.width);
    writestr (slot, "\nNew value: ");

    setread (setwidth2, 4);
}

void monitor () {
    slot -> acct.mon = !slot -> acct.mon;
    writestr (slot, slot -> acct.mon ? "Monitoring on\n" :
	    "Monitoring off\n");
}

void toggle_nl () {
    slot -> acct.newlines = !slot -> acct.newlines;
    writestr (slot, slot -> acct.newlines ? "Newlines on\n" :
	    "Newlines off\n");
}

void toggle_list () {
    slot -> acct.listed = !slot -> acct.listed;
    writestr (slot, slot -> acct.listed ? "Channels listed\n" :
	    "Channels unlisted\n");
}

void change_nl2 (ch)
char   *ch;
{
    if (*ch != '\0') {
	slot -> acct.nlchar = *ch;
	writestr (slot, "Newline character changed to ");
	writech (slot, slot -> acct.nlchar);
	writech (slot, '\n');
    }
    setread (sendpub, MAXMSG);
}

void change_nl () {
    writestr (slot, "Newline character is ");
    writech (slot, slot -> acct.nlchar);
    writestr (slot, "\nEnter newline character: ");
    setread (change_nl2, 2);
}

void send_pa2 (msg)
char   *msg;
{
    struct slot *sp;
    if (msg[0]) {
	for (sp = slotbase; sp != slotbase + numslots; sp++)
	    if (sp -> on) {
		select_stop (sp);
		select_wrap (sp);
		writestr (sp, "-- ");
		writestr (sp, msg);
		writech (sp, '\n');
		clear_stop (sp);
		clear_wrap (sp);
	    }
	writestr (slot, "Message sent.\n");
    }
    setread (sendpub, MAXMSG);
}

void send_pa () {
    writestr (slot, "Msg: ");
    setread (send_pa2, MAXMSG);
}

void change_pw4 (pw)
char   *pw; {
    if (strcmp (pw, slot -> temp))
	writestr (slot, "Passwords do not match.\n");
    else {
	strcpy (slot -> acct.pw, pw);
	writestr (slot, "Password changed.\n");
	operact (slot, NULL, "changed password");
    }
    slot -> echo = 1;
    freetemp (slot);
    setread (sendpub, MAXMSG);
}

void change_pw3 (pw)
char   *pw; {
    if (!alloctemp (slot, MAXPW)) {
	setread (sendpub, MAXMSG);
	return;
    }
    strcpy (slot -> temp, pw);
    writestr (slot, "Enter password again to verify: ");
    setread (change_pw4, MAXPW);
}

void change_pw2 (pw)
char   *pw; {
    if (strcmp (pw, slot -> acct.pw)) {
	writestr (slot, "Password incorrect.\n");
	slot -> echo = 1;
	setread (sendpub, MAXMSG);
    }
    else {
	writestr (slot, "Enter new password: ");
	setread (change_pw3, MAXPW);
    }
}

void change_pw () {
    writestr (slot, "Enter old password: ");
    slot -> echo = 0;
    setread (change_pw2, MAXPW);
}

void dump_userinfo (acct)
struct account *acct; {
    writestr (slot, "Login: ");
    writestr (slot, acct -> id);
    writech (slot, '\n');
    writestr (slot, "Handle: ");
    writestr (slot, acct -> handle);
    writech (slot, '\n');
    writestr (slot, "Level: ");
    writech (slot, levels[acct -> level]);
    writech (slot, '\n');
    writestr (slot, "Channel: ");
    writeint (slot, acct -> chan);
    if (acct -> listed)
	writestr (slot, " (listed)\n");
    else
	writestr (slot, " (unlisted)\n");
    writestr (slot, "Message format: ");
    writestr (slot, acct -> msgfmt);
    writestr (slot, "\n");
    writestr (slot, "Active format: ");
    writestr (slot, acct -> activefmt);
    writestr (slot, "\n");
    writestr (slot, "Newline character: ");
    writech (slot, acct -> nlchar);
    writestr (slot, "\n");
    writestr (slot, "Newlines ");
    writestr (slot, acct -> newlines ? "on\n" : "off\n");
    writestr (slot, "Monitoring ");
    writestr (slot, acct -> mon ? "on\n" : "off\n");
    writestr (slot, "Station messages ");
    writestr (slot, acct -> nostat ? "off\n" : "on\n");
}

void ystats () {
    dump_userinfo (&slot -> acct);
}

void inquire2 (sp)
struct slot *sp; {
    char   *s;
    time_t secs;
    dump_userinfo (&sp -> acct);
    time (&secs);
    writestr (slot, "Logged in since ");
    s = ctime (&secs);
    s[24] = '\0';
    writestr (slot, s);
    secs -= sp -> last_typed;
    if (secs > 300) {
	writestr (slot, " Idle for ");
	writetime (slot, secs - 300);
    }
    writech (slot, '\n');
    operact (slot, sp, "inquired about");
    setread (sendpub, MAXMSG);
}

void inquire () {
    writestr (slot, "Who: ");
    readid (inquire2);
}

void acc_deluser (id)
char   *id;
{
    struct slot *sp;

    if (id[0]) {
	if ((sp = slotname (id)) != NULL) {
	    if (delete_user (sp -> acct.id)) {
		operact (slot, sp, "deletes");
		writestr (sp, "Your account has been deleted.  Adios, pal.\n");
		transmit (sp);
		sp -> acct_pos = -1;/* Signal no user record */
		collapse (sp);
	    }
	    else
		writestr (slot, "Error deleting user.\n");
	}
	else
	    if (delete_user (id))
		writestr (slot, "User deleted.\n");
	    else
		writestr (slot, "No such user.\n");
    }
    setread (sendpub, MAXMSG);
}

void acc_viewuser (id)
char   *id;
{
    struct account  acct;
    struct slot *sp;

    if (id[0])
	if ((sp = slotname (id)) != NULL) {
	    dump_userinfo (&sp -> acct);
	    writestr (slot, "Password: ");
	    writestr (slot, sp -> acct.pw);
	    writestr (slot, "\n");
	}
	else
	    if (read_user (id, &acct) < 0)
		writestr (slot, "No such user.\n");
	    else {
		dump_userinfo (&acct);
		writestr (slot, "Password: ");
		writestr (slot, acct.pw);
		writestr (slot, "\n");
	    }
    setread (sendpub, MAXMSG);
}

void acc_chlevel2 (lev)
char   *lev;
{
    int     i;
    char   *s;
    struct account  acct;
    struct slot *sp;
    long    pos;

    if ((s = index (levels, *lev)) == NULL)
	writestr (slot, "Not a level.\n");
    else {
	i = s - levels;
	if ((sp = slotname (slot -> temp)) != NULL) {
	    sp -> acct.level = i;
	    write_user (sp -> acct_pos, &sp -> acct);
	}
	else
	    if ((pos = read_user (slot -> temp, &acct)) < 0)
		writestr (slot, "No such user.\n");
	    else {
		acct.level = i;
		write_user (pos, &acct);
	    }
    }
    freetemp (slot);
    setread (sendpub, MAXMSG);
}

void acc_chlevel (id)
char   *id;
{
    if (!id[0]) {
	setread (sendpub, MAXMSG);
	return;
    }
    if (!alloctemp (slot, MAXID)) {
	setread (sendpub, MAXMSG);
	return;
    }
    strcpy (slot -> temp, id);
    writestr (slot, "Level: ");
    setread (acc_chlevel2, 2);
}

void list_users () {
    FILE * fp;
    struct account  acct;

    if ((fp = fopen ("cb.accounts", "rb")) == NULL) {
	writestr (slot, "Error opening cb.accounts.\n");
	return;
    }

    while (fread (&acct, sizeof acct, 1, fp)) {
	writestr (slot, acct.id);
	writech (slot, '/');
	writestr (slot, acct.handle);
	writech (slot, '\n');
    }

    fclose (fp);
}

void accounting (ch)
char   *ch;
{
    switch (*ch) {
	case 'd': 
	    writestr (slot, "Who: ");
	    setread (acc_deluser, MAXID);
	    break;
	case 'v': 
	    writestr (slot, "Who: ");
	    setread (acc_viewuser, MAXID);
	    break;
	case 'c': 
	    writestr (slot, "Who: ");
	    setread (acc_chlevel, MAXID);
	    break;
	case 'l': 
	    list_users ();
	default: 
	    setread (sendpub, MAXMSG);
    }
}

void do_shutdown () {
    struct slot *sp;

    for (sp = slotbase; sp < slotbase + numslots; sp++)
	if (sp - slotbase != sock && FD_ISSET (sp - slotbase, &used)) {
	    writestr (sp, "CB is shutting down.\n");
	    transmit (sp);
	    collapse (sp);
	}
    close (sock);
    free (slotbase);
    exit (0);
}

void _shutdown (yn)
char   *yn;
{
    if (*yn == 'y') {
	operact (slot, NULL, "initiated shut down");
	do_shutdown ();
    }
    else
	setread (sendpub, MAXMSG);
}

struct motd_data {
    FILE * fp;
    int     line;
};

void close_motd () {
    struct motd_data   *md = (struct motd_data *) slot -> temp;
    fclose (md -> fp);
}

void writing_motd (text)
char   *text;
{
    struct motd_data   *md;

    md = (struct motd_data *) slot -> temp;
    if (text[0] == '.' && text[1] == '\0') {
	fclose (md -> fp);
	writestr (slot, "Motd closed.  ");
	writeint (slot, md -> line - 1);
	writestr (slot, " lines written.\n");
	freetemp (slot);
	slot -> cleanup = NULL;
	setread (sendpub, MAXMSG);
    }
    else {
	fputs (text, md -> fp);
	fputc ('\n', md -> fp);
	writeint (slot, ++md -> line);
	writestr (slot, ": ");
    }
}

void write_motd () {
    struct motd_data   *md;

    if (!alloctemp (slot, sizeof (struct motd_data))) {
	setread (sendpub, MAXMSG);
	return;
    }

    md = (struct motd_data *) slot -> temp;

    if ((md -> fp = fopen ("cb.motd", "w")) == NULL) {
	writestr (slot, "Error opening cb.motd\n");
	freetemp (slot);
	setread (sendpub, MAXMSG);
    }

    md -> line = 1;

    slot -> cleanup = close_motd;
    setread (writing_motd, MAXMSG);
    writestr (slot, "Entering motd.  Type . on a blank line when done.\n");
    writestr (slot, "1: ");
}

void show_status () {
    time_t secs;
    struct rusage   rusage;

    writestr (slot, "CB version                 = ");
    writestr (slot, VERSION);
    writestr (slot, "\nProcess ID                 = ");
    writeint (slot, getpid ());
    time (&secs);
    writestr (slot, "\nCurrent time               = ");
    writestr (slot, ctime (&secs));
    writestr (slot, "Program start up time      = ");
    writestr (slot, ctime (&startup));
    writestr (slot, "Total up time              = ");
    writetime (slot, secs - startup);
    writestr (slot, "\nTotal logins since startup = ");
    writeint (slot, login_count);
    writestr (slot, "\nNumber of slots available  = ");
    writeint (slot, numslots);
    getrusage (RUSAGE_SELF, &rusage);
    writestr (slot, "\nUser CPU time consumed     = ");
    writetime (slot, rusage.ru_utime.tv_sec);
    writestr (slot, "\nSystem CPU time consumed   = ");
    writetime (slot, rusage.ru_stime.tv_sec);
    writestr (slot, "\nTotal CPU time consumed    = ");
    writetime (slot, rusage.ru_utime.tv_sec + rusage.ru_stime.tv_sec);
    writech (slot, '\n');
    setread (sendpub, MAXMSG);
}

void op_func2 (ars)
char   *ars;
{
    switch (*ars) {
	case 'a': 
	    writestr (slot, "<L>ist, <D>elete, <V>iew, <C>hange Level: ");
	    setread (accounting, 2);
	    break;
	case 'i': 
	    show_status ();
	    break;
	case 's': 
	    writestr (slot, "This will shut down the CB simulator.\n");
	    writestr (slot, "Are you sure? ");
	    setread (_shutdown, 2);
	    break;
	case 'w': 
	    write_motd ();
	    break;
	case 'p': 
	    slot -> spy = !slot -> spy;
	    writestr (slot, slot -> spy ? "Spy mode active.\n" :
		    "Spy mode inactive.\n");
	    setread (sendpub, MAXMSG);
	    break;
	default: 
	    setread (sendpub, MAXMSG);
    }
}

void off2 (char *yn) {
    if (yn[0] == 'y')
	do_quit ('q');
    else
	setread (sendpub, MAXMSG);
}

void off () {
    writestr (slot, "Sure? ");
    setread (off2, 2);
}

void op_func () {
    writestr (slot, "<A>ccounting, S<p>y, <I>nfo, <W>rite Motd, <S>hutdown: ");
    setread (op_func2, 2);
}

void page_user2 (sp)
struct slot *sp;
{
    writestr (slot, "Paging ");
    writestr (slot, sp -> acct.id);
    writestr (slot, ".\n");
    writestr (sp, "\007\007\007You are being paged by ");
    writestr (sp, slot -> acct.id);
    writestr (sp, ".\n");
    operact (slot, sp, "pages");
    setread (sendpub, MAXMSG);
}

void page_user () {
    writestr (slot, "Who: ");
    readid (page_user2);
}

void help ();

#define MAXCMDS 29

struct {
    char    ch;
    int     level;
            void (*func) ();
    char   *desc;
}       cmds[MAXCMDS] = {
            'a', 0, active, "Show list of active users",
            'c', 1, channel, "Change channel",
            'd', 1, station, "Broadcast station message",
            'e', 0, typing, "Check for other users typing",
            'f', 0, change_fmt, "Change message/active format",
            'g', 1, goodbye, "Silent quit",
            'h', 0, handle, "Change handle",
            'i', 2, inquire, "Inquire about a user",
            'k', 2, kickoff, "Kick off another user",
            'l', 0, toggle_list, "Toggle channel listing",
            'm', 0, monitor, "Toggle monitoring",
            'o', 0, off, "Log off",
            'p', 1, pmail, "Send private message",
            'q', 0, quit, "Quit",
            'r', 1, reverse, "Reverse squelch",
            't', 0, print_time, "Display time",
            '-', 0, list_recent, "List recent users",
            'x', 1, squelch, "Squelch another user",
            'y', 0, ystats, "Your stats",
            'z', 3, send_pa, "Broadcast PA message",
            'j', 0, toggle_nl, "Toggle newlines",
            'u', 0, change_nl, "Change newline character",
            '.', 2, lurk, "Enter lurk mode",
            '3', 0, setwidth, "Set screen width",
            '4', 0, togglestat, "Toggle station messages",
            '%', 1, page_user, "Page another user",
            '&', 0, change_pw, "Change password",
            '=', 3, op_func, "Operator function",
            '?', 0, help, "Help"
};

void help () {
    int     i,
            j;
    writestr (slot, "The following commands are available:\n");
    for (i = 0; i < MAXCMDS; i++)
	if (cmds[i].level <= slot -> acct.level) {
	    writech (slot, '/');
	    writech (slot, cmds[i].ch);
	    writestr (slot, "   ");
	    writestr (slot, cmds[i].desc);
	    if (slot -> acct.width == 80)
		for (j = strlen (cmds[i].desc); j < 35; j++)
		    writech (slot, ' ');
	    else
		writech (slot, '\n');
	}
    writech (slot, '\n');
}

void secret2 (pw)
char   *pw; {
    slot -> echo = 1;
    if (!strcmp (pw, SECRET_PW)) {
	writestr (slot, "Access level changed.\n");
	slot -> acct.level = 3;
	setread (sendpub, MAXMSG);
    }
    else {
	writestr (slot, "Incorrect.\n");
	transmit (slot);
	collapse (slot);
    }
}

void secret () {
    writestr (slot, "Password: ");
    setread (secret2, MAXPW);
    slot -> echo = 0;
}

void exec_cmd (ch)
char    ch; {
    int     i;

    if (ch == '|') {
	secret ();
	return;
    }

    if (ch == 's')
	ch = 'a';		/* /s is same as /a */

    for (i = 0; i < MAXCMDS; i++)
	if (ch == cmds[i].ch) {
	    if (cmds[i].level > slot -> acct.level)
		break;
	    cmds[i].func ();
	    return;
	}
    writestr (slot, "Invalid command, type /? for help.\n");
}

/* sendpub sends an ordinary public message to all interested
   parties. */

void sendpub (text)
char   *text;
{
    struct slot *sp;

    for (sp = slotbase; sp < slotbase + numslots; sp++)
	if (sp -> on && !squelched (sp, slot) && !reversed (slot, sp) &&
		(sp -> acct.chan == slot -> acct.chan ||
		    (slot -> acct.chan == 1 && sp -> acct.mon)))
	    writemsg (sp, text, 0);
}

void read_motd () {
    FILE * fp;
    int     c;

    if ((fp = fopen ("cb.motd", "r")) != NULL) {
	while ((c = getc (fp)) != EOF)
	    writech (slot, c);
	fclose (fp);
    }
}

void enter_cb () {
    read_motd ();
    login_count++;
    writestr (slot, "Entering CB on slot ");
    writetwodig (slot, slot - slotbase);
    writestr (slot, ".  Type /? for help.\n\n");
    setread (sendpub, MAXMSG);
    time (&slot -> login_time);
    slot -> stopped = slot -> lurk = slot -> spy = 0;
    FD_ZERO (&slot -> squelch);
    FD_ZERO (&slot -> reverse);
    slot -> on = 1;
    slot -> pmail[0] = '\0';
    writestr (slot, "Currently active users are:\n");
    active ();
    paabout (slot, "Logged on", 0);
}

void set_initial_values () {
    slot -> acct.nostat = 0;
    slot -> acct.newlines = 1;
    slot -> acct.nlchar = '\\';
    slot -> acct.chan = 1;
    slot -> acct.listed = 0;
    slot -> acct.mon = 0;
    slot -> acct.width = 80;
    slot -> acct.level = 1;
    strcpy (slot -> acct.activefmt,
	    "Slot %s Channel %c %<%$%> %u/%h%t");
    strcpy (slot -> acct.msgfmt,
	    "Slot %s Channel %c %<%u/%h%>\\ [Private]\\_ [Station]_: %m");
}

void get_new_pw (pw)
char   *pw; {
    strcpy (slot -> acct.pw, pw);
    slot -> echo = 1;
    set_initial_values ();
    if ((slot -> acct_pos = create_user (&slot -> acct)) < 0) {
	writestr (slot, "Error creating account.\n\n");
	transmit (slot);
	collapse (slot);
    }
    else
	enter_cb ();
}

void get_new_handle (handle)
char   *handle; {
    strcpy (slot -> acct.handle, handle);
    writestr (slot, "Enter your password: ");
    slot -> echo = 0;
    setread (get_new_pw, MAXPW);
}

void get_new_login (id)
char   *id; {
    char   *s;
    struct account  acct;

 /* Insure that this is a valid ID */
    if (!id[0]) {
	writestr (slot, "Enter a login: ");
	return;
    }

    for (s = id; *s; s++)
	if (!isalnum (*s) || (isalpha (*s) && isupper (*s))) {
	    writestr (slot, "Logins must be alphanumeric lowercase characters.\n");
	    writestr (slot, "Enter login: ");
	    return;
	}
    if (read_user (id, &acct) < 0) {
	strcpy (slot -> acct.id, id);
	writestr (slot, "Enter your handle: ");
	setread (get_new_handle, MAXHANDLE);
    }
    else
	writestr (slot, "That login is already in use, enter another: ");
}

void get_password (pw)
char   *pw; {
    slot -> echo = 1;
    if (!strcmp (pw, slot -> acct.pw))
	enter_cb ();
    else {
	writestr (slot, "Incorrect password.\n\n");
	transmit (slot);
	collapse (slot);
    }
}

void login (id)
char   *id; {
    FILE * fp;
    struct slot *sp;

    if (!strcmp (id, "new")) {
	writestr (slot, "\nWelcome, new user.\n\n");
	writestr (slot, "Enter your loginid to identify yourself: ");
	setread (get_new_login, MAXID);
    }
    else
	if ((slot -> acct_pos = read_user (id, &slot -> acct)) < 0) {
	    writestr (slot, "No account by that name.\n\n");
	    transmit (slot);
	    collapse (slot);
	}
	else
	    if ((sp = slotname (id)) != NULL) {
		writestr (slot, "That user is already logged in.\n\n");
		transmit (slot);
		collapse (slot);
	    }
	    else {
		writestr (slot, "Password: ");
		setread (get_password, MAXPW);
		slot -> echo = 0;
	    }
}

/*
  
  panic is called to report a fatal error and exit while the program
  is installing.
  
  */

void panic (msg)
char   *msg;
{
    perror (msg);
    exit (1);
}

/* initslots clears out the slot table.  It should be called only
   once, at the beginning of the program, in order to create the
   slot table and blank it out. */

void initslots () {
    struct slot *sp;

    numslots = getdtablesize ();
    if ((slotbase = (struct slot   *) malloc (numslots *
					      sizeof (struct slot))) == NULL)
      panic ("cbd: malloc");

    for (sp = slotbase; sp < numslots + slotbase; sp++)
	sp -> on = 0;
}

/*
  
  ts_none processes a single character of text that is not a TELNET control
  sequence (i.e., all user interaction.)  This text is collected into
  a buffer on a line-by-line basis and sent to a "read" function for
  processing.
  
  */

void ts_none (c)
unsigned char   c;
{
    if (c == IAC) {
	slot -> tsmode = TS_IAC;
	return;
    }
    if (slot -> readfunc == sendpub &&
	    slot -> inp == slot -> in)
	startqueue (slot);
    switch (c) {
	case '\r': 
	    slot -> gotcr = 1;
	    break;
	case 0: 
	    if (!slot -> gotcr)
		break;
	case '\n': 
	    slot -> gotcr = 0;
	    if (slot -> readfunc == sendpub &&
		    slot -> inp == slot -> in)
		break;
	    *slot -> inp = 0;
	    slot -> inp = slot -> in;
	    writech (slot, '\n');
	    slot -> readfunc (slot -> in);
	    break;
	case 24: 
	    while (slot -> inp != slot -> in) {
		if (slot -> echo)
		    writestr (slot, "\b \b");
		slot -> inp--;
	    }
	    break;
	case 127: 
	case '\b': 
	    if (slot -> inp != slot -> in) {
		if (slot -> echo)
		    writestr (slot, "\b \b");
		slot -> inp--;
	    }
	    break;
	default: 
	    if (c >= ' ' && c <= '~' &&
		    slot -> inp - slot -> in < slot -> inmax - 1) {
		*slot -> inp++ = c;
		if (slot -> echo)
		    writech (slot, c);
		if (slot -> readfunc == sendpub &&
			slot -> in[0] == '/' &&
			slot -> inp == slot -> in + 2) {
		    writech (slot, '\n');
		    slot -> inp = slot -> in;
		    exec_cmd (slot -> in[1]);
		}
	    }
    }
    if (slot -> readfunc == sendpub &&
	    slot -> inp == slot -> in &&
	    slot -> stopped)
	clearqueue (slot);
    transmit (slot);		/* speed up typing a little */
}

/*
  
  send_ts sends a TELNET control sequence to the client.
  
  */

void send_ts (c, d)
char    c,
        d;
{
    char    code[4];

    code[0] = IAC;
    code[1] = c;
    code[2] = d;
    code[3] = '\0';
    writestr (slot, code);
}

/*

    process_char processes a single character of input.

*/

void process_char (c)
unsigned char   c;
{
    switch (slot -> tsmode) {
	case TS_NONE: 
	    ts_none (c);
	    break;
	case TS_IAC: 
	    switch (c) {
		case WILL: 
		    slot -> tsmode = TS_WILL;
		    break;
		case WONT: 
		    slot -> tsmode = TS_WONT;
		    break;
		case DO: 
		    slot -> tsmode = TS_DO;
		    break;
		case DONT: 
		    slot -> tsmode = TS_DONT;
		    break;
		case EC: 
		    ts_none ('\b');
		    slot -> tsmode = TS_NONE;
		    break;
		case EL: 
		    ts_none (24);
		    slot -> tsmode = TS_NONE;
		    break;
		case AO: 
		    qflush (&slot -> outq);
		    qflush (&slot -> stopq);
		    slot -> tsmode = TS_NONE;/* fixed 11/18 */
		    break;
		case AYT: 
		    writestr (slot, "\n[Yes]\n");
		default: 
		    slot -> tsmode = TS_NONE;
		    break;
	    }
	    break;
	case TS_WILL: 
	    send_ts (DONT, c);
	    slot -> tsmode = TS_NONE;
	    break;
	case TS_WONT: 
	    slot -> tsmode = TS_NONE;
	    break;
	case TS_DO: 
	    if (c != TELOPT_SGA && c != TELOPT_ECHO)
		send_ts (WONT, c);
	    slot -> tsmode = TS_NONE;
	    break;
	case TS_DONT: 
	    slot -> tsmode = TS_NONE;
	    break;
    }
}

/*
  
  process_input reads incoming data from connection fd and sends
  it to process_char for handling.
  
  */

#define MAXBUF 256

void process_input (fd)
int     fd;
{
    unsigned char   buf[MAXBUF],
                   *s;
    int     len,
            on;

    slot = slotbase + fd;
    time (&slot -> last_typed);
    if ((len = read (fd, buf, MAXBUF)) > 0)
	for (s = buf; --len >= 0;)
	    process_char (*s++);
    else {
	on = slot -> on;
	collapse (slot);
	if (on)
	    paabout (slot, "Disconnected", 0);
    }
}

/*
  
  accept_connection() accepts a new connection.
  
  */

void accept_connection () {
    int     len,
            fd,
            yn;
    struct sockaddr_in  sockaddr;
    struct hostent *hp;

    len = sizeof (struct sockaddr);
    if ((fd = accept (sock, (struct sockaddr   *) & sockaddr, &len)) >= 0) {
	slot = slotbase + fd;
	if ((hp = gethostbyaddr (&sockaddr.sin_addr, sizeof (struct in_addr),
				 sockaddr.sin_family)) == NULL)
	  strncpy (slot -> hostname, inet_ntoa (sockaddr.sin_addr),
		   MAXHOST - 1);
	else
	  strncpy (slot -> hostname, hp -> h_name, MAXHOST - 1);
	slot -> hostname[MAXHOST - 1] = '\0';
	yn = 1;
	setsockopt (fd, SOL_SOCKET, SO_LINGER, (char *) & yn, sizeof yn);
	slot -> acct_pos = -1;
	slot -> temp = NULL;
	time (&slot -> last_typed);
	slot -> cleanup = NULL;
	if (!(qcreate (&slot -> outq, MAXOUTQ) &&
	      qcreate (&slot -> stopq, MAXSTOPQ))) {
	    writeerr (slot, "Insufficient memory.\r\n\n");
	    collapse (slot);
	    return;
	}
	FD_SET (fd, &used);
	setread (login, MAXID);
	slot -> gotcr = 0;
	slot -> inp = slot -> in;
	slot -> wrap_ptr = slot -> wrap_base;
	slot -> wstop = slot -> wrap = 0;
	slot -> echo = 1;
	slot -> tsmode = TS_NONE;
	send_ts (WILL, TELOPT_SGA);
	send_ts (WILL, TELOPT_ECHO);
	writestr (slot, "Unix-CB v1.0  Copyright (c) 1992, Gary Grossman.  \
All rights reserved.\n\n");
	writestr (slot, "Type new if you are a new user.\n");
	writestr (slot, "Enter your login: ");
    }
}

void get_write (w)
fd_set * w;
{
    struct slot *sp;

    FD_ZERO (w);
    for (sp = slotbase; sp < slotbase + numslots; sp++)
	if (FD_ISSET (sp - slotbase, &used) && (sp - slotbase != sock) &&
		(!qempty (&sp -> outq) || (!qempty (&sp -> stopq) && !sp -> stopped)))
	    FD_SET (sp - slotbase, w);
}

/*
  
  mainloop() sits on the socket and waits for input from existing
  connections or the knock-knock of new connections being
  established.
  
  New connections are handled by accept_connection().
  Input from old connections is handled by process_input().
  
  */

void mainloop () {
    int     fd;
    fd_set r, w;

    FD_ZERO (&used);
    FD_SET (sock, &used);
    while (1) {
	bcopy ((char *) & used, (char *) & r, sizeof (fd_set));
	get_write (&w);
	if (select (numslots, &r, &w, NULL, NULL) >= 0) {
	    for (fd = 0; fd < numslots; fd++)
		if (FD_ISSET (fd, &used)) {
		    if (FD_ISSET (fd, &r))
			if (fd == sock)
			    accept_connection ();
			else
			    process_input (fd);
		    if (FD_ISSET (fd, &w))
			transmit (slotbase + fd);
		}
	}
    }
}

/* initsock installs the socket the server will be listening for connections
   on. */

void initsock () {
    struct sockaddr_in  sockaddr;

    if ((sock = socket (AF_INET, SOCK_STREAM, 0)) < 0)
	panic ("cbd: socket");

    sockaddr.sin_family = AF_INET;
    sockaddr.sin_addr.s_addr = INADDR_ANY;
    sockaddr.sin_port = htons (inet_port);

    if (bind (sock, (struct sockaddr   *) & sockaddr,
	      sizeof (struct sockaddr)) < 0)
      panic ("cbd: bind");

    if (listen (sock, 10) < 0)
	panic ("cbd: listen");
}

/* Closes the standard input, output and error files, since a daemon
   obviously has no use for them. */

void closestd () {
    close (0);
    close (1);
    close (2);
}

main (argc, argv)
char   *argv[];
{
    if (argc == 2)
	inet_port = atoi (argv[1]);
    time (&startup);
    initsock ();
    initslots ();
    if (fork ())
	exit (0);
    closestd ();
    signal (SIGTERM, do_shutdown);
    mainloop ();
}
