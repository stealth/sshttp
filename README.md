sshttp - hiding SSH servers behind HTTP
=======================================

![sshttp](https://github.com/stealth/sshttp/blob/master/sshttp.jpg)


[![paypal](https://www.paypalobjects.com/en_US/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=9MVF8BRMX2CWA)


0. Intro
--------

In case your FW policy forbids __SSH__ access to the DMZ or internal
network from outside, but you still want to use ssh on machines
which only have one open port, e.g. __HTTP__, you can use `sshttpd`.

_sshttpd_ can multiplex the following protocol pairs:

* SSH/HTTP
* SSH/HTTPS
* SSH/SMTP (without SMTP multiline banners)
* HTTPS SNI multiplexing
* SSH/HTTPS with SNI multiplexing


1. Build
---------

Be sure you run recent Linux kernel and install `nf-conntrack` as well
as `libcap` and `libcap-devel` if you want to use the capability feature.

```
$ make
```

There is a new `splice` branch inside the git. `git checkout splice`
before `make`, if you want to test this new branch. It implements
zero-copy in terms of the __splice(2)__ system call which has a performance
benefit since it avoids copying the network data between user and kernel
land back and forth (__read()/write()__), which could also just be spliced kernel-internally
at the "extra cost" of two additional pipe descriptors per connection.


2. Setup for single host
------------------------

This paragraph describes the setup where all services run on the same host
as _sshttpd_ itself. The muxing happens to the same IP/IP6 address that
the outside connects arrive to, so basically just the ports are changing per
detected service.

_sshttpd_ is an easy to use OSI-Layer5 switching daemon. It runs
transparently on __HTTP(S)__ port (`-L` switch, default 80) and decides
on incoming connections whether this is __SSH__ or __HTTP(S)__ traffic.
If its __HTTP(S)__ traffic, it switches the traffic to the `HTTP_PORT`
(`-H`, default 8080) and if its __SSH__ traffic to `SSH_PORT` (`-S`, default
22) respectively.

You need to edit `nf-setup` script to match your network device and `$PORTS` (`22` and `8080`
are just fine for the SSH/HTTP case) and run it to install the proxy rules.
Your _sshd_ has to run on `$SSH_PORT` and your webserver on `$HTTP_PORT`.
Thats basically it. Go ahead and run _sshttpd_ (as root) and it will layer5-switch
your traffic destinated to TCP port 80:

```
# ./nf-setup
Using network device eth0
Setting up port 22 ...
Setting up port 8080 ...
# ./sshttpd -S 22 -L 80 -H 8080 -U nobody -R /var/empty
sshttpd: Using HTTP_PORT=8080 SSH_PORT=22 and local port=80. Going background. Using caps/chroot.
#
```

If you want to mux __SMTP__ with _sshttpd_, just give `25` as `-L` parameter, `2525`
as `-H` parameter, and setup your smtp daemon to listen on 2525. Then
edit the `nf-setup` script to match these ports. In the `Makefile`, change the
`SMTP_DOMAIN` and `SSH_BANNER` to your needs (`SSH_BANNER` must match exactly
yours of the running _sshd_).
SMTP/SSH muxing was tested with OpenSSH client and Postfix client and server.

When muxing IPv6 connections, the setup is basically the same; just use the `nf6-setup`
script and invoke _sshttpd_ with `-6`.


3. Transparent proxy setup
--------------------------

You can run _sshttpd_ also on your gateway machine and transparently proxy/mux
all of your __HTTP(S)/SSH__ traffic to your internal LAN. To do so, run _sshttpd_ with
`-T` and use `nf-tproxy` rather than `nf-setup` as a template for your FW setup.
Carefully read `nf-tproxy` so you dont lock yourself out of the network and all
the network devices and IP addresses match your setup.

4. SNI Mux
----------

With _sshttpd_ you can also mux based on the HTTPS SNI. Just set up your
`nf-setup` to contain the SNI ports (there are already samples) and invoke
_sshttpd_ with `-N name:port` e.g. `sshttpd -S 22 -H 4433 -L 443 -N drops.v2:7350`
to hide a sshd on 22 and a [drops setup](https://github.com/stealth/drops) on port 7350 behind port 443, and at the same time serving
your webserver from port 4433 to be visible to outside on port 443.
This works because _drops_ sets the SNI of `drops.v2` in outgoing connects.
Multiple `-N` switches are allowed so you could mux a lot of services
via SNI. The ports/services must run all on the same machine where the original request
was destinated to. If you just want to mux based on SNI, you can set the SSH port to 0 via `-S 0`.

5. Misc
-------

You dont need to patch any of your ssh/web/smtp client or server software. It
works as is. _sshttpd_ runs only on Linux and needs `IP_TRANSPARENT` support.
It would work without, but by using `IP_TRANSPARENT` it is possible to even
have unmodified syslogs, e.g. the original source IP/port of incoming connections
is passed as-is to the SSH/HTTP/SMTP servers.

Make sure the `nf_conntrack` and `nf_conntrack_ipv4` or `nf_conntrack_ipv6` modules are loaded.
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
if compiled with `USE_CAPS`. It can also distinguish between __SSH__ and __SSL__
sessions, you just have to use an `LOCAL_PORT (-L)` of 443 or 4433 and change
the `HTTP_PORT` in the `nf-setup` script to match your webservers __HTTPS__ port.
You cannot mix HTTP/SSH and HTTPS/SSH in one _sshttpd_ instance but you can
run two sshttpd's to reach that goal: one on `LOCAL_PORT 80` and one on
`LOCAL_PORT 443`.


6. Alternative docu
-------------------

As per 2017 it seems you have to provide alternative facts for everything,
so here are some good writeups from other people for better understanding or in case my
description was too brief:

* [by stalkr](http://blog.stalkr.net/2012/02/sshhttps-multiplexing-with-sshttp.html)
* [by Will Rouesnel](http://blog.wrouesnel.com/articles/Setting%20up%20sshttp/)
* [by Yves](http://yalis.fr/cms/index.php/post/2014/02/22/Multiplex-SSH-and-HTTPS-on-a-single-port)

