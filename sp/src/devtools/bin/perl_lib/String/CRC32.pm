# OF2: pure-perl stand-in for the String::CRC32 CPAN module, which Valve's shader
# build scripts use and Git for Windows' perl doesn't ship. Standard CRC-32
# (the zlib polynomial), same results as the real module.
# buildof2shaders.bat puts this directory on PERL5LIB.
package String::CRC32;

use strict;
use warnings;
require Exporter;

our @ISA = qw(Exporter);
our @EXPORT = qw(crc32);
our $VERSION = '2.000';

my @table;
for my $n ( 0 .. 255 )
{
	my $c = $n;
	for ( 1 .. 8 )
	{
		$c = ( $c & 1 ) ? ( 0xEDB88320 ^ ( $c >> 1 ) ) : ( $c >> 1 );
	}
	$table[$n] = $c;
}

# crc32( STRING or FILEHANDLE [, INITIAL_CRC] )
sub crc32
{
	my ( $data, $init ) = @_;
	my $crc = ( defined $init ? $init : 0 ) ^ 0xFFFFFFFF;

	if ( ref( $data ) || ref( \$data ) eq 'GLOB' )
	{
		binmode( $data );
		my $buffer;
		while ( read( $data, $buffer, 32768 ) )
		{
			$crc = $table[ ( $crc ^ $_ ) & 0xFF ] ^ ( $crc >> 8 ) for unpack( 'C*', $buffer );
		}
	}
	else
	{
		$crc = $table[ ( $crc ^ $_ ) & 0xFF ] ^ ( $crc >> 8 ) for unpack( 'C*', $data );
	}

	return ( $crc ^ 0xFFFFFFFF ) & 0xFFFFFFFF;
}

1;
