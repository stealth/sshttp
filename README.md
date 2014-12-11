sshttp - hiding SSH servers behind HTTP servers
===============================================

0. Intro
--------

In case your FW policy forbids _SSH_ access to the DMZ or internal
network from outside, but you still want to use ssh on machines
which only have one open port, e.g. _HTTP_, you can use `sshttpd`.

_sshttpd_ can multiplex the following protocol pairs:

* SSH/HTTP
* SSH/HTTPS
* SSH/SMTP (without SMTP multiline banners)


1. Build
---------

Be sure you run recent Linux kernel and install `nf-conntrack` as well
as libcap/libcap-devel if you want to use the capability feature.

    $ make


1. Setup for one host
----------------------

_sshttpd_ is an easy to use OSI-Layer5 switching daemon. It runs
transparently on HTTP port (`-L` switch, default 80) and decides
on incoming connections whether this is _SSH_ or _HTTP_ traffic.
If its _HTTP_ traffic it switches the traffic to the `HTTP_PORT`
(`-H`, default 8080) and if its _SSH_ traffic to `SSH_PORT` (`-S`, default
22) respectively.

You might need to edit `nf-setup` script to match your ports (`22`, `80` and `8080`
are just fine) and run it to install the proxy rules.
Your _sshd_ has to run on `$SSH_PORT` and your webserver on `$HTTP_PORT`.
Thats basically it. Go ahead and run _sshttpd_ (as root) and it will layer5-switch
your traffic destinated to TCP port 80.

If you want to mux _SMTP_ with _sshttpd_, just give `25` as `-L` parameter, `2525`
as `-H` parameter, and setup your smtp daemon to listen on 2525. Then
edit the `nf-setup` script to match these ports. In the `Makefile`, change the
`SMTP_DOMAIN` and `SSH_BANNER` to your needs (`SSH_BANNER` must match exactly
yours of the running _sshd_).
SMTP/SSH muxing was tested with OpenSSH client and Postfix client and server.

When muxing IPv6 connections, the setup is basically the same; just use the `nf6-setup`
script and invoke _sshttpd_ with `-6`.


Do not forget to `modprobe nf_conntrack_ipv4` (or v6).


2. Transparent proxy setup
--------------------------

You can run _sshttpd_ also on your gateway machine and transparently proxy/mux
all of your HTTP/SSH traffic to your internal LAN. To do so, run _sshttpd_ with
`-T` and use `nf-tproxy` rather than `nf-setup`. Before you do so, carefully
read `nf-tproxy` so you dont lock yourself out of the network.


3. Misc
-------

You dont need to patch any of your ssh/web/smtp client or server software. It
works as is. _sshttpd_ runs only on Linux and needs `IP_TRANSPARENT` support.
It would work without, but by using `IP_TRANSPARENT` it is possible to even
have unmodified syslogs, e.g. the original source IP/port of incoming connections
is passed as-is to the SSH/HTTP/SMTP servers.

Make sure the `nf_conntrack` and `nf_conntrack_ipv4` modules are loaded.
_sshttpd_ is also a tricky anti-SSH0day (if ever:) and anti SSH-scanning/bruteforcing
measurement.
_sshttpd_ has small footprint and was optimized for speed so it also runs
on heavily loaded web servers.


Since version 0.24, _sshttpd_ also supports multiple CPU cores. Unless
`-n 1` is used as switch, _sshttpd_ binds one thread per CPU core,
to better exploit the hardware if running on heavily used web servers.
It still runs this fixed number of threads no matter how many 1000s connection
it handles at the same time.
_sshttpd_ runs as `nobody` user inside a `chroot()` (configurable via `-U` and `-R` switch)
if compiled with `USE_CAPS`. It can also distinguish between _SSH_ and _SSL_
sessions, you just have to use an `LOCAL_PORT (-L)` of 443 or 4433 and change
the `HTTP_PORT` in the `nf-setup` script to match your webservers _HTTPS_ port.
You cannot mix HTTP/SSH and HTTPS/SSH in one _sshttpd_ instance but you can
run two sshttpd's to reach that goal: one on `LOCAL_PORT 80` and one on
`LOCAL_PORT 443`.


Hints/bug reports beyond RTFM to sebastian.krahmer [at] gmail com.

