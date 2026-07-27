#!/usr/bin/perl
# read or edit HamClock's eeprom file IN PLACE
# version 2.01 
# revision log:
#   2026: updated to handle optional nvram .h files

use strict;
use warnings;

# majic cookie value before each value
my $COOKIE = 0x5A;

# layout indent
my $INDENT = "                                          ";

# default locations
my $eefile = "$ENV{HOME}/.hamclock/eeprom";
my $srcdir = "$ENV{PWD}";
my $hhfile1 = "nvramlen.h";
my $hhfile2 = "nvramenum.h";
my $hhfile = "HamClock.h";

my $nvfile = "nvram.cpp";
my $nvh    = "nvramsize.h";

# crack args
my ($verbose, $byname, $byaddr, $palA, $palB, $help, $nvname, $nvvalu, $deptoo);
while (@ARGV > 0 and $ARGV[0] =~ /^-/) {
    my $arg = shift;
       if ($arg eq "-v") { $verbose = 1; }
    elsif ($arg eq "-A") { $palA = 1; }
    elsif ($arg eq "-B") { $palB = 1; }
    elsif ($arg eq "-e") { $eefile = shift; }
    elsif ($arg eq "-s") { $srcdir = shift; }
    elsif ($arg eq "-a") { $byaddr = 1; }
    elsif ($arg eq "-l") { $byname = 1; }
    elsif ($arg eq "-d") { $deptoo = 1; }
    else                 { $help = 1; }
}
if (@ARGV == 1) { $nvname = shift; }
elsif (@ARGV == 2) { $nvname = shift; $nvvalu = shift; }
elsif (@ARGV > 0 or (!$byname and !$byaddr) or $help) { &usage(); }

# read eeprom file into array @eeprom of values given addrs
my @eeprom;                     # value at address index
open my $EE, "<", $eefile or die "$eefile: $!\n";
while (<$EE>) {
    chomp();
    next unless (/^(\S+) (\S+)$/);
    my ($addr, $value) = (hex($1), hex($2));
    # just use first occurance because of old bug
    $eeprom[$addr] = $value unless exists $eeprom[$addr];
    #printf STDERR "%X %X\n", $addr, $eeprom[$addr];
}
close $EE;

# if just showing palettes we only need the eeprom file
if ($palA) { &showPalette(0xF92); exit 0; }
if ($palB) { &showPalette(0xFC9); exit 0; }




sub parse_hhfile {
    my ($filepath, $hhdefs, $hhlen, $hhdesc) = @_;

    open my $HH, "<", $filepath or do {
        warn "Skipping $filepath: $!\n";
        return;
    };

    while (<$HH>) {
        chomp();
        if (/^#define\s+(\S+)\s+(\d+)/) {
            # capture all numeric #defines
            my ($nam, $val) = ($1, $2);
            $hhdefs->{$nam} = $val;
        }
        if (/^#define\s+(NV_\S+)\s+(\S+)/) {
            # capture all NV_ #defines, use $hhdefs if value is another define
            my ($nam, $val) = ($1, $2);
            $val = $hhdefs->{$val} if defined($hhdefs->{$val});  # indirection
            $hhlen->{$nam} = $val;
        }
        if (/^\s*(NV_[^,]+),\s*\/\/\s*(.*)/) {
            # capture NV descriptions
            my ($nam, $desc) = ($1, $2);
            $hhdesc->{$nam} = $desc;
        }
    }
    close $HH;
}

# call for each file, skipping missing ones
my %hhdefs;
my %hhlen;
my %hhdesc;

my @hhfiles = ($hhfile1, $hhfile2, $hhfile);  # hamclock.h files
for my $file (@hhfiles) {
    parse_hhfile("$srcdir/$file", \%hhdefs, \%hhlen, \%hhdesc);
}

# read nvram.cpp into hash %addr of address of first byte and hash %size of n bytes given name
my %addr;                       # first EEPROM address given name
my %size;                       # n bytes given name
#
# Old versions have one file nvram.cpp with all the information
# new versions have 

