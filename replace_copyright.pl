#!/usr/bin/perl

sub print_copyright
{
    *FILE   = $_[0];
    $prefix = $_[1];
    @copyright = (
        "",
        "",
        "Add license text here !!",
        "",
        "",
        );

    foreach $line ( @copyright ) {
        $p = "$prefix$line";
        $p =~ s/\s+$//;
        print FILE "$p\n";
    }
}

$org_file_name = "$ARGV[0].org";
$in_copyright = 0;

rename($ARGV[0], $org_file_name) || die "Cannot rename $ARGV[0]";

open(ORG, "<$org_file_name") || die "Cannot open $org_file_name";
open(NEW, ">$ARGV[0]") || die "Cannot open $ARGV[0]";

while (<ORG>) {
    if ($in_copyright == 0) {
        if ($_ =~ /^(\s*\S+\s*)OO_Copyright_BEGIN/) {
            $in_copyright = 1;
            $prefix = $1;
            print "Start copyright section: $ARGV[0]\n";
            print "Prefix = \"$prefix\"\n";
            print NEW $_;
            print_copyright(*NEW, $prefix);
        } elsif ($_ =~ /^OO_Copyright_BEGIN/) {
            $in_copyright = 1;
            print "Start copyright section: $ARGV[0]\n";
            print NEW $_;
            print_copyright(*NEW, "");
        }
    } else {
        if ($_ =~ /^(\s*\S+\s*)OO_Copyright_END/ || $_ =~ /^OO_Copyright_END/) {
            $in_copyright = 0;
            print "End copyright section: $ARGV[0]\n";
        }
    }

    if ($in_copyright == 0) {
        $_ =~ s/([Ll])ong ([Tt])erm ([Ff])ile ([Ss])ystem/$1inear $2ape $3ile $4ystem/g;
        print NEW $_;
    }
}

close(ORG);
close(NEW);

unlink($org_file_name);
