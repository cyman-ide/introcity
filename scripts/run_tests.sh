#!/bin/bash

CRED="\e[31m"
CGREEN="\e[32m"
CCYAN="\e[36m"
CRESET="\e[0m"

fails=0
echo > log.txt

for f in $@; do
    printf "\n----------------\nTEST $f\n----------------\n" >> log.txt
    printf "%-30b" "TEST ${CCYAN}$f${CRESET}"
    err=$(./$f 2>&1 >> log.txt)
    ok=$?
    printf -- "$err" >> log.txt
    if [ $ok -eq 0 ]; then
        printf "${CGREEN}OK"
    else
        printf "${CRED}FAILED${CRESET}\n"
        printf "    $err"
        fails=1
    fi
    printf "${CRESET}\n"
done

[ $fails -eq 0 ] \
  && printf "${CGREEN}All tests passed." \
  || printf "${CRED}Some tests failed."

printf "${CRESET}\n"
