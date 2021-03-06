# unix-cb [![Build Status](https://travis-ci.org/ggrossman/unix-cb.svg?branch=master)](https://travis-ci.org/ggrossman/unix-cb)

In middle school and high school in the late 80's / early 90's, I spent an
inordinate amount of time hanging out on [BBS's (Bulletin Board Systems)](http://en.wikipedia.org/wiki/Bulletin_board_system),
which I'd dial up on a 2400-baud modem and block up my family's phone line
for hours on end. My high school friends weren't at my high school...
one lived 10 miles north of me, and the other 20 miles south of me.
We'd talk every day on the BBS's but only occasionally on the phone.
We'd see each other in person maybe once a month. Without the BBS's, I
might not have *had* any friends in high school.

Most BBS's of that era were single-line, running off a PC with a modem
connected to a single dedicated phone line. When you dialed in, there was an
unwritten rule that you should read messages, post whatever messages you
wanted to post, upload/download pirated games, and then get off so that the
next user wouldn't get a busy signal. Any interaction between users was
strictly store-and-forward, either "bulletin boards" (public forums) or mail
(private messages).

There were a few multi-line boards in Orange County, however, and these were
the few places where users could interact in real-time. I spent a lot of time
on one called Morrison Hotel (MoHo). It had 10 lines and unlike most BBS's
which ran on software like PCBoard or WWIV on a IBM PC running MS-DOS,
MoHo was running custom BBS software built on SCO UNIX. It was written by
two Unix wizards, Mike Cantu and [Eric Pederson (@sourcedelica)](https://github.com/sourcedelica),
who worked for a company called US CAD. (US CAD appears to still be around;
see http://www.uscad.com)

The system was called "Skynet." It was text mode and menu-driven, like
everything online in that era, but it was surprisingly usable and
aesthetically pleasing. One of the components of the system was a chat line
called Unix-CB. Unix-CB had a CB metaphor with numbered channels to
separate conversations, although 99% of the time people just hung out
on Channel 1. It had a number of nice touches... all commands started
with `/` and were one character, and you didn't have to hit Enter after
typing the command you wanted. Little things like that made it fast and
a pleasure to use.

I still remember many of the commands:

- `/a` would show you the active users online.
- `/p` would send a private message to another user; after typing `/p` you
  could immediately type the username of the user you wanted
  to PM.
- `/q` logged you off ... but `/g` let you slip away silently, which was
  useful more frequently than you'd think.
- `/5` would turn on the [Jive filter](http://en.wikipedia.org/wiki/Jive_filter),
  a gag text filtering program, written with the [lex](http://en.wikipedia.org/wiki/Lex_%28software%29)
  lexical analyzer generator, which floated around Unix sites in those days.
  It's definitely not politically correct.

One of the best features was `/e` which showed you the list of
other users who were typing at that very moment. This was a distant
ancestor of the feature in iMessage and other modern messaging clients
that indicates graphically that the person you're chatting with is
typing.

There were also secret features like `/.` which let operators slip into
'lurk mode' and watch conversations without showing up as online.
MoHo was a paid site that charged $10/month. Users on MoHo had a character 
indicating their subscription status:

- `#` meant you were unpaid and would be disconnected after 10 minutes. As a
  13-year old kid with no money, I had this status for all but a month or two
  of my time on MoHo. At some point, US CAD changed the timeout for unpaid
  users to 3 minutes, after which I just started calling back
  every 3 minutes. Probably I should've gotten the hint that the site was
  troubled financially at that point and ponied up the $10/month.
- `$` meant you were a paid subscriber.
- `%` meant you were a co-sysop, one of a select group that could enter lurk mode,
  "kill" (disconnect) problem users with the `/k` command, or just "boot" a
  troublesome user back to the MoHo Lobby with the `/b` command.
- `@` was the Godlike status reserved for the sysops, Mike and Eric.

My first computer was an Apple ][, followed by a 512K "Fat Mac." I'd
often put in hours on my parents' PC clones as well. But I desperately
wanted to learn Unix, and this was a few years before Linus Torvalds
democratized access to Unix-like operating systems. At one point, my
friend Steve (another MoHo user) and I learned about a guy who was
giving away an old Unix box for free.  We drove over in his car and
picked it up, and it probably weighed 100 pounds. On the way to get it
home, we got in a car accident in Tustin and his car was totaled. We
were unhurt, but we never did get that Unix box...  it probably got
thrown in the dump.

When I was 16, an uncle passed on a discarded Sun 2 workstation to me.
I knew that Unix-CB was written as multiple processes coordinating via
System V IPC message queues. Another user on MoHo, Brett J. Vickers, now
lead programmer at ArenaNet, had written a game for the site called
Quest for Mordor. His first version of the game also used System V IPC,
but the second version was a single process that multiplexed all I/O via
BSD `select()`, partly because it was a simpler approach with less
concurrency challenges, and mostly because he'd moved the game to a
UCI server running on BSD which had BSD sockets and no System V IPC.

Inspired by Brett, I decided I wanted to learn BSD socket programming,
so I started duplicating Unix-CB using sockets and a `select()` loop at
the core, with every interaction handled using state transitions via
function pointers. I was working from memory to reincarnate the slick
UI of MoHo's Unix-CB; US CAD had pulled the plug on MoHo in 1989 over
a year before I started this work.

When I got to UC Berkeley, the CSUA (Computer Science Undergraduate
Association) ran a machine called `soda.berkeley.edu`. (The CS building
at Cal, which opened my junior year, is called Soda Hall.) As a
CS freshman, I immediately started collecting as many Unix accounts
as possible. When I learned that the CSUA handed out free (LIFETIME!)
Unix accounts on `soda.berkeley.edu`, I immediately joined the CSUA.
I started running my chat server on soda just to try it out.
The soda machine was, however, a very collaborative environment.
Students were constantly communicating with each other using `wall`
broadcast messages, to the point where someone wrote a CSUA-specific
`wallall` utility which had more configuration options than default
`wall`. On soda, anything you did was being watched by every other
CS major on the box. Several other students saw the open port
(I used `5492`, the last four digits of my parents' phone number)
and `telnet`-ed to it.

### Vrave

One of them was named Brian Behlendorf, nickname Vitamin B. He
expressed interest in the chat server and asked if he could have the
code. I happily gave it to him.  He was a leader in the San Francisco
rave scene centered at the 1015 Folsom club and set up a "virtual
rave" server using the Unix-CB code.  "Vrave" would often be set up at
raves as a desktop computer and you could communicate with ravers at
OTHER raves. They added features to the server like the string `BOOM
BOOM BOOM BOOM` being printed occasionally to give a techno feel.
Brian and his fellow developers also undoubtedly fixed numerous bugs
and actually got the software to be stable production code, which
it probably wasn't when I gave it to him.

Even though we were both at Cal at the same time, I never met Brian in
person or even traded more than a few e-mails with him. He, of course,
went on to become one of the founders of the Apache Server and a leading
figure in the open source movement.

His future wife, Laura La Gassa, contacted me at one point and offered
$100 to license the Unix-CB software. I replied that I didn't want any
money but that they were free to use the code for any purpose as long
as my name remained on the copyright. I also wrote that credit should
be given to Skynet since I had basically written an imitation of that
original software.  Laura agreed, and they kept that promise and
inserted a note about Skynet, although probably most people had no
idea who this "Gary Grossman" person was.

Vrave ran for 6 years at hyperreal.org. There is still a page up
about it here: http://hyperreal.org/raves/vrave/

I wasn't paying much attention to Vrave except in the first few months
that it was up. I've never even been to a rave. But I was amazed that
marriages were conducted over it. And unfortunately, it sounds like
there was some serious misbehavior by some bad actors that led to
its demise.

I forget what year this happened, but Eric Pederson, the real original
author of the first Unix-CB software, actually logged into Vrave at
some point and was floored that the UI was more or less identical to
something he had written 10+ years earlier. Through Vrave we reconnected
briefly and he set up an instance of the original Unix-CB and we
both logged into it and chatted.

### The Original Gangsta

After posting this code, I reached out to Eric Pederson. He was intrigued
and posted the code to the original Unix-CB to Github. It's available at
https://github.com/sourcedelica/unix-cb. This was just amazing to me because I
thought I'd never see that code. It also compiled and worked on a modern Linux
box with only a bit of hackery. Regrettably, it sounds like the rest of the
Skynet source code may be forever lost.

### Other flavors

#### telechat-ng

Back in 2000, I wondered whether my code had ever made it into any
open source project. I found a project called `telechat-ng` on the Web,
and I downloaded the tarball and saved it away. The Russian site
that hosted `telechat-ng` is now down, so I've checked that into
https://github.com/ggrossman/telechat-ng for posterity. Since I'm no
longer the sole author of that code, I didn't modify the `LICENSE` file.

I also realized that the maintainer of `telechat-ng` was
[Constantin Kaplinsky](https://twitter.com/glavconst), best known as the
author of TightVNC. So, this Unix-CB code has been worked on by some
notable developers.

That version of the code has been through many hands since I touched it, but
it's still recognizably the crap I wrote in high school. That version of the
code is probably more stable and well-organized than this code, which is more
or less the original code I handed off to Brian Behlendorf.

#### Enormous Trousers Chat

I'm also delighted to report that there's still a server out there,
Enormous Trousers Chat. You can reach it by typing this in a terminal
window:

```
telnet chat.f4.ca 6623
```

#### telechat-py

That server is maintained by [Skye Nott (@skyepn)](https://github.com/skyepn),
who has also written [telechat-py](https://github.com/skyepn/telechat-py), a Python
version of the chat server. I'm not sure whether Enormous Trousers
Chat is running code based on the original C code, or this Python port.

### Installation and Usage

Surprisingly, the code still runs and works on Ubuntu 14.04, even though
I probably last ran it on `soda.berkeley.edu` in 1992 when that was a
Sequent DYNIX box. Usage:

```
./autogen.sh
./configure
make
./cb
telnet localhost 5492
```

With a bit of futzing, I was also able to get the code to compile on
MacOS X Yosemite.

Of course, at this point, it's probably rife with security holes 
(`char buf[256]` was a common and acceptable coding pattern back
in those days) and is little more than a historical amusement.

-Gary Grossman, 11/28/14

### MoHo history

Some history of MoHo from http://bbslist.textfiles.com/714/oldschool.html:

>Morrison Hotel (MoHo) was started in 1986 as a way to learn Unix and
meet girls. It was a 10-line multi-user BBS offering Chat, Message
Boards, Email, Games, File Transfers, Voting, and much more.  "The
sysops were Mike Cantu and myself, both 19 year old (in 1986) college
students going to California State University Long Beach.
>
>Mike and I spent a number of years as interns administering a IBM
mainframe system used by students in our high school district. On that
system we wrote some of the predecessors of MoHo features (in
Fortran!). We also spent a lot of time online during high school on
different BBSes around the country and in Orange County.
>
>The software for the MoHo was developed from scratch by us in C,
running Unix System V/3 on a Convergent Technologies miniframe (first)
and then SCO Unix on a 386 tower. We had ten 2400 baud US Robotics
Sportsters.
>
>The users of the board were primarily college kids and teenagers. It
was a prime place to meet the opposite sex via Chat :) The users got
together for parties all the time.
>
>Because 10 phone lines weren't cheap (neither was the $10,000
Convergent Miniframe), we charged a $10/month flat rate subscription
(we also had a "credits" plan where you were charged for online
usage). Access to the message boards and email were free, as was
limited access to chat. We strongly believed that the basic features
of our bulletin board should always be free.
>
>The chat system we developed was heavily influenced by
Diversidial. Lots of cool and unique features (like Jive mode, if you
remember the old Unix jive filter). A clone of the MoHo chat system
(same UI, different code base) was used by Vrave based at
hyperreal.org
>
>The message boards we developed were very heavily influenced by
PicoSpan (the original software that the Well ran). Featuring threaded
topics, etc.
>
>The email system and the menuing system we developed were heavily
influenced by Forum-PC (probably the best DOS PC BBS software there
was, followed by WWIV)
>
>Among the many games (there were lots of freeware games for Unix) was
a game we wrote called TAC - Tactical Armored Command. This was a
real-time, multiuser tank battle game. People spent hours and hours
playing that game!
>
>File transfers used gz, a freeware Unix file transfer program that
supported x/y/zmodem.
>
>We did not advertise because the phone lines were always busy (tho we
had a 30 minute timeout after which you were hung up on to let someone
else get a chance to get in). We had numerous phone numbers, so that
MoHo would be local dialing to the widest area.
>
>A company called US CAD hired us, and adopted Morrison Hotel to try
to turn it into a profitable business (both from subscriptions and
from developing the software to sell to other sysops). The business
never panned out though, for a number of reasons. We were eventually
laid off from US CAD in 1989 and Morrison Hotel was shut down. We did
not have the motivation to start it back up again.
>
>That is the story of Morrison Hotel." - Eric Pederson
(https://www.linkedin.com/in/ericacm)
