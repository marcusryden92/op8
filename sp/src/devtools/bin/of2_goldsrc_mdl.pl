#!/usr/bin/perl
# OF2: Takes a Half-Life 1 (GoldSrc, studio version 10) model apart into what
# Source's studiomdl compiles: one reference .smd, one .smd per sequence, and
# the textures as .tga (sizes brought to powers of two).
#
#   perl of2_goldsrc_mdl.pl model.mdl outdir
#
# Prints the attachments, the textures with their GoldSrc flags and which
# meshes use them: what the .qc and the .vmt files are written from.
# Only models with everything in the one file (no T.mdl, no sequence groups).

use strict;
use warnings;

my ( $file, $out ) = @ARGV;
die "usage: of2_goldsrc_mdl.pl model.mdl outdir\n" unless defined $out;

open( my $fh, '<', $file ) or die "$file: $!\n";
binmode $fh;
my $d = do { local $/; <$fh> };
close $fh;

my ( $id, $ver ) = unpack( 'a4 V', $d );
die "not a GoldSrc model (id $id, version $ver)\n" unless $id eq 'IDST' && $ver == 10;

my ( $nbones, $boneindex, $nbc, $bcindex, $nhb, $hbindex, $nseq, $seqindex, $nsg, $sgindex,
	$ntex, $texindex, $texdataindex, $nskinref, $nskinfam, $skinindex, $nbody, $bodyindex,
	$natt, $attindex ) = unpack( 'V20', substr( $d, 140, 80 ) );

mkdir $out unless -d $out;
mkdir "$out/tex" unless -d "$out/tex";

my $HALFPI = atan2( 1, 1 ) * 2;

#------------------------------------------------------------------------------
# Bones. studiomdl turns everything a quarter turn about Z when it compiles, so
# the root bones are turned back here.
#------------------------------------------------------------------------------
my @bones;
for my $i ( 0 .. $nbones - 1 )
{
	my $o = $boneindex + $i * 112;
	my ( $name, $parent ) = unpack( 'Z32 l', substr( $d, $o, 36 ) );
	my @value = unpack( 'f6', substr( $d, $o + 64, 24 ) );
	my @scale = unpack( 'f6', substr( $d, $o + 88, 24 ) );
	push @bones, { name => $name, parent => $parent, value => \@value, scale => \@scale };
}

sub unturn
{
	my ( $bone, @v ) = @_;
	return @v unless $bone->{parent} == -1;
	return ( $v[1], -$v[0], $v[2], $v[3], $v[4], $v[5] - $HALFPI );
}

# 3x4 matrix as [ r00 r01 r02 tx, r10 r11 r12 ty, r20 r21 r22 tz ]
sub euler_matrix
{
	my ( $px, $py, $pz, $rx, $ry, $rz ) = @_;
	my ( $sy, $cy ) = ( sin( $rz * 0.5 ), cos( $rz * 0.5 ) );
	my ( $sp, $cp ) = ( sin( $ry * 0.5 ), cos( $ry * 0.5 ) );
	my ( $sr, $cr ) = ( sin( $rx * 0.5 ), cos( $rx * 0.5 ) );
	my $x = $sr * $cp * $cy - $cr * $sp * $sy;
	my $y = $cr * $sp * $cy + $sr * $cp * $sy;
	my $z = $cr * $cp * $sy - $sr * $sp * $cy;
	my $w = $cr * $cp * $cy + $sr * $sp * $sy;
	return [
		1 - 2 * $y * $y - 2 * $z * $z, 2 * $x * $y - 2 * $w * $z, 2 * $x * $z + 2 * $w * $y, $px,
		2 * $x * $y + 2 * $w * $z, 1 - 2 * $x * $x - 2 * $z * $z, 2 * $y * $z - 2 * $w * $x, $py,
		2 * $x * $z - 2 * $w * $y, 2 * $y * $z + 2 * $w * $x, 1 - 2 * $x * $x - 2 * $y * $y, $pz ];
}

sub concat
{
	my ( $a, $b ) = @_;
	my @m;
	for my $r ( 0 .. 2 )
	{
		for my $c ( 0 .. 3 )
		{
			my $v = $a->[ $r * 4 ] * $b->[$c] + $a->[ $r * 4 + 1 ] * $b->[ 4 + $c ] + $a->[ $r * 4 + 2 ] * $b->[ 8 + $c ];
			$v += $a->[ $r * 4 + 3 ] if $c == 3;
			$m[ $r * 4 + $c ] = $v;
		}
	}
	return \@m;
}

