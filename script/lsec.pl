#!/usr/bin/perl

#------------------------------------------------------------------------------
#
#  l s e c  -  List EtherCAT
#
#  Userspace tool for listing EtherCAT slaves.
#
#  $Id$
#
#  Copyright (C) 2006  Florian Pose, Ingenieurgemeinschaft IgH
#
#  This file is part of the IgH EtherCAT Master.
#
#  The IgH EtherCAT Master is free software; you can redistribute it
#  and/or modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2 of the
#  License, or (at your option) any later version.
#
#  The IgH EtherCAT Master is distributed in the hope that it will be
#  useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with the IgH EtherCAT Master; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
#  The right to use EtherCAT Technology is granted and comes free of
#  charge under condition of compatibility of product made by
#  Licensee. People intending to distribute/sell products based on the
#  code, have to sign an agreement to guarantee that products using
#  software based on IgH EtherCAT master stay compatible with the actual
#  EtherCAT specification (which are released themselves as an open
#  standard) as the (only) precondition to have the right to use EtherCAT
#  Technology, IP and trade marks.
#
#------------------------------------------------------------------------------

require 'sys/ioctl.ph';

use strict;
use Getopt::Std;

my $master_index;
my $master_dir;
my $term_width;

#------------------------------------------------------------------------------

$term_width = &get_terminal_width;
&get_options;
&query_master;
exit 0;

#------------------------------------------------------------------------------

sub get_terminal_width
{
    my $winsize;
    die "no TIOCGWINSZ " unless defined &TIOCGWINSZ;
    open(TTY, "+</dev/tty") or die "No tty: $!";
    unless (ioctl(TTY, &TIOCGWINSZ, $winsize='')) {
	die sprintf "$0: ioctl TIOCGWINSZ (%08x: $!)\n", &TIOCGWINSZ;
    }
    (my $row, my $col, my $xpixel, my $ypixel) = unpack('S4', $winsize);
    return $col;
}
#------------------------------------------------------------------------------

sub print_line
{
    for (my $i = 0; $i < $term_width; $i++) {print "-";}
    print "\n";
}

#------------------------------------------------------------------------------

sub query_master
{
    $master_dir = "/sys/ethercat" . $master_index;
    &query_slaves;
}

#------------------------------------------------------------------------------

sub query_slaves
{
    my $dirhandle;
    my $entry;
    my @slaves;
    my $slave;
    my $abs;
	my $line;

    unless (opendir $dirhandle, $master_dir) {
		print "Failed to open directory \"$master_dir\".\n";
		exit 1;
    }

    while ($entry = readdir $dirhandle) {
        next unless $entry =~ /^slave(\d+)$/;

		$slave = {};

		open INFO, "$master_dir/$entry/info" or die
			"ERROR: Failed to open $master_dir/$entry/info";

		while ($line = <INFO>) {
			if ($line =~ /^Name: (.*)$/) {
				$slave->{'name'} = $1;
			}
			elsif ($line =~ /^Ring position: (\d+)$/) {
				$slave->{'ring_position'} = $1;
			}
			elsif ($line =~ /^Advanced position: (\d+:\d+)$/) {
				$slave->{'advanced_position'} = $1;
			}
			elsif ($line =~ /^State: (.+)$/) {
				$slave->{'state'} = $1;
			}
			elsif ($line =~ /^Coupler: ([a-z]+)$/) {
				$slave->{'coupler'} = $1;
			}
		}

		close INFO;

		push @slaves, $slave;
    }
    closedir $dirhandle;

    @slaves = sort { $a->{'ring_position'} <=> $b->{'ring_position'} } @slaves;

    print "EtherCAT bus listing for master $master_index:\n";
    for $slave (@slaves) {
	print_line if $slave->{'coupler'} eq "yes";
	$abs = sprintf "%i", $slave->{'ring_position'};
	printf(" %3s %8s  %-6s  %s\n",
	       $abs, $slave->{'advanced_position'},
	       $slave->{'state'}, $slave->{'name'});
    }
}

#------------------------------------------------------------------------------

sub get_options
{
    my %opt;
    my $optret;

    $optret = getopts "m:h", \%opt;

    &print_usage if defined $opt{'h'} or $#ARGV > -1 or !$optret;

    if (defined $opt{'m'}) {
		$master_index = $opt{'m'};
    }
    else {
		$master_index = 0;
    }
}

#------------------------------------------------------------------------------

sub print_usage
{
	my $cmd = `basename $0`;
	chomp $cmd;
    print "Usage: $cmd [OPTIONS]\n";
    print "        -m <IDX>    Query master <IDX>.\n";
    print "        -h          Show this help.\n";
    exit 0;
}

#------------------------------------------------------------------------------
