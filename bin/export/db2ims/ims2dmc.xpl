
#
#  send pick files generated by mkIMS to DMC
#    and track in dmcbull table
#
#

# J.Eakins 1/2008

require "getopts.pl" ;
use Datascope;
use rtmail;

  if ( ! &Getopts('f:m:s:vV') || @ARGV < 2 ) { 
	&usage;
  }

  $file		=  $ARGV[0];
  $database	=  $ARGV[1];

  print  STDERR "\ndatabase is:$database\n" if $opt_v;
  print  STDERR "\nbulletin file is:$file\n" if $opt_v;

  print  STDERR "\nCurrent time:\n " if $opt_v;
  $t = time() ;
  ($sec, $min, $hour, $mday, $mon, $year, $wday, $yday, $isdst) = localtime($t) ; 
  printf  STDERR "%s UTC\n", &strydtime($t) if $opt_v;  
  printf  STDERR " %2d/%02d/%04d (%03d) %2d:%02d:%02d.000 %s %s\n\n", $mon+1, $mday, $year+1900, $yday+1, $hour, $min, $sec, $ENV{'TZ'}, $isdst ? "Daylight Savings Time" : "Standard Time" if $opt_v;

# get variables set up with getopts
   if ($opt_s) {
	$subject  = "$opt_s";
   } else {
	$subject  = "PICKS";
   }

   if (!$opt_m) {
	print STDERR "Must specify mail recipients with -m\n"; 
	&usage;	
   } else {
	$mail_list = $opt_m ; 
   }

   if ($opt_f) {
	$from = $opt_f;
   } 

#   if ($opt_t) {
#	$msg = $opt_t ;
#   } else {
#	$msg = " " ;
#   }
	
#
# open up database and lookup tables
#
   @db 		= dbopen ( $database, "r+") ; 
   @dbdmcbull	= dblookup(@db,"","dmcbull","","") ;
#   $nrecs	= dbquery (@dbdmcbull,"dbRECORD_COUNT");
   if (!dbquery (@dbdmcbull,"dbRECORD_COUNT")) {
	print STDERR "No records in dmcbull table\n";
   } 

#
#  get proper path and file name
#

   $fullpath	 = abspath($file) ;
   ($dir,$afile,$suffix)	 = parsepath($file) ; 
   if ($suffix) {
       $dfile	= "$afile".".$suffix" ;
   } else {
       $dfile	= "$afile" ;
   }	
#
# open up file to be sent and get start/end times (ARGH!)
#

   open(BULL, "$file") || die "Can't open $file.\n";
   while (<BULL>) {
        print STDERR "Line is: $_\n" if ($opt_V) ;
        if (/^\d{4}/) {                    # get date if line begins with 4 digit number
	    $evdate       = substr($_, 0,22);
	    push(@evdates,$evdate);
	    $nev++ ;
            next; 
        } elsif (/IRIS FDSNNETWORKCODE/) {
	    # this works because of the hokey format requirements
	    $nph++ ;
        } else {
            next;
	}
   }

   close(BULL);

   $start 	= $evdates[0];
   $end		= $evdates[$#evdates]; 

   @bull_record = ();
   $auth	= "ims2dmc:".getpwuid($<) ;

   push(@bull_record,    
			"data_start",	$start ,
			"data_end", 	$end ,
			"time",		&strydtime($t),
			"nev",		$nev,
			"nph",		$nph,
			"dir",		$dir,
			"dfile",	$dfile,
			"auth",		$auth
	) ;
		
   eval { dbaddv(@dbdmcbull,@bull_record) } ;
        if ($@) {
              warn $@;
              print STDERR "Problem adding record:  $start, $end, $time matches.\n"  ; 
              print STDERR "No record added!\n"; 
        } else {
              print STDERR "Added record to database: $database \n"  ; 
	}	

dbclose @db;

#
# now send file via email to DMC (or others)
#

$rtmail = "rtmail -s '$subject' $mail_list < $file" ; 

system ( $rtmail ) ;

if ($?) {
    print STDERR "Attempt to send mail failed: $?  \n";
} else {
    print STDERR "Sending phase pick mail to: $mail_list \n";
}	
	
sub usage{
	print STDERR <<ENDIT ;
\nUSAGE: \t$0 [-v] [-s subject] -m email1,email2,... file database 

ENDIT
exit;
}
