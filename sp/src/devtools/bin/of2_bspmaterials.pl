use strict;
# OF2: lists the materials a compiled map uses (its texdata string lump), one per line.
# usage: perl of2_bspmaterials.pl <map.bsp>
my ($bsp) = @ARGV;
open(my $f, "<", $bsp) or die "$bsp: $!"; binmode $f;
read($f, my $hdr, 8 + 64 * 16);
my ($ident, $version) = unpack("a4V", $hdr);
die "not a Source BSP\n" unless $ident eq "VBSP";
my ($ofs, $len) = unpack("VV", substr($hdr, 8 + 43 * 16, 8));	# LUMP_TEXDATA_STRING_DATA
seek($f, $ofs, 0); read($f, my $data, $len); close $f;
my %seen;
for my $name (split(/\0/, $data)) {
	$name =~ tr/\\/\//; $name = lc $name;
	next if $name eq "" || $seen{$name}++;
	print "$name\n";
}