my @world;
for my $i ( 0 .. $nbones - 1 )
{
	my $b = $bones[$i];
	my $local = euler_matrix( unturn( $b, @{ $b->{value} } ) );
	$world[$i] = $b->{parent} == -1 ? $local : concat( $world[ $b->{parent} ], $local );
}

sub nodes
{
	my $s = "version 1\nnodes\n";
	$s .= sprintf( "%d \"%s\" %d\n", $_, $bones[$_]{name}, $bones[$_]{parent} ) for 0 .. $nbones - 1;
	return $s . "end\n";
}

sub skeleton_line
{
	my ( $i, @v ) = @_;
	return sprintf( "%d %.6f %.6f %.6f %.6f %.6f %.6f\n", $i, unturn( $bones[$i], @v ) );
}

#------------------------------------------------------------------------------
# Textures
#------------------------------------------------------------------------------
sub pot
{
	my $n = shift;
	my $p = 1;
	$p *= 2 while $p * 1.5 < $n;	# nearest in proportion: 116 > 128, 180 > 128 too
	return $p;
}

my @tex;
print "textures (flags: 1 flat, 2 chrome, 4 fullbright, 16 alpha, 32 additive, 64 masked):\n";
for my $i ( 0 .. $ntex - 1 )
{
	my ( $name, $flags, $w, $h, $index ) = unpack( 'Z64 V V V V', substr( $d, $texindex + $i * 80, 80 ) );
	( my $base = lc $name ) =~ s/\.bmp$//;
	$base =~ s/[^a-z0-9_.\-]/_/g;
	push @tex, { name => $base, flags => $flags, w => $w, h => $h };

	my @pal = unpack( '(a3)256', substr( $d, $index + $w * $h, 768 ) );
	$_ = scalar reverse $_ for @pal;	# RGB > BGR

	my ( $tw, $th ) = ( pot($w), pot($h) );
	my @rows;
	if ( $tw == $w && $th == $h )
	{
		for my $y ( 0 .. $h - 1 )
		{
			push @rows, join( '', @pal[ unpack( 'C*', substr( $d, $index + $y * $w, $w ) ) ] );
		}
	}
	else
	{
		# Nearest texel is enough for the few odd sizes
		my @xs = map { int( ( $_ + 0.5 ) * $w / $tw ) } 0 .. $tw - 1;
		for my $y ( 0 .. $th - 1 )
		{
			my $sy = int( ( $y + 0.5 ) * $h / $th );
			my @src = unpack( 'C*', substr( $d, $index + $sy * $w, $w ) );
			push @rows, join( '', @pal[ @src[@xs] ] );
		}
	}

	# Average color, for the materials that stand in for chrome
	my ( $ab, $ag, $ar, $n ) = ( 0, 0, 0, 0 );
	for my $p ( unpack( 'C*', substr( $d, $index, $w * $h ) ) )
	{
		my ( $b, $g, $r ) = unpack( 'C3', $pal[$p] );
		$ab += $b; $ag += $g; $ar += $r; $n++;
	}

	open( my $t, '>', "$out/tex/$base.tga" ) or die "$out/tex/$base.tga: $!\n";
	binmode $t;
	print $t pack( 'C3 v2 C v4 C2', 0, 0, 2, 0, 0, 0, 0, 0, $tw, $th, 24, 0 );
	print $t join( '', reverse @rows );	# bottom row first
	close $t;

	printf "  %2d %-40s flags %2d  %dx%d > %dx%d  average %d %d %d\n", $i, $base, $flags, $w, $h, $tw, $th,
		$ar / $n, $ag / $n, $ab / $n;
}

my @skin = unpack( "s$nskinref", substr( $d, $skinindex, $nskinref * 2 ) );

