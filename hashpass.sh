#!/bin/bash
# Config
#segment-repeat=2
#line-repeat=2

# Setup
echo Testing, testing, 1 2 3
grub2-mkpasswd-pbkdf2
#response password=MyPass
#scan hash of your password is =pwdhash

echo Hash was: .${pwdhash}.
echo Arg1 was: .${arg1}.

