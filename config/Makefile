# 
#   Copyright (C) 1996 Free Software Foundation, Inc.
#   Written by Michael I. Bushnell, p/BSG.
#
#   This file is part of the GNU Hurd.
#
#   The GNU Hurd is free software; you can redistribute it and/or
#   modify it under the terms of the GNU General Public License as
#   published by the Free Software Foundation; either version 2, or (at
#   your option) any later version.
#
#   The GNU Hurd is distributed in the hope that it will be useful, but
#   WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.

dir := config
makemode := misc

# Files that are copied verbatim to $(sysconfdir).  But we never want
# to overwrite an existing file.  Print a warning for such files.
# If override_conf is set to `t' then install even on top of existing
# files.
SYSCONFFILES = protocols services shells motd ttys hostname fstab passwd group
installed_conf = $(addprefix $(sysconfdir)/,$(SYSCONFFILES))

LOGINDOTS = .bash_login .bashrc .hushlogin .profile README
installed_logins = $(addprefix $(sysconfdir)/login/,$(LOGINDOTS))

ROOTDOTS = .bash_login .bashrc .profile
installed_rootdots = $(addprefix $(prefix)/root/,$(ROOTDOTS))

# Mere symlinks are installed for these
LINKEDCONFFILES = rc
installed_links = $(addprefix $(sysconfdir)/,$(LINKEDCONFFILES))

DIST_FILES=$(SYSCONFFILES) \
	$(addprefix login-,$(LOGINDOTS)) \
	$(addprefix root-,$(ROOTDOTS))

foo=$(shell touch /dev/null)

ifeq ($(override_conf),t)
$(installed_rootdots): /dev/null
$(installed_conf): /dev/null
endif

$(installed_logins): /dev/null

include ../Makeconf

install: $(installed_conf) $(installed_links) $(installed_logins) \
	$(installed_rootdots)

$(installed_rootdots): $(prefix)/root
$(installed_logins): $(sysconfdir)/login

$(prefix)/root $(sysconfdir)/login: %:
	mkdir -p $@

$(installed_rootdots): $(prefix)/root/%: root-%
	$(INSTALL_DATA) $< $(prefix)/root/$*

$(installed_logins): $(sysconfdir)/login/%: login-%
	$(INSTALL_DATA) $< $(sysconfdir)/login/$*

$(installed_conf): $(sysconfdir)/%: %
	$(INSTALL_DATA) $< $(sysconfdir)/$*

$(sysconfdir)/rc:
	ln -s ../libexec/rc $(sysconfdir)/rc