#------------------------------------------------------------------------------
# Meshes: triangle strips and fans of ( vertex, normal, s, t ), all in the
# space of the bone each vertex belongs to.
#------------------------------------------------------------------------------
sub xform
{
	my ( $m, $x, $y, $z, $rotate_only ) = @_;
	my @r = (
		$m->[0] * $x + $m->[1] * $y + $m->[2] * $z,
		$m->[4] * $x + $m->[5] * $y + $m->[6] * $z,
		$m->[8] * $x + $m->[9] * $y + $m->[10] * $z );
	unless ($rotate_only) { $r[0] += $m->[3]; $r[1] += $m->[7]; $r[2] += $m->[11]; }
	return @r;
}

my $ref = nodes() . "skeleton\ntime 0\n";
$ref .= skeleton_line( $_, @{ $bones[$_]{value} } ) for 0 .. $nbones - 1;
$ref .= "end\ntriangles\n";

my ( $agree, $against ) = ( 0, 0 );
print "meshes:\n";
for my $bp ( 0 .. $nbody - 1 )
{
	my ( $bpname, $nmodels, $base, $modelindex ) = unpack( 'Z64 V V V', substr( $d, $bodyindex + $bp * 76, 76 ) );
	for my $m ( 0 .. $nmodels - 1 )
	{
		my $o = $modelindex + $m * 112;
		my $mname = unpack( 'Z64', substr( $d, $o, 64 ) );
		my ( $type, $radius, $nmesh, $meshindex, $nverts, $vertinfo, $vertindex, $nnorms, $norminfo, $normindex ) =
			unpack( 'V f V8', substr( $d, $o + 64, 40 ) );

		my @vbone = unpack( "C$nverts", substr( $d, $vertinfo, $nverts ) );
		my @nbone = unpack( "C$nnorms", substr( $d, $norminfo, $nnorms ) );
		my @v = unpack( 'f' . ( $nverts * 3 ), substr( $d, $vertindex, $nverts * 12 ) );
		my @n = unpack( 'f' . ( $nnorms * 3 ), substr( $d, $normindex, $nnorms * 12 ) );

		my ( @wv, @wn );
		for my $i ( 0 .. $nverts - 1 )
		{
			$wv[$i] = [ xform( $world[ $vbone[$i] ], @v[ $i * 3 .. $i * 3 + 2 ] ) ];
		}
		for my $i ( 0 .. $nnorms - 1 )
		{
			$wn[$i] = [ xform( $world[ $nbone[$i] ], @n[ $i * 3 .. $i * 3 + 2 ], 1 ) ];
		}

		for my $me ( 0 .. $nmesh - 1 )
		{
			my ( $ntris, $triindex, $skinref ) = unpack( 'V3', substr( $d, $meshindex + $me * 20, 12 ) );
			my $t = $tex[ $skin[$skinref] ];
			my ( $count, $smin, $smax, $tmin, $tmax ) = ( 0, 1e9, -1e9, 1e9, -1e9 );

			my $p = $triindex;
			while (1)
			{
				my $num = unpack( 's', substr( $d, $p, 2 ) );
				$p += 2;
				last if $num == 0;
				my $fan = $num < 0;
				$num = -$num if $fan;

				my @c;
				for ( 1 .. $num )
				{
					push @c, [ unpack( 's4', substr( $d, $p, 8 ) ) ];
					$p += 8;
				}

				for my $i ( 0 .. $num - 3 )
				{
					# The order the engine draws them in, which is clockwise seen from outside
					my @tri = $fan ? ( $c[0], $c[ $i + 1 ], $c[ $i + 2 ] )
						: ( $i & 1 ) ? ( $c[ $i + 1 ], $c[$i], $c[ $i + 2 ] )
						: ( $c[$i], $c[ $i + 1 ], $c[ $i + 2 ] );

					# Check that against the normals
					my ( $a, $b, $cc ) = map { $wv[ $_->[0] ] } @tri;
					my @e1 = map { $b->[$_] - $a->[$_] } 0 .. 2;
					my @e2 = map { $cc->[$_] - $a->[$_] } 0 .. 2;
					my @g = ( $e1[1] * $e2[2] - $e1[2] * $e2[1], $e1[2] * $e2[0] - $e1[0] * $e2[2], $e1[0] * $e2[1] - $e1[1] * $e2[0] );
					my $dot = 0;
					for my $c ( @tri ) { $dot += $g[$_] * $wn[ $c->[1] ][$_] for 0 .. 2; }
					if ( $dot > 0 ) { $against++; } elsif ( $dot < 0 ) { $agree++; }

					# An .smd has them the other way round
					$ref .= "$t->{name}\n";
					for my $c ( reverse @tri )
					{
						my ( $vi, $ni, $s, $tt ) = @$c;
						$smin = $s if $s < $smin; $smax = $s if $s > $smax;
						$tmin = $tt if $tt < $tmin; $tmax = $tt if $tt > $tmax;
						$ref .= sprintf( "%d %.6f %.6f %.6f %.6f %.6f %.6f %.6f %.6f\n", $vbone[$vi],
							@{ $wv[$vi] }, @{ $wn[$ni] }, $s / $t->{w}, 1 - $tt / $t->{h} );
					}
					$count++;
				}
			}

			printf "  %-10s %-45s %-36s %5d tris  s %d..%d t %d..%d\n", $bpname, $mname, $t->{name}, $count,
				$smin, $smax, $tmin, $tmax;
		}
	}
}
$ref .= "end\n";

