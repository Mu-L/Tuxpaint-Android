#!/bin/sh

find_depends(){
  for dllpath in `ntldd $@ | grep mingw | awk '{print $3}' | sort | uniq`
  do
    dllname=`basename $dllpath`
    if ! grep -q $dllname dlllist; then
      echo $dllpath >> dlllist
      if [ ! -f $DESTDIR/$dllname ]; then
          echo -n .
	  cp $dllpath $DESTDIR/
	  find_depends $dllpath
      fi
    fi
  done
}

DESTDIR=$3
echo > dlllist

find_depends $1 $2
echo
