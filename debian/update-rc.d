#! /usr/bin/perl
#
# update-rc.d	Update the links in /etc/rc[0-9S].d/
#
# Version:	@(#)update-rc.d.pl  2.02  05-Mar-1998  miquels@cistron.nl
#

$initd = "/etc/init.d";
$etcd  = "/etc/rc";
$notreally = 0;

# Print usage message and die.

sub usage {
	print STDERR "update-rc.d: error: @_\n" if ($#_ >= 0);
	print STDERR <<EOF;
usage: update-rc.d [-n] [-f] <basename> remove
       update-rc.d [-n] <basename> defaults [NN | sNN kNN]
       update-rc.d [-n] <basename> start|stop NN runlvl [runlvl] [...] .
		-n: not really
		-f: force
EOF
	exit (1);
}

# Check out options.

while($#ARGV >= 0 && ($_ = $ARGV[0]) =~ /^-/) {
	shift @ARGV;
	if (/^-n$/) { $notreally++; next }
	if (/^-f$/) { $force++; next }
	if (/^-h|--help$/) { &usage; }
	&usage("unknown option");
}

# Action.

&usage() if ($#ARGV < 1);
$bn = shift @ARGV;
if ($ARGV[0] ne 'remove') {
    if (! -f "$initd/$bn") {
	print STDERR "update-rc.d: $initd/$bn: file does not exist\n";
	exit (1);
    }
} elsif (-f "$initd/$bn") {
    if (!$force) {
	printf STDERR "update-rc.d: $initd/$bn exists during rc.d purge (use -f to force)\n";
	exit (1);
    } else {
	printf STDERR "update-rc.d: $initd/$bn exists during rc.d purge (continuing)\n";
    }
}

$_ = $ARGV[0];
if    (/^remove$/)       { &checklinks ("remove"); }
elsif (/^defaults$/)     { &defaults; &makelinks }
elsif (/^(start|stop)$/) { &startstop; &makelinks; }
else                     { &usage; }

exit (0);

# Check if there are links in /etc/rc[0-9S].d/ 
# Remove if the first argument is "remove" and the links 
# point to $bn.

sub is_link () {
    my ($op, $fn, $bn) = @_;
    if (! -l $fn) {
	print STDERR "update-rc.d: warning: $fn is not a symbolic link\n";
	return 0;
    } else {
	$linkdst = readlink ($fn);
	if (! defined $linkdst) {
	    die ("update-rc.d: error reading symbolic link: $!\n");
	}
	if (($linkdst ne "../init.d/$bn") && ($linkdst ne "../init.d/$bn")) {
	    print STDERR "update-rc.d: warning: $fn is not a link to ../init.d/$bn\n";
	    return 0;
	}
    }
    return 1;
}

sub checklinks {
    my ($i, $found, $fn, $islnk);

    print " Removing any system startup links for $initd/$bn ...\n"
	if ($_[0] eq 'remove');

    $found = 0;

    foreach $i (0..9, 'S') {
	unless (chdir ("$etcd$i.d")) {
	    next if ($i =~ m/^[789S]$/);
	    die("update-rc.d: chdir $etcd$i.d: $!\n");
	}
	opendir(DIR, ".");
	foreach $_ (readdir(DIR)) {
	    next unless (/^[SK]\d\d$bn$/);
	    $fn = "$etcd$i.d/$_";
	    $found = 1;
	    $islnk = &is_link ($_[0], $fn, $bn);
	    next if ($_[0] ne 'remove');
	    if (! $islnk) {
		print "   $fn is not a link to ../init.d/$bn; not removing\n"; 
		next;
	    }
	    print "   $etcd$i.d/$_\n";
	    next if ($notreally);
	    unlink ("$etcd$i.d/$_") ||
		die("update-rc.d: unlink: $!\n");
	}
	closedir(DIR);
    }
    $found;
}

# Process the arguments after the "defaults" keyword.

sub defaults {
    my ($start, $stop) = (20, 20);

    &usage ("defaults takes only one or two codenumbers") if ($#ARGV > 2);
    $start = $stop = $ARGV[1] if ($#ARGV >= 1);
    $stop  =         $ARGV[2] if ($#ARGV >= 2);
    &usage ("codenumber must be a number between 0 and 99")
	if ($start !~ /^\d\d?$/ || $stop  !~ /^\d\d?$/);

    $start = sprintf("%02d", $start);
    $stop  = sprintf("%02d", $stop);

    $stoplinks[0] = $stoplinks[1] = $stoplinks[6] = "K$stop";
    $startlinks[2] = $startlinks[3] =
	$startlinks[4] = $startlinks[5] = "S$start";

    1;
}

# Process the arguments after the start or stop keyword.

sub startstop {

    my($letter, $NN, $level);

    while ($#ARGV >= 0) {
	if    ($ARGV[0] eq 'start') { $letter = 'S'; }
	elsif ($ARGV[0] eq 'stop')  { $letter = 'K' }
	else {
	    &usage("expected start|stop");
	}

	if ($ARGV[1] !~ /^\d\d?$/) {
	    &usage("expected NN after $ARGV[0]");
	}
	$NN = sprintf("%02d", $ARGV[1]);

	shift @ARGV; shift @ARGV;
	$level = shift @ARGV;
	do {
	    if ($level !~ m/^[0-9S]$/) {
		&usage(
		       "expected runlevel [0-9S] (did you forget \".\" ?)");
	    }
	    if (! -d "$etcd$level.d") {
		print STDERR
		    "update-rc.d: $etcd$level.d: no such directory\n";
		exit(1);
	    }
	    $level = 99 if ($level eq 'S');
	    $startlinks[$level] = "$letter$NN" if ($letter eq 'S');
	    $stoplinks[$level]  = "$letter$NN" if ($letter eq 'K');
	} while (($level = shift @ARGV) ne '.');
	&usage("action with list of runlevels not terminated by \`.'")
	    if ($level ne '.');
    }
    1;
}

# Create the links.

sub makelinks {
    my($t, $i);
    my @links;

    if (&checklinks) {
	print " System startup links for $initd/$bn already exist.\n";
	exit (0);
    }
    print " Adding system startup for $initd/$bn ...\n";

    # nice unreadable perl mess :)

    for($t = 0; $t < 2; $t++) {
	@links = $t ? @startlinks : @stoplinks;
	for($i = 0; $i <= $#links; $i++) {
	    $lvl = $i;
	    $lvl = 'S' if ($i == 99);
	    next if ($links[$i] eq '');
	    print "   $etcd$lvl.d/$links[$i]$bn -> ../init.d/$bn\n";
	    next if ($notreally);
	    symlink("../init.d/$bn", "$etcd$lvl.d/$links[$i]$bn")
		|| die("update-rc.d: symlink: $!\n");
	}
    }

    1;
}