# open up nvram.cpp to get value of $NV_BASE
open my $NV, "<", "$srcdir/$nvfile" or die "$srcdir/$nvfile: $!\n";
my $offset = 1;                 # start with cookie
my $NV_BASE;
while (<$NV>) {
    chomp();
    $NV_BASE = int($1) if (/^#define\s+NV_BASE\s+(\d+)/);
    next unless (defined($NV_BASE));
    last
}
close $NV;


#
# size initializers are moved to nvramsize header - process them if present
if (open my $NV, "<", "$srcdir/$nvh") {
    while (<$NV>) {
        chomp();
        next unless (/^\s+([^,]+),\s+\/\/ (NV_\S+)/);
        my ($nbytes, $nam) = ($hhlen{$1} // $1, $2);
        $addr{$nam} = $offset + $NV_BASE;
        $size{$nam} = $nbytes;
        $offset += 1 + $nbytes;     # cookie and length
        # printf STDERR "%s %x\n", $nam, $addr{$nam};
    }
    close $NV;
}

# read size initializers - skipping past anylines before NV_BASE for compatibility 
my $NV_BASE_TOSS;
open $NV, "<", "$srcdir/$nvfile" or die "$srcdir/$nvfile: $!\n";
while (<$NV>) {
    chomp();
    $NV_BASE_TOSS = int($1) if (/^#define\s+NV_BASE\s+(\d+)/);
    next unless (defined($NV_BASE_TOSS));
    next unless (/^\s+([^,]+),\s+\/\/ (NV_\S+)/);
    my ($nbytes, $nam) = ($hhlen{$1} // $1, $2);
    $addr{$nam} = $offset + $NV_BASE;
    $size{$nam} = $nbytes;
    $offset += 1 + $nbytes;     # cookie and length
    # printf STDERR "%s %x\n", $nam, $addr{$nam};
}
close $NV;


# read all .cpp to get type from NVRead/Write
my %type;                       # hash of type given name
for my $fn (<"$srcdir/*.cpp">) {
    open my $SF, "<", $fn or die "$fn: $?\n";
    while (<$SF>) {
        my ($type, $name) = /NV(?:Read|Write)([^\( ]+).*(NV_[^, ]+)/;
        $type{$name} = $type if (defined($name));
    }
    close $SF;
}
# foreach my $n (keys %type) { printf "%s %s\n", $n, $type{$n}; }

# list all else read or write nvname
if ($byname) {
    # skip OLD unless want them too
    print "# Name                   Addr   Len Type  Description\n";
    for my $name (sort keys %addr) {
        next unless ($deptoo or !($name =~ m/_OLD/));
        printf "%-20s    0x%03X %3d    %3s  %s\n",
                                $name, $addr{$name}, $size{$name}, &getType($name), $hhdesc{$name};

        &NVprint($INDENT, $name);
    }
} elsif ($byaddr) {
    # skip OLD unless want them too
    print "# Addr   Name                   Len Type  Description\n";
    my %name = reverse %addr;        # make hash of name given addr
    for my $addr (sort { $a <=> $b } keys %name) {
        my $name = $name{$addr};
        next unless ($deptoo or !($name =~ m/_OLD/));
        printf "0x%03X %-20s    %3d    %3s  %s\n",
                                $addr{$name}, $name, $size{$name}, &getType($name), $hhdesc{$name};
        &NVprint($INDENT, $name);
    }
} else {
    # print value of $nvname or set to $nvvalu of given
    defined($nvvalu) ? &NVset($nvname,$nvvalu) : &NVprint("", $nvname);
}

# done
exit;



###############################################################################
#
# supporting subs
#
###############################################################################

# return type of name as "i", "s", "f" or "RGB", using %type or heuristic
sub getType
{
    my $nam = shift;
    return "RGB" if ($nam =~ m/COLOR/);
    return "f"   if (($type{$nam}//0) eq "Float");      # from any NV{Read,Write}Float
    return "s"   if (($type{$nam}//0) eq "String");     # from any NV{Read,Write}String
    return "s"   if ($nam =~ m/_OLD/);                  # default for deprecated values not in source code
    return "i";                                         # overall default
}

# print usage and exit
sub usage
{
    $0 =~ s:.*/::;     # basename
    print STDERR "Purpose: read or modify HamClock's config file at $eefile\n";
    print STDERR "Usage: $0 options NV_XXX [value]\n";
    print STDERR "where options:\n";
    print STDERR "   -A   : show palette A\n";
    print STDERR "   -B   : show palette B\n";
    print STDERR "   -e p : set path to eeprom, default is $eefile\n";
    print STDERR "   -s p : set path to src, default is $srcdir\n";
    print STDERR "   -v   : also show offset and size\n";
    print STDERR "   -l   : list all name, address, size, type and description sorted by name\n";
    print STDERR "   -a   : like -l but sorted by address\n";
    print STDERR "   -d   : show deprecated values also\n";
    print STDERR " NV_XXX : one of the names in the NV_Name enum in HamClock.h (list with -l)\n";
    print STDERR "[value] : optional new value\n";
    exit 1;
}

# set the value of NV name to value
sub NVset
{
    # collect args
    my $nam = shift;
    my $val = shift;
    my $typ = &getType($nam);

    # allow for hex
    $val = hex($val) if ($val =~ m/0x/);

    # get starting addr and size
    my $addr = $addr{$nam};
    $addr or die "$nam not defined\n";
    my $size = $size{$nam};

    # start with cookie just before addr
    @eeprom[$addr-1] = $COOKIE;

    # edit size bytes
    if ($typ eq "s") {
        my @chars = unpack("C*",$val);                          # N.B val may be shorter than size
        push @chars, (0) x ($size - scalar @chars);             # pad with 0s out to size
        @eeprom[$addr .. $addr+$size-1] = @chars;
    } elsif ($size == 1) {
        die "Type must be i\n" unless ($typ eq "i");
        $eeprom[$addr] = $val;
    } elsif ($size == 2) {
        if ($typ eq "i") {
            @eeprom[$addr .. $addr+1] = unpack("C2",pack("s",$val));
        } elsif ($typ eq "RGB") {
            my ($R,$G,$B) = ($val =~ m/^(\d+),(\d+),(\d+)$/);
            die "RGB format must be xxx,yyy,zzz\n" unless defined($B);
            my $rgb = (($R & 0xF8) << 8) | (($G & 0xFC) << 3) | ($B >> 3);
            @eeprom[$addr .. $addr+1] = unpack("C2",pack("s",$rgb));
        } else {
            die "Type must be i or RGB\n";
        }
    } elsif ($size == 4) {
        if ($typ eq "i") {
            @eeprom[$addr .. $addr+3] = unpack("C4",pack("i",$val));
        } elsif ($typ eq "f") {
            @eeprom[$addr .. $addr+3] = unpack("C4",pack("f",$val));
        } else {
            die "Type must be i or f\n";
        }
    } elsif ($typ eq "i") {
        my @bytes = split (' ', $val);
        die "Must supply exactly $size decimal bytes\n" unless (@bytes == $size);
        @eeprom[$addr .. $addr+$size-1] = @bytes;
    } else {
        die "Can not use type f with $nam\n" if ($typ eq "f");
        die "Unknown data type $typ\n";
    }

    # overwrite
    open my $NE, ">", $eefile or die "$eefile: $!\n";
    for (my $addr = 0; $addr < scalar @eeprom; $addr++) {
        printf $NE "%08X %02X\n", $addr, $eeprom[$addr];
    }
    close $NE;

    # confirm
    &NVprint ("", $nam);
}

# print value of the given NV name preceded with the given indentation string
sub NVprint
{
    # collect args
    my $indent = shift;
    my $nam = shift;
    my $typ = &getType ($nam);

    #print " > $indent $nam $typ <";
    # find starting addr and size
    my $addr = $addr{$nam};
    $addr or die "$nam not defined\n";
    $eeprom[$addr-1] == $COOKIE or ($verbose and printf "%sNo cookie for $nam\n", $indent);
    my $size = $size{$nam};
    print "$indent";

    if ($typ eq "s") {
        my $str = pack ("C*", @eeprom[$addr .. $addr+$size-1]);
        $str =~ s/[^[:ascii:]]//g;      # remove all non-ascii
        $str =~ s/\0.*//g;              # remove everything after first binary 0 even if more 0's
        $str =~ s/ *$//g;               # remove all trailing blanks
        $str =~ s/\n//g;                # remove new lines
        print "$str\n";

    } elsif ($typ eq "i") {

        # decimal
        my $template = $size == 1 ? "c" : $size == 2 ? "s" : "i";
        my $value = unpack ($template, pack ("C$size", @eeprom[$addr .. $addr+$size-1]));
        my $unsigned = $value & ((1<<8*$size)-1);
        printf "%d s ", $value;
        printf "%u u ", $unsigned;
        printf "0x%0*X ", 2*$size, $unsigned;

        # binary
        print "0b";
        for (my $i = $size-1; $i >= 0; $i--) {
            for (my $j = 7; $j >= 0; $j--) {
                printf "%d", (($eeprom[$addr+$i] >> $j) & 1);
            }
            printf "_" unless ($i == 0);        # byte separator
        }

        # 2 bytes might be a RGB565 color?
        if ($size == 2) {
            my $word = ($eeprom[$addr+1] << 8) | $eeprom[$addr];
            my $r = int(255*((($word) & 0xF800) >> 11)/((1<<5)-1));
            my $g = int(255*((($word) & 0x07E0) >> 5)/((1<<6)-1));
            my $b = int(255*(($word) & 0x001F)/((1<<5)-1));
            printf " %d,%d,%d", $r, $g, $b;
        }

        print "\n";

    } elsif ($typ eq "RGB") {
        if ($size == 2) {
            my $S = unpack ("S", pack ("C2", @eeprom[$addr .. $addr+1]));
            printf "%d,%d,%d\n", ((($S) & 0xF800) >> 8), ((($S) & 0x07E0) >> 3), ((($S) & 0x001F) << 3);
        } else {
            die "Type RGB requires 2 byte values\n";
        }

    } elsif ($typ eq "f") {
        die "Can not use type f with $nam\n" unless ($size == 4);
        printf "%g\n", unpack ("f", pack ("C4", @eeprom[$addr .. $addr+3]));

    } else {
        die "Unknown type $typ\n";
    }
}

# show palette starting at the given addr
sub showPalette
{
    # get addr and confirm it is the starting cookie
    my $addr = shift;
    $eeprom[$addr] == 0x5A or die "No palette cookie at $addr: $eeprom[$addr]\n";
    $addr++;

    # scan rgb triples
    my $n_colors = 18;
    for (my $i = 0; $i < $n_colors; $i++) {
        printf "%2d %3d %3d %3d\n", $i, $eeprom[$addr], $eeprom[$addr+1], $eeprom[$addr+2]; 
        $addr += 3;
    }
}
