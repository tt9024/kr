#!/usr/bin/perl

($CONFFILE) = @ARGV;

$commission = 0.00004; 

$action = `grep action $CONFFILE | grep -v noaction | perl -ane 'chomp;s/.*=//;s/\s//g;print'`;
chomp($action);
$probthreshold = `grep probthreshold $CONFFILE | perl -ane 'chomp;s/.*=//;s/\s//g;print'`;
chomp($probthreshold);

$c=0;
$r=0;
$g=0;
while(<STDIN>)
{
    chomp;
    @t = split(/\s+/, $_);
    if($t[22] eq $action && $t[23] > $probthreshold)
    {
	$c++; 
	if($t[0] eq $action)
	{
	    $r++;
	} 
	
	$g += $t[21];
	$g2 = $g - $c*$commission; 
	print "$r/$c ", sprintf("%.4f", $r/$c)," $t[21] $g $g2\n";
    }
} 

if($c == 0)
{
    print "$r/$c $g\n";
}
else
{ 
    $g2 = $g - $c*$commission; 
    print "$r/$c ", sprintf("%.4f", $r/$c)," $g $g2\n";
}
