#!/usr/bin/perl

sub print_copyright
{
    *FILE   = $_[0];
    $prefix = $_[1];
    @copyright = (
        "",
        "",
        "Copyright 2010, 2018 IBM Corp. All rights reserved.",
        "",
        "Redistribution and use in source and binary forms, with or without",
        " modification, are permitted provided that the following conditions",
        "are met:",
        "1. Redistributions of source code must retain the above copyright",
        "   notice, this list of conditions and the following disclaimer.",
        "2. Redistributions in binary form must reproduce the above copyright",
        "   notice, this list of conditions and the following disclaimer in the",
        "documentation and/or other materials provided with the distribution.",
        "3. Neither the name of the copyright holder nor the names of its",
        "   contributors may be used to endorse or promote products derived from",
        "   this software without specific prior written permission.",
        "",
        "THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''",
        "AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE",
        "IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE",
        "ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE",
        "LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR",
        "CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF",
        "SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS",
        "INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN",
        "CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)",
        "ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE",
        "POSSIBILITY OF SUCH DAMAGE.",
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