( my $basename = $file ) =~ s/.*[\/\\]//;
$basename =~ s/\.mdl$//i;
open( my $r, '>', "$out/${basename}_ref.smd" ) or die $!;
print $r $ref;
close $r;
print "winding: $agree triangles clockwise against their normals, $against the other way\n";

#------------------------------------------------------------------------------
# Sequences: per bone six channels, each either the bone's own value or a run
# length coded list of offsets from it
#------------------------------------------------------------------------------
sub channel
{
	my ( $p, $nframes ) = @_;
	my @val;
	while ( @val < $nframes )
	{
		my ( $valid, $total ) = unpack( 'C2', substr( $d, $p, 2 ) );
		my @run = unpack( "s$valid", substr( $d, $p + 2, $valid * 2 ) );
		for my $k ( 0 .. $total - 1 )
		{
			push @val, $k < $valid ? $run[$k] : $run[ $valid - 1 ];
		}
		$p += ( $valid + 1 ) * 2;
		last if $total == 0;
	}
	return @val;
}

print "sequences:\n";
for my $s ( 0 .. $nseq - 1 )
{
	my $o = $seqindex + $s * 176;
	my ( $label, $fps, $flags ) = unpack( 'Z32 f V', substr( $d, $o, 40 ) );
	my $nframes = unpack( 'V', substr( $d, $o + 56, 4 ) );
	my ( $nblends, $animindex ) = unpack( 'V2', substr( $d, $o + 120, 8 ) );
	my $seqgroup = unpack( 'V', substr( $d, $o + 156, 4 ) );
	die "$label is in sequence group $seqgroup\n" if $seqgroup != 0;

	my @frames;	# [bone][channel] = list
	for my $b ( 0 .. $nbones - 1 )
	{
		my $pa = $animindex + $b * 12;
		my @off = unpack( 'v6', substr( $d, $pa, 12 ) );
		for my $j ( 0 .. 5 )
		{
			$frames[$b][$j] = $off[$j] ? [ channel( $pa + $off[$j], $nframes ) ] : undef;
		}
	}

	my $smd = nodes() . "skeleton\n";
	for my $f ( 0 .. $nframes - 1 )
	{
		$smd .= "time $f\n";
		for my $b ( 0 .. $nbones - 1 )
		{
			my @v = @{ $bones[$b]{value} };
			for my $j ( 0 .. 5 )
			{
				$v[$j] += $frames[$b][$j][$f] * $bones[$b]{scale}[$j] if $frames[$b][$j];
			}
			$smd .= skeleton_line( $b, @v );
		}
	}
	$smd .= "end\n";

	open( my $a, '>', "$out/$label.smd" ) or die $!;
	print $a $smd;
	close $a;
	printf "  %-12s %3d frames at %g fps%s, %d blend(s)\n", $label, $nframes, $fps, ( $flags & 1 ) ? ', looping' : '', $nblends;
}

print "attachments:\n";
for my $i ( 0 .. $natt - 1 )
{
	my $o = $attindex + $i * 88;
	my ( $name, $type, $bone ) = unpack( 'Z32 V V', substr( $d, $o, 40 ) );
	my @org = unpack( 'f3', substr( $d, $o + 40, 12 ) );
	printf "  %d \"%s\" %.3f %.3f %.3f\n", $i, $bones[$bone]{name}, @org;
}
