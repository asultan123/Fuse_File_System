#!/usr/bin/perl

$exp = 0;
$skip = 0;
while (<>) {
  if (/#ifdef NEXT_TERM/) {
    $skip = $exp = -1;
  }
  if (/#ifdef EXPORT/) {
    $skip = $exp = 1;
  } elsif (/#else/) {
    $skip = $exp = -1;
  } elsif (/#endif/) {
    $exp = 0;
    $skip = 1;
  } elsif (/#ifndef EXPORT/) {
    $skip = $exp = -1;
  }
  print $_ if $exp >= 0 && !$skip;
  $skip = 0;
}

    
